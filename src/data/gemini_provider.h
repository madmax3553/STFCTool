#pragma once

#include "data/llm_client.h"
#include <string>
#include <sys/types.h>

namespace stfc {

// ---------------------------------------------------------------------------
// Gemini provider — Google's Gemini API (free tier: 15 RPM, 1M tokens/day)
//
// Uses the generativelanguage.googleapis.com REST API with an API key.
// Supports search grounding (Google Search as a tool) for current META data.
//
// All HTTP calls use curl subprocess instead of httplib to avoid the
// httplib SSLClient segfault (SSL_shutdown on corrupted pointer). This is
// the same pattern used by OllamaProvider for reliability.
//
// API reference: https://ai.google.dev/api/generate-content
// ---------------------------------------------------------------------------

class GeminiProvider : public LlmClient {
public:
    // api_key: the actual API key value (resolved from env var by caller)
    // model: e.g. "gemini-2.0-flash"
    GeminiProvider(const std::string& api_key, const std::string& model);

    LlmResponse query(const LlmRequest& req) override;

    std::string provider_name() const override { return "gemini"; }
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

    // Run curl to a Gemini API endpoint. Returns stdout/stderr/exit code.
    // method: "GET" or "POST"
    // path: e.g. "/v1beta/models?key=..."
    // body: JSON body for POST (empty for GET)
    CurlResult run_curl(const std::string& method,
                        const std::string& path,
                        const std::string& body = "",
                        int timeout_sec = 30) const;
};

} // namespace stfc
