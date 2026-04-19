#include "core/crew_optimizer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <cctype>

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
// Hostile type / objective string conversions
// ---------------------------------------------------------------------------

const char* hostile_type_str(HostileType t) {
    switch (t) {
        case HostileType::Generic:        return "generic";
        case HostileType::Swarm:          return "swarm";
        case HostileType::BorgProbe:      return "borg_probe";
        case HostileType::Eclipse:        return "eclipse";
        case HostileType::Gorn:           return "gorn";
        case HostileType::Xindi:          return "xindi";
        case HostileType::Silent:         return "silent";
        case HostileType::Species8472:    return "species_8472";
        case HostileType::Breen:          return "breen";
        case HostileType::Hirogen:        return "hirogen";
        case HostileType::TexasClass:     return "texas_class";
        case HostileType::Monaveen:       return "monaveen";
        case HostileType::MirrorUniverse: return "mirror_universe";
        case HostileType::Freebooter:     return "freebooter";
        case HostileType::Actian:         return "actian";
        case HostileType::Assimilated:    return "assimilated";
    }
    return "generic";
}

HostileType hostile_type_from_str(const std::string& s) {
    if (s == "swarm")           return HostileType::Swarm;
    if (s == "borg_probe")      return HostileType::BorgProbe;
    if (s == "eclipse")         return HostileType::Eclipse;
    if (s == "gorn")            return HostileType::Gorn;
    if (s == "xindi")           return HostileType::Xindi;
    if (s == "silent")          return HostileType::Silent;
    if (s == "species_8472")    return HostileType::Species8472;
    if (s == "breen")           return HostileType::Breen;
    if (s == "hirogen")         return HostileType::Hirogen;
    if (s == "texas_class")     return HostileType::TexasClass;
    if (s == "monaveen")        return HostileType::Monaveen;
    if (s == "mirror_universe") return HostileType::MirrorUniverse;
    if (s == "freebooter")      return HostileType::Freebooter;
    if (s == "actian")          return HostileType::Actian;
    if (s == "assimilated")     return HostileType::Assimilated;
    return HostileType::Generic;
}

const char* hostile_type_label(HostileType t) {
    switch (t) {
        case HostileType::Generic:        return "Generic Hostiles";
        case HostileType::Swarm:          return "Swarm";
        case HostileType::BorgProbe:      return "Borg Probes (Vi'Dar)";
        case HostileType::Eclipse:        return "Eclipse Hostiles";
        case HostileType::Gorn:           return "Gorn (Isolytic Only)";
        case HostileType::Xindi:          return "Xindi";
        case HostileType::Silent:         return "Silent Enemies";
        case HostileType::Species8472:    return "Species 8472 Bioships";
        case HostileType::Breen:          return "Breen";
        case HostileType::Hirogen:        return "Hirogen Elite";
        case HostileType::TexasClass:     return "Texas Class";
        case HostileType::Monaveen:       return "Monaveen";
        case HostileType::MirrorUniverse: return "Mirror Universe";
        case HostileType::Freebooter:     return "Freebooters";
        case HostileType::Actian:         return "Actian (Mantis)";
        case HostileType::Assimilated:    return "Assimilated Continuum";
    }
    return "Generic Hostiles";
}

const char* hostile_objective_str(HostileObjective o) {
    switch (o) {
        case HostileObjective::Balanced:   return "balanced";
        case HostileObjective::PunchUp:    return "punch_up";
        case HostileObjective::LootGrind:  return "loot_grind";
        case HostileObjective::HullLife:   return "hull_life";
        case HostileObjective::XPGrind:    return "xp_grind";
        case HostileObjective::RepGrind:   return "rep_grind";
        case HostileObjective::PartsGrind: return "parts_grind";
        case HostileObjective::Speed:      return "speed";
    }
    return "balanced";
}

HostileObjective hostile_objective_from_str(const std::string& s) {
    if (s == "punch_up")    return HostileObjective::PunchUp;
    if (s == "loot_grind")  return HostileObjective::LootGrind;
    if (s == "hull_life")   return HostileObjective::HullLife;
    if (s == "xp_grind")    return HostileObjective::XPGrind;
    if (s == "rep_grind")   return HostileObjective::RepGrind;
    if (s == "parts_grind") return HostileObjective::PartsGrind;
    if (s == "speed")       return HostileObjective::Speed;
    return HostileObjective::Balanced;
}

const char* hostile_objective_label(HostileObjective o) {
    switch (o) {
        case HostileObjective::Balanced:   return "Balanced (Default)";
        case HostileObjective::PunchUp:    return "Punch Up (Max Damage)";
        case HostileObjective::LootGrind:  return "Loot Grind (Max Loot/Hull)";
        case HostileObjective::HullLife:   return "Hull Life (Max Kills Before Repair)";
        case HostileObjective::XPGrind:    return "XP Grind (Ship XP)";
        case HostileObjective::RepGrind:   return "Rep Grind (Reputation)";
        case HostileObjective::PartsGrind: return "Parts Grind (Ship Parts)";
        case HostileObjective::Speed:      return "Speed (Fast Kills)";
    }
    return "Balanced (Default)";
}

// ---------------------------------------------------------------------------
// Armada type / objective string conversions
// ---------------------------------------------------------------------------

const char* armada_type_str(ArmadaType t) {
    switch (t) {
        case ArmadaType::Normal:  return "normal";
        case ArmadaType::Eclipse: return "eclipse";
        case ArmadaType::Swarm:   return "swarm";
        case ArmadaType::Borg:    return "borg";
    }
    return "normal";
}

ArmadaType armada_type_from_str(const std::string& s) {
    if (s == "eclipse") return ArmadaType::Eclipse;
    if (s == "swarm")   return ArmadaType::Swarm;
    if (s == "borg")    return ArmadaType::Borg;
    return ArmadaType::Normal;
}

const char* armada_type_label(ArmadaType t) {
    switch (t) {
        case ArmadaType::Normal:  return "Normal Armada";
        case ArmadaType::Eclipse: return "Eclipse Armada";
        case ArmadaType::Swarm:   return "Swarm Armada";
        case ArmadaType::Borg:    return "Borg Armada";
    }
    return "Normal Armada";
}

const char* armada_objective_str(ArmadaObjective o) {
    switch (o) {
        case ArmadaObjective::Balanced: return "balanced";
        case ArmadaObjective::MaxLoot:  return "max_loot";
        case ArmadaObjective::MaxPower: return "max_power";
        case ArmadaObjective::Support:  return "support";
    }
    return "balanced";
}

ArmadaObjective armada_objective_from_str(const std::string& s) {
    if (s == "max_loot")  return ArmadaObjective::MaxLoot;
    if (s == "max_power") return ArmadaObjective::MaxPower;
    if (s == "support")   return ArmadaObjective::Support;
    return ArmadaObjective::Balanced;
}

const char* armada_objective_label(ArmadaObjective o) {
    switch (o) {
        case ArmadaObjective::Balanced: return "Balanced (Default)";
        case ArmadaObjective::MaxLoot:  return "Max Loot (Directive ROI)";
        case ArmadaObjective::MaxPower: return "Max Power (Survivability + DPS)";
        case ArmadaObjective::Support:  return "Support (States for Alliance)";
    }
    return "Balanced (Default)";
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

// Mining value helpers removed — scoring simplified to stat-based only.
// These will be rebuilt when proper scoring system is implemented.

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
// Helpers for building ClassifiedOfficer from sync data
// (moved from main.cpp — these resolve game data into optimizer format)
// ---------------------------------------------------------------------------

static char rarity_letter(int rarity) {
    switch (rarity) {
        case 1: return 'C';
        case 2: return 'U';
        case 3: return 'R';
        case 4: return 'E';
        default: return ' ';
    }
}

static double ability_pct(const OfficerAbility& ability, int rank) {
    if (ability.values.empty()) return 0.0;
    int idx = std::max(0, std::min(rank, static_cast<int>(ability.values.size()) - 1));
    return ability.values[idx].value;
}

static std::string fmt_pct(double value) {
    std::ostringstream os;
    double pct = value * 100.0;
    if (std::abs(pct - std::round(pct)) < 0.0001) {
        os << static_cast<int>(std::round(pct)) << "%";
    } else {
        os << std::fixed << std::setprecision(1) << pct << "%";
    }
    return os.str();
}

static std::string replace_all_str(std::string text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

static std::string to_lower_str(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string collapse_whitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool prev_space = true;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prev_space) { out += ' '; prev_space = true; }
        } else {
            out += c;
            prev_space = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static std::string strip_color_tags(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            if (text.compare(i, 7, "<color=") == 0) {
                auto end = text.find('>', i);
                if (end != std::string::npos) { i = end + 1; continue; }
            } else if (text.compare(i, 8, "</color>") == 0) {
                i += 8; continue;
            }
        }
        out += text[i++];
    }
    return out;
}

static std::string resolve_officer_tooltip(const Officer& officer, int rank) {
    std::string text = officer.description;
    if (text.empty()) return text;

    const auto& cap = officer.captain_ability.values;
    const auto& abil = officer.ability.values;
    const auto& bda = officer.below_decks_ability.values;
    auto rank_idx = std::max(0, rank);

    auto cap_value = [&](int idx) {
        idx = std::max(0, std::min(idx, static_cast<int>(cap.size()) - 1));
        return cap.empty() ? 0.0 : cap[idx].value;
    };
    auto abil_value = [&](int idx) {
        idx = std::max(0, std::min(idx, static_cast<int>(abil.size()) - 1));
        return abil.empty() ? 0.0 : abil[idx].value;
    };
    auto bda_value = [&](int idx) {
        idx = std::max(0, std::min(idx, static_cast<int>(bda.size()) - 1));
        return bda.empty() ? 0.0 : bda[idx].value;
    };
    auto p0_value = [&](int idx) {
        return officer.has_bda ? bda_value(idx) : cap_value(idx);
    };

    auto fmt_num = [](double value) -> std::string {
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
    };

    // Percentage format patterns
    for (const char* pat : {"{0:#,#%}", "{0:#.#%}", "{0:0,#%}", "{0:0.#%}", "{0:#%}"})
        text = replace_all_str(text, pat, fmt_pct(p0_value(rank_idx)));
    for (const char* pat : {"{1:#,#%}", "{1:#.#%}", "{1:0,#%}", "{1:0.#%}", "{1:#%}"})
        text = replace_all_str(text, pat, fmt_pct(p0_value(std::min(rank_idx + 1, std::max(0, (int)cap.size() - 1)))));
    for (const char* pat : {"{2:#,#%}", "{2:#.#%}", "{2:0,#%}", "{2:0.#%}", "{2:#%}"})
        text = replace_all_str(text, pat, fmt_pct(abil_value(rank_idx)));
    for (const char* pat : {"{3:#,#%}", "{3:#.#%}", "{3:0,#%}", "{3:0.#%}", "{3:#%}"})
        text = replace_all_str(text, pat, fmt_pct(abil_value(rank_idx)));
    for (const char* pat : {"{4:#,#%}", "{4:#.#%}", "{4:0,#%}", "{4:0.#%}", "{4:#%}"})
        text = replace_all_str(text, pat, fmt_pct(abil_value(rank_idx)));

    // Non-percentage format patterns
    for (const char* pat : {"{0:#,#}", "{0:#}", "{0:0.##}", "{0:0.#}", "{0:0}"})
        text = replace_all_str(text, pat, fmt_num(p0_value(rank_idx)));
    for (const char* pat : {"{2:#,#}", "{2:#}", "{2:0.##}", "{2:0.#}"})
        text = replace_all_str(text, pat, fmt_num(abil_value(rank_idx)));
    for (const char* pat : {"{3:#,#}", "{3:#}", "{3:0.##}", "{3:0.#}"})
        text = replace_all_str(text, pat, fmt_num(abil_value(rank_idx)));
    for (const char* pat : {"{4:#,#}", "{4:#}", "{4:0.##}", "{4:0.#}"})
        text = replace_all_str(text, pat, fmt_num(abil_value(rank_idx)));

    return text;
}

static std::string build_optimizer_description(const Officer& officer, int rank) {
    std::string tooltip = resolve_officer_tooltip(officer, rank);
    tooltip = strip_color_tags(tooltip);

    std::string block0, block1;
    auto sep = tooltip.find("\n\n");
    if (sep != std::string::npos) {
        block0 = tooltip.substr(0, sep);
        block1 = tooltip.substr(sep + 2);
        auto sep2 = block1.find("\n\n");
        if (sep2 != std::string::npos) block1 = block1.substr(0, sep2);
    } else {
        block0 = tooltip;
    }

    block0 = collapse_whitespace(block0);
    block1 = collapse_whitespace(block1);

    std::string desc;
    if (officer.has_bda) {
        desc = "bda: " + block0 + " oa: " + block1;
    } else {
        desc = "cm: " + block0 + " oa: " + block1;
    }
    return to_lower_str(desc);
}

static void parse_status_effects(const std::string& desc, std::string& effect,
                                 bool& causes_effect) {
    effect.clear();
    causes_effect = false;

    static const char* morale_apply[] = {
        "inspire morale", "morale for", "apply morale", "cause morale", nullptr
    };
    static const char* breach_apply[] = {
        "hull breach for", "apply hull breach", "cause hull breach",
        "inflict hull breach", nullptr
    };
    static const char* burning_apply[] = {
        "burning for", "apply burning", "cause burning",
        "inflict burning", "burning to opponent", "burning to the opponent", nullptr
    };
    static const char* assimilate_apply[] = {
        "assimilate for", "apply assimilate", nullptr
    };

    static const char* morale_benefit[] = {
        "ship has morale", "with morale", "has morale",
        "when morale", "while morale", nullptr
    };
    static const char* breach_benefit[] = {
        "has hull breach", "with hull breach", "opponent hull breach",
        "when hull breach", nullptr
    };
    static const char* burning_benefit[] = {
        "is burning", "has burning", "opponent burning",
        "afflicted by burning", "when burning", "whilst burning", nullptr
    };
    static const char* assimilate_benefit[] = {
        "with assimilate", "has assimilate", "when assimilate",
        "is assimilated", nullptr
    };

    auto check_keywords = [&](const char* state, const char* const* apply_kw,
                               const char* const* benefit_kw) {
        for (const char* const* p = apply_kw; *p; ++p) {
            if (desc.find(*p) != std::string::npos) {
                effect = state;
                causes_effect = true;
                return true;
            }
        }
        for (const char* const* p = benefit_kw; *p; ++p) {
            if (desc.find(*p) != std::string::npos) {
                effect = state;
                causes_effect = false;
                return true;
            }
        }
        return false;
    };

    if (check_keywords("morale", morale_apply, morale_benefit)) return;
    if (check_keywords("breach", breach_apply, breach_benefit)) return;
    if (check_keywords("burning", burning_apply, burning_benefit)) return;
    if (check_keywords("assimilate", assimilate_apply, assimilate_benefit)) return;
}

// ---------------------------------------------------------------------------
// Constructor — builds ClassifiedOfficers directly from sync + game data
// ---------------------------------------------------------------------------

CrewOptimizer::CrewOptimizer(const PlayerData& player_data,
                             const GameData& game_data,
                             WeaknessProfile weakness)
    : weakness_(weakness) {
    officers_.reserve(player_data.officers.size());

    for (const auto& po : player_data.officers) {
        if (po.level <= 0) continue;

        auto it = game_data.officers.find(po.officer_id);
        if (it == game_data.officers.end()) continue;

        const auto& go = it->second;
        ClassifiedOfficer o;
        o.officer_id = po.officer_id;
        o.name = po.name.empty() ? (go.name.empty() ? go.short_name : go.name) : po.name;
        o.rarity = rarity_letter(go.rarity);
        o.officer_class = go.officer_class;
        o.level = po.level;
        o.rank = po.rank;

        // Resolve stats from game data at the player's level
        if (!go.stats.empty() && po.level > 0) {
            int idx = std::min(po.level - 1, static_cast<int>(go.stats.size()) - 1);
            idx = std::max(0, idx);
            o.attack = go.stats[idx].attack;
            o.defense = go.stats[idx].defense;
            o.health = go.stats[idx].health;
        } else {
            o.attack = po.attack;
            o.defense = po.defense;
            o.health = po.health;
        }
        o.group = go.group_name;

        // CM/BDA percentage resolution
        if (go.has_bda) {
            double bda_raw = ability_pct(go.below_decks_ability, 0);
            if (go.below_decks_ability.value_is_percentage) {
                o.cm_pct = bda_raw * 100.0;
            } else {
                o.cm_pct = bda_raw;
            }
        } else {
            double cm_raw = ability_pct(go.captain_ability, 0);
            o.cm_pct = go.captain_ability.value_is_percentage ? cm_raw * 100.0 : cm_raw;
        }

        double oa_raw = ability_pct(go.ability, po.rank);
        o.oa_pct = go.ability.value_is_percentage ? oa_raw * 100.0 : oa_raw;

        // Build optimizer-compatible description and parse status effects
        o.description = build_optimizer_description(go, po.rank);
        o.cm_text = extract_section(o.description, "cm:");
        o.bda_text = extract_section(o.description, "bda:");
        o.oa_text = extract_section(o.description, "oa:");
        parse_status_effects(o.description, o.effect, o.causes_effect);

        // Structured ability data for numeric scoring
        o.oa_is_pct = go.ability.value_is_percentage;
        for (const auto& av : go.ability.values) {
            o.oa_values.push_back(av.value);
        }
        if (!go.captain_ability.values.empty()) {
            o.cm_value = go.captain_ability.values[0].value;
        }
        if (go.has_bda) {
            for (const auto& av : go.below_decks_ability.values) {
                o.bda_values.push_back(av.value);
            }
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
        // API-sourced values take priority when present;
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

            // OA description from bootstrap — fallback when CSV oa_text is empty
            if (off.oa_text.empty() && sj.contains("officer_ability") && sj["officer_ability"].is_string()) {
                off.oa_text = sj["officer_ability"].get<std::string>();
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
// ---------------------------------------------------------------------------
// Simplified scoring — stat-based only (no magic number bonuses)
// ---------------------------------------------------------------------------
// All internal scoring logic has been stripped. Officers are ranked purely by
// their stats (attack + defense + health) and bridge synergy (real game
// mechanic). A proper scoring system will be built later using community data.
// ---------------------------------------------------------------------------

} // namespace stfc
