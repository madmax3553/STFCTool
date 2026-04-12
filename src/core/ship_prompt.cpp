#include "core/ship_prompt.h"

#include <algorithm>
#include <map>
#include <sstream>

#include "json.hpp"

namespace stfc {

static std::string rarity_str_local(int rarity) {
    switch (rarity) {
        case 1: return "Common";
        case 2: return "Uncommon";
        case 3: return "Rare";
        case 4: return "Epic";
        default: return "Unknown";
    }
}

static std::string hull_type_str_local(int hull_type) {
    switch (hull_type) {
        case 0: return "Explorer";
        case 1: return "Survey";
        case 2: return "Interceptor";
        case 3: return "Battleship";
        default: return "Unknown";
    }
}

static std::string faction_str_local(int64_t faction) {
    switch (faction) {
        case 2113010081: return "Federation";
        case 4153667145: return "Klingon";
        case 669838839: return "Romulan";
        default: return "Neutral/Other";
    }
}

static std::string strip_tags(std::string text) {
    std::string out;
    bool in_tag = false;
    for (char c : text) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out.push_back(c);
    }
    return out;
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string classify_ship_ability_prompt(const std::string& ability_name,
                                                const std::string& ability_description) {
    std::string name = to_lower(strip_tags(ability_name));
    std::string desc = to_lower(strip_tags(ability_description));
    if (name.find("mining") != std::string::npos ||
        desc.find("mining rate") != std::string::npos ||
        desc.find("mining speed") != std::string::npos ||
        desc.find("mining bonus") != std::string::npos) {
        if (desc.find("latinum") != std::string::npos) return "mining_latinum";
        if (desc.find("isogen") != std::string::npos) return "mining_isogen";
        if (desc.find("transogen") != std::string::npos) return "mining_transogen";
        if (desc.find("corrupted") != std::string::npos) return "mining_data";
        if (desc.find("parsteel") != std::string::npos) return "mining_parsteel";
        if (desc.find("tritanium") != std::string::npos) return "mining_tritanium";
        if (desc.find("dilithium") != std::string::npos) return "mining_dilithium";
        if (desc.find("crystal") != std::string::npos && desc.find("gas") != std::string::npos) return "mining_universal";
        if (desc.find("crystal") != std::string::npos) return "mining_crystal";
        if (desc.find("gas") != std::string::npos) return "mining_gas";
        if (desc.find("ore") != std::string::npos) return "mining_ore";
        return "mining_general";
    }
    if (desc.find("reward") != std::string::npos || desc.find("loot") != std::string::npos) return "loot";
    if (desc.find("support") != std::string::npos || desc.find("reinforce") != std::string::npos) return "support";
    if (desc.find("hostile") != std::string::npos || desc.find("armada") != std::string::npos) {
        if (desc.find("swarm") != std::string::npos) return "combat_swarm";
        if (desc.find("borg") != std::string::npos) return "combat_borg";
        if (desc.find("actian") != std::string::npos) return "combat_actian";
        return "combat_hostile";
    }
    if (desc.find("opponent") != std::string::npos || desc.find("weapon damage") != std::string::npos) return "combat_pvp";
    return "general";
}

static bool is_ship_specialty(const std::string& name, int hull_type) {
    std::string n = to_lower(name);
    if (hull_type == 1) return true;
    return n.find("franklin") != std::string::npos ||
           n.find("cerritos") != std::string::npos ||
           n.find("defiant") != std::string::npos ||
           n.find("mantis") != std::string::npos ||
           n.find("d'vor") != std::string::npos ||
           n.find("meridian") != std::string::npos ||
           n.find("vidar") != std::string::npos ||
           n.find("cube") != std::string::npos;
}

static bool is_ship_fkr_candidate(const Ship& s) {
    return faction_str_local(s.faction) != "Neutral/Other";
}

nlohmann::json build_ship_assessment_data(const PlayerData& pd, const GameData& gd) {
    struct Row {
        std::string name;
        int grade = 0;
        std::string rarity;
        int rarity_rank = 0;
        std::string faction;
        int64_t faction_id = 0;
        std::string hull_type;
        int owned_tier = 0;
        int owned_level = 0;
        int max_tier = 0;
        int max_level = 0;
        bool specialty_ship = false;
        bool fkr_candidate = false;
        std::string ability_tag;
        std::string ability_name;
        std::string ability_description;
        std::string description;
        int owned_count = 1;
    };

    std::map<int64_t, std::vector<Row>> by_hull;
    for (const auto& ps : pd.ships) {
        auto it = gd.ships.find(ps.hull_id);
        if (it == gd.ships.end()) continue;
        const auto& gs = it->second;
        Row row;
        row.name = ps.name.empty() ? gs.name : ps.name;
        row.grade = gs.grade;
        row.rarity = rarity_str_local(gs.rarity);
        row.rarity_rank = gs.rarity;
        row.faction = faction_str_local(gs.faction);
        row.faction_id = gs.faction;
        row.hull_type = hull_type_str_local(gs.hull_type);
        row.owned_tier = ps.tier;
        row.owned_level = ps.level;
        row.max_tier = gs.max_tier;
        row.max_level = gs.max_level;
        row.specialty_ship = is_ship_specialty(row.name, gs.hull_type);
        row.fkr_candidate = is_ship_fkr_candidate(gs);
        row.ability_tag = classify_ship_ability_prompt(gs.ability_name, gs.ability_description);
        row.ability_name = gs.ability_name;
        row.ability_description = gs.ability_description;
        row.description = gs.description;
        by_hull[ps.hull_id].push_back(row);
    }

    std::vector<Row> reduced;
    for (auto& [_, rows] : by_hull) {
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.owned_tier != b.owned_tier) return a.owned_tier > b.owned_tier;
            return a.owned_level > b.owned_level;
        });
        bool allow_duplicates = !rows.empty() &&
            (rows.front().hull_type == "Survey" || rows.front().ability_tag.rfind("mining_", 0) == 0);
        int keep_count = allow_duplicates ? std::min(2, (int)rows.size()) : 1;
        for (int i = 0; i < keep_count; ++i) {
            rows[i].owned_count = (int)rows.size();
            reduced.push_back(rows[i]);
        }
    }

    int highest_fkr_grade = 0;
    for (const auto& row : reduced) if (row.fkr_candidate) highest_fkr_grade = std::max(highest_fkr_grade, row.grade);
    int min_fkr_grade = highest_fkr_grade > 0 ? std::max(1, highest_fkr_grade - 1) : 0;

    std::vector<Row> selected;
    std::vector<Row> fallback_combat;
    for (const auto& row : reduced) {
        bool include = row.specialty_ship || (row.fkr_candidate && row.grade >= min_fkr_grade);
        if (!row.specialty_ship && !row.fkr_candidate && row.ability_tag.find("combat") != std::string::npos) fallback_combat.push_back(row);
        if (include) selected.push_back(row);
    }
    std::sort(fallback_combat.begin(), fallback_combat.end(), [](const Row& a, const Row& b) {
        if (a.grade != b.grade) return a.grade > b.grade;
        if (a.rarity_rank != b.rarity_rank) return a.rarity_rank > b.rarity_rank;
        if (a.owned_tier != b.owned_tier) return a.owned_tier > b.owned_tier;
        return a.owned_level > b.owned_level;
    });
    for (int i = 0; i < std::min(5, (int)fallback_combat.size()); ++i) selected.push_back(fallback_combat[i]);

    std::sort(selected.begin(), selected.end(), [](const Row& a, const Row& b) {
        if (a.fkr_candidate != b.fkr_candidate) return a.fkr_candidate > b.fkr_candidate;
        if (a.specialty_ship != b.specialty_ship) return a.specialty_ship > b.specialty_ship;
        if (a.grade != b.grade) return a.grade > b.grade;
        if (a.rarity_rank != b.rarity_rank) return a.rarity_rank > b.rarity_rank;
        if (a.owned_tier != b.owned_tier) return a.owned_tier > b.owned_tier;
        return a.owned_level > b.owned_level;
    });

    nlohmann::json ships = nlohmann::json::array();
    for (const auto& row : selected) {
        ships.push_back({
            {"name", row.name}, {"grade", row.grade}, {"rarity", row.rarity}, {"rarity_rank", row.rarity_rank},
            {"faction", row.faction}, {"faction_id", row.faction_id}, {"hull_type", row.hull_type},
            {"owned_tier", row.owned_tier}, {"owned_level", row.owned_level}, {"max_tier", row.max_tier},
            {"max_level", row.max_level}, {"specialty_ship", row.specialty_ship}, {"fkr_candidate", row.fkr_candidate},
            {"owned_count", row.owned_count}, {"ability_tag", row.ability_tag}, {"ability_name", row.ability_name},
            {"ability_description", row.ability_description}, {"description", row.description}
        });
    }
    return ships;
}

LlmRequest build_ship_assessment_request(const PlayerData& pd, const GameData& gd) {
    LlmRequest req;
    req.system_prompt = R"(You are an STFC ship progression analyst. Your job is to evaluate the player's owned ships and identify which ships matter most for progression, combat, faction/FKR value, and specialty use.

EVALUATION RULES:
- Consider ship grade/generation, rarity, current owned tier, current owned level, hull type, faction alignment, and ship role.
- Do NOT rank ships by one field alone. A higher-generation common ship is not automatically better than a highly developed lower-generation epic ship.
- Prioritize practical dock usage decisions over abstract ranking.
- Distinguish between PvP, PvE, armada, solo armada, mining, and specialty progression roles.
- Faction/FKR ships should be evaluated separately from specialty or utility ships, but do not assume every faction-tagged ship is an automatic dock priority.
- For mining roles, duplicate ships can be the best recommendation if they provide the strongest practical parallel mining setup.
- Consider practical mining constraints such as likely reach and survivability; do not recommend a weaker miner over a better duplicate just for hull variety.
- Use ONLY the ships in the provided data.
- If the data is insufficient to break a tie, say so briefly in the reasoning.

Respond with ONLY valid JSON, no other text:
{
  "pvp_ships": [{"name":"Ship Name","reason":"Why this ship is a top PvP choice right now","dock_priority":1}],
  "pve_ship": {"name":"Ship Name","reason":"Why this is the best general PvE hostile ship right now"},
  "armada_ship": {"name":"Ship Name","reason":"Why this is the best group armada ship right now"},
  "solo_armada_ship": {"name":"Ship Name","reason":"Why this is the best solo armada ship right now"},
  "miners": [{"name":"Ship Name","role":"What mining role this ship should fill","dock_priority":1}],
  "specialty_support_priority_ships": [{"name":"Ship Name","reason":"Why this non-core specialty/support ship still matters for progression","progression_priority":"high | medium | low"}],
  "dock_loadout": [{"slot":1,"name":"Ship Name","role":"Primary current dock role"}],
  "alternates_by_goal": [{"goal":"PvP | PvE | Armada | Solo Armada | Mining | Progression","swap_in":["Ship Name"],"swap_out":["Ship Name"],"reason":"Why this alternate loadout helps"}],
  "top_investment_targets": ["Top ships to prioritize next"],
  "summary": "Short explanation of the best current 7-dock strategy",
  "limitations": "What missing data reduces confidence"
})";
    req.temperature = 0.2;
    req.max_tokens = 4096;
    auto ships = build_ship_assessment_data(pd, gd);
    std::ostringstream user;
    user << "### DATA: ACCOUNT SNAPSHOT\n";
    user << "- Ops Level: " << pd.ops_level << "\n";
    user << "- Owned Ship Count: " << pd.ships.size() << "\n\n";
    user << "### DATA: OWNED SHIPS\n";
    user << ships.dump(2) << "\n\n";
    user << "### TASK\n";
    user << "Recommend the best 7-dock ship loadout for this account right now. Include: top 2-3 PvP ships, top PvE hostile ship, top armada ship, top solo armada ship, top 2 miners, and specialty/support ships that are still worth progression because they are not maxed or remain strategically important. Duplicate miners are allowed when that is the better real-world choice because of reach, survivability, or node access. Then provide alternates based on changing goals.\n";
    req.user_prompt = user.str();
    req.response_schema = R"({"type":"object","properties":{"pvp_ships":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"},"dock_priority":{"type":"integer"}},"required":["name","reason","dock_priority"]}},"pve_ship":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"}},"required":["name","reason"]},"armada_ship":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"}},"required":["name","reason"]},"solo_armada_ship":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"}},"required":["name","reason"]},"miners":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"role":{"type":"string"},"dock_priority":{"type":"integer"}},"required":["name","role","dock_priority"]}},"specialty_support_priority_ships":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"},"progression_priority":{"type":"string"}},"required":["name","reason","progression_priority"]}},"dock_loadout":{"type":"array","items":{"type":"object","properties":{"slot":{"type":"integer"},"name":{"type":"string"},"role":{"type":"string"}},"required":["slot","name","role"]}},"alternates_by_goal":{"type":"array","items":{"type":"object","properties":{"goal":{"type":"string"},"swap_in":{"type":"array","items":{"type":"string"}},"swap_out":{"type":"array","items":{"type":"string"}},"reason":{"type":"string"}},"required":["goal","swap_in","swap_out","reason"]}},"top_investment_targets":{"type":"array","items":{"type":"string"}},"summary":{"type":"string"},"limitations":{"type":"string"}},"required":["pvp_ships","pve_ship","armada_ship","solo_armada_ship","miners","specialty_support_priority_ships","dock_loadout","alternates_by_goal","top_investment_targets","summary","limitations"]})";
    return req;
}

} // namespace stfc
