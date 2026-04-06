#include "core/ai_crew_engine.h"

#include <sstream>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cctype>
#include <chrono>
#include <thread>
#include <set>
#include <regex>

#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

AiCrewEngine::AiCrewEngine() {}

AiCrewEngine::~AiCrewEngine() {
    shutdown();
}

// ===========================================================================
// Lifecycle
// ===========================================================================

std::string AiCrewEngine::initialize(const std::string& config_path) {
    config_path_ = config_path;
    config_ = load_ai_config(config_path);

    // Load history
    history_.load();

    // Load META cache
    load_meta_cache();

    // Use the factory that handles tunnel + fallback
    auto result = create_llm_client(config_);

    tunnel_ = std::move(result.tunnel);
    tunnel_status_ = result.tunnel_status;
    is_fallback_ = result.is_fallback;
    primary_error_ = result.primary_error;

    if (result.client) {
        client_ = std::move(result.client);
        // Keep a reference to client for group queries, also create advisor for legacy queries
        // We need to share the client — advisor takes ownership, so we create it differently.
        // Solution: create advisor only for legacy methods, group pipeline uses client_ directly.
        // Actually, create_llm_client already created one client. For the advisor, we need a second.
        // Simpler: keep client_ here, pass raw pointer to advisor.
        // BUT CrewAdvisor takes unique_ptr. Let's just not create the advisor for now —
        // legacy methods will use client_ directly through the existing parse logic.

        // Actually, simplest: create the advisor with the client, then also keep a reference.
        // CrewAdvisor can expose its client for the group pipeline.
        // EVEN SIMPLER: just create the advisor and use it for both paths.
        advisor_ = std::make_unique<CrewAdvisor>(std::move(client_));

        initialized_ = true;
        return "";
    }

    initialized_ = false;
    return primary_error_.empty() ? "Failed to create LLM client" : primary_error_;
}

std::string AiCrewEngine::reinitialize() {
    shutdown();
    return initialize(config_path_);
}

void AiCrewEngine::shutdown() {
    advisor_.reset();
    client_.reset();
    gemini_client_.reset();

    if (tunnel_) {
        tunnel_->close();
        tunnel_.reset();
    }

    initialized_ = false;
    is_fallback_ = false;
    primary_error_.clear();
    tunnel_status_.clear();
}

// ===========================================================================
// META cache
// ===========================================================================

void AiCrewEngine::load_meta_cache() {
    meta_cache_ = stfc::load_meta_cache();
}

bool AiCrewEngine::ensure_gemini_client() {
    if (gemini_client_) return true;

    // Create a dedicated Gemini client using fallback config
    // (Gemini is always the fallback in our setup)
    std::string provider = config_.fallback_provider;
    std::string model = config_.fallback_model;
    std::string endpoint = config_.fallback_endpoint;
    std::string api_key_env = config_.fallback_api_key_env;

    // If the primary IS Gemini, use that directly
    if (config_.provider == "gemini") {
        provider = config_.provider;
        model = config_.model;
        endpoint = config_.endpoint;
        api_key_env = config_.api_key_env;
    }

    if (provider != "gemini") {
        return false;  // No Gemini configured at all
    }

    gemini_client_ = create_provider(provider, model, endpoint, api_key_env);
    return gemini_client_ != nullptr;
}

// ===========================================================================
// Status
// ===========================================================================

AiStatus AiCrewEngine::status() const {
    AiStatus s;
    s.available = initialized_ && advisor_ != nullptr;
    s.is_fallback = is_fallback_;
    s.tunnel_status = tunnel_status_;

    if (advisor_) {
        s.provider = advisor_->provider_name();
        s.model = advisor_->model_name();
        auto caps = advisor_->capabilities();
        s.has_search = caps.search_grounding;
        s.has_streaming = caps.streaming;
    }

    if (!s.available) {
        s.error = primary_error_;
    }

    return s;
}

bool AiCrewEngine::is_available() const {
    return initialized_ && advisor_ != nullptr;
}

// ===========================================================================
// Config persistence
// ===========================================================================

bool AiCrewEngine::save_config() {
    return save_ai_config(config_, config_path_);
}

// ===========================================================================
// Snapshot builder (for legacy methods)
// ===========================================================================

AccountSnapshot AiCrewEngine::build_snapshot(
    const PlayerData& player_data,
    const GameData& game_data,
    const std::vector<ClassifiedOfficer>& officers,
    Scenario scenario,
    ShipType ship_type,
    const std::set<std::string>& excluded) const
{
    return build_account_snapshot(
        player_data, game_data, officers,
        scenario, ship_type, 40, excluded);
}

// ===========================================================================
// JSON response parsing for group pipeline
// (Replicates the essential crew parsing from CrewAdvisor, kept here
//  so the group pipeline doesn't depend on CrewAdvisor's internals)
// ===========================================================================

static json extract_json_from_text(const std::string& text) {
    // Try parsing the whole thing first
    try { return json::parse(text); } catch (...) {}

    // Markdown code block
    auto code_start = text.find("```json");
    if (code_start != std::string::npos) {
        auto json_start = text.find('\n', code_start);
        auto json_end = text.find("```", json_start + 1);
        if (json_start != std::string::npos && json_end != std::string::npos) {
            try { return json::parse(text.substr(json_start + 1, json_end - json_start - 1)); } catch (...) {}
        }
    }

    // Generic code block
    code_start = text.find("```\n");
    if (code_start != std::string::npos) {
        auto json_start = code_start + 4;
        auto json_end = text.find("```", json_start);
        if (json_end != std::string::npos) {
            try { return json::parse(text.substr(json_start, json_end - json_start)); } catch (...) {}
        }
    }

    // Greedy brace extraction
    auto first_brace = text.find('{');
    auto last_brace = text.rfind('}');
    if (first_brace != std::string::npos && last_brace != std::string::npos && last_brace > first_brace) {
        try { return json::parse(text.substr(first_brace, last_brace - first_brace + 1)); } catch (...) {}
    }

    // Bare array
    auto first_bracket = text.find('[');
    auto last_bracket = text.rfind(']');
    if (first_bracket != std::string::npos && last_bracket != std::string::npos && last_bracket > first_bracket) {
        try {
            auto arr = json::parse(text.substr(first_bracket, last_bracket - first_bracket + 1));
            if (arr.is_array()) {
                json wrapper;
                wrapper["crews"] = arr;
                return wrapper;
            }
        } catch (...) {}
    }

    // -----------------------------------------------------------------------
    // Regex-based crew extraction fallback.
    // Small models (1B-3B) often produce structurally invalid JSON with nested
    // "crews" keys or mismatched brackets. This extracts individual crew objects
    // by finding {"captain":"...","bridge":[...]} patterns via regex.
    // -----------------------------------------------------------------------
    {
        // Match individual crew-like objects:
        //   {"captain":"NAME","bridge":["NAME","NAME"],"reasoning":"TEXT"}
        // We find each "captain":"..." then grab the enclosing {} block.
        json crews_arr = json::array();
        std::regex captain_re(R"re("captain"\s*:\s*"([^"]+)")re");
        auto it = std::sregex_iterator(text.begin(), text.end(), captain_re);
        auto end = std::sregex_iterator();

        for (; it != end; ++it) {
            // Walk backwards from match to find opening {
            size_t match_pos = static_cast<size_t>(it->position());
            size_t obj_start = text.rfind('{', match_pos);
            if (obj_start == std::string::npos) continue;

            // Walk forward to find matching closing }
            int depth = 0;
            size_t obj_end = std::string::npos;
            for (size_t i = obj_start; i < text.size(); ++i) {
                if (text[i] == '{') depth++;
                else if (text[i] == '}') {
                    depth--;
                    if (depth == 0) { obj_end = i; break; }
                }
            }
            if (obj_end == std::string::npos) continue;

            std::string obj_text = text.substr(obj_start, obj_end - obj_start + 1);
            try {
                json crew_obj = json::parse(obj_text);
                // Must have captain as string and bridge as array
                if (crew_obj.contains("captain") && crew_obj["captain"].is_string() &&
                    crew_obj.contains("bridge") && crew_obj["bridge"].is_array()) {
                    crews_arr.push_back(crew_obj);
                }
            } catch (...) {}
        }

        if (!crews_arr.empty()) {
            json wrapper;
            wrapper["crews"] = crews_arr;
            return wrapper;
        }
    }

    return json();
}

static std::string jstr_g(const json& j, const std::vector<std::string>& keys, const std::string& def = "") {
    for (const auto& k : keys) {
        if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
    }
    return def;
}

static std::vector<std::string> jstr_arr_g(const json& j, const std::vector<std::string>& keys) {
    std::vector<std::string> result;
    for (const auto& k : keys) {
        if (j.contains(k) && j[k].is_array()) {
            for (const auto& v : j[k]) {
                if (v.is_string()) result.push_back(v.get<std::string>());
            }
            if (!result.empty()) return result;
        }
    }
    return result;
}

static double jdbl_g(const json& j, const std::vector<std::string>& keys, double def = 0.0) {
    for (const auto& k : keys) {
        if (j.contains(k) && j[k].is_number()) return j[k].get<double>();
    }
    return def;
}

static json find_crew_array_g(const json& j) {
    // Check common keys for crew arrays
    for (const auto& key : {"crews", "crew", "recommendations", "top_crews",
                             "results", "crew_combinations"}) {
        if (j.contains(key) && j[key].is_array()) return j[key];
    }
    if (j.is_array()) return j;
    // Any array of objects
    for (auto& [key, val] : j.items()) {
        if (val.is_array() && !val.empty() && val[0].is_object()) return val;
    }
    // Numbered-key objects (crew1, crew2, etc.)
    json arr = json::array();
    for (auto& [key, val] : j.items()) {
        if (!val.is_object()) continue;
        for (const auto& prefix : {"crew", "recommendation", "result", "team"}) {
            if (key.rfind(prefix, 0) == 0) {
                std::string suffix = key.substr(std::strlen(prefix));
                if (!suffix.empty() && (std::isdigit(suffix[0]) ||
                    (suffix[0] == '_' && suffix.size() > 1 && std::isdigit(suffix[1])))) {
                    arr.push_back(val);
                    break;
                }
            }
        }
    }
    if (!arr.empty()) return arr;
    return json();
}

// ---------------------------------------------------------------------------
// Normalize Ollama's varied JSON formats into a consistent structure.
//
// Ollama 1B models often return non-standard schemas like:
//   {"crew_combinations":[{"officers":[{"name":"X","rank":"Captain"},{"name":"Y","rank":"Bridge"}]}]}
//   {"crews":[{"captain":{"n":"X","cm":"..."},"bridge":[{"n":"Y"}]}]}
//
// We normalize each crew entry to: {"captain":"X","bridge":["Y","Z"],"reasoning":"..."}
// ---------------------------------------------------------------------------
static json normalize_crew_entry(const json& crew_j) {
    json normalized;

    // Case 1: Already has "captain" as string → standard format, return as-is
    if (crew_j.contains("captain") && crew_j["captain"].is_string()) {
        return crew_j;
    }

    // Case 2: "captain" is an object with "n" or "name" field
    if (crew_j.contains("captain") && crew_j["captain"].is_object()) {
        const auto& cap_obj = crew_j["captain"];
        normalized["captain"] = jstr_g(cap_obj, {"n", "name", "captain"});
        // Copy reasoning from crew level or captain level
        normalized["reasoning"] = jstr_g(crew_j, {"reasoning", "Reasoning", "reason", "explanation", "why", "notes"});
        if (normalized["reasoning"].get<std::string>().empty()) {
            normalized["reasoning"] = jstr_g(cap_obj, {"reasoning", "reason", "explanation"});
        }
    }

    // Case 3: "officers" array with "name"/"rank" fields
    // Model returns: {"officers":[{"name":"X","rank":"Captain"},{"name":"Y","rank":"First Officer"},...]}
    if (crew_j.contains("officers") && crew_j["officers"].is_array()) {
        std::string captain;
        std::vector<std::string> bridge;

        for (const auto& off : crew_j["officers"]) {
            if (!off.is_object()) continue;
            std::string name = jstr_g(off, {"name", "n", "officer"});
            if (name.empty()) continue;

            std::string role = jstr_g(off, {"rank", "role", "position", "type"});
            // Lowercase for comparison
            std::string role_lower = role;
            std::transform(role_lower.begin(), role_lower.end(), role_lower.begin(), ::tolower);

            if (captain.empty() && (role_lower.find("captain") != std::string::npos ||
                role_lower.find("commander") != std::string::npos ||
                role_lower.find("lead") != std::string::npos)) {
                captain = name;
            } else {
                bridge.push_back(name);
            }
        }
        // If no one was tagged captain, first officer is captain
        if (captain.empty() && !bridge.empty()) {
            captain = bridge[0];
            bridge.erase(bridge.begin());
        }
        if (!captain.empty()) {
            normalized["captain"] = captain;
            normalized["bridge"] = bridge;
        }
        // Pull reasoning from crew-level
        if (!normalized.contains("reasoning")) {
            normalized["reasoning"] = jstr_g(crew_j, {"reasoning", "Reasoning", "reason", "explanation", "why", "notes", "description", "name"});
        }
    }

    // Case 4: "bridge" is array of objects instead of strings
    if (crew_j.contains("bridge") && crew_j["bridge"].is_array() &&
        !crew_j["bridge"].empty() && crew_j["bridge"][0].is_object()) {
        std::vector<std::string> bridge_names;
        for (const auto& b : crew_j["bridge"]) {
            if (b.is_object()) {
                // Could be {"n":"X"} or {"officer1":{"n":"X"}} or {"name":"X"}
                std::string name = jstr_g(b, {"n", "name", "officer"});
                if (name.empty()) {
                    // Nested: {"officer1": {"n":"X", ...}}
                    for (auto& [k, v] : b.items()) {
                        if (v.is_object()) {
                            name = jstr_g(v, {"n", "name", "officer"});
                            if (!name.empty()) bridge_names.push_back(name);
                        }
                    }
                } else {
                    bridge_names.push_back(name);
                }
            } else if (b.is_string()) {
                bridge_names.push_back(b.get<std::string>());
            }
        }
        if (!bridge_names.empty()) {
            normalized["bridge"] = bridge_names;
        }
    }

    // If we extracted something, merge with original for fields we didn't handle
    if (normalized.contains("captain")) {
        // Carry over fields not already set
        for (auto& [key, val] : crew_j.items()) {
            if (!normalized.contains(key)) {
                normalized[key] = val;
            }
        }
        return normalized;
    }

    // Couldn't normalize — return original and let field extraction try
    return crew_j;
}

std::vector<AiCrewRecommendation> AiCrewEngine::parse_group_response(const std::string& content) const {
    std::vector<AiCrewRecommendation> result;

    json j = extract_json_from_text(content);
    json crews = find_crew_array_g(j);

    if (crews.is_null() || !crews.is_array() || crews.empty()) {
        // Fallback: treat whole response as reasoning text
        AiCrewRecommendation fallback;
        fallback.reasoning = content;
        fallback.confidence = 0.3;
        fallback.warnings.push_back("Could not parse structured response");
        result.push_back(fallback);
        return result;
    }

    for (const auto& raw_crew_j : crews) {
        if (!raw_crew_j.is_object()) continue;

        // Normalize varied Ollama output formats into a consistent structure
        json crew_j = normalize_crew_entry(raw_crew_j);

        AiCrewRecommendation rec;
        rec.captain = jstr_g(crew_j, {"captain", "Captain", "cap", "commander", "lead", "name"});
        rec.bridge = jstr_arr_g(crew_j, {"bridge", "Bridge", "bridge_officers", "officers", "members"});
        rec.below_decks = jstr_arr_g(crew_j, {"below_decks", "below_deck", "bda", "Below_Decks"});
        rec.reasoning = jstr_g(crew_j, {"reasoning", "Reasoning", "reason", "explanation", "why", "notes", "description"});
        rec.confidence = jdbl_g(crew_j, {"confidence", "Confidence", "score", "rating"}, 0.5);
        rec.ship_advice = jstr_g(crew_j, {"ship", "Ship", "ship_type", "vessel"});
        rec.warnings = jstr_arr_g(crew_j, {"warnings", "Warnings", "caveats"});

        // Fix: first bridge as captain if no captain
        if (rec.captain.empty() && !rec.bridge.empty()) {
            rec.captain = rec.bridge[0];
            rec.bridge.erase(rec.bridge.begin());
        }

        // Deduplicate captain from bridge
        if (!rec.captain.empty()) {
            auto cap_lower = rec.captain;
            std::transform(cap_lower.begin(), cap_lower.end(), cap_lower.begin(), ::tolower);
            rec.bridge.erase(
                std::remove_if(rec.bridge.begin(), rec.bridge.end(),
                    [&](const std::string& name) {
                        auto n = name;
                        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                        return n == cap_lower;
                    }),
                rec.bridge.end());
        }

        // Deduplicate bridge
        {
            std::vector<std::string> seen;
            rec.bridge.erase(
                std::remove_if(rec.bridge.begin(), rec.bridge.end(),
                    [&](const std::string& name) {
                        auto n = name;
                        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                        if (std::find(seen.begin(), seen.end(), n) != seen.end()) return true;
                        seen.push_back(n);
                        return false;
                    }),
                rec.bridge.end());
        }

        if (!rec.captain.empty()) {
            if (rec.bridge.size() < 2) {
                rec.confidence = std::min(rec.confidence, 0.2);
                rec.warnings.push_back("Incomplete crew (need 2 bridge officers)");
            }
            result.push_back(std::move(rec));
        }
    }

    return result;
}

// ===========================================================================
// NEW: META cache refresh (Gemini web-search-grounded)
// ===========================================================================

std::string AiCrewEngine::refresh_meta_cache(
    const std::vector<std::string>& known_officers,
    const MetaPlayerContext& player_ctx,
    AiStreamCallback stream_cb,
    MetaRefreshCallback progress_cb,
    std::atomic<bool>* cancel_flag)
{
    if (!ensure_gemini_client()) {
        return "No Gemini client available. Check GEMINI_API_KEY env var and fallback config.";
    }

    // Groups to query META for — now 15 groups (skip Mining — handled locally)
    // PvP: 7 groups (general + 3 ship types you fly + 3 ship types you fight)
    // PvE: 2 groups (general + specialized)
    // Other: 6 groups
    struct MetaGroupDef {
        OfficerGroupId id;
        std::string name;
        std::string description;
    };
    static const MetaGroupDef meta_groups[] = {
        // PvP granular by ship type
        {OfficerGroupId::PvP_General,        "PvP General",
            "Universal PvP officers that are strong on any ship type in STFC"},
        {OfficerGroupId::PvP_On_Explorer,    "PvP on Explorer",
            "Best PvP crew when the player flies an EXPLORER — officers that synergize with Explorer shield/balanced stats"},
        {OfficerGroupId::PvP_On_Battleship,  "PvP on Battleship",
            "Best PvP crew when the player flies a BATTLESHIP — officers that synergize with Battleship armor/weapon stats"},
        {OfficerGroupId::PvP_On_Interceptor, "PvP on Interceptor",
            "Best PvP crew when the player flies an INTERCEPTOR — officers that synergize with Interceptor speed/crit stats"},
        {OfficerGroupId::PvP_Vs_Explorer,    "PvP vs Explorer",
            "Best crew for KILLING EXPLORERS — officers with shield piercing, shield drain, anti-Explorer abilities"},
        {OfficerGroupId::PvP_Vs_Battleship,  "PvP vs Battleship",
            "Best crew for KILLING BATTLESHIPS — officers with armor piercing, hull damage, anti-armor abilities"},
        {OfficerGroupId::PvP_Vs_Interceptor, "PvP vs Interceptor",
            "Best crew for KILLING INTERCEPTORS — officers with anti-crit, accuracy, front-loaded damage"},

        // PvE level-aware
        {OfficerGroupId::PvE_General,        "PvE General",
            "General PvE hostile grinding in STFC — swarm, daily hostiles, regular hostile farming"},
        {OfficerGroupId::PvE_Specialized,    "PvE Specialized",
            "Specialized PvE hostiles in STFC — borg, eclipse, gorn, xindi, silent enemy, species 8472. "
            "Include officers for each hostile type that is relevant at the player's level."},

        // Other scenarios
        {OfficerGroupId::Base_Attack,   "Base Attack",      "Attacking player starbases in STFC"},
        {OfficerGroupId::Base_Defend,   "Base Defend",      "Defending your starbase in STFC"},
        {OfficerGroupId::Armada,        "Armada",           "Armada battles (coordinated multi-player) in STFC"},
        {OfficerGroupId::Loot_Cargo,    "Loot & Cargo",     "Loot multipliers, cargo, farming efficiency in STFC"},
        {OfficerGroupId::State_Chain,   "State Chain",      "State chain crews (burning, morale, breach, isolytic) in STFC"},
        {OfficerGroupId::Apex_Isolytic, "Apex & Isolytic",  "Apex barrier/shred and isolytic cascade/defense META in STFC"},
    };

    int total = static_cast<int>(std::size(meta_groups));
    MetaCache new_cache;

    // Start from existing cache — only re-query groups that are missing or errored
    new_cache = meta_cache_;

    auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int queried = 0;
    int skipped = 0;

    for (int i = 0; i < total; ++i) {
        if (cancel_flag && cancel_flag->load()) {
            // Save what we have so far before returning
            if (queried > 0) {
                new_cache.last_refresh = now_epoch;
                meta_cache_ = new_cache;
                save_meta_cache(meta_cache_);
            }
            return "Cancelled by user (" + std::to_string(queried) + " queried, " +
                   std::to_string(skipped) + " skipped)";
        }

        const auto& mg = meta_groups[i];

        // Skip groups that already have valid cached data (non-empty officer list,
        // no error in summary). This preserves successful results from previous
        // refreshes and manual template imports, only re-querying failures.
        auto* existing = new_cache.get_group(mg.name);
        if (existing && !existing->top_officers.empty() &&
            existing->meta_summary.substr(0, 6) != "Error:") {
            ++skipped;
            if (progress_cb) {
                progress_cb(i + 1, total, mg.name + " (cached)");
            }
            if (stream_cb) {
                stream_cb("\n--- META: " + mg.name + " (cached, " +
                          std::to_string(existing->top_officers.size()) + " officers) — skipped ---\n");
            }
            continue;
        }

        if (progress_cb) {
            progress_cb(i + 1, total, mg.name);
        }
        if (stream_cb) {
            stream_cb("\n--- META: " + mg.name + " (" + std::to_string(i + 1) + "/" + std::to_string(total) + ") ---\n");
        }

        // Build prompt — now includes date, sources, and player context
        std::string prompt = build_meta_query_prompt(mg.name, mg.description, player_ctx);

        LlmRequest req;
        req.system_prompt = "You are an expert at Star Trek Fleet Command (STFC), a mobile game by Scopely. "
                            "Use your web search capability to find the most current META information.";
        req.user_prompt = prompt;
        req.enable_search = true;   // Web grounding for Gemini
        req.temperature = 0.2;      // Low temp for factual responses
        req.max_tokens = 8192;      // Gemini thinking model needs space

        // Query Gemini
        LlmResponse resp;
        if (stream_cb) {
            resp = gemini_client_->query_stream(req, stream_cb);
        } else {
            resp = gemini_client_->query(req);
        }

        // Debug log
        {
            std::ofstream dbg("data/ai_debug.log", std::ios::app);
            if (dbg) {
                dbg << "=== META REFRESH: " << mg.name << " ===\n";
                dbg << "ok=" << resp.ok() << " error='" << resp.error << "'\n";
                dbg << "response length: " << resp.content.size() << "\n";
                dbg << "response:\n" << resp.content.substr(0, 2000) << "\n\n";
            }
        }

        MetaGroupEntry entry;
        entry.group = mg.name;
        entry.timestamp = now_epoch;
        entry.model_used = resp.model_used.empty() ? gemini_client_->model_name() : resp.model_used;

        if (resp.ok()) {
            entry.top_officers = parse_meta_officer_names(resp.content, known_officers);
            // Store full Gemini response — this is the META knowledge that gets
            // injected into Ollama prompts. Truncating it was losing the synergy
            // explanations that make Ollama actually pick meta-correct crews.
            // Cap at 4000 chars to keep cache file reasonable.
            entry.meta_summary = resp.content.substr(0, 4000);

            // Try to extract crew descriptions from JSON
            try {
                json j = json::parse(resp.content);
                if (j.contains("crews") && j["crews"].is_array()) {
                    for (const auto& crew : j["crews"]) {
                        if (crew.is_object()) {
                            std::string desc;
                            if (crew.contains("captain") && crew["captain"].is_string())
                                desc += crew["captain"].get<std::string>();
                            if (crew.contains("bridge") && crew["bridge"].is_array()) {
                                for (const auto& b : crew["bridge"]) {
                                    if (b.is_string()) desc += " + " + b.get<std::string>();
                                }
                            }
                            if (crew.contains("why") && crew["why"].is_string())
                                desc += ": " + crew["why"].get<std::string>();
                            if (!desc.empty()) entry.top_crews_desc.push_back(desc);
                        }
                    }
                }
            } catch (...) {}
        } else {
            // Even on error, store the entry so we know we tried
            entry.meta_summary = "Error: " + resp.error;
        }

        new_cache.groups[mg.name] = std::move(entry);
        ++queried;

        // Rate-limit protection: space out Gemini requests to avoid burning
        // the 20 req/day free tier limit. Skip delay after the last request.
        if (i < total - 1) {
            // Check if we got a rate-limit error — back off harder
            bool rate_limited = !resp.ok() && (
                resp.error.find("429") != std::string::npos ||
                resp.error.find("rate") != std::string::npos ||
                resp.error.find("Rate") != std::string::npos ||
                resp.error.find("quota") != std::string::npos ||
                resp.error.find("Quota") != std::string::npos ||
                resp.error.find("RESOURCE_EXHAUSTED") != std::string::npos);

            if (rate_limited) {
                // Save progress so far — don't lose successful queries
                new_cache.last_refresh = now_epoch;
                meta_cache_ = new_cache;
                save_meta_cache(meta_cache_);

                if (stream_cb) {
                    stream_cb("\n[Rate limited — saved " + std::to_string(queried) +
                              " results so far. Waiting 15s...]\n");
                }
            }

            int delay_secs = rate_limited ? 15 : 4;

            // Sleep in 1-second increments so we can check cancel flag
            for (int s = 0; s < delay_secs; ++s) {
                if (cancel_flag && cancel_flag->load()) {
                    // Save partial results before returning
                    new_cache.last_refresh = now_epoch;
                    meta_cache_ = new_cache;
                    save_meta_cache(meta_cache_);
                    return "Cancelled (" + std::to_string(queried) + " queried, " +
                           std::to_string(skipped) + " cached)";
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    new_cache.last_refresh = now_epoch;
    meta_cache_ = new_cache;
    save_meta_cache(meta_cache_);

    return "";
}

// ===========================================================================
// Build META-filtered officer groups
//
// Non-exclusive: officers can appear in MULTIPLE groups (same officer can be
// META for PvP General AND PvP on Explorer). Crew locking at Stage 2 handles
// preventing double-use in final results.
//
// Groups are created even with ZERO owned officers, as long as META data
// exists — they serve as aspirational goals. Not-owned META officers are
// stored in group.meta_not_owned for display and Ollama prompt context.
//
// Falls back to tag-based grouping if no META cache.
// ===========================================================================

std::vector<OfficerGroup> AiCrewEngine::build_meta_filtered_groups(
    const std::vector<ClassifiedOfficer>& officers) const
{
    // If no META cache, fall back to tag-based grouping
    if (meta_cache_.empty()) {
        return group_officers(officers);
    }

    // Build a lookup: officer name (lowercase) -> pointer
    std::map<std::string, const ClassifiedOfficer*> owned_lookup;
    for (const auto& off : officers) {
        std::string name_lower = off.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        owned_lookup[name_lower] = &off;
    }

    std::vector<OfficerGroup> result;

    // No exclusive assignment — officers can appear in multiple groups.
    // Crew locking (Stage 2) prevents double-use in final results.

    // Groups to process in order — all 15 non-Mining groups
    struct GroupDef {
        OfficerGroupId id;
        std::string name;
        std::string description;
        std::string guidance;
    };
    static const GroupDef group_defs[] = {
        // PvP granular
        {OfficerGroupId::PvP_General, "PvP General",
         "Universal PvP officers, good on any ship",
         "Focus on: armor/shield piercing, critical hits, damage bursts, stat boosters. "
         "Captain CM should deliver a powerful opening strike or critical debuff."},
        {OfficerGroupId::PvP_On_Explorer, "PvP on Explorer",
         "Best crews when YOU fly an Explorer",
         "Focus on: shield synergy, sustained damage, shield repair. Exploit Explorer defensive strengths."},
        {OfficerGroupId::PvP_On_Battleship, "PvP on Battleship",
         "Best crews when YOU fly a Battleship",
         "Focus on: raw damage output, armor piercing, hull-based survivability."},
        {OfficerGroupId::PvP_On_Interceptor, "PvP on Interceptor",
         "Best crews when YOU fly an Interceptor",
         "Focus on: devastating opening strikes, crit multipliers, speed advantages. End fights quickly."},
        {OfficerGroupId::PvP_Vs_Explorer, "PvP vs Explorer",
         "Crews optimized for killing Explorers",
         "Focus on: shield piercing, shield drain, bypass/strip shields."},
        {OfficerGroupId::PvP_Vs_Battleship, "PvP vs Battleship",
         "Crews optimized for killing Battleships",
         "Focus on: armor piercing, hull damage, abilities that reduce armor effectiveness."},
        {OfficerGroupId::PvP_Vs_Interceptor, "PvP vs Interceptor",
         "Crews optimized for killing Interceptors",
         "Focus on: anti-crit, accuracy vs fast targets, front-loaded damage to destroy them quickly."},

        // PvE
        {OfficerGroupId::PvE_General, "PvE General",
         "General hostile grinding (swarm, dailies)",
         "Focus on: sustained damage, survivability, crit, extra shots, hull/shield regen."},
        {OfficerGroupId::PvE_Specialized, "PvE Specialized",
         "Specialized hostiles (borg, eclipse, gorn, xindi, etc.)",
         "Focus on: hostile-type-specific abilities. Build per-hostile-type crews."},

        // Other
        {OfficerGroupId::Base_Attack, "Base Attack",
         "Officers for attacking player starbases",
         "Focus on: maximum burst damage, armor piercing, shield piercing."},
        {OfficerGroupId::Base_Defend, "Base Defend",
         "Officers for defending your starbase",
         "Focus on: damage mitigation, shield repair, hull repair."},
        {OfficerGroupId::Armada, "Armada",
         "Officers for armada battles",
         "Focus on: armada-tagged officers get bonuses. Sustained DPS + survivability."},
        {OfficerGroupId::Loot_Cargo, "Loot & Cargo",
         "Officers that increase loot drops and cargo",
         "Focus on: loot multipliers, cargo capacity, rep boosts, XP boosts."},
        {OfficerGroupId::State_Chain, "State Chain",
         "Officers that form state chains (burning, morale, breach, isolytic)",
         "Focus on: state application and benefit combos. Captain APPLY, bridge BENEFIT."},
        {OfficerGroupId::Apex_Isolytic, "Apex & Isolytic",
         "Officers with apex barrier/shred or isolytic cascade/defense",
         "Focus on: the Rock-Paper-Scissors META."},
    };

    for (const auto& gd : group_defs) {
        const auto* meta_entry = meta_cache_.get_group(gd.name);
        if (!meta_entry || meta_entry->top_officers.empty()) {
            continue;  // No META data for this group at all
        }

        OfficerGroup group;
        group.id = gd.id;
        group.name = gd.name;
        group.description = gd.description;
        group.prompt_guidance = gd.guidance;

        // Copy META context from Gemini cache
        group.meta_summary = meta_entry->meta_summary;
        group.meta_crew_descriptions = meta_entry->top_crews_desc;

        // Classify each META officer as owned or not-owned
        for (const auto& meta_name : meta_entry->top_officers) {
            std::string meta_lower = meta_name;
            std::transform(meta_lower.begin(), meta_lower.end(), meta_lower.begin(), ::tolower);

            auto it = owned_lookup.find(meta_lower);
            if (it == owned_lookup.end()) {
                // Not owned — store as aspirational goal
                group.meta_not_owned.push_back(meta_name);
                continue;
            }

            const ClassifiedOfficer* off = it->second;

            // Filter: skip very low-rank officers (rank < 2 = barely leveled)
            // but still keep them as not-owned-equivalent (too weak to use)
            if (off->rank < 2) {
                group.meta_not_owned.push_back(meta_name + " (owned but rank <2)");
                continue;
            }

            group.officers.push_back(off);
        }

        // Sort owned officers by rank desc, then rarity desc
        std::sort(group.officers.begin(), group.officers.end(),
            [](const ClassifiedOfficer* a, const ClassifiedOfficer* b) {
                if (a->rank != b->rank) return a->rank > b->rank;
                return a->rarity > b->rarity;
            });

        // Include group even if zero owned officers — it has META data
        // that serves as aspirational goals and Ollama context
        if (group.has_meta()) {
            result.push_back(std::move(group));
        }
    }

    // If META filtering produced NO groups (stale cache? all names mismatched?),
    // fall back to tag-based grouping
    if (result.empty()) {
        return group_officers(officers);
    }

    return result;
}

// ===========================================================================
// Prepare groups (public, for staged TUI workflow)
// ===========================================================================

std::vector<OfficerGroup> AiCrewEngine::prepare_groups(
    const std::vector<ClassifiedOfficer>& officers) const
{
    return build_meta_filtered_groups(officers);
}

// ===========================================================================
// NEW: Group-based query pipeline
// ===========================================================================

GroupQueryResult AiCrewEngine::query_single_group(
    const OfficerGroup& group,
    AiStreamCallback stream_cb)
{
    GroupQueryResult result;
    result.group_name = group.name;
    result.group_id = group.id;
    result.officer_count = group.size();

    if (!is_available()) {
        result.error = "AI advisor not available: " + primary_error_;
        return result;
    }

    // Build the focused prompt for this group
    // Pass real officer names so the example uses actual names (prevents 1B model hallucination)
    std::vector<std::string> example_names;
    for (const auto* off : group.officers) {
        example_names.push_back(off->name);
        if (example_names.size() >= 3) break;
    }
    std::string sys_prompt = group_system_prompt(group.id, example_names);

    // Inject META knowledge into system prompt when available.
    // Cached data from template workflow or prior sessions.
    if (!group.meta_summary.empty() &&
        group.meta_summary.find("Error:") == std::string::npos &&
        group.meta_summary.find("rate limit") == std::string::npos) {
        sys_prompt += "\n--- CURRENT META KNOWLEDGE (from game experts) ---\n";
        sys_prompt += group.meta_summary;
        sys_prompt += "\n--- END META KNOWLEDGE ---\n";
        sys_prompt += "Use the meta knowledge above to guide your crew choices. "
                      "Prioritize synergies and captain choices that match the current meta.\n";
    }

    // Inject good-rated prior responses as context
    std::string history_context = build_history_context(history_, group.name);
    if (!history_context.empty()) {
        sys_prompt += history_context;
    }

    // Build user prompt with officer roster including ability data.
    // Claude Sonnet handles rich context well — include CM/OA/BDA descriptions,
    // numeric values, synergy data, and classification tags for best quality.
    std::ostringstream user;

    // Rich officer roster with ability descriptions for high-quality crew reasoning
    if (group.size() > 0) {
        user << "Pick crews from ONLY these " << group.size() << " officers:\n";
        for (const auto* off : group.officers) {
            // Class label
            std::string cls;
            if (off->officer_class == 1) cls = "CMD";
            else if (off->officer_class == 2) cls = "SCI";
            else if (off->officer_class == 3) cls = "ENG";
            else cls = "???";

            // Header line: name, class, rank, group, synergy, BDA flag
            user << "- " << off->name << " (" << cls << " R" << off->rank;
            if (!off->group.empty()) {
                user << ", " << off->group;
            }
            if (off->synergy_full > 0.0 || off->synergy_half > 0.0) {
                user << ", synergy: "
                     << static_cast<int>(off->synergy_full * 100) << "%/"
                     << static_cast<int>(off->synergy_half * 100) << "%";
            }
            user << ")";
            if (off->is_bda()) user << " [BDA]";
            user << "\n";

            // Captain's Maneuver line
            if (off->is_bda()) {
                user << "  CM: (Below Decks — no captain maneuver)\n";
            } else {
                // Prefer cm_description (from JSON), fall back to cm_text (from CSV)
                std::string cm_desc = off->cm_description;
                if (cm_desc.empty()) cm_desc = off->cm_text;
                if (!cm_desc.empty()) {
                    user << "  CM: " << cm_desc;
                    if (off->cm_value > 0.0) {
                        // Format as percentage if value < 10 (likely a ratio), else raw
                        if (off->cm_value < 10.0)
                            user << " | " << static_cast<int>(off->cm_value * 100) << "%";
                        else
                            user << " | " << static_cast<int>(off->cm_value);
                    }
                    user << "\n";
                }
            }

            // Officer Ability line
            {
                std::string oa_desc = off->oa_text;
                // oa_text is populated from CSV description or JSON officer_ability fallback
                if (!oa_desc.empty() || off->oa_value > 0.0) {
                    user << "  OA: ";
                    if (!oa_desc.empty()) user << oa_desc;
                    if (off->oa_value > 0.0) {
                        // Format: percentage if < 10, raw number otherwise (e.g. apex barrier values)
                        if (off->oa_value < 10.0)
                            user << " | " << static_cast<int>(off->oa_value * 100) << "% at R" << off->rank;
                        else
                            user << " | " << static_cast<int>(off->oa_value) << " at R" << off->rank;
                    }
                    if (off->oa_chance > 0.0 && off->oa_chance < 1.0) {
                        user << " | " << static_cast<int>(off->oa_chance * 100) << "% chance";
                    }
                    user << "\n";
                }
            }

            // Below Decks Ability line (only for BDA officers)
            if (off->is_bda()) {
                std::string bda_desc = off->bda_description;
                if (bda_desc.empty()) bda_desc = off->bda_text;
                if (!bda_desc.empty() || off->bda_value > 0.0) {
                    user << "  BDA: ";
                    if (!bda_desc.empty()) user << bda_desc;
                    if (off->bda_value > 0.0) {
                        if (off->bda_value < 10.0)
                            user << " | " << static_cast<int>(off->bda_value * 100) << "% at R" << off->rank;
                        else
                            user << " | " << static_cast<int>(off->bda_value) << " at R" << off->rank;
                    }
                    user << "\n";
                }
            }

            // Tags line — classification tags for quick filtering
            std::vector<std::string> key_tags;
            if (off->shield_piercing) key_tags.push_back("shield_pierce");
            if (off->armor_piercing) key_tags.push_back("armor_pierce");
            if (off->crit_related) key_tags.push_back("crit");
            if (off->shield_related) key_tags.push_back("shield");
            if (off->mitigation_related) key_tags.push_back("mitigation");
            if (off->shots_related) key_tags.push_back("extra_shots");
            if (off->stat_booster) key_tags.push_back("stat_boost");
            if (off->apex_barrier) key_tags.push_back("apex_barrier");
            if (off->apex_shred) key_tags.push_back("apex_shred");
            if (off->isolytic_cascade) key_tags.push_back("isolytic");
            if (off->cumulative_stacking) key_tags.push_back("cumulative");
            for (const auto& s : off->states_applied)
                key_tags.push_back("applies:" + s);
            for (const auto& s : off->states_benefit)
                key_tags.push_back("benefits:" + s);
            if (!key_tags.empty()) {
                user << "  Tags: [";
                for (size_t ti = 0; ti < key_tags.size(); ++ti) {
                    if (ti > 0) user << ", ";
                    user << key_tags[ti];
                }
                user << "]\n";
            }
        }
    } else {
        user << "The player owns NONE of the META officers for " << group.name << ".\n";
    }

    user << "\n" << group.prompt_guidance << "\n\n";

    // NOT-OWNED officers are excluded from the prompt.
    // Including them caused hallucination in smaller models — the model would
    // pick names from an aspirational list and use them in crews.
    // The not-owned list is displayed in the TUI (Stage 1) as upgrade goals instead.

    if (group.size() > 0) {
        user << "Build 2-3 crews. Each crew = 1 captain + 2 bridge officers.\n";
        user << "You may ONLY use names from the numbered list above. Do NOT use any other names.\n";
        user << "Return JSON: {\"crews\":[{\"captain\":\"NAME\",\"bridge\":[\"NAME\",\"NAME\"],\"reasoning\":\"why\"}]}";
    } else {
        user << "Suggest which officers to acquire first. Return JSON with reasoning.";
    }

    LlmRequest req;
    req.system_prompt = sys_prompt;
    req.user_prompt = user.str();
    req.temperature = 0.3;
    req.max_tokens = 4096;
    req.response_schema = R"({"type":"object","properties":{"crews":{"type":"array"}},"required":["crews"]})";

    // Debug log
    {
        std::ofstream dbg("data/ai_debug.log", std::ios::app);
        if (dbg) {
            dbg << "=== GROUP QUERY: " << group.name << " (" << group.size() << " officers) ===\n";
            dbg << "sys_prompt length: " << sys_prompt.size() << "\n";
            dbg << "user_prompt length: " << req.user_prompt.size() << "\n";
            dbg << "total prompt: " << (sys_prompt.size() + req.user_prompt.size()) << " chars\n";
            dbg << "--- SYSTEM PROMPT ---\n" << sys_prompt << "\n";
            dbg << "--- USER PROMPT ---\n" << req.user_prompt << "\n\n";
        }
    }

    // Execute query
    LlmResponse resp;
    if (stream_cb && advisor_ && advisor_->capabilities().streaming) {
        resp = advisor_->client_query_stream(req, stream_cb);
    } else if (advisor_) {
        resp = advisor_->client_query(req);
    } else {
        result.error = "No advisor available";
        return result;
    }

    result.raw_response = resp.content;

    if (!resp.ok()) {
        result.error = resp.error;
    } else {
        result.crews = parse_group_response(resp.content);
    }

    // Store in history
    AiHistoryEntry entry;
    entry.group = group.name;
    entry.query_type = "group_crew";
    entry.model = resp.model_used.empty() ?
        (advisor_ ? advisor_->model_name() : "unknown") : resp.model_used;
    entry.prompt_summary = req.user_prompt.substr(0, 200);
    entry.response = resp.content;
    entry.input_tokens = resp.input_tokens;
    entry.output_tokens = resp.output_tokens;

    result.history_id = history_.add_entry(entry);

    // Debug log result
    {
        std::ofstream dbg("data/ai_debug.log", std::ios::app);
        if (dbg) {
            dbg << "=== GROUP RESULT: " << group.name << " ===\n";
            dbg << "ok=" << resp.ok() << " error='" << resp.error << "'\n";
            dbg << "crews parsed: " << result.crews.size() << "\n";
            dbg << "history_id: " << result.history_id << "\n";
            dbg << "response:\n" << resp.content.substr(0, 1000) << "\n\n";
        }
    }

    return result;
}

GroupQueryPipelineResult AiCrewEngine::query_by_groups(
    const std::vector<ClassifiedOfficer>& officers,
    AiStreamCallback stream_cb,
    GroupProgressCallback progress_cb,
    std::atomic<bool>* cancel_flag)
{
    GroupQueryPipelineResult pipeline;

    if (!is_available()) {
        pipeline.error = "AI advisor not available: " + primary_error_;
        return pipeline;
    }

    // Group the officers — prefer META-filtered groups, fallback to tag-based
    auto groups = build_meta_filtered_groups(officers);
    pipeline.groups_total = static_cast<int>(groups.size());

    if (advisor_) {
        pipeline.model_used = advisor_->model_name();
    }

    // Query each group sequentially (small model on slow hardware — sequential is correct)
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
        // Check cancellation
        if (cancel_flag && cancel_flag->load()) {
            pipeline.error = "Cancelled by user";
            break;
        }

        const auto& group = groups[i];

        // Skip Mining group — handled by local optimizer
        if (group.id == OfficerGroupId::Mining) {
            pipeline.groups_completed++;
            continue;
        }

        // Skip empty or very small groups
        if (group.size() < 3) {
            pipeline.groups_completed++;
            continue;
        }

        // Progress callback
        if (progress_cb) {
            progress_cb(i + 1, pipeline.groups_total, group.name);
        }

        // Clear stream text between groups (TUI shows live text)
        if (stream_cb) {
            stream_cb("\n--- " + group.name + " (" + std::to_string(group.size()) + " officers) ---\n");
        }

        auto result = query_single_group(group, stream_cb);
        pipeline.groups_completed++;

        if (result.ok()) {
            pipeline.groups_succeeded++;
        }

        pipeline.group_results.push_back(std::move(result));
    }

    return pipeline;
}

// ===========================================================================
// History & Rating
// ===========================================================================

bool AiCrewEngine::rate_result(const std::string& history_id, AiRating rating) {
    return history_.rate_entry(history_id, rating);
}

// ===========================================================================
// Legacy high-level queries
// ===========================================================================

AiCrewResult AiCrewEngine::recommend_crews(
    const PlayerData& player_data,
    const GameData& game_data,
    const std::vector<ClassifiedOfficer>& officers,
    Scenario scenario,
    ShipType ship_type,
    int top_n,
    const std::set<std::string>& excluded,
    AiStreamCallback stream_cb)
{
    if (!is_available()) {
        AiCrewResult result;
        result.error = "AI advisor not available: " + primary_error_;
        return result;
    }

    auto snapshot = build_snapshot(player_data, game_data, officers,
                                   scenario, ship_type, excluded);

    return advisor_->recommend_crew(snapshot, top_n, stream_cb);
}

ProgressionAdvice AiCrewEngine::advise_progression(
    const PlayerData& player_data,
    const GameData& game_data,
    const std::vector<ClassifiedOfficer>& officers,
    const std::string& goal,
    AiStreamCallback stream_cb)
{
    if (!is_available()) {
        ProgressionAdvice result;
        result.error = "AI advisor not available: " + primary_error_;
        return result;
    }

    auto snapshot = build_account_snapshot(
        player_data, game_data, officers,
        Scenario::Hybrid, ShipType::Explorer, 30);

    return advisor_->advise_progression(snapshot, goal, stream_cb);
}

MetaAnalysis AiCrewEngine::analyze_meta(
    Scenario scenario,
    const PlayerData& player_data,
    const GameData& game_data,
    const std::vector<ClassifiedOfficer>& officers,
    CrewOptimizer* optimizer,
    AiStreamCallback stream_cb)
{
    if (!is_available()) {
        MetaAnalysis result;
        result.error = "AI advisor not available: " + primary_error_;
        return result;
    }

    auto snapshot = build_snapshot(player_data, game_data, officers,
                                   scenario, ShipType::Explorer);

    std::vector<LocalCrewSummary> local_crews;
    if (optimizer) {
        static const std::vector<Scenario> meta_scenarios = {
            Scenario::PvP, Scenario::Hybrid, Scenario::BaseCracker,
            Scenario::PvEHostile, Scenario::Armada, Scenario::Loot,
        };

        for (auto sc : meta_scenarios) {
            auto results = optimizer->find_best_crews(sc, 2);
            for (const auto& cr : results) {
                LocalCrewSummary lc;
                lc.scenario = scenario_str(sc);
                lc.captain = cr.breakdown.captain;
                lc.bridge = cr.breakdown.bridge;
                lc.score = cr.score;
                lc.notes = cr.breakdown.synergy_notes;

                for (const auto& off : officers) {
                    if (off.name == cr.breakdown.captain && !off.group.empty()) {
                        lc.synergy_group = off.group;
                        break;
                    }
                }

                local_crews.push_back(std::move(lc));
            }
        }
    }

    return advisor_->analyze_meta(scenario, snapshot, local_crews, stream_cb);
}

LlmResponse AiCrewEngine::ask_question(
    const std::string& question,
    const PlayerData& player_data,
    const GameData& game_data,
    const std::vector<ClassifiedOfficer>& officers,
    AiStreamCallback stream_cb)
{
    if (!is_available()) {
        LlmResponse resp;
        resp.error = "AI advisor not available: " + primary_error_;
        return resp;
    }

    auto snapshot = build_snapshot(player_data, game_data, officers,
                                   Scenario::PvP, ShipType::Explorer);

    return advisor_->ask(snapshot, question, stream_cb);
}

// ===========================================================================
// Result persistence — save/load crew results to disk
// ===========================================================================

static json crew_rec_to_json(const AiCrewRecommendation& rec) {
    json j;
    j["captain"] = rec.captain;
    j["bridge"] = rec.bridge;
    j["below_decks"] = rec.below_decks;
    j["reasoning"] = rec.reasoning;
    j["confidence"] = rec.confidence;
    j["ship_advice"] = rec.ship_advice;
    j["warnings"] = rec.warnings;
    return j;
}

static AiCrewRecommendation crew_rec_from_json(const json& j) {
    AiCrewRecommendation rec;
    if (j.contains("captain") && j["captain"].is_string())
        rec.captain = j["captain"].get<std::string>();
    if (j.contains("bridge") && j["bridge"].is_array())
        for (const auto& b : j["bridge"])
            if (b.is_string()) rec.bridge.push_back(b.get<std::string>());
    if (j.contains("below_decks") && j["below_decks"].is_array())
        for (const auto& b : j["below_decks"])
            if (b.is_string()) rec.below_decks.push_back(b.get<std::string>());
    if (j.contains("reasoning") && j["reasoning"].is_string())
        rec.reasoning = j["reasoning"].get<std::string>();
    if (j.contains("confidence") && j["confidence"].is_number())
        rec.confidence = j["confidence"].get<double>();
    if (j.contains("ship_advice") && j["ship_advice"].is_string())
        rec.ship_advice = j["ship_advice"].get<std::string>();
    if (j.contains("warnings") && j["warnings"].is_array())
        for (const auto& w : j["warnings"])
            if (w.is_string()) rec.warnings.push_back(w.get<std::string>());
    return rec;
}

bool save_group_results(const GroupQueryPipelineResult& results,
                        const std::vector<bool>& locked,
                        const std::set<std::string>& locked_officers,
                        const std::string& path)
{
    try {
        json j;
        j["model_used"] = results.model_used;
        j["groups_total"] = results.groups_total;
        j["groups_completed"] = results.groups_completed;
        j["groups_succeeded"] = results.groups_succeeded;
        j["error"] = results.error;

        json groups_arr = json::array();
        for (size_t i = 0; i < results.group_results.size(); ++i) {
            const auto& gr = results.group_results[i];
            json gj;
            gj["group_name"] = gr.group_name;
            gj["group_id"] = static_cast<int>(gr.group_id);
            gj["officer_count"] = gr.officer_count;
            gj["raw_response"] = gr.raw_response;
            gj["error"] = gr.error;
            gj["history_id"] = gr.history_id;
            gj["rating"] = static_cast<int>(gr.rating);
            gj["locked"] = (i < locked.size()) ? locked[i] : false;

            json crews_arr = json::array();
            for (const auto& crew : gr.crews) {
                crews_arr.push_back(crew_rec_to_json(crew));
            }
            gj["crews"] = crews_arr;
            groups_arr.push_back(gj);
        }
        j["group_results"] = groups_arr;

        // Save locked officer names
        json locked_arr = json::array();
        for (const auto& name : locked_officers) {
            locked_arr.push_back(name);
        }
        j["locked_officer_names"] = locked_arr;

        std::ofstream out(path);
        if (!out) return false;
        out << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool load_group_results(GroupQueryPipelineResult& results,
                        std::vector<bool>& locked,
                        std::set<std::string>& locked_officers,
                        const std::string& path)
{
    try {
        std::ifstream in(path);
        if (!in) return false;

        json j;
        in >> j;

        results = {};
        locked.clear();
        locked_officers.clear();

        if (j.contains("model_used") && j["model_used"].is_string())
            results.model_used = j["model_used"].get<std::string>();
        if (j.contains("groups_total") && j["groups_total"].is_number_integer())
            results.groups_total = j["groups_total"].get<int>();
        if (j.contains("groups_completed") && j["groups_completed"].is_number_integer())
            results.groups_completed = j["groups_completed"].get<int>();
        if (j.contains("groups_succeeded") && j["groups_succeeded"].is_number_integer())
            results.groups_succeeded = j["groups_succeeded"].get<int>();
        if (j.contains("error") && j["error"].is_string())
            results.error = j["error"].get<std::string>();

        if (j.contains("group_results") && j["group_results"].is_array()) {
            for (const auto& gj : j["group_results"]) {
                GroupQueryResult gr;
                if (gj.contains("group_name") && gj["group_name"].is_string())
                    gr.group_name = gj["group_name"].get<std::string>();
                if (gj.contains("group_id") && gj["group_id"].is_number_integer())
                    gr.group_id = static_cast<OfficerGroupId>(gj["group_id"].get<int>());
                if (gj.contains("officer_count") && gj["officer_count"].is_number_integer())
                    gr.officer_count = gj["officer_count"].get<int>();
                if (gj.contains("raw_response") && gj["raw_response"].is_string())
                    gr.raw_response = gj["raw_response"].get<std::string>();
                if (gj.contains("error") && gj["error"].is_string())
                    gr.error = gj["error"].get<std::string>();
                if (gj.contains("history_id") && gj["history_id"].is_string())
                    gr.history_id = gj["history_id"].get<std::string>();
                if (gj.contains("rating") && gj["rating"].is_number_integer())
                    gr.rating = static_cast<AiRating>(gj["rating"].get<int>());

                if (gj.contains("crews") && gj["crews"].is_array()) {
                    for (const auto& cj : gj["crews"]) {
                        gr.crews.push_back(crew_rec_from_json(cj));
                    }
                }

                bool is_locked = false;
                if (gj.contains("locked") && gj["locked"].is_boolean())
                    is_locked = gj["locked"].get<bool>();

                results.group_results.push_back(std::move(gr));
                locked.push_back(is_locked);
            }
        }

        if (j.contains("locked_officer_names") && j["locked_officer_names"].is_array()) {
            for (const auto& name : j["locked_officer_names"]) {
                if (name.is_string())
                    locked_officers.insert(name.get<std::string>());
            }
        }

        return !results.group_results.empty();
    } catch (...) {
        return false;
    }
}

// ===========================================================================
// META Template Batch System — manual copy-paste workflow
// ===========================================================================

// Batch → group mapping (must match the 4-batch spec)
struct BatchGroupDef {
    OfficerGroupId id;
    std::string name;
    std::string description;
};

static const std::vector<std::vector<BatchGroupDef>>& get_batch_groups() {
    static const std::vector<std::vector<BatchGroupDef>> batches = {
        // Batch 0: PvP (7 groups)
        {
            {OfficerGroupId::PvP_General,        "PvP General",
                "Universal PvP officers that are strong on any ship type in STFC"},
            {OfficerGroupId::PvP_On_Explorer,    "PvP on Explorer",
                "Best PvP crew when the player flies an EXPLORER — officers that synergize with Explorer shield/balanced stats"},
            {OfficerGroupId::PvP_On_Battleship,  "PvP on Battleship",
                "Best PvP crew when the player flies a BATTLESHIP — officers that synergize with Battleship armor/weapon stats"},
            {OfficerGroupId::PvP_On_Interceptor, "PvP on Interceptor",
                "Best PvP crew when the player flies an INTERCEPTOR — officers that synergize with Interceptor speed/crit stats"},
            {OfficerGroupId::PvP_Vs_Explorer,    "PvP vs Explorer",
                "Best crew for KILLING EXPLORERS — officers with shield piercing, shield drain, anti-Explorer abilities"},
            {OfficerGroupId::PvP_Vs_Battleship,  "PvP vs Battleship",
                "Best crew for KILLING BATTLESHIPS — officers with armor piercing, hull damage, anti-armor abilities"},
            {OfficerGroupId::PvP_Vs_Interceptor, "PvP vs Interceptor",
                "Best crew for KILLING INTERCEPTORS — officers with anti-crit, accuracy, front-loaded damage"},
        },
        // Batch 1: PvE (2 groups)
        {
            {OfficerGroupId::PvE_General,        "PvE General",
                "General PvE hostile grinding in STFC — swarm, daily hostiles, regular hostile farming"},
            {OfficerGroupId::PvE_Specialized,    "PvE Specialized",
                "Specialized PvE hostiles in STFC — borg, eclipse, gorn, xindi, silent enemy, species 8472"},
        },
        // Batch 2: Strategy (4 groups)
        {
            {OfficerGroupId::Base_Attack,   "Base Attack",
                "Attacking player starbases in STFC"},
            {OfficerGroupId::Base_Defend,   "Base Defend",
                "Defending your starbase in STFC"},
            {OfficerGroupId::Armada,        "Armada",
                "Armada battles (coordinated multi-player) in STFC"},
            {OfficerGroupId::State_Chain,   "State Chain",
                "State chain crews (burning, morale, breach, isolytic) in STFC"},
        },
        // Batch 3: Utility (2 groups)
        {
            {OfficerGroupId::Loot_Cargo,    "Loot & Cargo",
                "Loot multipliers, cargo, farming efficiency in STFC"},
            {OfficerGroupId::Apex_Isolytic, "Apex & Isolytic",
                "Apex barrier/shred and isolytic cascade/defense META in STFC"},
        },
    };
    return batches;
}

std::string AiCrewEngine::meta_batch_name(int batch_index) {
    switch (batch_index) {
        case 0: return "PvP";
        case 1: return "PvE";
        case 2: return "Strategy";
        case 3: return "Utility";
        default: return "Unknown";
    }
}

std::string AiCrewEngine::generate_meta_template(
    int batch_index,
    const MetaPlayerContext& player_ctx) const
{
    if (batch_index < 0 || batch_index >= META_BATCH_COUNT) return "";

    const auto& batches = get_batch_groups();
    const auto& groups = batches[batch_index];
    std::string batch_name = meta_batch_name(batch_index);

    std::ostringstream ss;

    // Header with instructions
    ss << "You are an expert at Star Trek Fleet Command (STFC), a MOBILE GAME by Scopely.\n\n";

    ss << "I need the current META (best / most popular) officers and crews for "
       << groups.size() << " categories in the '" << batch_name << "' category.\n\n";

    if (player_ctx.has_context()) {
        ss << "PLAYER CONTEXT:\n"
           << "- " << player_ctx.summary() << "\n"
           << "- Focus on officers relevant at this level.\n\n";
    }

    ss << "INSTRUCTIONS:\n"
       << "- Use EXACT in-game officer names (e.g., 'PIC Worf' not 'Worf', "
       << "'SNW La\\'an' not 'La\\'an', 'Five of Eleven' not '5 of 11')\n"
       << "- Include officers from ALL eras/factions (TOS, TNG, DS9, SNW, PIC, Discovery, etc.)\n"
       << "- For EACH group below, provide your answer in this format:\n\n"
       << "--- GROUP: <Group Name> ---\n"
       << "{\"officers\":[\"name1\",\"name2\",...],\"crews\":[{\"captain\":\"name\","
       << "\"bridge\":[\"name\",\"name\"],\"why\":\"reason\"}],\"summary\":\"brief META overview\"}\n\n"
       << "Respond with ALL " << groups.size() << " groups, each starting with the "
       << "exact '--- GROUP: <name> ---' header line.\n\n";

    // Per-group sections
    ss << "=== GROUPS ===\n\n";
    for (const auto& g : groups) {
        ss << "--- GROUP: " << g.name << " ---\n"
           << "Context: " << g.description << "\n"
           << "List 15-20 top META officers and 3-5 best crew combos (captain + 2 bridge).\n\n";
    }

    return ss.str();
}

int AiCrewEngine::import_meta_response(
    int batch_index,
    const std::string& response,
    const std::vector<std::string>& known_officers)
{
    if (batch_index < 0 || batch_index >= META_BATCH_COUNT) return -1;
    if (response.empty()) return -1;

    const auto& batches = get_batch_groups();
    const auto& groups = batches[batch_index];

    auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Split the response by "--- GROUP: <name> ---" headers
    // Extract the text between each group header as that group's response
    struct GroupSection {
        std::string group_name;
        std::string content;
    };
    std::vector<GroupSection> sections;

    // Find all group header positions
    const std::string header_prefix = "--- GROUP: ";
    const std::string header_suffix = " ---";
    size_t pos = 0;
    while (pos < response.size()) {
        size_t hdr_start = response.find(header_prefix, pos);
        if (hdr_start == std::string::npos) break;

        size_t name_start = hdr_start + header_prefix.size();
        size_t hdr_end = response.find(header_suffix, name_start);
        if (hdr_end == std::string::npos) break;

        std::string group_name = response.substr(name_start, hdr_end - name_start);
        size_t content_start = hdr_end + header_suffix.size();

        // Content runs until the next group header or end of string
        size_t next_hdr = response.find(header_prefix, content_start);
        std::string content;
        if (next_hdr == std::string::npos) {
            content = response.substr(content_start);
        } else {
            content = response.substr(content_start, next_hdr - content_start);
        }

        sections.push_back({group_name, content});
        pos = (next_hdr == std::string::npos) ? response.size() : next_hdr;
    }

    // If no section headers found, try to use entire response for the first group
    // (handles case where AI ignores format instructions)
    if (sections.empty() && groups.size() == 1) {
        sections.push_back({groups[0].name, response});
    }

    // If still no sections and multiple groups, try to split by group names
    // appearing as natural headers in the text
    if (sections.empty() && groups.size() > 1) {
        // Fallback: assign entire response to all groups
        // (each group's parser will extract what it can)
        for (const auto& g : groups) {
            sections.push_back({g.name, response});
        }
    }

    int imported_count = 0;

    for (const auto& section : sections) {
        // Find matching group definition
        const BatchGroupDef* matched_group = nullptr;
        for (const auto& g : groups) {
            if (g.name == section.group_name) {
                matched_group = &g;
                break;
            }
        }
        if (!matched_group) {
            // Try fuzzy match — find closest group name
            std::string sec_lower = section.group_name;
            std::transform(sec_lower.begin(), sec_lower.end(), sec_lower.begin(), ::tolower);
            for (const auto& g : groups) {
                std::string g_lower = g.name;
                std::transform(g_lower.begin(), g_lower.end(), g_lower.begin(), ::tolower);
                if (sec_lower.find(g_lower) != std::string::npos ||
                    g_lower.find(sec_lower) != std::string::npos) {
                    matched_group = &g;
                    break;
                }
            }
        }
        if (!matched_group) continue;

        // Parse officer names from section content
        auto officers = parse_meta_officer_names(section.content, known_officers);

        // Try to extract crews and summary from JSON in the section
        std::vector<std::string> crew_descs;
        std::string summary;

        // Find JSON object in the section content
        size_t json_start = section.content.find('{');
        size_t json_end = section.content.rfind('}');
        if (json_start != std::string::npos && json_end != std::string::npos && json_end > json_start) {
            try {
                auto j = json::parse(section.content.substr(json_start, json_end - json_start + 1));
                // Extract crews
                for (const auto& key : {"crews", "top_crews"}) {
                    if (j.contains(key) && j[key].is_array()) {
                        for (const auto& crew : j[key]) {
                            if (!crew.is_object()) continue;
                            std::string desc;
                            if (crew.contains("captain") && crew["captain"].is_string())
                                desc = crew["captain"].get<std::string>();
                            if (crew.contains("bridge") && crew["bridge"].is_array()) {
                                for (const auto& b : crew["bridge"]) {
                                    if (b.is_string()) {
                                        if (!desc.empty()) desc += " + ";
                                        desc += b.get<std::string>();
                                    }
                                }
                            }
                            if (crew.contains("why") && crew["why"].is_string()) {
                                if (!desc.empty()) desc += " — ";
                                desc += crew["why"].get<std::string>();
                            }
                            if (!desc.empty()) crew_descs.push_back(desc);
                        }
                        break;
                    }
                }
                // Extract summary
                if (j.contains("summary") && j["summary"].is_string()) {
                    summary = j["summary"].get<std::string>();
                }
            } catch (...) {}
        }

        // Build MetaGroupEntry and store in cache
        MetaGroupEntry entry;
        entry.group = matched_group->name;
        entry.top_officers = officers;
        entry.top_crews_desc = crew_descs;
        entry.meta_summary = summary.empty()
            ? ("Manual import — " + std::to_string(officers.size()) + " officers matched")
            : summary;
        entry.timestamp = now_epoch;
        entry.model_used = "manual-template";

        // Only count as imported if we got at least some officers
        if (!officers.empty()) {
            meta_cache_.groups[matched_group->name] = std::move(entry);
            ++imported_count;
        }
    }

    // Save updated cache
    if (imported_count > 0) {
        meta_cache_.last_refresh = now_epoch;
        save_meta_cache(meta_cache_);
    }

    return imported_count;
}

} // namespace stfc
