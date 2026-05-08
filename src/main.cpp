// ---------------------------------------------------------------------------
// STFCTool — Main entry point (thin shell)
//
// 3 tabs: Dashboard, Events, Sync
// All rendering/input is delegated to src/tui/tab_*.h headers.
// ---------------------------------------------------------------------------

#include <string>
#include <memory>
#include <thread>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <climits>
#include <algorithm>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "data/models.h"
#include "data/api_client.h"
#include "data/ingress_server.h"
#include "data/community_data.h"
#include "app/account_snapshot.h"
#include "app/action_planner.h"

#include "tui/ui_common.h"
#include "tui/tab_dashboard.h"
#include "tui/tab_events.h"
#include "tui/tab_sync.h"
#include "tui/tab_plan.h"

using namespace ftxui;
namespace fs = std::filesystem;

namespace stfc {

static bool contains_unresolved_plan_label(const std::string& value) {
    return value.find("Research#") != std::string::npos ||
           value.find("Building#") != std::string::npos ||
           value.find("Resource#") != std::string::npos ||
           value.find("Ship#") != std::string::npos;
}

static bool action_plan_needs_regeneration(const ActionPlan& plan) {
    if (plan.generated_at == 0 || plan.top_research.empty()) return true;

    for (const auto& r : plan.top_research) {
        if (contains_unresolved_plan_label(r.name) || r.description.empty()) return true;
    }
    for (const auto& a : plan.do_now) {
        if (contains_unresolved_plan_label(a.action) ||
            contains_unresolved_plan_label(a.reason)) return true;
    }
    for (const auto& s : plan.save_for) {
        if (contains_unresolved_plan_label(s.target)) return true;
    }
    for (const auto& a : plan.avoid) {
        if (contains_unresolved_plan_label(a.action) ||
            contains_unresolved_plan_label(a.reason)) return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------

struct AppState {
    // Core data
    GameData game_data;
    PlayerData player_data;
    CommunityData community_data;
    ApiClient api_client;
    IngressServer ingress_server;

    // Global UI state
    std::atomic<bool> data_loaded{false};
    std::atomic<bool> loading{false};
    bool show_help = false;
    std::mutex status_mutex;
    std::string status_message = "r:refresh  s:sync  ?:help";

    void set_status(const std::string& msg) {
        std::lock_guard<std::mutex> lk(status_mutex);
        status_message = msg;
    }
    std::string get_status() {
        std::lock_guard<std::mutex> lk(status_mutex);
        return status_message;
    }

    // Per-tab state
    EventsTabState events_state;
    SyncTabState sync_state;
    PlanTabState plan_state;
    ActionPlan action_plan;

    // Constructor: load cached data
    AppState() : api_client("data/game_data"), ingress_server("data/player_data", 8270) {
        api_client.set_cache_only(true);

        // Load cached game data
        if (fs::exists("data/game_data/officers.json")) {
            api_client.fetch_all(game_data);
            data_loaded = !game_data.officers.empty();
            if (data_loaded) {
                status_message = "Loaded " +
                    std::to_string(game_data.officers.size()) + " officers, " +
                    std::to_string(game_data.ships.size()) + " ships, " +
                    std::to_string(game_data.researches.size()) + " research. [R] to refresh, [H] help";
            }
        }

        // Load player data and resolve names
        player_data = ingress_server.get_player_data();
        if (data_loaded) {
            resolve_player_names(player_data, game_data);
        }

        // Load community data (StewieDoo Officer Tool)
        load_community_data(community_data);
        if (!community_data.officer_scores.empty()) {
            status_message += " | " +
                std::to_string(community_data.officer_scores.size()) + " scored, " +
                std::to_string(community_data.preset_crews.size()) + " crews";
        }

        bool loaded_plan = load_action_plan(action_plan);
        if (data_loaded && (!loaded_plan || action_plan_needs_regeneration(action_plan))) {
            auto snapshot = build_full_snapshot(player_data, game_data);
            action_plan = generate_action_plan(snapshot, 45, "growth");
            save_action_plan(action_plan);
            status_message += " | plan refreshed";
        }
    }
};

// ---------------------------------------------------------------------------
// Help overlay
// ---------------------------------------------------------------------------

static Element render_help() {
    return vbox({
        text("STFC Tool v0.6 — Keyboard Reference") | bold | center,
        separator(),
        text("Navigation") | bold | color(Color::Cyan),
        text("  j/k          Up / Down in list"),
        text("  h/l          Prev / Next view (Sync tab)"),
        text("  g/G          Jump to top / bottom"),
        text("  Ctrl+d/u     Half-page down / up"),
        text("  1-4          Switch tab"),
        separator(),
        text("Actions") | bold | color(Color::Cyan),
        text("  f            Cycle filter (Events)"),
        text("  r            Refresh game data"),
        text("  s            Toggle sync server"),
        text("  p/P          Generate action plan"),
        text("  ?            Toggle this help"),
        text("  q            Quit"),
    }) | border;
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

static Element render_status_bar(AppState& state) {
    return hbox({
        text(" STFC Tool v0.6 ") | bold | color(Color::Cyan),
        separator(),
        text(" " + state.get_status() + " ") | flex,
        separator(),
        text(" [?]help [q]uit ") | dim,
    });
}

} // namespace stfc

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main() {
    using namespace stfc;

    auto state = std::make_shared<AppState>();

    // Tab structure
    int selected_tab = 0;
    static const char* tab_labels[] = {"Dashboard", "Events", "Sync", "Plan"};
    static const int tab_count = 4;

    auto main_renderer = Renderer([&] {
        // Tab bar
        Elements tabs;
        for (int i = 0; i < tab_count; i++) {
            auto tab = text(" " + std::to_string(i+1) + ":" + std::string(tab_labels[i]) + " ");
            if (i == selected_tab) tab = tab | bold | inverted;
            else tab = tab | dim;
            tabs.push_back(tab);
        }

        // Content
        Element content;
        switch (selected_tab) {
            case 0:
                content = render_dashboard_tab(state->player_data, state->game_data, state->data_loaded);
                break;
            case 1:
                content = render_events_tab(state->player_data, state->events_state);
                break;
            case 2:
                content = render_sync_tab(state->player_data, state->game_data,
                                          state->ingress_server, state->sync_state);
                break;
            case 3:
                content = render_plan_tab(state->action_plan, state->plan_state);
                break;
            default:
                content = text("Unknown tab") | center;
        }

        // Help overlay
        if (state->show_help) {
            content = dbox({content, render_help() | center});
        }

        return vbox({
            hbox(tabs) | center,
            separator(),
            content | flex,
            separator(),
            render_status_bar(*state),
        });
    });

    // Event handler
    auto main_component = CatchEvent(main_renderer, [&](Event event) {
        // --- Global keys ---
        if (event == Event::Character('?')) {
            state->show_help = !state->show_help;
            return true;
        }
        if (event == Event::Character('q')) {
            auto screen = ScreenInteractive::Active();
            if (screen) screen->ExitLoopClosure()();
            return true;
        }

        // Tab switching: 1-3
        if (event == Event::Character('1')) { selected_tab = 0; return true; }
        if (event == Event::Character('2')) { selected_tab = 1; return true; }
        if (event == Event::Character('3')) { selected_tab = 2; return true; }
        if (event == Event::Character('4')) { selected_tab = 3; return true; }

        // Also keep Tab/Shift+Tab
        if (event == Event::Tab) {
            selected_tab = (selected_tab + 1) % tab_count;
            return true;
        }
        if (event == Event::TabReverse) {
            selected_tab = (selected_tab - 1 + tab_count) % tab_count;
            return true;
        }

        // Refresh game data
        if (event == Event::Character('r')) {
            if (!state->loading) {
                state->loading = true;
                state->set_status("Fetching game data from api.spocks.club...");
                std::thread([s = state]() {
                    s->api_client.set_cache_only(false);
                    s->api_client.fetch_all(s->game_data);
                    s->api_client.set_cache_only(true);
                    s->data_loaded = !s->game_data.officers.empty();

                    // Reload player data and resolve names
                    s->player_data = s->ingress_server.get_player_data();
                    if (s->data_loaded) {
                        resolve_player_names(s->player_data, s->game_data);
                    }

                    s->set_status("Refreshed: " +
                        std::to_string(s->game_data.officers.size()) + " officers, " +
                        std::to_string(s->game_data.ships.size()) + " ships, " +
                        std::to_string(s->game_data.researches.size()) + " research");
                    s->loading = false;
                    auto screen = ScreenInteractive::Active();
                    if (screen) screen->PostEvent(Event::Custom);
                }).detach();
            }
            return true;
        }

        // Toggle sync server
        if (event == Event::Character('s')) {
            if (state->ingress_server.is_running()) {
                state->ingress_server.stop();
                state->set_status("Sync server stopped");
            } else {
                state->ingress_server.start();
                state->set_status("Sync server started on port " + std::to_string(state->ingress_server.port()));

                // Reload player data after server starts
                state->player_data = state->ingress_server.get_player_data();
                if (state->data_loaded) {
                    resolve_player_names(state->player_data, state->game_data);
                }
            }
            return true;
        }

        // Generate live action plan
        if (event == Event::Character('p') || event == Event::Character('P')) {
            state->player_data = state->ingress_server.get_player_data();
            if (state->data_loaded) {
                resolve_player_names(state->player_data, state->game_data);
            }

            auto snapshot = build_full_snapshot(state->player_data, state->game_data);
            state->action_plan = generate_action_plan(snapshot, 45, "growth");
            bool saved = save_action_plan(state->action_plan);
            state->set_status("Plan generated: " +
                std::to_string(state->action_plan.do_now.size()) + " ready, " +
                std::to_string(std::min<size_t>(5, state->action_plan.top_research.size())) +
                " research" + (saved ? "" : " (save failed)"));
            selected_tab = 3;
            return true;
        }

        // Help overlay captures all keys
        if (state->show_help) {
            state->show_help = false;
            return true;
        }

        // --- Per-tab input (vim keys mapped to arrow events) ---
        // j/k -> down/up, h/l -> left/right, g/G -> home/end, Ctrl+d/u -> pgdn/pgup
        Event mapped = event;
        if (event == Event::Character('j')) mapped = Event::ArrowDown;
        else if (event == Event::Character('k')) mapped = Event::ArrowUp;
        else if (event == Event::Character('h')) mapped = Event::ArrowLeft;
        else if (event == Event::Character('l')) mapped = Event::ArrowRight;
        else if (event == Event::Character('g')) mapped = Event::Home;
        else if (event == Event::Character('G')) mapped = Event::End;
        else if (event.input() == "\x04") mapped = Event::PageDown;  // Ctrl+d
        else if (event.input() == "\x15") mapped = Event::PageUp;    // Ctrl+u

        switch (selected_tab) {
            case 1: { // Events
                int count = (int)state->player_data.events.size();
                // 'f' handled inside events handler
                if (mapped != event || event == Event::Character('f'))
                    return handle_events_input(mapped != event ? mapped : event, state->events_state, count);
                return handle_events_input(event, state->events_state, count);
            }
            case 2: // Sync
                return handle_sync_input(mapped != event ? mapped : event, state->sync_state);
        }

        return false;
    });

    auto screen = ScreenInteractive::Fullscreen();
    screen.Loop(main_component);

    // Shutdown
    state->ingress_server.stop();
    return 0;
}
