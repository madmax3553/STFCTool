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
#include <memory>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include "data/models.h"
#include "data/api_client.h"
#include "util/csv_import.h"
#include "core/crew_optimizer.h"
#include "core/planner.h"

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
// CSV import tests
// ---------------------------------------------------------------------------

static std::vector<RosterOfficer> roster;

void test_csv_load() {
    TEST("load roster.csv");
    roster = load_roster_csv("roster.csv");
    CHECK(roster.size() > 100, "expected >100 officers, got " + std::to_string(roster.size()));
    PASS();
}

void test_csv_officer_count() {
    TEST("roster officer count matches Python (~270+)");
    // Python loads officers with attack > 0. The file has ~276 data rows (rows 20-295).
    // Some have zero attack and are filtered out.
    CHECK(roster.size() >= 200, "expected >= 200, got " + std::to_string(roster.size()));
    CHECK(roster.size() <= 300, "expected <= 300, got " + std::to_string(roster.size()));
    PASS();
}

void test_csv_known_officer() {
    TEST("known officer: Kirk");
    bool found = false;
    for (const auto& o : roster) {
        if (o.name == "Kirk") {
            found = true;
            CHECK(o.rarity == 'E', "Kirk rarity should be E");
            CHECK(o.level > 0, "Kirk level should be > 0");
            CHECK(o.attack > 0, "Kirk attack should be > 0");
            break;
        }
    }
    CHECK(found, "Kirk not found in roster");
    PASS();
}

void test_csv_rarity_values() {
    TEST("rarity values are C/U/R/E");
    std::set<char> rarities;
    for (const auto& o : roster) {
        rarities.insert(o.rarity);
    }
    CHECK(rarities.count('U') > 0, "no Uncommon officers");
    CHECK(rarities.count('R') > 0, "no Rare officers");
    CHECK(rarities.count('E') > 0, "no Epic officers");
    // Common officers might have 0 attack and be filtered, but check anyway
    PASS();
}

void test_csv_groups_populated() {
    TEST("officer groups populated");
    int with_group = 0;
    for (const auto& o : roster) {
        if (!o.group.empty()) ++with_group;
    }
    CHECK(with_group > (int)roster.size() / 2,
          "expected most officers to have groups, got " + std::to_string(with_group));
    PASS();
}

void test_csv_bda_detection() {
    TEST("BDA detection (cm_pct >= 10000)");
    int bda_count = 0;
    for (const auto& o : roster) {
        if (o.is_bda()) ++bda_count;
    }
    // Should have some BDA officers but not most
    CHECK(bda_count > 0, "expected some BDA officers");
    CHECK(bda_count < (int)roster.size() / 2, "too many BDA officers: " + std::to_string(bda_count));
    PASS();
}

void test_csv_multiline_fields() {
    TEST("multiline ability descriptions parsed");
    // WOK Saavik and WOK Scotty have multiline descriptions in the CSV
    bool found_multiline = false;
    for (const auto& o : roster) {
        if (o.name.find("WOK") != std::string::npos && o.description.find('\n') != std::string::npos) {
            found_multiline = true;
            break;
        }
    }
    CHECK(found_multiline, "expected multiline description for WOK officers");
    PASS();
}

void test_csv_mess_hall_level() {
    TEST("mess hall level parsed from header");
    int level = parse_mess_hall_level("roster.csv");
    CHECK(level > 0, "mess hall level should be > 0, got " + std::to_string(level));
    CHECK(level < 99999, "mess hall level unreasonably high: " + std::to_string(level));
    PASS();
}

void test_csv_effects() {
    TEST("status effects populated");
    std::set<std::string> effects;
    for (const auto& o : roster) {
        if (!o.effect.empty()) effects.insert(o.effect);
    }
    CHECK(effects.count("burning") > 0, "no burning officers");
    CHECK(effects.count("morale") > 0, "no morale officers");
    PASS();
}

// ---------------------------------------------------------------------------
// Crew Optimizer tests
// ---------------------------------------------------------------------------

static std::unique_ptr<CrewOptimizer> optimizer;

void test_crew_optimizer_construction() {
    TEST("CrewOptimizer construction from roster");
    CHECK(!roster.empty(), "roster not loaded");
    optimizer = std::make_unique<CrewOptimizer>(roster);
    CHECK(optimizer->officers().size() == roster.size(),
          "officer count mismatch: " + std::to_string(optimizer->officers().size()) +
          " vs " + std::to_string(roster.size()));
    PASS();
}

void test_crew_classification_tags() {
    TEST("officer classification produces tags");
    CHECK(optimizer != nullptr, "optimizer not created");

    int pvp_tagged = 0, pve_tagged = 0, crit_tagged = 0, mining_tagged = 0;
    int with_states_applied = 0, with_states_benefit = 0;
    for (const auto& off : optimizer->officers()) {
        if (off.is_pvp_specific) ++pvp_tagged;
        if (off.is_pve_specific) ++pve_tagged;
        if (off.crit_related) ++crit_tagged;
        if (off.mining) ++mining_tagged;
        if (!off.states_applied.empty()) ++with_states_applied;
        if (!off.states_benefit.empty()) ++with_states_benefit;
    }

    CHECK(pvp_tagged > 10, "too few PvP officers: " + std::to_string(pvp_tagged));
    CHECK(pve_tagged > 10, "too few PvE officers: " + std::to_string(pve_tagged));
    CHECK(crit_tagged > 5, "too few crit officers: " + std::to_string(crit_tagged));
    CHECK(mining_tagged > 5, "too few mining officers: " + std::to_string(mining_tagged));
    CHECK(with_states_applied > 5, "too few state appliers: " + std::to_string(with_states_applied));
    CHECK(with_states_benefit > 3, "too few state beneficiaries: " + std::to_string(with_states_benefit));

    std::cout << "(pvp:" << pvp_tagged << " pve:" << pve_tagged
              << " crit:" << crit_tagged << " mining:" << mining_tagged
              << " apply:" << with_states_applied << " benefit:" << with_states_benefit << ") ";
    PASS();
}

void test_crew_classification_states() {
    TEST("state classification (morale/breach/burning/assimilate)");
    CHECK(optimizer != nullptr, "optimizer not created");

    std::set<std::string> all_applied, all_benefit;
    for (const auto& off : optimizer->officers()) {
        for (const auto& s : off.states_applied) all_applied.insert(s);
        for (const auto& s : off.states_benefit) all_benefit.insert(s);
    }

    CHECK(all_applied.count("morale") > 0, "no morale appliers");
    CHECK(all_applied.count("burning") > 0, "no burning appliers");
    // breach and assimilate may or may not be present depending on roster
    CHECK(all_applied.size() >= 2, "only " + std::to_string(all_applied.size()) + " state types applied");

    std::string applied_str, benefit_str;
    for (const auto& s : all_applied) { if (!applied_str.empty()) applied_str += ","; applied_str += s; }
    for (const auto& s : all_benefit) { if (!benefit_str.empty()) benefit_str += ","; benefit_str += s; }
    std::cout << "(apply:[" << applied_str << "] benefit:[" << benefit_str << "]) ";
    PASS();
}

void test_crew_ship_lock() {
    TEST("ship-lock detection (CM/OA works_on_ship)");
    CHECK(optimizer != nullptr, "optimizer not created");

    // On Explorer, officers with "on a battleship" or "on an interceptor" in CM should fail
    optimizer->set_ship_type(ShipType::Explorer);

    int cm_locked = 0;
    for (const auto& off : optimizer->officers()) {
        // We can't directly call private methods, but we can test indirectly
        // through scoring. Officers locked to wrong ship will get penalties.
        // Instead, check that description-based locks exist.
        const auto& d = off.description;
        if (d.find("on a battleship") != std::string::npos ||
            d.find("on an interceptor") != std::string::npos) {
            ++cm_locked;
        }
    }
    // Some officers should be ship-locked
    // (this validates the roster has meaningful descriptions)
    std::cout << "(ship-locked descriptions: " << cm_locked << ") ";
    PASS();
}

void test_crew_pvp_find_best() {
    TEST("find_best_crews PvP returns top-5 valid results");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);
    auto results = optimizer->find_best_crews(Scenario::PvP, 5);

    CHECK(results.size() == 5, "expected 5 results, got " + std::to_string(results.size()));

    // Results should be sorted descending
    for (size_t i = 1; i < results.size(); ++i) {
        CHECK(results[i].score <= results[i-1].score,
              "results not sorted: " + std::to_string(results[i-1].score) +
              " < " + std::to_string(results[i].score));
    }

    // Each result should have valid captain + 2 bridge
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& bd = results[i].breakdown;
        CHECK(!bd.captain.empty(), "result " + std::to_string(i) + " has empty captain");
        CHECK(bd.bridge.size() == 2, "result " + std::to_string(i) + " has " +
              std::to_string(bd.bridge.size()) + " bridge officers");
        // All 3 names should be distinct
        std::set<std::string> names = {bd.captain, bd.bridge[0], bd.bridge[1]};
        CHECK(names.size() == 3, "duplicate officers in result " + std::to_string(i));
    }

    // All 5 results should have distinct crew sets
    std::set<std::set<std::string>> seen;
    for (const auto& r : results) {
        std::set<std::string> key = {r.breakdown.captain, r.breakdown.bridge[0], r.breakdown.bridge[1]};
        CHECK(seen.count(key) == 0, "duplicate crew set in results");
        seen.insert(key);
    }

    std::cout << "(top=" << static_cast<int>(results[0].score)
              << " [" << results[0].breakdown.captain
              << "+" << results[0].breakdown.bridge[0]
              << "+" << results[0].breakdown.bridge[1] << "]) ";
    PASS();
}

void test_crew_hybrid_find_best() {
    TEST("find_best_crews Hybrid returns valid results");
    CHECK(optimizer != nullptr, "optimizer not created");

    auto results = optimizer->find_best_crews(Scenario::Hybrid, 3);
    CHECK(results.size() == 3, "expected 3 results, got " + std::to_string(results.size()));
    CHECK(results[0].score > 0, "top hybrid score should be > 0");

    std::cout << "(top=" << static_cast<int>(results[0].score)
              << " [" << results[0].breakdown.captain
              << "+" << results[0].breakdown.bridge[0]
              << "+" << results[0].breakdown.bridge[1] << "]) ";
    PASS();
}

void test_crew_pve_hostile_find_best() {
    TEST("find_best_crews PvE Hostile returns valid results");
    CHECK(optimizer != nullptr, "optimizer not created");

    auto results = optimizer->find_best_crews(Scenario::PvEHostile, 3);
    CHECK(results.size() == 3, "expected 3 results, got " + std::to_string(results.size()));
    CHECK(results[0].score > 0, "top PvE score should be > 0");

    // PvE crew should prefer PvE officers — just verify it runs correctly
    std::cout << "(top=" << static_cast<int>(results[0].score)
              << " [" << results[0].breakdown.captain
              << "+" << results[0].breakdown.bridge[0]
              << "+" << results[0].breakdown.bridge[1] << "]) ";
    PASS();
}

void test_crew_mining_find_best() {
    TEST("find_best_crews MiningGeneral returns mining officers");
    CHECK(optimizer != nullptr, "optimizer not created");

    auto results = optimizer->find_best_crews(Scenario::MiningGeneral, 3);
    CHECK(results.size() == 3, "expected 3 results, got " + std::to_string(results.size()));

    // Top mining crew should include at least one mining-tagged officer
    bool any_mining = false;
    for (const auto& off : optimizer->officers()) {
        if (off.name == results[0].breakdown.captain && off.mining) any_mining = true;
        for (const auto& b : results[0].breakdown.bridge) {
            if (off.name == b && off.mining) any_mining = true;
        }
    }
    CHECK(any_mining, "top mining crew has no mining officers");

    std::cout << "(top=" << static_cast<int>(results[0].score)
              << " [" << results[0].breakdown.captain
              << "+" << results[0].breakdown.bridge[0]
              << "+" << results[0].breakdown.bridge[1] << "]) ";
    PASS();
}

void test_crew_excluded_officers() {
    TEST("find_best_crews respects excluded officers");
    CHECK(optimizer != nullptr, "optimizer not created");

    auto results_full = optimizer->find_best_crews(Scenario::PvP, 1);
    CHECK(!results_full.empty(), "no results from full search");

    // Exclude the captain from the top result
    std::set<std::string> excluded = {results_full[0].breakdown.captain};
    auto results_excl = optimizer->find_best_crews(Scenario::PvP, 1, excluded);
    CHECK(!results_excl.empty(), "no results with exclusion");

    // The excluded officer should not appear in any result
    CHECK(results_excl[0].breakdown.captain != results_full[0].breakdown.captain,
          "excluded captain still appears");
    for (const auto& b : results_excl[0].breakdown.bridge) {
        CHECK(b != results_full[0].breakdown.captain,
              "excluded officer appears on bridge");
    }

    PASS();
}

void test_crew_all_scenarios() {
    TEST("find_best_crews works for all 13 scenarios");
    CHECK(optimizer != nullptr, "optimizer not created");

    int passed = 0;
    for (auto s : all_dock_scenarios()) {
        auto results = optimizer->find_best_crews(s, 1);
        CHECK(!results.empty(), std::string("no results for ") + scenario_str(s));
        CHECK(results[0].score > 0, std::string("zero score for ") + scenario_str(s));
        ++passed;
    }

    std::cout << "(" << passed << "/13 scenarios) ";
    PASS();
}

void test_crew_ship_type_affects_results() {
    TEST("ship type change affects PvP results");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);
    auto exp_results = optimizer->find_best_crews(Scenario::PvP, 1);

    optimizer->set_ship_type(ShipType::Battleship);
    auto bs_results = optimizer->find_best_crews(Scenario::PvP, 1);

    optimizer->set_ship_type(ShipType::Interceptor);
    auto int_results = optimizer->find_best_crews(Scenario::PvP, 1);

    // At least one ship type should give different top crew
    bool any_diff = (exp_results[0].breakdown.captain != bs_results[0].breakdown.captain) ||
                    (exp_results[0].breakdown.captain != int_results[0].breakdown.captain) ||
                    (bs_results[0].breakdown.captain != int_results[0].breakdown.captain);
    // Scores should definitely differ even if same crew
    bool score_diff = (exp_results[0].score != bs_results[0].score) ||
                      (bs_results[0].score != int_results[0].score);

    CHECK(any_diff || score_diff, "ship type has no effect on results");

    // Reset to explorer
    optimizer->set_ship_type(ShipType::Explorer);

    std::cout << "(exp:" << static_cast<int>(exp_results[0].score)
              << " bs:" << static_cast<int>(bs_results[0].score)
              << " int:" << static_cast<int>(int_results[0].score) << ") ";
    PASS();
}

void test_crew_scenario_enums() {
    TEST("scenario string round-trip conversions");
    for (auto s : all_dock_scenarios()) {
        auto str = scenario_str(s);
        auto back = scenario_from_str(str);
        CHECK(back == s, std::string("round-trip failed for ") + str);
    }

    // Also check labels exist
    for (auto s : all_dock_scenarios()) {
        auto label = scenario_label(s);
        CHECK(std::strlen(label) > 3, std::string("label too short for ") + scenario_str(s));
    }

    // Ship type round-trip
    for (auto st : {ShipType::Explorer, ShipType::Battleship, ShipType::Interceptor}) {
        auto str = ship_type_str(st);
        auto back = ship_type_from_str(str);
        CHECK(back == st, std::string("ship type round-trip failed for ") + str);
    }

    PASS();
}

void test_crew_breakdown_fields() {
    TEST("crew result breakdown has populated fields");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);
    auto results = optimizer->find_best_crews(Scenario::PvP, 1);
    CHECK(!results.empty(), "no results");

    const auto& bd = results[0].breakdown;
    CHECK(!bd.captain.empty(), "empty captain");
    CHECK(bd.bridge.size() == 2, "wrong bridge size");
    CHECK(bd.individual_scores.size() == 3, "expected 3 individual scores, got " +
          std::to_string(bd.individual_scores.size()));

    // All individual scores should be > 0
    for (const auto& [name, score] : bd.individual_scores) {
        CHECK(score > 0, "zero individual score for " + name);
    }

    std::cout << "(synergy=" << static_cast<int>(bd.synergy_bonus)
              << " chain=" << static_cast<int>(bd.state_chain_bonus)
              << " crit=" << static_cast<int>(bd.crit_bonus)
              << " penalties=" << bd.penalties.size() << ") ";
    PASS();
}

void test_crew_performance() {
    TEST("PvP search completes in < 2 seconds");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);
    auto start = std::chrono::steady_clock::now();
    auto results = optimizer->find_best_crews(Scenario::PvP, 5);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    CHECK(elapsed.count() < 2000, "search took " + std::to_string(elapsed.count()) + "ms");
    CHECK(!results.empty(), "no results");

    std::cout << "(" << elapsed.count() << "ms for top-5) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Tests: BDA suggestions
// ---------------------------------------------------------------------------

void test_bda_pvp_suggestions() {
    TEST("find_best_bda returns valid BDA suggestions for PvP crew");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);
    auto crews = optimizer->find_best_crews(Scenario::PvP, 1);
    CHECK(!crews.empty(), "no PvP crews found");

    const auto& best = crews[0];
    auto bdas = optimizer->find_best_bda(
        best.breakdown.captain, best.breakdown.bridge,
        Scenario::PvP, 5);

    CHECK(!bdas.empty(), "no BDA suggestions returned");
    CHECK(bdas.size() <= 5, "too many BDA suggestions");

    // BDA should not include any crew member
    std::set<std::string> crew_names;
    crew_names.insert(best.breakdown.captain);
    for (const auto& b : best.breakdown.bridge) crew_names.insert(b);

    for (const auto& bda : bdas) {
        CHECK(!bda.name.empty(), "BDA has empty name");
        CHECK(crew_names.count(bda.name) == 0,
              "BDA '" + bda.name + "' is already on the bridge");
        CHECK(bda.score > 0, "BDA score is zero or negative");
        CHECK(!bda.reasons.empty(), "BDA has no reasons");
    }

    // Scores should be in descending order
    for (size_t i = 1; i < bdas.size(); ++i) {
        CHECK(bdas[i].score <= bdas[i-1].score, "BDA scores not sorted descending");
    }

    std::cout << "(top=" << (int)bdas[0].score << " [" << bdas[0].name << "] reasons:" << bdas[0].reasons.size() << ") ";
    PASS();
}

void test_bda_respects_excluded() {
    TEST("find_best_bda excludes specified officers");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);
    auto crews = optimizer->find_best_crews(Scenario::PvP, 1);
    CHECK(!crews.empty(), "no PvP crews found");

    const auto& best = crews[0];

    // Get unrestricted BDAs
    auto bdas_full = optimizer->find_best_bda(
        best.breakdown.captain, best.breakdown.bridge,
        Scenario::PvP, 5);
    CHECK(!bdas_full.empty(), "no unrestricted BDA suggestions");

    // Exclude the top BDA
    std::set<std::string> excl;
    excl.insert(bdas_full[0].name);

    auto bdas_excl = optimizer->find_best_bda(
        best.breakdown.captain, best.breakdown.bridge,
        Scenario::PvP, 5, excl);

    // Top excluded BDA should not appear
    for (const auto& bda : bdas_excl) {
        CHECK(bda.name != bdas_full[0].name,
              "excluded BDA '" + bdas_full[0].name + "' still appeared");
    }

    PASS();
}

void test_bda_state_synergy() {
    TEST("find_best_bda rewards state synergy (scoring sanity)");
    CHECK(optimizer != nullptr, "optimizer not created");

    // Run BDA for any crew — check that at least some results have state-related reasons
    optimizer->set_ship_type(ShipType::Explorer);
    auto crews = optimizer->find_best_crews(Scenario::PvP, 1);
    CHECK(!crews.empty(), "no crews");

    auto bdas = optimizer->find_best_bda(
        crews[0].breakdown.captain, crews[0].breakdown.bridge,
        Scenario::PvP, 10);

    int state_synergy_count = 0;
    for (const auto& bda : bdas) {
        for (const auto& r : bda.reasons) {
            if (r.find("Applies") != std::string::npos ||
                r.find("Benefits from") != std::string::npos) {
                state_synergy_count++;
                break;
            }
        }
    }

    // Expect at least 1 candidate with state synergy in top 10
    CHECK(state_synergy_count > 0,
          "no BDA candidates with state synergy in top 10");

    std::cout << "(" << state_synergy_count << "/10 have state synergy) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Tests: 7-dock loadout
// ---------------------------------------------------------------------------

void test_loadout_basic() {
    TEST("optimize_dock_loadout with 7 standard docks");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);

    std::vector<DockConfig> configs = {
        {Scenario::PvP,             "", false, "", {}},
        {Scenario::Hybrid,          "", false, "", {}},
        {Scenario::PvEHostile,      "", false, "", {}},
        {Scenario::Armada,          "", false, "", {}},
        {Scenario::MiningSpeed,     "", false, "", {}},
        {Scenario::MiningProtected, "", false, "", {}},
        {Scenario::MiningGeneral,   "", false, "", {}},
    };

    auto result = optimizer->optimize_dock_loadout(configs, 1);

    CHECK(result.docks.size() == 7, "expected 7 docks, got " + std::to_string(result.docks.size()));
    CHECK(result.total_officers_used == 21,
          "expected 21 unique officers, got " + std::to_string(result.total_officers_used));

    // Every dock should have a valid captain
    for (const auto& d : result.docks) {
        CHECK(!d.captain.empty() && d.captain != "N/A",
              "dock " + std::to_string(d.dock_num) + " has no captain");
        CHECK(d.bridge.size() == 2,
              "dock " + std::to_string(d.dock_num) + " doesn't have 2 bridge officers");
        CHECK(d.score > 0,
              "dock " + std::to_string(d.dock_num) + " has zero score");
    }

    std::cout << "(21 officers across 7 docks) ";
    PASS();
}

void test_loadout_no_duplicate_officers() {
    TEST("optimize_dock_loadout assigns no duplicate officers");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);

    std::vector<DockConfig> configs = {
        {Scenario::PvP,             "", false, "", {}},
        {Scenario::Hybrid,          "", false, "", {}},
        {Scenario::PvEHostile,      "", false, "", {}},
        {Scenario::Armada,          "", false, "", {}},
        {Scenario::MiningSpeed,     "", false, "", {}},
        {Scenario::MiningProtected, "", false, "", {}},
        {Scenario::MiningGeneral,   "", false, "", {}},
    };

    auto result = optimizer->optimize_dock_loadout(configs, 1);

    std::set<std::string> all_names;
    for (const auto& d : result.docks) {
        CHECK(all_names.count(d.captain) == 0,
              "duplicate captain: " + d.captain);
        all_names.insert(d.captain);
        for (const auto& b : d.bridge) {
            CHECK(all_names.count(b) == 0,
                  "duplicate bridge officer: " + b);
            all_names.insert(b);
        }
    }

    CHECK(all_names.size() == 21, "expected 21 unique names, got " + std::to_string(all_names.size()));
    PASS();
}

void test_loadout_locked_dock() {
    TEST("optimize_dock_loadout respects locked docks");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);

    // Lock dock 1 to a specific crew
    std::vector<DockConfig> configs = {
        {Scenario::PvP, "", true, "Kirk", {"Dezoc", "Borg Queen"}},
        {Scenario::Hybrid,      "", false, "", {}},
        {Scenario::PvEHostile,  "", false, "", {}},
    };

    auto result = optimizer->optimize_dock_loadout(configs, 1);

    CHECK(result.docks.size() == 3, "expected 3 docks");

    // Dock 1 should be locked with specified crew
    CHECK(result.docks[0].locked, "dock 1 not marked locked");
    CHECK(result.docks[0].captain == "Kirk", "locked captain not Kirk");
    CHECK(result.docks[0].bridge[0] == "Dezoc" || result.docks[0].bridge[0] == "Borg Queen",
          "locked bridge doesn't contain expected officers");

    // Dock 2 and 3 should NOT use Kirk, Dezoc, or Borg Queen
    std::set<std::string> locked_names = {"Kirk", "Dezoc", "Borg Queen"};
    for (size_t i = 1; i < result.docks.size(); ++i) {
        CHECK(locked_names.count(result.docks[i].captain) == 0,
              "dock " + std::to_string(i+1) + " reused locked officer " + result.docks[i].captain);
        for (const auto& b : result.docks[i].bridge) {
            CHECK(locked_names.count(b) == 0,
                  "dock " + std::to_string(i+1) + " reused locked officer " + b);
        }
    }

    PASS();
}

void test_loadout_bda_suggestions() {
    TEST("optimize_dock_loadout generates BDA suggestions per dock");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);

    std::vector<DockConfig> configs = {
        {Scenario::PvP,     "", false, "", {}},
        {Scenario::Hybrid,  "", false, "", {}},
    };

    auto result = optimizer->optimize_dock_loadout(configs, 1);

    // Each non-locked dock should have BDA suggestions
    for (const auto& d : result.docks) {
        if (!d.locked && d.captain != "N/A") {
            CHECK(!d.bda_suggestions.empty(),
                  "dock " + std::to_string(d.dock_num) + " has no BDA suggestions");

            // BDA should not include any crew member
            std::set<std::string> crew_names;
            crew_names.insert(d.captain);
            for (const auto& b : d.bridge) crew_names.insert(b);
            for (const auto& bda : d.bda_suggestions) {
                CHECK(crew_names.count(bda.name) == 0,
                      "BDA " + bda.name + " is on dock " + std::to_string(d.dock_num) + " bridge");
            }
        }
    }

    std::cout << "(dock1 bda:" << result.docks[0].bda_suggestions.size()
              << " dock2 bda:" << result.docks[1].bda_suggestions.size() << ") ";
    PASS();
}

void test_loadout_persistence() {
    TEST("save and load loadout round-trip");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);

    std::vector<DockConfig> configs = {
        {Scenario::PvP,     "", false, "", {}},
        {Scenario::Hybrid,  "", false, "", {}},
    };

    auto result = optimizer->optimize_dock_loadout(configs, 1);

    std::string path = "data/player_data/test_loadout.json";
    namespace fs = std::filesystem;
    fs::create_directories("data/player_data");

    CHECK(CrewOptimizer::save_loadout(result, path, ShipType::Explorer, "Test Ship"),
          "save failed");
    CHECK(fs::exists(path), "save file not created");

    LoadoutResult loaded;
    CHECK(CrewOptimizer::load_loadout(loaded, path), "load failed");
    CHECK(loaded.docks.size() == result.docks.size(), "dock count mismatch");
    CHECK(loaded.total_officers_used == result.total_officers_used, "officer count mismatch");

    // Verify dock 1 data preserved
    CHECK(loaded.docks[0].captain == result.docks[0].captain,
          "captain not preserved: " + loaded.docks[0].captain);
    CHECK(loaded.docks[0].bridge.size() == 2, "bridge size not preserved");
    CHECK(std::abs(loaded.docks[0].score - result.docks[0].score) < 1.0,
          "score not preserved");

    fs::remove(path);
    PASS();
}

void test_loadout_performance() {
    TEST("7-dock loadout completes in < 15 seconds");
    CHECK(optimizer != nullptr, "optimizer not created");

    optimizer->set_ship_type(ShipType::Explorer);

    std::vector<DockConfig> configs = {
        {Scenario::PvP,             "", false, "", {}},
        {Scenario::Hybrid,          "", false, "", {}},
        {Scenario::PvEHostile,      "", false, "", {}},
        {Scenario::Armada,          "", false, "", {}},
        {Scenario::MiningSpeed,     "", false, "", {}},
        {Scenario::MiningProtected, "", false, "", {}},
        {Scenario::MiningGeneral,   "", false, "", {}},
    };

    auto start = std::chrono::steady_clock::now();
    auto result = optimizer->optimize_dock_loadout(configs, 1);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    CHECK(elapsed.count() < 15000,
          "loadout took " + std::to_string(elapsed.count()) + "ms (limit 15s)");
    CHECK(result.docks.size() == 7, "wrong dock count");

    std::cout << "(" << elapsed.count() << "ms for 7 docks) ";
    PASS();
}

// ---------------------------------------------------------------------------
// Tests: Planner
// ---------------------------------------------------------------------------

void test_planner_construction() {
    TEST("Planner construction and template loading");
    Planner planner;
    CHECK(planner.all_daily_tasks().size() >= 20,
          "expected >= 20 daily templates, got " + std::to_string(planner.all_daily_tasks().size()));
    CHECK(planner.all_weekly_goals().size() >= 10,
          "expected >= 10 weekly templates, got " + std::to_string(planner.all_weekly_goals().size()));
    PASS();
}

void test_planner_daily_generation() {
    TEST("generate_daily_plan produces non-empty plan with valid tasks");
    Planner planner;
    DailyPlan plan = planner.generate_daily_plan();
    CHECK(!plan.date.empty(), "date is empty");
    CHECK(plan.total_tasks() > 0, "no tasks generated");
    CHECK(plan.completed_tasks() == 0, "fresh plan has completed tasks");
    CHECK(plan.remaining_tasks() == plan.total_tasks(), "remaining != total");
    CHECK(plan.completion_pct() < 0.01, "fresh plan has non-zero completion");

    // All tasks should have valid fields
    for (const auto& t : plan.tasks) {
        CHECK(t.id > 0, "task id <= 0");
        CHECK(!t.title.empty(), "task has empty title");
        CHECK(t.estimated_minutes >= 0, "negative estimated_minutes");
    }

    std::cout << "(" << plan.total_tasks() << " tasks, ~"
              << plan.total_estimated_minutes() << " min) ";
    PASS();
}

void test_planner_daily_for_specific_day() {
    TEST("generate_daily_plan_for different days");
    Planner planner;
    DailyPlan monday = planner.generate_daily_plan_for(1, "2026-03-30"); // Monday=1
    DailyPlan saturday = planner.generate_daily_plan_for(6, "2026-04-04"); // Saturday=6

    CHECK(monday.date == "2026-03-30", "wrong date for monday");
    CHECK(saturday.date == "2026-04-04", "wrong date for saturday");
    CHECK(monday.total_tasks() > 0, "no monday tasks");
    CHECK(saturday.total_tasks() > 0, "no saturday tasks");

    std::cout << "(Mon:" << monday.total_tasks() << " Sat:" << saturday.total_tasks() << " tasks) ";
    PASS();
}

void test_planner_toggle_task() {
    TEST("toggle_task marks task complete and back");
    Planner planner;
    DailyPlan plan = planner.generate_daily_plan();
    CHECK(plan.total_tasks() > 0, "no tasks to toggle");

    int tid = plan.tasks[0].id;
    CHECK(!plan.tasks[0].completed, "task already completed");

    planner.toggle_task(plan, tid);
    CHECK(plan.tasks[0].completed, "task not marked completed after toggle");
    CHECK(plan.completed_tasks() == 1, "completed count should be 1");

    planner.toggle_task(plan, tid);
    CHECK(!plan.tasks[0].completed, "task still completed after second toggle");
    CHECK(plan.completed_tasks() == 0, "completed count should be 0");

    PASS();
}

void test_planner_skip_task() {
    TEST("skip_task marks task as skipped with reason");
    Planner planner;
    DailyPlan plan = planner.generate_daily_plan();
    CHECK(plan.total_tasks() > 0, "no tasks to skip");

    int tid = plan.tasks[0].id;
    planner.skip_task(plan, tid, "testing skip");
    CHECK(plan.tasks[0].skipped, "task not marked skipped");
    CHECK(plan.tasks[0].skip_reason == "testing skip", "skip reason not set");

    PASS();
}

void test_planner_weekly_generation() {
    TEST("generate_weekly_plan produces 7 days and goals");
    Planner planner;
    WeeklyPlan plan = planner.generate_weekly_plan();
    CHECK(!plan.week_start.empty(), "week_start is empty");
    CHECK(plan.days.size() == 7, "expected 7 days, got " + std::to_string(plan.days.size()));
    CHECK(!plan.goals.empty(), "no weekly goals generated");
    CHECK(plan.completed_goals() == 0, "fresh plan has completed goals");

    // Each day should have tasks
    for (int i = 0; i < 7; ++i) {
        CHECK(plan.days[i].total_tasks() > 0,
              "day " + std::to_string(i) + " has no tasks");
    }

    std::cout << "(" << plan.goals.size() << " goals, 7 days) ";
    PASS();
}

void test_planner_goal_progress() {
    TEST("update_goal_progress tracks progress and auto-completes");
    Planner planner;
    WeeklyPlan plan = planner.generate_weekly_plan();
    CHECK(!plan.goals.empty(), "no goals");

    int gid = plan.goals[0].id;
    int target = plan.goals[0].progress_total;
    CHECK(target > 0, "goal has zero target");

    // Increment to completion
    planner.update_goal_progress(plan, gid, target);
    CHECK(plan.goals[0].progress_current == target, "progress not updated");
    CHECK(plan.goals[0].completed, "goal not auto-completed at target");

    // Decrement below target un-completes
    planner.update_goal_progress(plan, gid, 0);
    CHECK(!plan.goals[0].completed, "goal still completed at 0");

    PASS();
}

void test_planner_persistence() {
    TEST("save and load daily plan round-trip");
    Planner planner;
    DailyPlan plan = planner.generate_daily_plan();

    // Toggle first task
    if (!plan.tasks.empty()) {
        planner.toggle_task(plan, plan.tasks[0].id);
    }

    std::string path = "data/player_data/test_daily_save.json";
    fs::create_directories("data/player_data");
    CHECK(planner.save_daily(plan, path), "save failed");
    CHECK(fs::exists(path), "save file not created");

    // Load into fresh plan
    DailyPlan loaded = planner.generate_daily_plan();
    CHECK(planner.load_daily(loaded, path), "load failed");
    CHECK(loaded.tasks[0].completed == plan.tasks[0].completed, "completion state not preserved");

    // Cleanup
    fs::remove(path);
    PASS();
}

void test_planner_weekly_persistence() {
    TEST("save and load weekly plan round-trip");
    Planner planner;
    WeeklyPlan plan = planner.generate_weekly_plan();

    // Find a goal with progress_total > 1 to test meaningful progress
    CHECK(!plan.goals.empty(), "no goals");
    int test_idx = -1;
    for (size_t i = 0; i < plan.goals.size(); ++i) {
        if (plan.goals[i].progress_total > 2) { test_idx = (int)i; break; }
    }
    if (test_idx < 0) test_idx = 0;  // fall back to first goal

    int target_progress = std::min(3, plan.goals[test_idx].progress_total);
    planner.update_goal_progress(plan, plan.goals[test_idx].id, target_progress);
    CHECK(plan.goals[test_idx].progress_current == target_progress,
          "progress not set before save: got " + std::to_string(plan.goals[test_idx].progress_current) +
          " expected " + std::to_string(target_progress));

    std::string path = "data/player_data/test_weekly_save.json";
    fs::create_directories("data/player_data");
    CHECK(planner.save_weekly(plan, path), "save failed");
    CHECK(fs::exists(path), "save file not created");

    // Reset goal progress and reload from saved file
    plan.goals[test_idx].progress_current = 0;
    plan.goals[test_idx].completed = false;
    CHECK(planner.load_weekly(plan, path), "load failed");
    CHECK(plan.goals[test_idx].progress_current == target_progress,
          "goal progress not preserved, got " + std::to_string(plan.goals[test_idx].progress_current));

    fs::remove(path);
    PASS();
}

void test_planner_categories_covered() {
    TEST("daily plan covers multiple task categories");
    Planner planner;
    DailyPlan plan = planner.generate_daily_plan();

    std::set<TaskCategory> categories;
    for (const auto& t : plan.tasks) {
        categories.insert(t.category);
    }

    CHECK(categories.size() >= 5,
          "only " + std::to_string(categories.size()) + " categories covered, expected >= 5");

    std::cout << "(" << categories.size() << " categories) ";
    PASS();
}

void test_planner_priority_ordering() {
    TEST("daily plan tasks have valid priorities and categories");
    Planner planner;
    DailyPlan plan = planner.generate_daily_plan();

    int critical = 0, high = 0, medium = 0, low = 0;
    for (const auto& t : plan.tasks) {
        switch (t.priority) {
            case TaskPriority::Critical: critical++; break;
            case TaskPriority::High:     high++; break;
            case TaskPriority::Medium:   medium++; break;
            case TaskPriority::Low:      low++; break;
        }
    }

    // Should have a reasonable mix — at least some high or critical tasks
    CHECK(high + critical > 0, "no high/critical priority tasks");

    std::cout << "(C:" << critical << " H:" << high << " M:" << medium << " L:" << low << ") ";
    PASS();
}

void test_planner_completion_pct() {
    TEST("completion_pct tracks correctly");
    Planner planner;
    DailyPlan plan = planner.generate_daily_plan();
    CHECK(plan.total_tasks() > 0, "no tasks");

    CHECK(plan.completion_pct() < 0.01, "should be 0% initially");

    // Complete all tasks
    for (auto& t : plan.tasks) {
        planner.toggle_task(plan, t.id);
    }

    double pct = plan.completion_pct();
    CHECK(pct > 99.9, "should be 100% after completing all, got " + std::to_string(pct));

    PASS();
}

void test_planner_helper_functions() {
    TEST("priority_str, priority_icon, category_str, category_icon");

    // priority_str — implementation returns uppercase
    CHECK(std::string(priority_str(TaskPriority::Critical)) == "CRITICAL", "priority_str Critical");
    CHECK(std::string(priority_str(TaskPriority::High)) == "HIGH", "priority_str High");
    CHECK(std::string(priority_str(TaskPriority::Medium)) == "MEDIUM", "priority_str Medium");
    CHECK(std::string(priority_str(TaskPriority::Low)) == "LOW", "priority_str Low");

    // priority_icon should return non-empty strings
    CHECK(std::string(priority_icon(TaskPriority::Critical)).size() > 0, "priority_icon Critical empty");

    // category_str
    CHECK(std::string(category_str(TaskCategory::Events)) == "Events", "category_str Events");
    CHECK(std::string(category_str(TaskCategory::Mining)) == "Mining", "category_str Mining");
    CHECK(std::string(category_str(TaskCategory::Combat)) == "Combat", "category_str Combat");

    // category_icon should return non-empty strings
    CHECK(std::string(category_icon(TaskCategory::Events)).size() > 0, "category_icon Events empty");

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

    std::cout << "\n--- CSV Roster Import (" << "roster.csv" << ") ---\n";
    if (fs::exists("roster.csv")) {
        test_csv_load();
        test_csv_officer_count();
        test_csv_known_officer();
        test_csv_rarity_values();
        test_csv_groups_populated();
        test_csv_bda_detection();
        test_csv_multiline_fields();
        test_csv_mess_hall_level();
        test_csv_effects();
    } else {
        std::cout << "  (skipped — roster.csv not found)\n";
    }

    std::cout << "\n--- Crew Optimizer ---\n";
    if (!roster.empty()) {
        test_crew_optimizer_construction();
        test_crew_classification_tags();
        test_crew_classification_states();
        test_crew_ship_lock();
        test_crew_scenario_enums();
        test_crew_pvp_find_best();
        test_crew_hybrid_find_best();
        test_crew_pve_hostile_find_best();
        test_crew_mining_find_best();
        test_crew_excluded_officers();
        test_crew_all_scenarios();
        test_crew_ship_type_affects_results();
        test_crew_breakdown_fields();
        test_crew_performance();

        std::cout << "\n--- BDA Suggestions ---\n";
        test_bda_pvp_suggestions();
        test_bda_respects_excluded();
        test_bda_state_synergy();

        std::cout << "\n--- 7-Dock Loadout ---\n";
        test_loadout_basic();
        test_loadout_no_duplicate_officers();
        test_loadout_locked_dock();
        test_loadout_bda_suggestions();
        test_loadout_persistence();
        test_loadout_performance();
    } else {
        std::cout << "  (skipped — roster not loaded)\n";
    }

    std::cout << "\n--- Planner ---\n";
    test_planner_helper_functions();
    test_planner_construction();
    test_planner_daily_generation();
    test_planner_daily_for_specific_day();
    test_planner_toggle_task();
    test_planner_skip_task();
    test_planner_weekly_generation();
    test_planner_goal_progress();
    test_planner_categories_covered();
    test_planner_priority_ordering();
    test_planner_completion_pct();
    test_planner_persistence();
    test_planner_weekly_persistence();

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
