// Smoke test for STFCTool
//
// Usage:
//   smoke_test              Run against cached data (fast, offline)
//   smoke_test --live       Force fresh fetch from api.spocks.club (tests full network path)
//   smoke_test --clean      Wipe cache, then fetch live (coldest possible start)

#include <iostream>
#include <cassert>
#include <filesystem>
#include <set>
#include <algorithm>
#include <string>
#include <chrono>
#include <cstring>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include "data/models.h"
#include "data/api_client.h"

namespace fs = std::filesystem;
using namespace stfc;

// ---------------------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------------------

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  TEST: " << name << " ... " << std::flush; \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        std::cout << "\033[32mPASS\033[0m\n"; \
    } while(0)

#define FAIL(msg) \
    do { \
        tests_failed++; \
        std::cout << "\033[31mFAIL\033[0m: " << msg << "\n"; \
    } while(0)

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while(0)

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

static GameData game_data;
static bool data_loaded = false;
static bool live_mode = false;

// ---------------------------------------------------------------------------
// Test: live API connectivity
// ---------------------------------------------------------------------------

void test_api_reachable() {
    TEST("api.spocks.club is reachable (HTTPS GET /officer, first 1KB)");

    // Quick probe: just check we can connect and get a 200 with JSON
    httplib::SSLClient cli("api.spocks.club", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(15);

    auto res = cli.Get("/resource");  // smallest endpoint
    CHECK(res != nullptr, "connection failed (nullptr result)");
    CHECK(res->status == 200, "HTTP " + std::to_string(res->status));
    CHECK(res->body.size() > 100, "response too small: " + std::to_string(res->body.size()) + " bytes");
    CHECK(res->body[0] == '[', "response not a JSON array, starts with: " + res->body.substr(0, 20));
    PASS();
}

// ---------------------------------------------------------------------------
// Test: fetch all data (live or cached)
// ---------------------------------------------------------------------------

void test_fetch_all() {
    if (live_mode) {
        TEST("fetch_all from live api.spocks.club");
    } else {
        TEST("fetch_all from cache");
    }

    ApiClient client("data/game_data");
    if (live_mode) {
        client.set_force_refresh(true);
    }

    auto start = std::chrono::steady_clock::now();

    client.set_progress_callback([](const std::string& step, int current, int total) {
        // Print inline progress for live mode
        if (current == 0) {
            std::cout << "\n    fetching " << step << "..." << std::flush;
        }
    });

    bool ok = client.fetch_all(game_data);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (live_mode) std::cout << "\n    ";

    CHECK(ok, "fetch_all returned false");
    data_loaded = true;

    std::cout << "(" << elapsed.count() << "ms, "
              << game_data.officers.size() << " officers, "
              << game_data.ships.size() << " ships, "
              << game_data.researches.size() << " research, "
              << game_data.buildings.size() << " buildings, "
              << game_data.resources.size() << " resources) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: cache files written after live fetch
// ---------------------------------------------------------------------------

void test_cache_files_exist() {
    TEST("cache files exist and non-empty");

    std::vector<std::string> expected = {
        "officers.json", "ships.json", "research.json", "buildings.json",
        "resources.json", "translations_officers.json", "translations_ships.json",
        "translations_researches.json", "translations_buildings.json",
        "translations_resources.json", "translations_synergies.json",
    };

    size_t total_bytes = 0;
    for (auto& f : expected) {
        auto path = fs::path("data/game_data") / f;
        CHECK(fs::exists(path), "missing: " + f);
        auto sz = fs::file_size(path);
        CHECK(sz > 0, "empty: " + f);
        total_bytes += sz;
    }

    std::cout << "(" << (total_bytes / 1024) << " KB total) ";
    PASS();
}

void test_cache_freshness() {
    TEST("cache files are fresh (< 25h old)");

    auto now = fs::file_time_type::clock::now();
    for (auto& entry : fs::directory_iterator("data/game_data")) {
        if (entry.path().extension() == ".json") {
            auto age = std::chrono::duration_cast<std::chrono::hours>(
                now - fs::last_write_time(entry.path()));
            if (live_mode) {
                // After a live fetch, files should be seconds old
                CHECK(age.count() < 1, entry.path().filename().string() + " is " +
                      std::to_string(age.count()) + "h old after live fetch");
            } else {
                CHECK(age.count() < 25, entry.path().filename().string() + " is " +
                      std::to_string(age.count()) + "h old");
            }
        }
    }
    PASS();
}

// ---------------------------------------------------------------------------
// Test: each endpoint individually (live only)
// ---------------------------------------------------------------------------

void test_endpoint_officers() {
    TEST("GET /officer returns valid JSON array");

    httplib::SSLClient cli("api.spocks.club", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);
    auto res = cli.Get("/officer");

    CHECK(res != nullptr, "connection failed");
    CHECK(res->status == 200, "HTTP " + std::to_string(res->status));

    auto arr = nlohmann::json::parse(res->body);
    CHECK(arr.is_array(), "not an array");
    CHECK(arr.size() > 200, "only " + std::to_string(arr.size()) + " officers");

    // Spot check first officer has expected fields
    auto& first = arr[0];
    CHECK(first.contains("id"), "missing 'id' field");
    CHECK(first.contains("rarity"), "missing 'rarity' field");
    CHECK(first.contains("class"), "missing 'class' field");
    CHECK(first.contains("ability"), "missing 'ability' field");

    std::cout << "(" << arr.size() << " officers, " << (res->body.size() / 1024) << " KB) ";
    PASS();
}

void test_endpoint_ships() {
    TEST("GET /ship returns valid JSON array");

    httplib::SSLClient cli("api.spocks.club", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);
    auto res = cli.Get("/ship");

    CHECK(res != nullptr, "connection failed");
    CHECK(res->status == 200, "HTTP " + std::to_string(res->status));

    auto arr = nlohmann::json::parse(res->body);
    CHECK(arr.is_array(), "not an array");
    CHECK(arr.size() > 50, "only " + std::to_string(arr.size()) + " ships");

    auto& first = arr[0];
    CHECK(first.contains("id"), "missing 'id'");
    CHECK(first.contains("hull_type"), "missing 'hull_type'");
    CHECK(first.contains("grade"), "missing 'grade'");

    std::cout << "(" << arr.size() << " ships, " << (res->body.size() / 1024) << " KB) ";
    PASS();
}

void test_endpoint_translations() {
    TEST("GET /translations/en/officers returns keyed entries");

    httplib::SSLClient cli("api.spocks.club", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);
    auto res = cli.Get("/translations/en/officers");

    CHECK(res != nullptr, "connection failed");
    CHECK(res->status == 200, "HTTP " + std::to_string(res->status));

    auto arr = nlohmann::json::parse(res->body);
    CHECK(arr.is_array(), "not an array");
    CHECK(arr.size() > 100, "only " + std::to_string(arr.size()) + " entries");

    // Check structure
    auto& first = arr[0];
    CHECK(first.contains("id"), "missing 'id'");
    CHECK(first.contains("key"), "missing 'key'");
    CHECK(first.contains("text"), "missing 'text'");

    // Find Kirk somewhere
    bool found_kirk = false;
    for (auto& entry : arr) {
        std::string text = entry.value("text", "");
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("kirk") != std::string::npos) { found_kirk = true; break; }
    }
    CHECK(found_kirk, "Kirk not found in officer translations");

    std::cout << "(" << arr.size() << " translations) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: officer data sanity
// ---------------------------------------------------------------------------

void test_officer_count() {
    TEST("officer count > 200");
    CHECK(data_loaded, "data not loaded");
    CHECK(game_data.officers.size() > 200, "only " + std::to_string(game_data.officers.size()) + " officers");
    PASS();
}

void test_officer_fields() {
    TEST("officers have required fields");
    CHECK(data_loaded, "data not loaded");

    int named = 0, with_stats = 0, with_ability = 0;
    for (auto& [id, o] : game_data.officers) {
        CHECK(o.id != 0, "officer with id=0");
        CHECK(o.rarity >= 1 && o.rarity <= 4, "officer rarity out of range: " + std::to_string(o.rarity));
        CHECK(o.officer_class >= 1 && o.officer_class <= 3,
              "officer class out of range: " + std::to_string(o.officer_class));
        CHECK(o.max_rank > 0, "officer max_rank=0 for id=" + std::to_string(id));

        if (!o.name.empty() || !o.short_name.empty()) named++;
        if (!o.stats.empty()) with_stats++;
        if (o.ability.id != 0) with_ability++;
    }

    double name_pct = 100.0 * named / game_data.officers.size();
    CHECK(name_pct > 90.0, "only " + std::to_string(name_pct) + "% officers named");

    double stat_pct = 100.0 * with_stats / game_data.officers.size();
    CHECK(stat_pct > 80.0, "only " + std::to_string(stat_pct) + "% officers have stats");

    std::cout << "(" << named << " named, " << with_stats << " with stats, "
              << with_ability << " with abilities) ";
    PASS();
}

void test_officer_rarity_distribution() {
    TEST("officer rarity distribution");
    CHECK(data_loaded, "data not loaded");

    int counts[5] = {0};
    for (auto& [id, o] : game_data.officers) {
        if (o.rarity >= 1 && o.rarity <= 4) counts[o.rarity]++;
    }

    CHECK(counts[1] > 0, "no common officers");
    CHECK(counts[2] > 0, "no uncommon officers");
    CHECK(counts[3] > 0, "no rare officers");
    CHECK(counts[4] > 0, "no epic officers");

    std::cout << "(C:" << counts[1] << " U:" << counts[2]
              << " R:" << counts[3] << " E:" << counts[4] << ") ";
    PASS();
}

void test_known_officers_exist() {
    TEST("known officers: Kirk, Spock, Khan, Uhura, Scott");
    CHECK(data_loaded, "data not loaded");

    std::set<std::string> targets = {"kirk", "spock", "khan", "uhura", "scott"};
    std::set<std::string> found;

    for (auto& [id, o] : game_data.officers) {
        // Search both name and short_name
        std::string combined = o.name + " " + o.short_name;
        std::string lower = combined;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (auto& t : targets) {
            if (lower.find(t) != std::string::npos) found.insert(t);
        }
    }

    for (auto& t : targets) {
        CHECK(found.count(t), t + " not found in officer data");
    }
    PASS();
}

void test_officer_stats_sane() {
    TEST("officer stats values are non-negative and scale with level");
    CHECK(data_loaded, "data not loaded");

    int checked = 0;
    for (auto& [id, o] : game_data.officers) {
        if (o.stats.size() < 2) continue;
        checked++;

        for (auto& s : o.stats) {
            CHECK(s.attack >= 0, "negative attack for officer " + std::to_string(id));
            CHECK(s.defense >= 0, "negative defense for officer " + std::to_string(id));
            CHECK(s.health >= 0, "negative health for officer " + std::to_string(id));
        }

        // Last level stats should be >= first level stats
        auto& first = o.stats.front();
        auto& last = o.stats.back();
        CHECK(last.attack >= first.attack,
              "attack doesn't scale for " + o.name + ": " +
              std::to_string(first.attack) + " -> " + std::to_string(last.attack));
        CHECK(last.health >= first.health,
              "health doesn't scale for " + o.name);
    }

    std::cout << "(" << checked << " officers checked) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: synergy groups
// ---------------------------------------------------------------------------

void test_synergy_groups() {
    TEST("synergy group names populated");
    CHECK(data_loaded, "data not loaded");

    int with_group = 0;
    std::set<std::string> groups;
    for (auto& [id, o] : game_data.officers) {
        if (!o.group_name.empty()) {
            with_group++;
            groups.insert(o.group_name);
        }
    }

    CHECK(with_group > 50, "only " + std::to_string(with_group) + " officers have synergy groups");
    CHECK(groups.size() > 10, "only " + std::to_string(groups.size()) + " unique groups");

    bool found_enterprise = false;
    for (auto& g : groups) {
        std::string lower = g;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("enterprise") != std::string::npos) found_enterprise = true;
    }
    CHECK(found_enterprise, "ENTERPRISE CREW group not found");

    std::cout << "(" << with_group << " officers in " << groups.size() << " groups) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: ship data sanity
// ---------------------------------------------------------------------------

void test_ship_count() {
    TEST("ship count > 50");
    CHECK(data_loaded, "data not loaded");
    CHECK(game_data.ships.size() > 50, "only " + std::to_string(game_data.ships.size()) + " ships");
    PASS();
}

void test_ship_fields() {
    TEST("ships have required fields");
    CHECK(data_loaded, "data not loaded");

    int named = 0;
    for (auto& [id, s] : game_data.ships) {
        CHECK(s.id != 0, "ship with id=0");
        CHECK(s.hull_type >= 0 && s.hull_type <= 3,
              "hull_type out of range: " + std::to_string(s.hull_type) + " for id=" + std::to_string(id));
        CHECK(s.grade >= 1 && s.grade <= 9,
              "grade out of range: " + std::to_string(s.grade) + " for id=" + std::to_string(id));
        CHECK(s.max_tier > 0, "max_tier=0 for id=" + std::to_string(id));
        CHECK(s.max_level > 0, "max_level=0 for id=" + std::to_string(id));
        if (!s.name.empty()) named++;
    }

    double name_pct = 100.0 * named / game_data.ships.size();
    CHECK(name_pct > 90.0, "only " + std::to_string(name_pct) + "% ships named");

    std::cout << "(" << named << "/" << game_data.ships.size() << " named) ";
    PASS();
}

void test_hull_type_mapping() {
    TEST("hull type mapping: Phindra=interceptor, Realta=explorer, Talla=battleship, Fortunate=survey");
    CHECK(data_loaded, "data not loaded");

    struct Expected { std::string name_fragment; int hull_type; std::string type_name; };
    std::vector<Expected> checks = {
        {"phindra", 0, "Interceptor"},
        {"realta", 2, "Explorer"},
        {"talla", 3, "Battleship"},
        {"fortunate", 1, "Survey"},
    };

    for (auto& exp : checks) {
        bool found = false;
        for (auto& [id, s] : game_data.ships) {
            std::string lower = s.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(exp.name_fragment) != std::string::npos) {
                CHECK(s.hull_type == exp.hull_type,
                      s.name + " hull_type=" + std::to_string(s.hull_type) +
                      " expected " + std::to_string(exp.hull_type) + " (" + exp.type_name + ")");
                CHECK(std::string(hull_type_str(s.hull_type)) == exp.type_name,
                      "hull_type_str mismatch for " + s.name);
                found = true;
                break;
            }
        }
        CHECK(found, exp.name_fragment + " not found in ship data");
    }
    PASS();
}

void test_ship_grade_distribution() {
    TEST("ships span grades G1-G6+");
    CHECK(data_loaded, "data not loaded");

    std::set<int> grades;
    std::map<int, int> grade_counts;
    for (auto& [id, s] : game_data.ships) {
        grades.insert(s.grade);
        grade_counts[s.grade]++;
    }

    CHECK(grades.size() >= 5, "only " + std::to_string(grades.size()) + " grades");
    CHECK(grades.count(1), "no G1 ships");
    CHECK(grades.count(3), "no G3 ships");
    CHECK(grades.count(5), "no G5 ships");

    std::cout << "(";
    for (auto& [g, c] : grade_counts) std::cout << "G" << g << ":" << c << " ";
    std::cout << ") ";
    PASS();
}

void test_ship_hull_distribution() {
    TEST("all hull types represented");
    CHECK(data_loaded, "data not loaded");

    int counts[4] = {0};
    for (auto& [id, s] : game_data.ships) {
        if (s.hull_type >= 0 && s.hull_type <= 3) counts[s.hull_type]++;
    }

    CHECK(counts[0] > 0, "no interceptors");
    CHECK(counts[1] > 0, "no surveys");
    CHECK(counts[2] > 0, "no explorers");
    CHECK(counts[3] > 0, "no battleships");

    std::cout << "(Int:" << counts[0] << " Srv:" << counts[1]
              << " Exp:" << counts[2] << " BS:" << counts[3] << ") ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: research data sanity
// ---------------------------------------------------------------------------

void test_research_count() {
    TEST("research count > 1000");
    CHECK(data_loaded, "data not loaded");
    CHECK(game_data.researches.size() > 1000,
          "only " + std::to_string(game_data.researches.size()) + " research nodes");
    PASS();
}

void test_research_has_trees() {
    TEST("research nodes span multiple trees");
    CHECK(data_loaded, "data not loaded");

    std::set<int64_t> trees;
    for (auto& [id, r] : game_data.researches) {
        trees.insert(r.research_tree);
    }

    CHECK(trees.size() >= 3, "only " + std::to_string(trees.size()) + " research trees");
    std::cout << "(" << trees.size() << " distinct trees) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: building data sanity
// ---------------------------------------------------------------------------

void test_building_count() {
    TEST("building count > 50");
    CHECK(data_loaded, "data not loaded");
    CHECK(game_data.buildings.size() > 50,
          "only " + std::to_string(game_data.buildings.size()) + " buildings");
    PASS();
}

void test_building_levels() {
    TEST("buildings have levels with costs and build times");
    CHECK(data_loaded, "data not loaded");

    int with_levels = 0;
    int with_costs = 0;
    int with_build_time = 0;
    for (auto& [id, b] : game_data.buildings) {
        if (!b.levels.empty()) {
            with_levels++;
            for (auto& l : b.levels) {
                if (!l.costs.empty()) { with_costs++; break; }
            }
            for (auto& l : b.levels) {
                if (l.build_time_seconds > 0) { with_build_time++; break; }
            }
        }
    }

    double level_pct = 100.0 * with_levels / game_data.buildings.size();
    CHECK(level_pct > 80.0, "only " + std::to_string(level_pct) + "% buildings have levels");
    CHECK(with_costs > 0, "no buildings have costs");
    CHECK(with_build_time > 0, "no buildings have build times");

    std::cout << "(" << with_levels << " with levels, " << with_costs << " with costs, "
              << with_build_time << " with build times) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: resource data
// ---------------------------------------------------------------------------

void test_resource_count() {
    TEST("resource count > 100");
    CHECK(data_loaded, "data not loaded");
    CHECK(game_data.resources.size() > 100,
          "only " + std::to_string(game_data.resources.size()) + " resources");
    PASS();
}

void test_resource_names() {
    TEST("resources have names from translations");
    CHECK(data_loaded, "data not loaded");

    int named = 0;
    bool found_tritanium = false, found_dilithium = false;
    for (auto& [id, r] : game_data.resources) {
        if (!r.name.empty()) {
            named++;
            std::string lower = r.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("tritanium") != std::string::npos) found_tritanium = true;
            if (lower.find("dilithium") != std::string::npos) found_dilithium = true;
        }
    }

    double name_pct = 100.0 * named / game_data.resources.size();
    CHECK(name_pct > 50.0, "only " + std::to_string(name_pct) + "% resources named");
    CHECK(found_tritanium, "Tritanium not found");
    CHECK(found_dilithium, "Dilithium not found");

    std::cout << "(" << named << "/" << game_data.resources.size() << " named) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Test: helper functions
// ---------------------------------------------------------------------------

void test_hull_type_str() {
    TEST("hull_type_str all values");
    CHECK(std::string(hull_type_str(0)) == "Interceptor", "0 != Interceptor");
    CHECK(std::string(hull_type_str(1)) == "Survey", "1 != Survey");
    CHECK(std::string(hull_type_str(2)) == "Explorer", "2 != Explorer");
    CHECK(std::string(hull_type_str(3)) == "Battleship", "3 != Battleship");
    CHECK(std::string(hull_type_str(99)) == "Unknown", "99 != Unknown");
    PASS();
}

void test_rarity_str() {
    TEST("rarity_str all values");
    CHECK(std::string(rarity_str(1)) == "Common", "1 != Common");
    CHECK(std::string(rarity_str(2)) == "Uncommon", "2 != Uncommon");
    CHECK(std::string(rarity_str(3)) == "Rare", "3 != Rare");
    CHECK(std::string(rarity_str(4)) == "Epic", "4 != Epic");
    CHECK(std::string(rarity_str(0)) == "Unknown", "0 != Unknown");
    PASS();
}

void test_officer_class_str() {
    TEST("officer_class_str all values");
    CHECK(std::string(officer_class_str(1)) == "Command", "1 != Command");
    CHECK(std::string(officer_class_str(2)) == "Engineering", "2 != Engineering");
    CHECK(std::string(officer_class_str(3)) == "Science", "3 != Science");
    CHECK(std::string(officer_class_str(0)) == "Unknown", "0 != Unknown");
    PASS();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    bool clean_mode = false;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--live") == 0) live_mode = true;
        else if (std::strcmp(argv[i], "--clean") == 0) { live_mode = true; clean_mode = true; }
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: smoke_test [--live] [--clean]\n"
                      << "  (no args)  Test against cached data (fast, offline)\n"
                      << "  --live     Force fresh fetch from api.spocks.club\n"
                      << "  --clean    Wipe cache first, then fetch live\n";
            return 0;
        }
    }

    std::cout << "=== STFCTool Smoke Test ===\n";
    if (live_mode) {
        std::cout << "Mode: \033[33mLIVE\033[0m (fetching from api.spocks.club)\n";
    } else {
        std::cout << "Mode: CACHED (using data/game_data/)\n";
    }

    if (clean_mode) {
        std::cout << "Wiping cache...\n";
        for (auto& entry : fs::directory_iterator("data/game_data")) {
            if (entry.path().extension() == ".json") {
                fs::remove(entry.path());
            }
        }
    }

    if (!live_mode && !fs::exists("data/game_data/officers.json")) {
        std::cerr << "ERROR: No cached data. Run with --live first, or use the main app.\n";
        return 1;
    }

    std::cout << "\n--- Helper functions ---\n";
    test_hull_type_str();
    test_rarity_str();
    test_officer_class_str();

    if (live_mode) {
        std::cout << "\n--- API connectivity ---\n";
        test_api_reachable();
        test_endpoint_officers();
        test_endpoint_ships();
        test_endpoint_translations();
    }

    std::cout << "\n--- Data loading ---\n";
    test_fetch_all();
    test_cache_files_exist();
    test_cache_freshness();

    std::cout << "\n--- Officers (" << game_data.officers.size() << ") ---\n";
    test_officer_count();
    test_officer_fields();
    test_officer_rarity_distribution();
    test_known_officers_exist();
    test_officer_stats_sane();
    test_synergy_groups();

    std::cout << "\n--- Ships (" << game_data.ships.size() << ") ---\n";
    test_ship_count();
    test_ship_fields();
    test_hull_type_mapping();
    test_ship_grade_distribution();
    test_ship_hull_distribution();

    std::cout << "\n--- Research (" << game_data.researches.size() << ") ---\n";
    test_research_count();
    test_research_has_trees();

    std::cout << "\n--- Buildings (" << game_data.buildings.size() << ") ---\n";
    test_building_count();
    test_building_levels();

    std::cout << "\n--- Resources (" << game_data.resources.size() << ") ---\n";
    test_resource_count();
    test_resource_names();

    // Summary
    std::cout << "\n";
    if (tests_failed == 0) {
        std::cout << "=== \033[32m" << tests_passed << "/" << tests_run << " PASSED\033[0m ===\n";
    } else {
        std::cout << "=== " << tests_passed << "/" << tests_run << " passed, "
                  << "\033[31m" << tests_failed << " FAILED\033[0m ===\n";
    }

    return tests_failed > 0 ? 1 : 0;
}
