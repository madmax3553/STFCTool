#pragma once

#include <string>
#include <vector>
#include <optional>

namespace stfc {

// ---------------------------------------------------------------------------
// Roster officer — imported from roster.csv (the spreadsheet export).
// This mirrors the Python STFCCrewOptimizer's parsed officer dict.
// ---------------------------------------------------------------------------

struct RosterOfficer {
    std::string name;
    char rarity = ' ';           // C, U, R, E
    int officer_class = 0;       // 1=Command, 2=Science, 3=Engineering
    int level = 0;
    int rank = 0;
    double attack = 0.0;
    double defense = 0.0;
    double health = 0.0;
    std::string group;           // synergy/faction group name
    double cm_pct = 0.0;        // captain maneuver / BDA percentage
    double oa_pct = 0.0;        // officer ability percentage
    std::string effect;          // status effect (burning, breach, morale, etc.)
    bool causes_effect = false;  // whether officer causes the effect
    bool player_uses = false;    // whether player marked this officer as used
    std::string description;     // full CM/BDA/OA text

    // Derived: BDA officers have their BDA percentage in the CM/BDA slot or
    // description starts with "bda:".
    bool is_bda() const {
        if (cm_pct >= 10000.0) return true;
        if (description.size() >= 4 &&
            description[0] == 'b' && description[1] == 'd' &&
            description[2] == 'a' && description[3] == ':') return true;
        return false;
    }
};

// ---------------------------------------------------------------------------
// CSV import functions
// ---------------------------------------------------------------------------

// Parse roster.csv and return all officers with attack > 0.
// Returns empty vector on file-not-found or parse failure.
std::vector<RosterOfficer> load_roster_csv(const std::string& path);

// Extract the Mess Hall crew level from the header area (row 18).
// Returns 0 if not found.
int parse_mess_hall_level(const std::string& path);

} // namespace stfc
