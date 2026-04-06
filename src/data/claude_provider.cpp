#include "data/claude_provider.h"

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

ClaudeProvider::ClaudeProvider(const std::string& api_key, const std::string& model)
    : api_key_(api_key), model_(model) {}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

LlmCapabilities ClaudeProvider::capabilities() const {
    LlmCapabilities caps;
    caps.structured_output = true;    // Claude can produce JSON reliably
    caps.search_grounding = false;    // No web search
    caps.streaming = true;            // SSE streaming supported
    caps.function_calling = true;     // Claude supports tool use
    caps.context_window = 200000;     // 200K tokens for Claude Sonnet/Opus 4
    return caps;
}

// ---------------------------------------------------------------------------
// Curl helpers — read fd, wait for child
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

// ---------------------------------------------------------------------------
// Curl subprocess — non-streaming (wait for full response)
// ---------------------------------------------------------------------------

ClaudeProvider::CurlResult ClaudeProvider::run_curl(
    const std::string& body, int timeout_sec) const
{
    CurlResult cr;

    std::string url = "https://api.anthropic.com/v1/messages";
    std::string timeout_str = std::to_string(timeout_sec);
    std::string auth_header = "x-api-key: " + api_key_;

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

        execlp("curl", "curl",
               "-sS",
               "--max-time", timeout_str.c_str(),
               "-X", "POST",
               "-H", "Content-Type: application/json",
               "-H", auth_header.c_str(),
               "-H", "anthropic-version: 2023-06-01",
               "-d", body.c_str(),
               url.c_str(),
               nullptr);

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
// Curl subprocess — streaming (returns pipe fds for line-by-line reading)
// ---------------------------------------------------------------------------

ClaudeProvider::CurlStreamResult ClaudeProvider::run_curl_stream(
    const std::string& body) const
{
    CurlStreamResult cr;

    std::string url = "https://api.anthropic.com/v1/messages";
    std::string auth_header = "x-api-key: " + api_key_;

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

        // No --max-time for streaming — runs until Claude finishes
        // --no-buffer ensures SSE events arrive immediately
        execlp("curl", "curl",
               "-sS",
               "--no-buffer",
               "-X", "POST",
               "-H", "Content-Type: application/json",
               "-H", auth_header.c_str(),
               "-H", "anthropic-version: 2023-06-01",
               "-d", body.c_str(),
               url.c_str(),
               nullptr);

        // exec failed
        _exit(127);
    }

    // ---- Parent process ----
    ::close(pipe_out[1]);
    ::close(pipe_err[1]);

    cr.stdout_fd = pipe_out[0];
    cr.stderr_fd = pipe_err[0];
    cr.pid = pid;

    return cr;
}

// ---------------------------------------------------------------------------
// Connection test — quick request to verify API key works
// ---------------------------------------------------------------------------

std::string ClaudeProvider::test_connection() {
    if (api_key_.empty()) {
        return "No Claude API key configured. Set the ANTHROPIC_API_KEY environment variable "
               "or configure api_key_env in data/ai_config.json. "
               "Get a key at https://console.anthropic.com/";
    }

    // Send a minimal request to verify the key works
    json body;
    body["model"] = model_;
    body["max_tokens"] = 10;
    body["messages"] = json::array({
        {{"role", "user"}, {"content", "ok"}}
    });

    auto cr = run_curl(body.dump(), 15);

    if (!cr.error.empty()) {
        return "Cannot test Claude API: " + cr.error;
    }

    if (cr.exit_code != 0) {
        std::string msg = "Cannot connect to Claude API";
        if (cr.exit_code == 28) {
            msg += " — connection timed out";
        } else if (!cr.stderr_data.empty()) {
            msg += ": " + cr.stderr_data;
        }
        msg += " — check your internet connection";
        return msg;
    }

    if (cr.stdout_data.empty()) {
        return "Claude API returned empty response";
    }

    // Parse the response to check for errors
    try {
        auto j = json::parse(cr.stdout_data);

        if (j.contains("type") && j["type"] == "error") {
            std::string err_type = j.value("error", json::object()).value("type", "unknown");
            std::string err_msg = j.value("error", json::object()).value("message", "");

            if (err_type == "authentication_error") {
                return "Claude API key is invalid. Check your ANTHROPIC_API_KEY. "
                       "Get a key at https://console.anthropic.com/";
            }
            if (err_type == "not_found_error") {
                return "Model '" + model_ + "' not found. "
                       "Available models: claude-sonnet-4-20250514, claude-opus-4-20250514";
            }
            if (err_type == "rate_limit_error") {
                // Rate limited but key works — that's a successful connection test
                return "";
            }
            if (err_type == "overloaded_error") {
                // API is overloaded but key works
                return "";
            }
            return "Claude API error (" + err_type + "): " + err_msg;
        }

        // Success — we got a valid response
        if (j.contains("content") && j["content"].is_array() && !j["content"].empty()) {
            return "";  // All good
        }

    } catch (const json::exception&) {
        // Can't parse but we got a response
    }

    return "";  // Assume success if we got any response
}

// ---------------------------------------------------------------------------
// Query (non-streaming) — uses curl subprocess, waits until complete
// ---------------------------------------------------------------------------

LlmResponse ClaudeProvider::query(const LlmRequest& req) {
    LlmResponse resp;
    resp.model_used = model_;

    if (api_key_.empty()) {
        resp.error = "No Claude API key configured";
        return resp;
    }

    try {
        // Build request body per Anthropic API spec
        json body;
        body["model"] = model_;
        body["max_tokens"] = req.max_tokens > 0 ? req.max_tokens : 4096;

        // System prompt is a top-level field in Claude API
        if (!req.system_prompt.empty()) {
            body["system"] = req.system_prompt;
        }

        // Messages array
        json messages = json::array();
        messages.push_back({
            {"role", "user"},
            {"content", req.user_prompt}
        });
        body["messages"] = messages;

        // Temperature
        body["temperature"] = req.temperature;

        // POST to messages endpoint
        auto cr = run_curl(body.dump(), 120);

        if (!cr.error.empty()) {
            resp.error = "Claude API call failed: " + cr.error;
            return resp;
        }

        if (cr.exit_code != 0) {
            resp.error = "Failed to connect to Claude API";
            if (cr.exit_code == 28) {
                resp.error += " — request timed out";
            } else if (!cr.stderr_data.empty()) {
                resp.error += ": " + cr.stderr_data;
            }
            return resp;
        }

        if (cr.stdout_data.empty()) {
            resp.error = "Empty response from Claude API";
            if (!cr.stderr_data.empty()) resp.error += ": " + cr.stderr_data;
            return resp;
        }

        // Parse response
        auto j = json::parse(cr.stdout_data);

        // Check for API error in response body
        if (j.contains("type") && j["type"] == "error") {
            std::string err_type = j.value("error", json::object()).value("type", "unknown");
            std::string err_msg = j.value("error", json::object()).value("message", "");

            if (err_type == "rate_limit_error") {
                resp.error = "Claude rate limited — retry in a moment. " + err_msg;
            } else if (err_type == "overloaded_error") {
                resp.error = "Claude API overloaded — retry in a moment. " + err_msg;
            } else {
                resp.error = "Claude API error (" + err_type + "): " + err_msg;
            }
            return resp;
        }

        // Extract text from content[0].text
        if (j.contains("content") && j["content"].is_array()) {
            for (auto& block : j["content"]) {
                if (block.value("type", "") == "text" && block.contains("text")) {
                    resp.content += block["text"].get<std::string>();
                }
            }
        }

        // Extract model used
        if (j.contains("model")) {
            resp.model_used = j["model"].get<std::string>();
        }

        // Extract token counts
        if (j.contains("usage")) {
            resp.input_tokens = j["usage"].value("input_tokens", 0);
            resp.output_tokens = j["usage"].value("output_tokens", 0);
        }

    } catch (const json::exception& e) {
        resp.error = std::string("Failed to parse Claude response: ") + e.what();
    } catch (const std::exception& e) {
        resp.error = std::string("Claude query error: ") + e.what();
    }

    return resp;
}

// ---------------------------------------------------------------------------
// Query (streaming) — uses curl subprocess, reads SSE events line-by-line,
// streams text to callback.
//
// Claude streaming API returns Server-Sent Events (SSE):
//   event: content_block_delta
//   data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"..."}}
//
//   event: message_delta
//   data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":N}}
//
//   event: message_stop
//   data: {"type":"message_stop"}
// ---------------------------------------------------------------------------

LlmResponse ClaudeProvider::query_stream(const LlmRequest& req, LlmStreamCallback cb) {
    LlmResponse resp;
    resp.model_used = model_;

    if (!cb) {
        // No callback, fall back to non-streaming
        return query(req);
    }

    if (api_key_.empty()) {
        resp.error = "No Claude API key configured";
        return resp;
    }

    try {
        // Build request body — same as non-streaming but with "stream": true
        json body;
        body["model"] = model_;
        body["max_tokens"] = req.max_tokens > 0 ? req.max_tokens : 4096;
        body["stream"] = true;

        if (!req.system_prompt.empty()) {
            body["system"] = req.system_prompt;
        }

        json messages = json::array();
        messages.push_back({
            {"role", "user"},
            {"content", req.user_prompt}
        });
        body["messages"] = messages;
        body["temperature"] = req.temperature;

        auto cr = run_curl_stream(body.dump());

        if (!cr.error.empty()) {
            resp.error = cr.error;
            return resp;
        }

        // Read stdout line-by-line. Claude streams SSE events:
        //   event: <event_type>\n
        //   data: <json>\n
        //   \n
        //
        // We look for "data: " lines and parse the JSON to extract text deltas.
        std::string full_content;
        std::string line_buf;
        char buf[4096];

        for (;;) {
            ssize_t n = ::read(cr.stdout_fd, buf, sizeof(buf));
            if (n <= 0) break;  // EOF or error — curl is done

            line_buf.append(buf, static_cast<size_t>(n));

            // Process complete lines
            size_t pos = 0;
            size_t nl;
            while ((nl = line_buf.find('\n', pos)) != std::string::npos) {
                std::string line = line_buf.substr(pos, nl - pos);
                pos = nl + 1;

                // Strip trailing \r
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;

                // Only process "data: " lines
                if (line.rfind("data: ", 0) != 0) continue;
                std::string data = line.substr(6);

                if (data.empty() || data == "[DONE]") continue;

                try {
                    auto j = json::parse(data);
                    std::string type = j.value("type", "");

                    // Check for error event
                    if (type == "error") {
                        std::string err_type = j.value("error", json::object()).value("type", "unknown");
                        std::string err_msg = j.value("error", json::object()).value("message", "");
                        resp.error = "Claude streaming error (" + err_type + "): " + err_msg;
                        continue;
                    }

                    // Extract text deltas
                    if (type == "content_block_delta") {
                        if (j.contains("delta") && j["delta"].value("type", "") == "text_delta") {
                            std::string text = j["delta"].value("text", "");
                            if (!text.empty()) {
                                full_content += text;
                                cb(text);
                            }
                        }
                    }

                    // Extract metadata from message_start
                    if (type == "message_start" && j.contains("message")) {
                        auto& msg = j["message"];
                        if (msg.contains("model")) {
                            resp.model_used = msg["model"].get<std::string>();
                        }
                        if (msg.contains("usage")) {
                            resp.input_tokens = msg["usage"].value("input_tokens", 0);
                        }
                    }

                    // Extract final usage from message_delta
                    if (type == "message_delta") {
                        if (j.contains("usage")) {
                            resp.output_tokens = j["usage"].value("output_tokens", 0);
                        }
                    }

                } catch (const json::exception&) {
                    // Partial or malformed JSON line, skip
                }
            }

            // Keep the remaining partial line for next read
            if (pos > 0) {
                line_buf = line_buf.substr(pos);
            }
        }

        ::close(cr.stdout_fd);

        // Read stderr for diagnostics
        std::string err_output = read_all_fd(cr.stderr_fd);
        ::close(cr.stderr_fd);

        int exit_code = wait_child(cr.pid);

        if (exit_code != 0 && full_content.empty()) {
            resp.error = "curl failed (exit " + std::to_string(exit_code) + ")";
            if (!err_output.empty()) resp.error += ": " + err_output;
            return resp;
        }

        // Check if the full response is actually a JSON error (non-streaming error)
        // This happens when Claude returns an error before starting to stream
        if (full_content.empty() && !line_buf.empty()) {
            try {
                auto j = json::parse(line_buf);
                if (j.contains("type") && j["type"] == "error") {
                    std::string err_type = j.value("error", json::object()).value("type", "unknown");
                    std::string err_msg = j.value("error", json::object()).value("message", "");
                    resp.error = "Claude API error (" + err_type + "): " + err_msg;
                    return resp;
                }
            } catch (...) {}
        }

        if (full_content.empty() && resp.error.empty()) {
            resp.error = "Empty response from Claude";
            if (!err_output.empty()) resp.error += ": " + err_output;
        }

        resp.content = full_content;

    } catch (const std::exception& e) {
        resp.error = std::string("Claude streaming error: ") + e.what();
    }

    return resp;
}

} // namespace stfc
