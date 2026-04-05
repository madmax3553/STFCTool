#pragma once

#include "data/llm_client.h"
#include <string>

// Forward declare nlohmann json to avoid including the full header
#include "json.hpp"

namespace stfc {

// ---------------------------------------------------------------------------
// Ollama provider — local LLM via Ollama REST API
//
// Ollama runs locally (or via SSH tunnel on localhost:11434) and serves
// models like gemma2:9b, llama3.1:8b, mistral:7b, etc.
//
// Query methods use curl as a child process instead of httplib to avoid
// hard read timeouts. The worker runs until Ollama finishes — no cutoff.
//
// API reference: https://github.com/ollama/ollama/blob/main/docs/api.md
// ---------------------------------------------------------------------------

class OllamaProvider : public LlmClient {
public:
    // endpoint: e.g. "http://127.0.0.1:11434"
    // model: e.g. "gemma2:9b"
    OllamaProvider(const std::string& endpoint, const std::string& model);

    LlmResponse query(const LlmRequest& req) override;
    LlmResponse query_stream(const LlmRequest& req, LlmStreamCallback cb) override;

    std::string provider_name() const override { return "ollama"; }
    std::string model_name() const override { return model_; }
    LlmCapabilities capabilities() const override;
    std::string test_connection() override;

private:
    std::string host_;     // e.g. "127.0.0.1"
    int port_ = 11434;
    std::string model_;

    // Parse "http://host:port" into host_ and port_
    void parse_endpoint(const std::string& endpoint);

    // Build Ollama /api/chat request body JSON
    nlohmann::json build_request_body(const LlmRequest& req, bool stream) const;

    // Result of spawning curl as a child process
    struct CurlResult {
        int stdout_fd = -1;   // pipe read end for stdout
        int stderr_fd = -1;   // pipe read end for stderr
        pid_t pid = -1;       // child process id
        std::string error;    // non-empty if fork/pipe failed
    };

    // Spawn curl as a child process to POST to Ollama. Returns pipe fds
    // for reading stdout/stderr. The caller is responsible for closing
    // the fds and reaping the child with waitpid().
    CurlResult run_curl(const std::string& json_body, bool stream) const;
};

} // namespace stfc
