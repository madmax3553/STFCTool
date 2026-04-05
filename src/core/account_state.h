#pragma once

#include <string>
#include <vector>
#include <set>

#include "data/models.h"
#include "core/crew_optimizer.h"

namespace stfc {

// ---------------------------------------------------------------------------
// Account state snapshot — everything the AI needs to know about the player
//
// This is the bridge between the app's internal data structures and the
// compact JSON representation injected into LLM prompts. The design goals:
//
//   1. Pre-filter officers by scenario relevance (~30-50 candidates, not 200+)
//      to minimize token usage and keep responses focused
//   2. Include contextual data the AI needs: ships, tech, ops level
//   3. Compact JSON format — short keys, no redundant data
//   4. Scenario-aware: different scenarios need different officer subsets
// ---------------------------------------------------------------------------

// Compact officer representation for LLM consumption
struct CompactOfficer {
    std::string name;
    std::string officer_class;     // "Command", "Science", "Engineering"
    std::string rarity;            // "Common", "Uncommon", "Rare", "Epic"
    int level = 0;
    int rank = 0;
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;
    std::string group;             // synergy group

    // Abilities (human-readable text — the AI reads these)
    std::string captain_maneuver;  // CM description
    std::string officer_ability;   // OA description
    std::string below_decks;       // BDA description (empty if not BDA officer)
    double cm_pct = 0.0;           // CM percentage
    double oa_pct = 0.0;           // OA percentage

    // Key tags (subset — only the ones relevant for AI reasoning)
    std::vector<std::string> tags; // e.g. ["pvp", "crit", "burning", "armor_piercing"]
};

// Compact ship representation
struct CompactShip {
    std::string name;
    std::string hull_type;         // "Interceptor", "Survey", "Explorer", "Battleship"
    int tier = 0;
    int level = 0;
};

// Compact tech entry
struct CompactTech {
    int64_t tech_id = 0;
    int tier = 0;
    int level = 0;
};

// ---------------------------------------------------------------------------
// AccountSnapshot — the full payload for AI prompts
// ---------------------------------------------------------------------------

struct AccountSnapshot {
    // Player context
    std::string player_name;
    int ops_level = 0;

    // Pre-filtered officers (scenario-relevant subset)
    std::vector<CompactOfficer> officers;

    // Player's ships (all — ships are few enough to include fully)
    std::vector<CompactShip> ships;

    // Forbidden tech state (crucial for META positioning)
    std::vector<CompactTech> forbidden_tech;

    // Summary stats (helps AI calibrate advice)
    int total_officers_owned = 0;   // full count before filtering
    int total_ships_owned = 0;

    // The scenario and constraints this snapshot was built for
    std::string scenario;           // e.g. "PvP", "PvEHostile", etc.
    std::string ship_type;          // current ship type context
    std::set<std::string> excluded; // officers already assigned (multi-dock)
};

// ---------------------------------------------------------------------------
// Build an AccountSnapshot for a given scenario
//
// This is the main entry point. It:
//   1. Scores all officers for scenario relevance (lightweight — no full crew scoring)
//   2. Takes the top_n most relevant (default 40)
//   3. Packages them with ship/tech/research context
// ---------------------------------------------------------------------------

AccountSnapshot build_account_snapshot(
    const PlayerData& player_data,
    const GameData& game_data,
    const std::vector<ClassifiedOfficer>& classified_officers,
    Scenario scenario,
    ShipType ship_type = ShipType::Explorer,
    int top_n = 20,
    const std::set<std::string>& excluded = {});

// ---------------------------------------------------------------------------
// Snapshot JSON serialization options — different AI modes need different data
// ---------------------------------------------------------------------------

struct SnapshotJsonOptions {
    bool include_ships = false;      // Top ships by tier (for Progression)
    bool include_tech = false;       // Forbidden tech state (for Progression)
    bool include_stats = false;      // Officer attack/defense/health
    bool verbose_officers = false;   // Include level, class, rarity per officer
    int max_ships = 10;              // Cap ships included (top N by tier)
    int ability_truncate = 80;       // Max chars for ability text

    // Preset: minimal (Crew Recs, META)
    static SnapshotJsonOptions minimal() { return {}; }

    // Preset: full (Progression — needs ships, tech, verbose officers)
    static SnapshotJsonOptions full() {
        SnapshotJsonOptions o;
        o.include_ships = true;
        o.include_tech = true;
        o.verbose_officers = true;
        o.max_ships = 10;
        o.ability_truncate = 80;
        return o;
    }

    // Preset: roster overview (Ask — moderate detail)
    static SnapshotJsonOptions overview() {
        SnapshotJsonOptions o;
        o.include_ships = true;
        o.verbose_officers = true;
        o.max_ships = 5;
        o.ability_truncate = 60;
        return o;
    }
};

// ---------------------------------------------------------------------------
// Serialize to compact JSON string (for LLM prompt injection)
//
// Format is designed for minimal tokens:
//   - Short object keys
//   - No whitespace/indentation
//   - Numbers not quoted
//   - Tags as flat arrays
// ---------------------------------------------------------------------------

std::string snapshot_to_json(const AccountSnapshot& snap);
std::string snapshot_to_json(const AccountSnapshot& snap, const SnapshotJsonOptions& opts);

// ---------------------------------------------------------------------------
// Serialize to human-readable summary (for debugging / display)
// ---------------------------------------------------------------------------

std::string snapshot_to_summary(const AccountSnapshot& snap);

} // namespace stfc
