#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <mutex>

namespace stfc {

// ---------------------------------------------------------------------------
// META Knowledge Cache
//
// Stores per-group lists of "top officers" from Gemini (web-search-grounded).
// This is the community META — which officers are currently considered best
// for each scenario by STFC content creators and theorycrafters.
//
// Persisted to data/meta_cache.json. Refreshed manually (user presses 'M').
// Used by the group pipeline to filter the roster down to only META-relevant
// officers before sending to Ollama.
//
// Flow:
//   1. User presses 'M' → Gemini queries "top X officers for PvP/PvE/etc"
//   2. Results cached to disk with timestamp
//   3. Group pipeline reads cache, intersects with owned roster
//   4. Only owned META-relevant officers sent to Ollama
// ---------------------------------------------------------------------------

// A single group's META knowledge
struct MetaGroupEntry {
    std::string group;                              // "PvP Combat", etc.
    std::vector<std::string> top_officers;           // Officer names from META
    std::vector<std::string> top_crews_desc;         // Brief crew descriptions (optional)
    std::string meta_summary;                        // Brief META analysis text
    int64_t timestamp = 0;                           // When this was fetched
    std::string model_used;                          // Which model produced this
};

// The full cache
struct MetaCache {
    std::map<std::string, MetaGroupEntry> groups;    // keyed by group name
    int64_t last_refresh = 0;                        // Unix epoch of most recent refresh

    bool has_group(const std::string& group) const {
        return groups.count(group) > 0;
    }

    const MetaGroupEntry* get_group(const std::string& group) const {
        auto it = groups.find(group);
        return it != groups.end() ? &it->second : nullptr;
    }

    bool empty() const { return groups.empty(); }

    // How old is the cache in hours?
    double age_hours() const;

    // Human-readable age string: "2h ago", "3d ago", "never"
    std::string age_str() const;
};

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

MetaCache load_meta_cache(const std::string& path = "data/meta_cache.json");
bool save_meta_cache(const MetaCache& cache, const std::string& path = "data/meta_cache.json");

// ---------------------------------------------------------------------------
// Build the Gemini prompt for a specific group's META query
//
// Returns a prompt like:
//   "List the top 15 officers for PvP combat in Star Trek Fleet Command.
//    Include both captains and bridge officers. For each, state their role
//    (captain vs bridge) and why they're META."
// ---------------------------------------------------------------------------

std::string build_meta_query_prompt(const std::string& group_name,
                                     const std::string& group_description);

// ---------------------------------------------------------------------------
// Parse Gemini's META response into a list of officer names
//
// Gemini returns free-text with officer names. We extract all names that
// look like STFC officer names (matching against known officer list).
// Also handles JSON responses and bullet-point lists.
// ---------------------------------------------------------------------------

std::vector<std::string> parse_meta_officer_names(
    const std::string& response,
    const std::vector<std::string>& known_officers);

} // namespace stfc
