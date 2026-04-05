#include "data/ollama_provider.h"

#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <vector>
#include <array>

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OllamaProvider::OllamaProvider(const std::string& endpoint, const std::string& model)
    : model_(model) {
    parse_endpoint(endpoint);
}

void OllamaProvider::parse_endpoint(const std::string& endpoint) {
    // Parse "http://host:port" or "host:port" or just "host"
    std::string s = endpoint;

    // Strip protocol prefix
    if (s.rfind("http://", 0) == 0) s = s.substr(7);
    else if (s.rfind("https://", 0) == 0) s = s.substr(8);

    // Strip trailing slash
    if (!s.empty() && s.back() == '/') s.pop_back();

    // Split host:port
    auto colon = s.rfind(':');
    if (colon != std::string::npos) {
        host_ = s.substr(0, colon);
        try {
            port_ = std::stoi(s.substr(colon + 1));
        } catch (...) {
            port_ = 11434;
        }
    } else {
        host_ = s.empty() ? "127.0.0.1" : s;
        port_ = 11434;
    }

    // Force IPv4 loopback — httplib can resolve "localhost" to ::1 (IPv6)
    // which fails when the SSH tunnel only listens on 127.0.0.1
    if (host_ == "localhost") {
        host_ = "127.0.0.1";
    }
}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

LlmCapabilities OllamaProvider::capabilities() const {
    LlmCapabilities caps;
    caps.structured_output = true;   // Ollama supports format:"json"
    caps.search_grounding = false;   // Local model, no web access
    caps.streaming = true;           // Ollama supports streaming
    caps.function_calling = false;   // Not reliably across all models
    caps.context_window = 8192;      // Varies by model, 8K is a safe default
    return caps;
}

// ---------------------------------------------------------------------------
// Connection test — uses httplib for a quick health check (short timeout OK)
// ---------------------------------------------------------------------------

std::string OllamaProvider::test_connection() {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(5);

        // Check if Ollama is running
        auto res = cli.Get("/api/tags");
        if (!res) {
            auto err = res.error();
            std::string err_detail;
            switch (err) {
                case httplib::Error::Connection:     err_detail = "connection refused"; break;
                case httplib::Error::Read:            err_detail = "read timeout"; break;
                case httplib::Error::Write:           err_detail = "write error"; break;
                case httplib::Error::ConnectionTimeout: err_detail = "connection timeout"; break;
                default:                             err_detail = "error code " + std::to_string(static_cast<int>(err)); break;
            }
            return "Cannot connect to Ollama at " + host_ + ":" + std::to_string(port_) +
                   " (" + err_detail + ") — is Ollama running? (ollama serve)";
        }
        if (res->status != 200) {
            return "Ollama returned HTTP " + std::to_string(res->status);
        }

        // Check if the requested model is available
        try {
            auto j = json::parse(res->body);
            if (j.contains("models") && j["models"].is_array()) {
                bool found = false;
                for (auto& m : j["models"]) {
                    std::string name = m.value("name", "");
                    // Ollama model names can be "gemma2:9b" or "gemma2:9b-instruct-q4_0"
                    // Match on prefix
                    if (name == model_ || name.rfind(model_, 0) == 0) {
                        found = true;
                        break;
                    }
                    // Also check without tag (e.g. "gemma2" matches "gemma2:9b")
                    auto colon = name.find(':');
                    if (colon != std::string::npos) {
                        std::string base = name.substr(0, colon);
                        auto model_colon = model_.find(':');
                        std::string model_base = model_colon != std::string::npos
                            ? model_.substr(0, model_colon) : model_;
                        if (base == model_base) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    return "Model '" + model_ + "' not found in Ollama. "
                           "Pull it with: ollama pull " + model_;
                }
            }
        } catch (const json::exception& e) {
            // If we can't parse the model list, at least Ollama is responding
            // The model check will fail at query time if it's missing
        }

        return "";  // Success
    } catch (const std::exception& e) {
        return std::string("Ollama connection error: ") + e.what();
    }
}

// ---------------------------------------------------------------------------
// Helper: build JSON request body for Ollama /api/chat
// ---------------------------------------------------------------------------

json OllamaProvider::build_request_body(const LlmRequest& req, bool stream) const {
    json messages = json::array();
    if (!req.system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", req.system_prompt}});
    }
    messages.push_back({{"role", "user"}, {"content", req.user_prompt}});

    json body;
    body["model"] = model_;
    body["messages"] = messages;
    body["stream"] = stream;

    // Request structured JSON output if a schema hint is provided
    if (!req.response_schema.empty()) {
        body["format"] = "json";
    }

    // Model options
    json options;
    options["temperature"] = req.temperature;
    if (req.max_tokens > 0) {
        options["num_predict"] = req.max_tokens;
    }
    // Increase context window from Ollama's default 4096 to 8192.
    // The prompt (system + user + officer data) can exceed 4K tokens,
    // leaving no room for the model to generate a response.
    options["num_ctx"] = 8192;
    body["options"] = options;

    return body;
}

// ---------------------------------------------------------------------------
// Helper: run curl as a child process, read stdout via pipe
//
// This avoids httplib's hard read timeout. curl will block until the full
// response arrives — whether that takes 30 seconds or 30 minutes. The worker
// thread simply reads until EOF, so it runs until Ollama finishes.
// ---------------------------------------------------------------------------

OllamaProvider::CurlResult OllamaProvider::run_curl(
    const std::string& json_body, bool stream) const
{
    CurlResult cr;

    std::string url = "http://" + host_ + ":" + std::to_string(port_) + "/api/chat";

    // Create pipe for stdout
    int pipe_out[2];
    if (pipe(pipe_out) < 0) {
        cr.error = std::string("pipe() failed: ") + strerror(errno);
        return cr;
    }

    // Create pipe for stderr (to capture curl errors)
    int pipe_err[2];
    if (pipe(pipe_err) < 0) {
        ::close(pipe_out[0]);
        ::close(pipe_out[1]);
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
        // Redirect stdout to pipe
        ::close(pipe_out[0]);
        dup2(pipe_out[1], STDOUT_FILENO);
        ::close(pipe_out[1]);

        // Redirect stderr to pipe
        ::close(pipe_err[0]);
        dup2(pipe_err[1], STDERR_FILENO);
        ::close(pipe_err[1]);

        // exec curl with no timeout (--max-time 0 = no limit)
        // --no-buffer ensures streaming output isn't buffered
        // -s = silent (no progress bar)
        // -S = show errors even in silent mode
        execlp("curl", "curl",
               "-sS",
               "--no-buffer",
               "-X", "POST",
               "-H", "Content-Type: application/json",
               "-d", json_body.c_str(),
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
// Helper: read all data from an fd until EOF
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

// ---------------------------------------------------------------------------
// Helper: wait for child process and get exit code
// ---------------------------------------------------------------------------

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
// Query (non-streaming) — uses curl subprocess, waits until complete
// ---------------------------------------------------------------------------

LlmResponse OllamaProvider::query(const LlmRequest& req) {
    LlmResponse resp;
    resp.model_used = model_;

    try {
        json body = build_request_body(req, false);
        auto cr = run_curl(body.dump(), false);

        if (!cr.error.empty()) {
            resp.error = cr.error;
            return resp;
        }

        // Read all stdout (the full JSON response)
        std::string output = read_all_fd(cr.stdout_fd);
        ::close(cr.stdout_fd);

        // Read stderr for error diagnostics
        std::string err_output = read_all_fd(cr.stderr_fd);
        ::close(cr.stderr_fd);

        int exit_code = wait_child(cr.pid);

        if (exit_code != 0) {
            resp.error = "curl failed (exit " + std::to_string(exit_code) + ")";
            if (!err_output.empty()) {
                resp.error += ": " + err_output;
            }
            return resp;
        }

        if (output.empty()) {
            resp.error = "Empty response from Ollama";
            if (!err_output.empty()) resp.error += ": " + err_output;
            return resp;
        }

        // Parse response
        try {
            auto j = json::parse(output);

            // Check for Ollama error response
            if (j.contains("error")) {
                resp.error = "Ollama error: " + j["error"].get<std::string>();
                return resp;
            }

            // Extract message content
            if (j.contains("message") && j["message"].contains("content")) {
                resp.content = j["message"]["content"].get<std::string>();
            }

            // Extract token counts
            resp.input_tokens = j.value("prompt_eval_count", 0);
            resp.output_tokens = j.value("eval_count", 0);

            // Extract model name (may differ from requested if aliased)
            if (j.contains("model")) {
                resp.model_used = j["model"].get<std::string>();
            }

        } catch (const json::exception& e) {
            resp.error = std::string("Failed to parse Ollama response: ") + e.what();
            resp.content = output;  // Preserve raw response for debugging
        }

    } catch (const std::exception& e) {
        resp.error = std::string("Ollama query error: ") + e.what();
    }

    return resp;
}

// ---------------------------------------------------------------------------
// Query (streaming) — uses curl subprocess, reads line-by-line, streams
// tokens to callback. Runs until Ollama is done — no timeout.
// ---------------------------------------------------------------------------

LlmResponse OllamaProvider::query_stream(const LlmRequest& req, LlmStreamCallback cb) {
    LlmResponse resp;
    resp.model_used = model_;

    if (!cb) {
        // No callback, fall back to non-streaming
        return query(req);
    }

    try {
        json body = build_request_body(req, true);
        auto cr = run_curl(body.dump(), true);

        if (!cr.error.empty()) {
            resp.error = cr.error;
            return resp;
        }

        // Read stdout line-by-line. Ollama streams one JSON object per line.
        // Each line contains a token chunk. We parse and forward to the callback.
        // This loop blocks until curl exits (i.e., Ollama sends "done":true
        // and closes the connection). No artificial timeout.
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

                if (line.empty()) continue;

                // Strip trailing \r if present
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;

                try {
                    auto j = json::parse(line);

                    // Check for error
                    if (j.contains("error")) {
                        resp.error = "Ollama error: " + j["error"].get<std::string>();
                        // Keep reading to drain the pipe
                        continue;
                    }

                    if (j.contains("message") && j["message"].contains("content")) {
                        std::string text = j["message"]["content"].get<std::string>();
                        full_content += text;
                        cb(text);
                    }

                    // Check if this is the final message
                    if (j.value("done", false)) {
                        resp.input_tokens = j.value("prompt_eval_count", 0);
                        resp.output_tokens = j.value("eval_count", 0);
                        if (j.contains("model")) {
                            resp.model_used = j["model"].get<std::string>();
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

        if (full_content.empty() && resp.error.empty()) {
            resp.error = "Empty response from Ollama";
            if (!err_output.empty()) resp.error += ": " + err_output;
        }

        resp.content = full_content;

    } catch (const std::exception& e) {
        resp.error = std::string("Ollama streaming error: ") + e.what();
    }

    return resp;
}

} // namespace stfc
