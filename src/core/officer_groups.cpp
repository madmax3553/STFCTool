#include "core/officer_groups.h"

#include <algorithm>
#include <sstream>
#include <set>

#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ===========================================================================
// Group ID <-> string conversions
// ===========================================================================

std::string group_id_str(OfficerGroupId id) {
    switch (id) {
        case OfficerGroupId::PvP_Combat:    return "PvP Combat";
        case OfficerGroupId::PvE_Hostile:   return "PvE Hostile";
        case OfficerGroupId::Base_Attack:   return "Base Attack";
        case OfficerGroupId::Base_Defend:   return "Base Defend";
        case OfficerGroupId::Armada:        return "Armada";
        case OfficerGroupId::Mining:        return "Mining";
        case OfficerGroupId::Loot_Cargo:    return "Loot & Cargo";
        case OfficerGroupId::State_Chain:   return "State Chain";
        case OfficerGroupId::Apex_Isolytic: return "Apex & Isolytic";
        case OfficerGroupId::Support:       return "Support";
    }
    return "Unknown";
}

OfficerGroupId group_id_from_str(const std::string& s) {
    if (s == "PvP Combat")      return OfficerGroupId::PvP_Combat;
    if (s == "PvE Hostile")     return OfficerGroupId::PvE_Hostile;
    if (s == "Base Attack")     return OfficerGroupId::Base_Attack;
    if (s == "Base Defend")     return OfficerGroupId::Base_Defend;
    if (s == "Armada")          return OfficerGroupId::Armada;
    if (s == "Mining")          return OfficerGroupId::Mining;
    if (s == "Loot & Cargo")    return OfficerGroupId::Loot_Cargo;
    if (s == "State Chain")     return OfficerGroupId::State_Chain;
    if (s == "Apex & Isolytic") return OfficerGroupId::Apex_Isolytic;
    if (s == "Support")         return OfficerGroupId::Support;
    return OfficerGroupId::Support;
}

// ===========================================================================
// Group officers by classification tags
// ===========================================================================

std::vector<OfficerGroup> group_officers(
    const std::vector<ClassifiedOfficer>& officers,
    int min_group_size)
{
    // Initialize all groups
    std::map<OfficerGroupId, OfficerGroup> groups;

    auto init_group = [&](OfficerGroupId id, const std::string& desc, const std::string& guidance) {
        OfficerGroup g;
        g.id = id;
        g.name = group_id_str(id);
        g.description = desc;
        g.prompt_guidance = guidance;
        groups[id] = std::move(g);
    };

    init_group(OfficerGroupId::PvP_Combat,
        "Officers specializing in player-vs-player combat",
        "Focus on: armor/shield piercing, critical hits, damage bursts, "
        "stat boosters, ability amplifiers. Captain CM should deliver a powerful opening "
        "strike or critical debuff. Bridge OA should sustain damage output or defensive advantage.");

    init_group(OfficerGroupId::PvE_Hostile,
        "Officers effective against hostile NPCs",
        "Focus on: sustained damage, survivability, crit damage, extra shots, "
        "hull repair/shield regen. Captain CM should be a big damage opener. "
        "Bridge OA should keep damage output high through long fights. "
        "Note hostile-type tags (vs_swarm, vs_borg, etc.) for specialized crews.");

    init_group(OfficerGroupId::Base_Attack,
        "Officers for attacking player starbases",
        "Focus on: maximum burst damage, armor piercing, shield piercing. "
        "Base attacks are short — overwhelming firepower wins. "
        "Captain CM should deal massive opening damage.");

    init_group(OfficerGroupId::Base_Defend,
        "Officers for defending your starbase",
        "Focus on: damage mitigation, shield repair, hull repair, "
        "sustained defense over multiple rounds. Defender advantage means "
        "survivability matters more than burst.");

    init_group(OfficerGroupId::Armada,
        "Officers for armada battles",
        "Focus on: officers tagged 'armada' get bonuses in armadas. "
        "Armadas are long coordinated fights — sustained damage and survivability are key. "
        "NEVER use officers tagged 'non_armada_only'. "
        "Note armada-type tags (armada_eclipse, armada_swarm, armada_borg) for specialized crews.");

    init_group(OfficerGroupId::Mining,
        "Officers for mining operations",
        "Focus on: mining speed, resource yield (ore/gas/crystal), protected cargo, "
        "cargo capacity. Combat stats matter less here. Match mining subcategory "
        "(mine_speed, mine_ore, mine_gas, mine_crystal, protected) to the mining goal.");

    init_group(OfficerGroupId::Loot_Cargo,
        "Officers that increase loot drops and cargo",
        "Focus on: loot multipliers, cargo capacity, reputation boosts, XP boosts. "
        "These officers maximize rewards from hostile kills and missions. "
        "Kill speed also matters for efficient farming.");

    init_group(OfficerGroupId::State_Chain,
        "Officers that form state chains (burning, morale, breach, assimilate, isolytic)",
        "Focus on: state application and state benefit combos. "
        "A state chain is when one officer applies a state (e.g., burning via CM) "
        "and another officer's ability triggers off that state (e.g., OA does extra damage "
        "when target is burning). Captain should APPLY the state via CM, bridge should BENEFIT from it via OA. "
        "Match 'applies:X' tags with 'benefits:X' tags.");

    init_group(OfficerGroupId::Apex_Isolytic,
        "Officers with apex barrier/shred or isolytic cascade/defense",
        "Focus on: the Rock-Paper-Scissors META — apex barrier absorbs damage, "
        "apex shred strips barriers, isolytic cascade bypasses standard defense, "
        "isolytic defense reduces isolytic damage. These are the PvP META endgame mechanics. "
        "Understanding which counter applies is critical for crew composition.");

    init_group(OfficerGroupId::Support,
        "Utility officers: stat boosters, amplifiers, and miscellaneous",
        "These officers don't fit a single scenario but provide valuable support: "
        "stat boosts, ability amplification, cumulative stacking effects, weapon delay, "
        "repair, warp speed. They often fill bridge/BDA slots in crews from other groups.");

    // ---------------------------------------------------------------
    // EXCLUSIVE assignment: each officer goes to ONE primary group.
    // Priority order: most-specific tags first, broad tags last.
    // This prevents 225/289 officers landing in PvP Combat.
    // ---------------------------------------------------------------
    for (const auto& off : officers) {
        bool assigned = false;

        // 1. Mining — very specific, handled locally, skip AI
        if (!assigned && (off.mining || off.mining_speed ||
            off.mining_crystal || off.mining_gas || off.mining_ore ||
            off.protected_cargo)) {
            groups[OfficerGroupId::Mining].officers.push_back(&off);
            assigned = true;
        }

        // 2. Apex & Isolytic — very specific endgame META
        if (!assigned && (off.apex_barrier || off.apex_shred ||
            off.isolytic_cascade || off.isolytic_defense)) {
            groups[OfficerGroupId::Apex_Isolytic].officers.push_back(&off);
            assigned = true;
        }

        // 3. Loot & Cargo — specific niche
        if (!assigned && (off.loot || off.loot_multiplier ||
            off.rep_boost || off.xp_boost)) {
            groups[OfficerGroupId::Loot_Cargo].officers.push_back(&off);
            assigned = true;
        }

        // 4. Base Attack
        if (!assigned && off.base_attack) {
            groups[OfficerGroupId::Base_Attack].officers.push_back(&off);
            assigned = true;
        }

        // 5. Base Defend
        if (!assigned && off.base_defend) {
            groups[OfficerGroupId::Base_Defend].officers.push_back(&off);
            assigned = true;
        }

        // 6. Armada — specific tag
        if (!assigned && (off.armada || off.armada_solo ||
            off.armada_eclipse || off.armada_swarm || off.armada_borg) &&
            !off.non_armada_only) {
            groups[OfficerGroupId::Armada].officers.push_back(&off);
            assigned = true;
        }

        // 7. State Chain — officers with explicit state apply/benefit tags
        if (!assigned && (!off.states_applied.empty() || !off.states_benefit.empty())) {
            groups[OfficerGroupId::State_Chain].officers.push_back(&off);
            assigned = true;
        }

        // 8. PvE Hostile — specific PvE tags
        if (!assigned && (off.pve_hostile || off.is_pve_specific || off.mission_boss ||
            off.hostile_swarm || off.hostile_borg || off.hostile_eclipse ||
            off.hostile_gorn || off.hostile_xindi || off.hostile_silent || off.hostile_8472)) {
            groups[OfficerGroupId::PvE_Hostile].officers.push_back(&off);
            assigned = true;
        }

        // 9. PvP Combat — only pvp_specific, NOT dual_use/crit which are too broad
        if (!assigned && off.is_pvp_specific) {
            groups[OfficerGroupId::PvP_Combat].officers.push_back(&off);
            assigned = true;
        }

        // 10. Support — stat boosters, amplifiers, etc.
        if (!assigned && (off.stat_booster || off.ability_amplifier ||
            off.cumulative_stacking || off.weapon_delay || off.repair ||
            off.shots_related || off.is_dual_use || off.crit_related ||
            off.armor_piercing || off.shield_piercing)) {
            groups[OfficerGroupId::Support].officers.push_back(&off);
            assigned = true;
        }

        // 11. Unassigned high-rank officers → Support
        if (!assigned && (off.rank >= 3 || off.rarity == 'E' || off.rarity == 'R')) {
            groups[OfficerGroupId::Support].officers.push_back(&off);
        }
        // Low-rank untagged officers are dropped entirely — not useful for AI
    }

    // Merge small groups into Support
    auto& support = groups[OfficerGroupId::Support];
    for (auto& [id, group] : groups) {
        if (id == OfficerGroupId::Support) continue;
        if (id == OfficerGroupId::Mining) continue;  // Keep mining separate even if small
        if (static_cast<int>(group.officers.size()) < min_group_size && !group.empty()) {
            for (const auto* off : group.officers) {
                support.officers.push_back(off);
            }
            group.officers.clear();
        }
    }

    // Deduplicate within each group (an officer can match multiple criteria)
    for (auto& [id, group] : groups) {
        std::set<std::string> seen;
        auto& v = group.officers;
        v.erase(std::remove_if(v.begin(), v.end(),
            [&](const ClassifiedOfficer* off) {
                if (seen.count(off->name)) return true;
                seen.insert(off->name);
                return false;
            }), v.end());

        // Sort by rank desc, then rarity desc (most relevant first)
        std::sort(v.begin(), v.end(),
            [](const ClassifiedOfficer* a, const ClassifiedOfficer* b) {
                if (a->rank != b->rank) return a->rank > b->rank;
                return a->rarity > b->rarity;
            });

        // No hard cap — the natural filtering chain (META → owned → leveled)
        // produces small groups organically. This tag-based grouping is the
        // fallback when no META cache exists; exclusive assignment already
        // keeps groups manageable.
    }

    // Collect non-empty groups into result vector
    std::vector<OfficerGroup> result;
    // Ordered iteration
    static const OfficerGroupId order[] = {
        OfficerGroupId::PvP_Combat,
        OfficerGroupId::PvE_Hostile,
        OfficerGroupId::Base_Attack,
        OfficerGroupId::Base_Defend,
        OfficerGroupId::Armada,
        OfficerGroupId::State_Chain,
        OfficerGroupId::Apex_Isolytic,
        OfficerGroupId::Loot_Cargo,
        OfficerGroupId::Mining,
        OfficerGroupId::Support,
    };

    for (auto id : order) {
        auto& g = groups[id];
        if (!g.empty()) {
            result.push_back(std::move(g));
        }
    }

    return result;
}

// ===========================================================================
// Focused system prompts per group
// ===========================================================================

std::string group_system_prompt(OfficerGroupId group_id) {
    std::ostringstream ss;

    // Common preamble — much shorter than the monolithic prompt
    ss << "You are an expert STFC (Star Trek Fleet Command) crew advisor. "
       << "This is a MOBILE GAME by Scopely.\n\n"
       << "CREW MECHANICS:\n"
       << "- 1 Captain (CM fires ONCE at battle start) + 2 Bridge (OA always active) + optional Below Decks (BDA passive).\n"
       << "- Synergy: officers in the same group ('g' field) get bonus stats together.\n"
       << "- State chains: one officer applies a state via CM, another benefits via OA. Very powerful.\n\n"
       << "DATA FORMAT: n=name, rk=rank, g=group, cm=captain_maneuver, oa=officer_ability, bd=below_decks, t=tags\n\n"
       << "RULES:\n"
       << "1. ONLY use officer names from the provided list (these are OWNED officers).\n"
       << "2. Each crew = 1 captain + 2 bridge. No repeats across crews.\n"
       << "3. For each crew: explain WHY this captain (CM effect) and WHY each bridge (OA effect).\n\n";

    // Group-specific focus
    switch (group_id) {
        case OfficerGroupId::PvP_Combat:
            ss << "FOCUS: PvP COMBAT crews. Prioritize armor/shield piercing, crits, damage bursts, stat boosters.\n";
            break;
        case OfficerGroupId::PvE_Hostile:
            ss << "FOCUS: PvE HOSTILE GRINDING crews. Prioritize sustained damage, survivability, crit, extra shots.\n"
               << "Note hostile-type tags (vs_swarm, vs_borg, etc.) for specialized anti-hostile crews.\n";
            break;
        case OfficerGroupId::Base_Attack:
            ss << "FOCUS: BASE ATTACK crews. Maximum burst damage, armor piercing. Short fights = overwhelming firepower.\n";
            break;
        case OfficerGroupId::Base_Defend:
            ss << "FOCUS: BASE DEFENSE crews. Damage mitigation, shields, repair, sustained defense.\n";
            break;
        case OfficerGroupId::Armada:
            ss << "FOCUS: ARMADA crews. Officers tagged 'armada' get bonuses. Long fights = sustained DPS + survivability.\n"
               << "Note armada subtypes (eclipse, swarm, borg). NEVER use 'non_armada_only' officers.\n";
            break;
        case OfficerGroupId::Mining:
            ss << "FOCUS: MINING crews. Mining speed, resource yield (ore/gas/crystal), protected cargo, cargo capacity.\n"
               << "Match mining subtags to the mining goal. Combat stats are secondary.\n";
            break;
        case OfficerGroupId::Loot_Cargo:
            ss << "FOCUS: LOOT & CARGO crews. Loot multipliers, cargo, rep boosts, XP boosts. Maximize farming rewards.\n";
            break;
        case OfficerGroupId::State_Chain:
            ss << "FOCUS: STATE CHAIN crews. Match 'applies:X' with 'benefits:X' tags.\n"
               << "Captain CM should APPLY the state, bridge OA should BENEFIT from it.\n"
               << "States: burning, morale, breach, assimilate, isolytic. Chain combos are extremely powerful.\n";
            break;
        case OfficerGroupId::Apex_Isolytic:
            ss << "FOCUS: APEX & ISOLYTIC META crews. Rock-Paper-Scissors: apex_barrier absorbs damage, "
               << "apex_shred strips barriers, isolytic_cascade bypasses defense, isolytic_defense reduces isolytic.\n"
               << "Knowing the counter matchup is critical.\n";
            break;
        case OfficerGroupId::Support:
            ss << "FOCUS: SUPPORT & UTILITY officers. Stat boosters, ability amplifiers, cumulative stackers.\n"
               << "These officers are bridge/BDA fillers that enhance any crew. Recommend best uses for each.\n";
            break;
    }

    ss << "\nRespond with ONLY valid JSON. Use EXACTLY this format:\n"
       << R"({"crews":[{"captain":"exact officer name","bridge":["officer name 1","officer name 2"],"reasoning":"why this crew works"}]})" << "\n"
       << "CRITICAL: captain and bridge values must be plain strings (exact names from the list), NOT objects.\n"
       << "Example with 2 crews:\n"
       << R"({"crews":[{"captain":"Kirk","bridge":["Spock","McCoy"],"reasoning":"Kirk CM opens strong, Spock OA boosts damage, McCoy OA heals"},{"captain":"Picard","bridge":["Data","Worf"],"reasoning":"Picard CM buffs morale, Data OA crits, Worf OA pierces armor"}]})";

    return ss.str();
}

// ===========================================================================
// Serialize group officers to compact JSON
// ===========================================================================

std::string group_officers_to_json(const OfficerGroup& group) {
    json j = json::array();

    for (const auto* off : group.officers) {
        json oj;
        oj["n"] = off->name;
        oj["rk"] = off->rank;

        if (!off->group.empty()) oj["g"] = off->group;

        // Abilities — keep compact, truncate at 80 chars
        auto trunc = [](const std::string& s, size_t max_len) -> std::string {
            if (s.size() <= max_len) return s;
            return s.substr(0, max_len) + "...";
        };

        if (!off->cm_text.empty()) oj["cm"] = trunc(off->cm_text, 80);
        if (!off->oa_text.empty()) oj["oa"] = trunc(off->oa_text, 80);
        if (!off->bda_text.empty()) oj["bd"] = trunc(off->bda_text, 80);

        // Tags — the most important signal
        std::vector<std::string> tags;
        if (off->armor_piercing)      tags.push_back("armor_piercing");
        if (off->shield_piercing)     tags.push_back("shield_piercing");
        if (off->accuracy_boost)      tags.push_back("accuracy");
        if (off->crit_related)        tags.push_back("crit");
        if (off->shield_related)      tags.push_back("shield");
        if (off->mitigation_related)  tags.push_back("mitigation");
        if (off->shots_related)       tags.push_back("extra_shots");
        if (off->ability_amplifier)   tags.push_back("amplifier");
        if (off->stat_booster)        tags.push_back("stat_boost");
        if (off->isolytic_cascade)    tags.push_back("isolytic");
        if (off->isolytic_defense)    tags.push_back("isolytic_def");
        if (off->apex_barrier)        tags.push_back("apex_barrier");
        if (off->apex_shred)          tags.push_back("apex_shred");
        if (off->cumulative_stacking) tags.push_back("cumulative");
        if (off->is_pvp_specific)     tags.push_back("pvp");
        if (off->is_pve_specific)     tags.push_back("pve");
        if (off->is_dual_use)         tags.push_back("dual_use");
        if (off->base_attack)         tags.push_back("base_atk");
        if (off->base_defend)         tags.push_back("base_def");
        if (off->armada)              tags.push_back("armada");
        if (off->loot)                tags.push_back("loot");
        if (off->loot_multiplier)     tags.push_back("loot_mult");
        if (off->mining)              tags.push_back("mining");
        if (off->repair)              tags.push_back("repair");
        if (off->is_bda())            tags.push_back("bda");

        for (const auto& s : off->states_applied)
            tags.push_back("applies:" + s);
        for (const auto& s : off->states_benefit)
            tags.push_back("benefits:" + s);

        // Hostile-type tags
        if (off->hostile_swarm)       tags.push_back("vs_swarm");
        if (off->hostile_borg)        tags.push_back("vs_borg");
        if (off->hostile_eclipse)     tags.push_back("vs_eclipse");
        if (off->hostile_gorn)        tags.push_back("vs_gorn");
        if (off->hostile_xindi)       tags.push_back("vs_xindi");
        if (off->hostile_silent)      tags.push_back("vs_silent");
        if (off->hostile_8472)        tags.push_back("vs_8472");

        // Armada subtypes
        if (off->armada_eclipse)      tags.push_back("arm_eclipse");
        if (off->armada_swarm)        tags.push_back("arm_swarm");
        if (off->armada_borg)         tags.push_back("arm_borg");

        if (!tags.empty()) oj["t"] = tags;

        j.push_back(oj);
    }

    return j.dump();  // Compact, no indentation
}

} // namespace stfc
