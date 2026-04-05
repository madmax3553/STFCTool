#include "data/llm_client.h"
#include "data/ollama_provider.h"
#include "data/gemini_provider.h"
#include "data/ssh_tunnel.h"

#include <fstream>
#include <filesystem>
#include <cstdlib>

#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace stfc {

// ---------------------------------------------------------------------------
// Config I/O
// ---------------------------------------------------------------------------

AiConfig load_ai_config(const std::string& path) {
    AiConfig config;

    if (!fs::exists(path)) {
        // Write default config on first run
        save_ai_config(config, path);
        return config;
    }

    try {
        std::ifstream f(path);
        if (!f) return config;

        json j;
        f >> j;

        config.provider           = j.value("provider", config.provider);
        config.model              = j.value("model", config.model);
        config.endpoint           = j.value("endpoint", config.endpoint);
        config.api_key_env        = j.value("api_key_env", config.api_key_env);
        config.temperature        = j.value("temperature", config.temperature);
        config.max_tokens         = j.value("max_tokens", config.max_tokens);
        config.enable_search      = j.value("enable_search", config.enable_search);
        config.cache_ttl_hours    = j.value("cache_ttl_hours", config.cache_ttl_hours);

        // SSH tunnel config
        config.tunnel_enabled     = j.value("tunnel_enabled", config.tunnel_enabled);
        if (j.contains("tunnel") && j["tunnel"].is_object()) {
            auto& t = j["tunnel"];
            config.tunnel.ssh_user           = t.value("ssh_user", config.tunnel.ssh_user);
            config.tunnel.ssh_host           = t.value("ssh_host", config.tunnel.ssh_host);
            config.tunnel.ssh_port           = t.value("ssh_port", config.tunnel.ssh_port);
            config.tunnel.local_port         = t.value("local_port", config.tunnel.local_port);
            config.tunnel.remote_host        = t.value("remote_host", config.tunnel.remote_host);
            config.tunnel.remote_port        = t.value("remote_port", config.tunnel.remote_port);
            config.tunnel.connect_timeout    = t.value("connect_timeout", config.tunnel.connect_timeout);
            config.tunnel.ssh_key_path       = t.value("ssh_key_path", config.tunnel.ssh_key_path);
            config.tunnel.strict_host_checking = t.value("strict_host_checking", config.tunnel.strict_host_checking);
        }

        // Fallback
        config.fallback_provider     = j.value("fallback_provider", config.fallback_provider);
        config.fallback_model        = j.value("fallback_model", config.fallback_model);
        config.fallback_endpoint     = j.value("fallback_endpoint", config.fallback_endpoint);
        config.fallback_api_key_env  = j.value("fallback_api_key_env", config.fallback_api_key_env);

    } catch (const json::exception&) {
        // Corrupted config file — return defaults
    }

    return config;
}

bool save_ai_config(const AiConfig& config, const std::string& path) {
    try {
        // Ensure parent directory exists
        auto parent = fs::path(path).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }

        json j;
        j["provider"]           = config.provider;
        j["model"]              = config.model;
        j["endpoint"]           = config.endpoint;
        j["api_key_env"]        = config.api_key_env;
        j["temperature"]        = config.temperature;
        j["max_tokens"]         = config.max_tokens;
        j["enable_search"]      = config.enable_search;
        j["cache_ttl_hours"]    = config.cache_ttl_hours;

        // SSH tunnel config
        j["tunnel_enabled"]     = config.tunnel_enabled;
        json tunnel;
        tunnel["ssh_user"]             = config.tunnel.ssh_user;
        tunnel["ssh_host"]             = config.tunnel.ssh_host;
        tunnel["ssh_port"]             = config.tunnel.ssh_port;
        tunnel["local_port"]           = config.tunnel.local_port;
        tunnel["remote_host"]          = config.tunnel.remote_host;
        tunnel["remote_port"]          = config.tunnel.remote_port;
        tunnel["connect_timeout"]      = config.tunnel.connect_timeout;
        tunnel["ssh_key_path"]         = config.tunnel.ssh_key_path;
        tunnel["strict_host_checking"] = config.tunnel.strict_host_checking;
        j["tunnel"] = tunnel;

        // Fallback
        j["fallback_provider"]     = config.fallback_provider;
        j["fallback_model"]        = config.fallback_model;
        j["fallback_endpoint"]     = config.fallback_endpoint;
        j["fallback_api_key_env"]  = config.fallback_api_key_env;

        std::ofstream f(path);
        if (!f) return false;
        f << j.dump(2) << "\n";
        return true;

    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Resolve API key from environment variable name
// ---------------------------------------------------------------------------

static std::string resolve_api_key(const std::string& env_var_name) {
    if (env_var_name.empty()) return "";
    const char* val = std::getenv(env_var_name.c_str());
    return val ? std::string(val) : "";
}

// ---------------------------------------------------------------------------
// Provider factory
// ---------------------------------------------------------------------------

std::unique_ptr<LlmClient> create_provider(const std::string& provider,
                                            const std::string& model,
                                            const std::string& endpoint,
                                            const std::string& api_key_env) {
    if (provider == "ollama") {
        return std::make_unique<OllamaProvider>(endpoint, model);
    }

    if (provider == "gemini") {
        std::string api_key = resolve_api_key(api_key_env);
        return std::make_unique<GeminiProvider>(api_key, model);
    }

    // Future providers: claude, openai, etc.
    // if (provider == "claude") { ... }
    // if (provider == "openai") { ... }

    return nullptr;  // Unknown provider
}

LlmClientResult create_llm_client(const AiConfig& config) {
    LlmClientResult result;

    // -----------------------------------------------------------------------
    // Step 1: If primary is Ollama and tunnel is enabled, open the tunnel
    // -----------------------------------------------------------------------
    if (config.provider == "ollama" && config.tunnel_enabled) {
        result.tunnel = std::make_unique<SshTunnel>(config.tunnel);
        std::string tunnel_err = result.tunnel->open();

        if (tunnel_err.empty()) {
            result.tunnel_status = result.tunnel->status();
        } else {
            // Tunnel failed — record error, will try fallback
            result.tunnel_status = "Tunnel failed: " + tunnel_err;
            result.primary_error = tunnel_err;
            result.tunnel.reset();

            // Skip straight to fallback
            goto try_fallback;
        }
    }

    // -----------------------------------------------------------------------
    // Step 2: Try primary provider
    // -----------------------------------------------------------------------
    {
        auto primary = create_provider(config.provider, config.model,
                                       config.endpoint, config.api_key_env);
        if (primary) {
            std::string err = primary->test_connection();
            if (err.empty()) {
                result.client = std::move(primary);
                result.is_fallback = false;
                return result;
            }
            result.primary_error = err;
        } else {
            result.primary_error = "Unknown provider: " + config.provider;
        }
    }

    // If tunnel was opened but Ollama isn't responding, close tunnel
    if (result.tunnel) {
        result.tunnel->close();
        result.tunnel.reset();
        result.tunnel_status += " (closed — Ollama not responding)";
    }

try_fallback:
    // -----------------------------------------------------------------------
    // Step 3: Try fallback provider
    // -----------------------------------------------------------------------
    if (!config.fallback_provider.empty()) {
        // If fallback is Ollama and tunnel is enabled, open the tunnel now
        if (config.fallback_provider == "ollama" && config.tunnel_enabled && !result.tunnel) {
            result.tunnel = std::make_unique<SshTunnel>(config.tunnel);
            std::string tunnel_err = result.tunnel->open();

            if (tunnel_err.empty()) {
                result.tunnel_status = result.tunnel->status();
            } else {
                result.tunnel_status = "Tunnel failed: " + tunnel_err;
                result.tunnel.reset();
                result.primary_error += " | Fallback tunnel failed: " + tunnel_err;
                return result;  // Can't reach Ollama without tunnel
            }
        }

        auto fallback = create_provider(config.fallback_provider,
                                        config.fallback_model,
                                        config.fallback_endpoint,
                                        config.fallback_api_key_env);
        if (fallback) {
            std::string err = fallback->test_connection();
            if (err.empty()) {
                result.client = std::move(fallback);
                result.is_fallback = true;
                return result;
            }
            result.primary_error += " | Fallback (" + config.fallback_provider + "): " + err;
        }

        // Fallback Ollama failed, close tunnel
        if (result.tunnel) {
            result.tunnel->close();
            result.tunnel.reset();
        }
    }

    // Nothing available — return null client with error
    return result;
}

} // namespace stfc
