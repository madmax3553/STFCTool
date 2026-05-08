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

    // Allow stop() to terminate the server from another thread
    stop_requested_ = false;
    server_ptr_ = &svr;

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
                    if (elem.contains("components") && elem["components"].is_array()) {
                        for (auto& comp : elem["components"]) {
                            if (comp.is_number()) ps.components.push_back(comp.get<int64_t>());
                        }
                    }
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
                    if (elem.contains("expiry_time") && elem["expiry_time"].is_number()) {
                        pb.expiry_time = elem["expiry_time"].get<int64_t>();
                    }
                    // else: expiry_time stays nullopt (permanent buff)
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
                    pj.building_id = elem.value("bid", (int64_t)0);
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
                    if (elem.contains("params") && elem["params"].is_object()) {
                        auto& params = elem["params"];
                        if (params.contains("expiry_time") && params["expiry_time"].is_number()) {
                            ps.expiry_time = params["expiry_time"].get<int64_t>();
                        }
                    }
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

                } else if (etype == "emerald_chain") {
                    player_data_.emerald_chain.level = elem.value("level", 0);

                } else if (etype == "platform_event") {
                    PlayerEvent pe;
                    pe.config_id = elem.value("config_id", "");
                    pe.source = elem.value("source", "");
                    pe.event_type = elem.value("event_type", "");
                    pe.category = static_cast<EventCategory>(elem.value("category", 0));
                    pe.placement_type = elem.value("placement_type", 0);
                    pe.group_name = elem.value("group_name", "");
                    pe.category_group_id = elem.value("category_group_id", "");

                    if (elem.contains("schedule") && elem["schedule"].is_object()) {
                        auto& s = elem["schedule"];
                        pe.schedule.announce = s.value("announce", (int64_t)0);
                        pe.schedule.start = s.value("start", (int64_t)0);
                        pe.schedule.display = s.value("display", (int64_t)0);
                        pe.schedule.delay = s.value("delay", (int64_t)0);
                        pe.schedule.end = s.value("end", (int64_t)0);
                        pe.schedule.next_start = s.value("next_start", (int64_t)0);
                        pe.schedule.round_number = s.value("round_number", 0);
                    }

                    if (elem.contains("ranking") && elem["ranking"].is_object()) {
                        auto& r = elem["ranking"];
                        pe.ranking.id = r.value("id", "");
                        pe.ranking.position = r.value("position", 0);
                        pe.ranking.rank = r.value("rank", 0);
                        pe.ranking.score = r.value("score", 0.0);
                        pe.ranking.delta = r.value("delta", 0);
                        pe.ranking.relative_position = r.value("relative_position", 0);
                    }

                    if (elem.contains("entry_data") && elem["entry_data"].is_object()) {
                        auto& e = elem["entry_data"];
                        pe.entry_data.is_registered = e.value("is_registered", false);
                        pe.entry_data.can_claim = e.value("can_claim", false);
                        pe.entry_data.last_claimed_reward_index = e.value("last_claimed_reward_index", -1);
                        pe.entry_data.join_forbidden = e.value("join_forbidden", 0);
                    }

                    if (elem.contains("metadata") && elem["metadata"].is_object()) {
                        auto& m = elem["metadata"];
                        pe.metadata.is_auto_register = m.value("is_auto_register", false);
                        pe.metadata.auto_reward = m.value("auto_reward", false);
                        pe.metadata.immediate_reward = m.value("immediate_reward", false);
                        pe.metadata.is_cross_server = m.value("is_cross_server", false);
                        pe.metadata.cta = m.value("cta", (int64_t)0);
                        pe.metadata.priority = m.value("priority", (int64_t)0);
                        pe.metadata.icon_asset_id = m.value("icon_asset_id", "");
                        pe.metadata.battle_pass_link = m.value("battle_pass_link", "");
                        pe.metadata.battle_pass_resource_id = m.value("battle_pass_resource_id", "");
                        pe.metadata.battle_pass_type = m.value("battle_pass_type", 0);
                        pe.metadata.meta_event_day = m.value("meta_event_day", 0);
                        pe.metadata.meta_event_section = m.value("meta_event_section", 0);
                    }

                    if (elem.contains("segments") && elem["segments"].is_array()) {
                        for (auto& seg : elem["segments"]) {
                            EventSegment es;
                            es.type = seg.value("type", 0);
                            if (seg.contains("values") && seg["values"].is_array()) {
                                for (auto& v : seg["values"]) {
                                    if (v.is_number()) es.values.push_back(v.get<int64_t>());
                                }
                            }
                            if (seg.contains("rewards") && seg["rewards"].is_array()) {
                                for (auto& rew : seg["rewards"]) {
                                    EventReward er;
                                    er.amount = rew.value("amount", (int64_t)0);
                                    er.type = rew.value("type", "");
                                    er.level = rew.value("level", "");
                                    if (rew.contains("position") && rew["position"].is_array()) {
                                        for (auto& p : rew["position"]) {
                                            if (p.is_number()) er.position.push_back(p.get<int64_t>());
                                        }
                                    }
                                    es.rewards.push_back(std::move(er));
                                }
                            }
                            pe.segments.push_back(std::move(es));
                        }
                    }

                    // Upsert by config_id
                    bool found = false;
                    for (auto& existing : player_data_.events) {
                        if (existing.config_id == pe.config_id) {
                            existing = std::move(pe);
                            found = true;
                            break;
                        }
                    }
                    if (!found) player_data_.events.push_back(std::move(pe));
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
        for (auto& [t, c] : type_counts) {
            add_sync_event(t, c, true);
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
    server_ptr_ = nullptr;
    running_ = false;
}

bool IngressServer::start() {
    if (running_) return true;
    stop_requested_ = false;
    server_thread_ = std::thread([this]() { run_server(); });
    // Wait briefly for server to start
    for (int i = 0; i < 50 && !running_; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return running_;
}

void IngressServer::stop() {
    if (!running_) return;
    stop_requested_ = true;
    // Tell httplib::Server to stop accepting connections
    if (server_ptr_) {
        static_cast<httplib::Server*>(server_ptr_)->stop();
    }
    // Wait for the server thread to finish cleanly
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void IngressServer::save_player_data() {
    // Called under data_mutex_ lock from the POST handler
    auto path = fs::path(data_dir_) / "player_data.json";

    // Derive ops_level from Operations Center (building_id 0) if not already set
    if (player_data_.ops_level <= 0) {
        for (const auto& b : player_data_.buildings) {
            if (b.building_id == 0 && b.level > 0) {
                player_data_.ops_level = b.level;
                break;
            }
        }
    }

    try {
        json j;
        j["ops_level"] = player_data_.ops_level;
        j["player_name"] = player_data_.player_name;

        // Persist last_sync as a unix timestamp (seconds since epoch)
        if (player_data_.last_sync != std::chrono::system_clock::time_point{}) {
            j["last_sync"] = std::chrono::duration_cast<std::chrono::seconds>(
                player_data_.last_sync.time_since_epoch()).count();
        }

        json officers = json::array();
        for (auto& o : player_data_.officers) {
            officers.push_back({{"oid", o.officer_id}, {"level", o.level},
                {"rank", o.rank}, {"shard_count", o.shard_count}});
        }
        j["officers"] = officers;

        json ships = json::array();
        for (auto& s : player_data_.ships) {
            json sj = {{"psid", s.ship_id}, {"hull_id", s.hull_id},
                {"tier", s.tier}, {"level", s.level}, {"level_pct", s.level_percentage}};
            if (!s.components.empty()) {
                sj["components"] = s.components;
            }
            ships.push_back(sj);
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
            json bj = {{"bid", b.buff_id}, {"level", b.level}, {"expired", b.expired}};
            if (b.expiry_time.has_value()) {
                bj["expiry"] = b.expiry_time.value();
            }
            buffs.push_back(bj);
        }
        j["buffs"] = buffs;

        json jobs = json::array();
        for (auto& jb : player_data_.jobs) {
            jobs.push_back({{"uuid", jb.uuid}, {"type", jb.job_type},
                {"start", jb.start_time}, {"duration", jb.duration},
                {"reduction", jb.reduction}, {"rid", jb.research_id},
                {"bid", jb.building_id},
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
            json sj = {{"sid", s.slot_id}, {"slot_type", s.slot_type},
                {"spec_id", s.spec_id}, {"item_id", s.item_id}};
            if (s.expiry_time.has_value()) {
                sj["expiry"] = s.expiry_time.value();
            }
            slots.push_back(sj);
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

        json events = json::array();
        for (auto& ev : player_data_.events) {
            json ej = {
                {"config_id", ev.config_id},
                {"source", ev.source},
                {"event_type", ev.event_type},
                {"category", static_cast<int>(ev.category)},
                {"placement_type", ev.placement_type},
                {"group_name", ev.group_name},
                {"category_group_id", ev.category_group_id},
                {"schedule", {
                    {"announce", ev.schedule.announce},
                    {"start", ev.schedule.start},
                    {"display", ev.schedule.display},
                    {"delay", ev.schedule.delay},
                    {"end", ev.schedule.end},
                    {"next_start", ev.schedule.next_start},
                    {"round_number", ev.schedule.round_number},
                }},
                {"ranking", {
                    {"id", ev.ranking.id},
                    {"position", ev.ranking.position},
                    {"rank", ev.ranking.rank},
                    {"score", ev.ranking.score},
                    {"delta", ev.ranking.delta},
                    {"relative_position", ev.ranking.relative_position},
                }},
                {"entry_data", {
                    {"is_registered", ev.entry_data.is_registered},
                    {"can_claim", ev.entry_data.can_claim},
                    {"last_claimed", ev.entry_data.last_claimed_reward_index},
                    {"join_forbidden", ev.entry_data.join_forbidden},
                }},
                {"priority", ev.metadata.priority},
            };
            // Segments (compact — just reward count for persistence)
            ej["segment_count"] = static_cast<int>(ev.segments.size());
            int total_rewards = 0;
            for (auto& seg : ev.segments)
                total_rewards += static_cast<int>(seg.rewards.size());
            ej["reward_count"] = total_rewards;
            events.push_back(ej);
        }
        j["events"] = events;

        j["emerald_chain_level"] = player_data_.emerald_chain.level;

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

        // Restore last_sync timestamp
        if (j.contains("last_sync") && j["last_sync"].is_number()) {
            auto epoch_secs = j["last_sync"].get<int64_t>();
            player_data_.last_sync = std::chrono::system_clock::time_point(
                std::chrono::seconds(epoch_secs));
        }

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
                if (s.contains("components") && s["components"].is_array()) {
                    for (auto& comp : s["components"]) {
                        if (comp.is_number()) ps.components.push_back(comp.get<int64_t>());
                    }
                }
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
                if (b.contains("expiry") && b["expiry"].is_number()) {
                    pb.expiry_time = b["expiry"].get<int64_t>();
                }
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
                pj.building_id = jb.value("bid", (int64_t)0);
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
                if (s.contains("expiry") && s["expiry"].is_number()) {
                    ps.expiry_time = s["expiry"].get<int64_t>();
                }
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
        if (j.contains("events") && j["events"].is_array()) {
            player_data_.events.clear();
            for (auto& ev : j["events"]) {
                PlayerEvent pe;
                pe.config_id = ev.value("config_id", "");
                pe.source = ev.value("source", "");
                pe.event_type = ev.value("event_type", "");
                pe.category = static_cast<EventCategory>(ev.value("category", 0));
                pe.placement_type = ev.value("placement_type", 0);
                pe.group_name = ev.value("group_name", "");
                pe.category_group_id = ev.value("category_group_id", "");
                if (ev.contains("schedule") && ev["schedule"].is_object()) {
                    auto& s = ev["schedule"];
                    pe.schedule.announce = s.value("announce", (int64_t)0);
                    pe.schedule.start = s.value("start", (int64_t)0);
                    pe.schedule.display = s.value("display", (int64_t)0);
                    pe.schedule.delay = s.value("delay", (int64_t)0);
                    pe.schedule.end = s.value("end", (int64_t)0);
                    pe.schedule.next_start = s.value("next_start", (int64_t)0);
                    pe.schedule.round_number = s.value("round_number", 0);
                }
                if (ev.contains("ranking") && ev["ranking"].is_object()) {
                    auto& r = ev["ranking"];
                    pe.ranking.id = r.value("id", "");
                    pe.ranking.position = r.value("position", 0);
                    pe.ranking.rank = r.value("rank", 0);
                    pe.ranking.score = r.value("score", 0.0);
                    pe.ranking.delta = r.value("delta", 0);
                    pe.ranking.relative_position = r.value("relative_position", 0);
                }
                if (ev.contains("entry_data") && ev["entry_data"].is_object()) {
                    auto& e = ev["entry_data"];
                    pe.entry_data.is_registered = e.value("is_registered", false);
                    pe.entry_data.can_claim = e.value("can_claim", false);
                    pe.entry_data.last_claimed_reward_index = e.value("last_claimed", -1);
                    pe.entry_data.join_forbidden = e.value("join_forbidden", 0);
                }
                pe.metadata.priority = ev.value("priority", (int64_t)0);
                player_data_.events.push_back(std::move(pe));
            }
        }

        player_data_.emerald_chain.level = j.value("emerald_chain_level", 0);

    } catch (...) {
        // Best effort
    }

    // Derive ops_level from Operations Center (building_id 0) if not already set
    // (the JSON may have ops_level=0 if it was saved before this derivation existed)
    if (player_data_.ops_level <= 0) {
        for (const auto& b : player_data_.buildings) {
            if (b.building_id == 0 && b.level > 0) {
                player_data_.ops_level = b.level;
                break;
            }
        }
    }
}

} // namespace stfc
