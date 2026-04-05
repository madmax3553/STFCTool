#include "core/ai_crew_engine.h"

#include <sstream>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cctype>
#include <chrono>
#include <set>

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
    AiStreamCallback stream_cb,
    MetaRefreshCallback progress_cb,
    std::atomic<bool>* cancel_flag)
{
    if (!ensure_gemini_client()) {
        return "No Gemini client available. Check GEMINI_API_KEY env var and fallback config.";
    }

    // Groups to query META for (skip Mining — handled locally)
    struct MetaGroupDef {
        OfficerGroupId id;
        std::string name;
        std::string description;
    };
    static const MetaGroupDef meta_groups[] = {
        {OfficerGroupId::PvP_Combat,    "PvP Combat",       "Player-vs-player combat in Star Trek Fleet Command"},
        {OfficerGroupId::PvE_Hostile,   "PvE Hostile",      "Hostile NPC grinding (swarm, borg, eclipse, etc.)"},
        {OfficerGroupId::Base_Attack,   "Base Attack",      "Attacking player starbases"},
        {OfficerGroupId::Base_Defend,   "Base Defend",      "Defending your starbase"},
        {OfficerGroupId::Armada,        "Armada",           "Armada battles (coordinated multi-player)"},
        {OfficerGroupId::Loot_Cargo,    "Loot & Cargo",     "Loot multipliers, cargo, farming efficiency"},
        {OfficerGroupId::State_Chain,   "State Chain",      "State chain crews (burning, morale, breach, isolytic)"},
        {OfficerGroupId::Apex_Isolytic, "Apex & Isolytic",  "Apex barrier/shred and isolytic cascade/defense META"},
    };

    int total = static_cast<int>(std::size(meta_groups));
    MetaCache new_cache;

    auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (int i = 0; i < total; ++i) {
        if (cancel_flag && cancel_flag->load()) {
            return "Cancelled by user";
        }

        const auto& mg = meta_groups[i];
        if (progress_cb) {
            progress_cb(i + 1, total, mg.name);
        }
        if (stream_cb) {
            stream_cb("\n--- META: " + mg.name + " (" + std::to_string(i + 1) + "/" + std::to_string(total) + ") ---\n");
        }

        // Build prompt
        std::string prompt = build_meta_query_prompt(mg.name, mg.description);

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
            entry.meta_summary = resp.content.substr(0, 500);  // Keep summary for display

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
    }

    new_cache.last_refresh = now_epoch;
    meta_cache_ = new_cache;
    save_meta_cache(meta_cache_);

    return "";
}

// ===========================================================================
// Build META-filtered officer groups
//
// If META cache exists and is populated, intersect each group's META officer
// list with the player's owned roster. This produces naturally small groups
// (~5-15 officers per group) that the 1B model handles well.
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

    // For each META cache group, intersect with owned roster
    std::vector<OfficerGroup> result;
    std::set<std::string> assigned_officers;  // Track assigned officers (exclusive assignment)

    // Groups to process in order
    struct GroupDef {
        OfficerGroupId id;
        std::string name;
        std::string description;
        std::string guidance;
    };
    // Reuse the same descriptions/guidance from officer_groups.cpp
    static const GroupDef group_defs[] = {
        {OfficerGroupId::PvP_Combat, "PvP Combat",
         "Officers specializing in player-vs-player combat",
         "Focus on: armor/shield piercing, critical hits, damage bursts, "
         "stat boosters, ability amplifiers. Captain CM should deliver a powerful opening "
         "strike or critical debuff. Bridge OA should sustain damage output or defensive advantage."},
        {OfficerGroupId::PvE_Hostile, "PvE Hostile",
         "Officers effective against hostile NPCs",
         "Focus on: sustained damage, survivability, crit damage, extra shots, "
         "hull repair/shield regen. Captain CM should be a big damage opener."},
        {OfficerGroupId::Base_Attack, "Base Attack",
         "Officers for attacking player starbases",
         "Focus on: maximum burst damage, armor piercing, shield piercing."},
        {OfficerGroupId::Base_Defend, "Base Defend",
         "Officers for defending your starbase",
         "Focus on: damage mitigation, shield repair, hull repair."},
        {OfficerGroupId::Armada, "Armada",
         "Officers for armada battles",
         "Focus on: officers tagged 'armada' get bonuses in armadas. "
         "Sustained damage and survivability are key."},
        {OfficerGroupId::Loot_Cargo, "Loot & Cargo",
         "Officers that increase loot drops and cargo",
         "Focus on: loot multipliers, cargo capacity, rep boosts, XP boosts."},
        {OfficerGroupId::State_Chain, "State Chain",
         "Officers that form state chains (burning, morale, breach, assimilate, isolytic)",
         "Focus on: state application and state benefit combos. "
         "Captain should APPLY the state via CM, bridge should BENEFIT from it via OA."},
        {OfficerGroupId::Apex_Isolytic, "Apex & Isolytic",
         "Officers with apex barrier/shred or isolytic cascade/defense",
         "Focus on: the Rock-Paper-Scissors META."},
    };

    for (const auto& gd : group_defs) {
        const auto* meta_entry = meta_cache_.get_group(gd.name);
        if (!meta_entry || meta_entry->top_officers.empty()) {
            continue;  // No META data for this group
        }

        OfficerGroup group;
        group.id = gd.id;
        group.name = gd.name;
        group.description = gd.description;
        group.prompt_guidance = gd.guidance;

        // Intersect META officer names with owned roster
        for (const auto& meta_name : meta_entry->top_officers) {
            std::string meta_lower = meta_name;
            std::transform(meta_lower.begin(), meta_lower.end(), meta_lower.begin(), ::tolower);

            auto it = owned_lookup.find(meta_lower);
            if (it == owned_lookup.end()) continue;

            const ClassifiedOfficer* off = it->second;

            // Exclusive assignment: skip if already assigned to another group
            if (assigned_officers.count(off->name)) continue;

            // Filter: skip very low-rank officers (rank < 2 = barely leveled)
            if (off->rank < 2) continue;

            group.officers.push_back(off);
            assigned_officers.insert(off->name);
        }

        // Sort by rank desc, then rarity desc
        std::sort(group.officers.begin(), group.officers.end(),
            [](const ClassifiedOfficer* a, const ClassifiedOfficer* b) {
                if (a->rank != b->rank) return a->rank > b->rank;
                return a->rarity > b->rarity;
            });

        if (!group.empty()) {
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
    std::string sys_prompt = group_system_prompt(group.id);

    // Inject good-rated prior responses as context
    std::string history_context = build_history_context(history_, group.name);
    if (!history_context.empty()) {
        sys_prompt += history_context;
    }

    // Build user prompt with group officers
    std::ostringstream user;
    user << "Recommend the best 2-3 crew combinations from these "
         << group.size() << " officers (" << group.name << " group).\n\n";
    user << group.prompt_guidance << "\n\n";
    user << "Officers:\n" << group_officers_to_json(group) << "\n\n";
    user << "Return 2-3 crew combinations as JSON. Each crew must use DIFFERENT officers.";

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
            dbg << "total prompt: " << (sys_prompt.size() + req.user_prompt.size()) << " chars\n\n";
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

} // namespace stfc
