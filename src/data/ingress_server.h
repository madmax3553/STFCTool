#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include "data/models.h"

namespace stfc {

// A single sync event (received POST from the community mod)
struct SyncEvent {
    std::chrono::system_clock::time_point timestamp;
    std::string data_type;
    int record_count = 0;
    bool success = true;
    std::string error;  // non-empty on failure
};

// Local HTTP server that receives sync data from the STFC community mod.
// The mod POSTs JSON to /sync/ingress/ with a stfc-sync-token header.
class IngressServer {
public:
    explicit IngressServer(const std::string& data_dir = "data/player_data",
                           int port = 8270);
    ~IngressServer();

    // Start the server in a background thread
    bool start();

    // Stop the server
    void stop();

    // Check if server is running
    bool is_running() const { return running_.load(); }

    // Get the port
    int port() const { return port_; }

    // Set the expected sync token (for validation)
    void set_token(const std::string& token) { token_ = token; }

    // Get last received player data
    PlayerData get_player_data();

    // Get sync event log (most recent last)
    std::vector<SyncEvent> get_sync_log();

    // Set callback for when new data arrives
    using DataCallback = std::function<void(const std::string& data_type)>;
    void set_data_callback(DataCallback cb) { data_cb_ = std::move(cb); }

private:
    std::string data_dir_;
    int port_;
    std::string token_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    std::mutex data_mutex_;
    PlayerData player_data_;
    std::vector<SyncEvent> sync_log_;
    DataCallback data_cb_;

    void run_server();
    bool save_sync_data(const std::string& data_type, const std::string& json_body);
    bool validate_token(const std::string& provided_token);
    void add_sync_event(const std::string& data_type, int count, bool success, const std::string& error = "");
    void save_player_data();  // persist player_data_ to JSON on disk
    void load_player_data();  // restore player_data_ from disk
};

} // namespace stfc
