#include "core/officer_prompt.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>

namespace stfc {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

static std::string rarity_from_char(char c) {
    switch (c) {
        case 'C': return "Common";
        case 'U': return "Uncommon";
        case 'R': return "Rare";
        case 'E': return "Epic";
        default:  return "Unknown";
    }
}

static int rarity_rank_from_char(char c) {
    switch (c) {
        case 'C': return 1;
        case 'U': return 2;
        case 'R': return 3;
        case 'E': return 4;
        default:  return 0;
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

// ---------------------------------------------------------------------------
// Tooltip placeholder resolution for the fallback path (officers not in the
// classified roster, e.g. level-0 officers with shards only).
// ---------------------------------------------------------------------------

static std::string replace_all_local(std::string text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

static std::string fmt_pct_local(double value) {
    std::ostringstream os;
    double pct = value * 100.0;
    if (std::abs(pct - std::round(pct)) < 0.0001) {
        os << static_cast<int>(std::round(pct)) << "%";
    } else {
        os << std::fixed << std::setprecision(1) << pct << "%";
    }
    return os.str();
}

static std::string fmt_num_local(double value) {
    std::ostringstream os;
    if (std::abs(value - std::round(value)) < 0.0001) {
        auto v = static_cast<int64_t>(std::round(value));
        if (v >= 1000 || v <= -1000) {
            std::string s = std::to_string(std::abs(v));
            std::string result;
            int count = 0;
            for (int i = static_cast<int>(s.size()) - 1; i >= 0; --i) {
                if (count > 0 && count % 3 == 0) result = "," + result;
                result = s[i] + result;
                ++count;
            }
            if (v < 0) result = "-" + result;
            return result;
        }
        os << v;
    } else {
        os << std::fixed << std::setprecision(2) << value;
    }
    return os.str();
}

// Resolve .NET-style format placeholders in officer tooltip text.
// rank is the officer's current rank (0-4), used to index ability value arrays.
static std::string resolve_tooltip_local(const Officer& officer, int rank) {
    std::string text = officer.description;
    if (text.empty()) return text;
    const auto& cap = officer.captain_ability.values;
    const auto& abil = officer.ability.values;
    const auto& bda = officer.below_decks_ability.values;
    auto rank_idx = std::max(0, rank);
    auto cap_value = [&](int idx) -> double {
        idx = std::max(0, std::min(idx, static_cast<int>(cap.size()) - 1));
        return cap.empty() ? 0.0 : cap[idx].value;
    };
    auto abil_value = [&](int idx) -> double {
        idx = std::max(0, std::min(idx, static_cast<int>(abil.size()) - 1));
        return abil.empty() ? 0.0 : abil[idx].value;
    };
    auto bda_value = [&](int idx) -> double {
        idx = std::max(0, std::min(idx, static_cast<int>(bda.size()) - 1));
        return bda.empty() ? 0.0 : bda[idx].value;
    };
    auto p0_value = [&](int idx) -> double {
        return officer.has_bda ? bda_value(idx) : cap_value(idx);
    };
    // Percentage patterns
    for (const char* pat : {"{0:#,#%}", "{0:#.#%}", "{0:0,#%}", "{0:0.#%}", "{0:#%}"})
        text = replace_all_local(text, pat, fmt_pct_local(p0_value(rank_idx)));
    for (const char* pat : {"{1:#,#%}", "{1:#.#%}", "{1:0,#%}", "{1:0.#%}", "{1:#%}"})
        text = replace_all_local(text, pat, fmt_pct_local(p0_value(std::min(rank_idx + 1, std::max(0, (int)cap.size() - 1)))));
    for (const char* pat : {"{2:#,#%}", "{2:#.#%}", "{2:0,#%}", "{2:0.#%}", "{2:#%}"})
        text = replace_all_local(text, pat, fmt_pct_local(abil_value(rank_idx)));
    for (const char* pat : {"{3:#,#%}", "{3:#.#%}", "{3:0,#%}", "{3:0.#%}", "{3:#%}"})
        text = replace_all_local(text, pat, fmt_pct_local(abil_value(rank_idx)));
    for (const char* pat : {"{4:#,#%}", "{4:#.#%}", "{4:0,#%}", "{4:0.#%}", "{4:#%}"})
        text = replace_all_local(text, pat, fmt_pct_local(abil_value(rank_idx)));
    // Numeric patterns
    for (const char* pat : {"{0:#,#}", "{0:#}", "{0:0.##}", "{0:0.#}", "{0:0}"})
        text = replace_all_local(text, pat, fmt_num_local(p0_value(rank_idx)));
    for (const char* pat : {"{2:#,#}", "{2:#}", "{2:0.##}", "{2:0.#}"})
        text = replace_all_local(text, pat, fmt_num_local(abil_value(rank_idx)));
    for (const char* pat : {"{3:#,#}", "{3:#}", "{3:0.##}", "{3:0.#}"})
        text = replace_all_local(text, pat, fmt_num_local(abil_value(rank_idx)));
    for (const char* pat : {"{4:#,#}", "{4:#}", "{4:0.##}", "{4:0.#}"})
        text = replace_all_local(text, pat, fmt_num_local(abil_value(rank_idx)));
    return text;
}

// Collapse multiple whitespace chars into single spaces
static std::string collapse_ws(const std::string& s) {
    std::string out;
    bool prev_space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!prev_space) out.push_back(' ');
            prev_space = true;
        } else {
            out.push_back(c);
            prev_space = false;
        }
    }
    return out;
}

// Split tooltip text into CM and OA sections (two blocks separated by \n\n).
// For BDA officers the first block is the BDA ability, second is OA.
struct SplitDesc {
    std::string cm;   // or bda for BDA officers
    std::string oa;
};

static SplitDesc split_tooltip(const std::string& text, bool is_bda) {
    SplitDesc result;
    auto sep = text.find("\n\n");
    if (sep != std::string::npos) {
        result.cm = collapse_ws(text.substr(0, sep));
        std::string rest = text.substr(sep + 2);
        auto sep2 = rest.find("\n\n");
        if (sep2 != std::string::npos) rest = rest.substr(0, sep2);
        result.oa = collapse_ws(rest);
    } else {
        result.cm = collapse_ws(text);
    }
    return result;
}

// Determine primary role from classified tags
static std::string officer_role_tag(const ClassifiedOfficer& off) {
    if (off.is_bda()) return "below_decks";
    if (off.mining || off.mining_speed || off.mining_crystal || off.mining_gas || off.mining_ore)
        return "mining";
    if (off.armada || off.armada_solo) return "armada";
    if (off.pve_hostile || off.is_pve_specific) return "hostile";
    if (off.base_attack) return "base_attack";
    if (off.base_defend) return "base_defend";
    if (off.loot || off.loot_multiplier) return "loot";
    if (off.apex_barrier || off.apex_shred) return "apex";
    if (off.isolytic_cascade || off.isolytic_defense) return "isolytic";
    if (off.is_pvp_specific) return "pvp";
    if (off.is_dual_use) return "combat";
    if (off.stat_booster || off.ability_amplifier) return "support";
    return "general";
}

// Build a concise tags list from ClassifiedOfficer flags
static std::vector<std::string> build_tags(const ClassifiedOfficer& off) {
    std::vector<std::string> tags;
    if (off.armor_piercing)      tags.push_back("armor_pierce");
    if (off.shield_piercing)     tags.push_back("shield_pierce");
    if (off.accuracy_boost)      tags.push_back("accuracy");
    if (off.crit_related)        tags.push_back("crit");
    if (off.shield_related)      tags.push_back("shield");
    if (off.mitigation_related)  tags.push_back("mitigation");
    if (off.shots_related)       tags.push_back("extra_shots");
    if (off.stat_booster)        tags.push_back("stat_boost");
    if (off.ability_amplifier)   tags.push_back("amplifier");
    if (off.cumulative_stacking) tags.push_back("cumulative");
    if (off.apex_barrier)        tags.push_back("apex_barrier");
    if (off.apex_shred)          tags.push_back("apex_shred");
    if (off.isolytic_cascade)    tags.push_back("isolytic");
    if (off.isolytic_defense)    tags.push_back("isolytic_def");
    if (off.repair)              tags.push_back("repair");
    if (off.weapon_delay)        tags.push_back("weapon_delay");
    if (off.is_pvp_specific)     tags.push_back("pvp");
    if (off.is_pve_specific)     tags.push_back("pve");
    if (off.is_dual_use)         tags.push_back("dual_use");
    if (off.armada)              tags.push_back("armada");
    if (off.non_armada_only)     tags.push_back("non_armada_only");
    if (off.base_attack)         tags.push_back("base_atk");
    if (off.base_defend)         tags.push_back("base_def");
    if (off.loot)                tags.push_back("loot");
    if (off.loot_multiplier)     tags.push_back("loot_mult");
    if (off.mining)              tags.push_back("mining");
    if (off.mining_speed)        tags.push_back("mine_speed");
    if (off.mining_ore)          tags.push_back("mine_ore");
    if (off.mining_gas)          tags.push_back("mine_gas");
    if (off.mining_crystal)      tags.push_back("mine_crystal");
    if (off.protected_cargo)     tags.push_back("protected");
    if (off.cargo)               tags.push_back("cargo");
    if (off.is_bda())            tags.push_back("bda");
    // State chains
    for (const auto& s : off.states_applied)
        tags.push_back("applies:" + s);
    for (const auto& s : off.states_benefit)
        tags.push_back("benefits:" + s);
    // Hostile-type specializations
    if (off.hostile_swarm)   tags.push_back("vs_swarm");
    if (off.hostile_borg)    tags.push_back("vs_borg");
    if (off.hostile_eclipse) tags.push_back("vs_eclipse");
    if (off.hostile_gorn)    tags.push_back("vs_gorn");
    if (off.hostile_xindi)   tags.push_back("vs_xindi");
    if (off.hostile_silent)  tags.push_back("vs_silent");
    if (off.hostile_8472)    tags.push_back("vs_8472");
    // Armada subtypes
    if (off.armada_eclipse)  tags.push_back("arm_eclipse");
    if (off.armada_swarm)    tags.push_back("arm_swarm");
    if (off.armada_borg)     tags.push_back("arm_borg");
    return tags;
}

// Format a numeric value for display: percentage if < 10, raw integer otherwise
static std::string format_value(double val) {
    if (val == 0.0) return "";
    if (val < 10.0) {
        int pct = static_cast<int>(val * 100);
        return std::to_string(pct) + "%";
    }
    return std::to_string(static_cast<int>(val));
}

// ---------------------------------------------------------------------------
// build_officer_assessment_data — enriched with ability text from ClassifiedOfficer
// ---------------------------------------------------------------------------

nlohmann::json build_officer_assessment_data(const PlayerData& pd, const GameData& gd,
                                              const std::vector<ClassifiedOfficer>& classified) {
    // Build a quick lookup from officer_id to ClassifiedOfficer.
    // officer_id is unique per game officer, avoiding name collisions
    // (e.g., "Benjamin Sisko" exists as both DS9 and Fleet Commander versions).
    std::map<int64_t, const ClassifiedOfficer*> by_id;
    // Fallback: name-based lookup for CSV-sourced officers without IDs
    std::map<std::string, const ClassifiedOfficer*> by_name;
    for (const auto& off : classified) {
        if (off.officer_id != 0) {
            by_id[off.officer_id] = &off;
        } else {
            by_name[off.name] = &off;
        }
    }

    // Count group membership (how many officers owned per synergy group)
    std::map<std::string, int> group_counts;
    for (const auto& off : classified) {
        if (!off.group.empty()) group_counts[off.group]++;
    }

    struct Row {
        std::string name;
        int level = 0;
        int rank = 0;
        int shards = 0;
        int rarity_rank = 0;
        std::string rarity;
        std::string officer_class;
        std::string group_name;
        int group_owned = 0;     // how many group members the player owns
        int max_rank = 0;
        bool has_bda = false;
        std::string role_tag;
        // Ability descriptions
        std::string cm_desc;     // Captain's Maneuver text
        std::string oa_desc;     // Officer Ability text
        std::string bda_desc;    // Below Decks Ability text
        // Numeric ability values
        double cm_value = 0.0;
        double oa_value = 0.0;
        double oa_chance = 0.0;
        double bda_value = 0.0;
        // Synergy
        double synergy_full = 0.0;
        double synergy_half = 0.0;
        // Tags
        std::vector<std::string> tags;
    };

    std::vector<Row> selected;
    std::vector<Row> backups;

    for (const auto& po : pd.officers) {
        auto game_it = gd.officers.find(po.officer_id);
        if (game_it == gd.officers.end()) continue;
        const auto& game_off = game_it->second;

        // Resolve name
        std::string name = game_off.name.empty() ? game_off.short_name : game_off.name;

        // Find the ClassifiedOfficer for rich ability data (prefer ID match)
        const ClassifiedOfficer* co = nullptr;
        auto id_it = by_id.find(po.officer_id);
        if (id_it != by_id.end()) {
            co = id_it->second;
        } else {
            auto name_it = by_name.find(name);
            if (name_it != by_name.end()) co = name_it->second;
        }

        Row row;
        row.name = name;
        row.level = po.level;
        row.rank = po.rank;
        row.shards = po.shard_count;
        row.max_rank = game_off.max_rank;

        if (co) {
            // Use ClassifiedOfficer data (richer, has parsed tags and descriptions)
            row.rarity_rank = rarity_rank_from_char(co->rarity);
            row.rarity = rarity_from_char(co->rarity);
            row.officer_class = officer_class_str_local(co->officer_class);
            row.group_name = co->group;
            row.group_owned = co->group.empty() ? 0 : group_counts[co->group];
            row.has_bda = co->is_bda();
            row.role_tag = officer_role_tag(*co);

            // Ability descriptions — prefer resolved tooltip text (cm_text/oa_text/bda_text)
            // which has interpolated values, over bootstrap summaries (cm_description/bda_description)
            // which are often terse ("Increase Protected Cargo") or have raw placeholders.
            row.cm_desc = co->cm_text.empty() ? co->cm_description : co->cm_text;
            row.oa_desc = co->oa_text;
            row.bda_desc = co->bda_text.empty() ? co->bda_description : co->bda_text;

            // Strip HTML tags from ability text
            row.cm_desc = strip_tags(row.cm_desc);
            row.oa_desc = strip_tags(row.oa_desc);
            row.bda_desc = strip_tags(row.bda_desc);

            // Numeric values
            row.cm_value = co->cm_value;
            row.oa_value = co->oa_value;
            row.oa_chance = co->oa_chance;
            row.bda_value = co->bda_value;

            // Synergy
            row.synergy_full = co->synergy_full;
            row.synergy_half = co->synergy_half;

            // Tags
            row.tags = build_tags(*co);
        } else {
            // Fallback: use game data only (no classified data available).
            // This path is hit for officers not in the classified roster,
            // typically level-0 officers the player has shards for but hasn't unlocked.
            row.rarity_rank = game_off.rarity;
            row.rarity = rarity_str_local(game_off.rarity);
            row.officer_class = officer_class_str_local(game_off.officer_class);
            row.group_name = game_off.group_name;
            row.group_owned = game_off.group_name.empty() ? 0 : group_counts[game_off.group_name];
            row.has_bda = game_off.has_bda;
            row.role_tag = "general";

            // Resolve placeholders and split into CM/OA sections
            std::string resolved = strip_tags(resolve_tooltip_local(game_off, po.rank));
            auto parts = split_tooltip(resolved, game_off.has_bda);
            if (game_off.has_bda) {
                row.bda_desc = parts.cm;   // first block is BDA for BDA officers
            } else {
                row.cm_desc = parts.cm;
            }
            row.oa_desc = parts.oa;
        }

        // Filtering: keep officers that are likely useful
        bool keep = false;
        if (po.level >= 20 || po.rank >= 2) keep = true;
        if (row.rarity_rank >= 3) keep = true;          // Rare+
        if (row.has_bda) keep = true;
        if (!row.group_name.empty()) keep = true;
        if (row.role_tag != "general") keep = true;
        if (!row.tags.empty()) keep = true;             // has meaningful tags

        if (keep) selected.push_back(std::move(row));
        else backups.push_back(std::move(row));
    }

    // Sort: rarity desc, rank desc, level desc
    auto sort_fn = [](const Row& a, const Row& b) {
        if (a.rarity_rank != b.rarity_rank) return a.rarity_rank > b.rarity_rank;
        if (a.rank != b.rank) return a.rank > b.rank;
        if (a.level != b.level) return a.level > b.level;
        return a.name < b.name;
    };
    std::sort(selected.begin(), selected.end(), sort_fn);
    std::sort(backups.begin(), backups.end(), [](const Row& a, const Row& b) {
        if (a.rank != b.rank) return a.rank > b.rank;
        if (a.level != b.level) return a.level > b.level;
        return a.name < b.name;
    });
    // Add top backups for completeness
    for (int i = 0; i < std::min(12, (int)backups.size()); ++i)
        selected.push_back(backups[i]);

    // Serialize to JSON array
    nlohmann::json officers = nlohmann::json::array();
    for (const auto& row : selected) {
        nlohmann::json oj;
        oj["name"] = row.name;
        oj["level"] = row.level;
        oj["rank"] = row.rank;
        oj["shards"] = row.shards;
        oj["rarity"] = row.rarity;
        oj["class"] = row.officer_class;
        oj["max_rank"] = row.max_rank;
        oj["role"] = row.role_tag;

        // Synergy group with member count
        if (!row.group_name.empty()) {
            oj["group"] = row.group_name;
            oj["group_owned"] = row.group_owned;
        }

        // Ability descriptions — the core data the LLM needs for reasoning
        if (!row.cm_desc.empty()) {
            oj["cm"] = row.cm_desc;
            if (row.cm_value > 0.0)
                oj["cm_val"] = format_value(row.cm_value);
        }
        if (!row.oa_desc.empty()) {
            oj["oa"] = row.oa_desc;
            if (row.oa_value > 0.0)
                oj["oa_val"] = format_value(row.oa_value) + " at R" + std::to_string(row.rank);
            if (row.oa_chance > 0.0 && row.oa_chance < 1.0)
                oj["oa_chance"] = std::to_string(static_cast<int>(row.oa_chance * 100)) + "%";
        }
        if (row.has_bda && !row.bda_desc.empty()) {
            oj["bda"] = row.bda_desc;
            if (row.bda_value > 0.0)
                oj["bda_val"] = format_value(row.bda_value);
        }

        // Synergy values
        if (row.synergy_full > 0.0 || row.synergy_half > 0.0) {
            oj["synergy"] = std::to_string(static_cast<int>(row.synergy_full * 100)) + "%/" +
                            std::to_string(static_cast<int>(row.synergy_half * 100)) + "%";
        }

        // Tags
        if (!row.tags.empty())
            oj["tags"] = row.tags;

        officers.push_back(std::move(oj));
    }
    return officers;
}

// ---------------------------------------------------------------------------
// build_officer_assessment_request
// ---------------------------------------------------------------------------

LlmRequest build_officer_assessment_request(const PlayerData& pd, const GameData& gd,
                                             const std::vector<ClassifiedOfficer>& classified) {
    LlmRequest req;
    req.system_prompt = R"(You are an STFC officer planning analyst. Your job is to evaluate the player's owned officers and produce an overlap-aware crew matrix across combat, armada, loot, mining, and progression needs.

CREW MECHANICS:
- Each ship has 3 officer seats: 1 Captain + 2 Bridge officers.
- Captain seat: that officer's Captain Maneuver (CM) fires ONCE at battle start. Pick officers whose CM is a powerful opening burst or debuff.
- Bridge seats: those officers' Officer Abilities (OA) are PASSIVE during the entire battle. Their CM does NOT fire. Pick officers whose OA provides sustained combat value.
- Below Decks (BDA): passive bonus only. Only officers marked "bda" in tags can go here.
- Synergy: officers in the same group get bonus stats when crewed together. The "group" field shows the group name, "group_owned" shows how many group members the player owns. Full synergy (3/3) is much stronger than partial (2/3).

STATE CHAINS:
- State chains are when one officer applies a state (burning, morale, breach, assimilate, isolytic) via CM, and another officer's OA benefits from that state. Look for "applies:X" and "benefits:X" in the tags.
- Captain should APPLY the state via CM (fires once), bridge should BENEFIT from it via OA (always active).

DATA FORMAT:
- cm = Captain's Maneuver description, cm_val = CM effect magnitude
- oa = Officer Ability description, oa_val = OA effect at current rank, oa_chance = proc probability
- bda = Below Decks Ability description, bda_val = BDA effect magnitude
- group = synergy group, group_owned = number of group members the player owns
- tags = classification tags (pvp, pve, armada, crit, armor_pierce, applies:burning, benefits:burning, etc.)

EVALUATION RULES:
- Use ONLY the officers in the provided data.
- CRITICAL: Your training data about STFC officer abilities is OUTDATED and UNRELIABLE. Officers are frequently reworked, renamed, moved to different synergy groups, or given completely different abilities. You MUST base all reasoning SOLELY on the cm, oa, bda, group, and tags fields provided in the data. Do NOT rely on your prior knowledge of what an officer does.
- When explaining why an officer is chosen, QUOTE the actual ability text from the data to prove your reasoning.
- Do not pretend there is a single flat "best PvP crew". Different contexts need different options.
- Overlap matters: if an officer is core in multiple roles, provide multiple viable crew options because the same officer cannot be used everywhere at once.
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
    req.max_tokens = 8192;

    auto officers = build_officer_assessment_data(pd, gd, classified);
    std::ostringstream user;
    user << "### DATA: ACCOUNT SNAPSHOT\n";
    user << "- Ops Level: " << pd.ops_level << "\n";
    user << "- Owned Officer Count: " << pd.officers.size() << "\n\n";
    user << "### DATA: PRUNED OWNED OFFICERS\n";
    user << officers.dump(2) << "\n\n";
    user << "### TASK\n";
    user << "Build an overlap-aware crew matrix from the player's owned officers only. "
         << "Use the CM, OA, and BDA ability descriptions to reason about which officers "
         << "synergize best as captain vs bridge. Pay attention to state chains (applies/benefits tags), "
         << "synergy groups (especially full 3/3 groups), and overlap conflicts. "
         << "Return multiple viable crew options for PvP (general + ship-type matchups), "
         << "PvE (general hostiles + loot variants + specialized NPCs + mission bosses), "
         << "armadas (power + loot, group + solo), and mining (at least 3 simultaneous crews "
         << "plus speed, protected cargo, and resource-specific variants).\n";
    req.user_prompt = user.str();
    req.response_schema = R"({"type":"object","properties":{"pvp":{"type":"object","properties":{"general":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"vs_explorer":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"vs_interceptor":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"vs_battleship":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"anti_meta":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]}},"required":["general","vs_explorer","vs_interceptor","vs_battleship","anti_meta"]},"pve":{"type":"object","properties":{"general_hostiles":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"loot_hostiles":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"top_npcs":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"mission_boss":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]}},"required":["general_hostiles","loot_hostiles","top_npcs","mission_boss"]},"armadas":{"type":"object","properties":{"power":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"loot":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"solo_power":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"solo_loot":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]}},"required":["power","loot","solo_power","solo_loot"]},"mining":{"type":"object","properties":{"parallel_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}},"speed":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"protected_cargo":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"ore":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"gas":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"crystal":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]},"general":{"type":"object","properties":{"recommended_crews":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"captain":{"type":"string"},"bridge":{"type":"array","items":{"type":"string"}},"use_case":{"type":"string"},"reason":{"type":"string"},"substitutes":{"type":"array","items":{"type":"string"}}},"required":["priority","captain","bridge","use_case","reason","substitutes"]}}},"required":["recommended_crews"]}},"required":["parallel_crews","speed","protected_cargo","ore","gas","crystal","general"]},"below_decks_priority":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"},"priority":{"type":"string","enum":["high","medium","low"]}},"required":["name","reason","priority"]}},"investment_targets":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"},"priority":{"type":"string","enum":["high","medium","low"]}},"required":["name","reason","priority"]}},"overlap_risks":{"type":"array","items":{"type":"object","properties":{"officer":{"type":"string"},"used_in":{"type":"array","items":{"type":"string"}},"backup_needed_for":{"type":"string"}},"required":["officer","used_in","backup_needed_for"]}},"summary":{"type":"string"},"limitations":{"type":"string"}},"required":["pvp","pve","armadas","mining","below_decks_priority","investment_targets","overlap_risks","summary","limitations"]})";
    return req;
}

} // namespace stfc
