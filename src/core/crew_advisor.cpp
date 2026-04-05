#include "core/crew_advisor.h"

#include <sstream>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cctype>

#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ===========================================================================
// Construction
// ===========================================================================

CrewAdvisor::CrewAdvisor(std::unique_ptr<LlmClient> client)
    : client_(std::move(client)) {}

// ===========================================================================
// Provider info passthrough
// ===========================================================================

std::string CrewAdvisor::provider_name() const {
    return client_ ? client_->provider_name() : "none";
}

std::string CrewAdvisor::model_name() const {
    return client_ ? client_->model_name() : "none";
}

LlmCapabilities CrewAdvisor::capabilities() const {
    return client_ ? client_->capabilities() : LlmCapabilities{};
}

// ===========================================================================
// System prompts — carefully crafted for consistent structured output
// ===========================================================================

static const char* CREW_SYSTEM_PROMPT = R"sys(You are an expert advisor for the MOBILE GAME "Star Trek Fleet Command" (STFC) by Scopely. NOT about Star Trek TV shows. This is a mobile strategy game.

GAME MECHANICS — CREW ASSIGNMENT:
- Each ship has 3 officer seats: 1 Captain + 2 Bridge officers.
- Captain seat: that officer's Captain Maneuver (CM) fires ONCE at battle start. Pick officers whose CM is a powerful opening burst or debuff.
- Bridge seats: those officers' Officer Abilities (OA) are PASSIVE during the entire battle. Their CM does NOT fire. Pick officers whose OA provides sustained combat value.
- Below Decks (BDA): passive bonus only. Only officers with a "bd" field and "bda" tag can go here.
- Captain choice matters enormously: the CM is a one-shot ability, so a high-impact CM (big damage burst, critical state application, major debuff) should be captain.
- Bridge officers should have OA that synergize with the captain's CM or with each other's OA.

SYNERGY & STATE CHAINS:
- Officers in the same synergy group (the "g" field) get bonus stats when crewed together. Always check "g" fields for matches.
- State chains: one officer applies a state (burning, morale, breach, assimilate, isolytic), another officer's ability triggers off that state. Example: officer A's CM applies "burning" → officer B's OA does extra damage "when the target is burning". This is extremely powerful.
- Look at "applies:X" and "benefits:X" tags to find state chains.
- An officer who "applies:burning" as captain (CM fires once) pairs well with a bridge officer who "benefits:burning" (OA is always active checking for that state).

OFFICER QUALITY:
- Rank 5 officers are MUCH stronger than lower ranks. Strongly prefer rank 4-5.
- The "rk" field is rank (1-5). Higher is better.
- Tags (the "t" field) tell you what an officer specializes in.

DATA FORMAT — abbreviated JSON keys:
n=name, rk=rank, cm=captain_maneuver text, oa=officer_ability text, bd=below_decks text, g=synergy_group, t=tags

CRITICAL RULES:
1. ONLY use officer names from the "n" field in the list I provide. These are the officers the player ACTUALLY OWNS.
2. Do NOT invent officers or use names from Star Trek that are not in the list.
3. Copy officer names EXACTLY as written.
4. Each crew recommendation MUST use DIFFERENT officers. NEVER repeat the same officer across multiple crews. If you recommend 3 crews, that's 9 unique officers minimum (3 captains + 6 bridge).
5. For each crew, explain: WHY this officer is captain (what does their CM do?), and WHY each bridge officer was chosen (what does their OA contribute?).

Respond with ONLY valid JSON, no other text:
{"crews":[{"captain":"exact name","bridge":["exact name","exact name"],"below_decks":["exact name"],"reasoning":"Explain: why this captain (CM effect), why each bridge officer (OA effect), and what synergy/state chain they form","confidence":0.85}]})sys";

static const char* PROGRESSION_SYSTEM_PROMPT = R"sys(You are an expert advisor for the MOBILE GAME "Star Trek Fleet Command" (STFC) by Scopely. NOT about Star Trek TV shows. This is a mobile strategy game.

Your job is to analyze a player's FULL account and recommend the best INVESTMENTS to grow stronger. This is NOT about picking crews — it's about ACCOUNT PROGRESSION.

INVESTMENT CATEGORIES (you MUST recommend from MULTIPLE categories):
1. "officer" — Rank up an officer (especially rank 3→4 or 4→5, which are huge power spikes)
2. "ship" — Tier up a ship (higher tier = more stats, new abilities)
3. "tech" — Level up forbidden tech (provides combat multipliers)
4. "crew_gap" — An officer the player is MISSING that would complete a powerful crew/synergy group
5. "focus" — A specific game mode the player should focus on (e.g., "armada farming for materials", "hostiles for XP")

HOW TO ANALYZE:
- Look at officers with rank < 5 that are part of strong synergy groups or state chains. Ranking them up is high impact.
- Look for synergy groups where the player has 2 of 3 officers — the missing 3rd would be a priority to recruit.
- Look at ship tiers — a high-tier combat ship amplifies all crew bonuses.
- Look at forbidden tech — even small level increases provide multiplicative bonuses.
- Consider what game content the player's current account is best suited for, and what's the next tier to push into.

DATA FORMAT: n=name, rk=rank, l=level, c=class, r=rarity, g=group, cm/oa/bd=ability text, t=tags
Ships: n=name, type=hull_type, tier=tier, lvl=level
Tech: id=tech_id, tier=tier, lvl=level

RULES:
- Only reference officers/ships from the data I provide.
- Recommend EXACTLY 5-7 investments across MULTIPLE categories. Do NOT just list 5 officers to rank up — diversify.
- Priority 1 = most important, higher number = lower priority.

Respond with ONLY valid JSON, no other text:
{"investments":[{"category":"officer|ship|tech|crew_gap|focus","target":"exact name or description","action":"specific action to take","reason":"why this is high impact","priority":1}],"summary":"2-3 sentence overall account assessment — what is the player strong at, what are the biggest gaps"})sys";

static const char* META_SYSTEM_PROMPT = R"sys(You are an expert advisor for the MOBILE GAME "Star Trek Fleet Command" (STFC) by Scopely. NOT about Star Trek TV shows. This is a mobile strategy game.

Your job is to analyze crew recommendations and compare them against the CURRENT COMMUNITY META for a specific game scenario. You will receive:
1. LOCAL OPTIMIZER RESULTS: Real crew combinations already scored by our scoring engine, across multiple scenarios (PvP, Hybrid, BaseCracker, PvEHostile, Armada, Loot). These are VALIDATED combinations from the player's actual roster.
2. The player's full officer list.

USE YOUR WEB SEARCH to find the latest STFC meta and compare it against what our optimizer recommended.

GAME MECHANICS (brief):
- Ships have 1 Captain (CM fires at start) + 2 Bridge (OA always active) + optional Below Decks.
- Synergy groups give bonus stats when crewed together.
- State chains (burning, morale, breach, assimilate, isolytic) are extremely powerful combos.

YOUR ANALYSIS MUST:
1. Review the local optimizer's top crews and explain WHY they are strong (ability interactions, state chains, synergies)
2. Compare against community meta — do our optimizer picks match what the community recommends?
3. Identify any meta crews the optimizer MISSED (officers the player owns but the optimizer didn't combine optimally)
4. For meta crews requiring officers the player DOESN'T own, list what's missing and suggest substitutes
5. Rate overall meta-readiness

For each crew in your response:
- "meta_crew" = the ideal meta officers (can include officers the player doesn't own)
- "player_has" = which of those the player actually owns
- "missing" = which the player doesn't own
- "substitutes" = for each missing officer, the best available substitute from the player's roster

Respond with ONLY valid JSON, no other text:
{"top_crews":[{"meta_crew":{"captain":"ideal captain","bridge":["ideal bridge 1","ideal bridge 2"]},"player_has":["owned names"],"missing":["missing names"],"substitutes":{"missing_name":"substitute_name"},"scenario":"what this crew excels at","explanation":"why this is meta and how abilities interact"}],"meta_summary":"Overall assessment: how meta-ready is this player? Which optimizer picks are strongest? What are the biggest gaps?"})sys";

// ===========================================================================
// Prompt builders
// ===========================================================================

LlmRequest CrewAdvisor::build_crew_prompt(const AccountSnapshot& snapshot,
                                           int top_n) const {
    LlmRequest req;
    req.system_prompt = CREW_SYSTEM_PROMPT;
    req.temperature = 0.3;
    req.max_tokens = 8192;  // Gemini 2.5 Flash uses thinking tokens from this budget

    std::ostringstream user;
    user << "TASK: Pick the best " << top_n << " DIFFERENT crew(s) for the game scenario '"
         << snapshot.scenario << "' on a " << snapshot.ship_type << " ship.\n\n";

    // Scenario-specific guidance
    const std::string& s = snapshot.scenario;
    if (s == "PvP" || s == "Hybrid") {
        user << "PvP PRIORITIES: Armor piercing, shield piercing, state chains (burning/morale/breach/isolytic), "
             << "cumulative stacking, ability amplifiers. The opening CM burst matters a lot — pick a captain "
             << "whose CM does massive damage or applies a critical debuff.\n\n";
    } else if (s == "PvEHostile" || s == "MissionBoss") {
        user << "PvE PRIORITIES: High sustained damage (crit, extra shots), damage mitigation, "
             << "hull/shield repair. Captain CM should be a big damage opener or a defensive buff. "
             << "Look for officers with 'pve' or 'boss' tags.\n\n";
    } else if (s == "BaseCracker") {
        user << "BASE CRACKING PRIORITIES: Maximum burst damage, armor piercing, "
             << "officers with 'base_attack' tag. The goal is to destroy a player's starbase.\n\n";
    } else if (s == "Armada") {
        user << "ARMADA PRIORITIES: Officers tagged 'armada' get bonuses in armadas. "
             << "Prioritize sustained damage and survivability since armadas are long fights. "
             << "Officers tagged 'non_armada_only' must NOT be used here.\n\n";
    } else if (s.find("Mining") != std::string::npos) {
        user << "MINING PRIORITIES: Officers with mining-related tags (mine_speed, mine_ore, mine_gas, "
             << "mine_crystal, protected, cargo). Combat stats are less important here.\n\n";
    } else if (s == "Loot") {
        user << "LOOT PRIORITIES: Officers that increase loot drops (loot, loot_multiplier tags), "
             << "cargo capacity. Kill speed also matters to farm faster.\n\n";
    }

    user << "IMPORTANT: Each crew must use COMPLETELY DIFFERENT officers. No officer may appear in more than one crew.\n\n";

    user << "Here are the officers this player owns (ONLY use names from this list):\n";
    user << snapshot_to_json(snapshot) << "\n\n";

    user << "For each crew, explain:\n";
    user << "1. Why this officer is captain (what does their CM do at battle start?)\n";
    user << "2. Why each bridge officer was chosen (what does their OA do during battle?)\n";
    user << "3. What synergy/state chain connects them\n";
    user << "Return " << top_n << " crew(s) as JSON.";

    req.user_prompt = user.str();

    req.response_schema = R"({"type":"object","properties":{"crews":{"type":"array"}},"required":["crews"]})";

    return req;
}

LlmRequest CrewAdvisor::build_progression_prompt(const AccountSnapshot& snapshot,
                                                    const std::string& goal) const {
    LlmRequest req;
    req.system_prompt = PROGRESSION_SYSTEM_PROMPT;
    req.temperature = 0.4;
    req.max_tokens = 8192;  // Gemini 2.5 Flash uses thinking tokens from this budget
    req.enable_search = client_ && client_->capabilities().search_grounding;

    std::ostringstream user;
    user << "TASK: Analyze this player's FULL account and recommend what to invest resources in next.\n";
    user << "This is about ACCOUNT GROWTH, not just picking crews.\n\n";

    if (!goal.empty()) {
        user << "Player's stated goal: " << goal << "\n\n";
    }

    user << "ACCOUNT DATA (includes officers, ships, and forbidden tech):\n";
    user << snapshot_to_json(snapshot, SnapshotJsonOptions::full()) << "\n\n";

    user << "ANALYSIS CHECKLIST:\n";
    user << "- Which officers at rank 3-4 would gain the most from ranking up to 5? (Check synergy groups)\n";
    user << "- Are there synergy groups where 2/3 officers are owned? The missing one is a recruitment priority.\n";
    user << "- Which ships should be tiered up for the biggest combat improvement?\n";
    user << "- What forbidden tech levels would give the best multiplier boost?\n";
    user << "- What game mode should the player focus on for progression?\n\n";
    user << "Return 5-7 investments across MULTIPLE categories (officer, ship, tech, crew_gap, focus) as JSON.";

    req.user_prompt = user.str();

    req.response_schema = R"({"type":"object","properties":{"investments":{"type":"array"},"summary":{"type":"string"}},"required":["investments","summary"]})";

    return req;
}

LlmRequest CrewAdvisor::build_meta_prompt(Scenario scenario,
                                             const AccountSnapshot& snapshot,
                                             const std::vector<LocalCrewSummary>& local_crews) const {
    LlmRequest req;
    req.system_prompt = META_SYSTEM_PROMPT;
    req.temperature = 0.4;
    req.max_tokens = 8192;  // Gemini 2.5 Flash uses thinking tokens from this budget
    req.enable_search = client_ && client_->capabilities().search_grounding;

    std::ostringstream user;
    user << "TASK: Analyze the CURRENT COMMUNITY META for: " << scenario_str(scenario) << "\n\n";

    // Feed in local optimizer results — this is the key differentiator
    if (!local_crews.empty()) {
        user << "=== LOCAL OPTIMIZER RESULTS ===\n";
        user << "Our local scoring engine has already computed the best crews from the player's roster.\n";
        user << "These are REAL, validated crew combinations (not guesses):\n\n";
        for (size_t i = 0; i < local_crews.size(); ++i) {
            const auto& lc = local_crews[i];
            user << (i + 1) << ". [" << lc.scenario << "] Captain: " << lc.captain
                 << " | Bridge: ";
            for (size_t j = 0; j < lc.bridge.size(); ++j) {
                if (j > 0) user << ", ";
                user << lc.bridge[j];
            }
            user << " (score: " << static_cast<int>(lc.score) << ")";
            if (!lc.synergy_group.empty())
                user << " [synergy: " << lc.synergy_group << "]";
            if (!lc.notes.empty()) {
                user << " -- ";
                for (size_t j = 0; j < lc.notes.size() && j < 3; ++j) {
                    if (j > 0) user << "; ";
                    user << lc.notes[j];
                }
            }
            user << "\n";
        }
        user << "\n";
    }

    if (req.enable_search) {
        user << "USE YOUR WEB SEARCH to find the latest STFC meta. Search for:\n";
        user << "- \"Star Trek Fleet Command best " << scenario_str(scenario) << " crews 2025\"\n";
        user << "- \"STFC meta crew " << scenario_str(scenario) << " tier list\"\n\n";
        user << "YOUR JOB:\n";
        user << "1. Compare our local optimizer results against the community meta you find online\n";
        user << "2. Confirm which of our local crews align with the current meta\n";
        user << "3. Identify any meta crews we're missing or could improve\n";
        user << "4. For meta crews the player CAN'T build (missing officers), identify what they need\n\n";
    } else {
        user << "Note: Web search is not available. Analyze the local optimizer results using your knowledge of STFC meta.\n\n";
    }

    user << "Here are ALL officers the player owns (for checking what's available):\n";
    user << snapshot_to_json(snapshot) << "\n\n";

    user << "Return 3-5 meta crews as JSON. For each crew, indicate which officers the player has and which are missing.";

    req.user_prompt = user.str();

    // No response_schema when search is enabled (Gemini incompatibility)
    if (!req.enable_search) {
        req.response_schema = R"({"type":"object","properties":{"top_crews":{"type":"array"},"meta_summary":{"type":"string"}},"required":["top_crews","meta_summary"]})";
    }

    return req;
}

// ===========================================================================
// Response parsers
// ===========================================================================

// Helper: find JSON object in a response that might have surrounding text
static json extract_json(const std::string& text) {
    // Try parsing the whole thing first
    try {
        return json::parse(text);
    } catch (...) {}

    // Look for JSON embedded in markdown code blocks
    auto code_start = text.find("```json");
    if (code_start != std::string::npos) {
        auto json_start = text.find('\n', code_start);
        auto json_end = text.find("```", json_start + 1);
        if (json_start != std::string::npos && json_end != std::string::npos) {
            try {
                return json::parse(text.substr(json_start + 1, json_end - json_start - 1));
            } catch (...) {}
        }
    }

    // Look for code block without language specifier
    code_start = text.find("```\n");
    if (code_start != std::string::npos) {
        auto json_start = code_start + 4;
        auto json_end = text.find("```", json_start);
        if (json_end != std::string::npos) {
            try {
                return json::parse(text.substr(json_start, json_end - json_start));
            } catch (...) {}
        }
    }

    // Try to find first { and last } -- greedy JSON extraction
    auto first_brace = text.find('{');
    auto last_brace = text.rfind('}');
    if (first_brace != std::string::npos && last_brace != std::string::npos &&
        last_brace > first_brace) {
        try {
            return json::parse(text.substr(first_brace, last_brace - first_brace + 1));
        } catch (...) {}
    }

    // Try first [ and last ] -- model might return a bare array
    auto first_bracket = text.find('[');
    auto last_bracket = text.rfind(']');
    if (first_bracket != std::string::npos && last_bracket != std::string::npos &&
        last_bracket > first_bracket) {
        try {
            auto arr = json::parse(text.substr(first_bracket, last_bracket - first_bracket + 1));
            if (arr.is_array()) {
                // Wrap bare array in expected object
                json wrapper;
                wrapper["crews"] = arr;
                return wrapper;
            }
        } catch (...) {}
    }

    return json();  // null
}

// Helper: get a string from a json value, trying multiple key names
static std::string jstr(const json& j, const std::vector<std::string>& keys, const std::string& def = "") {
    for (const auto& k : keys) {
        if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
    }
    return def;
}

// Helper: get a string array from a json value, trying multiple key names
static std::vector<std::string> jstr_array(const json& j, const std::vector<std::string>& keys) {
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

// Helper: get a double from a json value, trying multiple key names
static double jdbl(const json& j, const std::vector<std::string>& keys, double def = 0.0) {
    for (const auto& k : keys) {
        if (j.contains(k) && j[k].is_number()) return j[k].get<double>();
    }
    return def;
}

// Helper: find the crew array regardless of what key the model used
static json find_crew_array(const json& j) {
    // Try known keys
    for (const auto& key : {"crews", "crew", "recommendations", "top_crews", "results"}) {
        if (j.contains(key) && j[key].is_array()) return j[key];
    }
    // If the top-level IS an array, use it directly
    if (j.is_array()) return j;
    // Look one level deep for any array of objects
    for (auto& [key, val] : j.items()) {
        if (val.is_array() && !val.empty() && val[0].is_object()) return val;
    }

    // FALLBACK: model returned numbered-key objects like {"crew1":{...}, "crew2":{...}}
    // Collect all top-level object values whose keys look like crew/recommendation + number
    {
        json arr = json::array();
        for (auto& [key, val] : j.items()) {
            if (!val.is_object()) continue;
            // Match keys like "crew1", "crew_2", "recommendation3", "result_1", etc.
            bool is_numbered = false;
            for (const auto& prefix : {"crew", "recommendation", "result", "team"}) {
                if (key.rfind(prefix, 0) == 0) {
                    // Check that the rest is digits or underscore+digits
                    std::string suffix = key.substr(std::strlen(prefix));
                    if (!suffix.empty() && (std::isdigit(suffix[0]) ||
                        (suffix[0] == '_' && suffix.size() > 1 && std::isdigit(suffix[1])))) {
                        is_numbered = true;
                        break;
                    }
                }
            }
            if (is_numbered) {
                arr.push_back(val);
            }
        }
        if (!arr.empty()) return arr;
    }

    return json();
}

AiCrewResult CrewAdvisor::parse_crew_response(const LlmResponse& resp) const {
    AiCrewResult result;
    result.raw_response = resp.content;
    result.model_used = resp.model_used;
    result.search_grounded = resp.search_grounded;

    // Debug log for diagnosing parse issues
    {
        std::ofstream dbg("data/ai_debug.log", std::ios::app);
        if (dbg) {
            dbg << "=== parse_crew_response ===\n";
            dbg << "resp.ok()=" << resp.ok() << " error='" << resp.error << "'\n";
            dbg << "content length=" << resp.content.size() << "\n";
            dbg << "content:\n" << resp.content.substr(0, 2000) << "\n";
            dbg << "===========================\n\n";
        }
    }

    if (!resp.ok()) {
        result.error = resp.error;
        return result;
    }

    json j = extract_json(resp.content);
    json crews = find_crew_array(j);

    // Debug: log what we extracted
    {
        std::ofstream dbg("data/ai_debug.log", std::ios::app);
        if (dbg) {
            dbg << "extract_json null=" << j.is_null() << "\n";
            dbg << "find_crew_array null=" << crews.is_null()
                << " is_array=" << crews.is_array()
                << " size=" << (crews.is_array() ? crews.size() : 0) << "\n\n";
        }
    }

    if (crews.is_null() || !crews.is_array() || crews.empty()) {
        // Fallback: treat the whole response as reasoning text
        AiCrewRecommendation fallback;
        fallback.reasoning = resp.content;
        fallback.confidence = 0.3;
        fallback.warnings.push_back("Could not parse structured response -- showing raw AI output");
        result.recommendations.push_back(fallback);
        return result;
    }

    for (const auto& crew_j : crews) {
        if (!crew_j.is_object()) continue;

        AiCrewRecommendation rec;
        rec.captain = jstr(crew_j, {"captain", "Captain", "cap", "commander", "lead", "name"});
        rec.bridge = jstr_array(crew_j, {"bridge", "Bridge", "bridge_officers", "officers", "members"});
        rec.below_decks = jstr_array(crew_j, {"below_decks", "below_deck", "bda", "Below_Decks"});
        rec.reasoning = jstr(crew_j, {"reasoning", "Reasoning", "reason", "explanation", "why", "notes", "description"});
        rec.confidence = jdbl(crew_j, {"confidence", "Confidence", "score", "rating"}, 0.5);
        rec.ship_advice = jstr(crew_j, {"ship", "Ship", "ship_type", "vessel"});
        rec.warnings = jstr_array(crew_j, {"warnings", "Warnings", "caveats", "notes"});

        // If no captain but bridge has entries, use first bridge as captain
        if (rec.captain.empty() && !rec.bridge.empty()) {
            rec.captain = rec.bridge[0];
            rec.bridge.erase(rec.bridge.begin());
        }

        // Remove captain from bridge list if model duplicated them
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

        // Remove duplicate bridge officers (case-insensitive)
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
            // Validate: a proper crew needs captain + 2 bridge officers
            if (rec.bridge.size() < 2) {
                rec.confidence = std::min(rec.confidence, 0.2);
                rec.warnings.push_back("Incomplete crew (need 2 bridge officers) -- model may have hallucinated");
            }
            result.recommendations.push_back(std::move(rec));
        }
    }

    if (result.recommendations.empty()) {
        AiCrewRecommendation fallback;
        fallback.reasoning = resp.content;
        fallback.confidence = 0.3;
        fallback.warnings.push_back("AI returned data but could not extract crew recommendations");
        result.recommendations.push_back(fallback);
    }

    return result;
}

// Helper: find an investments-like array from flexible JSON
static json find_investment_array(const json& j) {
    for (const auto& key : {"investments", "investment", "recommendations",
                            "priorities", "advice", "items", "results"}) {
        if (j.contains(key) && j[key].is_array()) return j[key];
    }
    if (j.is_array()) return j;
    for (auto& [key, val] : j.items()) {
        if (val.is_array() && !val.empty() && val[0].is_object()) return val;
    }
    return json();
}

ProgressionAdvice CrewAdvisor::parse_progression_response(const LlmResponse& resp) const {
    ProgressionAdvice result;
    result.raw_response = resp.content;
    result.model_used = resp.model_used;
    result.search_grounded = resp.search_grounded;

    if (!resp.ok()) {
        result.error = resp.error;
        return result;
    }

    json j = extract_json(resp.content);
    if (j.is_null()) {
        result.summary = resp.content;
        result.error = "Could not parse structured response";
        return result;
    }

    result.summary = jstr(j, {"summary", "Summary", "overall", "assessment", "overview"});

    json investments = find_investment_array(j);
    if (investments.is_array()) {
        for (const auto& inv_j : investments) {
            if (!inv_j.is_object()) continue;
            ProgressionAdvice::Investment inv;
            inv.category = jstr(inv_j, {"category", "Category", "type", "area"});
            inv.target = jstr(inv_j, {"target", "Target", "name", "officer", "ship", "item"});
            inv.action = jstr(inv_j, {"action", "Action", "recommendation", "what", "do"});
            inv.reason = jstr(inv_j, {"reason", "Reason", "why", "explanation", "rationale"});
            inv.priority = static_cast<int>(jdbl(inv_j, {"priority", "Priority", "rank", "order"}, 99));
            if (!inv.target.empty() || !inv.action.empty()) {
                result.investments.push_back(inv);
            }
        }
    }

    // Sort by priority
    std::sort(result.investments.begin(), result.investments.end(),
              [](const ProgressionAdvice::Investment& a,
                 const ProgressionAdvice::Investment& b) {
                  return a.priority < b.priority;
              });

    if (result.investments.empty() && result.summary.empty()) {
        // Fallback: show raw response
        result.summary = resp.content;
        result.error = "Could not extract structured investments -- showing raw AI output";
    }

    return result;
}

MetaAnalysis CrewAdvisor::parse_meta_response(const LlmResponse& resp) const {
    MetaAnalysis result;
    result.raw_response = resp.content;
    result.model_used = resp.model_used;
    result.search_grounded = resp.search_grounded;
    result.sources = resp.sources;

    if (!resp.ok()) {
        result.error = resp.error;
        return result;
    }

    json j = extract_json(resp.content);
    if (j.is_null()) {
        result.meta_summary = resp.content;
        result.error = "Could not parse structured response";
        return result;
    }

    result.meta_summary = jstr(j, {"meta_summary", "summary", "Summary", "analysis", "overview"});

    // Reuse find_crew_array which already tries "top_crews", "crews", etc.
    json crews = find_crew_array(j);
    if (crews.is_array()) {
        for (const auto& crew_j : crews) {
            if (!crew_j.is_object()) continue;
            MetaAnalysis::MetaCrew mc;

            // Try nested "meta_crew" object first (new schema), fall back to flat keys
            if (crew_j.contains("meta_crew") && crew_j["meta_crew"].is_object()) {
                const auto& meta = crew_j["meta_crew"];
                mc.captain = jstr(meta, {"captain", "Captain", "cap"});
                mc.bridge = jstr_array(meta, {"bridge", "Bridge", "bridge_officers"});
            } else {
                mc.captain = jstr(crew_j, {"captain", "Captain", "cap", "commander", "lead"});
                mc.bridge = jstr_array(crew_j, {"bridge", "Bridge", "bridge_officers", "officers", "members"});
            }

            mc.scenario = jstr(crew_j, {"scenario", "Scenario", "use_case", "context", "good_for"});
            mc.explanation = jstr(crew_j, {"explanation", "Explanation", "reasoning", "why", "reason", "notes"});

            // New meta comparison fields
            mc.player_has = jstr_array(crew_j, {"player_has", "owned", "available"});
            mc.missing = jstr_array(crew_j, {"missing", "Missing", "not_owned", "needed"});

            // Substitutes: map of missing_name -> substitute_name
            for (const auto& key : {"substitutes", "Substitutes", "subs", "replacements"}) {
                if (crew_j.contains(key) && crew_j[key].is_object()) {
                    for (auto& [mk, mv] : crew_j[key].items()) {
                        if (mv.is_string()) {
                            mc.substitutes[mk] = mv.get<std::string>();
                        }
                    }
                    if (!mc.substitutes.empty()) break;
                }
            }

            // If no captain but bridge has entries, use first bridge as captain
            if (mc.captain.empty() && !mc.bridge.empty()) {
                mc.captain = mc.bridge[0];
                mc.bridge.erase(mc.bridge.begin());
            }

            // Remove captain from bridge list if model duplicated them
            {
                auto cap_lower = mc.captain;
                std::transform(cap_lower.begin(), cap_lower.end(), cap_lower.begin(), ::tolower);
                mc.bridge.erase(
                    std::remove_if(mc.bridge.begin(), mc.bridge.end(),
                        [&](const std::string& name) {
                            auto n = name;
                            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                            return n == cap_lower;
                        }),
                    mc.bridge.end());
            }

            if (!mc.captain.empty()) {
                result.top_crews.push_back(std::move(mc));
            }
        }
    }

    if (result.meta_summary.empty() && result.top_crews.empty()) {
        // Fallback: show raw response
        result.meta_summary = resp.content;
        result.error = "Could not extract structured META analysis -- showing raw AI output";
    }

    return result;
}

// ===========================================================================
// Query methods
// ===========================================================================

AiCrewResult CrewAdvisor::recommend_crew(
    const AccountSnapshot& snapshot,
    int top_n,
    AdvisorStreamCallback stream_cb)
{
    if (!client_) {
        AiCrewResult result;
        result.error = "No LLM client configured";
        return result;
    }

    LlmRequest req = build_crew_prompt(snapshot, top_n);

    LlmResponse resp;
    if (stream_cb && client_->capabilities().streaming) {
        resp = client_->query_stream(req, stream_cb);
    } else {
        resp = client_->query(req);
    }

    auto result = parse_crew_response(resp);

    // Trim to requested count (small models often return more than asked)
    if (top_n > 0 && static_cast<int>(result.recommendations.size()) > top_n) {
        result.recommendations.resize(top_n);
    }

    return result;
}

ProgressionAdvice CrewAdvisor::advise_progression(
    const AccountSnapshot& snapshot,
    const std::string& goal,
    AdvisorStreamCallback stream_cb)
{
    if (!client_) {
        ProgressionAdvice result;
        result.error = "No LLM client configured";
        return result;
    }

    LlmRequest req = build_progression_prompt(snapshot, goal);

    LlmResponse resp;
    if (stream_cb && client_->capabilities().streaming) {
        resp = client_->query_stream(req, stream_cb);
    } else {
        resp = client_->query(req);
    }

    return parse_progression_response(resp);
}

MetaAnalysis CrewAdvisor::analyze_meta(
    Scenario scenario,
    const AccountSnapshot& snapshot,
    const std::vector<LocalCrewSummary>& local_crews,
    AdvisorStreamCallback stream_cb)
{
    if (!client_) {
        MetaAnalysis result;
        result.error = "No LLM client configured";
        return result;
    }

    LlmRequest req = build_meta_prompt(scenario, snapshot, local_crews);

    LlmResponse resp;
    if (stream_cb && client_->capabilities().streaming) {
        resp = client_->query_stream(req, stream_cb);
    } else {
        resp = client_->query(req);
    }

    return parse_meta_response(resp);
}

LlmResponse CrewAdvisor::ask(
    const AccountSnapshot& snapshot,
    const std::string& question,
    AdvisorStreamCallback stream_cb)
{
    if (!client_) {
        LlmResponse resp;
        resp.error = "No LLM client configured";
        return resp;
    }

    LlmRequest req;
    req.system_prompt = R"(You are an expert Star Trek Fleet Command (STFC) advisor. The player is asking a question about the MOBILE GAME Star Trek Fleet Command by Scopely. This is NOT about Star Trek TV shows or movies — it is a mobile strategy game with officers, ships, crew mechanics, PvP, mining, armadas, and combat.

You have the player's game account data for context. Only reference officers/ships the player actually owns (from the data below).

RESPONSE STYLE:
- Respond in plain text, NOT JSON. This is a conversation.
- Be concise but thorough. Use bullet points or short paragraphs.
- If the question is about crews, explain your reasoning (why captain, why bridge, what synergy).
- If the question is about game mechanics, explain clearly with examples.
- If you're unsure about current meta, say so rather than guessing.)";
    req.temperature = 0.4;
    req.max_tokens = 8192;  // Gemini 2.5 Flash uses thinking tokens from this budget
    req.enable_search = client_->capabilities().search_grounding;

    std::ostringstream user;
    user << "QUESTION: " << question << "\n\n";
    user << "MY ACCOUNT DATA:\n" << snapshot_to_json(snapshot, SnapshotJsonOptions::overview()) << "\n";

    req.user_prompt = user.str();

    // No response_schema for Ask — we want plain text, not JSON
    // No response_schema when search is enabled either (Gemini incompatibility)

    if (stream_cb && client_->capabilities().streaming) {
        return client_->query_stream(req, stream_cb);
    }
    return client_->query(req);
}

} // namespace stfc
