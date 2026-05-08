#pragma once

#include <cstdint>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "data/models.h"

namespace stfc {

// ---------------------------------------------------------------------------
// AccountSnapshot — the complete, resolved picture of a player's account
//
// This is the single source of truth for all downstream processing:
//   Stage 2 (math/scoring), Stage 3 (AI sub-strategy), Stage 5 (planner)
//
// Design principles:
//   - Every record is RESOLVED: IDs cross-referenced, names populated,
//     abilities merged from game data, stats computed at the player's level.
//   - No downstream code should need to touch GameData or PlayerData directly.
//   - Built once per sync/refresh, consumed by all pipeline stages.
//
// This is NOT the same as the existing AccountSnapshot in account_state.h,
// which is a pre-filtered, compact view for AI prompts. That becomes a
// downstream consumer of this struct (Stage 2 → Stage 3 adapter).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Resolved officer: player state + game data merged
// ---------------------------------------------------------------------------

struct ResolvedAbility {
    std::string name;                 // translated ability name
    std::string description;          // translated, placeholder-resolved description
    bool is_percentage = false;       // true if values are percentages
    std::vector<double> values;       // values per rank (R1-R5)
    std::vector<double> chances;      // proc chances per rank
};

struct ResolvedOfficer {
    // Identity
    int64_t id = 0;
    std::string name;
    std::string short_name;
    int officer_class = 0;            // 1=command, 2=science, 3=engineering
    int rarity = 0;                   // 1-4
    int64_t faction = 0;
    std::string group;                // synergy group name
    int64_t synergy_id = 0;

    // Player state
    int level = 0;
    int rank = 0;
    int shard_count = 0;
    bool owned = false;               // true if player owns this officer

    // Stats at current level (resolved from game data level tables)
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;

    // Abilities (resolved with actual values at current rank)
    ResolvedAbility captain_maneuver;
    ResolvedAbility officer_ability;
    ResolvedAbility below_decks;      // empty if not a BDA officer
    bool has_bda = false;

    // Traits (for away team missions)
    std::vector<PlayerTrait> traits;

    // Raw description (full tooltip, resolved placeholders, color-stripped)
    std::string description;
};

// ---------------------------------------------------------------------------
// Resolved ship: player state + game data merged
// ---------------------------------------------------------------------------

struct ResolvedShip {
    // Identity
    int64_t hull_id = 0;
    int64_t ship_id = 0;              // player's instance ID
    std::string name;
    int hull_type = 0;                // 0=interceptor, 1=survey, 2=explorer, 3=battleship
    int rarity = 0;
    int grade = 0;
    int64_t faction = -1;

    // Player state
    int tier = 0;
    int level = 0;
    double level_percentage = 0.0;
    int max_tier = 0;
    int max_level = 0;
    std::vector<int64_t> components;  // installed component IDs

    // Ability
    std::string ability_name;
    std::string ability_description;

    // Stats at current level (from game data level tables)
    double shield = 0.0;
    double health = 0.0;
};

// ---------------------------------------------------------------------------
// Resolved research: player state + game data merged
// ---------------------------------------------------------------------------

struct ResolvedResearch {
    int64_t id = 0;
    std::string name;
    std::string description;
    int64_t research_tree = 0;
    int current_level = 0;
    int unlock_level = 0;             // ops level required to see this
    int view_level = 0;
    int generation = 0;
    int row = 0;
    int column = 0;
    bool doubler = false;
    std::vector<ResearchBuff> buffs;  // what this research provides
    std::vector<ResearchLevel> levels; // per-level costs, requirements, durations
};

// ---------------------------------------------------------------------------
// Resolved building: player state + game data merged
// ---------------------------------------------------------------------------

struct ResolvedBuilding {
    int64_t id = 0;
    std::string name;
    std::string description;
    int current_level = 0;
    // Next level info (for upgrade priority)
    int next_build_time_seconds = 0;
    std::vector<ShipBuildCost> next_level_costs;
    std::vector<BuildingRequirement> next_level_requirements;
};

// ---------------------------------------------------------------------------
// Active job: resolved with names and time remaining
// ---------------------------------------------------------------------------

struct ResolvedJob {
    std::string uuid;
    int job_type = 0;                 // 1=research, 2=building, 3=ship build, 4=ship upgrade, 5=officer
    std::string job_type_name;        // "Research", "Building", etc.
    std::string target_name;          // name of what's being built/researched
    int level = 0;                    // level being upgraded to
    int64_t start_time = 0;
    int duration = 0;
    int reduction = 0;
    bool completed = false;

    // Computed
    int remaining_seconds() const {
        auto now = std::chrono::system_clock::now();
        auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        int64_t finish_time = start_time + duration - reduction;
        return static_cast<int>(finish_time - now_epoch);
    }
};

// ---------------------------------------------------------------------------
// Active buff: resolved with expiry state
// ---------------------------------------------------------------------------

struct ResolvedBuff {
    int64_t buff_id = 0;
    int level = 0;
    bool permanent = false;           // true if no expiry
    bool expired = false;
    std::optional<int64_t> expiry_time;

    int remaining_seconds() const {
        if (permanent || expired || !expiry_time.has_value()) return -1;
        auto now = std::chrono::system_clock::now();
        auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        return static_cast<int>(expiry_time.value() - now_epoch);
    }
};

// ---------------------------------------------------------------------------
// Resource with name
// ---------------------------------------------------------------------------

struct ResolvedResource {
    int64_t id = 0;
    std::string name;
    int64_t amount = 0;
};

// ---------------------------------------------------------------------------
// Forbidden/Chaos tech
// ---------------------------------------------------------------------------

struct ResolvedTech {
    int64_t tech_id = 0;
    int tier = 0;
    int level = 0;
    int shard_count = 0;
};

// ---------------------------------------------------------------------------
// Resolved event: platform event with computed state
// ---------------------------------------------------------------------------

enum class EventState {
    Upcoming,      // announced but not started
    Active,        // currently running
    Ended,         // past end time
    Unknown,
};

inline const char* event_state_str(EventState s) {
    switch (s) {
        case EventState::Upcoming: return "Upcoming";
        case EventState::Active: return "Active";
        case EventState::Ended: return "Ended";
        default: return "Unknown";
    }
}

struct ResolvedEvent {
    // From PlayerEvent
    std::string config_id;
    std::string source;
    std::string event_type;
    EventCategory category = EventCategory::Standard;
    std::string category_name;
    int placement_type = 0;
    std::string group_name;

    // Schedule
    EventSchedule schedule;

    // Player's progress
    EventRanking ranking;
    EventEntryData entry_data;

    // Metadata
    EventMetadata metadata;

    // Reward tiers
    std::vector<EventSegment> segments;

    // Computed
    EventState state = EventState::Unknown;
    int remaining_seconds = -1;     // seconds until end (-1 if unknown)
    int total_reward_tiers = 0;     // total milestone/reward tiers
};

// ---------------------------------------------------------------------------
// The snapshot itself
// ---------------------------------------------------------------------------

struct FullAccountSnapshot {
    // Metadata
    std::chrono::system_clock::time_point snapshot_time;
    std::chrono::system_clock::time_point last_sync;
    std::string player_name;
    int ops_level = 0;

    // Resolved entities (merged player state + game data)
    std::vector<ResolvedOfficer> officers;    // ALL known officers (owned flag distinguishes)
    std::vector<ResolvedShip> ships;          // player's ships only
    std::vector<ResolvedResearch> research;   // all known research with player current_level overlay
    std::vector<ResolvedBuilding> buildings;  // player's buildings
    std::vector<ResolvedResource> resources;  // all known resources with player amount overlay
    std::vector<ResolvedBuff> buffs;          // active buffs
    std::vector<ResolvedJob> jobs;            // active jobs (not completed)
    std::vector<ResolvedTech> tech;           // forbidden/chaos tech
    std::vector<ResolvedEvent> events;        // platform events (solo, battlepass, alliance, etc.)
    int emerald_chain_level = 0;

    // Inventory + slots + missions (kept as raw for now — no game data to resolve against)
    std::vector<PlayerInventoryItem> inventory;
    std::vector<PlayerSlot> slots;
    std::vector<PlayerMission> missions;

    // Convenience lookups (populated by builder)
    int owned_officer_count = 0;
    int active_job_count = 0;
    int idle_research_slots = 0;              // research slots not currently in use
    int idle_building_slots = 0;              // building slots not currently in use
    int active_event_count = 0;               // events currently running
    int claimable_event_count = 0;            // events with unclaimed rewards
};

// ---------------------------------------------------------------------------
// Build the full snapshot from raw data sources
// ---------------------------------------------------------------------------

FullAccountSnapshot build_full_snapshot(const PlayerData& player_data,
                                        const GameData& game_data);

} // namespace stfc
