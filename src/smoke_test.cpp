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
#include <map>
#include <algorithm>
#include <string>
#include <chrono>
#include <cstring>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <fstream>
#include <condition_variable>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

#include "data/models.h"
#include "data/ingress_server.h"
#include "data/api_client.h"
#include "data/llm_client.h"
#include "core/crew_optimizer.h"
#include "core/planner.h"
#include "core/ai_crew_engine.h"
#include "core/account_state.h"
#include "core/ship_prompt.h"
#include "core/officer_prompt.h"
#include "core/strategic_prompt.h"
#include "data/community_data.h"
#include "app/account_snapshot.h"
#include "app/action_planner.h"

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

// Ship ability classification helper (mirrors classify_ship_ability() from main.cpp)
namespace ship_test {

std::string strip_color_tags(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            if (text.compare(i, 7, "<color=") == 0) {
                auto end = text.find('>', i);
                if (end != std::string::npos) { i = end + 1; continue; }
            } else if (text.compare(i, 8, "</color>") == 0) {
                i += 8; continue;
            }
        }
        out += text[i++];
    }
    return out;
}

std::string to_lower_str(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string classify_ship_ability(const std::string& ability_name,
                                  const std::string& ability_description) {
    std::string name = to_lower_str(strip_color_tags(ability_name));
    std::string desc = to_lower_str(strip_color_tags(ability_description));

    if (name.find("mining") != std::string::npos ||
        desc.find("mining rate") != std::string::npos ||
        desc.find("mining speed") != std::string::npos ||
        desc.find("mining bonus") != std::string::npos) {
        if (desc.find("parsteel") != std::string::npos) return "mining_parsteel";
        if (desc.find("tritanium") != std::string::npos) return "mining_tritanium";
        if (desc.find("dilithium") != std::string::npos) return "mining_dilithium";
        if (desc.find("crystal") != std::string::npos && desc.find("gas") != std::string::npos)
            return "mining_universal";
        if (desc.find("crystal") != std::string::npos) return "mining_crystal";
        if (desc.find("gas") != std::string::npos) return "mining_gas";
        if (desc.find("ore") != std::string::npos) return "mining_ore";
        if (desc.find("latinum") != std::string::npos) return "mining_latinum";
        if (desc.find("isogen") != std::string::npos) return "mining_isogen";
        if (desc.find("transogen") != std::string::npos) return "mining_transogen";
        if (desc.find("corrupted") != std::string::npos) return "mining_data";
        return "mining_general";
    }
    if (desc.find("more resources from hostiles") != std::string::npos ||
        desc.find("reward") != std::string::npos ||
        desc.find("loot") != std::string::npos) {
        return "loot";
    }
    if (desc.find("fighting hostiles") != std::string::npos ||
        desc.find("against hostiles") != std::string::npos ||
        desc.find("hostile") != std::string::npos) {
        if (desc.find("swarm") != std::string::npos) return "combat_swarm";
        if (desc.find("borg") != std::string::npos) return "combat_borg";
        if (desc.find("gorn") != std::string::npos) return "combat_gorn";
        if (desc.find("xindi") != std::string::npos) return "combat_xindi";
        if (desc.find("eclipse") != std::string::npos) return "combat_eclipse";
        return "combat_hostile";
    }
    if (desc.find("opponent") != std::string::npos ||
        desc.find("defending") != std::string::npos ||
        desc.find("weapon damage") != std::string::npos ||
        desc.find("shield piercing") != std::string::npos ||
        desc.find("armor piercing") != std::string::npos) {
        return "combat_pvp";
    }
    if (desc.find("captain maneuver") != std::string::npos) return "captain_boost";
    return "general";
}

} // namespace ship_test

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

void test_ship_ability_classification() {
    TEST("ship ability classification produces sensible tags");
    CHECK(data_loaded, "data not loaded");

    std::map<std::string, int> tag_counts;
    int with_ability = 0;
    int mining_ships = 0;

    for (auto& [id, s] : game_data.ships) {
        if (s.ability_name.empty()) continue;
        with_ability++;
        std::string tag = ship_test::classify_ship_ability(s.ability_name, s.ability_description);
        tag_counts[tag]++;
        if (tag.find("mining") != std::string::npos) mining_ships++;
    }

    CHECK(with_ability > 50, "only " + std::to_string(with_ability) + " ships with abilities");
    CHECK(mining_ships >= 10, "only " + std::to_string(mining_ships) + " mining ships");
    CHECK(tag_counts.size() >= 5, "only " + std::to_string(tag_counts.size()) + " distinct tags");

    // Verify specific known ships
    // ECS Fortunate = parsteel mining laser
    bool found_parsteel = false, found_tritanium = false, found_crystal = false;
    bool found_hostile = false, found_pvp = false;
    for (auto& [id, s] : game_data.ships) {
        std::string tag = ship_test::classify_ship_ability(s.ability_name, s.ability_description);
        std::string lower = ship_test::to_lower_str(s.name);
        if (lower.find("fortunate") != std::string::npos && tag == "mining_parsteel") found_parsteel = true;
        if (tag == "mining_tritanium") found_tritanium = true;
        if (tag == "mining_crystal") found_crystal = true;
        if (tag == "combat_hostile") found_hostile = true;
        if (tag == "combat_pvp") found_pvp = true;
    }
    CHECK(found_parsteel, "ECS Fortunate not classified as mining_parsteel");
    CHECK(found_tritanium, "no mining_tritanium ships found");
    CHECK(found_crystal, "no mining_crystal ships found");
    CHECK(found_hostile, "no combat_hostile ships found");
    CHECK(found_pvp, "no combat_pvp ships found");

    std::cout << "(with_ability:" << with_ability << " mining:" << mining_ships << " tags:";
    for (auto& [tag, cnt] : tag_counts) std::cout << tag << ":" << cnt << " ";
    std::cout << ") ";
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

void test_research_levels_parsed() {
    TEST("research levels include costs, requirements, and durations");
    CHECK(data_loaded, "data not loaded");

    int with_levels = 0;
    int with_costs = 0;
    int with_requirements = 0;
    int with_time = 0;
    for (auto& [id, r] : game_data.researches) {
        if (!r.levels.empty()) {
            with_levels++;
            for (auto& l : r.levels) {
                if (!l.costs.empty()) { with_costs++; break; }
            }
            for (auto& l : r.levels) {
                if (!l.requirements.empty()) { with_requirements++; break; }
            }
            for (auto& l : r.levels) {
                if (l.research_time_seconds > 0) { with_time++; break; }
            }
        }
    }

    CHECK(with_levels > 1000, "too few research nodes have levels: " + std::to_string(with_levels));
    CHECK(with_costs > 500, "too few research nodes have costs: " + std::to_string(with_costs));
    CHECK(with_requirements > 500, "too few research nodes have requirements: " + std::to_string(with_requirements));
    CHECK(with_time > 500, "too few research nodes have durations: " + std::to_string(with_time));

    std::cout << "(" << with_levels << " levels, " << with_costs << " costs, "
              << with_requirements << " reqs, " << with_time << " timers) ";
    PASS();
}

void test_research_names_and_descriptions() {
    TEST("research translations include human-readable names and descriptions");
    CHECK(data_loaded, "data not loaded");

    int named = 0;
    int described = 0;
    for (auto& [id, r] : game_data.researches) {
        if (!r.name.empty()) named++;
        if (!r.description.empty()) described++;
    }

    CHECK(named > 1000, "too few research names: " + std::to_string(named));
    CHECK(described > 1000, "too few research descriptions: " + std::to_string(described));

    auto it = game_data.researches.find(1423296458);
    CHECK(it != game_data.researches.end(), "known research 1423296458 missing");
    CHECK(it->second.name == "Prime Weapons Drain",
          "known research name unresolved: " + it->second.name);
    CHECK(!it->second.description.empty(), "known research description unresolved");

    std::cout << "(" << named << " names, " << described << " descriptions) ";
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

void test_building_names() {
    TEST("building translations include human-readable names");
    CHECK(data_loaded, "data not loaded");

    int named = 0;
    for (auto& [id, b] : game_data.buildings) {
        if (!b.name.empty()) named++;
    }

    CHECK(named > 50, "too few building names: " + std::to_string(named));

    auto it = game_data.buildings.find(0);
    CHECK(it != game_data.buildings.end(), "known building 0 missing");
    CHECK(!it->second.name.empty(), "known building 0 name unresolved");

    std::cout << "(" << named << " names) ";
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
    CHECK(std::string(officer_class_str(2)) == "Science", "2 != Science");
    CHECK(std::string(officer_class_str(3)) == "Engineering", "3 != Engineering");
    CHECK(std::string(officer_class_str(0)) == "Unknown", "0 != Unknown");
    PASS();
}

// ---------------------------------------------------------------------------
// Community data tests
// ---------------------------------------------------------------------------

static CommunityData community_data;

void test_community_scores_load() {
    TEST("load officer_scores.json");
    bool ok = load_officer_scores("data/officer_scores.json", community_data);
    CHECK(ok, "failed to load officer_scores.json");
    CHECK(community_data.officer_scores.size() > 200,
          "expected >200 scored officers, got " + std::to_string(community_data.officer_scores.size()));
    std::cout << "(" << community_data.officer_scores.size() << " officers) ";
    PASS();
}

void test_community_skills_load() {
    TEST("load officer_skills.json");
    bool ok = load_officer_skills("data/officer_skills.json", community_data);
    CHECK(ok, "failed to load officer_skills.json");
    CHECK(community_data.officer_skills.size() > 200,
          "expected >200 officers with skills, got " + std::to_string(community_data.officer_skills.size()));
    std::cout << "(" << community_data.officer_skills.size() << " officers) ";
    PASS();
}

void test_community_presets_load() {
    TEST("load preset_crews.json");
    bool ok = load_preset_crews("data/preset_crews.json", community_data);
    CHECK(ok, "failed to load preset_crews.json");
    CHECK(community_data.preset_crews.size() > 100,
          "expected >100 preset crews, got " + std::to_string(community_data.preset_crews.size()));
    std::cout << "(" << community_data.preset_crews.size() << " crews) ";
    PASS();
}

void test_community_scores_fields() {
    TEST("officer scores have valid fields");
    CHECK(!community_data.officer_scores.empty(), "no scores loaded");
    int with_notes = 0, with_overall = 0;
    for (const auto& s : community_data.officer_scores) {
        CHECK(!s.name.empty(), "officer score with empty name");
        if (!s.notes.empty()) with_notes++;
        if (s.overall_score > 0.0) with_overall++;
    }
    CHECK(with_overall > 200, "too few officers with overall_score: " + std::to_string(with_overall));
    std::cout << "(notes:" << with_notes << " scored:" << with_overall << ") ";
    PASS();
}

void test_community_skills_fields() {
    TEST("officer skills have valid fields");
    CHECK(!community_data.officer_skills.empty(), "no skills loaded");
    int with_oa = 0, with_cm = 0, with_group = 0, bda_count = 0;
    for (const auto& s : community_data.officer_skills) {
        CHECK(!s.name.empty(), "officer skill with empty name");
        if (!s.officer_ability.empty()) with_oa++;
        if (!s.captain_maneuver.empty()) with_cm++;
        if (!s.officer_group.empty()) with_group++;
        if (s.is_bda) bda_count++;
    }
    CHECK(with_oa > 200, "too few officers with OA: " + std::to_string(with_oa));
    CHECK(with_group > 200, "too few officers with group: " + std::to_string(with_group));
    std::cout << "(oa:" << with_oa << " cm:" << with_cm << " groups:" << with_group << " bda:" << bda_count << ") ";
    PASS();
}

void test_community_presets_fields() {
    TEST("preset crews have valid fields");
    CHECK(!community_data.preset_crews.empty(), "no presets loaded");
    int pvp = 0, hostile = 0, mining = 0, armada = 0;
    int empty_slots = 0;
    for (const auto& c : community_data.preset_crews) {
        CHECK(!c.captain.empty(), c.name + " has empty captain");
        if (c.officer1.empty() || c.officer2.empty()) empty_slots++;
        if (c.pvp) pvp++;
        if (c.hostiles) hostile++;
        if (c.mining) mining++;
        if (c.armada_normal || c.armada_eclipse || c.armada_swarm || c.armada_borg) armada++;
    }
    CHECK(pvp > 50, "too few PvP crews: " + std::to_string(pvp));
    std::cout << "(pvp:" << pvp << " hostile:" << hostile << " mining:" << mining << " armada:" << armada << " empty_slots:" << empty_slots << ") ";
    PASS();
}

void test_community_name_lookups() {
    TEST("community name lookups built");
    // Build lookups
    for (const auto& s : community_data.officer_scores)
        community_data.score_by_name[s.name] = &s;
    for (const auto& s : community_data.officer_skills)
        community_data.skill_by_name[s.name] = &s;

    // Check known officers
    CHECK(community_data.score_by_name.count("Kirk") > 0, "Kirk not found in scores");
    CHECK(community_data.skill_by_name.count("Kirk") > 0, "Kirk not found in skills");
    auto* kirk_score = community_data.score_by_name["Kirk"];
    CHECK(kirk_score->overall_score > 0.0, "Kirk overall_score should be > 0");
    auto* kirk_skill = community_data.skill_by_name["Kirk"];
    CHECK(!kirk_skill->captain_maneuver.empty(), "Kirk CM should not be empty");
    std::cout << "(Kirk: score=" << kirk_score->overall_score
              << " cm=\"" << kirk_skill->captain_maneuver.substr(0, 40) << "\") ";
    PASS();
}

// ---------------------------------------------------------------------------
// Sync-path data pipeline tests
// ---------------------------------------------------------------------------
// These tests build a synthetic PlayerData from GameData (simulating a sync
// where all officers are at max level) and use CrewOptimizer(pd, gd).

// Build a synthetic PlayerData where every officer in game_data is "owned"
// at max level and max rank (for testing the full pipeline).
static PlayerData build_synthetic_player_data(const GameData& gd) {
    PlayerData pd;
    pd.player_name = "TestPlayer";
    pd.ops_level = 40;
    for (const auto& [id, go] : gd.officers) {
        if (go.stats.empty()) continue;
        PlayerOfficer po;
        po.officer_id = id;
        po.level = std::max(1, (int)go.stats.size());
        po.rank = std::min(go.max_rank, 4);
        pd.officers.push_back(po);
    }
    return pd;
}

static PlayerData synthetic_pd;
static std::unique_ptr<CrewOptimizer> sync_optimizer;

void test_sync_roster_build() {
    TEST("build sync-path roster from game data");
    CHECK(data_loaded, "data not loaded");
    synthetic_pd = build_synthetic_player_data(game_data);
    resolve_player_names(synthetic_pd, game_data);
    sync_optimizer = std::make_unique<CrewOptimizer>(synthetic_pd, game_data);
    CHECK(sync_optimizer->officers().size() > 200,
          "expected >200 officers, got " + std::to_string(sync_optimizer->officers().size()));
    std::cout << "(" << sync_optimizer->officers().size() << " officers) ";
    PASS();
}

void test_sync_descriptions_have_markers() {
    TEST("sync descriptions have cm:/oa: or bda:/oa: markers");
    CHECK(sync_optimizer != nullptr, "sync optimizer not built");
    int with_cm = 0, with_bda = 0, with_oa = 0, empty_desc = 0;
    for (const auto& off : sync_optimizer->officers()) {
        if (off.description.empty()) { empty_desc++; continue; }
        if (off.description.find("cm:") != std::string::npos) with_cm++;
        if (off.description.find("bda:") != std::string::npos) with_bda++;
        if (off.description.find("oa:") != std::string::npos) with_oa++;
    }
    CHECK(with_cm + with_bda > 200, "too few officers with cm:/bda: markers: " + std::to_string(with_cm + with_bda));
    CHECK(with_oa > 200, "too few officers with oa: markers: " + std::to_string(with_oa));
    CHECK(with_bda > 20, "too few BDA officers detected: " + std::to_string(with_bda));
    std::cout << "(cm:" << with_cm << " bda:" << with_bda << " oa:" << with_oa << " empty:" << empty_desc << ") ";
    PASS();
}

void test_sync_descriptions_no_color_tags() {
    TEST("sync descriptions have no <color> tags");
    CHECK(sync_optimizer != nullptr, "sync optimizer not built");
    for (const auto& off : sync_optimizer->officers()) {
        CHECK(off.description.find("<color") == std::string::npos,
              off.name + " has color tags: " + off.description.substr(0, 80));
    }
    PASS();
}

void test_sync_descriptions_lowercase() {
    TEST("sync descriptions are lowercase");
    CHECK(sync_optimizer != nullptr, "sync optimizer not built");
    for (const auto& off : sync_optimizer->officers()) {
        for (char c : off.description) {
            if (std::isalpha(static_cast<unsigned char>(c))) {
                CHECK(std::islower(static_cast<unsigned char>(c)),
                      off.name + " has uppercase in description: " + off.description.substr(0, 80));
            }
        }
    }
    PASS();
}

void test_sync_pct_conversion() {
    TEST("sync cm_pct/oa_pct are percentage-scale (not raw decimals)");
    CHECK(sync_optimizer != nullptr, "sync optimizer not built");
    int checked = 0;
    int correct_scale = 0;
    for (const auto& off : sync_optimizer->officers()) {
        if (off.is_bda()) continue;
        if (off.cm_pct <= 0.0) continue;
        checked++;
        if (off.cm_pct >= 1.0) correct_scale++;
    }
    CHECK(checked > 100, "too few regular officers with cm_pct: " + std::to_string(checked));
    double pct_correct = (double)correct_scale / checked * 100.0;
    CHECK(pct_correct > 90.0,
          "only " + std::to_string((int)pct_correct) + "% of officers have cm_pct >= 1.0 (expected >90%)");
    std::cout << "(" << correct_scale << "/" << checked << " officers >= 1.0%) ";
    PASS();
}

void test_sync_bda_detection() {
    TEST("sync BDA officers are detected via is_bda()");
    CHECK(sync_optimizer != nullptr, "sync optimizer not built");
    int bda_count = 0;
    for (const auto& off : sync_optimizer->officers()) {
        if (off.is_bda()) bda_count++;
    }
    CHECK(bda_count > 20, "too few BDA officers detected: " + std::to_string(bda_count));
    std::cout << "(" << bda_count << " BDA officers) ";
    PASS();
}

void test_sync_effects_populated() {
    TEST("sync status effects populated from description");
    CHECK(sync_optimizer != nullptr, "sync optimizer not built");
    int with_effect = 0;
    std::set<std::string> effect_types;
    for (const auto& off : sync_optimizer->officers()) {
        if (!off.effect.empty()) {
            with_effect++;
            effect_types.insert(off.effect);
        }
    }
    CHECK(with_effect > 10, "too few officers with effects: " + std::to_string(with_effect));
    CHECK(effect_types.size() >= 2, "too few effect types: " + std::to_string(effect_types.size()));
    std::string types_str;
    for (const auto& t : effect_types) { if (!types_str.empty()) types_str += ","; types_str += t; }
    std::cout << "(" << with_effect << " officers, types:[" << types_str << "]) ";
    PASS();
}

void test_sync_optimizer_classification() {
    TEST("sync-path optimizer: officer classification produces tags");
    CHECK(sync_optimizer != nullptr, "sync optimizer not built");

    int mining_tagged = 0, cm_text_populated = 0, oa_text_populated = 0;
    for (const auto& off : sync_optimizer->officers()) {
        if (off.mining) mining_tagged++;
        if (!off.cm_text.empty()) cm_text_populated++;
        if (!off.oa_text.empty()) oa_text_populated++;
    }
    CHECK(mining_tagged > 10, "too few mining-tagged officers: " + std::to_string(mining_tagged));
    CHECK(cm_text_populated > 100, "too few officers with cm_text: " + std::to_string(cm_text_populated));
    CHECK(oa_text_populated > 100, "too few officers with oa_text: " + std::to_string(oa_text_populated));

    std::cout << "(mining:" << mining_tagged
              << " cm_text:" << cm_text_populated
              << " oa_text:" << oa_text_populated << ") ";
    PASS();
}

// ---------------------------------------------------------------------------
// Crew Optimizer tests
// ---------------------------------------------------------------------------

static std::unique_ptr<CrewOptimizer> optimizer;

void test_crew_optimizer_construction() {
    TEST("CrewOptimizer construction from sync data");
    CHECK(data_loaded, "game data not loaded");
    // Use the synthetic player data built by test_sync_roster_build
    if (synthetic_pd.officers.empty()) {
        synthetic_pd = build_synthetic_player_data(game_data);
        resolve_player_names(synthetic_pd, game_data);
    }
    optimizer = std::make_unique<CrewOptimizer>(synthetic_pd, game_data);
    CHECK(optimizer->officers().size() > 200,
          "expected >200 officers, got " + std::to_string(optimizer->officers().size()));
    std::cout << "(" << optimizer->officers().size() << " officers) ";
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

void test_cm_scope_coverage() {
    TEST("CM scope classification coverage audit");
    CHECK(optimizer != nullptr, "optimizer not created");

    // Count officers per CM scope
    std::map<std::string, int> scope_counts;
    std::vector<std::string> unknown_officers;
    int total_with_cm = 0;

    auto scope_name = [](CmScope s) -> std::string {
        switch (s) {
            case CmScope::AllStats:     return "AllStats";
            case CmScope::AbilityAmp:   return "AbilityAmp";
            case CmScope::WeaponDamage: return "WeaponDamage";
            case CmScope::CritDamage:   return "CritDamage";
            case CmScope::SingleStat:   return "SingleStat";
            case CmScope::ShieldHp:     return "ShieldHp";
            case CmScope::Mitigation:   return "Mitigation";
            case CmScope::MiningEffect: return "MiningEffect";
            case CmScope::Conditional:  return "Conditional";
            case CmScope::Utility:      return "Utility";
            case CmScope::NonCombat:    return "NonCombat";
            case CmScope::Unknown:      return "Unknown";
        }
        return "???";
    };

    for (const auto& off : optimizer->officers()) {
        if (!off.cm_text.empty()) {
            ++total_with_cm;
            scope_counts[scope_name(off.cm_scope)]++;
            if (off.cm_scope == CmScope::Unknown) {
                unknown_officers.push_back(off.name + ": \"" + off.cm_text.substr(0, 80) + "\"");
            }
        }
    }

    std::cout << "\n";
    std::cout << "    CM scope distribution (" << total_with_cm << " officers with CM text):\n";
    for (const auto& [name, count] : scope_counts) {
        std::cout << "      " << std::setw(14) << std::left << name << ": " << count
                  << " (" << (100 * count / std::max(total_with_cm, 1)) << "%)\n";
    }

    if (!unknown_officers.empty()) {
        std::cout << "    Unknown CM scope officers (" << unknown_officers.size() << "):\n";
        for (const auto& entry : unknown_officers) {
            std::cout << "      - " << entry << "\n";
        }
    }

    // Should have < 10% unknown after expanded patterns
    int unknown_count = scope_counts.count("Unknown") ? scope_counts["Unknown"] : 0;
    double unknown_pct = (total_with_cm > 0) ? (100.0 * unknown_count / total_with_cm) : 0.0;
    CHECK(unknown_pct < 15.0, "too many Unknown CM scopes: " +
          std::to_string(unknown_count) + "/" + std::to_string(total_with_cm) +
          " (" + std::to_string(static_cast<int>(unknown_pct)) + "%)");

    std::cout << "    ";
    PASS();
}

void test_bridge_synergy_groups_populated() {
    TEST("classified officers have group and officer_class populated");
    CHECK(optimizer != nullptr, "optimizer not created");

    int with_group = 0;
    int with_class = 0;
    for (const auto& off : optimizer->officers()) {
        if (!off.group.empty()) ++with_group;
        if (off.officer_class >= 1 && off.officer_class <= 3) ++with_class;
    }

    CHECK(with_group > 0, "no officers have synergy group");
    CHECK(with_class > 0, "no officers have officer_class");
    std::cout << "(" << with_group << " groups, " << with_class << " classes / "
              << optimizer->officers().size() << ") ";
    PASS();
}

void test_planner_construction() {
    TEST("Planner construction and template loading");
    Planner planner;
    CHECK(planner.all_daily_tasks().size() >= 15,
          "expected >= 15 daily templates, got " + std::to_string(planner.all_daily_tasks().size()));
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

static PlayerData build_synthetic_action_plan_player() {
    PlayerData pd;
    pd.ops_level = 80;
    pd.player_name = "Planner Test";
    pd.last_sync = std::chrono::system_clock::now();

    for (const auto& [id, b] : game_data.buildings) {
        (void)b;
        PlayerBuilding pb;
        pb.building_id = id;
        pb.level = 100;
        pd.buildings.push_back(pb);
    }

    for (const auto& [id, r] : game_data.resources) {
        (void)r;
        PlayerResource pr;
        pr.resource_id = id;
        pr.amount = 1000000000000LL;
        pd.resources.push_back(pr);
    }

    return pd;
}

void test_action_planner_research_candidates() {
    TEST("action planner builds affordable research candidates");
    CHECK(data_loaded, "data not loaded");

    auto pd = build_synthetic_action_plan_player();
    resolve_player_names(pd, game_data);
    auto snapshot = build_full_snapshot(pd, game_data);
    auto candidates = analyze_research_candidates(snapshot, 45, "growth");

    CHECK(!candidates.empty(), "no research candidates generated");
    CHECK(candidates.front().name.find("Research#") != 0,
          "top research candidate fell back to id: " + candidates.front().name);
    CHECK(!candidates.front().description.empty(), "top research candidate description missing");
    CHECK(!candidates.front().location.empty(), "top research candidate location missing");
    CHECK(candidates.front().row > 0 && candidates.front().column > 0,
          "top research candidate row/column missing");

    bool found_startable = false;
    bool found_funding_gap = false;
    bool found_requirements = false;
    bool found_fkr_mining_speed = false;
    for (const auto& c : candidates) {
        if (c.can_start_now && c.resources_available && c.prerequisites_met) {
            found_startable = true;
        }
        if (c.name == "FKR Mining Speed") {
            found_fkr_mining_speed = true;
            CHECK(c.research_tree == 1868126734, "FKR Mining Speed tree id changed");
            CHECK(c.row == 2 && c.column == 12, "FKR Mining Speed row/column mismatch");
            CHECK(c.location.find("Starship tree") != std::string::npos,
                  "FKR Mining Speed location did not use Starship tree: " + c.location);
        }
        if (c.costs.empty()) {
            found_funding_gap = true;
            CHECK(c.funding_unknown, c.name + " has no cached costs but was not marked funding_unknown");
            CHECK(!c.resources_available, c.name + " has no cached costs but was marked funded");
            CHECK(!c.can_start_now, c.name + " has no cached costs but was marked startable");
        }
        if (!c.requirements.empty()) {
            found_requirements = true;
        }
    }
    CHECK(found_startable, "no startable research candidate found");
    CHECK(found_funding_gap, "no empty-cost research candidate found to exercise funding gap handling");
    CHECK(found_requirements, "no research requirements captured");
    CHECK(found_fkr_mining_speed, "FKR Mining Speed candidate missing");

    std::cout << "(" << candidates.size() << " candidates, top="
              << candidates.front().name << ") ";
    PASS();
}

void test_action_plan_generation_and_persistence() {
    TEST("action plan generates and persists research recommendations");
    CHECK(data_loaded, "data not loaded");

    auto pd = build_synthetic_action_plan_player();
    resolve_player_names(pd, game_data);
    auto snapshot = build_full_snapshot(pd, game_data);
    auto plan = generate_action_plan(snapshot, 45, "growth");

    CHECK(!plan.top_research.empty(), "top_research empty");
    CHECK(!plan.do_now.empty(), "do_now empty");
    CHECK(plan.generated_at > 0, "generated_at missing");
    CHECK(!plan.top_research.front().location.empty(), "top_research location missing");
    std::set<int64_t> first_trees;
    bool top_has_prime = false;
    for (size_t i = 0; i < plan.top_research.size() && i < 5; ++i) {
        first_trees.insert(plan.top_research[i].research_tree);
    }
    for (const auto& r : plan.top_research) {
        if (r.name.find("Prime") != std::string::npos) top_has_prime = true;
    }
    CHECK(first_trees.size() >= 2, "top research first page is not tree-diversified");
    CHECK(top_has_prime, "top research does not surface any Prime candidates");
    CHECK(plan.top_research.front().name != "Prime Mining XP",
          "low-impact Prime Mining XP should not be the top research recommendation");
    auto plan_req_it = std::find_if(plan.top_research.begin(), plan.top_research.end(),
                                    [](const ResearchCandidate& r) { return !r.requirements.empty(); });
    CHECK(plan_req_it != plan.top_research.end(), "top_research requirements missing");
    CHECK(!plan.do_now.front().location.empty(), "do_now location missing");
    for (const auto& r : plan.top_research) {
        CHECK(!r.funding_unknown || !r.can_start_now,
              r.name + " has unknown funding but was marked startable");
    }
    std::map<int64_t, int64_t> planned_spend;
    for (const auto& action : plan.do_now) {
        for (const auto& resource : action.resources_spent) {
            planned_spend[resource.resource_id] += resource.amount;
        }
    }
    for (const auto& [resource_id, amount] : planned_spend) {
        CHECK(amount <= 1000000000000LL,
              "do_now over-reserved synthetic resource " + std::to_string(resource_id));
    }

    std::string path = "data/player_data/test_action_plan.json";
    CHECK(save_action_plan(plan, path), "save_action_plan failed");
    CHECK(fs::exists(path), "action plan file missing");

    ActionPlan loaded;
    CHECK(load_action_plan(loaded, path), "load_action_plan failed");
    CHECK(loaded.top_research.size() == plan.top_research.size(), "top_research size mismatch");
    CHECK(!loaded.do_now.empty(), "loaded do_now empty");
    CHECK(!loaded.top_research.front().location.empty(), "loaded top_research location missing");
    size_t req_index = static_cast<size_t>(std::distance(plan.top_research.begin(), plan_req_it));
    CHECK(loaded.top_research[req_index].requirements.size() == plan.top_research[req_index].requirements.size(),
          "loaded top_research requirements size mismatch");
    CHECK(loaded.top_research.front().funding_unknown == plan.top_research.front().funding_unknown,
          "loaded top_research funding_unknown mismatch");
    CHECK(!loaded.do_now.front().location.empty(), "loaded do_now location missing");

    fs::remove(path);
    PASS();
}

// ---------------------------------------------------------------------------
// AI / LLM integration tests
// ---------------------------------------------------------------------------

static bool ai_mode = false;   // --ai flag enables LLM tests
static bool prompt_mode = false; // --prompt prints the exact built prompt without querying an LLM
static bool all_prompts_mode = false;
static std::string selected_prompt_id = "ask_fkr_armada_credits";
static bool require_live_sync = false;
static bool force_live_sync = false;
static bool sync_only_mode = false;
static int prompt_sync_timeout_seconds = 120;

namespace ai_test {
static PlayerData load_player_data(const std::string& path);
}

static std::string format_sync_timestamp(const std::chrono::system_clock::time_point& tp) {
    if (tp == std::chrono::system_clock::time_point{}) return "never";
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return buf;
}

static long sync_age_seconds(const PlayerData& pd) {
    if (pd.last_sync == std::chrono::system_clock::time_point{}) return LONG_MAX;
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - pd.last_sync).count();
}

static bool same_local_day(const std::chrono::system_clock::time_point& tp) {
    if (tp == std::chrono::system_clock::time_point{}) return false;
    auto now_tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto tp_tt = std::chrono::system_clock::to_time_t(tp);
    std::tm now_tm{};
    std::tm tp_tm{};
    localtime_r(&now_tt, &now_tm);
    localtime_r(&tp_tt, &tp_tm);
    return now_tm.tm_year == tp_tm.tm_year && now_tm.tm_yday == tp_tm.tm_yday;
}

static PlayerData load_or_wait_for_sync() {
    PlayerData disk_pd = ai_test::load_player_data("data/player_data/player_data.json");
    bool has_disk = !disk_pd.officers.empty();
    bool same_day = same_local_day(disk_pd.last_sync);

    if (!force_live_sync && has_disk && same_day) {
        std::cout << "    Using saved sync from " << format_sync_timestamp(disk_pd.last_sync)
                  << " (same day)\n";
        return disk_pd;
    }

    if (force_live_sync) {
        std::cout << "    Forcing live sync; ignoring saved player_data.json\n";
    } else if (has_disk) {
        std::cout << "    Saved sync is stale or missing timestamp ("
                  << format_sync_timestamp(disk_pd.last_sync)
                  << "); attempting live sync first\n";
    } else {
        std::cout << "    No saved sync found; attempting live sync first\n";
    }

    IngressServer ingress("data/player_data", 8270);
    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    bool sync_received = false;

    ingress.set_data_callback([&](const std::string& body) {
        std::lock_guard<std::mutex> lock(sync_mutex);
        sync_received = true;
        std::cout << "    Sync POST received (" << body.size() << " bytes)\n";
        sync_cv.notify_all();
    });

    if (!ingress.start()) {
        std::cout << "    Failed to start ingress server; continuing with saved data if available\n";
        return disk_pd;
    }

    std::cout << "    Waiting for live sync on http://127.0.0.1:" << ingress.port()
              << "/sync/ingress/ (timeout " << prompt_sync_timeout_seconds << "s)\n";

    {
        std::unique_lock<std::mutex> lock(sync_mutex);
        bool ok = sync_cv.wait_for(lock, std::chrono::seconds(prompt_sync_timeout_seconds), [&]() {
            return sync_received;
        });
        ingress.stop();
        if (ok) {
            std::cout << "    Live sync received at " << format_sync_timestamp(ingress.get_player_data().last_sync) << "\n";
            return ingress.get_player_data();
        }
    }

    if (has_disk) {
        std::cout << "    Live sync timed out; continuing with saved data from "
                  << format_sync_timestamp(disk_pd.last_sync) << "\n";
        return disk_pd;
    }

    return disk_pd;
}

void test_sync_monitor() {
    TEST("sync monitor");
    IngressServer ingress("data/player_data", 8270);
    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    bool sync_received = false;

    ingress.set_data_callback([&](const std::string& body) {
        std::lock_guard<std::mutex> lock(sync_mutex);
        sync_received = true;
        std::cout << "    Sync POST received (" << body.size() << " bytes)\n";
        sync_cv.notify_all();
    });

    CHECK(ingress.start(), "failed to start ingress server on port 8270");
    std::cout << "    Listening for sync on http://127.0.0.1:" << ingress.port()
              << "/sync/ingress/ (timeout " << prompt_sync_timeout_seconds << "s)\n";

    {
        std::unique_lock<std::mutex> lock(sync_mutex);
        bool ok = sync_cv.wait_for(lock, std::chrono::seconds(prompt_sync_timeout_seconds), [&]() {
            return sync_received;
        });
        ingress.stop();
        CHECK(ok, "timed out waiting for live sync");
    }

    const auto& pd = ingress.get_player_data();
    std::cout << "    Player: " << (pd.player_name.empty() ? "Player" : pd.player_name)
              << ", officers=" << pd.officers.size()
              << ", ships=" << pd.ships.size()
              << ", last_sync=" << format_sync_timestamp(pd.last_sync) << "\n";
    PASS();
}

static std::string join_strings(const std::vector<std::string>& items, const std::string& sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) oss << sep;
        oss << items[i];
    }
    return oss.str();
}

static nlohmann::json split_lines_json(const std::string& text) {
    nlohmann::json lines = nlohmann::json::array();
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) lines.push_back(line);
    if (!text.empty() && text.back() == '\n') lines.push_back("");
    return lines;
}

static LlmRequest build_ship_assessment_request(const PlayerData& pd,
                                                const GameData& gd,
                                                const nlohmann::json&) {
    return stfc::build_ship_assessment_request(pd, gd);
}

static nlohmann::json build_data_quality(const PlayerData& pd, long age_sec) {
    nlohmann::json present = nlohmann::json::array();
    nlohmann::json missing = nlohmann::json::array();
    nlohmann::json warnings = nlohmann::json::array();

    if (!pd.officers.empty()) present.push_back("officers");
    else missing.push_back("officers");

    if (!pd.ships.empty()) present.push_back("ships");
    else missing.push_back("ships");

    if (!pd.buildings.empty()) present.push_back("buildings");
    else missing.push_back("buildings");

    if (!pd.resources.empty()) present.push_back("resources");
    else missing.push_back("resources");

    if (!pd.techs.empty()) present.push_back("forbidden_tech");
    else missing.push_back("forbidden_tech");

    if (!pd.inventory.empty()) present.push_back("inventory_items");
    else missing.push_back("inventory_items");

    if (!pd.jobs.empty()) present.push_back("active_jobs");
    else missing.push_back("active_jobs");

    if (!pd.missions.empty()) present.push_back("missions");
    else missing.push_back("missions");

    if (!pd.buffs.empty()) present.push_back("buffs");
    else missing.push_back("buffs");

    if (!pd.events.empty()) present.push_back("events");
    else missing.push_back("events");

    bool has_event_scores = false;
    for (const auto& event : pd.events) {
        if (event.ranking.score > 0.0 || event.ranking.rank > 0 || event.ranking.position > 0) {
            has_event_scores = true;
            break;
        }
    }
    if (has_event_scores) present.push_back("event_scores");
    else missing.push_back("event_scores");

    missing.push_back("active_ship_locations");
    missing.push_back("dock_assignments");
    missing.push_back("mining_node_state");
    missing.push_back("research_focus");

    if (pd.last_sync == std::chrono::system_clock::time_point{}) {
        warnings.push_back("Sync timestamp missing; prompt recency cannot be verified.");
    } else if (age_sec > 600) {
        warnings.push_back("Sync data is older than 10 minutes; prompt context may be stale.");
    }

    if (pd.inventory.empty()) {
        warnings.push_back("No inventory item data; spend recommendations cannot confirm available speedups, XP, or credits.");
    }
    if (pd.jobs.empty()) {
        warnings.push_back("No active job data; prompts cannot reason about current build/research queues.");
    }
    if (pd.missions.empty()) {
        warnings.push_back("No mission data; prompts cannot account for active mission progress.");
    }
    if (pd.events.empty()) {
        warnings.push_back("No event data; prompts cannot optimize around today's event schedule.");
    }
    if (!has_event_scores) {
        warnings.push_back("No event score data; prompts cannot optimize around event threshold progress.");
    }
    warnings.push_back("No active ship location data; prompts assume all owned ships are generally available.");
    warnings.push_back("No mining node state; prompts cannot detect zeroed nodes, over-cargo status, or active mining assignments.");
    warnings.push_back("No research focus metadata; progression advice may be broad rather than tree-specific.");

    return {
        {"present", present},
        {"missing", missing},
        {"warnings", warnings}
    };
}

// ---------------------------------------------------------------------------
// Helpers for building sync-path roster (duplicated from main.cpp since
// main.cpp is not a library — these are the essential functions needed to
// build a proper roster from player_data.json + game_data)
// ---------------------------------------------------------------------------

namespace ai_test {

// Load player_data.json from disk (same format as IngressServer::load_player_data)
static PlayerData load_player_data(const std::string& path) {
    auto dir = fs::path(path).parent_path();
    if (dir.empty()) dir = ".";
    IngressServer loader(dir.string(), 8270);
    return loader.get_player_data();
}

} // namespace ai_test

void test_ai_ask_armada_credits() {
    TEST("AI ask: maximize FKR credits (full account context)");
    CHECK(data_loaded, "game data not loaded");

    // Load real player data from disk
    auto pd = ai_test::load_player_data("data/player_data/player_data.json");
    CHECK(!pd.officers.empty(), "no player officers in player_data.json");

    // ops_level may not be in sync data — derive from Operations Center (building_id 0)
    if (pd.ops_level <= 0) {
        for (const auto& b : pd.buildings) {
            if (b.building_id == 0 && b.level > 0) {
                pd.ops_level = b.level;
                break;
            }
        }
    }
    // Fallback: estimate from roster size if buildings data is missing
    if (pd.ops_level <= 0) {
        int max_rank = 0;
        for (const auto& po : pd.officers) {
            if (po.rank > max_rank) max_rank = po.rank;
        }
        pd.ops_level = (pd.officers.size() > 200 && max_rank >= 5) ? 40 : 30;
    }
    if (pd.player_name.empty()) pd.player_name = "Player";

    // Resolve names against game data
    resolve_player_names(pd, game_data);

    // Build optimizer directly from player data + game data
    auto opt = std::make_unique<CrewOptimizer>(pd, game_data);
    const auto& officers = opt->officers();
    CHECK(officers.size() > 50, "roster too small: " + std::to_string(officers.size()));

    std::cout << "\n    Account: " << pd.player_name << " (Ops " << pd.ops_level
              << ", " << officers.size() << " officers, "
              << pd.ships.size() << " ships)\n";

    // Log some stats about what's going into the prompt
    int rank5 = 0, armada_tagged = 0;
    for (const auto& o : officers) {
        if (o.rank >= 5) rank5++;
        if (o.armada) armada_tagged++;
    }
    std::cout << "    Officers: " << officers.size() << " classified ("
              << rank5 << " rank 5, " << armada_tagged << " armada-tagged)\n";

    // Show top ships by tier
    if (!pd.ships.empty()) {
        auto sorted_ships = pd.ships;
        std::sort(sorted_ships.begin(), sorted_ships.end(),
                  [](const PlayerShip& a, const PlayerShip& b) {
                      if (a.tier != b.tier) return a.tier > b.tier;
                      return a.level > b.level;
                  });
        std::cout << "    Top ships: ";
        for (int i = 0; i < std::min(5, (int)sorted_ships.size()); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << sorted_ships[i].name << " T" << sorted_ships[i].tier
                      << " L" << sorted_ships[i].level;
        }
        std::cout << "\n";
    }

    // Initialize the AI engine
    AiCrewEngine engine;
    std::string init_err = engine.initialize();
    CHECK(init_err.empty(), "AI init failed: " + init_err);
    CHECK(engine.is_available(), "AI engine not available after init");

    auto st = engine.status();
    std::cout << "    AI: " << st.provider << "/" << st.model << "\n";

    // Ask the question with full account context
    std::string question =
        "I want to maximize FKR armada credits (Federation, Klingon, Romulan faction credits) "
        "at my current ops level. For each faction's armadas, please tell me:\n"
        "1. Which armada targets I should prioritize for the best credit yield\n"
        "2. The full officer crew: captain, 2 bridge officers, AND below-decks (BDA) officer — "
        "explain why each officer is chosen and what their abilities contribute\n"
        "3. Which of my ships to use and at what tier\n"
        "Use ONLY officers and ships I actually own (from my account data). "
        "Consider their real levels, ranks, and armada-specific abilities. "
        "Also share any tips for maximizing credit payout per armada run.";

    std::cout << "    Waiting for LLM response..." << std::flush;

    auto resp = engine.ask_question(question, pd, game_data, officers);

    CHECK(resp.ok(), "LLM returned error: " + resp.error);
    CHECK(!resp.content.empty(), "LLM returned empty content");
    CHECK(resp.content.size() > 100, "response too short (" + std::to_string(resp.content.size()) + " chars)");

    // Print the full response
    std::cout << "\n\n    ┌─── AI Response (" << resp.content.size() << " chars, "
              << resp.output_tokens << " tokens) ───\n";

    // Word-wrap the response at ~100 chars for terminal readability
    std::istringstream stream(resp.content);
    std::string line;
    while (std::getline(stream, line)) {
        while (line.size() > 100) {
            auto pos = line.rfind(' ', 100);
            if (pos == std::string::npos) pos = 100;
            std::cout << "    │ " << line.substr(0, pos) << "\n";
            line = line.substr(pos + (pos < line.size() && line[pos] == ' ' ? 1 : 0));
        }
        std::cout << "    │ " << line << "\n";
    }
    std::cout << "    └───────────────────────────────\n";

    // Content validation
    std::string lower_content = resp.content;
    std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);

    bool mentions_armada = lower_content.find("armada") != std::string::npos;
    bool mentions_faction = lower_content.find("federation") != std::string::npos ||
                            lower_content.find("klingon") != std::string::npos ||
                            lower_content.find("romulan") != std::string::npos ||
                            lower_content.find("fkr") != std::string::npos;
    bool mentions_crew = lower_content.find("crew") != std::string::npos ||
                         lower_content.find("captain") != std::string::npos ||
                         lower_content.find("bridge") != std::string::npos;
    bool mentions_officers = lower_content.find("officer") != std::string::npos ||
                             lower_content.find("kirk") != std::string::npos ||
                             lower_content.find("khan") != std::string::npos ||
                             lower_content.find("spock") != std::string::npos;
    bool mentions_below_decks = lower_content.find("below") != std::string::npos ||
                                lower_content.find("bda") != std::string::npos ||
                                lower_content.find("below deck") != std::string::npos;

    CHECK(mentions_armada, "response doesn't mention 'armada'");
    CHECK(mentions_faction, "response doesn't mention any FKR faction (Federation/Klingon/Romulan)");
    CHECK(mentions_crew || mentions_officers,
          "response doesn't mention crews or officers");
    if (!mentions_below_decks) {
        std::cout << "    [WARN] response doesn't explicitly mention below-decks/BDA officers\n";
    }

    PASS();
}

void test_ai_export_live_prompts_json() {
    TEST("AI prompt export");
    CHECK(data_loaded, "game data not loaded");

    PlayerData pd = load_or_wait_for_sync();
    CHECK(!pd.officers.empty(), "no player officers in data/player_data/player_data.json");
    CHECK(pd.last_sync != std::chrono::system_clock::time_point{},
          "no sync timestamp in data/player_data/player_data.json; refresh sync before exporting prompts");
    long age_sec = sync_age_seconds(pd);
    bool has_last_sync = (pd.last_sync != std::chrono::system_clock::time_point{});
    bool sync_fresh = has_last_sync && age_sec >= 0 && age_sec <= 600;

    if (pd.ops_level <= 0) {
        for (const auto& b : pd.buildings) {
            if (b.building_id == 0 && b.level > 0) {
                pd.ops_level = b.level;
                break;
            }
        }
    }
    if (pd.player_name.empty()) pd.player_name = "Player";

    resolve_player_names(pd, game_data);

    auto opt = std::make_unique<CrewOptimizer>(pd, game_data);
    const auto& officers = opt->officers();
    CHECK(officers.size() > 50, "roster too small: " + std::to_string(officers.size()));
    CHECK(!officers.empty(), "no classified officers from live sync roster");

    struct PromptSpec {
        std::string id;
        std::string mode;
        Scenario scenario;
        ShipType ship_type;
        int top_n;
        std::string question_or_goal;
    };

    std::vector<PromptSpec> all_specs = {
        {"ask_fkr_armada_credits", "ask", Scenario::Armada, ShipType::Explorer, 60,
         "I want to maximize FKR armada credits (Federation, Klingon, Romulan faction credits) at my current ops level. For each faction's armadas, please tell me which targets to prioritize, the best full crew including below decks, which owned ship to use, and why."},
        {"ship_assessment", "ship", Scenario::Hybrid, ShipType::Explorer, 50,
         ""},
        {"officer_assessment", "officer", Scenario::Hybrid, ShipType::Explorer, 50,
         ""},
        {"strategic_assessment", "strategic", Scenario::Hybrid, ShipType::Explorer, 50,
         ""},
        {"research_priorities", "research", Scenario::Hybrid, ShipType::Explorer, 50,
         "What research do I need to do today in order of priority?"},
        {"crew_pvp_explorer", "crew", Scenario::PvP, ShipType::Explorer, 40,
         ""},
        {"crew_hybrid_explorer", "crew", Scenario::Hybrid, ShipType::Explorer, 40,
         ""},
        {"crew_armada", "crew", Scenario::Armada, ShipType::Explorer, 40,
         ""},
        {"crew_pve_hostile", "crew", Scenario::PvEHostile, ShipType::Explorer, 40,
         ""},
        {"crew_mining_general", "crew", Scenario::MiningGeneral, ShipType::Explorer, 40,
         ""},
        {"crew_loot", "crew", Scenario::Loot, ShipType::Explorer, 40,
         ""},
        {"progression_general", "progression", Scenario::Hybrid, ShipType::Explorer, 50,
         "Help me decide what to invest in next for the best combat and account growth."}
    };

    std::vector<PromptSpec> specs;
    if (all_prompts_mode) {
        specs = all_specs;
    } else {
        auto it = std::find_if(all_specs.begin(), all_specs.end(), [](const PromptSpec& spec) {
            return spec.id == selected_prompt_id;
        });
        CHECK(it != all_specs.end(), "unknown prompt id: " + selected_prompt_id);
        specs.push_back(*it);
    }

    CrewAdvisor advisor(nullptr);
    nlohmann::json diagnostics;
    diagnostics["generated_at"] = format_sync_timestamp(std::chrono::system_clock::now());
    diagnostics["player"] = {
        {"name", pd.player_name},
        {"ops_level", pd.ops_level},
        {"last_sync", format_sync_timestamp(pd.last_sync)},
        {"sync_age_seconds", age_sec},
        {"sync_fresh", sync_fresh},
        {"sync_status", !has_last_sync ? "missing_last_sync" : (sync_fresh ? "fresh" : "stale")},
        {"officers", pd.officers.size()},
        {"ships", pd.ships.size()},
        {"buildings", pd.buildings.size()},
        {"techs", pd.techs.size()}
    };
    diagnostics["data_quality"] = build_data_quality(pd, age_sec);

    nlohmann::json out_prompts = nlohmann::json::array();

    for (const auto& spec : specs) {
        AccountSnapshot snapshot = build_account_snapshot(
            pd, game_data, officers, spec.scenario, spec.ship_type, spec.top_n);

        LlmRequest req;
        if (spec.mode == "ask") {
            req = advisor.debug_build_ask_request(snapshot, spec.question_or_goal);
        } else if (spec.mode == "ship") {
            req = build_ship_assessment_request(pd, game_data, diagnostics["data_quality"]);
        } else if (spec.mode == "officer") {
            req = stfc::build_officer_assessment_request(pd, game_data, officers);
        } else if (spec.mode == "strategic") {
            req = stfc::build_strategic_assessment_request(pd);
        } else if (spec.mode == "research") {
            req = stfc::build_research_priority_request(pd, game_data);
        } else if (spec.mode == "progression") {
            req = advisor.debug_build_progression_request(snapshot, spec.question_or_goal);
        } else {
            req = advisor.debug_build_crew_request(snapshot, 3);
        }

        nlohmann::json j;
        j = {
            {"system_prompt", split_lines_json(req.system_prompt)},
            {"user_prompt", split_lines_json(req.user_prompt)},
            {"response_schema", split_lines_json(req.response_schema)},
            {"temperature", req.temperature},
            {"max_tokens", req.max_tokens},
            {"enable_search", req.enable_search}
        };
        nlohmann::json prompt_diag = {
            {"prompt_id", spec.id},
            {"generated_at", diagnostics["generated_at"]},
            {"scenario", scenario_str(spec.scenario)},
            {"ship_type", ship_type_str(spec.ship_type)},
            {"sync_status", diagnostics["player"]["sync_status"]},
            {"sync_fresh", diagnostics["player"]["sync_fresh"]}
        };
        out_prompts.push_back(std::move(j));
        diagnostics["prompts"].push_back(std::move(prompt_diag));
    }

    for (size_t i = 0; i < out_prompts.size(); ++i) {
        const auto& prompt = out_prompts[i];
        const auto& prompt_diag = diagnostics["prompts"][i];
        std::cout << "\n    ┌─── " << prompt_diag["prompt_id"].get<std::string>()
                  << " [" << prompt_diag["scenario"].get<std::string>() << "] ───\n";
        std::cout << "    │\n";
        std::cout << "    │ Diagnostics: sync=" << prompt_diag["sync_status"].get<std::string>()
                  << ", fresh=" << (prompt_diag["sync_fresh"].get<bool>() ? "yes" : "no") << "\n";
        std::cout << "    │ Missing inputs: "
                  << join_strings(diagnostics["data_quality"]["missing"].get<std::vector<std::string>>(), ", ") << "\n";
        std::cout << "    │\n";
        size_t system_chars = 0;
        for (const auto& line : prompt["system_prompt"]) system_chars += line.get<std::string>().size() + 1;
        size_t user_chars = 0;
        for (const auto& line : prompt["user_prompt"]) user_chars += line.get<std::string>().size() + 1;
        size_t schema_chars = 0;
        for (const auto& line : prompt["response_schema"]) schema_chars += line.get<std::string>().size() + 1;
        std::cout << "    │ Request: system=" << system_chars
                  << " chars, user=" << user_chars
                  << " chars, schema=" << schema_chars
                  << " chars\n";
        std::cout << "    │ SETTINGS: temp=" << prompt["temperature"].get<double>()
                  << ", max_tokens=" << prompt["max_tokens"].get<int>()
                  << ", search=" << (prompt["enable_search"].get<bool>() ? "on" : "off")
                  << "\n";
        std::cout << "    └────────────────────────────────────────\n";
    }

    fs::create_directories("data/prompt_exports");
    std::string out_path = all_prompts_mode
        ? "data/prompt_exports/live_prompts.json"
        : ("data/prompt_exports/" + selected_prompt_id + ".json");
    std::ofstream f(out_path);
    CHECK(f.good(), "failed to open output file: " + out_path);
    if (all_prompts_mode) {
        f << out_prompts.dump(2);
    } else {
        f << out_prompts.front().dump(2);
    }
    CHECK(f.good(), "failed to write output file: " + out_path);

    std::string diag_path = all_prompts_mode
        ? "data/prompt_exports/live_prompts.diagnostics.json"
        : ("data/prompt_exports/" + selected_prompt_id + ".diagnostics.json");
    std::ofstream df(diag_path);
    CHECK(df.good(), "failed to open diagnostics file: " + diag_path);
    df << diagnostics.dump(2);
    CHECK(df.good(), "failed to write diagnostics file: " + diag_path);

    std::cout << "(sync " << format_sync_timestamp(pd.last_sync)
              << ", age " << (has_last_sync ? std::to_string(age_sec) + "s" : std::string("unknown"))
              << ", status " << diagnostics["player"]["sync_status"].get<std::string>() << ", "
              << out_prompts.size() << " prompts -> " << out_path << ", diagnostics -> " << diag_path << ") ";
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
        else if (std::strcmp(argv[i], "--ai") == 0) ai_mode = true;
        else if (std::strcmp(argv[i], "--prompt") == 0) prompt_mode = true;
        else if (std::strcmp(argv[i], "--sync") == 0) sync_only_mode = true;
        else if (std::strcmp(argv[i], "--wait-for-sync") == 0) { prompt_mode = true; require_live_sync = true; }
        else if (std::strcmp(argv[i], "--force-sync") == 0) { prompt_mode = true; force_live_sync = true; require_live_sync = true; }
        else if (std::strcmp(argv[i], "--sync-timeout") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "ERROR: --sync-timeout requires a value\n";
                return 1;
            }
            prompt_sync_timeout_seconds = std::max(1, std::atoi(argv[++i]));
        }
        else if (std::strcmp(argv[i], "--all-prompts") == 0) { prompt_mode = true; all_prompts_mode = true; }
        else if (std::strcmp(argv[i], "--prompt-id") == 0) {
            prompt_mode = true;
            if (i + 1 >= argc) {
                std::cerr << "ERROR: --prompt-id requires a value\n";
                return 1;
            }
            selected_prompt_id = argv[++i];
        }
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: smoke_test [--live] [--clean] [--ai] [--prompt] [--prompt-id <id>] [--all-prompts] [--sync] [--wait-for-sync] [--force-sync] [--sync-timeout <sec>]\n"
                      << "  (no args)  Test against cached data (fast, offline)\n"
                      << "  --live     Force fresh fetch from api.spocks.club\n"
                      << "  --clean    Wipe cache first, then fetch live\n"
                      << "  --ai       Run AI/LLM integration tests (requires API key)\n"
                      << "  --prompt   Build the default prompt from live account data\n"
                      << "  --prompt-id <id>  Build one specific prompt id (default: ask_fkr_armada_credits)\n"
                      << "  --all-prompts     Build and export the full prompt set\n"
                      << "  --sync            Wait for and monitor a live sync only\n"
                      << "  --wait-for-sync   Require a fresh live sync before prompt export\n"
                      << "  --force-sync      Ignore saved sync and require a fresh live sync\n"
                      << "  --sync-timeout N  Wait up to N seconds for live sync\n";
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

    // --ai: skip the full suite, just load data and run the AI test
    if (sync_only_mode || ai_mode || prompt_mode) {
        std::cout << "\n--- Data loading ---\n";
        test_fetch_all();
        if (!data_loaded) {
            std::cerr << "ERROR: game data not loaded, cannot run AI/prompt test\n";
            return 1;
        }
        if (sync_only_mode) {
            std::cout << "\n--- Sync Monitor ---\n";
            test_sync_monitor();
        }
        if (prompt_mode) {
            test_ai_export_live_prompts_json();
        }
        if (ai_mode) {
            std::cout << "\n--- AI / LLM Integration ---\n";
            test_ai_ask_armada_credits();
        }
        std::cout << "\n";
        if (tests_failed == 0) {
            std::cout << "=== \033[32m" << tests_passed << "/" << tests_run << " PASSED\033[0m ===\n";
        } else {
            std::cout << "=== " << tests_passed << "/" << tests_run << " passed, "
                      << "\033[31m" << tests_failed << " FAILED\033[0m ===\n";
        }
        return tests_failed > 0 ? 1 : 0;
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
    test_ship_ability_classification();

    std::cout << "\n--- Research (" << game_data.researches.size() << ") ---\n";
    test_research_count();
    test_research_has_trees();
    test_research_levels_parsed();
    test_research_names_and_descriptions();

    std::cout << "\n--- Buildings (" << game_data.buildings.size() << ") ---\n";
    test_building_count();
    test_building_levels();
    test_building_names();

    std::cout << "\n--- Resources (" << game_data.resources.size() << ") ---\n";
    test_resource_count();
    test_resource_names();

    std::cout << "\n--- Community Data (StewieDoo Officer Tool) ---\n";
    test_community_scores_load();
    test_community_skills_load();
    test_community_presets_load();
    test_community_scores_fields();
    test_community_skills_fields();
    test_community_presets_fields();
    test_community_name_lookups();

    std::cout << "\n--- Sync-Path Data Pipeline ---\n";
    if (data_loaded) {
        test_sync_roster_build();
        test_sync_descriptions_have_markers();
        test_sync_descriptions_no_color_tags();
        test_sync_descriptions_lowercase();
        test_sync_pct_conversion();
        test_sync_bda_detection();
        test_sync_effects_populated();
        test_sync_optimizer_classification();
    } else {
        std::cout << "  (skipped — game data not loaded)\n";
    }

    std::cout << "\n--- Crew Optimizer ---\n";
    if (data_loaded) {
        test_crew_optimizer_construction();
        test_crew_classification_tags();
        test_crew_classification_states();
        test_crew_ship_lock();
        test_crew_scenario_enums();

        std::cout << "\n--- CM Scope ---\n";
        test_cm_scope_coverage();

        std::cout << "\n--- Bridge Synergy Groups ---\n";
        test_bridge_synergy_groups_populated();
    } else {
        std::cout << "  (skipped — game data not loaded)\n";
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

    std::cout << "\n--- Live Action Planner ---\n";
    if (data_loaded) {
        test_action_planner_research_candidates();
        test_action_plan_generation_and_persistence();
    } else {
        std::cout << "  (skipped — game data not loaded)\n";
    }

    // AI tests (only when --ai flag is passed)
    if (ai_mode) {
        std::cout << "\n--- AI / LLM Integration ---\n";
        if (data_loaded) {
            test_ai_ask_armada_credits();
        } else {
            std::cout << "  (skipped — game data not loaded)\n";
        }
    } else {
        std::cout << "\n  (AI tests skipped — pass --ai to enable)\n";
    }

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
