#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>

#include "core/crew_optimizer.h"

namespace stfc {

// ---------------------------------------------------------------------------
// Officer Group — a logical cluster of officers by function/scenario
//
// Instead of sending all 289 officers to the LLM at once (which overwhelms
// small models like llama3.2:1b), we split them into focused groups of
// ~10-25 officers based on their classification tags.
//
// Each group gets its own LLM query with a focused system prompt tailored
// to that group's purpose. This lets a 1B model handle each query well.
// ---------------------------------------------------------------------------

// Group identity — determines the prompt template and query focus
enum class OfficerGroupId {
    // PvP — granular by ship type
    PvP_General,          // Universal PvP officers, good on any ship
    PvP_On_Explorer,      // Best crews when YOU fly an Explorer
    PvP_On_Battleship,    // Best crews when YOU fly a Battleship
    PvP_On_Interceptor,   // Best crews when YOU fly an Interceptor
    PvP_Vs_Explorer,      // Crews optimized for killing Explorers
    PvP_Vs_Battleship,    // Crews optimized for killing Battleships
    PvP_Vs_Interceptor,   // Crews optimized for killing Interceptors

    // PvE — level-aware, only relevant content queried
    PvE_General,          // General hostile grinding (swarm, dailies)
    PvE_Specialized,      // Level-appropriate specialized hostiles (borg, eclipse, etc.)

    // Other scenarios
    Base_Attack,          // Base cracker specialists
    Base_Defend,          // Station defense specialists
    Armada,               // Armada combat officers
    Mining,               // Mining crew (skipped by AI — handled by local optimizer)
    Loot_Cargo,           // Loot multipliers and cargo officers
    State_Chain,          // Officers involved in state chains (burning, morale, etc.)
    Apex_Isolytic,        // Apex barrier/shred + isolytic cascade/defense
    Support,              // Stat boosters, ability amplifiers, miscellaneous
};

// A single officer group ready for LLM query
struct OfficerGroup {
    OfficerGroupId id;
    std::string name;                          // Human-readable: "PvP General"
    std::string description;                   // One-line: "Officers specializing in..."
    std::vector<const ClassifiedOfficer*> officers;  // OWNED officers (pointers into master list)

    // META context — full Gemini knowledge for this group
    std::vector<std::string> meta_not_owned;   // META officer names the player does NOT own (aspirational)
    std::vector<std::string> meta_crew_descriptions; // Gemini's ideal crew descriptions (if available)
    std::string meta_summary;                  // Gemini's META overview text

    // For prompt building
    std::string prompt_guidance;               // Scenario-specific LLM guidance

    int size() const { return static_cast<int>(officers.size()); }
    bool empty() const { return officers.empty(); }
    bool has_meta() const { return !meta_not_owned.empty() || !officers.empty() || !meta_summary.empty(); }
};

// ---------------------------------------------------------------------------
// Group officers by classification tags
//
// An officer can appear in multiple groups (e.g., a dual_use officer with
// state chains appears in PvP_Combat AND State_Chain). This is intentional —
// the LLM sees them in different contexts.
//
// min_group_size: groups smaller than this get merged into Support.
// ---------------------------------------------------------------------------

std::vector<OfficerGroup> group_officers(
    const std::vector<ClassifiedOfficer>& officers,
    int min_group_size = 3);

// ---------------------------------------------------------------------------
// Get a focused system prompt for a specific group
// (Shorter and more targeted than the monolithic CREW_SYSTEM_PROMPT)
// Accepts optional officer names to build a realistic JSON example
// (prevents 1B models from copying example names like "Kirk" verbatim)
// ---------------------------------------------------------------------------

std::string group_system_prompt(OfficerGroupId group_id,
                                const std::vector<std::string>& example_names = {});

// ---------------------------------------------------------------------------
// Serialize a group's officers to compact JSON for prompt injection
// (Similar to snapshot_to_json but takes raw ClassifiedOfficer pointers)
// ---------------------------------------------------------------------------

std::string group_officers_to_json(const OfficerGroup& group);

// ---------------------------------------------------------------------------
// Group ID / name conversions
// ---------------------------------------------------------------------------

std::string group_id_str(OfficerGroupId id);
OfficerGroupId group_id_from_str(const std::string& s);

} // namespace stfc
