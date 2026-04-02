#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <optional>
#include <regex>
#include <ostream>

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

enum class ShipType { Explorer, Battleship, Interceptor, Survey };
enum class MiningResource { None, General, Gas, Ore, Crystal, Parsteel, Tritanium, Dilithium };
enum class MiningObjective { None, Speed, Protected, Balanced };
const char* mining_resource_str(MiningResource r);
const char* mining_objective_str(MiningObjective o);
MiningResource scenario_mining_resource(Scenario s);
MiningObjective scenario_mining_objective(Scenario s);

// ---------------------------------------------------------------------------
// Hostile sub-types — what kind of hostile are we fighting?
// ---------------------------------------------------------------------------

enum class HostileType {
    Generic,      // General hostiles (no specific type)
    Swarm,        // Swarm hostiles (Franklin-A, never use Chen)
    BorgProbe,    // Borg probes (Vi'Dar, cargo + loot focus)
    Eclipse,      // Eclipse hostiles
    Gorn,         // Gorn (only affected by isolytic damage)
    Xindi,        // Xindi (loot + isolytic)
    Silent,       // Silent Enemies (crit damage reduction, burning interaction)
    Species8472,  // Species 8472 Bioships (piercing focus)
    Breen,        // Breen hostiles
    Hirogen,      // Hirogen Elite hostiles
    TexasClass,   // Texas Class
    Monaveen,     // Monaveen
    MirrorUniverse, // Mirror Universe
    Freebooter,   // Freebooters (punch up crew)
    Actian,       // Actian hostiles (mantis)
    Assimilated,  // Assimilated Continuum
};

const char* hostile_type_str(HostileType t);
HostileType hostile_type_from_str(const std::string& s);

inline const std::vector<HostileType>& all_hostile_types() {
    static const std::vector<HostileType> v = {
        HostileType::Generic, HostileType::Swarm, HostileType::BorgProbe,
        HostileType::Eclipse, HostileType::Gorn, HostileType::Xindi,
        HostileType::Silent, HostileType::Species8472, HostileType::Breen,
        HostileType::Hirogen, HostileType::TexasClass, HostileType::Monaveen,
        HostileType::MirrorUniverse, HostileType::Freebooter, HostileType::Actian,
        HostileType::Assimilated
    };
    return v;
}

// ---------------------------------------------------------------------------
// Hostile objectives — what are we optimizing for?
// ---------------------------------------------------------------------------

enum class HostileObjective {
    Balanced,     // Default: good mix of damage + survivability
    PunchUp,      // Max damage to kill above your level
    LootGrind,    // Maximize loot per hull
    HullLife,     // Maximize kills before hull death (sustained grinding)
    XPGrind,      // Ship XP efficiency
    RepGrind,     // Reputation gain maximization
    PartsGrind,   // Ship parts farming (within triangle)
    Speed,        // Fast kills (wave defense, impulse speed)
};

const char* hostile_objective_str(HostileObjective o);
HostileObjective hostile_objective_from_str(const std::string& s);

// ---------------------------------------------------------------------------
// Armada sub-types — what kind of armada are we running?
// ---------------------------------------------------------------------------

enum class ArmadaType {
    Normal,       // Standard armadas
    Eclipse,      // Eclipse armadas
    Swarm,        // Swarm armadas
    Borg,         // Borg armadas
};

const char* armada_type_str(ArmadaType t);
ArmadaType armada_type_from_str(const std::string& s);

inline const std::vector<ArmadaType>& all_armada_types() {
    static const std::vector<ArmadaType> v = {
        ArmadaType::Normal, ArmadaType::Eclipse,
        ArmadaType::Swarm, ArmadaType::Borg
    };
    return v;
}

// ---------------------------------------------------------------------------
// Armada objectives — what are we optimizing for?
// ---------------------------------------------------------------------------

enum class ArmadaObjective {
    Balanced,     // Default: good mix of loot + power
    MaxLoot,      // Maximize loot/directive ROI
    MaxPower,     // Maximize survivability + damage output
    Support,      // Apply states (breach/burning) for alliance benefit
};

const char* armada_objective_str(ArmadaObjective o);
ArmadaObjective armada_objective_from_str(const std::string& s);

const char* ship_type_str(ShipType s);
ShipType ship_type_from_str(const std::string& s);

struct ShipRecommendation {
    ShipType best;
    std::string reason;
};
const ShipRecommendation& get_ship_recommendation(Scenario s);

// ---------------------------------------------------------------------------
// CM (Captain's Maneuver) scope classification — determines how powerful
// the ability is based on what it affects, not just its percentage.
// ---------------------------------------------------------------------------

enum class CmScope {
    Unknown,         // Unrecognized CM text
    AllStats,        // "increases ALL officer stats by X%" — strongest (Kirk, etc.)
    AbilityAmp,      // "effectiveness of all officer abilities" — force multiplier
    WeaponDamage,    // "weapon damage by X%" — moderate single-dimension
    CritDamage,      // "critical hit damage by X%" — conditional burst
    SingleStat,      // "attack/defense/health by X%" — narrow boost
    ShieldHp,        // "shield HP/SHP by X%" — defensive
    Mitigation,      // "armor/dodge/mitigation by X%" — defensive
    MiningEffect,    // "mining speed/cargo by X%" — non-combat
    Conditional,     // "when below X%..." — unreliable trigger
    Utility,         // warp, repair, loot — non-damage utility
    NonCombat,       // "cost efficiency/disco spend" — never useful in combat
};

// Weight multiplier per CM scope: applied as cm_pct * weight to get CM score
inline double cm_scope_weight(CmScope scope) {
    switch (scope) {
        case CmScope::AllStats:      return 120.0;  // 70% * 120 = 8400
        case CmScope::AbilityAmp:    return 100.0;  // 50% * 100 = 5000
        case CmScope::WeaponDamage:  return  40.0;  // 80% * 40  = 3200
        case CmScope::CritDamage:    return  35.0;  // 70% * 35  = 2450
        case CmScope::SingleStat:    return  25.0;  // 100% * 25 = 2500
        case CmScope::ShieldHp:      return  20.0;
        case CmScope::Mitigation:    return  20.0;
        case CmScope::MiningEffect:  return  15.0;  // Non-combat, valued in mining scenarios
        case CmScope::Conditional:   return  15.0;  // Unreliable
        case CmScope::Utility:       return  10.0;
        case CmScope::NonCombat:     return   2.0;  // Nearly worthless in combat
        case CmScope::Unknown:       return  15.0;
    }
    return 15.0;
}

// ---------------------------------------------------------------------------
// Classified officer — RosterOfficer + all computed tags
// ---------------------------------------------------------------------------

struct ClassifiedOfficer {
    // Base data (from CSV)
    std::string name;
    char rarity = ' ';
    int officer_class = 0;       // 1=Command, 2=Science, 3=Engineering
    int level = 0;
    int rank = 0;
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;
    std::string group;
    double cm_pct = 0.0;
    double oa_pct = 0.0;
    CmScope cm_scope = CmScope::Unknown;  // Classified CM type
    std::string effect;
    bool causes_effect = false;
    bool player_uses = false;
    std::string description;
    std::string cm_text;
    std::string bda_text;
    std::string oa_text;

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
    bool isolytic_related = false;   // DEPRECATED: use isolytic_cascade/isolytic_defense
    bool weapon_delay = false;
    bool ability_amplifier = false;
    bool stat_booster = false;

    // PvP 2025 META fields (Step 1-4 framework)
    // Step 1: Mitigation Delta
    bool armor_piercing = false;     // Provides armor piercing
    bool shield_piercing = false;    // Provides shield piercing
    bool accuracy_boost = false;     // Provides accuracy
    // Step 3: Proc Factor
    bool proc_guaranteed = false;    // State application is guaranteed (round start) vs chance-based
    // Step 4: Rock-Paper-Scissors META
    bool apex_barrier = false;       // Defensive: absorbs damage via apex barrier
    bool apex_shred = false;         // Offensive: strips/pierces apex barrier
    bool isolytic_cascade = false;   // Offensive: bypasses standard defense
    bool isolytic_defense = false;   // Defensive: reduces isolytic damage
    bool cumulative_stacking = false;// Effect stacks each round (Cumulative)

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

    // Hostile-type specificity tags (which hostile types this officer excels against)
    bool hostile_swarm = false;        // Effective against swarm hostiles
    bool hostile_borg = false;         // Effective against borg/probes
    bool hostile_eclipse = false;      // Effective against eclipse hostiles
    bool hostile_gorn = false;         // Effective against gorn (isolytic only)
    bool hostile_xindi = false;        // Effective against xindi
    bool hostile_silent = false;       // Effective against silent enemies
    bool hostile_8472 = false;         // Effective against species 8472
    bool hostile_breen = false;        // Effective against breen
    bool hostile_hirogen = false;      // Effective against hirogen
    bool hostile_texas = false;        // Effective against texas class
    bool hostile_monaveen = false;     // Effective against monaveen
    bool hostile_mirror = false;       // Effective in mirror universe
    bool hostile_freebooter = false;   // Effective against freebooters
    bool hostile_actian = false;       // Effective against actian/mantis
    bool hostile_assimilated = false;  // Effective against assimilated continuum

    // Armada-type specificity tags
    bool armada_eclipse = false;       // Effective in eclipse armadas
    bool armada_swarm = false;         // Effective in swarm armadas
    bool armada_borg = false;          // Effective in borg armadas

    // Hostile objective affinity tags
    bool loot_multiplier = false;      // Specifically increases loot (not just generic loot tag)
    bool rep_boost = false;            // Increases reputation gain
    bool xp_boost = false;            // Increases ship XP
    bool impulse_speed = false;        // Increases impulse/warp for speed runs
    bool hull_sustain = false;         // Keeps ship alive longer (repair, shield regen, mitigation)
    bool crit_damage_reduce = false;   // Reduces critical damage taken (key vs Silent Enemies)
    bool extra_shots = false;          // Increases number of weapon shots

    // Weapon-type dependency
    bool needs_kinetic = false;        // Officer ability requires kinetic weapon hit
    bool needs_energy = false;         // Officer ability requires energy weapon hit

    // StewieDoo context (from Officer Tool spreadsheet)
    std::string stewiedoo_notes;       // Human-written context about this officer
    double stewiedoo_score = 0.0;      // Overall 1-5 rating from Officer Tool

    // Mining subcategory tags
    bool mining_crystal = false;
    bool mining_gas = false;
    bool mining_ore = false;
    bool mining_speed = false;
    bool protected_cargo = false;
    bool node_defense = false;

    // Parsed mining effect strengths by seat
    double cm_mining_speed_pct = 0.0;
    double cm_mining_gas_pct = 0.0;
    double cm_mining_ore_pct = 0.0;
    double cm_mining_crystal_pct = 0.0;
    double cm_protected_cargo_pct = 0.0;
    double cm_cargo_pct = 0.0;
    double oa_mining_speed_pct = 0.0;
    double oa_mining_gas_pct = 0.0;
    double oa_mining_ore_pct = 0.0;
    double oa_mining_crystal_pct = 0.0;
    double oa_protected_cargo_pct = 0.0;
    double oa_cargo_pct = 0.0;

    // Parsed BDA mining effect strengths (from bda_text)
    double bda_mining_speed_pct = 0.0;
    double bda_mining_gas_pct = 0.0;
    double bda_mining_ore_pct = 0.0;
    double bda_mining_crystal_pct = 0.0;
    double bda_protected_cargo_pct = 0.0;
    double bda_cargo_pct = 0.0;

    // -----------------------------------------------------------------------
    // Numeric ability data (from API / bootstrap officer_skills.json)
    // These replace magic-number bonuses with actual calculated scores.
    // -----------------------------------------------------------------------

    // Officer Ability (OA) — rank-indexed numeric values
    double oa_value = 0.0;           // OA effect magnitude at player's rank (e.g. 0.80)
    double oa_chance = 0.0;          // OA proc probability at player's rank (0.0-1.0)
    std::vector<double> oa_values;   // All 5 rank values [rank1..rank5]
    bool oa_is_pct = false;          // Whether OA values are percentages

    // Captain's Maneuver (CM) — primary value
    double cm_value = 0.0;           // CM primary effect magnitude (e.g. 0.40 for Kirk)
    std::string cm_description;      // CM description text from structured data

    // Below Decks Ability (BDA) — for BDA officers
    double bda_value = 0.0;          // BDA effect magnitude at player's rank
    std::vector<double> bda_values;  // All 5 rank values [rank1..rank5]
    std::string bda_description;     // BDA description text from structured data

    // Synergy values (game-defined, not heuristic)
    double synergy_full = 0.0;       // Full synergy multiplier (e.g. 0.20 = 20%)
    double synergy_half = 0.0;       // Half synergy multiplier (e.g. 0.10 = 10%)

    // Officer type from structured data
    std::string officer_type_str;    // "Command", "Science", "Engineering"

    // Helpers
    bool is_bda() const {
        if (cm_pct >= 10000.0) return true;
        if (description.size() >= 4 &&
            description[0] == 'b' && description[1] == 'd' &&
            description[2] == 'a' && description[3] == ':') return true;
        return false;
    }

    double extracted_pct(const std::string& text) const {
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

    double bda_effect_pct() const { return extracted_pct(bda_text); }
    double oa_effect_pct() const { return extracted_pct(oa_text); }
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
    double raw_total = 0.0;
    double synergy_bonus = 0.0;           // state coherence bonus (morale/breach/etc.)
    double bridge_synergy_bonus = 0.0;    // game synergy group CM multiplier bonus (points)
    double bridge_synergy_pct = 0.0;      // total synergy percentage (0-40%)
    int bridge_synergy_bars_left = 0;     // 0, 1, or 2 bars for left side officer
    int bridge_synergy_bars_right = 0;    // 0, 1, or 2 bars for right side officer
    double state_chain_bonus = 0.0;
    double crit_bonus = 0.0;
    double ship_type_bonus = 0.0;
    double scenario_bonus = 0.0;
    double penalty_total = 0.0;
    double weakness_counter_bonus = 0.0;
    double dual_use_bonus = 0.0;
    double amplifier_bonus = 0.0;
    double oa_bonus = 0.0;                // OA% magnitude bonus from apply_oa_bonus()
    std::vector<std::string> penalties;
    std::vector<std::string> synergy_notes;
};

struct CrewResult {
    double score = 0.0;
    CrewBreakdown breakdown;
};

struct CandidateScore {
    double raw_individual = 0.0;
    double captain_slot = 0.0;
    double bridge_slot = 0.0;
    double synergy = 0.0;
    double scenario = 0.0;
    double penalties = 0.0;
    double ship = 0.0;
    double bda = 0.0;

    double total() const {
        return raw_individual + captain_slot + bridge_slot + synergy + scenario + ship + bda - penalties;
    }
};

// ---------------------------------------------------------------------------
// BDA suggestion (below-deck assignment)
// ---------------------------------------------------------------------------

struct BdaSuggestion {
    std::string name;
    int level = 0;
    int rank = 0;
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;
    double oa_pct = 0.0;
    std::string display;      // full ability description
    double score = 0.0;
    std::vector<std::string> reasons;

    // Detail fields for richer display
    bool is_dedicated_bda = false;   // true = designed as BDA officer
    std::string officer_type;        // "Command", "Science", "Engineering"
    std::string bda_text;            // BDA ability description
    std::string oa_text;             // OA ability description
    double bda_value = 0.0;          // BDA effect magnitude at rank
    double oa_value = 0.0;           // OA effect magnitude at rank

    // Scenario-relevant tags shown as icons/labels
    bool cargo = false;
    bool loot = false;
    bool mining = false;
    bool protected_cargo = false;
    bool crit_related = false;
    bool shield_related = false;
    bool mitigation_related = false;
    bool repair = false;
    bool armada = false;

    // Mining/cargo/loot effect strengths (if applicable)
    double oa_cargo_pct = 0.0;
    double oa_protected_cargo_pct = 0.0;
    double oa_mining_speed_pct = 0.0;
};

// ---------------------------------------------------------------------------
// Dock configuration and loadout result
// ---------------------------------------------------------------------------

struct DockConfig {
    Scenario scenario = Scenario::PvP;
    std::string ship_override;     // empty = use recommendation
    bool locked = false;
    std::string locked_captain;    // only when locked
    std::vector<std::string> locked_bridge;  // only when locked (2 names)
    int priority = 0;
    std::string purpose;
    MiningResource mining_resource = MiningResource::None;
    MiningObjective mining_objective = MiningObjective::None;
};

struct DockResult {
    int dock_num = 0;              // 1-based
    Scenario scenario = Scenario::PvP;
    std::string scenario_label_str;
    std::string ship_recommended;
    std::string ship_used;
    bool locked = false;
    int priority = 0;
    std::string purpose;
    MiningResource mining_resource = MiningResource::None;
    MiningObjective mining_objective = MiningObjective::None;

    // Crew
    std::string captain;
    std::vector<std::string> bridge;
    double score = 0.0;
    CrewBreakdown breakdown;

    // BDA suggestions
    std::vector<BdaSuggestion> bda_suggestions;
};

struct LoadoutResult {
    std::vector<DockResult> docks;
    std::vector<std::string> excluded_officers;  // sorted
    int total_officers_used = 0;
    std::vector<std::string> analysis_notes;
};

struct OwnedShipCandidate {
    std::string name;
    ShipType ship_type = ShipType::Explorer;
    int tier = 0;
    int level = 0;
    int grade = 0;
    int rarity = 0;
    std::string ability_tag;  // e.g. "mining_crystal", "combat_hostile", "loot"
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

    // Find best BDA (below-deck assignment) for a given crew
    std::vector<BdaSuggestion> find_best_bda(
        const std::string& captain_name,
        const std::vector<std::string>& bridge_names,
        Scenario mode = Scenario::PvP,
        int top_n = 5,
        const std::set<std::string>& excluded = {}) const;

    // Optimize a full 7-dock loadout (mutates ship_type temporarily, restores on return)
    LoadoutResult optimize_dock_loadout(
        const std::vector<DockConfig>& dock_configs,
        const std::vector<OwnedShipCandidate>& owned_ships = {},
        int top_n = 1);

    // Loadout persistence
    static bool save_loadout(const LoadoutResult& result,
                             const std::string& path,
                             ShipType ship = ShipType::Explorer,
                             const std::string& ship_display = "");
    static bool load_loadout(LoadoutResult& result,
                             const std::string& path);

    // Access classified officers
    const std::vector<ClassifiedOfficer>& officers() const { return officers_; }
    void dump_mining_debug(std::ostream& os) const;

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
    void apply_oa_bonus(double& total, CrewBreakdown& bd,
                        const ClassifiedOfficer* crew[3],
                        Scenario scenario = Scenario::PvP) const;
    void apply_bridge_synergy(double& total, CrewBreakdown& bd,
                              const ClassifiedOfficer* crew[3]) const;

    std::vector<ClassifiedOfficer> officers_;
    ShipType ship_type_ = ShipType::Explorer;
    WeaknessProfile weakness_;

    // Roster stat maxima for normalization (computed in classify_officers)
    double roster_max_attack_ = 1.0;
    double roster_max_defense_ = 1.0;
    double roster_max_health_ = 1.0;

    // Ship type other-types for penalty detection
    std::vector<std::string> other_ship_types() const;
};

} // namespace stfc
