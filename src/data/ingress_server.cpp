#include "data/ingress_server.h"

#include <fstream>
#include <filesystem>
#include <set>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace stfc {

IngressServer::IngressServer(const std::string& data_dir, int port)
    : data_dir_(data_dir), port_(port) {
    fs::create_directories(data_dir_);
    load_player_data();
}

IngressServer::~IngressServer() {
    stop();
}

bool IngressServer::validate_token(const std::string& provided_token) {
    if (token_.empty()) return true;  // No token set = accept all
    return provided_token == token_;
}

bool IngressServer::save_sync_data(const std::string& data_type, const std::string& json_body) {
    auto path = fs::path(data_dir_) / (data_type + ".json");
    std::ofstream f(path);
    if (!f) return false;
    f << json_body;
    return true;
}

PlayerData IngressServer::get_player_data() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return player_data_;
}

std::vector<SyncEvent> IngressServer::get_sync_log() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return sync_log_;
}

void IngressServer::add_sync_event(const std::string& data_type, int count, bool success, const std::string& error) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    SyncEvent ev;
    ev.timestamp = std::chrono::system_clock::now();
    ev.data_type = data_type;
    ev.record_count = count;
    ev.success = success;
    ev.error = error;
    sync_log_.push_back(ev);
    // Keep at most 50 events
    if (sync_log_.size() > 50) {
        sync_log_.erase(sync_log_.begin());
    }
}

void IngressServer::run_server() {
    httplib::Server svr;

    // Health check
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","service":"stfctool-ingress"})", "application/json");
    });

    // Sync ingress endpoint - receives data from community mod
    // Format: POST with bare JSON array [{type:"officer",...}, {type:"ship",...}]
    // Each element has a "type" field identifying the data type.
    // A single POST may contain mixed types.
    svr.Post("/sync/ingress/", [this](const httplib::Request& req, httplib::Response& res) {
        // Validate token
        auto token_header = req.get_header_value("stfc-sync-token");
        if (!validate_token(token_header)) {
            res.status = 401;
            res.set_content(R"({"error":"invalid token"})", "application/json");
            add_sync_event("auth_fail", 0, false, "invalid token from: " + token_header);
            return;
        }

        // Check for X-PRIME-SYNC header (first sync indicator)
        bool is_first_sync = (req.get_header_value("X-PRIME-SYNC") == "2");

        // Save raw JSON to disk
        save_sync_data("raw_" + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count()), req.body);

        // Parse the JSON body
        json j;
        try {
            j = json::parse(req.body);
        } catch (const std::exception& e) {
            add_sync_event("parse_error", 0, false, std::string("JSON parse: ") + e.what());
            res.set_content(R"({"status":"error","message":"invalid json"})", "application/json");
            return;
        }

        // The mod sends a bare JSON array: [{type:"officer",...}, ...]
        // Group elements by type and process
        std::map<std::string, int> type_counts;

        auto process_array = [&](const json& arr) {
            std::lock_guard<std::mutex> lock(data_mutex_);

            // If first sync, clear existing data for types present in this batch
            std::set<std::string> types_in_batch;
            for (auto& elem : arr) {
                if (elem.contains("type") && elem["type"].is_string()) {
                    types_in_batch.insert(elem["type"].get<std::string>());
                }
            }

            if (is_first_sync) {
                for (auto& t : types_in_batch) {
                    if (t == "officer") player_data_.officers.clear();
                    else if (t == "ship") player_data_.ships.clear();
                    else if (t == "research") player_data_.researches.clear();
                    else if (t == "module") player_data_.buildings.clear();
                    else if (t == "resource") player_data_.resources.clear();
                    else if (t == "buff" || t == "expired_buff") player_data_.buffs.clear();
                    else if (t == "job" || t == "completed_job") player_data_.jobs.clear();
                    else if (t == "inventory") player_data_.inventory.clear();
                    else if (t == "slot") player_data_.slots.clear();
                    else if (t == "trait") player_data_.traits.clear();
                    else if (t == "ft") player_data_.techs.clear();
                    else if (t == "mission" || t == "active_mission") player_data_.missions.clear();
                }
            }

            for (auto& elem : arr) {
                if (!elem.contains("type") || !elem["type"].is_string()) continue;
                std::string etype = elem["type"].get<std::string>();
                type_counts[etype]++;

                if (etype == "officer") {
                    PlayerOfficer po;
                    po.officer_id = elem.value("oid", (int64_t)0);
                    po.rank = elem.value("rank", 0);
                    po.level = elem.value("level", 0);
                    po.shard_count = elem.value("shard_count", 0);
                    // Upsert by officer_id
                    bool found = false;
                    for (auto& existing : player_data_.officers) {
                        if (existing.officer_id == po.officer_id) {
                            existing = po;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.officers.push_back(po);

                } else if (etype == "ship") {
                    PlayerShip ps;
                    ps.ship_id = elem.value("psid", (int64_t)0);
                    ps.hull_id = elem.value("hull_id", (int64_t)0);
                    ps.tier = elem.value("tier", 0);
                    ps.level = elem.value("level", 0);
                    ps.level_percentage = elem.value("level_percentage", 0.0);
                    bool found = false;
                    for (auto& existing : player_data_.ships) {
                        if (existing.ship_id == ps.ship_id) {
                            existing = ps;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.ships.push_back(ps);

                } else if (etype == "research") {
                    PlayerResearch pr;
                    pr.research_id = elem.value("rid", (int64_t)0);
                    pr.level = elem.value("level", 0);
                    bool found = false;
                    for (auto& existing : player_data_.researches) {
                        if (existing.research_id == pr.research_id) {
                            existing = pr;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.researches.push_back(pr);

                } else if (etype == "module") {
                    // "module" = building in STFC mod terminology
                    PlayerBuilding pb;
                    pb.building_id = elem.value("bid", (int64_t)0);
                    pb.level = elem.value("level", 0);
                    bool found = false;
                    for (auto& existing : player_data_.buildings) {
                        if (existing.building_id == pb.building_id) {
                            existing = pb;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.buildings.push_back(pb);

                } else if (etype == "resource") {
                    PlayerResource pr;
                    pr.resource_id = elem.value("rid", (int64_t)0);
                    pr.amount = elem.value("amount", (int64_t)0);
                    bool found = false;
                    for (auto& existing : player_data_.resources) {
                        if (existing.resource_id == pr.resource_id) {
                            existing = pr;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.resources.push_back(pr);

                } else if (etype == "buff") {
                    PlayerBuff pb;
                    pb.buff_id = elem.value("bid", (int64_t)0);
                    pb.level = elem.value("level", 0);
                    pb.expiry_time = elem.value("expiry_time", (int64_t)0);
                    pb.expired = false;
                    bool found = false;
                    for (auto& existing : player_data_.buffs) {
                        if (existing.buff_id == pb.buff_id) {
                            existing = pb;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.buffs.push_back(pb);

                } else if (etype == "expired_buff") {
                    int64_t bid = elem.value("bid", (int64_t)0);
                    for (auto& existing : player_data_.buffs) {
                        if (existing.buff_id == bid) {
                            existing.expired = true;
                            break;
                        }
                    }

                } else if (etype == "job") {
                    PlayerJob pj;
                    pj.uuid = elem.value("uuid", "");
                    pj.job_type = elem.value("job_type", 0);
                    pj.start_time = elem.value("start_time", (int64_t)0);
                    pj.duration = elem.value("duration", 0);
                    pj.reduction = elem.value("reduction", 0);
                    pj.research_id = elem.value("rid", (int64_t)0);
                    pj.level = elem.value("level", 0);
                    pj.completed = false;
                    bool found = false;
                    for (auto& existing : player_data_.jobs) {
                        if (existing.uuid == pj.uuid) {
                            existing = pj;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.jobs.push_back(pj);

                } else if (etype == "completed_job") {
                    std::string uuid = elem.value("uuid", "");
                    for (auto& existing : player_data_.jobs) {
                        if (existing.uuid == uuid) {
                            existing.completed = true;
                            break;
                        }
                    }

                } else if (etype == "inventory") {
                    PlayerInventoryItem pi;
                    pi.item_type = elem.value("item_type", 0);
                    pi.ref_id = elem.value("refid", (int64_t)0);
                    pi.count = elem.value("count", (int64_t)0);
                    bool found = false;
                    for (auto& existing : player_data_.inventory) {
                        if (existing.ref_id == pi.ref_id && existing.item_type == pi.item_type) {
                            existing = pi;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.inventory.push_back(pi);

                } else if (etype == "slot") {
                    PlayerSlot ps;
                    ps.slot_id = elem.value("sid", (int64_t)0);
                    ps.slot_type = elem.value("slot_type", 0);
                    ps.spec_id = elem.value("spec_id", (int64_t)0);
                    ps.item_id = elem.value("item_id", (int64_t)0);
                    bool found = false;
                    for (auto& existing : player_data_.slots) {
                        if (existing.slot_id == ps.slot_id) {
                            existing = ps;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.slots.push_back(ps);

                } else if (etype == "trait") {
                    PlayerTrait pt;
                    pt.officer_id = elem.value("oid", (int64_t)0);
                    pt.trait_id = elem.value("tid", (int64_t)0);
                    pt.level = elem.value("level", 0);
                    bool found = false;
                    for (auto& existing : player_data_.traits) {
                        if (existing.officer_id == pt.officer_id && existing.trait_id == pt.trait_id) {
                            existing = pt;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.traits.push_back(pt);

                } else if (etype == "ft") {
                    // Forbidden tech / chaos tech
                    PlayerTech pt;
                    pt.tech_id = elem.value("fid", (int64_t)0);
                    pt.tier = elem.value("tier", 0);
                    pt.level = elem.value("level", 0);
                    pt.shard_count = elem.value("shard_count", 0);
                    bool found = false;
                    for (auto& existing : player_data_.techs) {
                        if (existing.tech_id == pt.tech_id) {
                            existing = pt;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.techs.push_back(pt);

                } else if (etype == "mission" || etype == "active_mission") {
                    PlayerMission pm;
                    pm.mission_id = elem.value("mid", (int64_t)0);
                    pm.active = (etype == "active_mission");
                    bool found = false;
                    for (auto& existing : player_data_.missions) {
                        if (existing.mission_id == pm.mission_id) {
                            existing = pm;
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.missions.push_back(pm);

                } else if (etype == "battlelog") {
                    // Save raw to disk but don't parse into memory
                    save_sync_data("battlelog", elem.dump());
                }
                // else: unknown type, counted but not processed
            }

            player_data_.last_sync = std::chrono::system_clock::now();
        };

        if (j.is_array()) {
            process_array(j);
        } else if (j.is_object()) {
            // Some endpoints might wrap in an object — handle both
            if (j.contains("data") && j["data"].is_array()) {
                process_array(j["data"]);
            } else {
                // Single object — wrap in array
                json arr = json::array();
                arr.push_back(j);
                process_array(arr);
            }
        }

        // Log events per type
        int total = 0;
        for (auto& [t, c] : type_counts) {
            add_sync_event(t, c, true);
            total += c;
        }
        if (type_counts.empty()) {
            add_sync_event("empty", 0, true, "no typed records in payload");
        }

        // Persist player data to disk
        save_player_data();

        // Notify callback for each type received
        if (data_cb_) {
            for (auto& [t, c] : type_counts) {
                data_cb_(t);
            }
        }

        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // Also handle without trailing slash
    svr.Post("/sync/ingress", [&svr](const httplib::Request& req, httplib::Response& res) {
        // Forward to the slash version
        res.status = 308;
        res.set_header("Location", "/sync/ingress/");
    });

    running_ = true;
    svr.listen("0.0.0.0", port_);
    running_ = false;
}

bool IngressServer::start() {
    if (running_) return true;
    server_thread_ = std::thread([this]() { run_server(); });
    // Wait briefly for server to start
    for (int i = 0; i < 50 && !running_; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return running_;
}

void IngressServer::stop() {
    running_ = false;
    if (server_thread_.joinable()) {
        server_thread_.detach();  // Server thread will exit when svr stops
    }
}

void IngressServer::save_player_data() {
    // Called under data_mutex_ lock from the POST handler
    auto path = fs::path(data_dir_) / "player_data.json";
    try {
        json j;
        j["ops_level"] = player_data_.ops_level;
        j["player_name"] = player_data_.player_name;

        json officers = json::array();
        for (auto& o : player_data_.officers) {
            officers.push_back({{"oid", o.officer_id}, {"level", o.level},
                {"rank", o.rank}, {"shard_count", o.shard_count}});
        }
        j["officers"] = officers;

        json ships = json::array();
        for (auto& s : player_data_.ships) {
            ships.push_back({{"psid", s.ship_id}, {"hull_id", s.hull_id},
                {"tier", s.tier}, {"level", s.level}, {"level_pct", s.level_percentage}});
        }
        j["ships"] = ships;

        json research = json::array();
        for (auto& r : player_data_.researches) {
            research.push_back({{"rid", r.research_id}, {"level", r.level}});
        }
        j["research"] = research;

        json buildings = json::array();
        for (auto& b : player_data_.buildings) {
            buildings.push_back({{"bid", b.building_id}, {"level", b.level}});
        }
        j["buildings"] = buildings;

        json resources = json::array();
        for (auto& r : player_data_.resources) {
            resources.push_back({{"rid", r.resource_id}, {"amount", r.amount}});
        }
        j["resources"] = resources;

        json buffs = json::array();
        for (auto& b : player_data_.buffs) {
            buffs.push_back({{"bid", b.buff_id}, {"level", b.level},
                {"expiry", b.expiry_time}, {"expired", b.expired}});
        }
        j["buffs"] = buffs;

        json jobs = json::array();
        for (auto& jb : player_data_.jobs) {
            jobs.push_back({{"uuid", jb.uuid}, {"type", jb.job_type},
                {"start", jb.start_time}, {"duration", jb.duration},
                {"reduction", jb.reduction}, {"rid", jb.research_id},
                {"level", jb.level}, {"completed", jb.completed}});
        }
        j["jobs"] = jobs;

        json inventory = json::array();
        for (auto& i : player_data_.inventory) {
            inventory.push_back({{"item_type", i.item_type},
                {"refid", i.ref_id}, {"count", i.count}});
        }
        j["inventory"] = inventory;

        json slots = json::array();
        for (auto& s : player_data_.slots) {
            slots.push_back({{"sid", s.slot_id}, {"slot_type", s.slot_type},
                {"spec_id", s.spec_id}, {"item_id", s.item_id}});
        }
        j["slots"] = slots;

        json traits = json::array();
        for (auto& t : player_data_.traits) {
            traits.push_back({{"oid", t.officer_id}, {"tid", t.trait_id}, {"level", t.level}});
        }
        j["traits"] = traits;

        json techs = json::array();
        for (auto& t : player_data_.techs) {
            techs.push_back({{"fid", t.tech_id}, {"tier", t.tier},
                {"level", t.level}, {"shards", t.shard_count}});
        }
        j["techs"] = techs;

        json missions = json::array();
        for (auto& m : player_data_.missions) {
            missions.push_back({{"mid", m.mission_id}, {"active", m.active}});
        }
        j["missions"] = missions;

        std::ofstream f(path);
        if (f) f << j.dump(2);
    } catch (...) {
        // Best effort — don't crash the server
    }
}

void IngressServer::load_player_data() {
    auto path = fs::path(data_dir_) / "player_data.json";
    if (!fs::exists(path)) return;

    try {
        std::ifstream f(path);
        if (!f) return;
        json j = json::parse(f);

        std::lock_guard<std::mutex> lock(data_mutex_);
        player_data_.ops_level = j.value("ops_level", 0);
        player_data_.player_name = j.value("player_name", "");

        if (j.contains("officers") && j["officers"].is_array()) {
            player_data_.officers.clear();
            for (auto& o : j["officers"]) {
                PlayerOfficer po;
                po.officer_id = o.value("oid", (int64_t)0);
                po.level = o.value("level", 0);
                po.rank = o.value("rank", 0);
                po.shard_count = o.value("shard_count", 0);
                player_data_.officers.push_back(po);
            }
        }
        if (j.contains("ships") && j["ships"].is_array()) {
            player_data_.ships.clear();
            for (auto& s : j["ships"]) {
                PlayerShip ps;
                ps.ship_id = s.value("psid", (int64_t)0);
                ps.hull_id = s.value("hull_id", (int64_t)0);
                ps.tier = s.value("tier", 0);
                ps.level = s.value("level", 0);
                ps.level_percentage = s.value("level_pct", 0.0);
                player_data_.ships.push_back(ps);
            }
        }
        if (j.contains("research") && j["research"].is_array()) {
            player_data_.researches.clear();
            for (auto& r : j["research"]) {
                PlayerResearch pr;
                pr.research_id = r.value("rid", (int64_t)0);
                pr.level = r.value("level", 0);
                player_data_.researches.push_back(pr);
            }
        }
        if (j.contains("buildings") && j["buildings"].is_array()) {
            player_data_.buildings.clear();
            for (auto& b : j["buildings"]) {
                PlayerBuilding pb;
                pb.building_id = b.value("bid", (int64_t)0);
                pb.level = b.value("level", 0);
                player_data_.buildings.push_back(pb);
            }
        }
        if (j.contains("resources") && j["resources"].is_array()) {
            player_data_.resources.clear();
            for (auto& r : j["resources"]) {
                PlayerResource pr;
                pr.resource_id = r.value("rid", (int64_t)0);
                pr.amount = r.value("amount", (int64_t)0);
                player_data_.resources.push_back(pr);
            }
        }
        if (j.contains("buffs") && j["buffs"].is_array()) {
            player_data_.buffs.clear();
            for (auto& b : j["buffs"]) {
                PlayerBuff pb;
                pb.buff_id = b.value("bid", (int64_t)0);
                pb.level = b.value("level", 0);
                pb.expiry_time = b.value("expiry", (int64_t)0);
                pb.expired = b.value("expired", false);
                player_data_.buffs.push_back(pb);
            }
        }
        if (j.contains("jobs") && j["jobs"].is_array()) {
            player_data_.jobs.clear();
            for (auto& jb : j["jobs"]) {
                PlayerJob pj;
                pj.uuid = jb.value("uuid", "");
                pj.job_type = jb.value("type", 0);
                pj.start_time = jb.value("start", (int64_t)0);
                pj.duration = jb.value("duration", 0);
                pj.reduction = jb.value("reduction", 0);
                pj.research_id = jb.value("rid", (int64_t)0);
                pj.level = jb.value("level", 0);
                pj.completed = jb.value("completed", false);
                player_data_.jobs.push_back(pj);
            }
        }
        if (j.contains("inventory") && j["inventory"].is_array()) {
            player_data_.inventory.clear();
            for (auto& i : j["inventory"]) {
                PlayerInventoryItem pi;
                pi.item_type = i.value("item_type", 0);
                pi.ref_id = i.value("refid", (int64_t)0);
                pi.count = i.value("count", (int64_t)0);
                player_data_.inventory.push_back(pi);
            }
        }
        if (j.contains("slots") && j["slots"].is_array()) {
            player_data_.slots.clear();
            for (auto& s : j["slots"]) {
                PlayerSlot ps;
                ps.slot_id = s.value("sid", (int64_t)0);
                ps.slot_type = s.value("slot_type", 0);
                ps.spec_id = s.value("spec_id", (int64_t)0);
                ps.item_id = s.value("item_id", (int64_t)0);
                player_data_.slots.push_back(ps);
            }
        }
        if (j.contains("traits") && j["traits"].is_array()) {
            player_data_.traits.clear();
            for (auto& t : j["traits"]) {
                PlayerTrait pt;
                pt.officer_id = t.value("oid", (int64_t)0);
                pt.trait_id = t.value("tid", (int64_t)0);
                pt.level = t.value("level", 0);
                player_data_.traits.push_back(pt);
            }
        }
        if (j.contains("techs") && j["techs"].is_array()) {
            player_data_.techs.clear();
            for (auto& t : j["techs"]) {
                PlayerTech pt;
                pt.tech_id = t.value("fid", (int64_t)0);
                pt.tier = t.value("tier", 0);
                pt.level = t.value("level", 0);
                pt.shard_count = t.value("shards", 0);
                player_data_.techs.push_back(pt);
            }
        }
        if (j.contains("missions") && j["missions"].is_array()) {
            player_data_.missions.clear();
            for (auto& m : j["missions"]) {
                PlayerMission pm;
                pm.mission_id = m.value("mid", (int64_t)0);
                pm.active = m.value("active", false);
                player_data_.missions.push_back(pm);
            }
        }
    } catch (...) {
        // Best effort
    }
}

} // namespace stfc
