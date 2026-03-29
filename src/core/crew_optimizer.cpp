#include "core/crew_optimizer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace stfc {

// ---------------------------------------------------------------------------
// Scenario string conversions
// ---------------------------------------------------------------------------

const char* scenario_str(Scenario s) {
    switch (s) {
        case Scenario::PvP:             return "pvp";
        case Scenario::Hybrid:          return "hybrid";
        case Scenario::BaseCracker:     return "base_cracker";
        case Scenario::PvEHostile:      return "pve_hostile";
        case Scenario::MissionBoss:     return "mission_boss";
        case Scenario::Loot:            return "loot";
        case Scenario::Armada:          return "armada";
        case Scenario::MiningSpeed:     return "mining_speed";
        case Scenario::MiningProtected: return "mining_protected";
        case Scenario::MiningCrystal:   return "mining_crystal";
        case Scenario::MiningGas:       return "mining_gas";
        case Scenario::MiningOre:       return "mining_ore";
        case Scenario::MiningGeneral:   return "mining_general";
    }
    return "unknown";
}

const char* scenario_label(Scenario s) {
    switch (s) {
        case Scenario::PvP:             return "PvP (Player Combat)";
        case Scenario::Hybrid:          return "Hybrid PvE/PvP";
        case Scenario::BaseCracker:     return "Base Cracker (Station Attack)";
        case Scenario::PvEHostile:      return "PvE Hostiles";
        case Scenario::MissionBoss:     return "Mission Boss";
        case Scenario::Loot:            return "Loot / Resource Gathering";
        case Scenario::Armada:          return "Armada";
        case Scenario::MiningSpeed:     return "Mining Speed (All Resources)";
        case Scenario::MiningProtected: return "Mining Protected Cargo";
        case Scenario::MiningCrystal:   return "Mining Crystal";
        case Scenario::MiningGas:       return "Mining Gas";
        case Scenario::MiningOre:       return "Mining Ore";
        case Scenario::MiningGeneral:   return "Mining General";
    }
    return "Unknown";
}

Scenario scenario_from_str(const std::string& s) {
    if (s == "pvp")              return Scenario::PvP;
    if (s == "hybrid")           return Scenario::Hybrid;
    if (s == "base_cracker")     return Scenario::BaseCracker;
    if (s == "pve_hostile")      return Scenario::PvEHostile;
    if (s == "mission_boss")     return Scenario::MissionBoss;
    if (s == "loot")             return Scenario::Loot;
    if (s == "armada")           return Scenario::Armada;
    if (s == "mining_speed")     return Scenario::MiningSpeed;
    if (s == "mining_protected") return Scenario::MiningProtected;
    if (s == "mining_crystal")   return Scenario::MiningCrystal;
    if (s == "mining_gas")       return Scenario::MiningGas;
    if (s == "mining_ore")       return Scenario::MiningOre;
    if (s == "mining_general")   return Scenario::MiningGeneral;
    return Scenario::PvP;
}

// ---------------------------------------------------------------------------
// Ship type conversions
// ---------------------------------------------------------------------------

const char* ship_type_str(ShipType s) {
    switch (s) {
        case ShipType::Explorer:     return "explorer";
        case ShipType::Battleship:   return "battleship";
        case ShipType::Interceptor:  return "interceptor";
    }
    return "unknown";
}

ShipType ship_type_from_str(const std::string& s) {
    if (s == "battleship")  return ShipType::Battleship;
    if (s == "interceptor") return ShipType::Interceptor;
    return ShipType::Explorer;
}

const ShipRecommendation& get_ship_recommendation(Scenario s) {
    static const std::map<Scenario, ShipRecommendation> recs = {
        {Scenario::PvP,             {ShipType::Explorer, "Explorers have the highest base HP and defense, giving survivability advantage in PvP"}},
        {Scenario::Hybrid,          {ShipType::Explorer, "Explorers are the safest hybrid choice -- strong PvE and can survive PvP encounters"}},
        {Scenario::BaseCracker,     {ShipType::Interceptor, "Interceptors deal the most burst damage, ideal for breaking station defenses quickly"}},
        {Scenario::PvEHostile,      {ShipType::Explorer, "Explorers have the best sustained survivability for hostile grinding"}},
        {Scenario::MissionBoss,     {ShipType::Battleship, "Battleships have the highest raw damage output for single-target boss fights"}},
        {Scenario::Loot,            {ShipType::Explorer, "Explorers are safest for mining/cargo -- high defense means you survive when jumped"}},
        {Scenario::Armada,          {ShipType::Explorer, "Explorers provide good balanced stats for sustained armada boss fights"}},
        {Scenario::MiningSpeed,     {ShipType::Explorer, "Explorers are safest for extended mining operations"}},
        {Scenario::MiningProtected, {ShipType::Explorer, "Explorers survive ganks while protecting cargo"}},
        {Scenario::MiningCrystal,   {ShipType::Explorer, "Explorers for crystal mining with defense against raiders"}},
        {Scenario::MiningGas,       {ShipType::Explorer, "Explorers for gas mining with defense against raiders"}},
        {Scenario::MiningOre,       {ShipType::Explorer, "Explorers for ore mining with defense against raiders"}},
        {Scenario::MiningGeneral,   {ShipType::Explorer, "Explorers are the safest all-around mining ship"}},
    };
    return recs.at(s);
}

// ---------------------------------------------------------------------------
// Substring helper
// ---------------------------------------------------------------------------

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static bool contains_any(const std::string& haystack,
                          std::initializer_list<const char*> needles) {
    for (auto n : needles) {
        if (haystack.find(n) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CrewOptimizer::CrewOptimizer(std::vector<RosterOfficer> roster,
                             WeaknessProfile weakness)
    : weakness_(weakness) {
    officers_.reserve(roster.size());
    for (auto& r : roster) {
        ClassifiedOfficer o;
        o.name = std::move(r.name);
        o.rarity = r.rarity;
        o.level = r.level;
        o.rank = r.rank;
        o.attack = r.attack;
        o.defense = r.defense;
        o.health = r.health;
        o.group = std::move(r.group);
        o.cm_pct = r.cm_pct;
        o.oa_pct = r.oa_pct;
        o.effect = std::move(r.effect);
        o.causes_effect = r.causes_effect;
        o.player_uses = r.player_uses;
        o.description = std::move(r.description);
        officers_.push_back(std::move(o));
    }
    classify_officers();
}

void CrewOptimizer::set_ship_type(ShipType ship) {
    ship_type_ = ship;
    classify_officers();
}

std::vector<std::string> CrewOptimizer::other_ship_types() const {
    switch (ship_type_) {
        case ShipType::Explorer:     return {"interceptor", "battleship"};
        case ShipType::Battleship:   return {"interceptor", "explorer"};
        case ShipType::Interceptor:  return {"battleship", "explorer"};
    }
    return {};
}

// ---------------------------------------------------------------------------
// Officer classification (faithful port of _classify_officers)
// ---------------------------------------------------------------------------

void CrewOptimizer::classify_officers() {
    const std::string ship_kw = ship_type_str(ship_type_);
    const auto others = other_ship_types();

    for (auto& off : officers_) {
        const auto& d = off.description;

        // Reset all tags
        off.pvp_tags.clear();
        off.states_applied.clear();
        off.states_benefit.clear();
        off.is_ship_specific = false;
        off.is_pvp_specific = false;
        off.is_pve_specific = false;
        off.is_dual_use = false;
        off.crit_related = false;
        off.shield_related = false;
        off.shots_related = false;
        off.mitigation_related = false;
        off.isolytic_related = false;
        off.weapon_delay = false;
        off.ability_amplifier = false;
        off.stat_booster = false;
        off.base_attack = false;
        off.base_defend = false;
        off.pve_hostile = false;
        off.mission_boss = false;
        off.mining = false;
        off.cargo = false;
        off.loot = false;
        off.warp = false;
        off.armada = false;
        off.armada_solo = false;
        off.non_armada_only = false;
        off.repair = false;
        off.apex = false;
        off.mining_crystal = false;
        off.mining_gas = false;
        off.mining_ore = false;
        off.mining_speed = false;
        off.protected_cargo = false;
        off.node_defense = false;

        // Base cracker / station combat
        if (contains_any(d, {"attacking a station", "defence platform",
                             "defense platform", "station combat",
                             "station and ship mitigation",
                             "damage to defence", "damage to defense"})) {
            off.base_attack = true;
        }
        if (contains_any(d, {"defending the station", "defending a station"})) {
            off.base_defend = true;
        }

        // PvE hostile
        if (contains_any(d, {"hostile", "non-player", "hosilte"})) {
            off.pve_hostile = true;
        }

        // Non-armada only
        if (contains(d, "non-armada") || contains(d, "non armada")) {
            off.non_armada_only = true;
        }

        // Mission boss
        if (contains(d, "mission")) off.mission_boss = true;

        // Mining / resource
        if (contains(d, "mining")) off.mining = true;
        if (contains_any(d, {"cargo", "protected cargo"})) off.cargo = true;
        if (contains(d, "loot") || contains(d, "reward")) off.loot = true;
        if (contains_any(d, {"warp range", "warp speed", "warp distance"})) off.warp = true;

        // Mining subcategories
        if (contains_any(d, {"crystal", "raw crystal"})) off.mining_crystal = true;
        if (contains_any(d, {"gas", "raw gas"})) off.mining_gas = true;
        if (contains_any(d, {"ore", "raw ore"})) off.mining_ore = true;
        if (contains_any(d, {"mining speed", "mining rate", "mining efficiency"})) off.mining_speed = true;
        if (contains_any(d, {"protected cargo", "protect cargo"})) off.protected_cargo = true;
        if (contains_any(d, {"while mining", "on a mining node",
                             "defending a mining", "mining defense", "mining node"})) {
            off.node_defense = true;
        }

        // Armada
        if (contains(d, "armada")) off.armada = true;
        if (contains(d, "solo armada")) off.armada_solo = true;

        // Repair
        if (contains(d, "repair")) off.repair = true;

        // Apex
        if (contains_any(d, {"apex barrier", "apex shred", "apex"})) off.apex = true;

        // PvP specific
        if (contains_any(d, {"player", "pvp", "against player", "fighting player"})) {
            off.is_pvp_specific = true;
            off.pvp_tags.insert("pvp");
        }

        // PvE specific
        if (!off.is_pvp_specific &&
            contains_any(d, {"hostile", "mining", "cargo", "resources",
                             "warp range", "warp speed", "non-player", "reputation"})) {
            off.is_pve_specific = true;
        }

        // Dual-use
        bool has_player_ref = contains_any(d, {"player", "pvp"});
        bool has_hostile_ref = contains_any(d, {"hostile", "non-player", "armada"});
        bool has_no_target_lock = !has_player_ref && !has_hostile_ref;
        if ((has_player_ref && has_hostile_ref) || has_no_target_lock) {
            off.is_dual_use = true;
        }

        // Ability amplifier
        if (contains_any(d, {"effectiveness of all officer",
                             "all officer stats", "all officers"})) {
            off.ability_amplifier = true;
        }
        // Note: Python also checks "increase.*officer abilit" but it's substring, not regex.
        // Since ".*" is left as literal, it would match "increase.*officer abilit" literally.
        // This rarely if ever matches, but we include it for faithfulness.
        if (contains(d, "increase.*officer abilit")) off.ability_amplifier = true;

        // Stat booster
        if (contains_any(d, {"officer stats", "all officer", "officer attack",
                             "officer defence", "officer defense", "officer health"})) {
            off.stat_booster = true;
        }

        // Ship-type specific
        if (contains(d, ship_kw)) {
            off.is_ship_specific = true;
            off.pvp_tags.insert(ship_kw);
        }

        // State classification
        struct StatePattern {
            const char* state;
            std::vector<const char*> apply_kw;
            std::vector<const char*> benefit_kw;  // with .* stripped
        };
        static const StatePattern state_patterns[] = {
            {"morale",
             {"morale for", "inspire morale", "apply morale"},
             {"whenmorale", "with morale", "has morale", "ship has morale", "shipmorale"}},
            {"breach",
             {"hull breach for", "apply hull breach", "cause hull breach", "inflict hull breach"},
             {"has hull breach", "with hull breach", "opponenthull breach", "hull breach,"}},
            {"burning",
             {"burning for", "apply burning", "cause burning", "inflict burning"},
             {"is burning", "has burning", "opponentburning", "burning,"}},
            {"assimilate",
             {"assimilate for", "apply assimilate"},
             {"with assimilate", "has assimilate"}},
        };

        for (const auto& sp : state_patterns) {
            if (contains(off.effect, sp.state)) {
                off.pvp_tags.insert(sp.state);
                if (off.causes_effect) {
                    off.states_applied.insert(sp.state);
                }
            }
            for (auto kw : sp.apply_kw) {
                if (contains(d, kw)) {
                    off.states_applied.insert(sp.state);
                    off.pvp_tags.insert(sp.state);
                }
            }
            for (auto kw : sp.benefit_kw) {
                if (contains(d, kw)) {
                    off.states_benefit.insert(sp.state);
                    off.pvp_tags.insert(sp.state);
                }
            }
        }

        // Crit
        if (contains_any(d, {"critical hit", "critical damage", "crit"})) {
            off.crit_related = true;
            off.pvp_tags.insert("crit");
        }

        // Shield
        if (contains_any(d, {"shield", "shp"})) off.shield_related = true;

        // Shots
        if (contains_any(d, {"shots", "number of shots", "weapon shots"})) {
            off.shots_related = true;
            off.pvp_tags.insert("shots");
        }

        // Mitigation
        if (contains_any(d, {"mitigation", "armour", "armor", "dodge", "deflection"})) {
            off.mitigation_related = true;
        }

        // Isolytic
        if (contains_any(d, {"isolytic", "apex"})) {
            off.isolytic_related = true;
            off.pvp_tags.insert("isolytic");
        }

        // Weapon delay
        if (contains(d, "delay")) {
            off.weapon_delay = true;
            off.pvp_tags.insert("delay");
        }
    }
}

// ---------------------------------------------------------------------------
// Ship-lock helpers
// ---------------------------------------------------------------------------

bool CrewOptimizer::cm_works_on_ship(const ClassifiedOfficer& off) const {
    std::string cm_text = off.description;
    // Extract CM portion (before " oa:" or " bda:")
    for (auto delim : {" oa:", " bda:"}) {
        auto idx = cm_text.find(delim);
        if (idx != std::string::npos && idx > 0) {
            cm_text = cm_text.substr(0, idx);
            break;
        }
    }
    for (const auto& other : other_ship_types()) {
        if (contains(cm_text, "on an " + other) ||
            contains(cm_text, "on a " + other) ||
            contains(cm_text, "on " + other) ||
            contains(cm_text, "while on " + other)) {
            return false;
        }
    }
    return true;
}

bool CrewOptimizer::oa_works_on_ship(const ClassifiedOfficer& off) const {
    std::string oa_text;
    for (auto delim : {"oa:", "bda:"}) {
        auto idx = off.description.find(delim);
        if (idx != std::string::npos) {
            oa_text = off.description.substr(idx);
            break;
        }
    }
    if (oa_text.empty()) return true;  // No OA text = no lock

    for (const auto& other : other_ship_types()) {
        if (contains(oa_text, "on an " + other) ||
            contains(oa_text, "on a " + other) ||
            contains(oa_text, "on " + other) ||
            contains(oa_text, "while on " + other)) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Individual scoring
// ---------------------------------------------------------------------------

double CrewOptimizer::score_pvp_individual(const ClassifiedOfficer& off) const {
    double score = off.attack * 0.5 + off.defense * 0.3 + off.health * 0.2;

    if (off.is_pvp_specific) score *= 1.4;
    if (off.is_ship_specific) score *= 1.5;
    if (!off.states_applied.empty()) score *= 1.3;

    if (off.crit_related) {
        double crit_weight = 1.3;
        if (weakness_.crit_damage_gap > 5) crit_weight = 1.6;
        score *= crit_weight;
    }

    if (off.shots_related) score *= 1.25;
    if (off.isolytic_related) score *= 1.2;
    if (off.weapon_delay) score *= 1.3;
    if (off.player_uses) score *= 1.15;

    return score;
}

double CrewOptimizer::score_hybrid_individual(const ClassifiedOfficer& off) const {
    double score = off.attack * 0.4 + off.defense * 0.35 + off.health * 0.25;

    if (off.is_dual_use) score *= 1.5;
    if (off.ability_amplifier) score *= 1.6;
    if (off.stat_booster) score *= 1.3;
    if (off.is_ship_specific) score *= 1.35;
    if (!off.states_applied.empty()) score *= 1.25;

    if (off.crit_related) {
        double crit_weight = 1.25;
        if (weakness_.crit_damage_gap > 5) crit_weight = 1.45;
        score *= crit_weight;
    }

    if (off.shield_related) score *= 1.15;
    if (off.shots_related) score *= 1.15;
    if (off.mitigation_related) score *= 1.1;
    if (off.player_uses) score *= 1.15;

    // Penalties
    if (off.is_pve_specific && !off.is_dual_use) score *= 0.45;
    if (off.is_pvp_specific && !off.is_dual_use) score *= 0.55;

    return score;
}

double CrewOptimizer::score_scenario_individual(const ClassifiedOfficer& off,
                                                 Scenario scenario) const {
    if (scenario == Scenario::PvP) return score_pvp_individual(off);
    if (scenario == Scenario::Hybrid) return score_hybrid_individual(off);

    const auto& d = off.description;
    double score = off.attack * 0.5 + off.defense * 0.3 + off.health * 0.2;

    switch (scenario) {
    case Scenario::BaseCracker:
        if (off.base_attack) score *= 2.5;
        if (off.base_defend) score *= 0.1;
        if (off.crit_related) score *= 1.4;
        if (off.shots_related) score *= 1.3;
        if (off.weapon_delay) score *= 1.4;
        if (off.apex && contains(d, "shred")) score *= 1.3;
        if (contains(d, "shield piercing") || contains(d, "armor piercing")) score *= 1.2;
        if (off.is_pvp_specific) score *= 1.2;
        if (off.pve_hostile && !off.base_attack && !off.is_pvp_specific) score *= 0.4;
        if (off.mining || off.cargo) score *= 0.2;
        break;

    case Scenario::PvEHostile:
        if (off.pve_hostile) score *= 2.0;
        if (off.mitigation_related) score *= 1.5;
        if (off.shield_related) score *= 1.3;
        if (off.crit_related) score *= 1.3;
        if (off.repair) score *= 1.2;
        if (off.stat_booster) score *= 1.3;
        if (off.ability_amplifier) score *= 1.5;
        if (off.loot) score *= 1.15;
        if (off.is_pvp_specific && !off.pve_hostile) score *= 0.3;
        if (off.mining) score *= 0.2;
        break;

    case Scenario::MissionBoss:
        if (off.mission_boss) score *= 2.0;
        if (off.pve_hostile) score *= 1.8;
        score += off.attack * 0.5;  // Extra weight on attack
        if (off.crit_related) score *= 1.5;
        if (off.isolytic_related) score *= 1.4;
        if (off.shots_related) score *= 1.3;
        if (off.mitigation_related) score *= 1.3;
        if (off.shield_related) score *= 1.2;
        if (off.ability_amplifier) score *= 1.4;
        if (off.stat_booster) score *= 1.3;
        if (off.armada && !off.pve_hostile) score *= 0.4;
        if (off.mining || off.cargo) score *= 0.1;
        break;

    case Scenario::Loot:
        if (off.mining) score *= 3.0;
        if (off.cargo) score *= 2.5;
        if (off.loot) score *= 2.5;
        if (off.warp) score *= 1.5;
        score += off.defense * 0.5;
        score += off.health * 0.3;
        if (off.shield_related) score *= 1.2;
        if (off.mitigation_related) score *= 1.2;
        if (off.is_pvp_specific) score *= 1.1;
        if (!off.mining && !off.cargo && !off.loot && !off.warp) score *= 0.4;
        break;

    case Scenario::Armada:
        if (off.armada) score *= 2.5;
        if (off.armada_solo) score *= 1.3;
        if (off.non_armada_only) score *= 0.2;
        if (off.pve_hostile && !off.non_armada_only) score *= 1.3;
        score += off.attack * 0.4;
        if (off.crit_related) score *= 1.4;
        if (off.isolytic_related) score *= 1.4;
        if (off.shots_related) score *= 1.3;
        if (off.mitigation_related) score *= 1.3;
        if (off.ability_amplifier) score *= 1.3;
        if (off.stat_booster) score *= 1.2;
        if (off.is_pvp_specific && !off.armada) score *= 0.3;
        if (off.mining || off.cargo) score *= 0.1;
        break;

    case Scenario::MiningSpeed:
        if (off.mining_speed) score *= 4.0;
        else if (off.mining) score *= 2.5;
        if (off.cargo) score *= 1.5;
        if (off.warp) score *= 1.2;
        if (off.node_defense) score *= 1.3;
        if (!off.mining && !off.mining_speed && !off.cargo) score *= 0.2;
        break;

    case Scenario::MiningProtected:
        if (off.protected_cargo) score *= 4.0;
        else if (off.cargo) score *= 2.5;
        if (off.mining) score *= 1.5;
        if (off.node_defense) score *= 1.5;
        if (off.shield_related) score *= 1.3;
        if (off.mitigation_related) score *= 1.3;
        if (!off.protected_cargo && !off.cargo && !off.mining && !off.node_defense) score *= 0.2;
        break;

    case Scenario::MiningCrystal:
        if (off.mining_crystal) score *= 4.0;
        else if (off.mining) score *= 2.0;
        if (off.mining_speed) score *= 1.5;
        if (off.cargo) score *= 1.3;
        if (off.protected_cargo) score *= 1.3;
        if (off.node_defense) score *= 1.2;
        if (!off.mining && !off.mining_crystal && !off.cargo) score *= 0.2;
        break;

    case Scenario::MiningGas:
        if (off.mining_gas) score *= 4.0;
        else if (off.mining) score *= 2.0;
        if (off.mining_speed) score *= 1.5;
        if (off.cargo) score *= 1.3;
        if (off.protected_cargo) score *= 1.3;
        if (off.node_defense) score *= 1.2;
        if (!off.mining && !off.mining_gas && !off.cargo) score *= 0.2;
        break;

    case Scenario::MiningOre:
        if (off.mining_ore) score *= 4.0;
        else if (off.mining) score *= 2.0;
        if (off.mining_speed) score *= 1.5;
        if (off.cargo) score *= 1.3;
        if (off.protected_cargo) score *= 1.3;
        if (off.node_defense) score *= 1.2;
        if (!off.mining && !off.mining_ore && !off.cargo) score *= 0.2;
        break;

    case Scenario::MiningGeneral:
        if (off.mining) score *= 3.0;
        if (off.mining_speed) score *= 1.5;
        if (off.cargo) score *= 2.0;
        if (off.protected_cargo) score *= 1.5;
        if (off.node_defense) score *= 1.3;
        if (off.warp) score *= 1.2;
        {
            int mining_tags = 0;
            if (off.mining_crystal) ++mining_tags;
            if (off.mining_gas) ++mining_tags;
            if (off.mining_ore) ++mining_tags;
            if (off.mining_speed) ++mining_tags;
            if (off.protected_cargo) ++mining_tags;
            if (mining_tags >= 2) score *= 1.0 + mining_tags * 0.2;
        }
        if (!off.mining && !off.cargo && !off.node_defense) score *= 0.2;
        break;

    default:
        break;
    }

    // Universal bonuses for non-pvp/non-hybrid scenarios
    if (off.is_ship_specific) score *= 1.3;
    if (off.player_uses) score *= 1.1;

    return score;
}

// ---------------------------------------------------------------------------
// Shared crew-level scoring helpers
// ---------------------------------------------------------------------------

void CrewOptimizer::apply_state_chain(double& total, CrewBreakdown& bd,
                                       const ClassifiedOfficer* crew[3],
                                       double chain_per_state,
                                       bool penalize_none) const {
    std::set<std::string> all_applied, all_benefit;
    for (int i = 0; i < 3; ++i) {
        for (const auto& s : crew[i]->states_applied) all_applied.insert(s);
        for (const auto& s : crew[i]->states_benefit) all_benefit.insert(s);
    }

    // Intersection = synergy states
    std::set<std::string> synergy;
    for (const auto& s : all_applied) {
        if (all_benefit.count(s)) synergy.insert(s);
    }

    if (!synergy.empty()) {
        double chain_bonus = static_cast<double>(synergy.size()) * chain_per_state;
        int beneficiaries = 0;
        for (int i = 0; i < 3; ++i) {
            for (const auto& s : crew[i]->states_benefit) {
                if (all_applied.count(s)) { ++beneficiaries; break; }
            }
        }
        chain_bonus *= 1.0 + beneficiaries * 0.5;
        total += chain_bonus;
        bd.state_chain_bonus = chain_bonus;

        std::string joined;
        for (const auto& s : synergy) {
            if (!joined.empty()) joined += ", ";
            joined += s;
        }
        bd.synergy_notes.push_back(
            "State synergy chain: " + joined + " (" +
            std::to_string(beneficiaries) + " officers benefit)");
    } else if (penalize_none && all_applied.empty()) {
        double penalty = total * 0.3;
        total -= penalty;
        bd.penalties.push_back("No state applicator: conditional abilities may not trigger");
    }
}

void CrewOptimizer::apply_coherence(double& total, CrewBreakdown& bd,
                                     const ClassifiedOfficer* crew[3],
                                     double bonus3, double bonus2) const {
    static const std::set<std::string> state_set = {"morale", "breach", "burning", "assimilate"};
    std::map<std::string, int> state_counts;
    for (int i = 0; i < 3; ++i) {
        for (const auto& tag : crew[i]->pvp_tags) {
            if (state_set.count(tag)) state_counts[tag]++;
        }
    }
    if (state_counts.empty()) return;

    int best_count = 0;
    std::string best_state;
    for (const auto& [s, c] : state_counts) {
        if (c > best_count) { best_count = c; best_state = s; }
    }

    if (best_count >= 3) {
        total += bonus3;
        bd.synergy_bonus += bonus3;
        bd.synergy_notes.push_back(
            "Full crew coherence on '" + best_state + "' (all 3 officers)");
    } else if (best_count == 2) {
        total += bonus2;
        bd.synergy_bonus += bonus2;
        bd.synergy_notes.push_back(
            "Partial crew coherence on '" + best_state + "' (2/3 officers)");
    }
}

// Count officers that reduce enemy crit (for weakness counters)
static int count_crit_reducers(const ClassifiedOfficer* crew[3]) {
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        const auto& d = crew[i]->description;
        if ((contains(d, "decrease") && contains(d, "critical")) ||
            (contains(d, "reduce") && contains(d, "critical")) ||
            (contains(d, "opponent") && contains(d, "critical"))) {
            ++count;
        }
    }
    return count;
}

void CrewOptimizer::apply_weakness_counters_pvp(double& total, CrewBreakdown& bd,
                                                 const ClassifiedOfficer* crew[3],
                                                 double crit_mult,
                                                 double shield_mult) const {
    double weakness_bonus = 0.0;

    if (weakness_.crit_damage_gap > 3) {
        int reducers = count_crit_reducers(crew);
        if (reducers > 0) {
            double bonus = reducers * crit_mult * (weakness_.crit_damage_gap / 10.0);
            weakness_bonus += bonus;
            bd.synergy_notes.push_back(
                "Counters crit gap weakness (" + std::to_string(reducers) + " crit reducers)");
        }
    }

    if (weakness_.shield_timing_loss > 3) {
        int helpers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->shield_related) ++helpers;
        if (helpers > 0) {
            weakness_bonus += helpers * shield_mult * (weakness_.shield_timing_loss / 10.0);
        }
    }

    if (weakness_.damage_escalation > 3) {
        int mitigators = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->mitigation_related) ++mitigators;
        if (mitigators > 0) weakness_bonus += mitigators * 10000;
    }

    total += weakness_bonus;
    bd.weakness_counter_bonus = weakness_bonus;
}

void CrewOptimizer::apply_oa_ship_penalty(double& total, CrewBreakdown& bd,
                                           const ClassifiedOfficer& b1,
                                           const ClassifiedOfficer& b2) const {
    std::string ship_label = ship_type_str(ship_type_);
    ship_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ship_label[0])));

    for (const auto* off : {&b1, &b2}) {
        if (!oa_works_on_ship(*off)) {
            double penalty = total * 0.15;
            total -= penalty;
            bd.penalties.push_back(
                "'" + off->name + "' OA locked to non-" + ship_label + " ship type");
        }
    }
}

// ---------------------------------------------------------------------------
// Crew scoring: PvP
// ---------------------------------------------------------------------------

CrewResult CrewOptimizer::score_pvp_crew(const ClassifiedOfficer& captain,
                                          const ClassifiedOfficer& b1,
                                          const ClassifiedOfficer& b2) const {
    CrewResult result;
    auto& bd = result.breakdown;
    bd.captain = captain.name;
    bd.bridge = {b1.name, b2.name};

    const ClassifiedOfficer* crew[3] = {&captain, &b1, &b2};
    double total = 0.0;

    // Individual scores
    for (int i = 0; i < 3; ++i) {
        double s = score_pvp_individual(*crew[i]);
        bd.individual_scores[crew[i]->name] = s;
        total += s;
    }

    std::string ship_label = ship_type_str(ship_type_);
    ship_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ship_label[0])));

    // Captain maneuver
    if (captain.is_bda()) {
        double penalty = total * 0.25;
        total -= penalty;
        bd.penalties.push_back("'" + captain.name + "' has a BDA, not a Captain Maneuver -- wasted captain slot");
    } else if (!cm_works_on_ship(captain)) {
        double penalty = total * 0.2;
        total -= penalty;
        bd.penalties.push_back("'" + captain.name + "' CM locked to non-" + ship_label + " ship type");
    } else {
        double cm_bonus = std::min(captain.cm_pct, 500.0) * 50.0;
        total += cm_bonus;
    }

    // State synergy chain
    apply_state_chain(total, bd, crew, 50000, true);

    // Coherence
    apply_coherence(total, bd, crew, 40000, 15000);

    // Crit coverage
    {
        int crit_officers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->crit_related) ++crit_officers;
        if (crit_officers >= 2) {
            total += 30000;
            bd.crit_bonus = 30000;
            bd.synergy_notes.push_back(
                "Strong crit coverage (" + std::to_string(crit_officers) + "/3 officers)");
        }
    }

    // Ship-type coverage
    {
        int ship_officers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->is_ship_specific) ++ship_officers;
        if (ship_officers >= 2) {
            total += 25000;
            bd.ship_type_bonus = 25000;
            bd.synergy_notes.push_back(
                ship_label + "-specialized (" + std::to_string(ship_officers) + "/3 officers)");
        }
    }

    // Weakness counters
    apply_weakness_counters_pvp(total, bd, crew, 20000, 10000);

    // PvE-only penalty
    for (int i = 0; i < 3; ++i) {
        const auto& d = crew[i]->description;
        if (contains_any(d, {"hostile", "mining", "cargo", "resources",
                             "armada only", "non-player", "reputation",
                             "warp range", "warp speed"})) {
            if (!crew[i]->is_pvp_specific) {
                double penalty = total * 0.15;
                total -= penalty;
                bd.penalties.push_back(
                    "PvE officer '" + crew[i]->name + "' in PvP crew");
            }
        }
    }

    // OA ship-lock penalty
    apply_oa_ship_penalty(total, bd, b1, b2);

    result.score = total;
    return result;
}

// ---------------------------------------------------------------------------
// Crew scoring: Hybrid
// ---------------------------------------------------------------------------

CrewResult CrewOptimizer::score_hybrid_crew(const ClassifiedOfficer& captain,
                                             const ClassifiedOfficer& b1,
                                             const ClassifiedOfficer& b2) const {
    CrewResult result;
    auto& bd = result.breakdown;
    bd.captain = captain.name;
    bd.bridge = {b1.name, b2.name};

    const ClassifiedOfficer* crew[3] = {&captain, &b1, &b2};
    double total = 0.0;

    for (int i = 0; i < 3; ++i) {
        double s = score_hybrid_individual(*crew[i]);
        bd.individual_scores[crew[i]->name] = s;
        total += s;
    }

    std::string ship_label = ship_type_str(ship_type_);
    ship_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ship_label[0])));

    // Captain maneuver
    if (captain.is_bda()) {
        double penalty = total * 0.25;
        total -= penalty;
        bd.penalties.push_back("'" + captain.name + "' has a BDA, not a Captain Maneuver -- wasted captain slot");
    } else if (!cm_works_on_ship(captain)) {
        double penalty = total * 0.2;
        total -= penalty;
        bd.penalties.push_back("'" + captain.name + "' CM locked to non-" + ship_label + " ship type");
    } else {
        total += std::min(captain.cm_pct, 500.0) * 50.0;
    }

    // Ability amplifier captain bonus
    if (captain.ability_amplifier) {
        total += 60000;
        bd.amplifier_bonus = 60000;
        bd.synergy_notes.push_back(
            "Ability amplifier captain '" + captain.name + "' -- boosts all bridge abilities in both PvE and PvP");
    }

    // Dual-use coherence
    {
        int dual_use_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->is_dual_use) ++dual_use_count;
        if (dual_use_count == 3) {
            total += 50000;
            bd.dual_use_bonus = 50000;
            bd.synergy_notes.push_back("Full dual-use crew -- all 3 officers work in both PvE and PvP");
        } else if (dual_use_count == 2) {
            total += 20000;
            bd.dual_use_bonus = 20000;
            bd.synergy_notes.push_back("Partial dual-use crew -- 2/3 officers work in both modes");
        }
    }

    // State synergy chain (40000 per state, no penalty for none)
    apply_state_chain(total, bd, crew, 40000, false);

    // Coherence
    apply_coherence(total, bd, crew, 30000, 12000);

    // Crit coverage
    {
        int crit_officers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->crit_related) ++crit_officers;
        if (crit_officers >= 2) {
            total += 25000;
            bd.crit_bonus = 25000;
            bd.synergy_notes.push_back(
                "Strong crit coverage (" + std::to_string(crit_officers) + "/3 officers)");
        }
    }

    // Ship-type coverage
    {
        int ship_officers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->is_ship_specific) ++ship_officers;
        if (ship_officers >= 2) {
            total += 20000;
            bd.ship_type_bonus = 20000;
            bd.synergy_notes.push_back(
                ship_label + "-specialized (" + std::to_string(ship_officers) + "/3 officers)");
        }
    }

    // Weakness counters (15000/8000 for hybrid)
    apply_weakness_counters_pvp(total, bd, crew, 15000, 8000);

    // Single-mode penalties
    for (int i = 0; i < 3; ++i) {
        if (crew[i]->is_pve_specific && !crew[i]->is_dual_use) {
            double penalty = total * 0.12;
            total -= penalty;
            bd.penalties.push_back("PvE-only officer '" + crew[i]->name + "' -- dead weight in PvP");
        } else if (crew[i]->is_pvp_specific && !crew[i]->is_dual_use) {
            double penalty = total * 0.10;
            total -= penalty;
            bd.penalties.push_back("PvP-only officer '" + crew[i]->name + "' -- dead weight in PvE");
        }
    }

    // OA ship-lock penalty
    apply_oa_ship_penalty(total, bd, b1, b2);

    result.score = total;
    return result;
}

// ---------------------------------------------------------------------------
// Crew scoring: Generic scenario
// ---------------------------------------------------------------------------

CrewResult CrewOptimizer::score_scenario_crew(const ClassifiedOfficer& captain,
                                               const ClassifiedOfficer& b1,
                                               const ClassifiedOfficer& b2,
                                               Scenario scenario) const {
    if (scenario == Scenario::PvP) return score_pvp_crew(captain, b1, b2);
    if (scenario == Scenario::Hybrid) return score_hybrid_crew(captain, b1, b2);

    CrewResult result;
    auto& bd = result.breakdown;
    bd.captain = captain.name;
    bd.bridge = {b1.name, b2.name};

    const ClassifiedOfficer* crew[3] = {&captain, &b1, &b2};
    double total = 0.0;

    for (int i = 0; i < 3; ++i) {
        double s = score_scenario_individual(*crew[i], scenario);
        bd.individual_scores[crew[i]->name] = s;
        total += s;
    }

    std::string ship_label = ship_type_str(ship_type_);
    ship_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ship_label[0])));

    // Captain maneuver
    if (captain.is_bda()) {
        double penalty = total * 0.25;
        total -= penalty;
        bd.penalties.push_back("'" + captain.name + "' has a BDA, not a Captain Maneuver -- wasted captain slot");
    } else if (!cm_works_on_ship(captain)) {
        double penalty = total * 0.2;
        total -= penalty;
        bd.penalties.push_back("'" + captain.name + "' CM locked to non-" + ship_label + " ship type");
    } else {
        total += std::min(captain.cm_pct, 500.0) * 50.0;
    }

    // Ability amplifier captain
    if (captain.ability_amplifier) {
        total += 50000;
        bd.amplifier_bonus = 50000;
        bd.synergy_notes.push_back(
            "Ability amplifier captain '" + captain.name + "' boosts all bridge abilities");
    }

    // State synergy chain (40000, no penalty for none)
    apply_state_chain(total, bd, crew, 40000, false);

    // Coherence
    apply_coherence(total, bd, crew, 35000, 12000);

    // Crit coverage
    {
        int crit_officers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->crit_related) ++crit_officers;
        if (crit_officers >= 2) {
            total += 25000;
            bd.crit_bonus = 25000;
            bd.synergy_notes.push_back(
                "Strong crit coverage (" + std::to_string(crit_officers) + "/3 officers)");
        }
    }

    // Ship-type coverage
    {
        int ship_officers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->is_ship_specific) ++ship_officers;
        if (ship_officers >= 2) {
            total += 20000;
            bd.ship_type_bonus = 20000;
            bd.synergy_notes.push_back(
                ship_label + "-specialized (" + std::to_string(ship_officers) + "/3 officers)");
        }
    }

    // Scenario-specific crew bonuses
    double scenario_bonus = 0.0;

    if (scenario == Scenario::BaseCracker) {
        int base_attackers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->base_attack) ++base_attackers;
        if (base_attackers >= 2) {
            scenario_bonus += base_attackers * 30000;
            bd.synergy_notes.push_back(
                "Station attack crew (" + std::to_string(base_attackers) + "/3 officers)");
        }
        int delay_officers = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->weapon_delay) ++delay_officers;
        if (delay_officers >= 1) {
            scenario_bonus += delay_officers * 15000;
            bd.synergy_notes.push_back(
                "Weapon delay (" + std::to_string(delay_officers) + " officer(s))");
        }
    } else if (scenario == Scenario::PvEHostile) {
        int pve_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->pve_hostile) ++pve_count;
        if (pve_count >= 2) {
            scenario_bonus += pve_count * 20000;
            bd.synergy_notes.push_back(
                "PvE-focused crew (" + std::to_string(pve_count) + "/3 officers)");
        }
        int repair_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->repair) ++repair_count;
        if (repair_count >= 1) {
            scenario_bonus += repair_count * 10000;
            bd.synergy_notes.push_back(
                "Repair capability (" + std::to_string(repair_count) + " officer(s))");
        }
    } else if (scenario == Scenario::MissionBoss) {
        int mission_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->mission_boss) ++mission_count;
        if (mission_count >= 1) {
            scenario_bonus += mission_count * 25000;
            bd.synergy_notes.push_back(
                "Mission specialist (" + std::to_string(mission_count) + " officer(s))");
        }
        double total_atk = 0;
        for (int i = 0; i < 3; ++i) total_atk += crew[i]->attack;
        if (total_atk > 150000) {
            scenario_bonus += static_cast<int>((total_atk - 150000) * 0.1);
        }
        int iso_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->isolytic_related) ++iso_count;
        if (iso_count >= 2) scenario_bonus += iso_count * 15000;
    } else if (scenario == Scenario::Loot) {
        int mining_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->mining) ++mining_count;
        if (mining_count >= 2) {
            scenario_bonus += mining_count * 30000;
            bd.synergy_notes.push_back(
                "Mining crew (" + std::to_string(mining_count) + "/3 officers)");
        }
        int cargo_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->cargo) ++cargo_count;
        if (cargo_count >= 1) scenario_bonus += cargo_count * 20000;
        int loot_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->loot) ++loot_count;
        if (loot_count >= 1) scenario_bonus += loot_count * 20000;
        double total_def = 0, total_hp = 0;
        for (int i = 0; i < 3; ++i) { total_def += crew[i]->defense; total_hp += crew[i]->health; }
        scenario_bonus += static_cast<int>(total_def * 0.1 + total_hp * 0.05);
    } else if (scenario == Scenario::Armada) {
        int armada_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->armada) ++armada_count;
        if (armada_count >= 2) {
            scenario_bonus += armada_count * 25000;
            bd.synergy_notes.push_back(
                "Armada crew (" + std::to_string(armada_count) + "/3 officers)");
        }
        int iso_count = 0;
        for (int i = 0; i < 3; ++i) if (crew[i]->isolytic_related) ++iso_count;
        if (iso_count >= 2) scenario_bonus += iso_count * 15000;
    }

    total += scenario_bonus;
    bd.scenario_bonus = scenario_bonus;

    // Weakness counters (only for pvp/base_cracker in generic)
    if (scenario == Scenario::BaseCracker) {
        double weakness_bonus = 0.0;
        if (weakness_.crit_damage_gap > 3) {
            int reducers = count_crit_reducers(crew);
            if (reducers > 0) {
                weakness_bonus += reducers * 15000 * (weakness_.crit_damage_gap / 10.0);
            }
        }
        if (weakness_.shield_timing_loss > 3) {
            int helpers = 0;
            for (int i = 0; i < 3; ++i) if (crew[i]->shield_related) ++helpers;
            if (helpers > 0) {
                weakness_bonus += helpers * 8000 * (weakness_.shield_timing_loss / 10.0);
            }
        }
        total += weakness_bonus;
        bd.weakness_counter_bonus = weakness_bonus;
    }

    // Scenario-specific penalties
    if (scenario == Scenario::BaseCracker) {
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->mining || crew[i]->cargo) {
                double penalty = total * 0.15;
                total -= penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is a mining/cargo officer -- useless in station combat");
            }
            if (crew[i]->base_defend && !crew[i]->base_attack) {
                double penalty = total * 0.2;
                total -= penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is a station DEFENDER -- wrong role for attacking");
            }
        }
    } else if (scenario == Scenario::Armada) {
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->non_armada_only) {
                double penalty = total * 0.3;
                total -= penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' explicitly does NOT work in armadas");
            }
            if (crew[i]->mining || crew[i]->cargo) {
                double penalty = total * 0.15;
                total -= penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is a mining/cargo officer -- useless in armada");
            }
        }
    } else if (scenario == Scenario::Loot) {
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->is_pvp_specific && !crew[i]->mining && !crew[i]->cargo && !crew[i]->loot) {
                double penalty = total * 0.1;
                total -= penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is PvP-only -- limited value while mining");
            }
        }
    }

    // OA ship-lock penalty
    apply_oa_ship_penalty(total, bd, b1, b2);

    result.score = total;
    return result;
}

// ---------------------------------------------------------------------------
// Combinatorial search
// ---------------------------------------------------------------------------

std::vector<CrewResult> CrewOptimizer::find_best_crews(
    Scenario scenario, int top_n,
    const std::set<std::string>& excluded) const {

    // Pre-filter: score all individuals
    std::vector<std::pair<double, size_t>> scored;
    scored.reserve(officers_.size());
    for (size_t i = 0; i < officers_.size(); ++i) {
        if (excluded.count(officers_[i].name)) continue;
        double s = score_scenario_individual(officers_[i], scenario);
        scored.push_back({s, i});
    }

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // Take top 40 candidates
    const int N = std::min(static_cast<int>(scored.size()), 40);
    std::vector<size_t> candidates;
    candidates.reserve(N);
    for (int i = 0; i < N; ++i) candidates.push_back(scored[i].second);

    // Evaluate all C(N,3) x 3 captain rotations
    std::vector<CrewResult> results;
    results.reserve(N * (N - 1) * (N - 2) / 2);  // rough estimate

    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            for (int k = j + 1; k < N; ++k) {
                size_t idxs[3] = {candidates[i], candidates[j], candidates[k]};
                // Try each as captain
                for (int c = 0; c < 3; ++c) {
                    int b0 = (c + 1) % 3;
                    int b1_idx = (c + 2) % 3;
                    auto r = score_scenario_crew(
                        officers_[idxs[c]],
                        officers_[idxs[b0]],
                        officers_[idxs[b1_idx]],
                        scenario);
                    results.push_back(std::move(r));
                }
            }
        }
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const CrewResult& a, const CrewResult& b) {
                  return a.score > b.score;
              });

    // Deduplicate by frozenset of names
    std::set<std::set<std::string>> seen;
    std::vector<CrewResult> unique;
    unique.reserve(top_n);
    for (auto& r : results) {
        std::set<std::string> key;
        key.insert(r.breakdown.captain);
        for (const auto& b : r.breakdown.bridge) key.insert(b);
        if (seen.count(key)) continue;
        seen.insert(key);
        unique.push_back(std::move(r));
        if (static_cast<int>(unique.size()) >= top_n) break;
    }

    return unique;
}

} // namespace stfc
