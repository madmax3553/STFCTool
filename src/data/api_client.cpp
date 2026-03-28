#include "data/api_client.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <ctime>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace stfc {

ApiClient::ApiClient(const std::string& cache_dir)
    : cache_dir_(cache_dir) {
    fs::create_directories(cache_dir_);
}

std::string ApiClient::api_get(const std::string& path) {
    httplib::SSLClient cli("api.spocks.club", 443);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);

    auto res = cli.Get(path);
    if (res && res->status == 200) {
        return res->body;
    }
    return "";
}

std::string ApiClient::read_cache(const std::string& filename) {
    auto path = fs::path(cache_dir_) / filename;
    if (!fs::exists(path)) return "";
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ApiClient::write_cache(const std::string& filename, const std::string& data) {
    auto path = fs::path(cache_dir_) / filename;
    std::ofstream f(path);
    if (!f) return false;
    f << data;
    return true;
}

bool ApiClient::is_cache_fresh(const std::string& filename, int max_age_hours) {
    if (force_refresh_) return false;
    auto path = fs::path(cache_dir_) / filename;
    if (!fs::exists(path)) return false;

    auto mod_time = fs::last_write_time(path);
    auto now = fs::file_time_type::clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - mod_time);
    return age.count() < max_age_hours;
}

void ApiClient::report_progress(const std::string& step, int current, int total) {
    if (progress_cb_) {
        progress_cb_(step, current, total);
    }
}

// ---------------------------------------------------------------------------
// Parse helpers
// ---------------------------------------------------------------------------

static AbilityValue parse_ability_value(const json& j) {
    AbilityValue v;
    if (j.contains("value")) v.value = j["value"].get<double>();
    if (j.contains("chance")) v.chance = j["chance"].get<double>();
    return v;
}

static OfficerAbility parse_officer_ability(const json& j) {
    OfficerAbility a;
    if (j.contains("id")) a.id = j["id"].get<int64_t>();
    if (j.contains("value_is_percentage")) a.value_is_percentage = j["value_is_percentage"].get<bool>();
    if (j.contains("values") && j["values"].is_array()) {
        for (auto& v : j["values"]) {
            a.values.push_back(parse_ability_value(v));
        }
    }
    return a;
}

// ---------------------------------------------------------------------------
// Fetch officers
// ---------------------------------------------------------------------------

bool ApiClient::fetch_officers(GameData& data) {
    report_progress("officers", 0, 1);

    std::string body;
    if (is_cache_fresh("officers.json")) {
        body = read_cache("officers.json");
    } else {
        body = api_get("/officer");
        if (body.empty()) {
            // Try cache even if stale
            body = read_cache("officers.json");
            if (body.empty()) return false;
        } else {
            write_cache("officers.json", body);
        }
    }

    try {
        auto arr = json::parse(body);
        for (auto& j : arr) {
            Officer o;
            o.id = j.value("id", (int64_t)0);
            o.art_id = j.value("art_id", 0);
            o.loca_id = j.value("loca_id", 0);
            o.faction = j.value("faction", (int64_t)0);
            o.officer_class = j.value("class", 0);
            o.max_rank = j.value("max_rank", 0);
            o.synergy_id = j.value("synergy_id", (int64_t)0);

            // Rarity comes as string from API
            if (j.contains("rarity")) {
                auto& r = j["rarity"];
                if (r.is_string()) {
                    o.rarity = std::stoi(r.get<std::string>());
                } else {
                    o.rarity = r.get<int>();
                }
            }

            if (j.contains("ability")) o.ability = parse_officer_ability(j["ability"]);
            if (j.contains("captain_ability")) o.captain_ability = parse_officer_ability(j["captain_ability"]);

            if (j.contains("stats") && j["stats"].is_array()) {
                for (auto& s : j["stats"]) {
                    OfficerStats st;
                    st.level = s.value("level", 0);
                    st.attack = s.value("attack", 0.0);
                    st.defense = s.value("defense", 0.0);
                    st.health = s.value("health", 0.0);
                    o.stats.push_back(st);
                }
            }

            if (j.contains("levels") && j["levels"].is_array()) {
                for (auto& l : j["levels"]) {
                    o.level_xp.push_back(l.value("xp", 0));
                }
            }

            data.officers[o.id] = std::move(o);
        }
    } catch (const json::exception& e) {
        return false;
    }

    report_progress("officers", 1, 1);
    return true;
}

// ---------------------------------------------------------------------------
// Fetch ships
// ---------------------------------------------------------------------------

bool ApiClient::fetch_ships(GameData& data) {
    report_progress("ships", 0, 1);

    std::string body;
    if (is_cache_fresh("ships.json")) {
        body = read_cache("ships.json");
    } else {
        body = api_get("/ship");
        if (body.empty()) {
            body = read_cache("ships.json");
            if (body.empty()) return false;
        } else {
            write_cache("ships.json", body);
        }
    }

    try {
        auto arr = json::parse(body);
        for (auto& j : arr) {
            Ship s;
            s.id = j.value("id", (int64_t)0);
            s.art_id = j.value("art_id", 0);
            s.loca_id = j.value("loca_id", 0);
            s.max_tier = j.value("max_tier", 0);
            s.grade = j.value("grade", 0);
            s.scrap_level = j.value("scrap_level", -1);
            s.build_time_seconds = j.value("build_time_in_seconds", 0);
            s.faction = j.value("faction", (int64_t)-1);
            s.blueprints_required = j.value("blueprints_required", 0);
            s.hull_type = j.value("hull_type", 0);
            s.max_level = j.value("max_level", 0);

            if (j.contains("rarity")) {
                auto& r = j["rarity"];
                if (r.is_string()) {
                    s.rarity = std::stoi(r.get<std::string>());
                } else {
                    s.rarity = r.get<int>();
                }
            }

            if (j.contains("build_cost") && j["build_cost"].is_array()) {
                for (auto& c : j["build_cost"]) {
                    ShipBuildCost bc;
                    bc.resource_id = c.value("resource_id", (int64_t)0);
                    bc.amount = c.value("amount", (int64_t)0);
                    s.build_cost.push_back(bc);
                }
            }

            if (j.contains("crew_slots") && j["crew_slots"].is_array()) {
                for (auto& cs : j["crew_slots"]) {
                    ShipCrewSlot sl;
                    sl.slots = cs.value("slots", 0);
                    sl.unlock_level = cs.value("unlock_level", 0);
                    s.crew_slots.push_back(sl);
                }
            }

            if (j.contains("levels") && j["levels"].is_array()) {
                for (auto& l : j["levels"]) {
                    ShipLevel sl;
                    sl.level = l.value("level", 0);
                    sl.xp = l.value("xp", 0);
                    sl.shield = l.value("shield", 0.0);
                    sl.health = l.value("health", 0.0);
                    s.levels.push_back(sl);
                }
            }

            data.ships[s.id] = std::move(s);
        }
    } catch (const json::exception& e) {
        return false;
    }

    report_progress("ships", 1, 1);
    return true;
}

// ---------------------------------------------------------------------------
// Fetch research
// ---------------------------------------------------------------------------

bool ApiClient::fetch_research(GameData& data) {
    report_progress("research", 0, 1);

    std::string body;
    if (is_cache_fresh("research.json")) {
        body = read_cache("research.json");
    } else {
        body = api_get("/research");
        if (body.empty()) {
            body = read_cache("research.json");
            if (body.empty()) return false;
        } else {
            write_cache("research.json", body);
        }
    }

    try {
        auto arr = json::parse(body);
        for (auto& j : arr) {
            Research r;
            r.id = j.value("id", (int64_t)0);
            r.art_id = j.value("art_id", 0);
            r.loca_id = j.value("loca_id", 0);
            r.view_level = j.value("view_level", 0);
            r.unlock_level = j.value("unlock_level", 0);
            r.research_tree = j.value("research_tree", (int64_t)0);

            if (j.contains("buffs") && j["buffs"].is_array()) {
                for (auto& b : j["buffs"]) {
                    ResearchBuff rb;
                    rb.id = b.value("id", (int64_t)0);
                    rb.value_is_percentage = b.value("value_is_percentage", false);
                    if (b.contains("values") && b["values"].is_array()) {
                        for (auto& v : b["values"]) {
                            rb.values.push_back(parse_ability_value(v));
                        }
                    }
                    r.buffs.push_back(std::move(rb));
                }
            }

            data.researches[r.id] = std::move(r);
        }
    } catch (const json::exception& e) {
        return false;
    }

    report_progress("research", 1, 1);
    return true;
}

// ---------------------------------------------------------------------------
// Fetch buildings
// ---------------------------------------------------------------------------

bool ApiClient::fetch_buildings(GameData& data) {
    report_progress("buildings", 0, 1);

    std::string body;
    if (is_cache_fresh("buildings.json")) {
        body = read_cache("buildings.json");
    } else {
        body = api_get("/building");
        if (body.empty()) {
            body = read_cache("buildings.json");
            if (body.empty()) return false;
        } else {
            write_cache("buildings.json", body);
        }
    }

    try {
        auto arr = json::parse(body);
        for (auto& j : arr) {
            Building b;
            b.id = j.value("id", (int64_t)0);

            if (j.contains("levels") && j["levels"].is_array()) {
                for (auto& l : j["levels"]) {
                    BuildingLevel bl;
                    bl.id = l.value("id", 0);
                    bl.player_strength = l.value("player_strength", (int64_t)0);
                    bl.strength = l.value("strength", (int64_t)0);
                    bl.generation = l.value("generation", 0);
                    bl.build_time_seconds = l.value("build_time_in_seconds", 0);
                    bl.hard_currency_cost = l.value("hard_currency_cost", 0);

                    if (l.contains("costs") && l["costs"].is_array()) {
                        for (auto& c : l["costs"]) {
                            ShipBuildCost bc;
                            bc.resource_id = c.value("resource_id", (int64_t)0);
                            bc.amount = c.value("amount", (int64_t)0);
                            bl.costs.push_back(bc);
                        }
                    }

                    if (l.contains("requirements") && l["requirements"].is_array()) {
                        for (auto& r : l["requirements"]) {
                            BuildingRequirement br;
                            if (r.contains("requirement_type")) {
                                auto& rt = r["requirement_type"];
                                if (rt.is_string()) br.requirement_type = rt.get<std::string>();
                                else br.requirement_type = std::to_string(rt.get<int>());
                            }
                            br.requirement_id = r.value("requirement_id", 0);
                            br.requirement_level = r.value("requirement_level", 0);
                            bl.requirements.push_back(std::move(br));
                        }
                    }

                    b.levels.push_back(std::move(bl));
                }
            }

            data.buildings[b.id] = std::move(b);
        }
    } catch (const json::exception& e) {
        return false;
    }

    report_progress("buildings", 1, 1);
    return true;
}

// ---------------------------------------------------------------------------
// Fetch resources
// ---------------------------------------------------------------------------

bool ApiClient::fetch_resources(GameData& data) {
    report_progress("resources", 0, 1);

    std::string body;
    if (is_cache_fresh("resources.json")) {
        body = read_cache("resources.json");
    } else {
        body = api_get("/resource");
        if (body.empty()) {
            body = read_cache("resources.json");
            if (body.empty()) return false;
        } else {
            write_cache("resources.json", body);
        }
    }

    try {
        auto arr = json::parse(body);
        for (auto& j : arr) {
            Resource r;
            r.id = j.value("id", (int64_t)0);
            data.resources[r.id] = std::move(r);
        }
    } catch (const json::exception& e) {
        return false;
    }

    report_progress("resources", 1, 1);
    return true;
}

// ---------------------------------------------------------------------------
// Fetch translations
// ---------------------------------------------------------------------------

bool ApiClient::fetch_translations(GameData& data, const std::string& lang) {
    struct TranslationTarget {
        std::string endpoint;
        std::string cache_file;
        std::string id_key;
    };

    std::vector<TranslationTarget> targets = {
        {"/translations/" + lang + "/officers", "translations_officers.json", "officer_name_short_"},
        {"/translations/" + lang + "/ships", "translations_ships.json", "ship_name_"},
        {"/translations/" + lang + "/researches", "translations_researches.json", "research_name_"},
        {"/translations/" + lang + "/buildings", "translations_buildings.json", "building_name_"},
        {"/translations/" + lang + "/resources", "translations_resources.json", "resource_name_"},
        {"/translations/" + lang + "/synergies", "translations_synergies.json", "synergy_name_"},
    };

    int step = 0;
    for (auto& target : targets) {
        report_progress("translations", step, (int)targets.size());

        std::string body;
        if (is_cache_fresh(target.cache_file)) {
            body = read_cache(target.cache_file);
        } else {
            body = api_get(target.endpoint);
            if (body.empty()) {
                body = read_cache(target.cache_file);
                if (body.empty()) { step++; continue; }
            } else {
                write_cache(target.cache_file, body);
            }
        }

        try {
            auto arr = json::parse(body);
            // Build a map: id -> {key -> text}
            std::map<std::string, std::map<std::string, std::string>> trans_map;
            for (auto& entry : arr) {
                std::string id = entry.value("id", std::string(""));
                std::string key = entry.value("key", std::string(""));
                std::string text = entry.value("text", std::string(""));
                trans_map[id][key] = text;
            }

            // Apply translations to game data
            if (target.endpoint.find("/officers") != std::string::npos) {
                for (auto& [id, officer] : data.officers) {
                    auto id_str = std::to_string(id);
                    if (trans_map.count(id_str)) {
                        auto& t = trans_map[id_str];
                        // Find name keys: officer_name_short_{loca_id} or officer_name_{loca_id}
                        for (auto& [k, v] : t) {
                            if (k.find("officer_name_short_") == 0) officer.short_name = v;
                            else if (k.find("officer_name_") == 0 && k.find("short") == std::string::npos) officer.name = v;
                            else if (k.find("officer_flavor_text_") == 0) officer.flavor_text = v;
                        }
                    }
                }
            } else if (target.endpoint.find("/ships") != std::string::npos) {
                for (auto& [id, ship] : data.ships) {
                    auto id_str = std::to_string(id);
                    if (trans_map.count(id_str)) {
                        auto& t = trans_map[id_str];
                        for (auto& [k, v] : t) {
                            if (k.find("ship_name_") == 0) ship.name = v;
                            else if (k.find("ship_description_") == 0) ship.description = v;
                            else if (k.find("ship_ability_name_") == 0) ship.ability_name = v;
                            else if (k.find("ship_ability_desc_") == 0) ship.ability_description = v;
                        }
                    }
                }
            } else if (target.endpoint.find("/researches") != std::string::npos) {
                for (auto& [id, research] : data.researches) {
                    auto id_str = std::to_string(id);
                    if (trans_map.count(id_str)) {
                        auto& t = trans_map[id_str];
                        for (auto& [k, v] : t) {
                            if (k.find("research_name_") == 0) research.name = v;
                            else if (k.find("research_description_") == 0) research.description = v;
                        }
                    }
                }
            } else if (target.endpoint.find("/buildings") != std::string::npos) {
                for (auto& [id, building] : data.buildings) {
                    auto id_str = std::to_string(id);
                    if (trans_map.count(id_str)) {
                        auto& t = trans_map[id_str];
                        for (auto& [k, v] : t) {
                            if (k.find("building_name_") == 0) building.name = v;
                            else if (k.find("building_description_") == 0) building.description = v;
                        }
                    }
                }
            } else if (target.endpoint.find("/resources") != std::string::npos) {
                for (auto& [id, resource] : data.resources) {
                    auto id_str = std::to_string(id);
                    if (trans_map.count(id_str)) {
                        auto& t = trans_map[id_str];
                        for (auto& [k, v] : t) {
                            if (k.find("resource_name_") == 0) resource.name = v;
                        }
                    }
                }
            } else if (target.endpoint.find("/synergies") != std::string::npos) {
                // Map synergy IDs to officer group names
                // Key format: fleet_officer_synergy_group_{id}
                std::map<int64_t, std::string> synergy_names;
                for (auto& [id_str, keys] : trans_map) {
                    for (auto& [k, v] : keys) {
                        if (k.find("fleet_officer_synergy_group_") == 0) {
                            try {
                                synergy_names[std::stoll(id_str)] = v;
                            } catch (...) {}
                        }
                    }
                }
                for (auto& [id, officer] : data.officers) {
                    if (synergy_names.count(officer.synergy_id)) {
                        officer.group_name = synergy_names[officer.synergy_id];
                    }
                }
            }

        } catch (const json::exception& e) {
            // Continue with other translations
        }

        step++;
    }

    report_progress("translations", (int)targets.size(), (int)targets.size());
    return true;
}

// ---------------------------------------------------------------------------
// Fetch all
// ---------------------------------------------------------------------------

bool ApiClient::fetch_all(GameData& data) {
    bool ok = true;
    ok = fetch_officers(data) && ok;
    ok = fetch_ships(data) && ok;
    ok = fetch_research(data) && ok;
    ok = fetch_buildings(data) && ok;
    ok = fetch_resources(data) && ok;
    ok = fetch_translations(data) && ok;
    return ok;
}

} // namespace stfc
