#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "data/ssh_tunnel.h"

namespace stfc {

// ---------------------------------------------------------------------------
// Model-agnostic LLM interface
//
// Abstracts away provider differences (Ollama, Gemini, Claude, OpenAI, etc.)
// so the advisor layer builds prompts once and any backend can execute them.
//
// Design for the quickly-changing AI landscape: swap providers by changing
// a config file, not by rewriting code.
// ---------------------------------------------------------------------------

// Request to send to an LLM
struct LlmRequest {
    std::string system_prompt;         // Role/context instructions
    std::string user_prompt;           // The actual question + account data
    bool enable_search = false;        // Web grounding (if provider supports it)
    std::string response_schema;       // JSON schema hint for structured output (optional)
    int max_tokens = 4096;             // Response length limit
    double temperature = 0.3;          // 0.0 = deterministic, 1.0 = creative
};

// Response from an LLM
struct LlmResponse {
    std::string content;               // Raw response text (or JSON string)
    std::string model_used;            // Which model actually responded
    int input_tokens = 0;              // For cost/usage tracking
    int output_tokens = 0;
    bool search_grounded = false;      // Did it use web search?
    std::vector<std::string> sources;  // URLs if search-grounded
    std::string error;                 // Empty if success

    bool ok() const { return error.empty() && !content.empty(); }
};

// Provider capabilities (so caller can adapt behavior)
struct LlmCapabilities {
    bool structured_output = false;    // Can produce JSON to a schema
    bool search_grounding = false;     // Can search the web during inference
    bool streaming = false;            // Supports streaming responses
    bool function_calling = false;     // Supports tool/function calls
    int context_window = 0;            // Max tokens (input + output)
};

// Streaming callback: called with each chunk of text as it arrives
using LlmStreamCallback = std::function<void(const std::string& chunk)>;

// ---------------------------------------------------------------------------
// Abstract LLM client — one implementation per provider
// ---------------------------------------------------------------------------

class LlmClient {
public:
    virtual ~LlmClient() = default;

    // Send a request and get a complete response
    virtual LlmResponse query(const LlmRequest& req) = 0;

    // Send a request with streaming (default: falls back to non-streaming)
    virtual LlmResponse query_stream(const LlmRequest& req, LlmStreamCallback cb) {
        auto resp = query(req);
        if (resp.ok() && cb) cb(resp.content);
        return resp;
    }

    // Provider metadata
    virtual std::string provider_name() const = 0;
    virtual std::string model_name() const = 0;
    virtual LlmCapabilities capabilities() const = 0;

    // Connection test — returns empty string on success, error message on failure
    virtual std::string test_connection() = 0;
};

// ---------------------------------------------------------------------------
// AI configuration (parsed from data/ai_config.json)
// ---------------------------------------------------------------------------

struct AiConfig {
    std::string provider = "ollama";                    // ollama | gemini | claude | openai
    std::string model = "gemma2:9b";                    // model name within provider
    std::string endpoint = "http://127.0.0.1:11434";     // API endpoint
    std::string api_key_env = "";                        // env var name for API key (not the key itself)
    double temperature = 0.3;
    int max_tokens = 4096;
    bool enable_search = false;                          // web grounding when available
    int cache_ttl_hours = 24;                            // how long to cache AI responses

    // SSH tunnel config (for reaching Ollama on a remote machine)
    bool tunnel_enabled = true;                          // auto-open SSH tunnel for Ollama
    SshTunnelConfig tunnel;                              // tunnel settings (user, host, ports, etc.)

    // Fallback provider (used when primary is unavailable, e.g. Ollama
    // via SSH tunnel is down → fall back to Gemini free tier)
    std::string fallback_provider = "gemini";
    std::string fallback_model = "gemini-2.0-flash";
    std::string fallback_endpoint = "https://generativelanguage.googleapis.com";
    std::string fallback_api_key_env = "GEMINI_API_KEY";
};

// ---------------------------------------------------------------------------
// Factory: create the right LlmClient from config
//
// If primary provider fails connection test and a fallback is configured,
// automatically returns the fallback provider instead.
// ---------------------------------------------------------------------------

struct LlmClientResult {
    std::unique_ptr<LlmClient> client;
    std::unique_ptr<SshTunnel> tunnel;     // managed tunnel (kept alive while client is in use)
    bool is_fallback = false;              // true if primary failed and fallback was used
    std::string primary_error;             // non-empty if primary failed
    std::string tunnel_status;             // tunnel status message for display
};

// Create client for a specific provider (no fallback logic)
std::unique_ptr<LlmClient> create_provider(const std::string& provider,
                                            const std::string& model,
                                            const std::string& endpoint,
                                            const std::string& api_key_env);

// Create client with automatic fallback
LlmClientResult create_llm_client(const AiConfig& config);

// ---------------------------------------------------------------------------
// Config I/O
// ---------------------------------------------------------------------------

AiConfig load_ai_config(const std::string& path = "data/ai_config.json");
bool save_ai_config(const AiConfig& config, const std::string& path = "data/ai_config.json");

} // namespace stfc
