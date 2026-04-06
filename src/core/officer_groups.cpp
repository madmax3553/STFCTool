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
        case OfficerGroupId::PvP_General:        return "PvP General";
        case OfficerGroupId::PvP_On_Explorer:    return "PvP on Explorer";
        case OfficerGroupId::PvP_On_Battleship:  return "PvP on Battleship";
        case OfficerGroupId::PvP_On_Interceptor: return "PvP on Interceptor";
        case OfficerGroupId::PvP_Vs_Explorer:    return "PvP vs Explorer";
        case OfficerGroupId::PvP_Vs_Battleship:  return "PvP vs Battleship";
        case OfficerGroupId::PvP_Vs_Interceptor: return "PvP vs Interceptor";
        case OfficerGroupId::PvE_General:        return "PvE General";
        case OfficerGroupId::PvE_Specialized:    return "PvE Specialized";
        case OfficerGroupId::Base_Attack:        return "Base Attack";
        case OfficerGroupId::Base_Defend:        return "Base Defend";
        case OfficerGroupId::Armada:             return "Armada";
        case OfficerGroupId::Mining:             return "Mining";
        case OfficerGroupId::Loot_Cargo:         return "Loot & Cargo";
        case OfficerGroupId::State_Chain:        return "State Chain";
        case OfficerGroupId::Apex_Isolytic:      return "Apex & Isolytic";
        case OfficerGroupId::Support:            return "Support";
    }
    return "Unknown";
}

OfficerGroupId group_id_from_str(const std::string& s) {
    if (s == "PvP General")        return OfficerGroupId::PvP_General;
    if (s == "PvP on Explorer")    return OfficerGroupId::PvP_On_Explorer;
    if (s == "PvP on Battleship")  return OfficerGroupId::PvP_On_Battleship;
    if (s == "PvP on Interceptor") return OfficerGroupId::PvP_On_Interceptor;
    if (s == "PvP vs Explorer")    return OfficerGroupId::PvP_Vs_Explorer;
    if (s == "PvP vs Battleship")  return OfficerGroupId::PvP_Vs_Battleship;
    if (s == "PvP vs Interceptor") return OfficerGroupId::PvP_Vs_Interceptor;
    if (s == "PvE General")        return OfficerGroupId::PvE_General;
    if (s == "PvE Specialized")    return OfficerGroupId::PvE_Specialized;
    if (s == "Base Attack")        return OfficerGroupId::Base_Attack;
    if (s == "Base Defend")        return OfficerGroupId::Base_Defend;
    if (s == "Armada")             return OfficerGroupId::Armada;
    if (s == "Mining")             return OfficerGroupId::Mining;
    if (s == "Loot & Cargo")       return OfficerGroupId::Loot_Cargo;
    if (s == "State Chain")        return OfficerGroupId::State_Chain;
    if (s == "Apex & Isolytic")    return OfficerGroupId::Apex_Isolytic;
    if (s == "Support")            return OfficerGroupId::Support;
    // Legacy compat: map old group names to new equivalents
    if (s == "PvP Combat")         return OfficerGroupId::PvP_General;
    if (s == "PvE Hostile")        return OfficerGroupId::PvE_General;
    return OfficerGroupId::Support;
}

// ===========================================================================
// Group officers by classification tags
//
// NOTE: Tag-based grouping is the FALLBACK when no META cache exists.
// With META cache, build_meta_filtered_groups() in ai_crew_engine.cpp
// handles the grouping by intersecting Gemini's per-group officer lists
// with the owned roster.
//
// For the tag-based fallback, we only produce a subset of groups:
// - PvP_General (all PvP officers — Gemini will split by ship type later)
// - PvE_General (all PvE officers — Gemini will split general vs specialized)
// - Plus all the non-PvP/PvE groups unchanged
//
// The 7 PvP sub-groups and 2 PvE sub-groups are only meaningful when
// driven by Gemini's META knowledge. Tag-based classification can't tell
// "good on Explorer" from "good on Battleship" — that requires META.
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

    // For tag-based fallback, we use PvP_General as the catch-all PvP bucket
    init_group(OfficerGroupId::PvP_General,
        "Officers specializing in player-vs-player combat (all ship types)",
        "Focus on: armor/shield piercing, critical hits, damage bursts, "
        "stat boosters, ability amplifiers. Captain CM should deliver a powerful opening "
        "strike or critical debuff. Bridge OA should sustain damage output or defensive advantage.");

    // For tag-based fallback, PvE_General is the catch-all PvE bucket
    init_group(OfficerGroupId::PvE_General,
        "Officers effective against hostile NPCs (swarm, dailies, grinding)",
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
    // This prevents 225/289 officers landing in PvP General.
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

        // 8. PvE General — specific PvE tags
        if (!assigned && (off.pve_hostile || off.is_pve_specific || off.mission_boss ||
            off.hostile_swarm || off.hostile_borg || off.hostile_eclipse ||
            off.hostile_gorn || off.hostile_xindi || off.hostile_silent || off.hostile_8472)) {
            groups[OfficerGroupId::PvE_General].officers.push_back(&off);
            assigned = true;
        }

        // 9. PvP General — only pvp_specific, NOT dual_use/crit which are too broad
        if (!assigned && off.is_pvp_specific) {
            groups[OfficerGroupId::PvP_General].officers.push_back(&off);
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

        // 11. Unassigned high-rank officers -> Support
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

        // No hard cap — the natural filtering chain (META -> owned -> leveled)
        // produces small groups organically. This tag-based grouping is the
        // fallback when no META cache exists; exclusive assignment already
        // keeps groups manageable.
    }

    // Collect non-empty groups into result vector
    std::vector<OfficerGroup> result;
    // Ordered iteration — for tag-based fallback, only PvP_General and PvE_General
    // are populated (the 7 sub-PvP and PvE_Specialized are Gemini-driven only)
    static const OfficerGroupId order[] = {
        OfficerGroupId::PvP_General,
        OfficerGroupId::PvE_General,
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
        auto it = groups.find(id);
        if (it != groups.end() && !it->second.empty()) {
            result.push_back(std::move(it->second));
        }
    }

    return result;
}

// ===========================================================================
// Focused system prompts per group
// ===========================================================================

std::string group_system_prompt(OfficerGroupId group_id,
                               const std::vector<std::string>& example_names) {
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
        // --- PvP granular by ship type ---
        case OfficerGroupId::PvP_General:
            ss << "FOCUS: UNIVERSAL PvP COMBAT crews. These should work well on ANY ship type.\n"
               << "Prioritize armor/shield piercing, crits, damage bursts, stat boosters.\n"
               << "Captain CM should deliver a powerful opening strike or critical debuff.\n";
            break;
        case OfficerGroupId::PvP_On_Explorer:
            ss << "FOCUS: PvP crews when the player FLIES AN EXPLORER.\n"
               << "Explorers have strong shields and balanced stats. Optimize for shield synergy, "
               << "sustained damage, and exploiting the Explorer's defensive strengths.\n"
               << "Consider officers whose abilities scale with shield stats or provide shield repair.\n";
            break;
        case OfficerGroupId::PvP_On_Battleship:
            ss << "FOCUS: PvP crews when the player FLIES A BATTLESHIP.\n"
               << "Battleships have high hull/armor and strong weapons. Optimize for raw damage output, "
               << "armor piercing through enemy defenses, and hull-based survivability.\n"
               << "Consider officers whose abilities scale with weapon damage or hull HP.\n";
            break;
        case OfficerGroupId::PvP_On_Interceptor:
            ss << "FOCUS: PvP crews when the player FLIES AN INTERCEPTOR.\n"
               << "Interceptors are fast with high crit chance but fragile. Optimize for devastating "
               << "opening strikes, crit damage multipliers, and speed-based advantages.\n"
               << "Captain CM should aim to end fights quickly before the Interceptor takes too much damage.\n";
            break;
        case OfficerGroupId::PvP_Vs_Explorer:
            ss << "FOCUS: PvP crews optimized for KILLING EXPLORERS.\n"
               << "Explorers have strong shields. Prioritize shield piercing, shield drain, "
               << "and abilities that bypass or strip shields.\n"
               << "The triangle advantage: Interceptors beat Explorers, so consider "
               << "crews that amplify the Interceptor advantage or negate Explorer shield strength.\n";
            break;
        case OfficerGroupId::PvP_Vs_Battleship:
            ss << "FOCUS: PvP crews optimized for KILLING BATTLESHIPS.\n"
               << "Battleships have heavy armor/hull. Prioritize armor piercing, hull damage, "
               << "and abilities that reduce armor effectiveness.\n"
               << "The triangle advantage: Explorers beat Battleships, so consider "
               << "crews that amplify Explorer advantages or bypass Battleship armor.\n";
            break;
        case OfficerGroupId::PvP_Vs_Interceptor:
            ss << "FOCUS: PvP crews optimized for KILLING INTERCEPTORS.\n"
               << "Interceptors are fast and crit-heavy but fragile. Prioritize damage mitigation "
               << "against crits, accuracy to hit fast targets, and front-loaded damage to destroy them quickly.\n"
               << "The triangle advantage: Battleships beat Interceptors, so consider "
               << "crews that amplify Battleship firepower or reduce incoming crit damage.\n";
            break;

        // --- PvE ---
        case OfficerGroupId::PvE_General:
            ss << "FOCUS: PvE HOSTILE GRINDING crews for daily swarm grinding, general hostiles.\n"
               << "Prioritize sustained damage, survivability, crit damage, extra shots.\n"
               << "Hull repair/shield regen keep you grinding longer without going home to repair.\n"
               << "Captain CM should be a big damage opener. Bridge OA should sustain DPS.\n";
            break;
        case OfficerGroupId::PvE_Specialized:
            ss << "FOCUS: PvE SPECIALIZED HOSTILE crews — borg, eclipse, gorn, xindi, silent enemy, 8472.\n"
               << "Each hostile type has specific mechanics. Match officers to the hostile type:\n"
               << "- vs_borg: borg-specific abilities, assimilation defense\n"
               << "- vs_eclipse: eclipse armada and hostile specialists\n"
               << "- vs_gorn: gorn-specific combat abilities\n"
               << "- vs_xindi: xindi engagement specialists\n"
               << "- vs_silent: silent enemy combat abilities\n"
               << "- vs_8472: species 8472 specialists\n"
               << "Build specialized crews per hostile type where possible.\n";
            break;

        // --- Other scenarios (unchanged) ---
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

    // Build JSON format example using REAL officer names from the group
    // (1B models copy examples verbatim, so fake names like "Kirk" cause hallucination)
    ss << "\nRespond with ONLY valid JSON. Use EXACTLY this format:\n"
       << R"({"crews":[{"captain":"NAME","bridge":["NAME","NAME"],"reasoning":"why"}]})" << "\n"
       << "CRITICAL: captain and bridge values must be plain strings (exact names from the list), NOT objects.\n";

    if (example_names.size() >= 3) {
        // Use first 3 real officer names as example
        ss << "Example: "
           << R"({"crews":[{"captain":")" << example_names[0]
           << R"(","bridge":[")" << example_names[1] << R"(",")" << example_names[2]
           << R"("],"reasoning":"explain why"}]})" << "\n";
    }

    ss << "IMPORTANT: You may ONLY use names from the officer list provided. Do NOT invent names.\n";

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
