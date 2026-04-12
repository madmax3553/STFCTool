#include "core/officer_prompt.h"

#include <algorithm>
#include <sstream>

namespace stfc {

static std::string officer_class_str_local(int officer_class) {
    switch (officer_class) {
        case 1: return "Command";
        case 2: return "Science";
        case 3: return "Engineering";
        default: return "Unknown";
    }
}

static std::string rarity_str_local(int rarity) {
    switch (rarity) {
        case 1: return "Common";
        case 2: return "Uncommon";
        case 3: return "Rare";
        case 4: return "Epic";
        default: return "Unknown";
    }
}

static double ability_value_at_rank(const OfficerAbility& ability, int rank) {
    if (ability.values.empty()) return 0.0;
    int idx = std::clamp(rank, 0, (int)ability.values.size() - 1);
    return ability.values[idx].value;
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

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string officer_role_tag(const Officer& o) {
    std::string desc = lower(strip_tags(o.description + " " + o.flavor_text));
    if (o.has_bda) return "below_decks";
    if (desc.find("armada") != std::string::npos) return "armada";
    if (desc.find("hostile") != std::string::npos) return "hostile";
    if (desc.find("mining") != std::string::npos) return "mining";
    if (desc.find("opponent") != std::string::npos || desc.find("player") != std::string::npos || desc.find("ship") != std::string::npos) return "combat";
    if (desc.find("support") != std::string::npos || desc.find("below decks") != std::string::npos) return "support";
    return "general";
}

static std::string compact_effect_summary(const Officer& o, int rank) {
    std::ostringstream ss;
    std::string role = officer_role_tag(o);
    ss << role;
    double oa = ability_value_at_rank(o.ability, rank);
    double cm = ability_value_at_rank(o.captain_ability, 0);
    double bda = o.has_bda ? ability_value_at_rank(o.below_decks_ability, 0) : 0.0;
    if (oa != 0.0) ss << ", officer=" << oa;
    if (cm != 0.0) ss << ", captain=" << cm;
    if (bda != 0.0) ss << ", bda=" << bda;
    return ss.str();
}

nlohmann::json build_officer_assessment_data(const PlayerData& pd, const GameData& gd) {
    struct Row {
        std::string name;
        int level = 0;
        int rank = 0;
        int shards = 0;
        int rarity_rank = 0;
        std::string rarity;
        std::string officer_class;
        std::string group_name;
        int max_rank = 0;
        bool has_bda = false;
        std::string role_tag;
        double captain_value = 0.0;
        double officer_value = 0.0;
        double below_decks_value = 0.0;
        std::string effect_summary;
    };

    std::vector<Row> selected;
    std::vector<Row> backups;
    for (const auto& po : pd.officers) {
        auto it = gd.officers.find(po.officer_id);
        if (it == gd.officers.end()) continue;
        const auto& o = it->second;

        Row row;
        row.name = o.name.empty() ? o.short_name : o.name;
        row.level = po.level;
        row.rank = po.rank;
        row.shards = po.shard_count;
        row.rarity_rank = o.rarity;
        row.rarity = rarity_str_local(o.rarity);
        row.officer_class = officer_class_str_local(o.officer_class);
        row.group_name = o.group_name;
        row.max_rank = o.max_rank;
        row.has_bda = o.has_bda;
        row.role_tag = officer_role_tag(o);
        row.captain_value = ability_value_at_rank(o.captain_ability, 0);
        row.officer_value = ability_value_at_rank(o.ability, po.rank);
        row.below_decks_value = o.has_bda ? ability_value_at_rank(o.below_decks_ability, 0) : 0.0;
        row.effect_summary = compact_effect_summary(o, po.rank);

        bool keep = false;
        if (po.level >= 20 || po.rank >= 2) keep = true;
        if (o.rarity >= 3) keep = true;
        if (o.has_bda) keep = true;
        if (!o.group_name.empty()) keep = true;
        if (row.role_tag != "general") keep = true;

        if (keep) selected.push_back(row);
        else backups.push_back(row);
    }

    std::sort(selected.begin(), selected.end(), [](const Row& a, const Row& b) {
        if (a.rarity_rank != b.rarity_rank) return a.rarity_rank > b.rarity_rank;
        if (a.rank != b.rank) return a.rank > b.rank;
        if (a.level != b.level) return a.level > b.level;
        return a.name < b.name;
    });
    std::sort(backups.begin(), backups.end(), [](const Row& a, const Row& b) {
        if (a.rank != b.rank) return a.rank > b.rank;
        if (a.level != b.level) return a.level > b.level;
        return a.name < b.name;
    });
    for (int i = 0; i < std::min(12, (int)backups.size()); ++i) selected.push_back(backups[i]);

    nlohmann::json officers = nlohmann::json::array();
    for (const auto& row : selected) {
        officers.push_back({
            {"name", row.name},
            {"level", row.level},
            {"rank", row.rank},
            {"shards", row.shards},
            {"rarity", row.rarity},
            {"rarity_rank", row.rarity_rank},
            {"class", row.officer_class},
            {"group_name", row.group_name},
            {"max_rank", row.max_rank},
            {"has_bda", row.has_bda},
            {"role_tag", row.role_tag},
            {"effect_summary", row.effect_summary},
            {"captain_value", row.captain_value},
            {"officer_value", row.officer_value},
            {"below_decks_value", row.below_decks_value}
        });
    }
    return officers;
}

LlmRequest build_officer_assessment_request(const PlayerData& pd, const GameData& gd) {
    LlmRequest req;
    req.system_prompt = R"(You are an STFC officer planning analyst. Your job is to evaluate the player's owned officers and produce an overlap-aware crew matrix across combat, armada, loot, mining, and progression needs.

EVALUATION RULES:
- Use ONLY the officers in the provided data.
- Do not pretend there is a single flat "best PvP crew". Different contexts need different options.
- Overlap matters: if an officer is core in multiple roles, provide multiple viable crew options because the same officer cannot be used everywhere at once.
- Consider officer rarity, current rank, current level, captain value, officer ability value, below-decks value, role tags, effect summaries, and synergy groups.
- Prefer recommendations that are practical for the player's current owned roster, not idealized missing officers.
- Explicitly include multiple options where overlap or cascading substitutions matter.
- Mining often needs 3 simultaneous crews; return enough options to cover that reality.
- Different PvE goals may need different crews, such as loot-focused versus damage-focused setups.

Respond with ONLY valid JSON, no other text:
{
  "pvp": {
    "general": {"recommended_crews": [{"priority":1,"captain":"Officer","bridge":["Officer","Officer"],"use_case":"...","reason":"...","substitutes":["Officer"]}]},
    "vs_explorer": {"recommended_crews": []},
    "vs_interceptor": {"recommended_crews": []},
    "vs_battleship": {"recommended_crews": []},
    "anti_meta": {"recommended_crews": []}
  },
  "pve": {
    "general_hostiles": {"recommended_crews": []},
    "loot_hostiles": {"recommended_crews": []},
    "top_npcs": {"recommended_crews": []},
    "mission_boss": {"recommended_crews": []}
  },
  "armadas": {
    "power": {"recommended_crews": []},
    "loot": {"recommended_crews": []},
    "solo_power": {"recommended_crews": []},
    "solo_loot": {"recommended_crews": []}
  },
  "mining": {
    "parallel_crews": [{"priority":1,"captain":"Officer","bridge":["Officer","Officer"],"use_case":"...","reason":"...","substitutes":["Officer"]}],
    "speed": {"recommended_crews": []},
    "protected_cargo": {"recommended_crews": []},
    "ore": {"recommended_crews": []},
    "gas": {"recommended_crews": []},
    "crystal": {"recommended_crews": []},
    "general": {"recommended_crews": []}
  },
  "below_decks_priority": [{"name":"Officer","reason":"...","priority":"high | medium | low"}],
  "investment_targets": [{"name":"Officer","reason":"...","priority":"high | medium | low"}],
  "overlap_risks": [{"officer":"Officer","used_in":["role"],"backup_needed_for":"role"}],
  "summary": "Short explanation of the officer roster state",
  "limitations": "What missing data reduces confidence"
})";
    req.temperature = 0.2;
    req.max_tokens = 4096;

    auto officers = build_officer_assessment_data(pd, gd);
    std::ostringstream user;
    user << "### DATA: ACCOUNT SNAPSHOT\n";
    user << "- Ops Level: " << pd.ops_level << "\n";
    user << "- Owned Officer Count: " << pd.officers.size() << "\n\n";
    user << "### DATA: PRUNED OWNED OFFICERS\n";
    user << officers.dump(2) << "\n\n";
    user << "### TASK\n";
    user << "Build an overlap-aware crew matrix from the player's owned officers only. Return multiple viable crew options for PvP, PvE, armadas, solo armadas, loot-focused variants, mission bosses, and mining. Mining should cover at least 3 simultaneous crews plus speed, protected cargo, and resource-specific variants.\n";
    req.user_prompt = user.str();
    req.response_schema = R"({"type":"object","properties":{"pvp":{"type":"object","properties":{"general":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"vs_explorer":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"vs_interceptor":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"vs_battleship":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"anti_meta":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]}},"required":["general","vs_explorer","vs_interceptor","vs_battleship","anti_meta"]},"pve":{"type":"object","properties":{"general_hostiles":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"loot_hostiles":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"top_npcs":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"mission_boss":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]}},"required":["general_hostiles","loot_hostiles","top_npcs","mission_boss"]},"armadas":{"type":"object","properties":{"power":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"loot":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"solo_power":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"solo_loot":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]}},"required":["power","loot","solo_power","solo_loot"]},"mining":{"type":"object","properties":{"parallel_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}},"speed":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"protected_cargo":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"ore":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"gas":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"crystal":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]},"general":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"$ref":"#/definitions/crew_option"}}},"required":["recommended_crews"]}},"required":["parallel_crews","speed","protected_cargo","ore","gas","crystal","general"]},"below_decks_priority":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"},"priority":{"type":"string"}},"required":["name","reason","priority"]}},"investment_targets":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"},"priority":{"type":"string"}},"required":["name","reason","priority"]}},"overlap_risks":{"type":"array","items":{"type":"object","properties":{"officer":{"type":"string"},"used_in":{"type":"array","items":{"type":"string"}},"backup_needed_for":{"type":"string"}},"required":["officer","used_in","backup_needed_for"]}},"summary":{"type":"string"},"limitations":{"type":"string"}},"required":["pvp","pve","armadas","mining","below_decks_priority","investment_targets","overlap_risks","summary","limitations"],"definitions":{"crew_option":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}})";
    return req;
}

} // namespace stfc
