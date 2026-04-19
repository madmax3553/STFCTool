// Standalone ingress server - runs without the TUI
#include "data/ingress_server.h"
#include <csignal>
#include <iostream>
#include <atomic>

static std::atomic<bool> quit{false};

void signal_handler(int) { quit.store(true); }

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    stfc::IngressServer server("data/player_data", 8270);
    server.set_data_callback([](const std::string& type) {
        std::cout << "[SYNC] Received: " << type << std::endl;
    });

    if (!server.start()) {
        std::cerr << "Failed to start ingress server on port 8270" << std::endl;
        return 1;
    }

    std::cout << "Ingress server running on port 8270. Ctrl+C to stop." << std::endl;

    while (!quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nShutting down..." << std::endl;
    server.stop();
    return 0;
}
