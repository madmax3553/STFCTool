#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>

namespace stfc {

// ---------------------------------------------------------------------------
// Static game data (from api.spocks.club)
// ---------------------------------------------------------------------------

struct OfficerStats {
    int level = 0;
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;
};

struct OfficerRank {
    int rank = 0;
    // Rank costs, shard requirements, etc. added later
};

struct AbilityValue {
    double value = 0.0;
    double chance = 0.0;
};

struct OfficerAbility {
    int64_t id = 0;
    bool value_is_percentage = false;
    std::vector<AbilityValue> values;
};

struct Officer {
    int64_t id = 0;
    int art_id = 0;
    int loca_id = 0;
    int64_t faction = 0;
    int officer_class = 0;    // 1=command, 2=engineering, 3=science
    int rarity = 0;           // 1=common, 2=uncommon, 3=rare, 4=epic
    int64_t synergy_id = 0;
    int max_rank = 0;
    OfficerAbility ability;
    OfficerAbility captain_ability;
    std::vector<OfficerStats> stats;
    std::vector<int> level_xp;       // XP per level
    std::vector<OfficerRank> ranks;

    // Translated fields (populated from translations endpoint)
    std::string name;
    std::string short_name;
    std::string description;
    std::string flavor_text;
    std::string group_name;       // synergy group name
};

// ---------------------------------------------------------------------------
// Ship data
// ---------------------------------------------------------------------------

struct ShipBuildCost {
    int64_t resource_id = 0;
    int64_t amount = 0;
};

struct ShipLevel {
    int level = 0;
    int xp = 0;
    double shield = 0.0;
    double health = 0.0;
};

struct ShipCrewSlot {
    int slots = 0;
    int unlock_level = 0;
};

struct Ship {
    int64_t id = 0;
    int art_id = 0;
    int loca_id = 0;
    int max_tier = 0;
    int rarity = 0;
    int grade = 0;
    int scrap_level = -1;
    int build_time_seconds = 0;
    int64_t faction = -1;
    int blueprints_required = 0;
    int hull_type = 0;           // 0=interceptor, 1=survey, 2=explorer, 3=battleship
    int max_level = 0;
    std::vector<ShipBuildCost> build_cost;
    std::vector<ShipCrewSlot> crew_slots;
    std::vector<ShipLevel> levels;

    // Translated
    std::string name;
    std::string description;
    std::string ability_name;
    std::string ability_description;
};

// ---------------------------------------------------------------------------
// Research data
// ---------------------------------------------------------------------------

struct ResearchBuff {
    int64_t id = 0;
    bool value_is_percentage = false;
    std::vector<AbilityValue> values;
};

struct ResearchCost {
    int64_t resource_id = 0;
    int64_t amount = 0;
};

struct Research {
    int64_t id = 0;
    int art_id = 0;
    int loca_id = 0;
    int view_level = 0;
    int unlock_level = 0;
    int64_t research_tree = 0;
    std::vector<ResearchBuff> buffs;

    // Translated
    std::string name;
    std::string description;
};

// ---------------------------------------------------------------------------
// Building data
// ---------------------------------------------------------------------------

struct BuildingRequirement {
    std::string requirement_type;
    int requirement_id = 0;
    int requirement_level = 0;
};

struct BuildingLevel {
    int id = 0;
    int64_t player_strength = 0;
    int64_t strength = 0;
    int generation = 0;
    int build_time_seconds = 0;
    std::vector<ShipBuildCost> costs;
    int hard_currency_cost = 0;
    std::vector<BuildingRequirement> requirements;
};

struct Building {
    int64_t id = 0;
    std::vector<BuildingLevel> levels;

    // Translated
    std::string name;
    std::string description;
};

// ---------------------------------------------------------------------------
// Resource data
// ---------------------------------------------------------------------------

struct Resource {
    int64_t id = 0;
    std::string name;
    std::string description;
};

// ---------------------------------------------------------------------------
// Hostile data
// ---------------------------------------------------------------------------

struct Hostile {
    int64_t id = 0;
    std::string name;
    std::string description;
};

// ---------------------------------------------------------------------------
// Player data (from community mod sync)
// ---------------------------------------------------------------------------

struct PlayerOfficer {
    int64_t officer_id = 0;
    int level = 0;
    int rank = 0;
    int shard_count = 0;
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;
    std::string name;  // resolved from game data
};

struct PlayerShip {
    int64_t ship_id = 0;  // psid from mod
    int64_t hull_id = 0;
    int tier = 0;
    int level = 0;
    double level_percentage = 0.0;
    std::string name;  // resolved from game data
};

struct PlayerResearch {
    int64_t research_id = 0;
    int level = 0;
    std::string name;  // resolved from game data
};

struct PlayerBuilding {
    int64_t building_id = 0;
    int level = 0;
    std::string name;  // resolved from game data
};

struct PlayerResource {
    int64_t resource_id = 0;
    int64_t amount = 0;
    std::string name;  // resolved from game data
};

struct PlayerBuff {
    int64_t buff_id = 0;
    int level = 0;
    int64_t expiry_time = 0;  // unix timestamp, 0 = no expiry
    bool expired = false;
};

struct PlayerJob {
    std::string uuid;
    int job_type = 0;
    int64_t start_time = 0;
    int duration = 0;        // seconds
    int reduction = 0;       // seconds reduced
    int64_t research_id = 0; // associated research if applicable
    int level = 0;
    bool completed = false;
};

struct PlayerInventoryItem {
    int item_type = 0;
    int64_t ref_id = 0;
    int64_t count = 0;
};

struct PlayerSlot {
    int64_t slot_id = 0;
    int slot_type = 0;
    int64_t spec_id = 0;
    int64_t item_id = 0;
};

struct PlayerTrait {
    int64_t officer_id = 0;
    int64_t trait_id = 0;
    int level = 0;
};

struct PlayerTech {
    int64_t tech_id = 0;
    int tier = 0;
    int level = 0;
    int shard_count = 0;
};

struct PlayerMission {
    int64_t mission_id = 0;
    bool active = false;   // true = in-progress, false = completed
};

// ---------------------------------------------------------------------------
// Aggregate game data cache
// ---------------------------------------------------------------------------

struct GameData {
    std::map<int64_t, Officer> officers;
    std::map<int64_t, Ship> ships;
    std::map<int64_t, Research> researches;
    std::map<int64_t, Building> buildings;
    std::map<int64_t, Resource> resources;
    std::map<int64_t, Hostile> hostiles;

    // Translation maps: id -> {key -> text}
    std::map<std::string, std::map<std::string, std::string>> officer_translations;
    std::map<std::string, std::map<std::string, std::string>> ship_translations;
    std::map<std::string, std::map<std::string, std::string>> research_translations;
    std::map<std::string, std::map<std::string, std::string>> building_translations;
};

struct PlayerData {
    std::vector<PlayerOfficer> officers;
    std::vector<PlayerShip> ships;
    std::vector<PlayerResearch> researches;
    std::vector<PlayerBuilding> buildings;
    std::vector<PlayerResource> resources;
    std::vector<PlayerBuff> buffs;
    std::vector<PlayerJob> jobs;
    std::vector<PlayerInventoryItem> inventory;
    std::vector<PlayerSlot> slots;
    std::vector<PlayerTrait> traits;
    std::vector<PlayerTech> techs;
    std::vector<PlayerMission> missions;
    int ops_level = 0;
    std::string player_name;
    std::chrono::system_clock::time_point last_sync;
};

// Hull type helpers (API values: 0=interceptor, 1=survey, 2=explorer, 3=battleship)
inline const char* hull_type_str(int hull_type) {
    switch (hull_type) {
        case 0: return "Interceptor";
        case 1: return "Survey";
        case 2: return "Explorer";
        case 3: return "Battleship";
        default: return "Unknown";
    }
}

inline const char* rarity_str(int rarity) {
    switch (rarity) {
        case 1: return "Common";
        case 2: return "Uncommon";
        case 3: return "Rare";
        case 4: return "Epic";
        default: return "Unknown";
    }
}

inline const char* officer_class_str(int cls) {
    switch (cls) {
        case 1: return "Command";
        case 2: return "Engineering";
        case 3: return "Science";
        default: return "Unknown";
    }
}

} // namespace stfc
