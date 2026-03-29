#include "data/ingress_server.h"

#include <fstream>
#include <filesystem>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace stfc {

IngressServer::IngressServer(const std::string& data_dir, int port)
    : data_dir_(data_dir), port_(port) {
    fs::create_directories(data_dir_);
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
    svr.Post("/sync/ingress/", [this](const httplib::Request& req, httplib::Response& res) {
        // Validate token
        auto token_header = req.get_header_value("stfc-sync-token");
        if (!validate_token(token_header)) {
            res.status = 401;
            res.set_content(R"({"error":"invalid token"})", "application/json");
            add_sync_event("auth_fail", 0, false, "invalid token");
            return;
        }

        // Determine data type from content or header
        std::string data_type = req.get_header_value("stfc-sync-type");
        if (data_type.empty()) {
            // Try to infer from the JSON content
            try {
                auto j = json::parse(req.body);
                if (j.contains("type")) {
                    data_type = j["type"].get<std::string>();
                }
            } catch (...) {
                data_type = "unknown";
            }
        }

        // Save raw JSON
        save_sync_data(data_type, req.body);

        // Parse and update player data
        int record_count = 0;
        try {
            auto j = json::parse(req.body);
            std::lock_guard<std::mutex> lock(data_mutex_);

            if (data_type == "officers" || data_type == "officer") {
                player_data_.officers.clear();
                if (j.contains("data") && j["data"].is_array()) {
                    for (auto& o : j["data"]) {
                        PlayerOfficer po;
                        po.officer_id = o.value("id", (int64_t)0);
                        po.level = o.value("level", 0);
                        po.rank = o.value("rank", 0);
                        player_data_.officers.push_back(po);
                    }
                    record_count = (int)player_data_.officers.size();
                }
            } else if (data_type == "ships" || data_type == "ship") {
                player_data_.ships.clear();
                if (j.contains("data") && j["data"].is_array()) {
                    for (auto& s : j["data"]) {
                        PlayerShip ps;
                        ps.ship_id = s.value("id", (int64_t)0);
                        ps.tier = s.value("tier", 0);
                        ps.level = s.value("level", 0);
                        player_data_.ships.push_back(ps);
                    }
                    record_count = (int)player_data_.ships.size();
                }
            } else if (data_type == "resources" || data_type == "resource") {
                player_data_.resources.clear();
                if (j.contains("data") && j["data"].is_array()) {
                    for (auto& r : j["data"]) {
                        PlayerResource pr;
                        pr.resource_id = r.value("id", (int64_t)0);
                        pr.amount = r.value("amount", (int64_t)0);
                        player_data_.resources.push_back(pr);
                    }
                    record_count = (int)player_data_.resources.size();
                }
            } else if (data_type == "research") {
                player_data_.researches.clear();
                if (j.contains("data") && j["data"].is_array()) {
                    for (auto& r : j["data"]) {
                        PlayerResearch pr;
                        pr.research_id = r.value("id", (int64_t)0);
                        pr.level = r.value("level", 0);
                        player_data_.researches.push_back(pr);
                    }
                    record_count = (int)player_data_.researches.size();
                }
            } else if (data_type == "buildings" || data_type == "building") {
                player_data_.buildings.clear();
                if (j.contains("data") && j["data"].is_array()) {
                    for (auto& b : j["data"]) {
                        PlayerBuilding pb;
                        pb.building_id = b.value("id", (int64_t)0);
                        pb.level = b.value("level", 0);
                        player_data_.buildings.push_back(pb);
                    }
                    record_count = (int)player_data_.buildings.size();
                }
            } else {
                // Unknown data type — still count records if there's a data array
                if (j.contains("data") && j["data"].is_array()) {
                    record_count = (int)j["data"].size();
                }
            }
        } catch (const std::exception& e) {
            add_sync_event(data_type, 0, false, e.what());
            res.set_content(R"({"status":"ok"})", "application/json");
            return;
        }

        // Log the successful event
        add_sync_event(data_type, record_count, true);

        // Notify callback
        if (data_cb_) {
            data_cb_(data_type);
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

} // namespace stfc
