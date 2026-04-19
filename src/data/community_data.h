#pragma once

// ---------------------------------------------------------------------------
// Community data loader — reads StewieDoo Officer Tool JSON exports
// ---------------------------------------------------------------------------

#include <string>
#include <fstream>
#include <filesystem>

#include "data/models.h"
#include "json.hpp"

namespace stfc {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Load officer_scores.json
inline bool load_officer_scores(const std::string& path, CommunityData& cd) {
    if (!fs::exists(path)) return false;
    std::ifstream f(path);
    if (!f) return false;
    json j;
    try { j = json::parse(f); } catch (...) { return false; }

    cd.version = j.value("version", "");

    if (!j.contains("officers") || !j["officers"].is_array()) return false;
    for (auto& o : j["officers"]) {
        CommunityOfficerScore score;
        score.name = o.value("name", "");
        score.rarity = o.value("rarity", "");
        score.notes = o.value("stewiedoo_notes", o.value("notes", ""));
        score.immediate_value = o.value("immediate_value", 0.0);
        score.upgrade_value = o.value("upgrade_value", 0.0);
        score.overall_score = o.value("overall_score", 0.0);
        if (o.contains("scores") && o["scores"].is_object()) {
            for (auto& [k, v] : o["scores"].items()) {
                if (v.is_number()) score.scores[k] = v.get<double>();
            }
        }
        cd.officer_scores.push_back(std::move(score));
    }
    return true;
}

// Load officer_skills.json
inline bool load_officer_skills(const std::string& path, CommunityData& cd) {
    if (!fs::exists(path)) return false;
    std::ifstream f(path);
    if (!f) return false;
    json j;
    try { j = json::parse(f); } catch (...) { return false; }

    if (!j.contains("officers") || !j["officers"].is_array()) return false;
    for (auto& o : j["officers"]) {
        CommunityOfficerSkill skill;
        skill.name = o.value("name", "");
        skill.alternate_name = o.value("alternate_name", "");
        skill.officer_ability = o.value("officer_ability", "");
        if (o.contains("oa_values") && o["oa_values"].is_array()) {
            for (auto& v : o["oa_values"]) {
                skill.oa_values.push_back(v.is_number() ? v.get<double>() : 0.0);
            }
        }
        skill.captain_maneuver = o.value("captain_maneuver", "");
        skill.cm_value = o.value("cm_value", 0.0);
        skill.officer_group = o.value("officer_group", "");
        skill.officer_type = o.value("officer_type", "");
        skill.synergy_full = o.value("synergy_full", 0.0);
        skill.synergy_half = o.value("synergy_half", 0.0);
        skill.is_bda = o.value("is_bda", false);

        if (o.contains("states") && o["states"].is_object()) {
            auto& s = o["states"];
            skill.cause_burning = s.value("cause_burning", false);
            skill.use_burning = s.value("use_burning", false);
            skill.cause_breach = s.value("cause_breach", false);
            skill.use_breach = s.value("use_breach", false);
            skill.cause_morale = s.value("cause_morale", false);
            skill.use_morale = s.value("use_morale", false);
            skill.cause_assimilate = s.value("cause_assimilate", false);
            skill.use_assimilate = s.value("use_assimilate", false);
        }
        if (o.contains("stat_boosts") && o["stat_boosts"].is_object()) {
            auto& b = o["stat_boosts"];
            skill.oa_attack_bridge = b.value("attack_bridge", false);
            skill.oa_attack_all = b.value("attack_all", false);
            skill.oa_defence_bridge = b.value("defence_bridge", false);
            skill.oa_defence_all = b.value("defence_all", false);
            skill.oa_health_bridge = b.value("health_bridge", false);
            skill.oa_health_all = b.value("health_all", false);
        }
        if (o.contains("cm_stat_boosts") && o["cm_stat_boosts"].is_object()) {
            auto& b = o["cm_stat_boosts"];
            skill.cm_attack_bridge = b.value("attack_bridge", false);
            skill.cm_attack_all = b.value("attack_all", false);
            skill.cm_defence_bridge = b.value("defence_bridge", false);
            skill.cm_defence_all = b.value("defence_all", false);
            skill.cm_health_bridge = b.value("health_bridge", false);
            skill.cm_health_all = b.value("health_all", false);
        }
        cd.officer_skills.push_back(std::move(skill));
    }
    return true;
}

// Load preset_crews.json
inline bool load_preset_crews(const std::string& path, CommunityData& cd) {
    if (!fs::exists(path)) return false;
    std::ifstream f(path);
    if (!f) return false;
    json j;
    try { j = json::parse(f); } catch (...) { return false; }

    if (!j.contains("crews") || !j["crews"].is_array()) return false;
    for (auto& c : j["crews"]) {
        PresetCrew crew;
        crew.name = c.value("name", "");
        crew.captain = c.value("captain", "");
        crew.captain_rank = c.value("captain_rank", 0.0);
        crew.officer1 = c.value("officer1", "");
        crew.officer1_rank = c.value("officer1_rank", 0.0);
        crew.officer2 = c.value("officer2", "");
        crew.officer2_rank = c.value("officer2_rank", 0.0);
        crew.notes = c.value("notes", "");

        if (c.contains("scenarios") && c["scenarios"].is_object()) {
            auto& s = c["scenarios"];
            crew.pvp = s.value("pvp", false);
            crew.hostiles = s.value("hostiles", false);
            crew.mining = s.value("mining", false);
            crew.bases = s.value("bases", false);
            crew.mission_boss = s.value("mission_boss", false);
            crew.swarms = s.value("swarms", false);
            crew.eclipse = s.value("eclipse", false);
            crew.probes = s.value("probes", false);
            crew.xp_grinding = s.value("xp_grinding", false);
            crew.armada_normal = s.value("armada_normal", false);
            crew.armada_eclipse = s.value("armada_eclipse", false);
            crew.armada_swarm = s.value("armada_swarm", false);
            crew.armada_borg = s.value("armada_borg", false);
            crew.for_explorer = s.value("for_explorer", false);
            crew.for_interceptor = s.value("for_interceptor", false);
            crew.for_battleship = s.value("for_battleship", false);
            crew.vs_explorer = s.value("vs_explorer", false);
            crew.vs_interceptor = s.value("vs_interceptor", false);
            crew.vs_battleship = s.value("vs_battleship", false);
            crew.vs_survey = s.value("vs_survey", false);
        }
        cd.preset_crews.push_back(std::move(crew));
    }
    return true;
}

// Load all community data and build lookup indices
inline bool load_community_data(CommunityData& cd,
                                const std::string& base_dir = "data") {
    bool ok = false;
    ok |= load_officer_scores(base_dir + "/officer_scores.json", cd);
    ok |= load_officer_skills(base_dir + "/officer_skills.json", cd);
    ok |= load_preset_crews(base_dir + "/preset_crews.json", cd);

    // Build name -> pointer lookups
    for (const auto& s : cd.officer_scores) {
        cd.score_by_name[s.name] = &s;
    }
    for (const auto& s : cd.officer_skills) {
        cd.skill_by_name[s.name] = &s;
    }

    return ok;
}

} // namespace stfc
