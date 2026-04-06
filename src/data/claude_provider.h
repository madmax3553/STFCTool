#pragma once

#include "data/llm_client.h"
#include <string>
#include <sys/types.h>

namespace stfc {

// ---------------------------------------------------------------------------
// Claude provider — Anthropic's Claude API
//
// Uses the api.anthropic.com REST API with an API key.
// Supports streaming via SSE (Server-Sent Events).
//
// All HTTP calls use curl subprocess instead of httplib to avoid the
// httplib SSLClient segfault (SSL_shutdown on corrupted pointer). This is
// the same pattern used by GeminiProvider and OllamaProvider.
//
// Models: claude-sonnet-4-20250514, claude-opus-4-20250514
// API reference: https://docs.anthropic.com/en/api/messages
// ---------------------------------------------------------------------------

class ClaudeProvider : public LlmClient {
public:
    // api_key: the actual API key value (resolved from env var by caller)
    // model: e.g. "claude-sonnet-4-20250514"
    ClaudeProvider(const std::string& api_key, const std::string& model);

    LlmResponse query(const LlmRequest& req) override;
    LlmResponse query_stream(const LlmRequest& req, LlmStreamCallback cb) override;

    std::string provider_name() const override { return "claude"; }
    std::string model_name() const override { return model_; }
    LlmCapabilities capabilities() const override;
    std::string test_connection() override;

private:
    std::string api_key_;
    std::string model_;

    // Result of spawning curl as a child process
    struct CurlResult {
        std::string stdout_data;
        std::string stderr_data;
        int exit_code = -1;
        std::string error;          // non-empty if fork/pipe failed
    };

    // Result of spawning curl for streaming (returns pipe fds)
    struct CurlStreamResult {
        int stdout_fd = -1;   // pipe read end for stdout
        int stderr_fd = -1;   // pipe read end for stderr
        pid_t pid = -1;       // child process id
        std::string error;    // non-empty if fork/pipe failed
    };

    // Run curl to Claude API, wait for full response
    CurlResult run_curl(const std::string& body, int timeout_sec = 60) const;

    // Spawn curl for streaming, return pipe fds (caller reads and reaps)
    CurlStreamResult run_curl_stream(const std::string& body) const;
};

} // namespace stfc
