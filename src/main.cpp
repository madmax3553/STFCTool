#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"

#include "data/models.h"
#include "data/api_client.h"
#include "data/ingress_server.h"

using namespace ftxui;

namespace stfc {

// ---------------------------------------------------------------------------
// Dashboard state
// ---------------------------------------------------------------------------

struct AppState {
    GameData game_data;
    PlayerData player_data;
    ApiClient api_client;
    IngressServer ingress_server;

    int selected_tab = 0;
    bool data_loaded = false;
    bool loading = false;
    std::string status_message = "Press [R] to refresh game data from api.spocks.club";

    // Officer browser state
    int selected_officer = 0;
    std::string officer_filter;

    // Ship browser state
    int selected_ship = 0;
    std::string ship_filter;

    AppState() : api_client("data/game_data"), ingress_server("data/player_data", 8270) {}
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Strip Unity rich-text <color> tags from strings (used by translations)
[[maybe_unused]]
static std::string strip_color_tags(const std::string& text) {
    std::string result;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<' && text.substr(i).find("color") != std::string::npos) {
            auto end = text.find('>', i);
            if (end != std::string::npos) {
                i = end + 1;
                continue;
            }
        }
        if (text[i] == '<' && text.substr(i, 8) == "</color>") {
            i += 8;
            continue;
        }
        result += text[i];
        i++;
    }
    return result;
}

static std::string format_number(int64_t n) {
    if (n >= 1000000000) return std::to_string(n / 1000000000) + "." + std::to_string((n % 1000000000) / 100000000) + "B";
    if (n >= 1000000) return std::to_string(n / 1000000) + "." + std::to_string((n % 1000000) / 100000) + "M";
    if (n >= 1000) return std::to_string(n / 1000) + "." + std::to_string((n % 1000) / 100) + "K";
    return std::to_string(n);
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

static Element render_overview(AppState& state) {
    auto& gd = state.game_data;

    std::vector<std::vector<std::string>> table_data = {
        {"Category", "Count"},
        {"Officers", std::to_string(gd.officers.size())},
        {"Ships", std::to_string(gd.ships.size())},
        {"Research", std::to_string(gd.researches.size())},
        {"Buildings", std::to_string(gd.buildings.size())},
        {"Resources", std::to_string(gd.resources.size())},
    };

    auto table = Table(table_data);
    table.SelectAll().Border(LIGHT);
    table.SelectRow(0).Decorate(bold);
    table.SelectRow(0).DecorateCells(center);
    table.SelectColumn(1).DecorateCells(center);

    // Count by hull type (API: 0=interceptor, 1=survey, 2=explorer, 3=battleship)
    int interceptors = 0, explorers = 0, battleships = 0, surveys = 0;
    for (auto& [id, ship] : gd.ships) {
        switch (ship.hull_type) {
            case 0: interceptors++; break;
            case 1: surveys++; break;
            case 2: explorers++; break;
            case 3: battleships++; break;
        }
    }

    // Count by rarity
    int common = 0, uncommon = 0, rare = 0, epic = 0;
    for (auto& [id, officer] : gd.officers) {
        switch (officer.rarity) {
            case 1: common++; break;
            case 2: uncommon++; break;
            case 3: rare++; break;
            case 4: epic++; break;
        }
    }

    auto ship_breakdown = vbox({
        text("Ships by Type") | bold,
        separator(),
        hbox({text("  Interceptors: "), text(std::to_string(interceptors)) | color(Color::Cyan)}),
        hbox({text("  Explorers:    "), text(std::to_string(explorers)) | color(Color::Green)}),
        hbox({text("  Battleships:  "), text(std::to_string(battleships)) | color(Color::Red)}),
        hbox({text("  Surveys:      "), text(std::to_string(surveys)) | color(Color::Yellow)}),
    });

    auto officer_breakdown = vbox({
        text("Officers by Rarity") | bold,
        separator(),
        hbox({text("  Common:   "), text(std::to_string(common)) | color(Color::White)}),
        hbox({text("  Uncommon: "), text(std::to_string(uncommon)) | color(Color::Green)}),
        hbox({text("  Rare:     "), text(std::to_string(rare)) | color(Color::Blue)}),
        hbox({text("  Epic:     "), text(std::to_string(epic)) | color(Color::Magenta)}),
    });

    auto ingress_status = state.ingress_server.is_running()
        ? hbox({text(" INGRESS: "), text("RUNNING on port " + std::to_string(state.ingress_server.port())) | color(Color::Green)})
        : hbox({text(" INGRESS: "), text("STOPPED") | color(Color::Red)});

    return vbox({
        text("STFC Tool - Game Data Overview") | bold | center,
        separator(),
        hbox({
            table.Render() | flex,
            separator(),
            vbox({ship_breakdown, separator(), officer_breakdown}) | flex,
        }),
        separator(),
        ingress_status,
    });
}

static Element render_officers(AppState& state) {
    auto& gd = state.game_data;

    // Build sorted officer list
    std::vector<const Officer*> officers;
    for (auto& [id, o] : gd.officers) {
        if (!state.officer_filter.empty()) {
            std::string name_lower = o.name;
            std::string filter_lower = state.officer_filter;
            for (auto& c : name_lower) c = std::tolower(c);
            for (auto& c : filter_lower) c = std::tolower(c);
            if (name_lower.find(filter_lower) == std::string::npos &&
                o.short_name.find(state.officer_filter) == std::string::npos) {
                continue;
            }
        }
        officers.push_back(&o);
    }

    // Sort by name
    std::sort(officers.begin(), officers.end(), [](const Officer* a, const Officer* b) {
        return a->name < b->name;
    });

    // Build table
    std::vector<std::vector<std::string>> table_data;
    table_data.push_back({"Name", "Rarity", "Class", "Group", "Max Rank", "Max Atk", "Max Def", "Max HP"});

    for (auto* o : officers) {
        std::string max_atk = "-", max_def = "-", max_hp = "-";
        if (!o->stats.empty()) {
            auto& last = o->stats.back();
            max_atk = format_number((int64_t)last.attack);
            max_def = format_number((int64_t)last.defense);
            max_hp = format_number((int64_t)last.health);
        }
        table_data.push_back({
            o->name.empty() ? o->short_name : o->name,
            rarity_str(o->rarity),
            officer_class_str(o->officer_class),
            o->group_name,
            std::to_string(o->max_rank),
            max_atk, max_def, max_hp,
        });
    }

    if (table_data.size() <= 1) {
        return vbox({
            text("Officers") | bold | center,
            separator(),
            text("No officers loaded. Press [R] to refresh data.") | center,
        });
    }

    auto table = Table(table_data);
    table.SelectAll().Border(LIGHT);
    table.SelectRow(0).Decorate(bold);

    return vbox({
        text("Officers (" + std::to_string(officers.size()) + " total)") | bold | center,
        separator(),
        table.Render() | vscroll_indicator | yframe | flex,
    });
}

static Element render_ships(AppState& state) {
    auto& gd = state.game_data;

    std::vector<const Ship*> ships;
    for (auto& [id, s] : gd.ships) {
        if (!state.ship_filter.empty()) {
            std::string name_lower = s.name;
            std::string filter_lower = state.ship_filter;
            for (auto& c : name_lower) c = std::tolower(c);
            for (auto& c : filter_lower) c = std::tolower(c);
            if (name_lower.find(filter_lower) == std::string::npos) continue;
        }
        ships.push_back(&s);
    }

    std::sort(ships.begin(), ships.end(), [](const Ship* a, const Ship* b) {
        if (a->grade != b->grade) return a->grade < b->grade;
        return a->name < b->name;
    });

    std::vector<std::vector<std::string>> table_data;
    table_data.push_back({"Name", "Type", "Grade", "Rarity", "Max Tier", "Max Level", "Build Time"});

    for (auto* s : ships) {
        int hours = s->build_time_seconds / 3600;
        int mins = (s->build_time_seconds % 3600) / 60;
        std::string build_time = std::to_string(hours) + "h " + std::to_string(mins) + "m";
        if (s->build_time_seconds == 0) build_time = "-";

        table_data.push_back({
            s->name,
            hull_type_str(s->hull_type),
            "G" + std::to_string(s->grade),
            rarity_str(s->rarity),
            "T" + std::to_string(s->max_tier),
            std::to_string(s->max_level),
            build_time,
        });
    }

    if (table_data.size() <= 1) {
        return vbox({
            text("Ships") | bold | center,
            separator(),
            text("No ships loaded. Press [R] to refresh data.") | center,
        });
    }

    auto table = Table(table_data);
    table.SelectAll().Border(LIGHT);
    table.SelectRow(0).Decorate(bold);

    return vbox({
        text("Ships (" + std::to_string(ships.size()) + " total)") | bold | center,
        separator(),
        table.Render() | vscroll_indicator | yframe | flex,
    });
}

static Element render_sync(AppState& state) {
    bool running = state.ingress_server.is_running();

    auto config_example = vbox({
        text("Community Mod Config (config.toml)") | bold,
        separator(),
        text("[sync]") | color(Color::Yellow),
        text("url = \"http://localhost:" + std::to_string(state.ingress_server.port()) + "/sync/ingress/\"") | color(Color::Green),
        text("token = \"your-token-here\"") | color(Color::Green),
        text("resources = true"),
        text("battlelogs = true"),
        text("officer = true"),
        text("missions = true"),
        text("research = true"),
        text("tech = true"),
        text("traits = true"),
        text("buildings = true"),
        text("ships = true"),
    });

    auto status_box = vbox({
        text("Ingress Server Status") | bold,
        separator(),
        hbox({
            text("  Status: "),
            running ? text("RUNNING") | color(Color::Green) | bold
                    : text("STOPPED") | color(Color::Red) | bold,
        }),
        hbox({text("  Port:   "), text(std::to_string(state.ingress_server.port()))}),
        hbox({text("  URL:    "), text("http://localhost:" + std::to_string(state.ingress_server.port()) + "/sync/ingress/")}),
    });

    auto player_info = vbox({
        text("Synced Player Data") | bold,
        separator(),
        hbox({text("  Officers:  "), text(std::to_string(state.player_data.officers.size()))}),
        hbox({text("  Ships:     "), text(std::to_string(state.player_data.ships.size()))}),
        hbox({text("  Research:  "), text(std::to_string(state.player_data.researches.size()))}),
        hbox({text("  Buildings: "), text(std::to_string(state.player_data.buildings.size()))}),
        hbox({text("  Resources: "), text(std::to_string(state.player_data.resources.size()))}),
    });

    return vbox({
        text("Mod Sync Configuration") | bold | center,
        separator(),
        hbox({
            vbox({status_box, separator(), player_info}) | flex,
            separator(),
            config_example | flex,
        }),
        separator(),
        text("  Press [S] to start/stop the ingress server") | dim,
        text("  Configure the community mod to point to this URL") | dim,
    });
}

static Element render_status_bar(AppState& state) {
    return hbox({
        text(" STFC Tool v0.1 ") | bold | bgcolor(Color::Blue) | color(Color::White),
        text(" "),
        text(state.status_message) | flex,
        text(" "),
        text(" [R]efresh  [S]ync  [Q]uit ") | dim,
    });
}

} // namespace stfc

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    using namespace stfc;

    auto state = std::make_shared<AppState>();

    // Tab labels
    std::vector<std::string> tab_labels = {
        "Overview",
        "Officers",
        "Ships",
        "Sync",
    };

    int selected_tab = 0;
    auto tab_toggle = Toggle(&tab_labels, &selected_tab);

    // Main renderer
    auto main_renderer = Renderer(tab_toggle, [&]() {
        Element content;
        switch (selected_tab) {
            case 0: content = render_overview(*state); break;
            case 1: content = render_officers(*state); break;
            case 2: content = render_ships(*state); break;
            case 3: content = render_sync(*state); break;
            default: content = text("Unknown tab");
        }

        return vbox({
            tab_toggle->Render() | center,
            separator(),
            content | flex,
            separator(),
            render_status_bar(*state),
        }) | border;
    });

    // Handle keyboard events
    auto main_component = CatchEvent(main_renderer, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            auto screen = ScreenInteractive::Active();
            if (screen) screen->Exit();
            return true;
        }

        if (event == Event::Character('r') || event == Event::Character('R')) {
            if (!state->loading) {
                state->loading = true;
                state->status_message = "Fetching game data from api.spocks.club...";

                // Fetch in background thread
                std::thread([state]() {
                    state->api_client.set_progress_callback(
                        [state](const std::string& step, int current, int total) {
                            state->status_message = "Loading " + step + "...";
                        }
                    );

                    bool ok = state->api_client.fetch_all(state->game_data);
                    if (ok) {
                        state->status_message = "Loaded " +
                            std::to_string(state->game_data.officers.size()) + " officers, " +
                            std::to_string(state->game_data.ships.size()) + " ships, " +
                            std::to_string(state->game_data.researches.size()) + " research nodes";
                        state->data_loaded = true;
                    } else {
                        state->status_message = "Failed to fetch game data. Check network connection.";
                    }
                    state->loading = false;
                }).detach();
            }
            return true;
        }

        if (event == Event::Character('s') || event == Event::Character('S')) {
            if (state->ingress_server.is_running()) {
                state->ingress_server.stop();
                state->status_message = "Ingress server stopped.";
            } else {
                state->ingress_server.set_data_callback([state](const std::string& data_type) {
                    state->status_message = "Received sync data: " + data_type;
                    state->player_data = state->ingress_server.get_player_data();
                });
                if (state->ingress_server.start()) {
                    state->status_message = "Ingress server started on port " +
                        std::to_string(state->ingress_server.port());
                } else {
                    state->status_message = "Failed to start ingress server!";
                }
            }
            return true;
        }

        return false;
    });

    auto screen = ScreenInteractive::Fullscreen();
    screen.Loop(main_component);

    // Cleanup
    state->ingress_server.stop();

    return 0;
}
