#include "core/account_state.h"

#include <algorithm>
#include <sstream>
#include <cmath>

#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ---------------------------------------------------------------------------
// Helpers: extract tags from a ClassifiedOfficer for compact representation
// ---------------------------------------------------------------------------

static std::vector<std::string> extract_tags(const ClassifiedOfficer& off, Scenario scenario) {
    std::vector<std::string> tags;

    // Combat meta tags
    if (off.armor_piercing)      tags.push_back("armor_piercing");
    if (off.shield_piercing)     tags.push_back("shield_piercing");
    if (off.accuracy_boost)      tags.push_back("accuracy");
    if (off.crit_related)        tags.push_back("crit");
    if (off.shield_related)      tags.push_back("shield");
    if (off.mitigation_related)  tags.push_back("mitigation");
    if (off.shots_related)       tags.push_back("extra_shots");
    if (off.weapon_delay)        tags.push_back("weapon_delay");
    if (off.ability_amplifier)   tags.push_back("amplifier");
    if (off.stat_booster)        tags.push_back("stat_boost");
    if (off.isolytic_cascade)    tags.push_back("isolytic");
    if (off.isolytic_defense)    tags.push_back("isolytic_defense");
    if (off.apex_barrier)        tags.push_back("apex_barrier");
    if (off.apex_shred)          tags.push_back("apex_shred");
    if (off.cumulative_stacking) tags.push_back("cumulative");

    // Scenario affinity
    if (off.is_pvp_specific)     tags.push_back("pvp_only");
    if (off.is_pve_specific)     tags.push_back("pve_only");
    if (off.is_dual_use)         tags.push_back("dual_use");
    if (off.base_attack)         tags.push_back("base_attack");
    if (off.base_defend)         tags.push_back("base_defend");
    if (off.pve_hostile)         tags.push_back("pve");
    if (off.mission_boss)        tags.push_back("boss");
    if (off.armada)              tags.push_back("armada");
    if (off.loot)                tags.push_back("loot");

    // Mining tags (only include when scenario is mining-related)
    bool is_mining = (scenario == Scenario::MiningSpeed ||
                      scenario == Scenario::MiningProtected ||
                      scenario == Scenario::MiningCrystal ||
                      scenario == Scenario::MiningGas ||
                      scenario == Scenario::MiningOre ||
                      scenario == Scenario::MiningGeneral);
    if (is_mining) {
        if (off.mining)           tags.push_back("mining");
        if (off.mining_speed)     tags.push_back("mine_speed");
        if (off.mining_gas)       tags.push_back("mine_gas");
        if (off.mining_ore)       tags.push_back("mine_ore");
        if (off.mining_crystal)   tags.push_back("mine_crystal");
        if (off.protected_cargo)  tags.push_back("protected");
        if (off.cargo)            tags.push_back("cargo");
        if (off.node_defense)     tags.push_back("node_def");
    }

    // States applied/benefited
    for (const auto& s : off.states_applied) {
        tags.push_back("applies:" + s);
    }
    for (const auto& s : off.states_benefit) {
        tags.push_back("benefits:" + s);
    }

    // Hostile-specific
    if (off.hostile_swarm)       tags.push_back("vs_swarm");
    if (off.hostile_borg)        tags.push_back("vs_borg");
    if (off.hostile_eclipse)     tags.push_back("vs_eclipse");
    if (off.hostile_gorn)        tags.push_back("vs_gorn");
    if (off.hostile_xindi)       tags.push_back("vs_xindi");
    if (off.hostile_silent)      tags.push_back("vs_silent");
    if (off.hostile_8472)        tags.push_back("vs_8472");
    if (off.hostile_breen)       tags.push_back("vs_breen");
    if (off.hostile_hirogen)     tags.push_back("vs_hirogen");
    if (off.hostile_actian)      tags.push_back("vs_actian");

    // BDA indicator
    if (off.is_bda())            tags.push_back("bda");

    return tags;
}

// ---------------------------------------------------------------------------
// Lightweight relevance scoring for pre-filtering
//
// This is NOT the full scoring engine — just enough to identify the ~40 most
// relevant officers for a given scenario. The AI will do the real analysis.
// ---------------------------------------------------------------------------

static double relevance_score(const ClassifiedOfficer& off, Scenario scenario) {
    double score = 0.0;

    // Base stat contribution (normalized, light weight)
    score += (off.attack + off.defense + off.health) * 0.001;

    // Level/rank contribution
    score += off.level * 10.0;
    score += off.rank * 500.0;

    // Rarity bonus
    switch (off.rarity) {
        case 'E': score += 2000.0; break;
        case 'R': score += 1000.0; break;
        case 'U': score += 500.0;  break;
        default: break;
    }

    // Scenario-specific relevance
    switch (scenario) {
        case Scenario::PvP:
        case Scenario::Hybrid:
            if (off.is_pvp_specific || off.is_dual_use)  score += 5000.0;
            if (off.armor_piercing)      score += 3000.0;
            if (off.shield_piercing)     score += 3000.0;
            if (off.crit_related)        score += 2000.0;
            if (off.isolytic_cascade)    score += 3000.0;
            if (off.apex_shred)          score += 3000.0;
            if (off.apex_barrier)        score += 2500.0;
            if (off.ability_amplifier)   score += 2500.0;
            if (off.stat_booster)        score += 2000.0;
            if (off.cumulative_stacking) score += 2000.0;
            if (!off.states_applied.empty()) score += 2000.0;
            if (off.mining && !off.is_dual_use) score -= 8000.0;
            break;

        case Scenario::BaseCracker:
            if (off.base_attack)         score += 8000.0;
            if (off.is_pvp_specific || off.is_dual_use)  score += 3000.0;
            if (off.armor_piercing)      score += 2000.0;
            if (off.mining && !off.is_dual_use) score -= 10000.0;
            break;

        case Scenario::PvEHostile:
            if (off.pve_hostile)         score += 6000.0;
            if (off.is_pve_specific || off.is_dual_use)  score += 3000.0;
            if (off.crit_related)        score += 2000.0;
            if (off.loot)               score += 1000.0;
            if (off.mining && !off.is_dual_use) score -= 8000.0;
            break;

        case Scenario::MissionBoss:
            if (off.mission_boss)        score += 6000.0;
            if (off.pve_hostile)         score += 3000.0;
            if (off.crit_related)        score += 2000.0;
            if (off.mining && !off.is_dual_use) score -= 8000.0;
            break;

        case Scenario::Loot:
            if (off.loot)               score += 8000.0;
            if (off.cargo)              score += 5000.0;
            if (off.mining)             score += 3000.0;
            if (off.loot_multiplier)    score += 4000.0;
            break;

        case Scenario::Armada:
            if (off.armada)             score += 8000.0;
            if (off.non_armada_only)    score -= 15000.0;
            if (off.mining && !off.is_dual_use) score -= 8000.0;
            break;

        case Scenario::MiningSpeed:
            if (off.mining_speed)       score += 10000.0;
            if (off.mining)             score += 5000.0;
            if (!off.mining && !off.cargo) score -= 8000.0;
            break;

        case Scenario::MiningProtected:
            if (off.protected_cargo)    score += 10000.0;
            if (off.cargo)             score += 5000.0;
            if (off.mining)            score += 3000.0;
            if (!off.mining && !off.cargo && !off.protected_cargo) score -= 8000.0;
            break;

        case Scenario::MiningCrystal:
            if (off.mining_crystal)     score += 10000.0;
            if (off.mining_speed)       score += 3000.0;
            if (off.mining)            score += 3000.0;
            if (!off.mining && !off.cargo) score -= 8000.0;
            break;

        case Scenario::MiningGas:
            if (off.mining_gas)         score += 10000.0;
            if (off.mining_speed)       score += 3000.0;
            if (off.mining)            score += 3000.0;
            if (!off.mining && !off.cargo) score -= 8000.0;
            break;

        case Scenario::MiningOre:
            if (off.mining_ore)         score += 10000.0;
            if (off.mining_speed)       score += 3000.0;
            if (off.mining)            score += 3000.0;
            if (!off.mining && !off.cargo) score -= 8000.0;
            break;

        case Scenario::MiningGeneral:
            if (off.mining)            score += 6000.0;
            if (off.mining_speed)      score += 3000.0;
            if (off.protected_cargo)   score += 2000.0;
            if (off.cargo)            score += 2000.0;
            if (!off.mining && !off.cargo) score -= 8000.0;
            break;
    }

    // Synergy group presence is valuable (AI can reason about combos)
    if (!off.group.empty()) score += 500.0;

    // High CM% officers are always interesting for captain seat
    if (off.cm_pct >= 40.0 && !off.is_bda()) score += 1500.0;

    return score;
}

// ---------------------------------------------------------------------------
// Convert ClassifiedOfficer → CompactOfficer
// ---------------------------------------------------------------------------

static CompactOfficer to_compact(const ClassifiedOfficer& off, Scenario scenario) {
    CompactOfficer co;
    co.name = off.name;
    co.officer_class = officer_class_str(off.officer_class);
    switch (off.rarity) {
        case 'C': co.rarity = "Common";   break;
        case 'U': co.rarity = "Uncommon"; break;
        case 'R': co.rarity = "Rare";     break;
        case 'E': co.rarity = "Epic";     break;
        default:  co.rarity = "Unknown";  break;
    }
    co.level = off.level;
    co.rank = off.rank;
    co.attack = off.attack;
    co.defense = off.defense;
    co.health = off.health;
    co.group = off.group;
    co.captain_maneuver = off.cm_text;
    co.officer_ability = off.oa_text;
    co.below_decks = off.bda_text;
    co.cm_pct = off.cm_pct;
    co.oa_pct = off.oa_pct;
    co.tags = extract_tags(off, scenario);
    return co;
}

// ---------------------------------------------------------------------------
// Build the account snapshot
// ---------------------------------------------------------------------------

AccountSnapshot build_account_snapshot(
    const PlayerData& player_data,
    const GameData& game_data,
    const std::vector<ClassifiedOfficer>& classified_officers,
    Scenario scenario,
    ShipType ship_type,
    int top_n,
    const std::set<std::string>& excluded)
{
    AccountSnapshot snap;

    // Player context
    snap.player_name = player_data.player_name;
    snap.ops_level = player_data.ops_level;
    snap.scenario = scenario_str(scenario);
    snap.ship_type = ship_type_str(ship_type);
    snap.excluded = excluded;
    snap.total_officers_owned = static_cast<int>(player_data.officers.size());
    snap.total_ships_owned = static_cast<int>(player_data.ships.size());

    // -----------------------------------------------------------------------
    // Pre-filter officers by scenario relevance
    // -----------------------------------------------------------------------
    struct ScoredOfficer {
        const ClassifiedOfficer* officer;
        double score;
    };

    std::vector<ScoredOfficer> scored;
    scored.reserve(classified_officers.size());

    for (const auto& off : classified_officers) {
        // Skip excluded officers (already assigned to other docks)
        if (excluded.count(off.name)) continue;

        double s = relevance_score(off, scenario);
        scored.push_back({&off, s});
    }

    // Sort by relevance descending
    std::sort(scored.begin(), scored.end(),
              [](const ScoredOfficer& a, const ScoredOfficer& b) {
                  return a.score > b.score;
              });

    // Take top_n
    int count = std::min(top_n, static_cast<int>(scored.size()));

    // Also rescue any officers with synergy groups that appear in top_n
    // (same idea as the crew optimizer's synergy reserve expansion)
    std::set<std::string> top_groups;
    for (int i = 0; i < count; ++i) {
        if (!scored[i].officer->group.empty()) {
            top_groups.insert(scored[i].officer->group);
        }
    }

    std::set<std::string> included_names;
    for (int i = 0; i < count; ++i) {
        snap.officers.push_back(to_compact(*scored[i].officer, scenario));
        included_names.insert(scored[i].officer->name);
    }

    // Synergy rescue: add officers outside top_n that share synergy groups
    int max_total = top_n + 5;  // hard cap — keep it tight for token budget
    for (int i = count; i < static_cast<int>(scored.size()) && 
         static_cast<int>(snap.officers.size()) < max_total; ++i) {
        const auto& off = *scored[i].officer;
        if (!off.group.empty() && top_groups.count(off.group) &&
            !included_names.count(off.name)) {
            snap.officers.push_back(to_compact(off, scenario));
            included_names.insert(off.name);
        }
    }

    // -----------------------------------------------------------------------
    // Ships
    // -----------------------------------------------------------------------
    for (const auto& ps : player_data.ships) {
        CompactShip cs;
        auto it = game_data.ships.find(ps.hull_id);
        if (it != game_data.ships.end()) {
            cs.name = it->second.name.empty() ? ps.name : it->second.name;
            cs.hull_type = hull_type_str(it->second.hull_type);
        } else {
            cs.name = ps.name;
            cs.hull_type = "Unknown";
        }
        cs.tier = ps.tier;
        cs.level = ps.level;
        snap.ships.push_back(cs);
    }

    // -----------------------------------------------------------------------
    // Forbidden tech
    // -----------------------------------------------------------------------
    for (const auto& pt : player_data.techs) {
        CompactTech ct;
        ct.tech_id = pt.tech_id;
        ct.tier = pt.tier;
        ct.level = pt.level;
        snap.forbidden_tech.push_back(ct);
    }

    return snap;
}

// ---------------------------------------------------------------------------
// Helper: truncate a string to max_len chars, appending "..." if truncated
// ---------------------------------------------------------------------------

static std::string truncate(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "...";
}

// ---------------------------------------------------------------------------
// JSON serialization — compact format for LLM prompts
//
// Key abbreviations to save tokens:
//   n=name, c=class, r=rarity, rk=rank, l=level
//   g=group, cm=captain_maneuver, oa=officer_ability, bd=below_decks
//   t=tags
//
// SnapshotJsonOptions controls what's included:
//   minimal()  — officers only, no ships/tech (Crew Recs, META)
//   full()     — officers + ships + tech + verbose (Progression)
//   overview() — officers + top ships, verbose (Ask)
// ---------------------------------------------------------------------------

std::string snapshot_to_json(const AccountSnapshot& snap) {
    return snapshot_to_json(snap, SnapshotJsonOptions::minimal());
}

std::string snapshot_to_json(const AccountSnapshot& snap, const SnapshotJsonOptions& opts) {
    json j;

    // Player context
    j["scenario"] = snap.scenario;
    j["ship_type"] = snap.ship_type;

    if (opts.verbose_officers) {
        j["ops_level"] = snap.ops_level;
        j["total_officers"] = snap.total_officers_owned;
    }

    // Officers — ultra-compact keys
    json officers = json::array();
    for (const auto& o : snap.officers) {
        json oj;
        oj["n"] = o.name;
        oj["rk"] = o.rank;

        if (opts.verbose_officers) {
            oj["l"] = o.level;
            oj["c"] = o.officer_class;
            oj["r"] = o.rarity;
        }

        if (!o.group.empty()) oj["g"] = o.group;

        // Abilities — truncated to save tokens
        if (!o.captain_maneuver.empty()) oj["cm"] = truncate(o.captain_maneuver, opts.ability_truncate);
        if (!o.officer_ability.empty())  oj["oa"] = truncate(o.officer_ability, opts.ability_truncate);
        if (!o.below_decks.empty())      oj["bd"] = truncate(o.below_decks, opts.ability_truncate);

        // Tags — the most important signal for the LLM
        if (!o.tags.empty()) oj["t"] = o.tags;

        officers.push_back(oj);
    }
    j["officers"] = officers;

    // Ships — top N by tier (for Progression / Ask modes)
    if (opts.include_ships && !snap.ships.empty()) {
        // Sort a copy by tier descending, level descending
        auto sorted_ships = snap.ships;
        std::sort(sorted_ships.begin(), sorted_ships.end(),
                  [](const CompactShip& a, const CompactShip& b) {
                      if (a.tier != b.tier) return a.tier > b.tier;
                      return a.level > b.level;
                  });

        int ship_count = std::min(opts.max_ships, static_cast<int>(sorted_ships.size()));
        json ships = json::array();
        for (int i = 0; i < ship_count; ++i) {
            const auto& s = sorted_ships[i];
            json sj;
            sj["n"] = s.name;
            sj["type"] = s.hull_type;
            sj["tier"] = s.tier;
            sj["lvl"] = s.level;
            ships.push_back(sj);
        }
        j["ships"] = ships;
        j["total_ships"] = snap.total_ships_owned;
    }

    // Forbidden tech (for Progression mode)
    if (opts.include_tech && !snap.forbidden_tech.empty()) {
        json techs = json::array();
        for (const auto& t : snap.forbidden_tech) {
            json tj;
            tj["id"] = t.tech_id;
            tj["tier"] = t.tier;
            tj["lvl"] = t.level;
            techs.push_back(tj);
        }
        j["forbidden_tech"] = techs;
    }

    // Excluded officers
    if (!snap.excluded.empty()) {
        j["excluded"] = std::vector<std::string>(snap.excluded.begin(), snap.excluded.end());
    }

    return j.dump();  // No indentation — compact for LLM
}

// ---------------------------------------------------------------------------
// Human-readable summary (for debugging / display in TUI)
// ---------------------------------------------------------------------------

std::string snapshot_to_summary(const AccountSnapshot& snap) {
    std::ostringstream os;

    os << "Account: " << snap.player_name << " (Ops " << snap.ops_level << ")\n";
    os << "Scenario: " << snap.scenario << " | Ship: " << snap.ship_type << "\n";
    os << "Officers: " << snap.officers.size() << " selected / "
       << snap.total_officers_owned << " owned\n";
    os << "Ships: " << snap.total_ships_owned << " | Tech: "
       << snap.forbidden_tech.size() << " entries\n";

    if (!snap.excluded.empty()) {
        os << "Excluded: ";
        for (const auto& e : snap.excluded) os << e << ", ";
        os << "\n";
    }

    os << "\n--- Top Officers ---\n";
    for (const auto& o : snap.officers) {
        os << "  " << o.name << " [" << o.rarity[0] << "/" << o.officer_class[0]
           << "] L" << o.level << " R" << o.rank;
        if (!o.group.empty()) os << " (" << o.group << ")";
        if (!o.tags.empty()) {
            os << " {";
            for (size_t i = 0; i < o.tags.size(); ++i) {
                if (i > 0) os << ",";
                os << o.tags[i];
            }
            os << "}";
        }
        os << "\n";
    }

    if (!snap.ships.empty()) {
        os << "\n--- Ships ---\n";
        for (const auto& s : snap.ships) {
            os << "  " << s.name << " [" << s.hull_type << "] T" << s.tier
               << " L" << s.level << "\n";
        }
    }

    return os.str();
}

} // namespace stfc
