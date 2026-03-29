#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <optional>

#include "util/csv_import.h"

namespace stfc {

// ---------------------------------------------------------------------------
// Scenario definitions
// ---------------------------------------------------------------------------

enum class Scenario {
    PvP, Hybrid, BaseCracker, PvEHostile, MissionBoss,
    Loot, Armada,
    MiningSpeed, MiningProtected, MiningCrystal, MiningGas,
    MiningOre, MiningGeneral
};

const char* scenario_str(Scenario s);
const char* scenario_label(Scenario s);
Scenario scenario_from_str(const std::string& s);

inline const std::vector<Scenario>& all_scenarios() {
    static const std::vector<Scenario> v = {
        Scenario::PvP, Scenario::Hybrid, Scenario::BaseCracker,
        Scenario::PvEHostile, Scenario::MissionBoss, Scenario::Loot,
        Scenario::Armada
    };
    return v;
}

inline const std::vector<Scenario>& mining_scenarios() {
    static const std::vector<Scenario> v = {
        Scenario::MiningSpeed, Scenario::MiningProtected, Scenario::MiningCrystal,
        Scenario::MiningGas, Scenario::MiningOre, Scenario::MiningGeneral
    };
    return v;
}

inline const std::vector<Scenario>& all_dock_scenarios() {
    static const std::vector<Scenario> v = {
        Scenario::PvP, Scenario::Hybrid, Scenario::BaseCracker,
        Scenario::PvEHostile, Scenario::MissionBoss, Scenario::Loot,
        Scenario::Armada,
        Scenario::MiningSpeed, Scenario::MiningProtected, Scenario::MiningCrystal,
        Scenario::MiningGas, Scenario::MiningOre, Scenario::MiningGeneral
    };
    return v;
}

// ---------------------------------------------------------------------------
// Ship types
// ---------------------------------------------------------------------------

enum class ShipType { Explorer, Battleship, Interceptor };

const char* ship_type_str(ShipType s);
ShipType ship_type_from_str(const std::string& s);

struct ShipRecommendation {
    ShipType best;
    std::string reason;
};
const ShipRecommendation& get_ship_recommendation(Scenario s);

// ---------------------------------------------------------------------------
// Classified officer — RosterOfficer + all computed tags
// ---------------------------------------------------------------------------

struct ClassifiedOfficer {
    // Base data (from CSV)
    std::string name;
    char rarity = ' ';
    int level = 0;
    int rank = 0;
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;
    std::string group;
    double cm_pct = 0.0;
    double oa_pct = 0.0;
    std::string effect;
    bool causes_effect = false;
    bool player_uses = false;
    std::string description;

    // Classification tags (set by classify())
    std::set<std::string> pvp_tags;
    std::set<std::string> states_applied;
    std::set<std::string> states_benefit;

    bool is_ship_specific = false;
    bool is_pvp_specific = false;
    bool is_pve_specific = false;
    bool is_dual_use = false;
    bool crit_related = false;
    bool shield_related = false;
    bool shots_related = false;
    bool mitigation_related = false;
    bool isolytic_related = false;
    bool weapon_delay = false;
    bool ability_amplifier = false;
    bool stat_booster = false;

    // Scenario tags
    bool base_attack = false;
    bool base_defend = false;
    bool pve_hostile = false;
    bool mission_boss = false;
    bool mining = false;
    bool cargo = false;
    bool loot = false;
    bool warp = false;
    bool armada = false;
    bool armada_solo = false;
    bool non_armada_only = false;
    bool repair = false;
    bool apex = false;

    // Mining subcategory tags
    bool mining_crystal = false;
    bool mining_gas = false;
    bool mining_ore = false;
    bool mining_speed = false;
    bool protected_cargo = false;
    bool node_defense = false;

    // Helpers
    bool is_bda() const {
        if (cm_pct >= 10000.0) return true;
        if (description.size() >= 4 &&
            description[0] == 'b' && description[1] == 'd' &&
            description[2] == 'a' && description[3] == ':') return true;
        return false;
    }
};

// ---------------------------------------------------------------------------
// Weakness profile (from battle log analysis — zeroes if no logs)
// ---------------------------------------------------------------------------

struct WeaknessProfile {
    double crit_damage_gap = 0.0;
    double crit_hit_disadvantage = 0.0;
    double shield_timing_loss = 0.0;
    double stat_paradox = 0.0;
    double state_vulnerability = 0.0;
    double damage_escalation = 0.0;
    int losses = 0;
    int total_battles = 0;
};

// ---------------------------------------------------------------------------
// Crew result (output of optimization)
// ---------------------------------------------------------------------------

struct CrewBreakdown {
    std::string captain;
    std::vector<std::string> bridge;
    std::map<std::string, double> individual_scores;
    double synergy_bonus = 0.0;
    double state_chain_bonus = 0.0;
    double crit_bonus = 0.0;
    double ship_type_bonus = 0.0;
    double scenario_bonus = 0.0;
    double weakness_counter_bonus = 0.0;
    double dual_use_bonus = 0.0;
    double amplifier_bonus = 0.0;
    std::vector<std::string> penalties;
    std::vector<std::string> synergy_notes;
};

struct CrewResult {
    double score = 0.0;
    CrewBreakdown breakdown;
};

// ---------------------------------------------------------------------------
// CrewOptimizer — the main engine
// ---------------------------------------------------------------------------

class CrewOptimizer {
public:
    // Construct with roster officers and optional weakness profile
    CrewOptimizer(std::vector<RosterOfficer> roster,
                  WeaknessProfile weakness = {});

    // Set ship type and reclassify all officers
    void set_ship_type(ShipType ship);
    ShipType ship_type() const { return ship_type_; }

    // Find best crews for a scenario
    std::vector<CrewResult> find_best_crews(
        Scenario scenario, int top_n = 5,
        const std::set<std::string>& excluded = {}) const;

    // Access classified officers
    const std::vector<ClassifiedOfficer>& officers() const { return officers_; }

private:
    void classify_officers();

    // Ship-lock helpers
    bool cm_works_on_ship(const ClassifiedOfficer& off) const;
    bool oa_works_on_ship(const ClassifiedOfficer& off) const;

    // Individual scoring
    double score_pvp_individual(const ClassifiedOfficer& off) const;
    double score_hybrid_individual(const ClassifiedOfficer& off) const;
    double score_scenario_individual(const ClassifiedOfficer& off, Scenario s) const;

    // Crew scoring
    CrewResult score_pvp_crew(const ClassifiedOfficer& captain,
                              const ClassifiedOfficer& b1,
                              const ClassifiedOfficer& b2) const;
    CrewResult score_hybrid_crew(const ClassifiedOfficer& captain,
                                 const ClassifiedOfficer& b1,
                                 const ClassifiedOfficer& b2) const;
    CrewResult score_scenario_crew(const ClassifiedOfficer& captain,
                                   const ClassifiedOfficer& b1,
                                   const ClassifiedOfficer& b2,
                                   Scenario s) const;

    // Shared crew-level logic
    void apply_state_chain(double& total, CrewBreakdown& bd,
                           const ClassifiedOfficer* crew[3],
                           double chain_per_state, bool penalize_none) const;
    void apply_coherence(double& total, CrewBreakdown& bd,
                         const ClassifiedOfficer* crew[3],
                         double bonus3, double bonus2) const;
    void apply_weakness_counters_pvp(double& total, CrewBreakdown& bd,
                                     const ClassifiedOfficer* crew[3],
                                     double crit_mult, double shield_mult) const;
    void apply_oa_ship_penalty(double& total, CrewBreakdown& bd,
                               const ClassifiedOfficer& b1,
                               const ClassifiedOfficer& b2) const;

    std::vector<ClassifiedOfficer> officers_;
    ShipType ship_type_ = ShipType::Explorer;
    WeaknessProfile weakness_;

    // Ship type other-types for penalty detection
    std::vector<std::string> other_ship_types() const;
};

} // namespace stfc
