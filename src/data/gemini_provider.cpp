#include "data/gemini_provider.h"

#include <cstdlib>
#include <sstream>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <sys/wait.h>

#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GeminiProvider::GeminiProvider(const std::string& api_key, const std::string& model)
    : api_key_(api_key), model_(model) {}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

LlmCapabilities GeminiProvider::capabilities() const {
    LlmCapabilities caps;
    caps.structured_output = true;    // JSON mode supported
    caps.search_grounding = true;     // Google Search as a tool
    caps.streaming = false;           // Not implemented yet
    caps.function_calling = true;     // Gemini supports function calling
    caps.context_window = 1048576;    // 1M tokens for Gemini 2.0 Flash
    return caps;
}

// ---------------------------------------------------------------------------
// Curl subprocess — all HTTPS goes through curl to avoid httplib SSL segfault
// ---------------------------------------------------------------------------

static std::string read_all_fd(int fd) {
    std::string result;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        result.append(buf, static_cast<size_t>(n));
    }
    return result;
}

static int wait_child(pid_t pid) {
    int wstatus = 0;
    for (;;) {
        pid_t w = waitpid(pid, &wstatus, 0);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;
    }
    if (WIFEXITED(wstatus)) return WEXITSTATUS(wstatus);
    return -1;
}

GeminiProvider::CurlResult GeminiProvider::run_curl(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    int timeout_sec) const
{
    CurlResult cr;

    std::string url = "https://generativelanguage.googleapis.com" + path;

    // Create pipes for stdout and stderr
    int pipe_out[2], pipe_err[2];
    if (pipe(pipe_out) < 0) {
        cr.error = std::string("pipe() failed: ") + strerror(errno);
        return cr;
    }
    if (pipe(pipe_err) < 0) {
        ::close(pipe_out[0]); ::close(pipe_out[1]);
        cr.error = std::string("pipe() failed: ") + strerror(errno);
        return cr;
    }

    pid_t pid = fork();
    if (pid < 0) {
        ::close(pipe_out[0]); ::close(pipe_out[1]);
        ::close(pipe_err[0]); ::close(pipe_err[1]);
        cr.error = std::string("fork() failed: ") + strerror(errno);
        return cr;
    }

    if (pid == 0) {
        // ---- Child process ----
        ::close(pipe_out[0]);
        dup2(pipe_out[1], STDOUT_FILENO);
        ::close(pipe_out[1]);

        ::close(pipe_err[0]);
        dup2(pipe_err[1], STDERR_FILENO);
        ::close(pipe_err[1]);

        // Build timeout string
        std::string timeout_str = std::to_string(timeout_sec);

        if (method == "GET" || body.empty()) {
            execlp("curl", "curl",
                   "-sS",
                   "--max-time", timeout_str.c_str(),
                   url.c_str(),
                   nullptr);
        } else {
            execlp("curl", "curl",
                   "-sS",
                   "--max-time", timeout_str.c_str(),
                   "-X", "POST",
                   "-H", "Content-Type: application/json",
                   "-d", body.c_str(),
                   url.c_str(),
                   nullptr);
        }

        // exec failed
        _exit(127);
    }

    // ---- Parent process ----
    ::close(pipe_out[1]);
    ::close(pipe_err[1]);

    cr.stdout_data = read_all_fd(pipe_out[0]);
    ::close(pipe_out[0]);

    cr.stderr_data = read_all_fd(pipe_err[0]);
    ::close(pipe_err[0]);

    cr.exit_code = wait_child(pid);
    return cr;
}

// ---------------------------------------------------------------------------
// Connection test — uses curl subprocess for HTTPS (no httplib SSL)
// ---------------------------------------------------------------------------

std::string GeminiProvider::test_connection() {
    if (api_key_.empty()) {
        return "No Gemini API key configured. Set the GEMINI_API_KEY environment variable "
               "or configure ai.fallback_api_key_env in data/ai_config.json. "
               "Get a free key at https://aistudio.google.com/apikey";
    }

    // List models to verify the API key works
    std::string path = "/v1beta/models?key=" + api_key_;
    auto cr = run_curl("GET", path, "", 15);

    if (!cr.error.empty()) {
        return "Cannot test Gemini API: " + cr.error;
    }

    if (cr.exit_code != 0) {
        std::string msg = "Cannot connect to Gemini API";
        if (cr.exit_code == 28) {
            msg += " — connection timed out";
        } else if (!cr.stderr_data.empty()) {
            msg += ": " + cr.stderr_data;
        }
        msg += " — check your internet connection";
        return msg;
    }

    if (cr.stdout_data.empty()) {
        return "Gemini API returned empty response";
    }

    // Parse the response to check for errors
    try {
        auto j = json::parse(cr.stdout_data);

        // Check for API error response
        if (j.contains("error")) {
            int code = 0;
            std::string message;
            if (j["error"].contains("code")) code = j["error"]["code"].get<int>();
            if (j["error"].contains("message")) message = j["error"]["message"].get<std::string>();

            if (code == 400 || code == 403) {
                return "Gemini API key is invalid or expired. "
                       "Get a new key at https://aistudio.google.com/apikey";
            }
            if (code == 429) {
                if (message.find("limit: 0") != std::string::npos) {
                    return "Gemini quota: your API key has zero free tier allocation. "
                           "Create a new key at https://aistudio.google.com/apikey "
                           "using a personal Google account (not Workspace), "
                           "and select 'Create API key in new project'.";
                }
                return "Gemini rate limited — retry in a moment. " + message;
            }
            if (code != 0) {
                return "Gemini API error " + std::to_string(code) + ": " + message;
            }
        }

        // Verify our specific model exists
        if (j.contains("models") && j["models"].is_array()) {
            bool found = false;
            for (auto& m : j["models"]) {
                std::string name = m.value("name", "");
                // Model names look like "models/gemini-2.0-flash"
                if (name == "models/" + model_ || name == model_) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return "Model '" + model_ + "' not found in Gemini API. "
                       "Available models listed at https://ai.google.dev/gemini-api/docs/models";
            }
        }

    } catch (const json::exception&) {
        // Can't parse but we got a response — API key likely works
    }

    return "";  // Success
}

// ---------------------------------------------------------------------------
// Query — uses curl subprocess for HTTPS POST to generateContent
// ---------------------------------------------------------------------------

LlmResponse GeminiProvider::query(const LlmRequest& req) {
    LlmResponse resp;
    resp.model_used = model_;

    if (api_key_.empty()) {
        resp.error = "No Gemini API key configured";
        return resp;
    }

    try {
        // Build request body per Gemini API spec
        json body;

        // System instruction (separate field in Gemini API)
        if (!req.system_prompt.empty()) {
            body["system_instruction"] = {
                {"parts", json::array({{{"text", req.system_prompt}}})}
            };
        }

        // Contents (messages)
        json contents = json::array();
        contents.push_back({
            {"role", "user"},
            {"parts", json::array({{{"text", req.user_prompt}}})},
        });
        body["contents"] = contents;

        // Generation config
        json gen_config;
        gen_config["temperature"] = req.temperature;
        if (req.max_tokens > 0) {
            gen_config["maxOutputTokens"] = req.max_tokens;
        }

        // Request JSON output if schema hint provided.
        // NOTE: Gemini does not support responseMimeType with tools (search
        // grounding), so only enable JSON mode when search is off. When search
        // is on, the prompt already asks for JSON — the model will comply.
        if (!req.response_schema.empty() && !req.enable_search) {
            gen_config["responseMimeType"] = "application/json";
        }

        body["generationConfig"] = gen_config;

        // Enable Google Search grounding if requested
        if (req.enable_search) {
            body["tools"] = json::array({
                {{"google_search", json::object()}}
            });
        }

        // POST to generateContent endpoint
        std::string path = "/v1beta/models/" + model_ + ":generateContent?key=" + api_key_;
        auto cr = run_curl("POST", path, body.dump(), 120);

        if (!cr.error.empty()) {
            resp.error = "Gemini API call failed: " + cr.error;
            return resp;
        }

        if (cr.exit_code != 0) {
            resp.error = "Failed to connect to Gemini API";
            if (cr.exit_code == 28) {
                resp.error += " — request timed out";
            } else if (!cr.stderr_data.empty()) {
                resp.error += ": " + cr.stderr_data;
            }
            return resp;
        }

        if (cr.stdout_data.empty()) {
            resp.error = "Empty response from Gemini API";
            if (!cr.stderr_data.empty()) resp.error += ": " + cr.stderr_data;
            return resp;
        }

        // Parse response
        auto j = json::parse(cr.stdout_data);

        // Check for API error in response body
        if (j.contains("error")) {
            int code = 0;
            std::string message;
            if (j["error"].contains("code")) code = j["error"]["code"].get<int>();
            if (j["error"].contains("message")) message = j["error"]["message"].get<std::string>();

            if (code == 429 && message.find("limit: 0") != std::string::npos) {
                resp.error = "Gemini quota: your API key has zero free tier allocation. "
                             "Create a new key at https://aistudio.google.com/apikey "
                             "using a personal Google account (not Workspace), "
                             "and select 'Create API key in new project'.";
            } else if (code == 429) {
                resp.error = "Gemini rate limited — retry in a moment. " + message;
            } else {
                resp.error = "Gemini API error " + std::to_string(code) + ": " + message;
            }
            return resp;
        }

        // Extract text from candidates[0].content.parts[0].text
        if (j.contains("candidates") && !j["candidates"].empty()) {
            auto& candidate = j["candidates"][0];
            if (candidate.contains("content") && candidate["content"].contains("parts")) {
                auto& parts = candidate["content"]["parts"];
                if (!parts.empty() && parts[0].contains("text")) {
                    resp.content = parts[0]["text"].get<std::string>();
                }
            }

            // Check for grounding metadata (search sources)
            if (candidate.contains("groundingMetadata")) {
                resp.search_grounded = true;
                auto& gm = candidate["groundingMetadata"];
                if (gm.contains("groundingChunks") && gm["groundingChunks"].is_array()) {
                    for (auto& chunk : gm["groundingChunks"]) {
                        if (chunk.contains("web") && chunk["web"].contains("uri")) {
                            resp.sources.push_back(chunk["web"]["uri"].get<std::string>());
                        }
                    }
                }
            }
        }

        // Extract token counts
        if (j.contains("usageMetadata")) {
            resp.input_tokens = j["usageMetadata"].value("promptTokenCount", 0);
            resp.output_tokens = j["usageMetadata"].value("candidatesTokenCount", 0);
        }

    } catch (const json::exception& e) {
        resp.error = std::string("Failed to parse Gemini response: ") + e.what();
    } catch (const std::exception& e) {
        resp.error = std::string("Gemini query error: ") + e.what();
    }

    return resp;
}

} // namespace stfc
