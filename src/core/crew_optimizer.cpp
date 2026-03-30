#include "core/crew_optimizer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <fstream>
#include <filesystem>

#include "json.hpp"

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

const char* mining_resource_str(MiningResource r) {
    switch (r) {
        case MiningResource::None:      return "none";
        case MiningResource::General:   return "general";
        case MiningResource::Gas:       return "gas";
        case MiningResource::Ore:       return "ore";
        case MiningResource::Crystal:   return "crystal";
        case MiningResource::Parsteel:  return "parsteel";
        case MiningResource::Tritanium: return "tritanium";
        case MiningResource::Dilithium: return "dilithium";
    }
    return "none";
}

const char* mining_objective_str(MiningObjective o) {
    switch (o) {
        case MiningObjective::None:      return "none";
        case MiningObjective::Speed:     return "speed";
        case MiningObjective::Protected: return "protected";
        case MiningObjective::Balanced:  return "balanced";
    }
    return "none";
}

MiningResource scenario_mining_resource(Scenario s) {
    switch (s) {
        case Scenario::MiningGas:     return MiningResource::Gas;
        case Scenario::MiningOre:     return MiningResource::Ore;
        case Scenario::MiningCrystal: return MiningResource::Crystal;
        case Scenario::MiningSpeed:
        case Scenario::MiningProtected:
        case Scenario::MiningGeneral:
        case Scenario::Loot:
            return MiningResource::General;
        default:
            return MiningResource::None;
    }
}

MiningObjective scenario_mining_objective(Scenario s) {
    switch (s) {
        case Scenario::MiningSpeed:
        case Scenario::MiningGas:
        case Scenario::MiningOre:
        case Scenario::MiningCrystal:
            return MiningObjective::Speed;
        case Scenario::MiningProtected:
            return MiningObjective::Protected;
        case Scenario::MiningGeneral:
        case Scenario::Loot:
            return MiningObjective::Balanced;
        default:
            return MiningObjective::None;
    }
}

// ---------------------------------------------------------------------------
// Ship type conversions
// ---------------------------------------------------------------------------

const char* ship_type_str(ShipType s) {
    switch (s) {
        case ShipType::Explorer:     return "explorer";
        case ShipType::Battleship:   return "battleship";
        case ShipType::Interceptor:  return "interceptor";
        case ShipType::Survey:       return "survey";
    }
    return "unknown";
}

ShipType ship_type_from_str(const std::string& s) {
    if (s == "survey")      return ShipType::Survey;
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
        {Scenario::Loot,            {ShipType::Survey, "Survey ships are the most efficient cargo and gathering hulls"}},
        {Scenario::Armada,          {ShipType::Explorer, "Explorers provide good balanced stats for sustained armada boss fights"}},
        {Scenario::MiningSpeed,     {ShipType::Survey, "Survey ships maximize mining throughput"}},
        {Scenario::MiningProtected, {ShipType::Survey, "Survey ships are the right base hull for protected cargo mining"}},
        {Scenario::MiningCrystal,   {ShipType::Survey, "Survey ships are purpose-built for crystal mining"}},
        {Scenario::MiningGas,       {ShipType::Survey, "Survey ships are purpose-built for gas mining"}},
        {Scenario::MiningOre,       {ShipType::Survey, "Survey ships are purpose-built for ore mining"}},
        {Scenario::MiningGeneral,   {ShipType::Survey, "Survey ships are the best all-around mining hulls"}},
    };
    return recs.at(s);
}

// ---------------------------------------------------------------------------
// Substring helper
// ---------------------------------------------------------------------------

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static double first_pct(const std::string& text) {
    static const std::regex pct_re(R"((\d+(?:\.\d+)?)\s*%)");
    std::smatch match;
    if (std::regex_search(text, match, pct_re)) {
        try {
            return std::stod(match[1].str());
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

static double captain_mining_value(const ClassifiedOfficer& off, Scenario scenario) {
    switch (scenario) {
        case Scenario::MiningSpeed:
            return off.cm_mining_speed_pct + off.oa_mining_speed_pct * 0.8 + off.oa_cargo_pct * 0.15 + off.oa_protected_cargo_pct * 0.1;
        case Scenario::MiningProtected:
            return off.cm_protected_cargo_pct + off.oa_protected_cargo_pct + off.cm_cargo_pct * 0.5 + off.oa_cargo_pct * 0.5;
        case Scenario::MiningGas:
            return off.cm_mining_gas_pct * 1.2 + off.oa_mining_gas_pct + off.cm_mining_speed_pct * 0.6 + off.oa_mining_speed_pct * 0.5 + off.oa_protected_cargo_pct * 0.2;
        case Scenario::MiningOre:
            return off.cm_mining_ore_pct * 1.2 + off.oa_mining_ore_pct + off.cm_mining_speed_pct * 0.6 + off.oa_mining_speed_pct * 0.5 + off.oa_protected_cargo_pct * 0.2;
        case Scenario::MiningCrystal:
            return off.cm_mining_crystal_pct * 1.2 + off.oa_mining_crystal_pct + off.cm_mining_speed_pct * 0.6 + off.oa_mining_speed_pct * 0.5 + off.oa_protected_cargo_pct * 0.2;
        case Scenario::MiningGeneral:
        case Scenario::Loot:
            return off.cm_mining_speed_pct + off.oa_mining_speed_pct + off.cm_cargo_pct * 0.5 + off.oa_cargo_pct * 0.7 + off.cm_protected_cargo_pct * 0.5 + off.oa_protected_cargo_pct * 0.7;
        default:
            return 0.0;
    }
}

static double bridge_mining_value(const ClassifiedOfficer& off, Scenario scenario) {
    switch (scenario) {
        case Scenario::MiningSpeed:
            return off.oa_mining_speed_pct + off.oa_cargo_pct * 0.15 + off.oa_protected_cargo_pct * 0.1;
        case Scenario::MiningProtected:
            return off.oa_protected_cargo_pct + off.oa_cargo_pct * 0.6;
        case Scenario::MiningGas:
            return off.oa_mining_gas_pct * 1.1 + off.oa_mining_speed_pct * 0.5 + off.oa_protected_cargo_pct * 0.25 + off.oa_cargo_pct * 0.15;
        case Scenario::MiningOre:
            return off.oa_mining_ore_pct * 1.1 + off.oa_mining_speed_pct * 0.5 + off.oa_protected_cargo_pct * 0.25 + off.oa_cargo_pct * 0.15;
        case Scenario::MiningCrystal:
            return off.oa_mining_crystal_pct * 1.1 + off.oa_mining_speed_pct * 0.5 + off.oa_protected_cargo_pct * 0.25 + off.oa_cargo_pct * 0.15;
        case Scenario::MiningGeneral:
        case Scenario::Loot:
            return off.oa_mining_speed_pct + off.oa_cargo_pct * 0.7 + off.oa_protected_cargo_pct * 0.7;
        default:
            return 0.0;
    }
}

static std::string extract_section(const std::string& text, const std::string& marker) {
    auto start = text.find(marker);
    if (start == std::string::npos) return "";
    auto next_cm = text.find(" cm:", start + marker.size());
    auto next_oa = text.find(" oa:", start + marker.size());
    auto next_bda = text.find(" bda:", start + marker.size());
    size_t end = std::string::npos;
    if (next_cm != std::string::npos) end = next_cm;
    if (next_oa != std::string::npos) end = next_oa;
    if (next_bda != std::string::npos) end = std::min(end, next_bda);
    if (end == std::string::npos) return text.substr(start);
    return text.substr(start, end - start);
}

static bool contains_phrase(const std::string& text, std::initializer_list<const char*> phrases) {
    for (const auto* phrase : phrases) {
        if (contains(text, phrase)) return true;
    }
    return false;
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
        o.cm_text = extract_section(o.description, "cm:");
        o.bda_text = extract_section(o.description, "bda:");
        o.oa_text = extract_section(o.description, "oa:");
        officers_.push_back(std::move(o));
    }
    classify_officers();
}

void CrewOptimizer::set_ship_type(ShipType ship) {
    ship_type_ = ship;
    classify_officers();
}

void CrewOptimizer::dump_mining_debug(std::ostream& os) const {
    static const std::set<std::string> watch = {
        "Three Of Eleven", "T'Pring", "Fess", "Mavery", "Quark", "Ten Of Eleven"
    };
    os << "=== Mining Debug Dump ===\n";
    for (const auto& off : officers_) {
        if (!watch.count(off.name)) continue;
        os << off.name
           << " | lvl=" << off.level
           << " rank=" << off.rank
           << " | cm=" << off.cm_pct
           << " oa=" << off.oa_pct << "\n";
        os << "  cm_text: " << off.cm_text << "\n";
        os << "  oa_text: " << off.oa_text << "\n";
        os << "  parsed cm_speed=" << off.cm_mining_speed_pct
           << " cm_gas=" << off.cm_mining_gas_pct
           << " cm_ore=" << off.cm_mining_ore_pct
           << " cm_crystal=" << off.cm_mining_crystal_pct
           << " cm_pcargo=" << off.cm_protected_cargo_pct
           << " cm_cargo=" << off.cm_cargo_pct << "\n";
        os << "  parsed oa_speed=" << off.oa_mining_speed_pct
           << " oa_gas=" << off.oa_mining_gas_pct
           << " oa_ore=" << off.oa_mining_ore_pct
           << " oa_crystal=" << off.oa_mining_crystal_pct
           << " oa_pcargo=" << off.oa_protected_cargo_pct
           << " oa_cargo=" << off.oa_cargo_pct << "\n";
    }
}

std::vector<std::string> CrewOptimizer::other_ship_types() const {
    switch (ship_type_) {
        case ShipType::Explorer:     return {"interceptor", "battleship"};
        case ShipType::Battleship:   return {"interceptor", "explorer"};
        case ShipType::Interceptor:  return {"battleship", "explorer"};
        case ShipType::Survey:       return {"interceptor", "battleship", "explorer"};
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
        off.cm_mining_speed_pct = 0.0;
        off.cm_mining_gas_pct = 0.0;
        off.cm_mining_ore_pct = 0.0;
        off.cm_mining_crystal_pct = 0.0;
        off.cm_protected_cargo_pct = 0.0;
        off.cm_cargo_pct = 0.0;
        off.oa_mining_speed_pct = 0.0;
        off.oa_mining_gas_pct = 0.0;
        off.oa_mining_ore_pct = 0.0;
        off.oa_mining_crystal_pct = 0.0;
        off.oa_protected_cargo_pct = 0.0;
        off.oa_cargo_pct = 0.0;

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

        const auto& cm = off.cm_text;
        const auto& oa = off.oa_text;
        if (contains_phrase(cm, {"mining speed", "base mining speed", "increase mining speed", "increases mining speed"})) {
            off.cm_mining_speed_pct = off.cm_pct;
        }
        if (contains_phrase(cm, {"gas mining", "mining speed (gas)", "gas mining speed"})) {
            off.cm_mining_gas_pct = off.cm_pct;
        }
        if (contains_phrase(cm, {"ore mining", "mining speed (ore)", "ore mining speed"})) {
            off.cm_mining_ore_pct = off.cm_pct;
        }
        if (contains_phrase(cm, {"crystal mining", "mining speed (crystal)", "crystal mining speed"})) {
            off.cm_mining_crystal_pct = off.cm_pct;
        }
        if (contains_phrase(cm, {"protected cargo", "increase protected cargo", "increases protected cargo"})) {
            off.cm_protected_cargo_pct = off.cm_pct;
        }
        if (contains_phrase(cm, {"max cargo", "cargo"})) {
            off.cm_cargo_pct = off.cm_pct;
        }

        if (contains_phrase(oa, {"mining speed", "mining rate", "mining efficiency", "increase mining speed", "increases mining speed"})) {
            off.oa_mining_speed_pct = off.oa_pct > 0.0 ? off.oa_pct : first_pct(oa);
        }
        if (contains_phrase(oa, {"gas mining", "gas mining speed"})) off.oa_mining_gas_pct = off.oa_pct > 0.0 ? off.oa_pct : first_pct(oa);
        if (contains_phrase(oa, {"ore mining", "ore mining speed"})) off.oa_mining_ore_pct = off.oa_pct > 0.0 ? off.oa_pct : first_pct(oa);
        if (contains_phrase(oa, {"crystal mining", "crystal mining speed"})) off.oa_mining_crystal_pct = off.oa_pct > 0.0 ? off.oa_pct : first_pct(oa);
        if (contains_phrase(oa, {"protected cargo", "increase protected cargo", "increases protected cargo"})) off.oa_protected_cargo_pct = off.oa_pct > 0.0 ? off.oa_pct : first_pct(oa);
        if (contains_phrase(oa, {"max cargo", "cargo"})) off.oa_cargo_pct = off.oa_pct > 0.0 ? off.oa_pct : first_pct(oa);

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

        // Ability amplifier — officers that boost all officer stats/abilities
        // "Increase all officer stats" (Kirk, Cadet Kirk, Kumak, etc.)
        // "effectiveness of all officer" / "all officers" patterns
        if (contains_any(d, {"effectiveness of all officer",
                             "all officer stats", "all officers",
                             "increase officer stats",
                             "increase all officer",
                             "officer ability"})) {
            off.ability_amplifier = true;
        }

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
             {"morale for", "inspire morale", "apply morale", "cause morale"},
             // Real roster text: "ship has morale", "with morale", "has morale",
             // "when ship has morale", "when the ship has morale"
             {"ship has morale", "with morale", "has morale",
              "when morale", "while morale"}},
            {"breach",
             {"hull breach for", "apply hull breach", "cause hull breach", "inflict hull breach"},
             // Real roster text: "opponent has hull breach", "with hull breach",
             // "has hull breach", "enemy player has hull breach",
             // "fighting a player with hull breach"
             {"has hull breach", "with hull breach", "opponent hull breach",
              "player has hull breach", "enemy has hull breach",
              "when hull breach"}},
            {"burning",
             {"burning for", "apply burning", "cause burning", "inflict burning"},
             // Real roster text: "opponent is burning", "is burning",
             // "has burning", "enemy player has burning", "afflicted by burning"
             {"is burning", "has burning", "opponent burning",
              "player has burning", "afflicted by burning",
              "when burning", "whilst burning"}},
            {"assimilate",
             {"assimilate for", "apply assimilate"},
             {"with assimilate", "has assimilate",
              "when assimilate", "is assimilated"}},
        };

        for (const auto& sp : state_patterns) {
            if (contains(off.effect, sp.state)) {
                off.pvp_tags.insert(sp.state);
                if (off.causes_effect) {
                    off.states_applied.insert(sp.state);
                } else {
                    // effect column lists the state, but causes_effect=N → benefits from it
                    off.states_benefit.insert(sp.state);
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
        if (off.mining_speed) score *= 6.0;
        else if (off.mining) score *= 3.0;
        if (off.mining_gas || off.mining_ore || off.mining_crystal) score *= 1.8;
        if (off.cargo) score *= 1.4;
        if (off.protected_cargo) score *= 1.2;
        if (off.warp) score *= 1.2;
        if (off.node_defense) score *= 1.2;
        if (off.is_pvp_specific && !off.mining && !off.cargo) score *= 0.15;
        if (!off.mining && !off.mining_speed && !off.cargo) score *= 0.1;
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
        if (off.mining_gas) score *= 7.0;
        else if (off.mining) score *= 2.5;
        if (off.mining_speed) score *= 2.0;
        if (off.cargo) score *= 1.5;
        if (off.protected_cargo) score *= 1.6;
        if (off.node_defense) score *= 1.2;
        if (off.is_pvp_specific && !off.mining && !off.cargo) score *= 0.15;
        if (!off.mining && !off.mining_gas && !off.cargo) score *= 0.08;
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

// OA% bonus: all bridge officers' Officer Abilities fire passively.
// Officers with higher OA% have stronger abilities. Dedicated BDAs are detected
// from the roster's CM/BDA slot and excluded since they are designed as
// below-deck officers.
// are excluded since they're designed as below-deck.
void CrewOptimizer::apply_oa_bonus(double& total, CrewBreakdown& bd,
                                    const ClassifiedOfficer* crew[3]) const {
    double oa_bonus = 0.0;
    for (int i = 0; i < 3; ++i) {
        if (crew[i]->is_bda()) continue;  // BDA officers don't have meaningful OA%
        double oa = crew[i]->oa_pct;
        if (oa <= 0) continue;
        // Cap at 1500% — beyond that it's usually a special mechanic (e.g. 290000%)
        // that doesn't linearly scale with the percentage value
        double capped = std::min(oa, 1500.0);
        oa_bonus += capped * 20.0;  // 20 per OA%, so 100% OA = +2000, 1500% = +30000
    }
    if (oa_bonus > 0) {
        total += oa_bonus;
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

    // OA% bonus — bridge officers with strong abilities
    apply_oa_bonus(total, bd, crew);

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

    // OA% bonus — bridge officers with strong abilities
    apply_oa_bonus(total, bd, crew);

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
    CandidateScore score_parts;

    for (int i = 0; i < 3; ++i) {
        double s = score_scenario_individual(*crew[i], scenario);
        bd.individual_scores[crew[i]->name] = s;
        total += s;
    }
    bd.raw_total = total;
    score_parts.raw_individual = total;

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
        double captain_slot_bonus = std::min(captain.cm_pct, 500.0) * 50.0;
        total += captain_slot_bonus;
        score_parts.captain_slot += captain_slot_bonus;
    }

    // Ability amplifier captain
    if (captain.ability_amplifier) {
        total += 50000;
        bd.amplifier_bonus = 50000;
        score_parts.synergy += 50000;
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
    } else if (scenario == Scenario::MiningSpeed ||
               scenario == Scenario::MiningProtected ||
               scenario == Scenario::MiningGas ||
               scenario == Scenario::MiningOre ||
               scenario == Scenario::MiningCrystal ||
               scenario == Scenario::MiningGeneral ||
               scenario == Scenario::Loot) {
        double mining_value = captain_mining_value(captain, scenario) +
                              bridge_mining_value(b1, scenario) +
                              bridge_mining_value(b2, scenario);
        scenario_bonus += mining_value * 400.0;
        bd.synergy_notes.push_back(
            "Seat-aware mining value: " + std::to_string(static_cast<int>(std::round(mining_value))) + "%");

        double bridge_specialists = bridge_mining_value(b1, scenario) + bridge_mining_value(b2, scenario);
        if (bridge_specialists > 0.0) {
            scenario_bonus += bridge_specialists * 120.0;
        }

        if (captain_mining_value(captain, scenario) <= 0.0) {
            double penalty = total * 0.18;
            total -= penalty;
            bd.penalty_total += penalty;
            score_parts.penalties += penalty;
            bd.penalties.push_back("Captain has no relevant mining captain-seat value");
        }

        if (scenario == Scenario::MiningGas &&
            (captain.cm_mining_gas_pct > 0.0 || b1.oa_mining_gas_pct > 0.0 || b2.oa_mining_gas_pct > 0.0)) {
            scenario_bonus += 20000;
            bd.synergy_notes.push_back("Gas-specific mining effects detected");
        }
        if (scenario == Scenario::MiningSpeed &&
            (captain.cm_mining_speed_pct > 0.0 || b1.oa_mining_speed_pct > 0.0 || b2.oa_mining_speed_pct > 0.0)) {
            scenario_bonus += 15000;
            bd.synergy_notes.push_back("Mining-speed effects detected");
        }
        if (scenario == Scenario::MiningProtected &&
            (captain.cm_protected_cargo_pct > 0.0 || b1.oa_protected_cargo_pct > 0.0 || b2.oa_protected_cargo_pct > 0.0)) {
            scenario_bonus += 15000;
            bd.synergy_notes.push_back("Protected cargo effects detected");
        }
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
    score_parts.scenario += scenario_bonus;

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
        score_parts.synergy += weakness_bonus;
    }

    // Scenario-specific penalties
    if (scenario == Scenario::BaseCracker) {
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->mining || crew[i]->cargo) {
                double penalty = total * 0.15;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is a mining/cargo officer -- useless in station combat");
            }
            if (crew[i]->base_defend && !crew[i]->base_attack) {
                double penalty = total * 0.2;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is a station DEFENDER -- wrong role for attacking");
            }
        }
    } else if (scenario == Scenario::Armada) {
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->non_armada_only) {
                double penalty = total * 0.3;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' explicitly does NOT work in armadas");
            }
            if (crew[i]->mining || crew[i]->cargo) {
                double penalty = total * 0.15;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is a mining/cargo officer -- useless in armada");
            }
        }
    } else if (scenario == Scenario::Loot) {
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->is_pvp_specific && !crew[i]->mining && !crew[i]->cargo && !crew[i]->loot) {
                double penalty = total * 0.1;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is PvP-only -- limited value while mining");
            }
        }
    } else if (scenario == Scenario::MiningSpeed || scenario == Scenario::MiningGas) {
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->is_pvp_specific && !crew[i]->mining && !crew[i]->cargo) {
                double penalty = total * 0.2;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is combat-focused -- weak mining value");
            }
        }
    }

    // OA% bonus — bridge officers with strong abilities
    double before_oa = total;
    apply_oa_bonus(total, bd, crew);
    score_parts.bridge_slot += (total - before_oa);

    // OA ship-lock penalty
    double before_ship_penalty = total;
    apply_oa_ship_penalty(total, bd, b1, b2);
    if (before_ship_penalty > total) {
        score_parts.penalties += (before_ship_penalty - total);
        bd.penalty_total += (before_ship_penalty - total);
    }

    bd.synergy_bonus = score_parts.synergy;

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

    // Take top 40 candidates by individual score
    const int BASE_N = std::min(static_cast<int>(scored.size()), 40);
    std::set<size_t> candidate_set;
    for (int i = 0; i < BASE_N; ++i) candidate_set.insert(scored[i].second);

    // Synergy reserve: also include officers outside top 40 who have synergy
    // value that can't be captured by individual scoring alone.
    // State applicators/beneficiaries, ability amplifiers, and high CM% officers
    // are often low-stat but create massive crew-level bonuses.
    for (const auto& [s, idx] : scored) {
        if (candidate_set.count(idx)) continue;
        const auto& o = officers_[idx];
        bool has_synergy = !o.states_applied.empty()
                        || !o.states_benefit.empty()
                        || o.ability_amplifier
                        || o.cm_pct >= 40;
        if (has_synergy) candidate_set.insert(idx);
    }

    // Cap at 55 to keep search time reasonable (C(55,3)*3 = ~78K evals)
    std::vector<size_t> candidates(candidate_set.begin(), candidate_set.end());
    if (candidates.size() > 55) {
        // Sort by individual score descending, keep top 55
        std::map<size_t, double> score_map;
        for (const auto& [s, idx] : scored) score_map[idx] = s;
        std::sort(candidates.begin(), candidates.end(),
                  [&score_map](size_t a, size_t b) {
                      return score_map[a] > score_map[b];
                  });
        candidates.resize(55);
    }
    const int N = static_cast<int>(candidates.size());

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

// ---------------------------------------------------------------------------
// BDA suggestion system (port of find_best_bda)
// ---------------------------------------------------------------------------

std::vector<BdaSuggestion> CrewOptimizer::find_best_bda(
    const std::string& captain_name,
    const std::vector<std::string>& bridge_names,
    Scenario mode,
    int top_n,
    const std::set<std::string>& excluded) const
{
    // Build exclusion set: crew members + externally excluded
    std::set<std::string> excl = excluded;
    excl.insert(captain_name);
    for (const auto& b : bridge_names) excl.insert(b);

    // Profile the existing crew
    std::set<std::string> crew_applied, crew_benefit;
    bool has_crit = false, has_shield = false, has_mitigation = false;

    for (const auto& off : officers_) {
        if (off.name == captain_name ||
            std::find(bridge_names.begin(), bridge_names.end(), off.name) != bridge_names.end()) {
            crew_applied.insert(off.states_applied.begin(), off.states_applied.end());
            crew_benefit.insert(off.states_benefit.begin(), off.states_benefit.end());
            if (off.crit_related) has_crit = true;
            if (off.shield_related) has_shield = true;
            if (off.mitigation_related) has_mitigation = true;
        }
    }

    // Score every non-excluded officer
    struct ScoredBda {
        const ClassifiedOfficer* off;
        double score;
        bool dedicated_bda;
        std::vector<std::string> reasons;
    };
    std::vector<ScoredBda> candidates;

    for (const auto& off : officers_) {
        if (excl.count(off.name)) continue;
        if (off.attack <= 0) continue;

        double score = 0.0;
        std::vector<std::string> reasons;
        bool dedicated_bda = off.is_bda();

        // 1. Base OA value
        if (dedicated_bda) {
            score += 50000.0;
            reasons.push_back("Designed as BDA officer");
        } else {
            // Non-BDA officers are fallback options only.
            score -= 25000.0;
        }
        score += std::min(off.oa_pct, 500.0) * 10.0;
        double bda_effect = off.bda_effect_pct();
        if (bda_effect > 0) {
            score += std::min(bda_effect, 300.0) * 120.0;
            reasons.push_back("Strong BDA effect: " + std::to_string((int)std::round(bda_effect)) + "%");
        }

        // 2. State synergy: BDA applies states the crew benefits from
        std::set<std::string> chain_add;
        for (const auto& s : off.states_applied) {
            if (crew_benefit.count(s)) chain_add.insert(s);
        }
        if (!chain_add.empty()) {
            score += static_cast<double>(chain_add.size()) * 20000.0;
            std::string states_str;
            for (const auto& s : chain_add) {
                if (!states_str.empty()) states_str += ", ";
                states_str += s;
            }
            reasons.push_back("Applies " + states_str + " (crew benefits)");
        }

        // 3. State synergy: BDA benefits from states the crew applies
        std::set<std::string> chain_receive;
        for (const auto& s : off.states_benefit) {
            if (crew_applied.count(s)) chain_receive.insert(s);
        }
        if (!chain_receive.empty()) {
            score += static_cast<double>(chain_receive.size()) * 15000.0;
            std::string states_str;
            for (const auto& s : chain_receive) {
                if (!states_str.empty()) states_str += ", ";
                states_str += s;
            }
            reasons.push_back("Benefits from " + states_str + " (crew applies)");
        }

        // 4. Cover missing crew capabilities
        if (!has_crit && off.crit_related) {
            score += 10000.0;
            reasons.push_back("Adds crit coverage");
        }
        if (!has_shield && off.shield_related) {
            score += 8000.0;
            reasons.push_back("Adds shield support");
        }
        if (!has_mitigation && off.mitigation_related) {
            score += 8000.0;
            reasons.push_back("Adds mitigation");
        }

        // 5. Scenario fit: below-deck picks still need to match the dock's job.
        switch (mode) {
        case Scenario::PvP:
            if (off.is_pvp_specific) {
                score += 15000.0;
                reasons.push_back("Fits PvP dock");
            }
            if (off.crit_related) score += 5000.0;
            if (off.shots_related) score += 5000.0;
            if (off.isolytic_related) score += 5000.0;
            if (off.is_pve_specific && !off.is_pvp_specific) score -= 12000.0;
            break;

        case Scenario::Hybrid:
            if (off.is_dual_use) {
                score += 12000.0;
                reasons.push_back("Fits hybrid dock");
            }
            if (off.is_pve_specific && !off.is_dual_use) score -= 4000.0;
            if (off.is_pvp_specific && !off.is_dual_use) score -= 4000.0;
            break;

        case Scenario::BaseCracker:
            if (off.base_attack) {
                score += 20000.0;
                reasons.push_back("Fits station attack");
            }
            if (off.weapon_delay) score += 8000.0;
            if (off.base_defend) score -= 10000.0;
            if (off.mining || off.cargo) score -= 12000.0;
            break;

        case Scenario::PvEHostile:
            if (off.pve_hostile) {
                score += 18000.0;
                reasons.push_back("Fits hostile grinding");
            }
            if (off.repair) score += 6000.0;
            if (off.loot) score += 4000.0;
            if (off.is_pvp_specific && !off.pve_hostile) score -= 10000.0;
            break;

        case Scenario::MissionBoss:
            if (off.mission_boss || off.pve_hostile) {
                score += 16000.0;
                reasons.push_back("Fits mission boss");
            }
            if (off.crit_related) score += 6000.0;
            if (off.isolytic_related) score += 6000.0;
            if (off.armada && !off.pve_hostile) score -= 5000.0;
            break;

        case Scenario::Loot:
            if (off.loot) {
                score += 18000.0;
                reasons.push_back("Fits loot dock");
            }
            if (off.mining) score += 12000.0;
            if (off.cargo) score += 10000.0;
            if (off.warp) score += 4000.0;
            if (contains(off.bda_text, "loot") || contains(off.bda_text, "reward")) score += 12000.0;
            if (contains(off.bda_text, "cargo")) score += 8000.0;
            if (!off.loot && !off.mining && !off.cargo && !off.warp) score -= 10000.0;
            break;

        case Scenario::Armada:
            if (off.armada) {
                score += 20000.0;
                reasons.push_back("Fits armada dock");
            }
            if (off.armada_solo) score += 6000.0;
            if (off.crit_related) score += 5000.0;
            if (off.isolytic_related) score += 5000.0;
            if (off.non_armada_only) score -= 15000.0;
            break;

        case Scenario::MiningSpeed:
            if (off.mining_speed) {
                score += 25000.0;
                reasons.push_back("Fits mining speed dock");
            }
            if (off.mining) score += 12000.0;
            if (off.cargo) score += 5000.0;
            if (contains(off.bda_text, "mining speed") || contains(off.bda_text, "mining rate") || contains(off.bda_text, "mining efficiency")) {
                score += 16000.0;
            }
            if (!off.mining_speed && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningProtected:
            if (off.protected_cargo) {
                score += 25000.0;
                reasons.push_back("Fits protected cargo dock");
            }
            if (off.cargo) score += 12000.0;
            if (off.node_defense) score += 8000.0;
            if (off.mining) score += 5000.0;
            if (contains(off.bda_text, "protected cargo") || contains(off.bda_text, "protect cargo")) {
                score += 16000.0;
            }
            if (!off.protected_cargo && !off.cargo && !off.node_defense) score -= 12000.0;
            break;

        case Scenario::MiningCrystal:
            if (off.mining_crystal) {
                score += 25000.0;
                reasons.push_back("Fits crystal mining dock");
            }
            if (off.mining_speed) score += 10000.0;
            if (off.mining) score += 8000.0;
            if (off.cargo || off.protected_cargo) score += 4000.0;
            if (contains(off.bda_text, "crystal")) score += 16000.0;
            if (!off.mining_crystal && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningGas:
            if (off.mining_gas) {
                score += 25000.0;
                reasons.push_back("Fits gas mining dock");
            }
            if (off.mining_speed) score += 10000.0;
            if (off.mining) score += 8000.0;
            if (off.cargo || off.protected_cargo) score += 4000.0;
            if (contains(off.bda_text, "gas")) score += 16000.0;
            if (!off.mining_gas && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningOre:
            if (off.mining_ore) {
                score += 25000.0;
                reasons.push_back("Fits ore mining dock");
            }
            if (off.mining_speed) score += 10000.0;
            if (off.mining) score += 8000.0;
            if (off.cargo || off.protected_cargo) score += 4000.0;
            if (contains(off.bda_text, "ore")) score += 16000.0;
            if (!off.mining_ore && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningGeneral:
            if (off.mining) {
                score += 18000.0;
                reasons.push_back("Fits mining dock");
            }
            if (off.mining_speed) score += 12000.0;
            if (off.cargo) score += 10000.0;
            if (off.protected_cargo) score += 8000.0;
            if (off.node_defense) score += 5000.0;
            if (contains(off.bda_text, "mining") || contains(off.bda_text, "cargo")) score += 12000.0;
            if (!off.mining && !off.cargo && !off.protected_cargo) score -= 12000.0;
            break;
        }

        // 6. Stat contribution (BDA gives partial stats)
        double stat_score = off.attack * 0.2 + off.defense * 0.15 + off.health * 0.1;
        score += stat_score;

        // 7. Ship-type match multiplier
        if (off.is_ship_specific) {
            score *= 1.2;
        }

        // 8. OA ship compatibility penalty
        if (!oa_works_on_ship(off)) {
            score *= 0.3;
        }

        // 9. Hybrid dual-use bonus
        if (mode == Scenario::Hybrid && off.is_dual_use) {
            score *= 1.15;
        }

        // 10. Default reason
        if (reasons.empty()) {
            reasons.push_back("General stat contribution");
        }

        candidates.push_back({&off, score, dedicated_bda, std::move(reasons)});
    }

    // Sort dedicated BDAs ahead of fallback bridge officers.
    std::sort(candidates.begin(), candidates.end(),
              [](const ScoredBda& a, const ScoredBda& b) {
                  if (a.dedicated_bda != b.dedicated_bda) {
                      return a.dedicated_bda > b.dedicated_bda;
                  }
                  return a.score > b.score;
              });

    // Build result
    std::vector<BdaSuggestion> result;
    int n = std::min(top_n, static_cast<int>(candidates.size()));
    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        const auto& c = candidates[i];
        BdaSuggestion bda;
        bda.name = c.off->name;
        bda.level = c.off->level;
        bda.rank = c.off->rank;
        bda.attack = c.off->attack;
        bda.defense = c.off->defense;
        bda.health = c.off->health;
        bda.oa_pct = c.off->oa_pct;
        bda.display = c.off->description;
        bda.score = std::round(c.score);
        bda.reasons = c.reasons;
        result.push_back(std::move(bda));
    }

    return result;
}

// ---------------------------------------------------------------------------
// 7-dock loadout optimization (port of optimize_dock_loadout)
// ---------------------------------------------------------------------------

LoadoutResult CrewOptimizer::optimize_dock_loadout(
    const std::vector<DockConfig>& dock_configs,
    const std::vector<OwnedShipCandidate>& owned_ships,
    int top_n)
{
    LoadoutResult loadout;
    const size_t n_docks = std::min(dock_configs.size(), size_t(7));
    loadout.docks.resize(n_docks);
    std::set<std::string> excluded;
    std::set<std::string> used_ships;
    ShipType original_ship = ship_type_;

    auto score_owned_ship = [](const OwnedShipCandidate& ship, Scenario scenario) {
        std::string lower_name = ship.name;
        for (auto& c : lower_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        int score = ship.tier * 12 + ship.level + ship.rarity * 24;

        auto has_name = [&](const std::string& token) {
            return lower_name.find(token) != std::string::npos;
        };

        auto grade_band_bonus = [&](int target_grade) {
            int diff = std::abs(ship.grade - target_grade);
            if (diff == 0) return 140;
            if (diff == 1) return 80;
            if (diff == 2) return 30;
            return -120 * diff;
        };

        switch (scenario) {
            case Scenario::PvP:
            case Scenario::Hybrid:
            case Scenario::BaseCracker:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Interceptor) score += 90;
                if (ship.ship_type == ShipType::Battleship) score += 70;
                if (ship.ship_type == ShipType::Explorer) score += 50;
                if (has_name("revenant") || has_name("cube")) score += 120;
                break;
            case Scenario::Armada:
            case Scenario::MissionBoss:
            case Scenario::PvEHostile:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Explorer) score += 95;
                if (ship.ship_type == ShipType::Battleship) score += 70;
                if (ship.ship_type == ShipType::Interceptor) score += 50;
                if (has_name("protector") || has_name("voyager") || has_name("cerritos")) score += 120;
                break;
            case Scenario::Loot:
            case Scenario::MiningSpeed:
            case Scenario::MiningProtected:
            case Scenario::MiningCrystal:
            case Scenario::MiningGas:
            case Scenario::MiningOre:
            case Scenario::MiningGeneral:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Survey) score += 120;
                if (ship.ship_type == ShipType::Explorer) score += 30;
                if (has_name("selkie")) score += 240;
                if (has_name("meridian")) score += 140;
                if (has_name("feesha") || has_name("d'vor")) score -= 180;
                if (has_name("north star")) score -= 220;
                break;
        }
        return score;
    };

    auto choose_ship_for_dock = [&](const DockConfig& cfg, const std::string& recommended) {
        if (!cfg.ship_override.empty()) return cfg.ship_override;

        auto requires_survey = [&](Scenario scenario) {
            switch (scenario) {
                case Scenario::Loot:
                case Scenario::MiningSpeed:
                case Scenario::MiningProtected:
                case Scenario::MiningCrystal:
                case Scenario::MiningGas:
                case Scenario::MiningOre:
                case Scenario::MiningGeneral:
                    return true;
                default:
                    return false;
            }
        };

        int best_score = -1;
        std::string best_name;
        for (const auto& ship : owned_ships) {
            if (ship.name.empty() || used_ships.count(ship.name)) continue;
            if (requires_survey(cfg.scenario) && ship.ship_type != ShipType::Survey) continue;
            int score = score_owned_ship(ship, cfg.scenario);
            if (score > best_score) {
                best_score = score;
                best_name = ship.name;
            }
        }
        return best_name.empty() ? recommended : best_name;
    };

    auto choose_ship_type = [&](const std::string& ship_name, Scenario scenario) {
        for (const auto& ship : owned_ships) {
            if (ship.name == ship_name) return ship.ship_type;
        }
        return ship_type_from_str(ship_name.empty() ? ship_type_str(get_ship_recommendation(scenario).best) : ship_name);
    };

    for (size_t i = 0; i < n_docks; ++i) {
        const auto& cfg = dock_configs[i];
        auto& dr = loadout.docks[i];

        dr.dock_num = static_cast<int>(i) + 1;
        dr.scenario = cfg.scenario;
        dr.scenario_label_str = scenario_label(cfg.scenario);
        dr.priority = cfg.priority;
        dr.purpose = cfg.purpose;
        dr.mining_resource = cfg.mining_resource == MiningResource::None
            ? scenario_mining_resource(cfg.scenario) : cfg.mining_resource;
        dr.mining_objective = cfg.mining_objective == MiningObjective::None
            ? scenario_mining_objective(cfg.scenario) : cfg.mining_objective;

        const auto& rec = get_ship_recommendation(cfg.scenario);
        dr.ship_recommended = ship_type_str(rec.best);
        dr.ship_used = choose_ship_for_dock(cfg, dr.ship_recommended);
        dr.locked = cfg.locked;
        if (!dr.ship_used.empty()) used_ships.insert(dr.ship_used);

        if (cfg.locked && !cfg.locked_captain.empty()) {
            dr.captain = cfg.locked_captain;
            dr.bridge = cfg.locked_bridge;

            const ClassifiedOfficer* cap_off = nullptr;
            const ClassifiedOfficer* b1_off = nullptr;
            const ClassifiedOfficer* b2_off = nullptr;
            for (const auto& off : officers_) {
                if (off.name == cfg.locked_captain) cap_off = &off;
                if (cfg.locked_bridge.size() > 0 && off.name == cfg.locked_bridge[0]) b1_off = &off;
                if (cfg.locked_bridge.size() > 1 && off.name == cfg.locked_bridge[1]) b2_off = &off;
            }
            if (cap_off && b1_off && b2_off) {
                CrewResult cr = score_scenario_crew(*cap_off, *b1_off, *b2_off, cfg.scenario);
                dr.score = cr.score;
                dr.breakdown = cr.breakdown;
            }

            excluded.insert(cfg.locked_captain);
            for (const auto& b : cfg.locked_bridge) excluded.insert(b);
            continue;
        }

        // Switch ship type if needed
        ShipType target_ship = choose_ship_type(dr.ship_used, cfg.scenario);
        if (target_ship != ship_type_) set_ship_type(target_ship);

        auto crews = find_best_crews(cfg.scenario, top_n, excluded);

        if (!crews.empty()) {
            const auto& best = crews[0];
            dr.captain = best.breakdown.captain;
            dr.bridge = best.breakdown.bridge;
            dr.score = best.score;
            dr.breakdown = best.breakdown;

            // Reserve these officers
            excluded.insert(dr.captain);
            for (const auto& b : dr.bridge) excluded.insert(b);

            // Find BDA suggestions
            dr.bda_suggestions = find_best_bda(
                dr.captain, dr.bridge, cfg.scenario, 3, excluded);
        } else {
            dr.captain = "N/A";
            dr.bridge = {"N/A", "N/A"};
            dr.score = 0.0;
        }

        // Restore ship type if changed
        if (ship_type_ != original_ship) set_ship_type(original_ship);
    }

    // -----------------------------------------------------------------------
    // Finalize: build excluded list
    // -----------------------------------------------------------------------
    loadout.excluded_officers.assign(excluded.begin(), excluded.end());
    std::sort(loadout.excluded_officers.begin(), loadout.excluded_officers.end());
    loadout.total_officers_used = static_cast<int>(excluded.size());

    return loadout;
}

// ---------------------------------------------------------------------------
// Loadout JSON persistence
// ---------------------------------------------------------------------------

using json = nlohmann::json;

bool CrewOptimizer::save_loadout(const LoadoutResult& result,
                                  const std::string& path,
                                  ShipType ship,
                                  const std::string& ship_display)
{
    json j;

    json docks_arr = json::array();
    for (const auto& dr : result.docks) {
        json dj;
        dj["dock_num"] = dr.dock_num;
        dj["scenario"] = scenario_str(dr.scenario);
        dj["scenario_label"] = dr.scenario_label_str;
        dj["ship_recommended"] = dr.ship_recommended;
        dj["ship_used"] = dr.ship_used;
        dj["locked"] = dr.locked;

        json crew;
        crew["captain"] = dr.captain;
        crew["bridge"] = dr.bridge;
        crew["score"] = static_cast<int>(std::round(dr.score));

        // Flatten breakdown (Python compat: synergy_notes, penalties, scenario_bonus, individual_scores)
        json syn_notes = json::array();
        for (const auto& n : dr.breakdown.synergy_notes) syn_notes.push_back(n);
        crew["synergy_notes"] = syn_notes;

        json penalties = json::array();
        for (const auto& p : dr.breakdown.penalties) penalties.push_back(p);
        crew["penalties"] = penalties;

        crew["scenario_bonus"] = static_cast<int>(dr.breakdown.scenario_bonus);

        json ind_scores;
        for (const auto& [name, sc] : dr.breakdown.individual_scores) {
            ind_scores[name] = static_cast<int>(std::round(sc));
        }
        crew["individual_scores"] = ind_scores;

        dj["crew"] = crew;

        // BDA suggestions
        json bda_arr = json::array();
        for (const auto& b : dr.bda_suggestions) {
            json bj;
            bj["name"] = b.name;
            bj["level"] = b.level;
            bj["rank"] = b.rank;
            bj["attack"] = b.attack;
            bj["defense"] = b.defense;
            bj["health"] = b.health;
            bj["oa_pct"] = b.oa_pct;
            bj["display"] = b.display;
            bj["score"] = static_cast<int>(b.score);
            json reasons_arr = json::array();
            for (const auto& r : b.reasons) reasons_arr.push_back(r);
            bj["reasons"] = reasons_arr;
            bda_arr.push_back(bj);
        }
        dj["bda_suggestions"] = bda_arr;

        docks_arr.push_back(dj);
    }

    j["docks"] = docks_arr;

    json excl_arr = json::array();
    for (const auto& e : result.excluded_officers) excl_arr.push_back(e);
    j["excluded_officers"] = excl_arr;
    j["total_officers_used"] = result.total_officers_used;
    j["ship"] = ship_type_str(ship);
    j["ship_display"] = ship_display.empty() ? ship_type_str(ship) : ship_display;

    namespace fs = std::filesystem;
    fs::path out_path(path);
    const fs::path parent = out_path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return f.good();
}

bool CrewOptimizer::load_loadout(LoadoutResult& result, const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        json j = json::parse(f);

        result.docks.clear();
        if (j.contains("docks") && j["docks"].is_array()) {
            for (const auto& dj : j["docks"]) {
                DockResult dr;
                if (dj.contains("dock_num")) dr.dock_num = dj["dock_num"].get<int>();
                if (dj.contains("scenario")) dr.scenario = scenario_from_str(dj["scenario"].get<std::string>());
                if (dj.contains("scenario_label")) dr.scenario_label_str = dj["scenario_label"].get<std::string>();
                if (dj.contains("ship_recommended")) dr.ship_recommended = dj["ship_recommended"].get<std::string>();
                if (dj.contains("ship_used")) dr.ship_used = dj["ship_used"].get<std::string>();
                if (dj.contains("locked")) dr.locked = dj["locked"].get<bool>();

                if (dj.contains("crew")) {
                    const auto& cj = dj["crew"];
                    if (cj.contains("captain")) dr.captain = cj["captain"].get<std::string>();
                    if (cj.contains("bridge")) {
                        for (const auto& b : cj["bridge"]) {
                            dr.bridge.push_back(b.get<std::string>());
                        }
                    }
                    if (cj.contains("score")) dr.score = cj["score"].get<double>();

                    // Load flattened breakdown fields
                    dr.breakdown.captain = dr.captain;
                    dr.breakdown.bridge = dr.bridge;
                    if (cj.contains("synergy_notes")) {
                        for (const auto& n : cj["synergy_notes"]) {
                            dr.breakdown.synergy_notes.push_back(n.get<std::string>());
                        }
                    }
                    if (cj.contains("penalties")) {
                        for (const auto& p : cj["penalties"]) {
                            dr.breakdown.penalties.push_back(p.get<std::string>());
                        }
                    }
                    if (cj.contains("scenario_bonus")) {
                        dr.breakdown.scenario_bonus = cj["scenario_bonus"].get<double>();
                    }
                    if (cj.contains("individual_scores")) {
                        for (auto& [name, sc] : cj["individual_scores"].items()) {
                            dr.breakdown.individual_scores[name] = sc.get<double>();
                        }
                    }
                }

                if (dj.contains("bda_suggestions")) {
                    for (const auto& bj : dj["bda_suggestions"]) {
                        BdaSuggestion bda;
                        if (bj.contains("name")) bda.name = bj["name"].get<std::string>();
                        if (bj.contains("level")) bda.level = bj["level"].get<int>();
                        if (bj.contains("rank")) bda.rank = bj["rank"].get<int>();
                        if (bj.contains("attack")) bda.attack = bj["attack"].get<double>();
                        if (bj.contains("defense")) bda.defense = bj["defense"].get<double>();
                        if (bj.contains("health")) bda.health = bj["health"].get<double>();
                        if (bj.contains("oa_pct")) bda.oa_pct = bj["oa_pct"].get<double>();
                        if (bj.contains("display")) bda.display = bj["display"].get<std::string>();
                        if (bj.contains("score")) bda.score = bj["score"].get<double>();
                        if (bj.contains("reasons")) {
                            for (const auto& r : bj["reasons"]) {
                                bda.reasons.push_back(r.get<std::string>());
                            }
                        }
                        dr.bda_suggestions.push_back(std::move(bda));
                    }
                }

                result.docks.push_back(std::move(dr));
            }
        }

        if (j.contains("excluded_officers")) {
            result.excluded_officers.clear();
            for (const auto& e : j["excluded_officers"]) {
                result.excluded_officers.push_back(e.get<std::string>());
            }
        }
        if (j.contains("total_officers_used")) {
            result.total_officers_used = j["total_officers_used"].get<int>();
        }

        return true;
    } catch (...) {
        return false;
    }
}

} // namespace stfc
