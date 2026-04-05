#pragma once

#include <string>
#include <functional>
#include "data/models.h"

namespace stfc {

// Fetches game data from api.spocks.club and caches locally as JSON files.
class ApiClient {
public:
    explicit ApiClient(const std::string& cache_dir = "data/game_data");

    // Fetch all game data (officers, ships, research, buildings, resources)
    // and store to the GameData struct. Uses cache if available and fresh.
    bool fetch_all(GameData& data);

    // Individual fetch methods
    bool fetch_officers(GameData& data);
    bool fetch_ships(GameData& data);
    bool fetch_research(GameData& data);
    bool fetch_buildings(GameData& data);
    bool fetch_resources(GameData& data);

    // Fetch translation data and apply to existing game data
    bool fetch_translations(GameData& data, const std::string& lang = "en");

    // Force refresh from API (ignore cache)
    void set_force_refresh(bool force) { force_refresh_ = force; }

    // Cache-only mode: never make network requests, only read from disk
    void set_cache_only(bool cache_only) { cache_only_ = cache_only; }

    // Set a progress callback: (step_name, current, total)
    using ProgressCallback = std::function<void(const std::string&, int, int)>;
    void set_progress_callback(ProgressCallback cb) { progress_cb_ = std::move(cb); }

private:
    std::string cache_dir_;
    bool force_refresh_ = false;
    bool cache_only_ = false;
    ProgressCallback progress_cb_;

    // HTTP GET to api.spocks.club, returns response body or empty on error
    std::string api_get(const std::string& path);

    // Cache management
    std::string read_cache(const std::string& filename);
    bool write_cache(const std::string& filename, const std::string& data);
    bool is_cache_fresh(const std::string& filename, int max_age_hours = 24);

    void report_progress(const std::string& step, int current, int total);
};

} // namespace stfc
