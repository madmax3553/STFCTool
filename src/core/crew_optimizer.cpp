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

// BDA mining value — scores below-deck officers' mining ability by their
// parsed bda_text percentages, analogous to captain/bridge mining helpers.
// BDA effects are weaker than bridge OA (partial contribution), so scale
// is ~60% of bridge_mining_value weights.
static double bda_mining_value(const ClassifiedOfficer& off, Scenario scenario) {
    switch (scenario) {
        case Scenario::MiningSpeed:
            return off.bda_mining_speed_pct * 0.6 + off.bda_cargo_pct * 0.1 + off.bda_protected_cargo_pct * 0.06;
        case Scenario::MiningProtected:
            return off.bda_protected_cargo_pct * 0.6 + off.bda_cargo_pct * 0.35;
        case Scenario::MiningGas:
            return off.bda_mining_gas_pct * 0.65 + off.bda_mining_speed_pct * 0.3 + off.bda_protected_cargo_pct * 0.15;
        case Scenario::MiningOre:
            return off.bda_mining_ore_pct * 0.65 + off.bda_mining_speed_pct * 0.3 + off.bda_protected_cargo_pct * 0.15;
        case Scenario::MiningCrystal:
            return off.bda_mining_crystal_pct * 0.65 + off.bda_mining_speed_pct * 0.3 + off.bda_protected_cargo_pct * 0.15;
        case Scenario::MiningGeneral:
        case Scenario::Loot:
            return off.bda_mining_speed_pct * 0.6 + off.bda_cargo_pct * 0.4 + off.bda_protected_cargo_pct * 0.4;
        default:
            return 0.0;
    }
}

// Word-boundary-aware contains: ensures needle is not part of a larger word.
// E.g. contains_word("more damage", "ore") returns false, contains_word("ore mining", "ore") returns true.
static bool contains_word(const std::string& haystack, const std::string& needle) {
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        bool left_ok = (pos == 0 || !std::isalpha(static_cast<unsigned char>(haystack[pos - 1])));
        size_t end = pos + needle.size();
        bool right_ok = (end >= haystack.size() || !std::isalpha(static_cast<unsigned char>(haystack[end])));
        if (left_ok && right_ok) return true;
        ++pos;
    }
    return false;
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
// Officer skills bootstrap data (from data/officer_skills.json)
// ---------------------------------------------------------------------------

using json_t = nlohmann::json;

// Normalize officer name for lookup: lowercase, strip non-breaking spaces,
// collapse whitespace.
static std::string normalize_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    bool last_space = false;
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char c = name[i];
        // Skip UTF-8 non-breaking space (0xC2 0xA0)
        if (c == 0xC2 && i + 1 < name.size() &&
            static_cast<unsigned char>(name[i + 1]) == 0xA0) {
            if (!last_space) { out += ' '; last_space = true; }
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (!last_space) { out += ' '; last_space = true; }
            continue;
        }
        last_space = false;
        out += static_cast<char>(std::tolower(c));
    }
    // Trim trailing space
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Load and cache the officer skills JSON as a map: normalized_name -> json object.
// Also stores alternate_name -> same json object for fallback matching.
// Returns empty map if file not found (non-fatal).
static const std::map<std::string, const json_t*>& get_officer_skills_map() {
    static std::map<std::string, const json_t*> name_map;
    static json_t root;
    static bool loaded = false;

    if (!loaded) {
        loaded = true;
        // Try multiple paths: relative to CWD and relative to executable
        for (const char* path : {
            "data/officer_skills.json",
            "../data/officer_skills.json",
        }) {
            std::ifstream f(path);
            if (f.is_open()) {
                try {
                    root = json_t::parse(f);
                    if (root.contains("officers") && root["officers"].is_array()) {
                        for (const auto& entry : root["officers"]) {
                            if (entry.contains("name") && entry["name"].is_string()) {
                                std::string norm = normalize_name(entry["name"].get<std::string>());
                                name_map[norm] = &entry;
                            }
                            if (entry.contains("alternate_name") && entry["alternate_name"].is_string()) {
                                std::string alt_norm = normalize_name(entry["alternate_name"].get<std::string>());
                                if (name_map.find(alt_norm) == name_map.end()) {
                                    name_map[alt_norm] = &entry;
                                }
                            }
                        }
                    }
                } catch (...) {
                    // Parse failure — leave map empty
                    name_map.clear();
                }
                break;
            }
        }
    }
    return name_map;
}

// Look up an officer in the bootstrap JSON by name.
// Returns nullptr if not found.
static const json_t* find_officer_skills(const std::string& officer_name) {
    const auto& skills_map = get_officer_skills_map();
    std::string norm = normalize_name(officer_name);
    auto it = skills_map.find(norm);
    if (it != skills_map.end()) return it->second;
    return nullptr;
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
        o.officer_class = r.officer_class;
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

        // Transfer structured API ability data (populated by build_roster_from_sync
        // or Phase 0.4; empty vectors are fine — bootstrap JSON fills gaps)
        o.oa_values = std::move(r.api_oa_values);
        o.oa_is_pct = r.api_oa_is_pct;
        if (!r.api_cm_values.empty()) o.cm_value = r.api_cm_values[0];
        if (!r.api_bda_values.empty()) {
            o.bda_values = std::move(r.api_bda_values);
        }

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
        off.bda_mining_speed_pct = 0.0;
        off.bda_mining_gas_pct = 0.0;
        off.bda_mining_ore_pct = 0.0;
        off.bda_mining_crystal_pct = 0.0;
        off.bda_protected_cargo_pct = 0.0;
        off.bda_cargo_pct = 0.0;

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
        // Fix cargo double-dip: check protected_cargo first, only set generic
        // cargo if it's NOT a protected_cargo match
        if (contains_any(d, {"protected cargo", "protect cargo"})) {
            off.cargo = true;  // protected cargo is also cargo
        } else if (contains(d, "cargo")) {
            off.cargo = true;
        }
        if (contains(d, "loot") || contains(d, "reward")) off.loot = true;
        if (contains_any(d, {"warp range", "warp speed", "warp distance"})) off.warp = true;

        // Mining subcategories — use contains_word for short words to avoid
        // false positives ("ore" in "more"/"explore", "gas" in "gasp")
        if (contains_any(d, {"crystal", "raw crystal"})) off.mining_crystal = true;
        if (contains_word(d, "gas") || contains(d, "raw gas")) off.mining_gas = true;
        if (contains_word(d, "ore") || contains(d, "raw ore")) off.mining_ore = true;
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

        // BDA text scanning — pick up mining tags for officers whose mining
        // ability is only in their below-deck ability (not in description/OA).
        // Also parse BDA mining percentages for proportional scoring.
        const auto& bda = off.bda_text;
        if (!bda.empty()) {
            // Boolean tags from BDA text (only set if not already set by description)
            if (!off.mining && contains(bda, "mining")) off.mining = true;
            if (!off.cargo && contains(bda, "cargo")) off.cargo = true;
            if (!off.mining_speed && contains_phrase(bda, {"mining speed", "mining rate", "mining efficiency"})) off.mining_speed = true;
            if (!off.mining_crystal && contains(bda, "crystal")) off.mining_crystal = true;
            if (!off.mining_gas && contains_word(bda, "gas")) off.mining_gas = true;
            if (!off.mining_ore && contains_word(bda, "ore")) off.mining_ore = true;
            if (!off.protected_cargo && contains_any(bda, {"protected cargo", "protect cargo"})) off.protected_cargo = true;
            if (!off.node_defense && contains_any(bda, {"while mining", "on a mining node", "mining defense", "mining node"})) off.node_defense = true;

            // Parse BDA mining percentages
            double bda_pct = first_pct(bda);
            if (contains_phrase(bda, {"mining speed", "mining rate", "mining efficiency"})) {
                off.bda_mining_speed_pct = bda_pct;
            }
            if (contains_phrase(bda, {"gas mining", "gas mining speed"}) || (contains_word(bda, "gas") && contains(bda, "mining"))) {
                off.bda_mining_gas_pct = bda_pct;
            }
            if (contains_phrase(bda, {"ore mining", "ore mining speed"}) || (contains_word(bda, "ore") && contains(bda, "mining"))) {
                off.bda_mining_ore_pct = bda_pct;
            }
            if (contains_phrase(bda, {"crystal mining", "crystal mining speed"}) || (contains(bda, "crystal") && contains(bda, "mining"))) {
                off.bda_mining_crystal_pct = bda_pct;
            }
            if (contains_any(bda, {"protected cargo", "protect cargo"})) {
                off.bda_protected_cargo_pct = bda_pct;
            }
            if (contains(bda, "cargo") && !contains(bda, "protected cargo")) {
                off.bda_cargo_pct = bda_pct;
            }
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

        // --- PvP 2025 META classifications ---

        // Step 1: Mitigation Delta — piercing officers
        if (contains_any(d, {"armor piercing", "armour piercing"})) {
            off.armor_piercing = true;
            off.pvp_tags.insert("piercing");
        }
        if (contains(d, "shield piercing")) {
            off.shield_piercing = true;
            off.pvp_tags.insert("piercing");
        }
        if (contains(d, "accuracy") && !contains(d, "decrease") && !contains(d, "reduce")) {
            off.accuracy_boost = true;
            off.pvp_tags.insert("piercing");
        }

        // Step 3: Proc reliability — guaranteed vs chance-based
        // "on round start" / "on combat start" / "at the start of each round" = guaranteed
        // "chance to" = chance-based
        if (!off.states_applied.empty()) {
            bool has_guarantee = contains_any(d, {"on round start", "on combat start",
                                                   "at the start of each round",
                                                   "at the start of combat",
                                                   "in round 1", "automatically"});
            off.proc_guaranteed = has_guarantee;
        }

        // Step 4: Rock-Paper-Scissors META
        // Apex Barrier (defensive — absorbs hits)
        if (contains(d, "apex barrier")) {
            off.apex_barrier = true;
            off.apex = true;
        }
        // Apex Shred (offensive — strips/pierces apex barrier)
        if (contains(d, "apex shred")) {
            off.apex_shred = true;
            off.apex = true;
        }
        // Generic apex without barrier/shred
        if (!off.apex_barrier && !off.apex_shred && contains(d, "apex")) {
            off.apex = true;
        }

        // Isolytic Cascade (offensive — bypasses standard defense)
        if (contains(d, "isolytic cascade")) {
            off.isolytic_cascade = true;
            off.isolytic_related = true;
            off.pvp_tags.insert("isolytic");
        }
        // Isolytic Defense (defensive — reduces isolytic damage)
        if (contains_any(d, {"isolytic defense", "isolytic defence"})) {
            off.isolytic_defense = true;
            off.isolytic_related = true;
            off.pvp_tags.insert("isolytic");
        }
        // Legacy: generic isolytic or apex → isolytic_related for backward compat
        if (!off.isolytic_cascade && !off.isolytic_defense &&
            contains_any(d, {"isolytic", "apex"})) {
            off.isolytic_related = true;
            off.pvp_tags.insert("isolytic");
        }

        // Cumulative stacking
        if (contains(d, "cumulative")) {
            off.cumulative_stacking = true;
        }

        // Weapon delay
        if (contains(d, "delay")) {
            off.weapon_delay = true;
            off.pvp_tags.insert("delay");
        }

        // CM scope classification — determines ability power by what it affects
        // Use cm_text presence (not is_bda()) since some officers have cm_pct >= 10000
        // from CSV but still have a valid CM text from game data enrichment
        off.cm_scope = CmScope::Unknown;
        if (!off.cm_text.empty()) {
            const auto& cm = off.cm_text;
            if (contains_any(cm, {"all officer stats", "all officers' stats",
                                   "officer stats by",
                                   "all officer attack, defense and health",
                                   "captains stats", "captain's stats",
                                   "officer stats each round"})) {
                off.cm_scope = CmScope::AllStats;
            } else if (contains_any(cm, {"effectiveness of all officer",
                                          "all officer abilities",
                                          "officer ability effectiveness",
                                          "officer abilities during combat",
                                          "increase officer abilities"})) {
                off.cm_scope = CmScope::AbilityAmp;
            } else if (contains_any(cm, {"weapon damage", "weapons damage",
                                          "damage dealt", "all damage",
                                          "increase damage", "increases damage",
                                          "isolytic cascade"})) {
                off.cm_scope = CmScope::WeaponDamage;
            } else if (contains_any(cm, {"critical hit damage", "critical damage",
                                          "crit damage"})) {
                off.cm_scope = CmScope::CritDamage;
            } else if (contains_any(cm, {"attack by", "officer attack",
                                          "defense by", "officer defense",
                                          "officer defence",
                                          "officers defence", "officers defense",
                                          "defence of bridge", "defense of bridge",
                                          "health by", "officer health",
                                          "officers health", "officers base health",
                                          "health of all",
                                          "health of bridge",
                                          "hull health",
                                          "accuracy", "penetration"})) {
                off.cm_scope = CmScope::SingleStat;
            } else if (contains_any(cm, {"shield", "shp",
                                          "apex barrier"})) {
                off.cm_scope = CmScope::ShieldHp;
            } else if (contains_any(cm, {"mitigation", "armor", "armour",
                                          "dodge", "deflection"})) {
                off.cm_scope = CmScope::Mitigation;
            } else if (contains_any(cm, {"mining", "cargo", "protected cargo"})) {
                off.cm_scope = CmScope::MiningEffect;
            } else if (contains_any(cm, {"shots", "number of shots"})) {
                off.cm_scope = CmScope::WeaponDamage;  // Shots = more weapon hits
            } else if (contains_any(cm, {"when", "below", "chance", "if ",
                                          "on round start", "on combat start",
                                          "maneuver effectiveness",
                                          "chain of command",
                                          "resurrect"})) {
                off.cm_scope = CmScope::Conditional;
            } else if (contains_any(cm, {"cost efficiency", "jump and summoning",
                                          "disco cost", "disco spend",
                                          "summoning cost", "transwarp cost",
                                          "refining"})) {
                off.cm_scope = CmScope::NonCombat;
            } else if (contains_any(cm, {"warp", "repair", "loot", "reward",
                                          "speed", "cost", "ship xp",
                                          "resources you get"})) {
                off.cm_scope = CmScope::Utility;
            }
        }

        // -------------------------------------------------------------------
        // Populate numeric ability data from bootstrap JSON (officer_skills.json).
        // API-sourced values (passed via RosterOfficer) take priority when present;
        // the bootstrap JSON fills gaps for CSV-only rosters.
        // -------------------------------------------------------------------
        const json_t* skills = find_officer_skills(off.name);
        if (skills) {
            const auto& sj = *skills;

            // OA values — use API data if present, else bootstrap
            if (off.oa_values.empty() && sj.contains("oa_values") && sj["oa_values"].is_array()) {
                for (const auto& v : sj["oa_values"]) {
                    off.oa_values.push_back(v.get<double>());
                }
            }
            // OA value at player's rank (rank is 1-based, vector is 0-based)
            if (!off.oa_values.empty()) {
                int idx = std::max(0, std::min(off.rank - 1, static_cast<int>(off.oa_values.size()) - 1));
                off.oa_value = off.oa_values[idx];
            }

            // CM value — use API-sourced value if already set, else bootstrap
            if (off.cm_value == 0.0 && sj.contains("cm_value") && sj["cm_value"].is_number()) {
                off.cm_value = sj["cm_value"].get<double>();
            }

            // BDA values — use API data if present, else bootstrap
            if (off.bda_values.empty() && sj.contains("bda_values") && sj["bda_values"].is_array()) {
                for (const auto& v : sj["bda_values"]) {
                    off.bda_values.push_back(v.get<double>());
                }
            }
            if (!off.bda_values.empty()) {
                int idx = std::max(0, std::min(off.rank - 1, static_cast<int>(off.bda_values.size()) - 1));
                off.bda_value = off.bda_values[idx];
            }

            // BDA description from bootstrap
            if (sj.contains("bda_ability") && sj["bda_ability"].is_string()) {
                off.bda_description = sj["bda_ability"].get<std::string>();
            }

            // Synergy values
            if (sj.contains("synergy_full") && sj["synergy_full"].is_number()) {
                off.synergy_full = sj["synergy_full"].get<double>();
            }
            if (sj.contains("synergy_half") && sj["synergy_half"].is_number()) {
                off.synergy_half = sj["synergy_half"].get<double>();
            }

            // Officer type string
            if (sj.contains("officer_type") && sj["officer_type"].is_string()) {
                off.officer_type_str = sj["officer_type"].get<std::string>();
            }

            // CM description from bootstrap
            if (sj.contains("captain_maneuver") && sj["captain_maneuver"].is_string()) {
                off.cm_description = sj["captain_maneuver"].get<std::string>();
            }

            // State data from bootstrap — enrich the tag-based classification
            // with structured data when the text-matching missed something
            if (sj.contains("states") && sj["states"].is_object()) {
                const auto& st = sj["states"];
                auto check_state = [&](const char* cause_key, const char* use_key,
                                       const char* state_name) {
                    if (st.contains(cause_key) && st[cause_key].get<bool>()) {
                        off.states_applied.insert(state_name);
                        off.pvp_tags.insert(state_name);
                    }
                    if (st.contains(use_key) && st[use_key].get<bool>()) {
                        off.states_benefit.insert(state_name);
                        off.pvp_tags.insert(state_name);
                    }
                };
                check_state("cause_burning", "use_burning", "burning");
                check_state("cause_breach", "use_breach", "breach");
                check_state("cause_morale", "use_morale", "morale");
                check_state("cause_assimilate", "use_assimilate", "assimilate");
            }

            // Stat boost enrichment from bootstrap
            if (sj.contains("stat_boosts") && sj["stat_boosts"].is_object()) {
                const auto& sb = sj["stat_boosts"];
                bool has_any = false;
                for (auto& [key, val] : sb.items()) {
                    if (val.get<bool>()) { has_any = true; break; }
                }
                if (has_any) off.stat_booster = true;
            }
        }
    }

    // Compute roster stat max for normalization (used in scoring)
    roster_max_attack_ = 0.0;
    roster_max_defense_ = 0.0;
    roster_max_health_ = 0.0;
    for (const auto& off : officers_) {
        if (off.attack > roster_max_attack_) roster_max_attack_ = off.attack;
        if (off.defense > roster_max_defense_) roster_max_defense_ = off.defense;
        if (off.health > roster_max_health_) roster_max_health_ = off.health;
    }
    // Avoid division by zero
    if (roster_max_attack_ <= 0.0) roster_max_attack_ = 1.0;
    if (roster_max_defense_ <= 0.0) roster_max_defense_ = 1.0;
    if (roster_max_health_ <= 0.0) roster_max_health_ = 1.0;
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
// VALUE-BASED SCORING HELPERS
// ---------------------------------------------------------------------------
// These replace magic-number bonuses with scores derived from actual ability
// magnitudes.  The general pattern:
//
//   score = ability_value * scale_factor [* proc_chance] [* relevance]
//
// Scale factors are tuned so that a "typical strong" officer (e.g., rank 5
// rare with 0.80 OA value) lands in the same ballpark as the old magic
// numbers, but now rank/level progression matters.
//
// When oa_value is 0 (no bootstrap data or API data available), fall back to
// the tag-only minimum so officers without numeric data still get reasonable
// placement.
// ---------------------------------------------------------------------------

// Compute an ability-based score with a fallback minimum for officers
// where numeric data isn't available (CSV-only rosters without API sync).
static inline double value_or_fallback(double oa_val, double scale, double fallback) {
    if (oa_val > 0.0) return oa_val * scale;
    return fallback;
}

// For proc-based abilities: value * chance * scale, with fallback
// OA chance of 0.0 means "not parsed" → treat as reasonable default (0.5)
// CM value-based score: uses the actual CM magnitude * scope weight.
// If cm_value (from API/bootstrap) is available, use it; else fall back to
// cm_pct/100 which is the legacy CSV percentage.
static inline double cm_quality_score(const ClassifiedOfficer& off) {
    if (off.is_bda()) return 0.0;
    double cm_val = off.cm_value;
    if (cm_val <= 0.0 && off.cm_pct > 0.0) {
        cm_val = off.cm_pct / 100.0;  // Legacy: convert percentage to decimal
    }
    if (cm_val <= 0.0) return 0.0;
    // Cap at 5.0 (500%) to prevent extreme outliers
    cm_val = std::min(cm_val, 5.0);
    return cm_val * cm_scope_weight(off.cm_scope);
}

// OA quality score for individual ranking.
// Uses oa_value (actual ability magnitude) when available, else oa_pct.
static inline double oa_quality_score(const ClassifiedOfficer& off, double scale) {
    if (off.is_bda()) return 0.0;
    if (off.oa_value > 0.0) {
        // oa_value is the decimal value (e.g., 0.80 = 80%).
        // Scale so that a typical strong OA (0.80) gives ~4000 at scale=5000.
        return std::min(off.oa_value, 5.0) * scale;
    }
    // Fallback to legacy oa_pct
    if (off.oa_pct > 0.0) {
        return std::min(off.oa_pct / 100.0, 5.0) * scale;
    }
    return 0.0;
}

// ---------------------------------------------------------------------------

double CrewOptimizer::score_pvp_individual(const ClassifiedOfficer& off) const {
    // ---------------------------------------------------------------------------
    // 2025 PvP META Individual Scoring — Value-Based Framework
    // ---------------------------------------------------------------------------
    // Same 4-step priority hierarchy, but scores now reflect actual ability
    // magnitudes instead of flat magic numbers.
    //
    // Scale factors are calibrated so that:
    //   - A strong rank-5 officer ≈ old magic number value
    //   - Rank progression creates meaningful differentiation
    //   - Officers without numeric data fall back to reasonable minimums
    // ---------------------------------------------------------------------------

    // 1. Normalized stat tiebreaker (max ~5000)
    double norm_atk = off.attack / roster_max_attack_;
    double norm_def = off.defense / roster_max_defense_;
    double norm_hp  = off.health / roster_max_health_;
    double stat_score = (norm_atk * 0.5 + norm_def * 0.3 + norm_hp * 0.2) * 5000.0;

    double ability_score = 0.0;

    // -----------------------------------------------------------------------
    // STEP 1: Mitigation Delta — piercing bypasses the 71.2% mitigation cap
    // -----------------------------------------------------------------------
    // Scale: 15000 per 1.0 (100%) of piercing value.
    // E.g., Charvanek rank 5 (oa_value=0.16 for 16%) → 0.16 * 15000 = 2400.
    //       But her values can be much higher at full effect (16.0 = 1600%).
    // Fallback: 8000 (old ~12000 for "any piercing" was too generous).
    if (off.armor_piercing)  ability_score += value_or_fallback(off.oa_value, 15000.0, 8000);
    if (off.shield_piercing) ability_score += value_or_fallback(off.oa_value, 15000.0, 8000);
    if (off.accuracy_boost)  ability_score += value_or_fallback(off.oa_value, 12000.0, 6000);
    // Multi-piercing synergy — still flat because it's a combinatorial bonus
    {
        int pierce_count = (off.armor_piercing ? 1 : 0) +
                           (off.shield_piercing ? 1 : 0) +
                           (off.accuracy_boost ? 1 : 0);
        if (pierce_count >= 2) ability_score += 6000;
    }

    // -----------------------------------------------------------------------
    // STEP 3: Proc Factor — guaranteed procs are META-defining
    // -----------------------------------------------------------------------
    // State application value: scales with OA magnitude and proc chance.
    // Scale: 10000 per 1.0 of effect value.
    // Guaranteed procs get a 1.8x multiplier (was flat +8000 extra).
    if (!off.states_applied.empty()) {
        double state_val = value_or_fallback(off.oa_value, 10000.0, 6000);
        if (off.oa_chance > 0.0 && off.oa_chance < 1.0) {
            // Chance-based: scale by proc probability
            state_val *= off.oa_chance;
        }
        if (off.proc_guaranteed) {
            state_val *= 1.8;  // Guaranteed self-proc near-doubles value
        }
        ability_score += state_val;
    }
    if (!off.states_benefit.empty()) {
        ability_score += value_or_fallback(off.oa_value, 5000.0, 3000);
    }

    // Cumulative stacking — grows each round, scales with base value
    if (off.cumulative_stacking) {
        ability_score += value_or_fallback(off.oa_value, 8000.0, 5000);
    }

    // -----------------------------------------------------------------------
    // STEP 4: Rock-Paper-Scissors META archetypes
    // -----------------------------------------------------------------------
    // Apex/isolytic values can be quite large (e.g., barrier points in thousands).
    // Use a log-scaled approach for absolute values, linear for percentages.
    if (off.apex_shred) {
        ability_score += value_or_fallback(off.oa_value, 18000.0, 10000);
    }
    if (off.apex_barrier) {
        ability_score += value_or_fallback(off.oa_value, 15000.0, 8000);
    }
    if (off.isolytic_cascade) {
        ability_score += value_or_fallback(off.oa_value, 12000.0, 7000);
    }
    if (off.isolytic_defense) {
        ability_score += value_or_fallback(off.oa_value, 5000.0, 3000);
    }

    // Generalist archetype: shots + raw damage
    if (off.shots_related) {
        ability_score += value_or_fallback(off.oa_value, 10000.0, 6000);
    }

    // -----------------------------------------------------------------------
    // Combat mechanics (secondary tier)
    // -----------------------------------------------------------------------
    if (off.crit_related) {
        ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
        if (weakness_.crit_damage_gap > 5) {
            ability_score += 3000;  // Weakness counter stays flat
        }
    }
    if (off.weapon_delay) ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
    if (off.mitigation_related) ability_score += value_or_fallback(off.oa_value, 5000.0, 3000);
    if (off.shield_related) ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);

    // -----------------------------------------------------------------------
    // CM quality — value-based captain candidacy score
    // -----------------------------------------------------------------------
    ability_score += cm_quality_score(off);

    // Ability type bonuses — scale with synergy_full (how much they amplify)
    if (off.ability_amplifier) {
        double amp_val = (off.synergy_full > 0.0) ? off.synergy_full * 50000.0 : 10000.0;
        ability_score += amp_val;
    }
    if (off.stat_booster) ability_score += 4000;

    // PvP relevance tags
    if (off.is_pvp_specific) ability_score += 6000;
    if (off.is_ship_specific) ability_score += 4000;
    if (off.is_dual_use) ability_score += 2000;

    // OA quality — value-based
    ability_score += oa_quality_score(off, 5000.0);

    // Player uses (popular = generally effective)
    if (off.player_uses) ability_score += 2000;

    // -----------------------------------------------------------------------
    // Penalties
    // -----------------------------------------------------------------------
    if (off.is_pve_specific && !off.is_dual_use) ability_score -= 8000;
    if (off.mining || off.cargo) ability_score -= 12000;

    return std::max(stat_score + ability_score, 0.0);
}

double CrewOptimizer::score_hybrid_individual(const ClassifiedOfficer& off) const {
    // Hybrid: values officers that work in BOTH PvP and PvE.
    // Uses value-based scoring at reduced PvP weights + PvE survivability.
    double norm_atk = off.attack / roster_max_attack_;
    double norm_def = off.defense / roster_max_defense_;
    double norm_hp  = off.health / roster_max_health_;
    double stat_score = (norm_atk * 0.4 + norm_def * 0.35 + norm_hp * 0.25) * 5000.0;

    double ability_score = 0.0;

    // CM quality — value-based
    ability_score += cm_quality_score(off);

    // Dual-use is king in hybrid — officers that work in both modes
    if (off.is_dual_use) ability_score += 10000;
    if (off.ability_amplifier) {
        double amp_val = (off.synergy_full > 0.0) ? off.synergy_full * 50000.0 : 10000.0;
        ability_score += amp_val;
    }
    if (off.stat_booster) ability_score += 5000;

    // PvP META at reduced weight (50-70% of PvP values), value-based
    if (off.armor_piercing)  ability_score += value_or_fallback(off.oa_value, 9000.0, 5000);
    if (off.shield_piercing) ability_score += value_or_fallback(off.oa_value, 9000.0, 5000);
    if (off.accuracy_boost)  ability_score += value_or_fallback(off.oa_value, 7500.0, 4000);

    // Apex/isolytic at reduced weight
    if (off.apex_shred)       ability_score += value_or_fallback(off.oa_value, 10000.0, 6000);
    if (off.apex_barrier)     ability_score += value_or_fallback(off.oa_value, 9000.0, 5000);
    if (off.isolytic_cascade) ability_score += value_or_fallback(off.oa_value, 7500.0, 4000);
    if (off.isolytic_defense) ability_score += value_or_fallback(off.oa_value, 3000.0, 1500);

    // State application with proc reliability
    if (!off.states_applied.empty()) {
        double state_val = value_or_fallback(off.oa_value, 7500.0, 4500);
        if (off.oa_chance > 0.0 && off.oa_chance < 1.0) {
            state_val *= off.oa_chance;
        }
        if (off.proc_guaranteed) {
            state_val *= 1.6;
        }
        ability_score += state_val;
    }
    if (!off.states_benefit.empty()) {
        ability_score += value_or_fallback(off.oa_value, 3500.0, 2000);
    }
    if (off.cumulative_stacking) {
        ability_score += value_or_fallback(off.oa_value, 5000.0, 3000);
    }

    // Standard combat mechanics
    if (off.is_ship_specific) ability_score += 4000;
    if (off.shots_related) ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
    if (off.crit_related) {
        ability_score += value_or_fallback(off.oa_value, 5000.0, 3000);
        if (weakness_.crit_damage_gap > 5) ability_score += 2000;
    }
    if (off.shield_related) ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);
    if (off.mitigation_related) ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);
    if (off.weapon_delay) ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);
    if (off.player_uses) ability_score += 2000;

    // OA quality — value-based
    ability_score += oa_quality_score(off, 4000.0);

    // Penalties for single-mode officers
    if (off.is_pve_specific && !off.is_dual_use) ability_score -= 6000;
    if (off.is_pvp_specific && !off.is_dual_use) ability_score -= 4000;
    if (off.mining || off.cargo) ability_score -= 8000;

    return std::max(stat_score + ability_score, 0.0);
}

double CrewOptimizer::score_scenario_individual(const ClassifiedOfficer& off,
                                                 Scenario scenario) const {
    if (scenario == Scenario::PvP) return score_pvp_individual(off);
    if (scenario == Scenario::Hybrid) return score_hybrid_individual(off);

    // Normalized stat tiebreaker
    double norm_atk = off.attack / roster_max_attack_;
    double norm_def = off.defense / roster_max_defense_;
    double norm_hp  = off.health / roster_max_health_;
    double stat_score = (norm_atk * 0.5 + norm_def * 0.3 + norm_hp * 0.2) * 5000.0;

    double ability_score = 0.0;

    switch (scenario) {
    case Scenario::BaseCracker:
        if (off.base_attack) ability_score += value_or_fallback(off.oa_value, 18000.0, 12000);
        if (off.base_defend) ability_score -= 10000;
        if (off.crit_related) ability_score += value_or_fallback(off.oa_value, 7500.0, 5000);
        if (off.shots_related) ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
        if (off.weapon_delay) ability_score += value_or_fallback(off.oa_value, 7500.0, 5000);
        if (off.apex_shred) ability_score += value_or_fallback(off.oa_value, 5000.0, 3000);
        if (off.shield_piercing || off.armor_piercing)
            ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);
        if (off.is_pvp_specific) ability_score += 3000;
        if (off.ability_amplifier) {
            ability_score += (off.synergy_full > 0.0) ? off.synergy_full * 25000.0 : 5000.0;
        }
        if (off.stat_booster) ability_score += 3000;
        if (off.pve_hostile && !off.base_attack && !off.is_pvp_specific) ability_score -= 6000;
        if (off.mining || off.cargo) ability_score -= 12000;
        break;

    case Scenario::PvEHostile:
        if (off.pve_hostile) ability_score += value_or_fallback(off.oa_value, 15000.0, 10000);
        if (off.mitigation_related) ability_score += value_or_fallback(off.oa_value, 8500.0, 5000);
        if (off.shield_related) ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
        if (off.crit_related) ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
        if (off.repair) ability_score += value_or_fallback(off.oa_value, 5000.0, 3000);
        if (off.stat_booster) ability_score += 5000;
        if (off.ability_amplifier) {
            ability_score += (off.synergy_full > 0.0) ? off.synergy_full * 40000.0 : 8000.0;
        }
        if (off.loot) ability_score += 2000;
        if (off.is_pvp_specific && !off.pve_hostile) ability_score -= 8000;
        if (off.mining) ability_score -= 10000;
        break;

    case Scenario::MissionBoss:
        if (off.mission_boss) ability_score += value_or_fallback(off.oa_value, 15000.0, 10000);
        if (off.pve_hostile) ability_score += value_or_fallback(off.oa_value, 10000.0, 6000);
        if (off.crit_related) ability_score += value_or_fallback(off.oa_value, 8500.0, 5000);
        if (off.isolytic_cascade) ability_score += value_or_fallback(off.oa_value, 7500.0, 5000);
        if (off.isolytic_defense) ability_score += value_or_fallback(off.oa_value, 3000.0, 1500);
        if (off.shots_related) ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
        if (off.mitigation_related) ability_score += value_or_fallback(off.oa_value, 6000.0, 4000);
        if (off.shield_related) ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);
        if (off.ability_amplifier) {
            ability_score += (off.synergy_full > 0.0) ? off.synergy_full * 30000.0 : 6000.0;
        }
        if (off.stat_booster) ability_score += 4000;
        // Extra weight on high attack in boss fights
        ability_score += norm_atk * 3000.0;
        if (off.armada && !off.pve_hostile) ability_score -= 5000;
        if (off.mining || off.cargo) ability_score -= 12000;
        break;

    case Scenario::Loot:
        if (off.mining) ability_score += 12000;
        if (off.cargo) ability_score += 10000;
        if (off.loot) ability_score += 10000;
        if (off.warp) ability_score += 5000;
        if (off.shield_related) ability_score += 2000;
        if (off.mitigation_related) ability_score += 2000;
        if (off.is_pvp_specific && !off.mining && !off.cargo) ability_score -= 8000;
        if (!off.mining && !off.cargo && !off.loot && !off.warp) ability_score -= 6000;
        break;

    case Scenario::Armada:
        // Armada priority: MAX LOOT/DIRECTIVES ROI, not raw combat power.
        if (off.loot) ability_score += 18000;
        if (off.armada) ability_score += value_or_fallback(off.oa_value, 15000.0, 10000);
        if (off.armada_solo) ability_score += 6000;
        if (off.non_armada_only) ability_score -= 15000;
        // Combat is secondary — enough to survive but not the focus
        if (off.pve_hostile && !off.non_armada_only) ability_score += 4000;
        if (off.mitigation_related) ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);
        if (off.shield_related) ability_score += 2000;
        if (off.crit_related) ability_score += value_or_fallback(off.oa_value, 4000.0, 2000);
        if (off.stat_booster) ability_score += 3000;
        if (off.ability_amplifier) {
            ability_score += (off.synergy_full > 0.0) ? off.synergy_full * 20000.0 : 4000.0;
        }
        if (off.is_pvp_specific && !off.armada) ability_score -= 6000;
        if (off.mining || off.cargo) ability_score -= 10000;
        break;

    // Mining scenarios — mostly left as-is (working fine), only light improvements
    case Scenario::MiningSpeed:
        if (off.mining_speed) ability_score += 18000;
        else if (off.mining) ability_score += 8000;
        if (off.mining_gas || off.mining_ore || off.mining_crystal) ability_score += 4000;
        if (off.cargo && !off.protected_cargo) ability_score += 3000;
        if (off.protected_cargo) ability_score += 2000;
        if (off.warp) ability_score += 2000;
        if (off.node_defense) ability_score += 2000;
        if (off.is_pvp_specific && !off.mining && !off.cargo) ability_score -= 10000;
        if (!off.mining && !off.mining_speed && !off.cargo) ability_score -= 8000;
        break;

    case Scenario::MiningProtected:
        if (off.protected_cargo) ability_score += 16000;
        else if (off.cargo) ability_score += 8000;
        if (off.mining) ability_score += 4000;
        if (off.node_defense) ability_score += 5000;
        if (off.shield_related) ability_score += 3000;
        if (off.mitigation_related) ability_score += 3000;
        if (!off.protected_cargo && !off.cargo && !off.mining && !off.node_defense) ability_score -= 8000;
        break;

    case Scenario::MiningCrystal:
        if (off.mining_crystal) ability_score += 16000;
        else if (off.mining) ability_score += 6000;
        if (off.mining_speed) ability_score += 4000;
        if (off.cargo && !off.protected_cargo) ability_score += 3000;
        if (off.protected_cargo) ability_score += 3000;
        if (off.node_defense) ability_score += 2000;
        if (!off.mining && !off.mining_crystal && !off.cargo) ability_score -= 8000;
        break;

    case Scenario::MiningGas:
        if (off.mining_gas) ability_score += 18000;
        else if (off.mining) ability_score += 6000;
        if (off.mining_speed) ability_score += 5000;
        if (off.cargo && !off.protected_cargo) ability_score += 3000;
        if (off.protected_cargo) ability_score += 4000;
        if (off.node_defense) ability_score += 2000;
        if (off.is_pvp_specific && !off.mining && !off.cargo) ability_score -= 10000;
        if (!off.mining && !off.mining_gas && !off.cargo) ability_score -= 10000;
        break;

    case Scenario::MiningOre:
        if (off.mining_ore) ability_score += 16000;
        else if (off.mining) ability_score += 6000;
        if (off.mining_speed) ability_score += 4000;
        if (off.cargo && !off.protected_cargo) ability_score += 3000;
        if (off.protected_cargo) ability_score += 3000;
        if (off.node_defense) ability_score += 2000;
        if (!off.mining && !off.mining_ore && !off.cargo) ability_score -= 8000;
        break;

    case Scenario::MiningGeneral:
        if (off.mining) ability_score += 10000;
        if (off.mining_speed) ability_score += 5000;
        if (off.cargo && !off.protected_cargo) ability_score += 6000;
        if (off.protected_cargo) ability_score += 5000;
        if (off.node_defense) ability_score += 4000;
        if (off.warp) ability_score += 2000;
        {
            int mining_tags = 0;
            if (off.mining_crystal) ++mining_tags;
            if (off.mining_gas) ++mining_tags;
            if (off.mining_ore) ++mining_tags;
            if (off.mining_speed) ++mining_tags;
            if (off.protected_cargo) ++mining_tags;
            if (mining_tags >= 2) ability_score += mining_tags * 2000;
        }
        if (!off.mining && !off.cargo && !off.node_defense) ability_score -= 8000;
        break;

    default:
        break;
    }

    // Universal bonuses for non-pvp/non-hybrid scenarios
    if (off.is_ship_specific) ability_score += 3000;
    if (off.player_uses) ability_score += 1500;

    return std::max(stat_score + ability_score, 0.0);
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
        // Cap beneficiary multiplier: 1.0 + min(beneficiaries, 2) * 0.25
        // This prevents 3-beneficiary crews from dominating everything
        chain_bonus *= 1.0 + std::min(beneficiaries, 2) * 0.25;
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
// Officers with higher OA values have stronger abilities. Dedicated BDAs are
// excluded since they are designed as below-deck officers.
// Value-based: uses oa_value (actual ability magnitude) when available, else oa_pct.
// OA bonus is scenario-weighted — a mining OA shouldn't help PvP.
void CrewOptimizer::apply_oa_bonus(double& total, CrewBreakdown& bd,
                                    const ClassifiedOfficer* crew[3],
                                    Scenario scenario) const {
    double oa_bonus = 0.0;
    for (int i = 0; i < 3; ++i) {
        if (crew[i]->is_bda()) continue;  // BDA officers don't have meaningful OA%

        // Prefer oa_value (actual decimal magnitude) over oa_pct (legacy percentage)
        double oa_val = crew[i]->oa_value;
        if (oa_val <= 0.0 && crew[i]->oa_pct > 0.0) {
            oa_val = crew[i]->oa_pct / 100.0;
        }
        if (oa_val <= 0.0) continue;

        // Check scenario relevance: penalize OA that doesn't match the scenario
        double relevance = 1.0;
        bool is_combat = (scenario == Scenario::PvP || scenario == Scenario::Hybrid ||
                          scenario == Scenario::BaseCracker || scenario == Scenario::Armada ||
                          scenario == Scenario::PvEHostile || scenario == Scenario::MissionBoss);
        bool is_mining = (scenario == Scenario::MiningSpeed || scenario == Scenario::MiningGas ||
                          scenario == Scenario::MiningOre || scenario == Scenario::MiningCrystal ||
                          scenario == Scenario::MiningProtected || scenario == Scenario::MiningGeneral ||
                          scenario == Scenario::Loot);

        if (is_combat && (crew[i]->mining || crew[i]->cargo) &&
            !crew[i]->is_pvp_specific && !crew[i]->is_dual_use) {
            relevance = 0.1;  // Mining OA in combat = nearly worthless
        }
        if (is_mining && crew[i]->is_pvp_specific &&
            !crew[i]->mining && !crew[i]->cargo) {
            relevance = 0.1;  // PvP OA in mining = nearly worthless
        }

        // Cap at 15.0 (1500%) — beyond that it's usually a special mechanic
        double capped = std::min(oa_val, 15.0);
        // Scale: a typical strong OA (0.80) gives 0.80 * 2000 = 1600 points
        oa_bonus += capped * 2000.0 * relevance;
    }
    if (oa_bonus > 0) {
        total += oa_bonus;
        bd.oa_bonus = oa_bonus;
        bd.synergy_notes.push_back(
            "OA% bonus: +" + std::to_string(static_cast<int>(oa_bonus)) + " from crew ability magnitudes");
    }
}

// ---------------------------------------------------------------------------
// Bridge synergy: game synergy group + class-based CM multiplier
// ---------------------------------------------------------------------------
// STFC synergy rules (from the "Officer Tool"):
//
// Synergy acts as a multiplier on the Captain's Maneuver (CM).  Each side
// officer (b1, b2) is evaluated independently:
//
//   2 bars (20%): side officer is in the captain's group AND all 3 officers
//                 have unique classes (Command / Science / Engineering).
//   1 bar  (10%): side officer is in the captain's group BUT shares a class
//                 with another bridge member.
//   0 bars ( 0%): side officer is NOT in the captain's group.
//
// Max synergy: 20% + 20% = 40%.
// Formula: Effective CM = Base CM * (1 + total_synergy_pct)
//
// The scoring impact is computed as the additional CM value multiplied by a
// weight factor that converts the CM percentage boost into optimizer points.

void CrewOptimizer::apply_bridge_synergy(double& total, CrewBreakdown& bd,
                                          const ClassifiedOfficer* crew[3]) const {
    const auto& captain = *crew[0];
    const auto& b1 = *crew[1];
    const auto& b2 = *crew[2];

    // Captain must have a group for synergy to apply
    if (captain.group.empty()) return;

    // Check if all 3 classes are unique (the "Rule of Three")
    bool all_unique_classes = (captain.officer_class != 0 &&
                               b1.officer_class != 0 &&
                               b2.officer_class != 0 &&
                               captain.officer_class != b1.officer_class &&
                               captain.officer_class != b2.officer_class &&
                               b1.officer_class != b2.officer_class);

    // Evaluate each side officer independently
    auto calc_bars = [&](const ClassifiedOfficer& side) -> int {
        if (side.group.empty() || side.group != captain.group) return 0;
        // Same group as captain
        if (all_unique_classes) return 2;  // Full synergy
        return 1;  // Half synergy (class collision)
    };

    int bars_left = calc_bars(b1);
    int bars_right = calc_bars(b2);

    // Use per-officer synergy_full/synergy_half values when available;
    // fall back to hardcoded 20%/10% if bootstrap data is missing.
    auto synergy_for = [&](const ClassifiedOfficer& side, int bars) -> double {
        if (bars == 2) return (side.synergy_full > 0.0) ? side.synergy_full * 100.0 : 20.0;
        if (bars == 1) return (side.synergy_half > 0.0) ? side.synergy_half * 100.0 : 10.0;
        return 0.0;
    };

    double synergy_pct = synergy_for(b1, bars_left) + synergy_for(b2, bars_right);

    if (synergy_pct <= 0.0) return;

    bd.bridge_synergy_pct = synergy_pct;
    bd.bridge_synergy_bars_left = bars_left;
    bd.bridge_synergy_bars_right = bars_right;

    // The synergy multiplies the captain's CM.  In scoring terms, the CM
    // bonus was already applied as value-based: cm_val * cm_scope_weight() * 100.
    // The synergy adds (synergy_pct / 100) * that same CM contribution.
    double cm_val = captain.cm_value;
    if (cm_val <= 0.0 && captain.cm_pct > 0.0) cm_val = captain.cm_pct / 100.0;
    cm_val = std::min(cm_val, 5.0);
    double cm_base = cm_val * cm_scope_weight(captain.cm_scope) * 100.0;
    double synergy_bonus = cm_base * (synergy_pct / 100.0);

    // Additional flat incentive for achieving synergy — this encourages the
    // optimizer to prefer same-group crews even when the CM scope weight is low.
    // 40% synergy (max) = +40K flat bonus, 20% = +20K, 10% = +10K
    double flat_synergy_incentive = synergy_pct * 1000.0;
    synergy_bonus += flat_synergy_incentive;

    // For BDA captains (who got penalized, no CM bonus applied), synergy
    // still matters in the game but is worth less in our scoring model
    if (captain.is_bda()) {
        // Use a flat approximation since no CM bonus was applied
        synergy_bonus = synergy_pct * 1200.0;  // ~48000 at 40%
    }

    total += synergy_bonus;
    bd.bridge_synergy_bonus = synergy_bonus;

    // Build descriptive note
    std::string bar_str_l = (bars_left == 2) ? "##" : (bars_left == 1 ? "#" : "-");
    std::string bar_str_r = (bars_right == 2) ? "##" : (bars_right == 1 ? "#" : "-");
    std::string note = "Bridge synergy [" + bar_str_l + "|" + bar_str_r + "] +" +
                       std::to_string(static_cast<int>(synergy_pct)) + "% CM";
    if (!captain.group.empty()) {
        note += " (" + captain.group + ")";
    }
    if (all_unique_classes) {
        note += " [Cmd/Sci/Eng]";
    }
    bd.synergy_notes.push_back(note);
}

// ---------------------------------------------------------------------------
// Crew scoring: PvP — 2025 META 4-Step Framework
// ---------------------------------------------------------------------------
// Step 1: Mitigation Delta — Does the crew bypass the 71.2% mitigation cap?
// Step 2: Synergy-Adjusted CM — Is the CM maximized with bridge synergy?
// Step 3: Proc Factor — Does the crew self-proc states reliably?
// Step 4: RPS META — Does it form a coherent archetype (generalist/apex/strike)?
// Winner Checklist: (a) mitigation cap? (b) synergy maxed? (c) self-proc? (d) isolytic?
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

    // Individual scores (includes piercing, apex, isolytic, proc bonuses)
    for (int i = 0; i < 3; ++i) {
        double s = score_pvp_individual(*crew[i]);
        bd.individual_scores[crew[i]->name] = s;
        total += s;
    }

    std::string ship_label = ship_type_str(ship_type_);
    ship_label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ship_label[0])));

    // ===================================================================
    // STEP 2: Synergy-Adjusted CM — PRIMARY scoring factor (value-based)
    // ===================================================================
    if (captain.is_bda()) {
        double penalty = total * 0.25;
        total -= penalty;
        bd.penalty_total += penalty;
        bd.penalties.push_back("'" + captain.name + "' has a BDA, not a Captain Maneuver -- wasted captain slot");
    } else if (!cm_works_on_ship(captain)) {
        double penalty = total * 0.2;
        total -= penalty;
        bd.penalty_total += penalty;
        bd.penalties.push_back("'" + captain.name + "' CM locked to non-" + ship_label + " ship type");
    } else {
        // Compute synergy percentage inline
        double synergy_pct = 0.0;
        if (!captain.group.empty()) {
            bool all_unique_classes = (captain.officer_class != 0 &&
                                       b1.officer_class != 0 &&
                                       b2.officer_class != 0 &&
                                       captain.officer_class != b1.officer_class &&
                                       captain.officer_class != b2.officer_class &&
                                       b1.officer_class != b2.officer_class);
            auto calc_bars = [&](const ClassifiedOfficer& side) -> int {
                if (side.group.empty() || side.group != captain.group) return 0;
                if (all_unique_classes) return 2;
                return 1;
            };
            int bars_left = calc_bars(b1);
            int bars_right = calc_bars(b2);
            synergy_pct = bars_left * 10.0 + bars_right * 10.0;

            if (synergy_pct > 0) {
                bd.bridge_synergy_pct = synergy_pct;
                bd.bridge_synergy_bars_left = bars_left;
                bd.bridge_synergy_bars_right = bars_right;
                std::string bar_str_l = (bars_left == 2) ? "##" : (bars_left == 1 ? "#" : "-");
                std::string bar_str_r = (bars_right == 2) ? "##" : (bars_right == 1 ? "#" : "-");
                std::string note = "Bridge synergy [" + bar_str_l + "|" + bar_str_r + "] +" +
                                   std::to_string(static_cast<int>(synergy_pct)) + "% CM";
                if (!captain.group.empty()) note += " (" + captain.group + ")";
                if (all_unique_classes) note += " [Cmd/Sci/Eng]";
                bd.synergy_notes.push_back(note);
            }
        }

        // Value-based CM: use cm_value (actual decimal) * scope_weight * synergy
        double cm_val = captain.cm_value;
        if (cm_val <= 0.0 && captain.cm_pct > 0.0) cm_val = captain.cm_pct / 100.0;
        cm_val = std::min(cm_val, 5.0);  // Cap at 500%

        double cm_base = cm_val * cm_scope_weight(captain.cm_scope) * 100.0;  // Scale to crew range
        double synergy_mult = 1.0 + synergy_pct / 100.0;
        double cm_bonus = cm_base * synergy_mult;

        // Flat synergy incentive: encourages high synergy even for low-scope CMs
        cm_bonus += synergy_pct * 2500.0;

        total += cm_bonus;
        bd.bridge_synergy_bonus = cm_bonus - cm_base;
    }

    // ===================================================================
    // STEP 1: Crew-level Mitigation Delta — value-based piercing coverage
    // ===================================================================
    // Sum actual piercing values from all officers for value-based scoring.
    // Officers with higher piercing values create a stronger mitigation delta.
    {
        int pierce_officers = 0;
        double pierce_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->armor_piercing || crew[i]->shield_piercing || crew[i]->accuracy_boost) {
                ++pierce_officers;
                pierce_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (pierce_officers >= 2) {
            double pierce_bonus = pierce_value_sum * 40000.0;
            if (pierce_bonus < 40000) pierce_bonus = 40000;  // Minimum floor
            total += pierce_bonus;
            bd.synergy_notes.push_back(
                "Piercing coverage (" + std::to_string(pierce_officers) +
                "/3 officers bypass mitigation cap)");
        } else if (pierce_officers == 1) {
            double pierce_bonus = pierce_value_sum * 25000.0;
            if (pierce_bonus < 15000) pierce_bonus = 15000;
            total += pierce_bonus;
            bd.synergy_notes.push_back("Partial piercing coverage (1/3 officers)");
        }
    }

    // ===================================================================
    // Crit coverage — value-based (PvP is always combat)
    // ===================================================================
    {
        std::set<std::string> all_applied, all_benefit;
        for (int i = 0; i < 3; ++i) {
            for (const auto& s : crew[i]->states_applied) all_applied.insert(s);
            for (const auto& s : crew[i]->states_benefit) all_benefit.insert(s);
        }

        std::set<std::string> synergy_states;
        for (const auto& s : all_applied) {
            if (all_benefit.count(s)) synergy_states.insert(s);
        }

        if (!synergy_states.empty()) {
            // Value-based state chain: sum OA values of state officers,
            // then scale by state type importance
            double crew_state_value = 0.0;
            for (int i = 0; i < 3; ++i) {
                if (!crew[i]->states_applied.empty() || !crew[i]->states_benefit.empty()) {
                    crew_state_value += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
                }
            }

            double chain_bonus = 0.0;
            for (const auto& s : synergy_states) {
                double state_base = crew_state_value * 25000.0;
                if (s == "burning" || s == "breach") {
                    chain_bonus += std::max(state_base, 35000.0);
                } else if (s == "morale") {
                    chain_bonus += std::max(state_base * 0.7, 20000.0);
                } else {
                    chain_bonus += std::max(state_base * 0.85, 28000.0);
                }
            }

            // Beneficiary multiplier
            int beneficiaries = 0;
            for (int i = 0; i < 3; ++i) {
                for (const auto& s : crew[i]->states_benefit) {
                    if (all_applied.count(s)) { ++beneficiaries; break; }
                }
            }
            chain_bonus *= 1.0 + std::min(beneficiaries, 2) * 0.25;

            // Proc reliability: scale by actual proc chances when available
            int guaranteed_count = 0;
            double avg_chance = 0.0;
            int chance_count = 0;
            for (int i = 0; i < 3; ++i) {
                if (!crew[i]->states_applied.empty()) {
                    if (crew[i]->proc_guaranteed) ++guaranteed_count;
                    if (crew[i]->oa_chance > 0.0) {
                        avg_chance += crew[i]->oa_chance;
                        ++chance_count;
                    }
                }
            }
            if (guaranteed_count >= 2) {
                chain_bonus *= 1.4;
                bd.synergy_notes.push_back(
                    "Guaranteed state procs (" + std::to_string(guaranteed_count) + " officers)");
            } else if (guaranteed_count == 1) {
                chain_bonus *= 1.15;
            } else if (chance_count > 0) {
                // Use actual average proc chance to scale
                double avg_ch = avg_chance / chance_count;
                chain_bonus *= std::max(avg_ch, 0.3);  // Floor at 30% effectiveness
            }

            total += chain_bonus;
            bd.state_chain_bonus = chain_bonus;

            std::string joined;
            for (const auto& s : synergy_states) {
                if (!joined.empty()) joined += ", ";
                joined += s;
            }
            bd.synergy_notes.push_back(
                "State synergy chain: " + joined + " (" +
                std::to_string(beneficiaries) + " officers benefit)");

            // State-spread penalty
            if (all_applied.size() >= 3) {
                double spread_penalty = total * 0.10;
                total -= spread_penalty;
                bd.penalties.push_back(
                    "State spread (" + std::to_string(all_applied.size()) +
                    " different states) -- focused crews are more effective");
            }
        } else if (all_applied.empty()) {
            double penalty = total * 0.15;
            total -= penalty;
            bd.penalties.push_back("No state applicator: conditional abilities may not trigger");
        }
    }

    // Coherence (focused state theme)
    apply_coherence(total, bd, crew, 40000, 15000);

    // ===================================================================
    // STEP 4: RPS META Archetype Detection — value-based
    // ===================================================================
    {
        int apex_barrier_count = 0;
        int apex_shred_count = 0;
        int isolytic_cascade_count = 0;
        int shots_count = 0;
        int cumulative_count = 0;
        double barrier_value_sum = 0.0;
        double shred_value_sum = 0.0;
        double isolytic_value_sum = 0.0;
        double shots_value_sum = 0.0;
        double cumul_value_sum = 0.0;

        for (int i = 0; i < 3; ++i) {
            if (crew[i]->apex_barrier) {
                ++apex_barrier_count;
                barrier_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
            if (crew[i]->apex_shred) {
                ++apex_shred_count;
                shred_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
            if (crew[i]->isolytic_cascade) {
                ++isolytic_cascade_count;
                isolytic_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
            if (crew[i]->shots_related) {
                ++shots_count;
                shots_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
            if (crew[i]->cumulative_stacking) {
                ++cumulative_count;
                cumul_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }

        if (apex_barrier_count >= 2) {
            double bonus = std::max(barrier_value_sum * 30000.0, 35000.0);
            total += bonus;
            bd.synergy_notes.push_back(
                "Apex Barrier crew (" + std::to_string(apex_barrier_count) + "/3 officers)");
        }
        if (apex_shred_count >= 2) {
            double bonus = std::max(shred_value_sum * 35000.0, 40000.0);
            total += bonus;
            bd.synergy_notes.push_back(
                "Apex Shred crew (" + std::to_string(apex_shred_count) +
                "/3) -- counters Apex Barrier META");
        }
        if (isolytic_cascade_count >= 2) {
            double bonus = std::max(isolytic_value_sum * 25000.0, 28000.0);
            total += bonus;
            bd.synergy_notes.push_back(
                "Isolytic Cascade coverage (" + std::to_string(isolytic_cascade_count) + "/3 officers)");
        }
        if (shots_count >= 2) {
            double bonus = std::max(shots_value_sum * 20000.0, 20000.0);
            total += bonus;
            bd.synergy_notes.push_back(
                "Generalist shots crew (" + std::to_string(shots_count) + "/3 officers)");
        }
        if (cumulative_count >= 2) {
            double bonus = std::max(cumul_value_sum * 22000.0, 25000.0);
            total += bonus;
            bd.synergy_notes.push_back(
                "Cumulative stacking crew (" + std::to_string(cumulative_count) +
                "/3) -- effects compound each round");
        }
    }

    // Ship-type coverage (PvP is always combat)
    {
        int crit_officers = 0;
        double crit_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->crit_related) {
                ++crit_officers;
                crit_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (crit_officers >= 2) {
            double bonus = std::max(crit_value_sum * 18000.0, 20000.0);
            total += bonus;
            bd.crit_bonus = bonus;
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
    apply_weakness_counters_pvp(total, bd, crew, 25000, 15000);

    // PvE-only penalty — use classification tags, not fragile text matching
    for (int i = 0; i < 3; ++i) {
        if (crew[i]->mining || crew[i]->cargo || crew[i]->pve_hostile ||
            crew[i]->is_pve_specific || crew[i]->warp) {
            if (!crew[i]->is_pvp_specific && !crew[i]->is_dual_use) {
                double penalty = total * 0.15;
                total -= penalty;
                bd.penalty_total += penalty;
                bd.penalties.push_back(
                    "PvE/utility officer '" + crew[i]->name + "' in PvP crew");
            }
        }
    }

    // OA% bonus — scenario-weighted
    apply_oa_bonus(total, bd, crew, Scenario::PvP);

    // OA ship-lock penalty
    apply_oa_ship_penalty(total, bd, b1, b2);

    result.score = total;
    return result;
}

// ---------------------------------------------------------------------------
// Crew scoring: Hybrid — balanced PvP/PvE with synergy-adjusted CM
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

    // ===================================================================
    // Synergy-Adjusted CM — value-based (same as PvP but slightly lower synergy incentive)
    // ===================================================================
    if (captain.is_bda()) {
        double penalty = total * 0.25;
        total -= penalty;
        bd.penalty_total += penalty;
        bd.penalties.push_back("'" + captain.name + "' has a BDA, not a Captain Maneuver -- wasted captain slot");
    } else if (!cm_works_on_ship(captain)) {
        double penalty = total * 0.2;
        total -= penalty;
        bd.penalty_total += penalty;
        bd.penalties.push_back("'" + captain.name + "' CM locked to non-" + ship_label + " ship type");
    } else {
        // Compute synergy inline
        double synergy_pct = 0.0;
        if (!captain.group.empty()) {
            bool all_unique_classes = (captain.officer_class != 0 &&
                                       b1.officer_class != 0 &&
                                       b2.officer_class != 0 &&
                                       captain.officer_class != b1.officer_class &&
                                       captain.officer_class != b2.officer_class &&
                                       b1.officer_class != b2.officer_class);
            auto calc_bars = [&](const ClassifiedOfficer& side) -> int {
                if (side.group.empty() || side.group != captain.group) return 0;
                if (all_unique_classes) return 2;
                return 1;
            };
            int bars_left = calc_bars(b1);
            int bars_right = calc_bars(b2);
            synergy_pct = bars_left * 10.0 + bars_right * 10.0;

            if (synergy_pct > 0) {
                bd.bridge_synergy_pct = synergy_pct;
                bd.bridge_synergy_bars_left = bars_left;
                bd.bridge_synergy_bars_right = bars_right;
                std::string bar_str_l = (bars_left == 2) ? "##" : (bars_left == 1 ? "#" : "-");
                std::string bar_str_r = (bars_right == 2) ? "##" : (bars_right == 1 ? "#" : "-");
                std::string note = "Bridge synergy [" + bar_str_l + "|" + bar_str_r + "] +" +
                                   std::to_string(static_cast<int>(synergy_pct)) + "% CM";
                if (!captain.group.empty()) note += " (" + captain.group + ")";
                if (all_unique_classes) note += " [Cmd/Sci/Eng]";
                bd.synergy_notes.push_back(note);
            }
        }

        // Value-based CM: use cm_value (actual decimal) * scope_weight * synergy
        double cm_val = captain.cm_value;
        if (cm_val <= 0.0 && captain.cm_pct > 0.0) cm_val = captain.cm_pct / 100.0;
        cm_val = std::min(cm_val, 5.0);  // Cap at 500%

        double cm_base = cm_val * cm_scope_weight(captain.cm_scope) * 100.0;  // Scale to crew range
        double synergy_mult = 1.0 + synergy_pct / 100.0;
        double cm_bonus = cm_base * synergy_mult;

        // Slightly lower flat synergy incentive than PvP (2000 vs 2500)
        cm_bonus += synergy_pct * 2000.0;
        total += cm_bonus;
        bd.bridge_synergy_bonus = cm_bonus - cm_base;
    }

    // ===================================================================
    // Ability amplifier captain — value-based
    // ===================================================================
    if (captain.ability_amplifier) {
        // Value-based: amplifier captain's CM value scales the bonus
        double amp_val = captain.cm_value;
        if (amp_val <= 0.0 && captain.cm_pct > 0.0) amp_val = captain.cm_pct / 100.0;
        double amp_bonus = std::max(amp_val * 200000.0, 60000.0);
        total += amp_bonus;
        bd.amplifier_bonus = amp_bonus;
        bd.synergy_notes.push_back(
            "Ability amplifier captain '" + captain.name + "' -- boosts all bridge abilities in both PvE and PvP");
    }

    // ===================================================================
    // Dual-use coherence — value-based
    // ===================================================================
    {
        int dual_use_count = 0;
        double dual_use_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->is_dual_use) {
                ++dual_use_count;
                dual_use_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (dual_use_count == 3) {
            double bonus = std::max(dual_use_value_sum * 35000.0, 50000.0);
            total += bonus;
            bd.dual_use_bonus = bonus;
            bd.synergy_notes.push_back("Full dual-use crew -- all 3 officers work in both PvE and PvP");
        } else if (dual_use_count == 2) {
            double bonus = std::max(dual_use_value_sum * 20000.0, 20000.0);
            total += bonus;
            bd.dual_use_bonus = bonus;
            bd.synergy_notes.push_back("Partial dual-use crew -- 2/3 officers work in both modes");
        }
    }

    // ===================================================================
    // State synergy chain — value-weighted (hybrid: lower than PvP)
    // ===================================================================
    {
        std::set<std::string> all_applied, all_benefit;
        for (int i = 0; i < 3; ++i) {
            for (const auto& s : crew[i]->states_applied) all_applied.insert(s);
            for (const auto& s : crew[i]->states_benefit) all_benefit.insert(s);
        }

        std::set<std::string> synergy_states;
        for (const auto& s : all_applied) {
            if (all_benefit.count(s)) synergy_states.insert(s);
        }

        if (!synergy_states.empty()) {
            // Sum OA values of state-relevant officers
            double crew_state_value = 0.0;
            for (int i = 0; i < 3; ++i) {
                if (!crew[i]->states_applied.empty() || !crew[i]->states_benefit.empty()) {
                    crew_state_value += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
                }
            }

            double chain_bonus = 0.0;
            for (const auto& s : synergy_states) {
                // Hybrid: 20000 base per 1.0 of state value (lower than PvP's 25000)
                double state_base = crew_state_value * 20000.0;
                if (s == "burning" || s == "breach") {
                    chain_bonus += std::max(state_base, 28000.0);
                } else if (s == "morale") {
                    chain_bonus += std::max(state_base * 0.7, 16000.0);
                } else {
                    chain_bonus += std::max(state_base * 0.85, 22000.0);
                }
            }

            // Beneficiary multiplier
            int beneficiaries = 0;
            for (int i = 0; i < 3; ++i) {
                for (const auto& s : crew[i]->states_benefit) {
                    if (all_applied.count(s)) { ++beneficiaries; break; }
                }
            }
            chain_bonus *= 1.0 + std::min(beneficiaries, 2) * 0.25;

            // Proc reliability
            int guaranteed_count = 0;
            double avg_chance = 0.0;
            int chance_count = 0;
            for (int i = 0; i < 3; ++i) {
                if (!crew[i]->states_applied.empty()) {
                    if (crew[i]->proc_guaranteed) ++guaranteed_count;
                    if (crew[i]->oa_chance > 0.0) {
                        avg_chance += crew[i]->oa_chance;
                        ++chance_count;
                    }
                }
            }
            if (guaranteed_count >= 2) {
                chain_bonus *= 1.3;
            } else if (guaranteed_count == 1) {
                chain_bonus *= 1.1;
            } else if (chance_count > 0) {
                double avg_ch = avg_chance / chance_count;
                chain_bonus *= std::max(avg_ch, 0.3);
            }

            total += chain_bonus;
            bd.state_chain_bonus = chain_bonus;

            std::string joined;
            for (const auto& s : synergy_states) {
                if (!joined.empty()) joined += ", ";
                joined += s;
            }
            bd.synergy_notes.push_back(
                "State synergy chain: " + joined + " (" +
                std::to_string(beneficiaries) + " officers benefit)");

            // State-spread penalty (mirroring PvP)
            if (all_applied.size() >= 3) {
                double spread_penalty = total * 0.10;
                total -= spread_penalty;
                bd.penalties.push_back(
                    "State spread (" + std::to_string(all_applied.size()) +
                    " different states) -- focused crews are more effective");
            }
        }
    }

    // Coherence (focused state theme)
    apply_coherence(total, bd, crew, 35000, 15000);

    // ===================================================================
    // Crit coverage — value-based
    // ===================================================================
    {
        int crit_officers = 0;
        double crit_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->crit_related) {
                ++crit_officers;
                crit_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (crit_officers >= 2) {
            double bonus = std::max(crit_value_sum * 15000.0, 18000.0);
            total += bonus;
            bd.crit_bonus = bonus;
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

    // NOTE: Bridge synergy computed inline above with CM.
    // apply_bridge_synergy() NOT called to avoid double-counting.

    // OA% bonus — scenario-weighted
    apply_oa_bonus(total, bd, crew, Scenario::Hybrid);

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

    // ===================================================================
    // Captain maneuver — value-based scope-weighted
    // ===================================================================
    if (captain.is_bda()) {
        double penalty = total * 0.25;
        total -= penalty;
        bd.penalty_total += penalty;
        bd.penalties.push_back("'" + captain.name + "' has a BDA, not a Captain Maneuver -- wasted captain slot");
    } else if (!cm_works_on_ship(captain)) {
        double penalty = total * 0.2;
        total -= penalty;
        bd.penalty_total += penalty;
        bd.penalties.push_back("'" + captain.name + "' CM locked to non-" + ship_label + " ship type");
    } else {
        // Value-based CM: use cm_value (actual decimal) * scope_weight
        double cm_val = captain.cm_value;
        if (cm_val <= 0.0 && captain.cm_pct > 0.0) cm_val = captain.cm_pct / 100.0;
        cm_val = std::min(cm_val, 5.0);
        double captain_slot_bonus = cm_val * cm_scope_weight(captain.cm_scope) * 100.0;
        total += captain_slot_bonus;
        score_parts.captain_slot += captain_slot_bonus;
    }

    // ===================================================================
    // Ability amplifier captain — value-based
    // ===================================================================
    if (captain.ability_amplifier) {
        double amp_val = captain.cm_value;
        if (amp_val <= 0.0 && captain.cm_pct > 0.0) amp_val = captain.cm_pct / 100.0;
        double amp_bonus = std::max(amp_val * 150000.0, 45000.0);
        total += amp_bonus;
        bd.amplifier_bonus = amp_bonus;
        score_parts.synergy += amp_bonus;
        bd.synergy_notes.push_back(
            "Ability amplifier captain '" + captain.name + "' boosts all bridge abilities");
    }

    // State synergy chain — only for combat scenarios, NOT mining
    bool is_mining_scenario = (scenario == Scenario::MiningSpeed ||
                               scenario == Scenario::MiningProtected ||
                               scenario == Scenario::MiningGas ||
                               scenario == Scenario::MiningOre ||
                               scenario == Scenario::MiningCrystal ||
                               scenario == Scenario::MiningGeneral);
    if (!is_mining_scenario) {
        // ===================================================================
        // Combat scenarios: value-based state chain
        // ===================================================================
        {
            std::set<std::string> all_applied, all_benefit;
            for (int i = 0; i < 3; ++i) {
                for (const auto& s : crew[i]->states_applied) all_applied.insert(s);
                for (const auto& s : crew[i]->states_benefit) all_benefit.insert(s);
            }

            std::set<std::string> synergy_states;
            for (const auto& s : all_applied) {
                if (all_benefit.count(s)) synergy_states.insert(s);
            }

            if (!synergy_states.empty()) {
                double crew_state_value = 0.0;
                for (int i = 0; i < 3; ++i) {
                    if (!crew[i]->states_applied.empty() || !crew[i]->states_benefit.empty()) {
                        crew_state_value += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
                    }
                }

                double chain_bonus = 0.0;
                for (const auto& s : synergy_states) {
                    double state_base = crew_state_value * 22000.0;
                    if (s == "burning" || s == "breach") {
                        chain_bonus += std::max(state_base, 30000.0);
                    } else if (s == "morale") {
                        chain_bonus += std::max(state_base * 0.7, 18000.0);
                    } else {
                        chain_bonus += std::max(state_base * 0.85, 24000.0);
                    }
                }

                int beneficiaries = 0;
                for (int i = 0; i < 3; ++i) {
                    for (const auto& s : crew[i]->states_benefit) {
                        if (all_applied.count(s)) { ++beneficiaries; break; }
                    }
                }
                chain_bonus *= 1.0 + std::min(beneficiaries, 2) * 0.25;

                // Proc reliability
                int guaranteed_count = 0;
                double avg_chance = 0.0;
                int chance_count = 0;
                for (int i = 0; i < 3; ++i) {
                    if (!crew[i]->states_applied.empty()) {
                        if (crew[i]->proc_guaranteed) ++guaranteed_count;
                        if (crew[i]->oa_chance > 0.0) {
                            avg_chance += crew[i]->oa_chance;
                            ++chance_count;
                        }
                    }
                }
                if (guaranteed_count >= 2) {
                    chain_bonus *= 1.3;
                } else if (guaranteed_count == 1) {
                    chain_bonus *= 1.1;
                } else if (chance_count > 0) {
                    double avg_ch = avg_chance / chance_count;
                    chain_bonus *= std::max(avg_ch, 0.3);
                }

                total += chain_bonus;
                bd.state_chain_bonus = chain_bonus;

                std::string joined;
                for (const auto& s : synergy_states) {
                    if (!joined.empty()) joined += ", ";
                    joined += s;
                }
                bd.synergy_notes.push_back(
                    "State synergy chain: " + joined + " (" +
                    std::to_string(beneficiaries) + " officers benefit)");
            }
        }
        // Coherence
        apply_coherence(total, bd, crew, 40000, 15000);
    }

    // ===================================================================
    // Crit coverage — value-based
    // ===================================================================
    {
        int crit_officers = 0;
        double crit_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->crit_related) {
                ++crit_officers;
                crit_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (crit_officers >= 2) {
            double crit_bonus = std::max(crit_value_sum * 14000.0, 18000.0);
            total += crit_bonus;
            bd.crit_bonus = crit_bonus;
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

    // ===================================================================
    // Scenario-specific crew bonuses — value-based
    // ===================================================================
    double scenario_bonus = 0.0;

    if (scenario == Scenario::BaseCracker) {
        int base_attackers = 0;
        double attacker_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->base_attack) {
                ++base_attackers;
                attacker_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (base_attackers >= 2) {
            scenario_bonus += std::max(attacker_value_sum * 40000.0, 50000.0);
            bd.synergy_notes.push_back(
                "Station attack crew (" + std::to_string(base_attackers) + "/3 officers)");
        }
        int delay_officers = 0;
        double delay_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->weapon_delay) {
                ++delay_officers;
                delay_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (delay_officers >= 1) {
            scenario_bonus += std::max(delay_value_sum * 20000.0, 12000.0);
            bd.synergy_notes.push_back(
                "Weapon delay (" + std::to_string(delay_officers) + " officer(s))");
        }
    } else if (scenario == Scenario::PvEHostile) {
        int pve_count = 0;
        double pve_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->pve_hostile) {
                ++pve_count;
                pve_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (pve_count >= 2) {
            scenario_bonus += std::max(pve_value_sum * 25000.0, 35000.0);
            bd.synergy_notes.push_back(
                "PvE-focused crew (" + std::to_string(pve_count) + "/3 officers)");
        }
        int repair_count = 0;
        double repair_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->repair) {
                ++repair_count;
                repair_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (repair_count >= 1) {
            scenario_bonus += std::max(repair_value_sum * 14000.0, 8000.0);
            bd.synergy_notes.push_back(
                "Repair capability (" + std::to_string(repair_count) + " officer(s))");
        }
    } else if (scenario == Scenario::MissionBoss) {
        int mission_count = 0;
        double mission_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->mission_boss) {
                ++mission_count;
                mission_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (mission_count >= 1) {
            scenario_bonus += std::max(mission_value_sum * 30000.0, 20000.0);
            bd.synergy_notes.push_back(
                "Mission specialist (" + std::to_string(mission_count) + " officer(s))");
        }
        double total_atk = 0;
        for (int i = 0; i < 3; ++i) total_atk += crew[i]->attack;
        if (total_atk > 150000) {
            scenario_bonus += static_cast<int>((total_atk - 150000) * 0.1);
        }
        int iso_count = 0;
        double iso_value_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->isolytic_cascade || crew[i]->isolytic_defense) {
                ++iso_count;
                iso_value_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (iso_count >= 2) {
            scenario_bonus += std::max(iso_value_sum * 18000.0, 25000.0);
        }
    } else if (scenario == Scenario::Loot) {
        int mining_count = 0;
        double mining_val_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->mining) {
                ++mining_count;
                mining_val_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (mining_count >= 2) {
            scenario_bonus += std::max(mining_val_sum * 35000.0, 50000.0);
            bd.synergy_notes.push_back(
                "Mining crew (" + std::to_string(mining_count) + "/3 officers)");
        }
        int cargo_count = 0;
        double cargo_val_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->cargo) {
                ++cargo_count;
                cargo_val_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (cargo_count >= 1) {
            scenario_bonus += std::max(cargo_val_sum * 25000.0, 15000.0);
        }
        int loot_count = 0;
        double loot_val_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->loot) {
                ++loot_count;
                loot_val_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (loot_count >= 1) {
            scenario_bonus += std::max(loot_val_sum * 25000.0, 15000.0);
        }
        double total_def = 0, total_hp = 0;
        for (int i = 0; i < 3; ++i) { total_def += crew[i]->defense; total_hp += crew[i]->health; }
        scenario_bonus += static_cast<int>(total_def * 0.1 + total_hp * 0.05);
    } else if (scenario == Scenario::MiningSpeed ||
               scenario == Scenario::MiningProtected ||
               scenario == Scenario::MiningGas ||
               scenario == Scenario::MiningOre ||
               scenario == Scenario::MiningCrystal ||
               scenario == Scenario::MiningGeneral) {
        // Mining scenarios: left mostly as-is per instructions
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
        // Armada: LOOT ROI focus — value-based
        int armada_count = 0;
        int loot_count = 0;
        double armada_val_sum = 0.0;
        double loot_val_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->armada) {
                ++armada_count;
                armada_val_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
            if (crew[i]->loot) {
                ++loot_count;
                loot_val_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        // Loot coverage is the PRIMARY crew-level factor
        if (loot_count >= 2) {
            scenario_bonus += std::max(loot_val_sum * 35000.0, 50000.0);
            bd.synergy_notes.push_back(
                "Loot crew (" + std::to_string(loot_count) + "/3 officers boost rewards)");
        } else if (loot_count == 1) {
            scenario_bonus += std::max(loot_val_sum * 20000.0, 12000.0);
            bd.synergy_notes.push_back("Partial loot coverage (1/3 officers)");
        }
        // Armada-specific officers are secondary
        if (armada_count >= 2) {
            scenario_bonus += std::max(armada_val_sum * 18000.0, 25000.0);
            bd.synergy_notes.push_back(
                "Armada crew (" + std::to_string(armada_count) + "/3 officers)");
        }
        int iso_count = 0;
        double iso_val_sum = 0.0;
        for (int i = 0; i < 3; ++i) {
            if (crew[i]->isolytic_related) {
                ++iso_count;
                iso_val_sum += (crew[i]->oa_value > 0.0) ? crew[i]->oa_value : 0.5;
            }
        }
        if (iso_count >= 2) {
            scenario_bonus += std::max(iso_val_sum * 12000.0, 16000.0);
        }
    }

    total += scenario_bonus;
    bd.scenario_bonus = scenario_bonus;
    score_parts.scenario += scenario_bonus;

    // Weakness counters (only for base_cracker in generic scenario)
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
    } else if (scenario == Scenario::PvEHostile || scenario == Scenario::MissionBoss) {
        for (int i = 0; i < 3; ++i) {
            if ((crew[i]->mining || crew[i]->cargo) &&
                !crew[i]->pve_hostile && !crew[i]->mission_boss && !crew[i]->is_dual_use) {
                double penalty = total * 0.15;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' is a mining/cargo officer -- useless in hostile combat");
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
    } else if (is_mining_scenario) {
        // ALL mining scenarios: penalize non-mining officers
        for (int i = 0; i < 3; ++i) {
            if (!crew[i]->mining && !crew[i]->cargo && !crew[i]->loot) {
                // Non-mining officer in a mining slot: heavy penalty
                double penalty = total * 0.35;
                total -= penalty;
                bd.penalty_total += penalty;
                score_parts.penalties += penalty;
                bd.penalties.push_back("'" + crew[i]->name + "' has no mining/cargo abilities -- weak mining value");
            }
        }
    }

    // Bridge synergy group (game synergy groups)
    apply_bridge_synergy(total, bd, crew);

    // OA% bonus — scenario-weighted
    double before_oa = total;
    apply_oa_bonus(total, bd, crew, scenario);
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

    // Take top 50 candidates by individual score
    const int BASE_N = std::min(static_cast<int>(scored.size()), 50);
    std::set<size_t> candidate_set;
    for (int i = 0; i < BASE_N; ++i) candidate_set.insert(scored[i].second);

    // Synergy reserve: also include officers outside top 40 who have synergy
    // value that can't be captured by individual scoring alone.
    // State applicators/beneficiaries, ability amplifiers, high CM% officers,
    // and officers sharing a bridge synergy group with existing candidates
    // are often low-stat but create massive crew-level bonuses.

    // First pass: collect groups already represented in candidate set
    std::set<std::string> candidate_groups;
    for (size_t idx : candidate_set) {
        if (!officers_[idx].group.empty())
            candidate_groups.insert(officers_[idx].group);
    }

    for (const auto& [s, idx] : scored) {
        if (candidate_set.count(idx)) continue;
        const auto& o = officers_[idx];
        bool has_synergy = !o.states_applied.empty()
                        || !o.states_benefit.empty()
                        || o.ability_amplifier
                        || o.cm_pct >= 40
                        || (!o.group.empty() && candidate_groups.count(o.group));
        if (has_synergy) candidate_set.insert(idx);
    }

    // Cap at 65 to keep search time reasonable (C(65,3)*3 = ~130K evals)
    std::vector<size_t> candidates(candidate_set.begin(), candidate_set.end());
    if (candidates.size() > 65) {
        // Sort by individual score descending, keep top 65
        std::map<size_t, double> score_map;
        for (const auto& [s, idx] : scored) score_map[idx] = s;
        std::sort(candidates.begin(), candidates.end(),
                  [&score_map](size_t a, size_t b) {
                      return score_map[a] > score_map[b];
                  });
        candidates.resize(65);
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

        // 1. Base BDA value — value-based
        // Dedicated BDA officers have bda_value from bootstrap/API data.
        // Use actual ability magnitude instead of flat bonus.
        if (dedicated_bda) {
            double bda_val = off.bda_value;
            if (bda_val <= 0.0 && off.oa_value > 0.0) bda_val = off.oa_value;
            double base_bonus = std::max(bda_val * 80000.0, 40000.0);
            score += base_bonus;
            reasons.push_back("Designed as BDA officer");
        } else {
            // Non-BDA officers are fallback options only.
            score -= 25000.0;
        }

        // OA quality: use actual oa_value instead of oa_pct * 10
        double oa_val = off.oa_value;
        if (oa_val <= 0.0 && off.oa_pct > 0.0) oa_val = off.oa_pct / 100.0;
        if (oa_val > 0.0) {
            score += std::min(oa_val, 5.0) * 2000.0;  // up to 10000 for extreme values
        }

        // BDA effect magnitude
        double bda_effect = off.bda_effect_pct();
        if (bda_effect > 0) {
            // Value-based: scale by actual BDA value when available
            double bda_val = off.bda_value;
            if (bda_val > 0.0) {
                score += std::min(bda_val, 3.0) * 12000.0;  // up to 36000
            } else {
                score += std::min(bda_effect, 300.0) * 120.0;  // legacy fallback
            }
            reasons.push_back("Strong BDA effect: " + std::to_string((int)std::round(bda_effect)) + "%");
        }

        // 2. State synergy: BDA applies states the crew benefits from — value-weighted
        std::set<std::string> chain_add;
        for (const auto& s : off.states_applied) {
            if (crew_benefit.count(s)) chain_add.insert(s);
        }
        if (!chain_add.empty()) {
            double state_val = (oa_val > 0.0) ? oa_val : 0.5;
            score += static_cast<double>(chain_add.size()) * std::max(state_val * 25000.0, 15000.0);
            std::string states_str;
            for (const auto& s : chain_add) {
                if (!states_str.empty()) states_str += ", ";
                states_str += s;
            }
            reasons.push_back("Applies " + states_str + " (crew benefits)");
        }

        // 3. State synergy: BDA benefits from states the crew applies — value-weighted
        std::set<std::string> chain_receive;
        for (const auto& s : off.states_benefit) {
            if (crew_applied.count(s)) chain_receive.insert(s);
        }
        if (!chain_receive.empty()) {
            double state_val = (oa_val > 0.0) ? oa_val : 0.5;
            score += static_cast<double>(chain_receive.size()) * std::max(state_val * 18000.0, 10000.0);
            std::string states_str;
            for (const auto& s : chain_receive) {
                if (!states_str.empty()) states_str += ", ";
                states_str += s;
            }
            reasons.push_back("Benefits from " + states_str + " (crew applies)");
        }

        // 4. Cover missing crew capabilities — value-weighted
        if (!has_crit && off.crit_related) {
            double crit_val = std::max(oa_val * 12000.0, 7000.0);
            score += crit_val;
            reasons.push_back("Adds crit coverage");
        }
        if (!has_shield && off.shield_related) {
            double shield_val = std::max(oa_val * 10000.0, 5000.0);
            score += shield_val;
            reasons.push_back("Adds shield support");
        }
        if (!has_mitigation && off.mitigation_related) {
            double mit_val = std::max(oa_val * 10000.0, 5000.0);
            score += mit_val;
            reasons.push_back("Adds mitigation");
        }

        // 5. Scenario fit — value-based where applicable
        // BDA below-deck picks still need to match the dock's job.
        switch (mode) {
        case Scenario::PvP:
            if (off.is_pvp_specific) {
                score += std::max(oa_val * 18000.0, 12000.0);
                reasons.push_back("Fits PvP dock");
            }
            if (off.crit_related) score += std::max(oa_val * 6000.0, 3000.0);
            if (off.shots_related) score += std::max(oa_val * 6000.0, 3000.0);
            if (off.isolytic_related) score += std::max(oa_val * 6000.0, 3000.0);
            if (off.is_pve_specific && !off.is_pvp_specific) score -= 12000.0;
            break;

        case Scenario::Hybrid:
            if (off.is_dual_use) {
                score += std::max(oa_val * 14000.0, 8000.0);
                reasons.push_back("Fits hybrid dock");
            }
            if (off.is_pve_specific && !off.is_dual_use) score -= 4000.0;
            if (off.is_pvp_specific && !off.is_dual_use) score -= 4000.0;
            break;

        case Scenario::BaseCracker:
            if (off.base_attack) {
                score += std::max(oa_val * 25000.0, 15000.0);
                reasons.push_back("Fits station attack");
            }
            if (off.weapon_delay) score += std::max(oa_val * 10000.0, 5000.0);
            if (off.base_defend) score -= 10000.0;
            if (off.mining || off.cargo) score -= 12000.0;
            break;

        case Scenario::PvEHostile:
            if (off.pve_hostile) {
                score += std::max(oa_val * 22000.0, 14000.0);
                reasons.push_back("Fits hostile grinding");
            }
            if (off.repair) score += std::max(oa_val * 8000.0, 4000.0);
            if (off.loot) score += 4000.0;
            if (off.is_pvp_specific && !off.pve_hostile) score -= 10000.0;
            break;

        case Scenario::MissionBoss:
            if (off.mission_boss || off.pve_hostile) {
                score += std::max(oa_val * 20000.0, 12000.0);
                reasons.push_back("Fits mission boss");
            }
            if (off.crit_related) score += std::max(oa_val * 8000.0, 4000.0);
            if (off.isolytic_related) score += std::max(oa_val * 8000.0, 4000.0);
            if (off.armada && !off.pve_hostile) score -= 5000.0;
            break;

        case Scenario::Loot:
            if (off.loot) {
                score += std::max(oa_val * 22000.0, 14000.0);
                reasons.push_back("Fits loot dock");
            }
            if (off.mining) score += 12000.0;
            if (off.cargo) score += 10000.0;
            if (off.warp) score += 4000.0;
            if (contains(off.bda_text, "loot") || contains(off.bda_text, "reward")) {
                score += std::max(off.bda_value * 15000.0, 10000.0);
            }
            if (contains(off.bda_text, "cargo")) {
                score += std::max(off.bda_value * 10000.0, 6000.0);
            }
            if (!off.loot && !off.mining && !off.cargo && !off.warp) score -= 10000.0;
            break;

        case Scenario::Armada:
            if (off.armada) {
                score += std::max(oa_val * 25000.0, 15000.0);
                reasons.push_back("Fits armada dock");
            }
            if (off.armada_solo) score += std::max(oa_val * 8000.0, 4000.0);
            if (off.crit_related) score += std::max(oa_val * 6000.0, 3000.0);
            if (off.isolytic_related) score += std::max(oa_val * 6000.0, 3000.0);
            if (off.non_armada_only) score -= 15000.0;
            break;

        case Scenario::MiningSpeed:
            if (off.mining_speed) {
                score += std::max(oa_val * 30000.0, 20000.0);
                reasons.push_back("Fits mining speed dock");
            }
            if (off.mining) score += std::max(oa_val * 15000.0, 8000.0);
            if (off.cargo) score += std::max(oa_val * 6000.0, 3000.0);
            {
                double bda_mv = bda_mining_value(off, Scenario::MiningSpeed);
                if (bda_mv > 0.0) {
                    score += bda_mv * 600.0;  // e.g., 30% speed → 30*0.6*600 = 10800
                    reasons.push_back("BDA mining speed +" + std::to_string(static_cast<int>(off.bda_mining_speed_pct)) + "%");
                }
            }
            if (!off.mining_speed && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningProtected:
            if (off.protected_cargo) {
                score += std::max(oa_val * 30000.0, 20000.0);
                reasons.push_back("Fits protected cargo dock");
            }
            if (off.cargo) score += std::max(oa_val * 15000.0, 8000.0);
            if (off.node_defense) score += std::max(oa_val * 10000.0, 6000.0);
            if (off.mining) score += std::max(oa_val * 6000.0, 3000.0);
            {
                double bda_mv = bda_mining_value(off, Scenario::MiningProtected);
                if (bda_mv > 0.0) {
                    score += bda_mv * 600.0;
                    reasons.push_back("BDA protected cargo +" + std::to_string(static_cast<int>(off.bda_protected_cargo_pct)) + "%");
                }
            }
            if (!off.protected_cargo && !off.cargo && !off.node_defense) score -= 12000.0;
            break;

        case Scenario::MiningCrystal:
            if (off.mining_crystal) {
                score += std::max(oa_val * 30000.0, 20000.0);
                reasons.push_back("Fits crystal mining dock");
            }
            if (off.mining_speed) score += std::max(oa_val * 12000.0, 7000.0);
            if (off.mining) score += std::max(oa_val * 10000.0, 5000.0);
            if (off.cargo || off.protected_cargo) score += std::max(oa_val * 5000.0, 2500.0);
            {
                double bda_mv = bda_mining_value(off, Scenario::MiningCrystal);
                if (bda_mv > 0.0) {
                    score += bda_mv * 600.0;
                    reasons.push_back("BDA crystal mining +" + std::to_string(static_cast<int>(off.bda_mining_crystal_pct)) + "%");
                }
            }
            if (!off.mining_crystal && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningGas:
            if (off.mining_gas) {
                score += std::max(oa_val * 30000.0, 20000.0);
                reasons.push_back("Fits gas mining dock");
            }
            if (off.mining_speed) score += std::max(oa_val * 12000.0, 7000.0);
            if (off.mining) score += std::max(oa_val * 10000.0, 5000.0);
            if (off.cargo || off.protected_cargo) score += std::max(oa_val * 5000.0, 2500.0);
            {
                double bda_mv = bda_mining_value(off, Scenario::MiningGas);
                if (bda_mv > 0.0) {
                    score += bda_mv * 600.0;
                    reasons.push_back("BDA gas mining +" + std::to_string(static_cast<int>(off.bda_mining_gas_pct)) + "%");
                }
            }
            if (!off.mining_gas && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningOre:
            if (off.mining_ore) {
                score += std::max(oa_val * 30000.0, 20000.0);
                reasons.push_back("Fits ore mining dock");
            }
            if (off.mining_speed) score += std::max(oa_val * 12000.0, 7000.0);
            if (off.mining) score += std::max(oa_val * 10000.0, 5000.0);
            if (off.cargo || off.protected_cargo) score += std::max(oa_val * 5000.0, 2500.0);
            {
                double bda_mv = bda_mining_value(off, Scenario::MiningOre);
                if (bda_mv > 0.0) {
                    score += bda_mv * 600.0;
                    reasons.push_back("BDA ore mining +" + std::to_string(static_cast<int>(off.bda_mining_ore_pct)) + "%");
                }
            }
            if (!off.mining_ore && !off.mining && !off.cargo) score -= 12000.0;
            break;

        case Scenario::MiningGeneral:
            if (off.mining) {
                score += std::max(oa_val * 22000.0, 14000.0);
                reasons.push_back("Fits mining dock");
            }
            if (off.mining_speed) score += std::max(oa_val * 15000.0, 8000.0);
            if (off.cargo) score += std::max(oa_val * 12000.0, 7000.0);
            if (off.protected_cargo) score += std::max(oa_val * 10000.0, 5000.0);
            if (off.node_defense) score += std::max(oa_val * 6000.0, 3000.0);
            {
                double bda_mv = bda_mining_value(off, Scenario::MiningGeneral);
                if (bda_mv > 0.0) {
                    score += bda_mv * 500.0;
                    reasons.push_back("BDA mining ability");
                }
            }
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
        // Core fields
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
        // Detail fields
        bda.is_dedicated_bda = c.dedicated_bda;
        bda.officer_type = c.off->officer_type_str;
        bda.bda_text = c.off->bda_text;
        bda.oa_text = c.off->oa_text;
        bda.bda_value = c.off->bda_value;
        bda.oa_value = c.off->oa_value;
        // Tag fields
        bda.cargo = c.off->cargo;
        bda.loot = c.off->loot;
        bda.mining = c.off->mining;
        bda.protected_cargo = c.off->protected_cargo;
        bda.crit_related = c.off->crit_related;
        bda.shield_related = c.off->shield_related;
        bda.mitigation_related = c.off->mitigation_related;
        bda.repair = c.off->repair;
        bda.armada = c.off->armada;
        // Percentage fields
        bda.oa_cargo_pct = c.off->oa_cargo_pct;
        bda.oa_protected_cargo_pct = c.off->oa_protected_cargo_pct;
        bda.oa_mining_speed_pct = c.off->oa_mining_speed_pct;
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

        const std::string& tag = ship.ability_tag;
        bool is_mining_ship = tag.find("mining") != std::string::npos;
        bool is_loot_ship = (tag == "loot");
        bool is_hostile_ship = (tag == "combat_hostile" || tag == "combat_swarm" ||
                                tag == "combat_borg" || tag == "combat_gorn" ||
                                tag == "combat_xindi" || tag == "combat_eclipse");
        bool is_pvp_ship = (tag == "combat_pvp");

        switch (scenario) {
            case Scenario::PvP:
            case Scenario::Hybrid:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Explorer) score += 90;
                if (ship.ship_type == ShipType::Battleship) score += 70;
                if (ship.ship_type == ShipType::Interceptor) score += 50;
                if (is_pvp_ship) score += 100;
                if (has_name("revenant") || has_name("cube")) score += 120;
                if (is_mining_ship) score -= 200;  // mining ships bad for PvP
                break;
            case Scenario::BaseCracker:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Interceptor) score += 90;
                if (ship.ship_type == ShipType::Battleship) score += 70;
                if (ship.ship_type == ShipType::Explorer) score += 50;
                if (is_pvp_ship) score += 100;
                if (has_name("revenant") || has_name("cube")) score += 120;
                if (is_mining_ship) score -= 200;
                break;
            case Scenario::Armada:
            case Scenario::MissionBoss:
            case Scenario::PvEHostile:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Explorer) score += 95;
                if (ship.ship_type == ShipType::Battleship) score += 70;
                if (ship.ship_type == ShipType::Interceptor) score += 50;
                if (is_hostile_ship) score += 120;
                if (has_name("protector") || has_name("voyager") || has_name("cerritos")) score += 120;
                if (is_mining_ship) score -= 200;
                break;
            case Scenario::Loot:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Survey) score += 120;
                if (is_loot_ship) score += 200;
                if (is_mining_ship) score += 80;  // still survey ships
                break;
            case Scenario::MiningCrystal:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Survey) score += 120;
                if (tag == "mining_crystal") score += 300;
                else if (tag == "mining_universal") score += 250;
                else if (is_mining_ship) score += 100;
                if (has_name("selkie")) score += 240;
                if (has_name("meridian")) score += 140;
                break;
            case Scenario::MiningGas:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Survey) score += 120;
                if (tag == "mining_gas") score += 300;
                else if (tag == "mining_universal") score += 250;
                else if (is_mining_ship) score += 100;
                if (has_name("selkie")) score += 240;
                if (has_name("meridian")) score += 140;
                break;
            case Scenario::MiningOre:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Survey) score += 120;
                if (tag == "mining_ore") score += 300;
                else if (tag == "mining_universal") score += 250;
                else if (is_mining_ship) score += 100;
                if (has_name("selkie")) score += 240;
                if (has_name("meridian")) score += 140;
                break;
            case Scenario::MiningSpeed:
            case Scenario::MiningProtected:
            case Scenario::MiningGeneral:
                score += grade_band_bonus(6);
                if (ship.ship_type == ShipType::Survey) score += 120;
                if (tag == "mining_universal") score += 250;
                else if (is_mining_ship) score += 180;
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

    // Build priority-sorted fill order: highest priority docks get best officers.
    // Locked docks are processed first (they don't compete for officers).
    // Then remaining docks in descending priority order.
    std::vector<size_t> fill_order;
    fill_order.reserve(n_docks);

    // Phase 1: locked docks first (they just claim their officers)
    for (size_t i = 0; i < n_docks; ++i) {
        if (dock_configs[i].locked && !dock_configs[i].locked_captain.empty())
            fill_order.push_back(i);
    }

    // Phase 2: unlocked docks sorted by priority (highest first, ties break by
    // dock index to keep deterministic ordering)
    std::vector<size_t> unlocked;
    for (size_t i = 0; i < n_docks; ++i) {
        if (!(dock_configs[i].locked && !dock_configs[i].locked_captain.empty()))
            unlocked.push_back(i);
    }
    std::sort(unlocked.begin(), unlocked.end(), [&](size_t a, size_t b) {
        if (dock_configs[a].priority != dock_configs[b].priority)
            return dock_configs[a].priority > dock_configs[b].priority;
        return a < b;
    });
    fill_order.insert(fill_order.end(), unlocked.begin(), unlocked.end());

    // Track BDA assignments across docks so the same BD officer isn't suggested
    // for multiple docks.
    std::set<std::string> bda_excluded;

    for (size_t idx : fill_order) {
        const auto& cfg = dock_configs[idx];
        auto& dr = loadout.docks[idx];

        dr.dock_num = static_cast<int>(idx) + 1;
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

            // BDA suggestions for locked docks too
            std::set<std::string> combined_excl = excluded;
            combined_excl.insert(bda_excluded.begin(), bda_excluded.end());
            dr.bda_suggestions = find_best_bda(
                dr.captain, dr.bridge, cfg.scenario, 3, combined_excl);
            for (const auto& bda : dr.bda_suggestions)
                bda_excluded.insert(bda.name);

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

            // Find BDA suggestions, excluding officers used in other docks'
            // bridge AND officers already assigned as BDA to higher-priority docks
            std::set<std::string> combined_excl = excluded;
            combined_excl.insert(bda_excluded.begin(), bda_excluded.end());
            dr.bda_suggestions = find_best_bda(
                dr.captain, dr.bridge, cfg.scenario, 3, combined_excl);
            for (const auto& bda : dr.bda_suggestions)
                bda_excluded.insert(bda.name);
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
