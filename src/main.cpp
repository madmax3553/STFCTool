#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"

#include "data/models.h"
#include "data/api_client.h"
#include "data/ingress_server.h"
#include "util/csv_import.h"
#include "core/crew_optimizer.h"
#include "core/planner.h"

using namespace ftxui;
namespace fs = std::filesystem;

namespace stfc {

// ---------------------------------------------------------------------------
// Dashboard state
// ---------------------------------------------------------------------------

struct AppState {
    GameData game_data;
    PlayerData player_data;
    ApiClient api_client;
    IngressServer ingress_server;

    int selected_tab_ = 0;  // unused — tab is tracked by local var in main()
    std::atomic<bool> data_loaded{false};
    std::atomic<bool> loading{false};
    bool show_help = false;
    std::mutex status_mutex;
    std::string status_message = "Press [R] to refresh game data, [H] for help";

    void set_status(const std::string& msg) {
        std::lock_guard<std::mutex> lk(status_mutex);
        status_message = msg;
    }
    std::string get_status() {
        std::lock_guard<std::mutex> lk(status_mutex);
        return status_message;
    }

    // Officer browser state
    int selected_officer = 0;
    std::string officer_filter;

    // Ship browser state
    int selected_ship = 0;
    std::string ship_filter;

    // Sync tab browser state
    int sync_view_mode = 0;   // 0=officers, 1=ships, 2=resources, 3=buildings, 4=research, 5=jobs, 6=buffs
    int sync_selected_row = 0;

    // Planner state
    Planner planner;
    DailyPlan daily_plan;
    WeeklyPlan weekly_plan;
    int selected_daily_task = 0;
    int selected_weekly_day = 0;   // 0=Mon..6=Sun
    int selected_weekly_goal = 0;
    bool show_completed = true;

    // Crew optimizer state
    std::vector<RosterOfficer> roster;
    std::unique_ptr<CrewOptimizer> optimizer;
    int crew_scenario = 0;         // index into all_dock_scenarios()
    int crew_ship_type = 0;        // 0=Explorer, 1=Battleship, 2=Interceptor
    std::vector<CrewResult> crew_results;
    std::vector<BdaSuggestion> crew_bda_results;  // BDA for selected crew
    int selected_crew = 0;
    bool crew_loaded = false;

    // Loadout state
    std::vector<DockConfig> dock_configs;
    LoadoutResult loadout_result;
    int selected_dock = 0;         // 0-6 dock selection
    int selected_dock_bda = 0;     // BDA selection within a dock
    bool loadout_computed = false;
    bool loadout_running = false;

    void init_dock_configs() {
        // Default 7-dock setup — common late-game configuration
        dock_configs.resize(7);
        dock_configs[0].scenario = Scenario::PvP;
        dock_configs[1].scenario = Scenario::Hybrid;
        dock_configs[2].scenario = Scenario::PvEHostile;
        dock_configs[3].scenario = Scenario::Armada;
        dock_configs[4].scenario = Scenario::MiningProtected;
        dock_configs[5].scenario = Scenario::MiningSpeed;
        dock_configs[6].scenario = Scenario::Loot;
    }

    AppState() : api_client("data/game_data"), ingress_server("data/player_data", 8270) {
        // Auto-load cached game data if available
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

        // Load player data from cache and resolve names against game data
        player_data = ingress_server.get_player_data();
        if (data_loaded) {
            resolve_player_names(player_data, game_data);
        }

        // Generate today's plans
        daily_plan = planner.generate_daily_plan();
        weekly_plan = planner.generate_weekly_plan();

        // Load saved state if available
        planner.load_daily(daily_plan, "data/player_data/daily_plan.json");
        planner.load_weekly(weekly_plan, "data/player_data/weekly_plan.json");

        // Enrich daily plan with player data context
        if (data_loaded) {
            planner.enrich_plan_with_player_data(daily_plan, player_data, game_data);
        }

        // Load roster if available
        if (fs::exists("roster.csv")) {
            roster = load_roster_csv("roster.csv");
            if (!roster.empty()) {
                optimizer = std::make_unique<CrewOptimizer>(roster);
                crew_loaded = true;
            }
        }

        // Initialize dock configs and try loading saved loadout
        init_dock_configs();
        if (fs::exists(".stfc_loadout.json")) {
            if (CrewOptimizer::load_loadout(loadout_result, ".stfc_loadout.json")) {
                loadout_computed = true;
            }
        }
    }

    void save_plans() {
        fs::create_directories("data/player_data");
        planner.save_daily(daily_plan, "data/player_data/daily_plan.json");
        planner.save_weekly(weekly_plan, "data/player_data/weekly_plan.json");
    }

    void run_crew_optimizer() {
        if (!optimizer) return;
        const auto& scenarios = all_dock_scenarios();
        if (crew_scenario < 0 || crew_scenario >= (int)scenarios.size()) return;

        ShipType st = ShipType::Explorer;
        if (crew_ship_type == 1) st = ShipType::Battleship;
        if (crew_ship_type == 2) st = ShipType::Interceptor;

        optimizer->set_ship_type(st);
        crew_results = optimizer->find_best_crews(scenarios[crew_scenario], 5);
        crew_bda_results.clear();

        // Auto-compute BDA for the top result
        if (!crew_results.empty()) {
            const auto& top = crew_results[0].breakdown;
            crew_bda_results = optimizer->find_best_bda(
                top.captain, top.bridge, scenarios[crew_scenario], 3);
        }
    }

    void update_crew_bda() {
        if (!optimizer || crew_results.empty()) return;
        if (selected_crew < 0 || selected_crew >= (int)crew_results.size()) return;

        const auto& scenarios = all_dock_scenarios();
        const auto& bd = crew_results[selected_crew].breakdown;
        crew_bda_results = optimizer->find_best_bda(
            bd.captain, bd.bridge, scenarios[crew_scenario], 3);
    }

    void run_loadout_optimizer() {
        if (!optimizer || dock_configs.empty()) return;
        loadout_running = true;

        // Set ship type for loadout
        ShipType st = ShipType::Explorer;
        if (crew_ship_type == 1) st = ShipType::Battleship;
        if (crew_ship_type == 2) st = ShipType::Interceptor;
        optimizer->set_ship_type(st);

        loadout_result = optimizer->optimize_dock_loadout(dock_configs, 1);
        loadout_computed = true;
        loadout_running = false;

        // Save loadout
        CrewOptimizer::save_loadout(loadout_result, ".stfc_loadout.json", st);
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[maybe_unused]]
static std::string strip_color_tags(const std::string& text) {
    std::string result;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<' && text.substr(i).find("color") != std::string::npos) {
            auto end = text.find('>', i);
            if (end != std::string::npos) { i = end + 1; continue; }
        }
        if (text[i] == '<' && text.substr(i, 8) == "</color>") { i += 8; continue; }
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

static Color priority_color(TaskPriority p) {
    switch (p) {
        case TaskPriority::Critical: return Color::Red;
        case TaskPriority::High:     return Color::Yellow;
        case TaskPriority::Medium:   return Color::Cyan;
        case TaskPriority::Low:      return Color::GrayDark;
    }
    return Color::White;
}

static Color category_color(TaskCategory c) {
    switch (c) {
        case TaskCategory::Events:    return Color::Magenta;
        case TaskCategory::SpeedUps:  return Color::Yellow;
        case TaskCategory::Ships:     return Color::Cyan;
        case TaskCategory::Research:  return Color::Blue;
        case TaskCategory::Officers:  return Color::Green;
        case TaskCategory::Mining:    return Color(Color::Gold1);
        case TaskCategory::Combat:    return Color::Red;
        case TaskCategory::Alliance:  return Color::MagentaLight;
        case TaskCategory::Store:     return Color::GreenLight;
        case TaskCategory::Misc:      return Color::GrayLight;
    }
    return Color::White;
}

// ---------------------------------------------------------------------------
// View: Overview — Player-centric dashboard
// ---------------------------------------------------------------------------

static Element render_overview(AppState& state) {
    auto& gd = state.game_data;
    auto& pd = state.player_data;
    bool has_player = !pd.officers.empty() || !pd.ships.empty() || !pd.resources.empty();

    // --- Player identity header ---
    std::string player_label = pd.player_name.empty() ? "Unknown Commander" : pd.player_name;
    std::string ops_label = pd.ops_level > 0 ? "Ops " + std::to_string(pd.ops_level) : "";

    // Last sync time
    std::string sync_label = "Never";
    if (pd.last_sync != std::chrono::system_clock::time_point{}) {
        auto tt = std::chrono::system_clock::to_time_t(pd.last_sync);
        std::tm tm_buf{};
        localtime_r(&tt, &tm_buf);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        sync_label = buf;
    }

    auto identity_box = vbox({
        hbox({
            text(player_label) | bold | color(Color::Cyan),
            ops_label.empty() ? text("") : hbox({text("  "), text(ops_label) | color(Color::Yellow) | bold}),
            filler(),
            text("Last sync: ") | dim,
            text(sync_label) | (pd.last_sync != std::chrono::system_clock::time_point{}
                ? color(Color::Green) : color(Color::GrayDark)),
        }),
    });

    // --- Resources panel (key resources with amounts) ---
    // Well-known resource IDs for STFC
    // We'll show all resources sorted by amount, highlighting the big 3 if found
    Elements resource_lines;
    resource_lines.push_back(text("Resources") | bold);
    resource_lines.push_back(separator());

    if (has_player && !pd.resources.empty()) {
        // Sort by amount descending, show top 12
        auto sorted_res = pd.resources;
        std::sort(sorted_res.begin(), sorted_res.end(),
            [](const PlayerResource& a, const PlayerResource& b) {
                return a.amount > b.amount;
            });

        int shown = 0;
        for (auto& r : sorted_res) {
            if (shown >= 12) break;
            if (r.amount == 0) continue;
            std::string name = r.name.empty() ? ("ID:" + std::to_string(r.resource_id)) : r.name;
            // Truncate long names
            if (name.size() > 22) name = name.substr(0, 20) + "..";
            resource_lines.push_back(hbox({
                text("  " + name) | size(WIDTH, EQUAL, 24),
                text(format_number(r.amount)) | bold | color(Color::Green),
            }));
            shown++;
        }
        if (shown == 0) {
            resource_lines.push_back(text("  No resources tracked") | dim);
        }
    } else {
        resource_lines.push_back(text("  No player data synced yet") | dim);
        resource_lines.push_back(text("  Start sync with [S]") | dim);
    }

    // --- Active Jobs panel ---
    Elements job_lines;
    job_lines.push_back(text("Active Jobs") | bold);
    job_lines.push_back(separator());

    if (has_player) {
        int active_count = 0;
        for (auto& j : pd.jobs) {
            if (j.completed) continue;
            int remaining = job_remaining_seconds(j);
            if (remaining < -3600) continue; // Skip jobs finished >1hr ago

            std::string type_str = job_type_str(j.job_type);
            std::string time_str = remaining > 0
                ? format_duration_short(remaining)
                : "DONE";
            Color time_color = remaining > 0
                ? (remaining < 300 ? Color::Yellow : Color::White)
                : Color::Green;

            job_lines.push_back(hbox({
                text("  " + type_str) | size(WIDTH, EQUAL, 18),
                text(time_str) | bold | color(time_color),
            }));
            active_count++;
        }
        if (active_count == 0) {
            job_lines.push_back(text("  No active jobs") | dim);
            job_lines.push_back(text("  Queue is empty!") | color(Color::Yellow));
        }
    } else {
        job_lines.push_back(text("  Awaiting sync data") | dim);
    }

    // --- Ship progression panel ---
    Elements ship_lines;
    ship_lines.push_back(text("Ship Fleet") | bold);
    ship_lines.push_back(separator());

    if (has_player && !pd.ships.empty()) {
        // Group by hull type, show top tier ships
        auto sorted_ships = pd.ships;
        std::sort(sorted_ships.begin(), sorted_ships.end(),
            [](const PlayerShip& a, const PlayerShip& b) {
                return a.tier > b.tier || (a.tier == b.tier && a.level > b.level);
            });

        ship_lines.push_back(hbox({
            text("  Total: ") | dim,
            text(std::to_string(pd.ships.size())) | bold,
            text(" ships") | dim,
        }));

        int shown = 0;
        for (auto& s : sorted_ships) {
            if (shown >= 6) break;
            std::string name = s.name.empty() ? ("Hull#" + std::to_string(s.hull_id)) : s.name;
            if (name.size() > 18) name = name.substr(0, 16) + "..";

            // Look up hull type from game data
            std::string type_tag = "";
            auto git = gd.ships.find(s.hull_id);
            if (git != gd.ships.end()) {
                type_tag = std::string(hull_type_str(git->second.hull_type)).substr(0, 3);
            }

            ship_lines.push_back(hbox({
                text("  " + name) | size(WIDTH, EQUAL, 20),
                text("T" + std::to_string(s.tier)) | bold | color(Color::Cyan),
                text(" Lv" + std::to_string(s.level)) | dim,
                type_tag.empty() ? text("") : hbox({text(" "), text(type_tag) | dim}),
            }));
            shown++;
        }
        if (pd.ships.size() > 6) {
            ship_lines.push_back(text("  +" + std::to_string(pd.ships.size() - 6) + " more...") | dim);
        }
    } else {
        ship_lines.push_back(text("  Awaiting sync data") | dim);
    }

    // --- Officer roster summary ---
    Elements officer_lines;
    officer_lines.push_back(text("Officer Roster") | bold);
    officer_lines.push_back(separator());

    if (has_player && !pd.officers.empty()) {
        officer_lines.push_back(hbox({
            text("  Total: ") | dim,
            text(std::to_string(pd.officers.size())) | bold,
            text(" officers") | dim,
        }));

        // Count by rank
        std::map<int, int> rank_counts;
        int max_level = 0;
        for (auto& o : pd.officers) {
            rank_counts[o.rank]++;
            if (o.level > max_level) max_level = o.level;
        }
        officer_lines.push_back(hbox({
            text("  Max level: ") | dim,
            text(std::to_string(max_level)) | bold | color(Color::Yellow),
        }));

        for (auto& [rank, count] : rank_counts) {
            officer_lines.push_back(hbox({
                text("  Rank " + std::to_string(rank) + ": ") | dim,
                text(std::to_string(count)) | bold,
            }));
        }
    } else {
        officer_lines.push_back(text("  Awaiting sync data") | dim);
    }

    // --- Daily progress summary ---
    auto& dp = state.daily_plan;
    std::string pct_str = std::to_string((int)dp.completion_pct()) + "%";
    auto daily_summary = vbox({
        text("Today's Progress") | bold,
        separator(),
        hbox({text("  Completed: "), text(std::to_string(dp.completed_tasks()) + "/" + std::to_string(dp.total_tasks())) | bold}),
        hbox({text("  Remaining: "), text(std::to_string(dp.remaining_tasks()) + " (" + std::to_string(dp.remaining_estimated_minutes()) + " min)")}),
        hbox({text("  Progress:  "), text(pct_str) | bold | color(dp.completion_pct() >= 80 ? Color::Green : dp.completion_pct() >= 50 ? Color::Yellow : Color::Red)}),
        gauge(dp.completion_pct() / 100.0) | color(Color::Green),
    });

    // --- Game data stats (compact) ---
    auto game_stats = vbox({
        text("Game Data") | bold,
        separator(),
        hbox({text("  Officers:  "), text(std::to_string(gd.officers.size())) | dim}),
        hbox({text("  Ships:     "), text(std::to_string(gd.ships.size())) | dim}),
        hbox({text("  Research:  "), text(std::to_string(gd.researches.size())) | dim}),
        hbox({text("  Buildings: "), text(std::to_string(gd.buildings.size())) | dim}),
        hbox({text("  Resources: "), text(std::to_string(gd.resources.size())) | dim}),
    });

    auto ingress_status = state.ingress_server.is_running()
        ? hbox({text(" INGRESS: "), text("RUNNING on port " + std::to_string(state.ingress_server.port())) | color(Color::Green)})
        : hbox({text(" INGRESS: "), text("STOPPED") | color(Color::GrayDark)});

    auto crew_status = state.crew_loaded
        ? hbox({text(" ROSTER: "), text(std::to_string(state.roster.size()) + " officers loaded") | color(Color::Green)})
        : hbox({text(" ROSTER: "), text("No roster.csv found") | color(Color::Yellow)});

    return vbox({
        text("STFC Tool - Dashboard") | bold | center,
        separator(),
        identity_box,
        separator(),
        hbox({
            vbox({vbox(resource_lines), separator(), vbox(job_lines)}) | flex,
            separator(),
            vbox({vbox(ship_lines), separator(), vbox(officer_lines)}) | flex,
            separator(),
            vbox({daily_summary, separator(), game_stats}) | flex,
        }) | flex,
        separator(),
        hbox({ingress_status, text("  "), crew_status}),
    }) | vscroll_indicator | yframe;
}

// ---------------------------------------------------------------------------
// View: Daily Planner
// ---------------------------------------------------------------------------

static Element render_daily_planner(AppState& state) {
    auto& plan = state.daily_plan;

    // Header with date and progress
    bool has_player_data = !state.player_data.officers.empty() || !state.player_data.jobs.empty();
    auto header = vbox({
        hbox({
            text("Daily Planner") | bold,
            text(" - "),
            text(plan.date) | color(Color::Cyan),
            text("  "),
            has_player_data
                ? (text("[LIVE]") | color(Color::Green) | bold)
                : (text("[no sync data]") | color(Color::GrayDark)),
            filler(),
            text(std::to_string(plan.completed_tasks()) + "/" + std::to_string(plan.total_tasks()) + " done") | bold,
            text("  ~" + std::to_string(plan.remaining_estimated_minutes()) + " min left") | dim,
        }),
        gauge(plan.completion_pct() / 100.0) | color(Color::Green),
    });

    // Task list sorted by effective_score (impact + dynamic boost)
    // No category grouping — priority order IS the display order
    Elements task_rows;

    // Find the first incomplete task index (for "DO THIS FIRST" highlight)
    int first_incomplete = -1;
    for (size_t i = 0; i < plan.tasks.size(); ++i) {
        if (!plan.tasks[i].completed && !plan.tasks[i].skipped) {
            first_incomplete = static_cast<int>(i);
            break;
        }
    }

    for (size_t i = 0; i < plan.tasks.size(); ++i) {
        const auto& t = plan.tasks[i];

        // Skip completed if toggled
        if (!state.show_completed && t.completed) continue;
        if (t.skipped && !state.show_completed) continue;

        // Task row
        bool selected = ((int)i == state.selected_daily_task);
        bool is_top_task = ((int)i == first_incomplete);
        auto dp = t.display_priority();

        std::string check = t.completed ? "[x]" : (t.skipped ? "[-]" : "[ ]");
        std::string pri_icon = priority_icon(dp);
        std::string time_str = t.estimated_minutes > 0
            ? "~" + std::to_string(t.estimated_minutes) + "m"
            : "";

        auto task_text = t.completed
            ? text(t.title) | dim | strikethrough
            : (t.skipped
                ? text(t.title) | dim
                : text(t.title));

        // "DO THIS FIRST" marker for the top incomplete task
        auto top_marker = is_top_task
            ? (text(" >> DO THIS FIRST") | color(Color::Green) | bold)
            : text("");

        // Urgency badge: [!] shown separately from priority for time-sensitive tasks
        auto urgency_badge = (t.urgent && !t.completed && !t.skipped)
            ? (text("[!]") | color(Color::Yellow) | bold)
            : text("   ");

        auto row = hbox({
            text("  "),
            text(check) | color(t.completed ? Color::Green : (t.skipped ? Color::GrayDark : Color::White)),
            text(" "),
            text(pri_icon) | color(priority_color(dp)),
            text(" "),
            urgency_badge,
            text(" "),
            text(std::string(category_icon(t.category)) + " ") | color(category_color(t.category)) | dim,
            task_text | flex,
            // Show inline status from player data
            t.queue_idle
                ? (text(" IDLE!") | color(Color::Red) | bold)
                : (t.has_active_job
                    ? (text(" active") | color(Color::Green) | dim)
                    : text("")),
            top_marker,
            text(" "),
            text(time_str) | dim,
        });

        if (selected) {
            row = row | inverted | focus;
        }

        task_rows.push_back(row);
    }

    // Detail panel for selected task
    Element detail = text("");
    if (state.selected_daily_task >= 0 && state.selected_daily_task < (int)plan.tasks.size()) {
        const auto& t = plan.tasks[state.selected_daily_task];
        Elements detail_lines;
        auto dp = t.display_priority();
        detail_lines.push_back(text(t.title) | bold);
        detail_lines.push_back(separator());
        detail_lines.push_back(hbox({
            text("Priority: "),
            text(priority_str(dp)) | color(priority_color(dp)),
            text("  (impact:" + std::to_string(t.impact_score)),
            t.dynamic_boost > 0
                ? (text("+" + std::to_string(t.dynamic_boost)) | color(Color::Green))
                : text(""),
            text(")") | dim,
        }));
        detail_lines.push_back(hbox({text("Category: "), text(category_str(t.category)) | color(category_color(t.category))}));
        if (t.urgent) {
            detail_lines.push_back(text("[!] TIME SENSITIVE") | color(Color::Yellow) | bold);
        }
        if (t.estimated_minutes > 0) {
            detail_lines.push_back(hbox({text("Time:     ~"), text(std::to_string(t.estimated_minutes) + " min")}));
        }
        if (!t.best_time.empty()) {
            detail_lines.push_back(hbox({text("When:     "), text(t.best_time) | color(Color::Cyan)}));
        }
        detail_lines.push_back(separator());

        // Word-wrap description to ~50 chars
        std::string desc = t.description;
        while (!desc.empty()) {
            if (desc.size() <= 55) {
                detail_lines.push_back(text(desc) | dim);
                break;
            }
            size_t cut = desc.rfind(' ', 55);
            if (cut == std::string::npos || cut == 0) cut = 55;
            detail_lines.push_back(text(desc.substr(0, cut)) | dim);
            desc = desc.substr(cut + (desc[cut] == ' ' ? 1 : 0));
        }

        // Player data context hints
        if (!t.context_hints.empty()) {
            detail_lines.push_back(separator());
            detail_lines.push_back(text("Your Status:") | bold | color(Color::Cyan));
            for (const auto& hint : t.context_hints) {
                Color hint_color = Color::White;
                if (hint.find("IDLE") != std::string::npos || hint.find("!!") != std::string::npos) {
                    hint_color = Color::Red;
                } else if (hint.find("DONE") != std::string::npos) {
                    hint_color = Color::Green;
                } else if (hint.find("left") != std::string::npos) {
                    hint_color = Color::Yellow;
                }
                detail_lines.push_back(text("  " + hint) | color(hint_color));
            }
        }

        if (t.progress_total > 0) {
            detail_lines.push_back(separator());
            detail_lines.push_back(hbox({
                text("Progress: "),
                text(std::to_string(t.progress_current) + "/" + std::to_string(t.progress_total)),
            }));
            detail_lines.push_back(gauge((double)t.progress_current / t.progress_total));
        }

        if (t.skipped && !t.skip_reason.empty()) {
            detail_lines.push_back(separator());
            detail_lines.push_back(hbox({text("Skipped: "), text(t.skip_reason) | color(Color::Yellow)}));
        }

        detail = vbox(detail_lines) | border;
    }

    return vbox({
        header,
        separator(),
        hbox({
            vbox(task_rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 60),
        }) | flex,
        separator(),
        hbox({
            text(" [SPACE] Toggle  ") | dim,
            text("[S] Skip  ") | dim,
            text("[C] Show/Hide Done  ") | dim,
            text("[Up/Down] Navigate") | dim,
        }),
    });
}

// ---------------------------------------------------------------------------
// View: Weekly Planner
// ---------------------------------------------------------------------------

static const char* short_dow(int i) {
    // i = 0..6 mapping to Mon..Sun
    static const char* names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    if (i >= 0 && i <= 6) return names[i];
    return "?";
}

static Element render_weekly_planner(AppState& state) {
    auto& plan = state.weekly_plan;

    // Header
    auto header = vbox({
        hbox({
            text("Weekly Planner") | bold,
            text(" - Week of "),
            text(plan.week_start) | color(Color::Cyan),
            filler(),
            text(std::to_string(plan.completed_goals()) + "/" + std::to_string(plan.goals.size()) + " goals") | bold,
        }),
        gauge(plan.goal_completion_pct() / 100.0) | color(Color::Blue),
    });

    // Day tabs with progress
    Elements day_tabs;
    for (int i = 0; i < 7 && i < (int)plan.days.size(); ++i) {
        bool sel = (i == state.selected_weekly_day);
        auto& day = plan.days[i];
        std::string label = std::string(short_dow(i)) + " " +
            std::to_string(day.completed_tasks()) + "/" + std::to_string(day.total_tasks());

        auto tab = text(label);
        if (sel) {
            tab = tab | bold | inverted;
        } else if (day.completion_pct() >= 100) {
            tab = tab | color(Color::Green);
        } else if (day.completion_pct() >= 50) {
            tab = tab | color(Color::Yellow);
        }
        day_tabs.push_back(tab);
        if (i < 6) day_tabs.push_back(text(" | ") | dim);
    }

    // Selected day's task summary
    Elements day_tasks;
    if (state.selected_weekly_day >= 0 && state.selected_weekly_day < (int)plan.days.size()) {
        auto& day = plan.days[state.selected_weekly_day];
        day_tasks.push_back(hbox({
            text(std::string(short_dow(state.selected_weekly_day)) + " - " + day.date) | bold,
            filler(),
            text(std::to_string((int)day.completion_pct()) + "% done") | dim,
        }));
        day_tasks.push_back(gauge(day.completion_pct() / 100.0) | color(Color::Green));
        day_tasks.push_back(separatorEmpty());

        // Show tasks grouped by priority
        for (auto pri : {TaskPriority::Critical, TaskPriority::High, TaskPriority::Medium, TaskPriority::Low}) {
            bool any = false;
            for (const auto& t : day.tasks) {
                if (t.priority != pri) continue;
                if (!any) {
                    day_tasks.push_back(
                        text(std::string(priority_str(pri)) + ":") | bold | color(priority_color(pri))
                    );
                    any = true;
                }
                std::string check = t.completed ? "[x]" : (t.skipped ? "[-]" : "[ ]");
                auto label = t.completed
                    ? text(t.title) | dim | strikethrough
                    : (t.skipped ? text(t.title) | dim : text(t.title));
                day_tasks.push_back(hbox({
                    text("  " + check + " ") | color(t.completed ? Color::Green : Color::White),
                    label | flex,
                    text("~" + std::to_string(t.estimated_minutes) + "m") | dim,
                }));
            }
        }
    }

    // Weekly goals panel
    Elements goal_rows;
    goal_rows.push_back(text("Weekly Goals") | bold | color(Color::Blue));
    goal_rows.push_back(separator());

    for (size_t i = 0; i < plan.goals.size(); ++i) {
        const auto& g = plan.goals[i];
        bool sel = ((int)i == state.selected_weekly_goal);

        std::string check = g.completed ? "[x]" : "[ ]";
        double pct = (g.progress_total > 0) ? (double)g.progress_current / g.progress_total : 0;
        std::string prog_str = std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total);

        auto row = hbox({
            text(check + " ") | color(g.completed ? Color::Green : Color::White),
            text(category_icon(g.category)) | color(category_color(g.category)),
            text(" "),
            (g.completed ? text(g.title) | dim | strikethrough : text(g.title)) | flex,
            text(prog_str) | dim,
        });

        if (sel) row = row | inverted | focus;
        goal_rows.push_back(row);

        // Mini progress bar
        goal_rows.push_back(hbox({
            text("      "),
            gauge(pct) | size(WIDTH, EQUAL, 30) | color(g.completed ? Color::Green : Color::Cyan),
        }));
    }

    // Goal detail
    Element goal_detail = text("");
    if (state.selected_weekly_goal >= 0 && state.selected_weekly_goal < (int)plan.goals.size()) {
        const auto& g = plan.goals[state.selected_weekly_goal];
        Elements lines;
        lines.push_back(text(g.title) | bold);
        lines.push_back(separator());
        lines.push_back(hbox({text("Category: "), text(category_str(g.category)) | color(category_color(g.category))}));
        lines.push_back(hbox({text("Priority: "), text(priority_str(g.priority)) | color(priority_color(g.priority))}));
        lines.push_back(hbox({text("Progress: "), text(std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total))}));
        lines.push_back(separator());

        std::string desc = g.description;
        while (!desc.empty()) {
            if (desc.size() <= 50) { lines.push_back(text(desc) | dim); break; }
            size_t cut = desc.rfind(' ', 50);
            if (cut == std::string::npos || cut == 0) cut = 50;
            lines.push_back(text(desc.substr(0, cut)) | dim);
            desc = desc.substr(cut + (desc[cut] == ' ' ? 1 : 0));
        }

        goal_detail = vbox(lines) | border;
    }

    return vbox({
        header,
        separator(),
        hbox(day_tabs) | center,
        separator(),
        hbox({
            vbox(day_tasks) | vscroll_indicator | yframe | flex,
            separator(),
            vbox({
                vbox(goal_rows) | vscroll_indicator | yframe | flex,
                separator(),
                goal_detail,
            }) | flex,
        }) | flex,
        separator(),
        hbox({
            text(" [Left/Right] Day  ") | dim,
            text("[Up/Down] Goal  ") | dim,
            text("[+/-] Goal Progress  ") | dim,
        }),
    });
}

// ---------------------------------------------------------------------------
// View: Crew Optimizer
// ---------------------------------------------------------------------------

static Element render_crew_optimizer(AppState& state) {
    if (!state.crew_loaded) {
        return vbox({
            text("Crew Optimizer") | bold | center,
            separator(),
            text("No roster loaded.") | center,
            text("Place roster.csv in the project root and restart.") | center | dim,
            text("Export from the STFC spreadsheet to CSV format.") | center | dim,
        });
    }

    const auto& scenarios = all_dock_scenarios();
    int safe_scenario = std::clamp(state.crew_scenario, 0, (int)scenarios.size() - 1);
    std::string scenario_name = scenario_label(scenarios[safe_scenario]);

    static const char* ship_names[] = {"Explorer", "Battleship", "Interceptor"};
    int safe_ship = std::clamp(state.crew_ship_type, 0, 2);
    std::string ship_name = ship_names[safe_ship];

    auto header = vbox({
        text("Crew Optimizer") | bold | center,
        separator(),
        hbox({
            text("  Scenario: "),
            text(scenario_name) | bold | color(Color::Cyan),
            text("  [</>] change") | dim,
            filler(),
            text("  Ship: "),
            text(ship_name) | bold | color(Color::Yellow),
            text("  [T] cycle") | dim,
        }),
        hbox({
            text("  Roster: "),
            text(std::to_string(state.roster.size()) + " officers") | dim,
            filler(),
            text("  [Enter] Run Optimizer") | dim,
        }),
    });

    // Results
    Elements result_rows;
    if (state.crew_results.empty()) {
        result_rows.push_back(text("Press [Enter] to run the optimizer for this scenario.") | center | dim);
    } else {
        for (size_t i = 0; i < state.crew_results.size(); ++i) {
            const auto& r = state.crew_results[i];
            const auto& bd = r.breakdown;
            bool sel = ((int)i == state.selected_crew);

            std::string rank_str = "#" + std::to_string(i + 1);
            std::string score_str = std::to_string((int)r.score);

            auto row = vbox({
                hbox({
                    text(rank_str) | bold | color(i == 0 ? Color(Color::Gold1) : (i == 1 ? Color(Color::GrayLight) : Color(Color::GrayDark))),
                    text("  Score: "),
                    text(score_str) | bold,
                    filler(),
                    text("Captain: "),
                    text(bd.captain) | bold | color(Color::Yellow),
                }),
                hbox({
                    text("    Bridge: "),
                    text(bd.bridge.size() > 0 ? bd.bridge[0] : "?") | color(Color::Cyan),
                    text(" + "),
                    text(bd.bridge.size() > 1 ? bd.bridge[1] : "?") | color(Color::Cyan),
                }),
            });

            if (sel) {
                row = row | inverted | focus;
            }
            result_rows.push_back(row);
            if (i < state.crew_results.size() - 1) {
                result_rows.push_back(separatorLight());
            }
        }
    }

    // Detail panel for selected crew
    Element detail = text("");
    if (state.selected_crew >= 0 && state.selected_crew < (int)state.crew_results.size()) {
        const auto& r = state.crew_results[state.selected_crew];
        const auto& bd = r.breakdown;

        Elements lines;
        lines.push_back(text("Crew Breakdown") | bold);
        lines.push_back(separator());

        lines.push_back(text("Individual Scores:") | bold);
        for (const auto& [name, score] : bd.individual_scores) {
            bool is_captain = (name == bd.captain);
            lines.push_back(hbox({
                text("  "),
                text(is_captain ? "[CPT] " : "      "),
                text(name) | (is_captain ? bold : nothing),
                filler(),
                text(std::to_string((int)score)),
            }));
        }

        lines.push_back(separator());
        lines.push_back(text("Bonuses:") | bold);
        if (bd.synergy_bonus > 0)
            lines.push_back(hbox({text("  Synergy:       +"), text(std::to_string((int)bd.synergy_bonus)) | color(Color::Green)}));
        if (bd.state_chain_bonus > 0)
            lines.push_back(hbox({text("  State Chain:   +"), text(std::to_string((int)bd.state_chain_bonus)) | color(Color::Green)}));
        if (bd.crit_bonus > 0)
            lines.push_back(hbox({text("  Crit Coverage: +"), text(std::to_string((int)bd.crit_bonus)) | color(Color::Green)}));
        if (bd.ship_type_bonus > 0)
            lines.push_back(hbox({text("  Ship Spec:     +"), text(std::to_string((int)bd.ship_type_bonus)) | color(Color::Green)}));
        if (bd.scenario_bonus > 0)
            lines.push_back(hbox({text("  Scenario:      +"), text(std::to_string((int)bd.scenario_bonus)) | color(Color::Green)}));
        if (bd.weakness_counter_bonus > 0)
            lines.push_back(hbox({text("  Weakness Cnt:  +"), text(std::to_string((int)bd.weakness_counter_bonus)) | color(Color::Green)}));
        if (bd.dual_use_bonus > 0)
            lines.push_back(hbox({text("  Dual Use:      +"), text(std::to_string((int)bd.dual_use_bonus)) | color(Color::Green)}));
        if (bd.amplifier_bonus > 0)
            lines.push_back(hbox({text("  Amplifier:     +"), text(std::to_string((int)bd.amplifier_bonus)) | color(Color::Green)}));

        if (!bd.penalties.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Penalties:") | bold | color(Color::Red));
            for (const auto& p : bd.penalties) {
                // Wrap penalty text
                std::string s = p;
                while (!s.empty()) {
                    if (s.size() <= 48) { lines.push_back(text("  " + s) | color(Color::Red) | dim); break; }
                    size_t cut = s.rfind(' ', 48);
                    if (cut == std::string::npos || cut == 0) cut = 48;
                    lines.push_back(text("  " + s.substr(0, cut)) | color(Color::Red) | dim);
                    s = s.substr(cut + 1);
                }
            }
        }

        if (!bd.synergy_notes.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Synergy Notes:") | bold | color(Color::Blue));
            for (const auto& note : bd.synergy_notes) {
                std::string s = note;
                while (!s.empty()) {
                    if (s.size() <= 48) { lines.push_back(text("  " + s) | color(Color::Blue) | dim); break; }
                    size_t cut = s.rfind(' ', 48);
                    if (cut == std::string::npos || cut == 0) cut = 48;
                    lines.push_back(text("  " + s.substr(0, cut)) | color(Color::Blue) | dim);
                    s = s.substr(cut + 1);
                }
            }
        }

        // BDA suggestions for this crew
        if (!state.crew_bda_results.empty()) {
            lines.push_back(separator());
            lines.push_back(text("BDA Suggestions:") | bold | color(Color::Magenta));
            for (size_t bi = 0; bi < state.crew_bda_results.size(); ++bi) {
                const auto& bda = state.crew_bda_results[bi];
                lines.push_back(hbox({
                    text("  #" + std::to_string(bi + 1) + " "),
                    text(bda.name) | bold | color(Color::Cyan),
                    filler(),
                    text("OA:" + std::to_string((int)bda.oa_pct) + "%") | dim,
                    text("  Score:" + std::to_string((int)bda.score)) | dim,
                }));
                for (const auto& reason : bda.reasons) {
                    lines.push_back(text("     " + reason) | dim | color(Color::Magenta));
                }
            }
        }

        detail = vbox(lines);
    }

    return vbox({
        header,
        separator(),
        hbox({
            vbox(result_rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 55) | vscroll_indicator | yframe,
        }) | flex,
    });
}

// ---------------------------------------------------------------------------
// View: Loadout (7-dock system)
// ---------------------------------------------------------------------------

static Element render_loadout(AppState& state) {
    if (!state.crew_loaded) {
        return vbox({
            text("7-Dock Loadout Optimizer") | bold | center,
            separator(),
            text("No roster loaded.") | center,
            text("Place roster.csv in the project root and restart.") | center | dim,
        });
    }

    // Header
    static const char* ship_names[] = {"Explorer", "Battleship", "Interceptor"};
    int safe_ship = std::clamp(state.crew_ship_type, 0, 2);
    std::string ship_name = ship_names[safe_ship];

    auto header = vbox({
        text("7-Dock Loadout Optimizer") | bold | center,
        separator(),
        hbox({
            text("  Ship: "),
            text(ship_name) | bold | color(Color::Yellow),
            text("  [T] cycle") | dim,
            filler(),
            text("  [Enter] Optimize All Docks") | dim,
            text("  [L] Load Saved") | dim,
        }),
    });

    // Dock list (left panel)
    Elements dock_rows;
    for (int d = 0; d < 7; ++d) {
        bool sel = (d == state.selected_dock);
        auto& cfg = state.dock_configs[d];
        std::string label = "Dock " + std::to_string(d + 1) + ": ";
        std::string scen = scenario_label(cfg.scenario);

        Elements row_parts;
        row_parts.push_back(text(label) | bold);
        row_parts.push_back(text(scen) | color(Color::Cyan));

        if (cfg.locked) {
            row_parts.push_back(text(" [LOCKED]") | color(Color::Yellow) | dim);
        }

        // Show crew if computed
        if (state.loadout_computed && d < (int)state.loadout_result.docks.size()) {
            const auto& dr = state.loadout_result.docks[d];
            row_parts.push_back(filler());
            row_parts.push_back(text(dr.captain) | bold | color(Color::Yellow));
            if (dr.bridge.size() >= 2) {
                row_parts.push_back(text(" + ") | dim);
                row_parts.push_back(text(dr.bridge[0]) | color(Color::Cyan));
                row_parts.push_back(text(" + ") | dim);
                row_parts.push_back(text(dr.bridge[1]) | color(Color::Cyan));
            }
            row_parts.push_back(text("  "));
            row_parts.push_back(text(std::to_string((int)dr.score)) | dim);
        }

        auto row = hbox(row_parts);
        if (sel) row = row | inverted | focus;
        dock_rows.push_back(row);

        // Show BDA for selected dock
        if (sel && state.loadout_computed && d < (int)state.loadout_result.docks.size()) {
            const auto& dr = state.loadout_result.docks[d];
            if (!dr.bda_suggestions.empty()) {
                dock_rows.push_back(text("  BDA Suggestions:") | dim | color(Color::Magenta));
                for (size_t bi = 0; bi < dr.bda_suggestions.size(); ++bi) {
                    const auto& bda = dr.bda_suggestions[bi];
                    bool bda_sel = (sel && (int)bi == state.selected_dock_bda);
                    auto bda_row = hbox({
                        text("    #" + std::to_string(bi + 1) + " "),
                        text(bda.name) | bold | color(Color::Cyan),
                        filler(),
                        text("OA:" + std::to_string((int)bda.oa_pct) + "%") | dim,
                        text("  Score:" + std::to_string((int)bda.score)) | dim,
                    });
                    if (bda_sel) bda_row = bda_row | bgcolor(Color::GrayDark);
                    dock_rows.push_back(bda_row);
                }
            }
        }

        if (d < 6) dock_rows.push_back(separatorLight());
    }

    // Detail panel (right) — selected dock breakdown
    Element detail = text("");
    if (state.loadout_computed && state.selected_dock >= 0 &&
        state.selected_dock < (int)state.loadout_result.docks.size()) {
        const auto& dr = state.loadout_result.docks[state.selected_dock];
        const auto& bd = dr.breakdown;
        Elements lines;

        lines.push_back(hbox({
            text("Dock " + std::to_string(dr.dock_num)) | bold,
            text(" - "),
            text(dr.scenario_label_str) | color(Color::Cyan),
        }));
        lines.push_back(separator());

        lines.push_back(hbox({text("Ship: "), text(dr.ship_used) | bold | color(Color::Yellow)}));
        if (!dr.ship_recommended.empty() && dr.ship_recommended != dr.ship_used) {
            lines.push_back(hbox({text("Recommended: "), text(dr.ship_recommended) | dim}));
        }
        lines.push_back(separator());

        lines.push_back(hbox({
            text("Captain: "),
            text(dr.captain) | bold | color(Color::Yellow),
        }));
        if (dr.bridge.size() >= 2) {
            lines.push_back(hbox({
                text("Bridge:  "),
                text(dr.bridge[0]) | color(Color::Cyan),
                text(" + "),
                text(dr.bridge[1]) | color(Color::Cyan),
            }));
        }
        lines.push_back(hbox({text("Score:   "), text(std::to_string((int)dr.score)) | bold}));

        if (dr.locked) {
            lines.push_back(text("[LOCKED - pre-assigned]") | color(Color::Yellow) | dim);
        }

        // Breakdown
        lines.push_back(separator());
        lines.push_back(text("Individual:") | bold);
        for (const auto& [name, score] : bd.individual_scores) {
            bool is_cpt = (name == bd.captain);
            lines.push_back(hbox({
                text("  "),
                text(is_cpt ? "[CPT] " : "      "),
                text(name) | (is_cpt ? bold : nothing),
                filler(),
                text(std::to_string((int)score)),
            }));
        }

        lines.push_back(separator());
        lines.push_back(text("Bonuses:") | bold);
        if (bd.synergy_bonus > 0)
            lines.push_back(hbox({text("  Synergy:     +"), text(std::to_string((int)bd.synergy_bonus)) | color(Color::Green)}));
        if (bd.state_chain_bonus > 0)
            lines.push_back(hbox({text("  State Chain: +"), text(std::to_string((int)bd.state_chain_bonus)) | color(Color::Green)}));
        if (bd.crit_bonus > 0)
            lines.push_back(hbox({text("  Crit:        +"), text(std::to_string((int)bd.crit_bonus)) | color(Color::Green)}));
        if (bd.ship_type_bonus > 0)
            lines.push_back(hbox({text("  Ship Spec:   +"), text(std::to_string((int)bd.ship_type_bonus)) | color(Color::Green)}));
        if (bd.scenario_bonus > 0)
            lines.push_back(hbox({text("  Scenario:    +"), text(std::to_string((int)bd.scenario_bonus)) | color(Color::Green)}));

        if (!bd.penalties.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Penalties:") | bold | color(Color::Red));
            for (const auto& p : bd.penalties) {
                lines.push_back(text("  " + p) | color(Color::Red) | dim);
            }
        }

        // BDA detail for selected suggestion
        if (!dr.bda_suggestions.empty()) {
            lines.push_back(separator());
            lines.push_back(text("BDA Details:") | bold | color(Color::Magenta));
            int bi = state.selected_dock_bda;
            if (bi >= 0 && bi < (int)dr.bda_suggestions.size()) {
                const auto& bda = dr.bda_suggestions[bi];
                lines.push_back(hbox({
                    text("  "),
                    text(bda.name) | bold | color(Color::Cyan),
                    text("  Lv" + std::to_string(bda.level) + " Rk" + std::to_string(bda.rank)),
                }));
                lines.push_back(hbox({
                    text("  Atk:"),
                    text(std::to_string((int)bda.attack)) | dim,
                    text("  Def:"),
                    text(std::to_string((int)bda.defense)) | dim,
                    text("  HP:"),
                    text(std::to_string((int)bda.health)) | dim,
                }));
                for (const auto& reason : bda.reasons) {
                    lines.push_back(text("  " + reason) | dim | color(Color::Magenta));
                }
            }
        }

        // Excluded officers
        if (!state.loadout_result.excluded_officers.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Excluded:") | bold | dim);
            std::string excl;
            for (size_t ei = 0; ei < state.loadout_result.excluded_officers.size() && ei < 10; ++ei) {
                if (!excl.empty()) excl += ", ";
                excl += state.loadout_result.excluded_officers[ei];
            }
            if (state.loadout_result.excluded_officers.size() > 10) {
                excl += " +" + std::to_string(state.loadout_result.excluded_officers.size() - 10) + " more";
            }
            // Wrap to ~50 chars
            while (!excl.empty()) {
                if (excl.size() <= 50) { lines.push_back(text("  " + excl) | dim); break; }
                size_t cut = excl.rfind(',', 50);
                if (cut == std::string::npos || cut == 0) cut = 50;
                lines.push_back(text("  " + excl.substr(0, cut + 1)) | dim);
                excl = excl.substr(cut + 1);
                if (!excl.empty() && excl[0] == ' ') excl = excl.substr(1);
            }
        }

        detail = vbox(lines);
    } else if (!state.loadout_computed) {
        detail = vbox({
            text("Press [Enter] to optimize all 7 docks.") | center | dim,
            separatorEmpty(),
            text("Use [</>] to change dock scenario.") | center | dim,
            text("Each dock gets best available crew") | center | dim,
            text("from the remaining officer pool.") | center | dim,
        });
    }

    // Dock config editor line
    std::string dock_scenario_str;
    if (state.selected_dock >= 0 && state.selected_dock < (int)state.dock_configs.size()) {
        dock_scenario_str = scenario_label(state.dock_configs[state.selected_dock].scenario);
    }

    return vbox({
        header,
        separator(),
        hbox({
            vbox(dock_rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 55) | vscroll_indicator | yframe,
        }) | flex,
        separator(),
        hbox({
            text(" Dock " + std::to_string(state.selected_dock + 1) + ": ") | bold,
            text(dock_scenario_str) | color(Color::Cyan),
            filler(),
            text(" [Up/Dn] Dock  ") | dim,
            text("[</>] Scenario  ") | dim,
            text("[B] BDA nav  ") | dim,
            text("[K] Lock/Unlock") | dim,
        }),
    });
}

// ---------------------------------------------------------------------------
// View: Officers — with player data overlay (owned/unowned, levels, ranks)
// ---------------------------------------------------------------------------

static Element render_officers(AppState& state) {
    auto& gd = state.game_data;
    auto& pd = state.player_data;

    // Build player officer lookup by officer_id
    std::map<int64_t, const PlayerOfficer*> player_map;
    for (auto& po : pd.officers) {
        player_map[po.officer_id] = &po;
    }
    bool has_player = !player_map.empty();

    std::vector<const Officer*> officers;
    for (auto& [id, o] : gd.officers) {
        if (!state.officer_filter.empty()) {
            std::string name_lower = o.name;
            std::string filter_lower = state.officer_filter;
            for (auto& c : name_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : filter_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name_lower.find(filter_lower) == std::string::npos &&
                o.short_name.find(state.officer_filter) == std::string::npos) {
                continue;
            }
        }
        officers.push_back(&o);
    }

    // Sort: owned first (by level desc), then unowned alphabetically
    std::sort(officers.begin(), officers.end(), [&](const Officer* a, const Officer* b) {
        auto pa = player_map.find(a->id);
        auto pb = player_map.find(b->id);
        bool a_owned = (pa != player_map.end());
        bool b_owned = (pb != player_map.end());
        if (a_owned != b_owned) return a_owned > b_owned; // owned first
        if (a_owned && b_owned) {
            if (pa->second->level != pb->second->level) return pa->second->level > pb->second->level;
            if (pa->second->rank != pb->second->rank) return pa->second->rank > pb->second->rank;
        }
        return a->name < b->name;
    });

    if (officers.empty()) {
        return vbox({
            text("Officers") | bold | center,
            separator(),
            text("No officers loaded. Press [R] to refresh data.") | center,
        });
    }

    // Clamp selection
    state.selected_officer = std::clamp(state.selected_officer, 0, (int)officers.size() - 1);

    // Count owned
    int owned_count = 0;
    for (auto* o : officers) {
        if (player_map.count(o->id)) owned_count++;
    }

    // Build rows
    Elements rows;

    // Header row
    if (has_player) {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 24),
            text("Lv") | bold | size(WIDTH, EQUAL, 5),
            text("Rk") | bold | size(WIDTH, EQUAL, 5),
            text("Shards") | bold | size(WIDTH, EQUAL, 8),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("Class") | bold | size(WIDTH, EQUAL, 12),
            text("Group") | bold | size(WIDTH, EQUAL, 20),
            text("MaxRk") | bold | size(WIDTH, EQUAL, 7),
        }));
    } else {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 28),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("Class") | bold | size(WIDTH, EQUAL, 12),
            text("Group") | bold | size(WIDTH, EQUAL, 22),
            text("MaxRk") | bold | size(WIDTH, EQUAL, 7),
            text("Atk") | bold | size(WIDTH, EQUAL, 10),
            text("Def") | bold | size(WIDTH, EQUAL, 10),
            text("HP") | bold | size(WIDTH, EQUAL, 10),
        }));
    }
    rows.push_back(separator());

    for (int i = 0; i < (int)officers.size(); ++i) {
        auto* o = officers[i];
        bool sel = (i == state.selected_officer);
        auto pit = player_map.find(o->id);
        bool owned = (pit != player_map.end());

        Color rcolor = Color::White;
        switch (o->rarity) {
            case 1: rcolor = Color::GrayLight; break;
            case 2: rcolor = Color::Green; break;
            case 3: rcolor = Color::Blue; break;
            case 4: rcolor = Color::Magenta; break;
        }

        std::string name = o->name.empty() ? o->short_name : o->name;
        Element row;

        if (has_player) {
            std::string lv_str = owned ? std::to_string(pit->second->level) : "-";
            std::string rk_str = owned ? std::to_string(pit->second->rank) : "-";
            std::string shard_str = owned ? std::to_string(pit->second->shard_count) : "-";

            row = hbox({
                (owned ? text(name) : text(name) | dim) | size(WIDTH, EQUAL, 24),
                text(lv_str) | (owned ? bold : dim) | size(WIDTH, EQUAL, 5),
                text(rk_str) | (owned ? bold : dim) | size(WIDTH, EQUAL, 5),
                text(shard_str) | dim | size(WIDTH, EQUAL, 8),
                text(rarity_str(o->rarity)) | color(rcolor) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 10),
                text(officer_class_str(o->officer_class)) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 12),
                text(o->group_name) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 20),
                text(std::to_string(o->max_rank)) | dim | size(WIDTH, EQUAL, 7),
            });
        } else {
            std::string max_atk = "-", max_def = "-", max_hp = "-";
            if (!o->stats.empty()) {
                auto& last = o->stats.back();
                max_atk = format_number((int64_t)last.attack);
                max_def = format_number((int64_t)last.defense);
                max_hp = format_number((int64_t)last.health);
            }
            row = hbox({
                text(name) | size(WIDTH, EQUAL, 28),
                text(rarity_str(o->rarity)) | color(rcolor) | size(WIDTH, EQUAL, 10),
                text(officer_class_str(o->officer_class)) | size(WIDTH, EQUAL, 12),
                text(o->group_name) | size(WIDTH, EQUAL, 22),
                text(std::to_string(o->max_rank)) | size(WIDTH, EQUAL, 7),
                text(max_atk) | size(WIDTH, EQUAL, 10),
                text(max_def) | size(WIDTH, EQUAL, 10),
                text(max_hp) | size(WIDTH, EQUAL, 10),
            });
        }

        if (sel) row = row | inverted | focus;
        rows.push_back(row);
    }

    // Detail panel for selected officer
    Element detail = text("");
    if (state.selected_officer >= 0 && state.selected_officer < (int)officers.size()) {
        auto* o = officers[state.selected_officer];
        auto pit = player_map.find(o->id);
        bool owned = (pit != player_map.end());

        Elements lines;
        lines.push_back(text(o->name.empty() ? o->short_name : o->name) | bold);
        if (owned) {
            lines.push_back(text("OWNED") | bold | color(Color::Green));
        } else {
            lines.push_back(text("NOT OWNED") | dim);
        }
        lines.push_back(separator());
        lines.push_back(hbox({text("Rarity: "), text(rarity_str(o->rarity))}));
        lines.push_back(hbox({text("Class:  "), text(officer_class_str(o->officer_class))}));
        lines.push_back(hbox({text("Group:  "), text(o->group_name)}));
        lines.push_back(hbox({text("Ranks:  "), text(std::to_string(o->max_rank))}));

        if (owned) {
            lines.push_back(separator());
            lines.push_back(text("Player Stats:") | bold | color(Color::Cyan));
            lines.push_back(hbox({text("  Level:  "), text(std::to_string(pit->second->level)) | bold}));
            lines.push_back(hbox({text("  Rank:   "), text(std::to_string(pit->second->rank)) | bold}));
            lines.push_back(hbox({text("  Shards: "), text(std::to_string(pit->second->shard_count))}));
        }

        if (!o->stats.empty()) {
            auto& last = o->stats.back();
            lines.push_back(separator());
            lines.push_back(text("Max Stats (Game Data):") | bold);
            lines.push_back(hbox({text("  Atk: "), text(format_number((int64_t)last.attack)) | color(Color::Red)}));
            lines.push_back(hbox({text("  Def: "), text(format_number((int64_t)last.defense)) | color(Color::Cyan)}));
            lines.push_back(hbox({text("  HP:  "), text(format_number((int64_t)last.health)) | color(Color::Green)}));

            // If owned, also show current level stats
            if (owned && pit->second->level > 0 && pit->second->level <= (int)o->stats.size()) {
                auto& cur = o->stats[pit->second->level - 1];
                lines.push_back(separator());
                lines.push_back(text("Current Stats (Lv" + std::to_string(pit->second->level) + "):") | bold | color(Color::Cyan));
                lines.push_back(hbox({text("  Atk: "), text(format_number((int64_t)cur.attack)) | color(Color::Red)}));
                lines.push_back(hbox({text("  Def: "), text(format_number((int64_t)cur.defense)) | color(Color::Cyan)}));
                lines.push_back(hbox({text("  HP:  "), text(format_number((int64_t)cur.health)) | color(Color::Green)}));
            }
        }

        if (!o->description.empty()) {
            lines.push_back(separator());
            std::string desc = o->description;
            while (!desc.empty()) {
                if (desc.size() <= 38) { lines.push_back(text(desc) | dim); break; }
                size_t cut = desc.rfind(' ', 38);
                if (cut == std::string::npos || cut == 0) cut = 38;
                lines.push_back(text(desc.substr(0, cut)) | dim);
                desc = desc.substr(cut + (desc[cut] == ' ' ? 1 : 0));
            }
        }
        detail = vbox(lines) | border;
    }

    std::string title = has_player
        ? "Officers (" + std::to_string(owned_count) + " owned / " + std::to_string(officers.size()) + " total)"
        : "Officers (" + std::to_string(officers.size()) + " total)";

    return vbox({
        hbox({
            text(title) | bold,
            filler(),
            text("[Up/Down] Navigate  [F] Filter") | dim,
        }),
        separator(),
        hbox({
            vbox(rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 42) | vscroll_indicator | yframe,
        }) | flex,
    });
}

// ---------------------------------------------------------------------------
// View: Ships — with player data overlay (owned tier/level)
// ---------------------------------------------------------------------------

static Element render_ships(AppState& state) {
    auto& gd = state.game_data;
    auto& pd = state.player_data;

    // Build player ship lookup by hull_id (may have multiple instances of same hull)
    // Use a multimap since player can have multiple ships of same hull
    std::map<int64_t, std::vector<const PlayerShip*>> player_hull_map;
    for (auto& ps : pd.ships) {
        player_hull_map[ps.hull_id].push_back(&ps);
    }
    bool has_player = !pd.ships.empty();

    std::vector<const Ship*> ships;
    for (auto& [id, s] : gd.ships) {
        if (!state.ship_filter.empty()) {
            std::string name_lower = s.name;
            std::string filter_lower = state.ship_filter;
            for (auto& c : name_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : filter_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name_lower.find(filter_lower) == std::string::npos) continue;
        }
        ships.push_back(&s);
    }

    // Sort: owned first (by tier desc), then by grade/name
    std::sort(ships.begin(), ships.end(), [&](const Ship* a, const Ship* b) {
        bool a_owned = player_hull_map.count(a->id) > 0;
        bool b_owned = player_hull_map.count(b->id) > 0;
        if (a_owned != b_owned) return a_owned > b_owned;
        if (a_owned && b_owned) {
            auto& a_ps = player_hull_map[a->id];
            auto& b_ps = player_hull_map[b->id];
            int a_tier = a_ps.empty() ? 0 : a_ps[0]->tier;
            int b_tier = b_ps.empty() ? 0 : b_ps[0]->tier;
            if (a_tier != b_tier) return a_tier > b_tier;
        }
        if (a->grade != b->grade) return a->grade < b->grade;
        return a->name < b->name;
    });

    if (ships.empty()) {
        return vbox({
            text("Ships") | bold | center,
            separator(),
            text("No ships loaded. Press [R] to refresh data.") | center,
        });
    }

    // Clamp selection
    state.selected_ship = std::clamp(state.selected_ship, 0, (int)ships.size() - 1);

    // Count owned
    int owned_count = 0;
    for (auto* s : ships) {
        if (player_hull_map.count(s->id)) owned_count++;
    }

    // Build rows
    Elements rows;

    // Header row
    if (has_player) {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 24),
            text("Tier") | bold | size(WIDTH, EQUAL, 6),
            text("Lv") | bold | size(WIDTH, EQUAL, 5),
            text("Type") | bold | size(WIDTH, EQUAL, 14),
            text("Grade") | bold | size(WIDTH, EQUAL, 7),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("MaxT") | bold | size(WIDTH, EQUAL, 6),
            text("MaxLv") | bold | size(WIDTH, EQUAL, 7),
        }));
    } else {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 28),
            text("Type") | bold | size(WIDTH, EQUAL, 14),
            text("Grade") | bold | size(WIDTH, EQUAL, 8),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("MaxTier") | bold | size(WIDTH, EQUAL, 9),
            text("MaxLv") | bold | size(WIDTH, EQUAL, 8),
            text("Build") | bold | size(WIDTH, EQUAL, 12),
        }));
    }
    rows.push_back(separator());

    for (int i = 0; i < (int)ships.size(); ++i) {
        auto* s = ships[i];
        bool sel = (i == state.selected_ship);
        auto pit = player_hull_map.find(s->id);
        bool owned = (pit != player_hull_map.end() && !pit->second.empty());

        Color tcolor = Color::White;
        switch (s->hull_type) {
            case 0: tcolor = Color::Cyan; break;
            case 1: tcolor = Color::Yellow; break;
            case 2: tcolor = Color::Green; break;
            case 3: tcolor = Color::Red; break;
        }

        Element row;
        if (has_player) {
            std::string tier_str = owned ? "T" + std::to_string(pit->second[0]->tier) : "-";
            std::string lv_str = owned ? std::to_string(pit->second[0]->level) : "-";

            row = hbox({
                (owned ? text(s->name) : text(s->name) | dim) | size(WIDTH, EQUAL, 24),
                text(tier_str) | (owned ? bold : dim) | color(owned ? Color::Cyan : Color::GrayDark) | size(WIDTH, EQUAL, 6),
                text(lv_str) | (owned ? bold : dim) | size(WIDTH, EQUAL, 5),
                text(hull_type_str(s->hull_type)) | color(tcolor) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 14),
                text("G" + std::to_string(s->grade)) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 7),
                text(rarity_str(s->rarity)) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 10),
                text("T" + std::to_string(s->max_tier)) | dim | size(WIDTH, EQUAL, 6),
                text(std::to_string(s->max_level)) | dim | size(WIDTH, EQUAL, 7),
            });
        } else {
            int hours = s->build_time_seconds / 3600;
            int mins = (s->build_time_seconds % 3600) / 60;
            std::string build_time = std::to_string(hours) + "h " + std::to_string(mins) + "m";
            if (s->build_time_seconds == 0) build_time = "-";

            row = hbox({
                text(s->name) | size(WIDTH, EQUAL, 28),
                text(hull_type_str(s->hull_type)) | color(tcolor) | size(WIDTH, EQUAL, 14),
                text("G" + std::to_string(s->grade)) | size(WIDTH, EQUAL, 8),
                text(rarity_str(s->rarity)) | size(WIDTH, EQUAL, 10),
                text("T" + std::to_string(s->max_tier)) | size(WIDTH, EQUAL, 9),
                text(std::to_string(s->max_level)) | size(WIDTH, EQUAL, 8),
                text(build_time) | size(WIDTH, EQUAL, 12),
            });
        }

        if (sel) row = row | inverted | focus;
        rows.push_back(row);
    }

    // Detail panel for selected ship
    Element detail = text("");
    if (state.selected_ship >= 0 && state.selected_ship < (int)ships.size()) {
        auto* s = ships[state.selected_ship];
        auto pit = player_hull_map.find(s->id);
        bool owned = (pit != player_hull_map.end() && !pit->second.empty());

        Elements lines;
        lines.push_back(text(s->name) | bold);
        if (owned) {
            lines.push_back(text("OWNED") | bold | color(Color::Green));
        } else {
            lines.push_back(text("NOT BUILT") | dim);
        }
        lines.push_back(separator());
        lines.push_back(hbox({text("Type:   "), text(hull_type_str(s->hull_type))}));
        lines.push_back(hbox({text("Grade:  G"), text(std::to_string(s->grade))}));
        lines.push_back(hbox({text("Rarity: "), text(rarity_str(s->rarity))}));
        lines.push_back(hbox({text("MaxTier: T"), text(std::to_string(s->max_tier))}));
        lines.push_back(hbox({text("MaxLv:  "), text(std::to_string(s->max_level))}));

        if (owned) {
            lines.push_back(separator());
            lines.push_back(text("Player Ship:") | bold | color(Color::Cyan));
            for (auto* ps : pit->second) {
                lines.push_back(hbox({text("  Tier:  T"), text(std::to_string(ps->tier)) | bold}));
                lines.push_back(hbox({text("  Level: "), text(std::to_string(ps->level)) | bold}));
                int pct = (int)(ps->level_percentage * 100);
                lines.push_back(hbox({text("  Lvl%:  "), text(std::to_string(pct) + "%") | dim}));
                // Progress to max tier
                if (ps->tier < s->max_tier) {
                    lines.push_back(hbox({text("  Progress: T"), text(std::to_string(ps->tier)),
                        text("/T"), text(std::to_string(s->max_tier)) | color(Color::Yellow)}));
                    double tier_pct = (double)ps->tier / s->max_tier;
                    lines.push_back(hbox({text("  "), gauge(tier_pct) | size(WIDTH, EQUAL, 24) | color(Color::Cyan)}));
                } else {
                    lines.push_back(text("  MAX TIER") | bold | color(Color::Green));
                }
            }
        }

        if (s->build_time_seconds > 0) {
            lines.push_back(separator());
            int h = s->build_time_seconds / 3600;
            int m = (s->build_time_seconds % 3600) / 60;
            lines.push_back(hbox({text("Build:  "), text(std::to_string(h) + "h " + std::to_string(m) + "m")}));
        }
        if (!s->crew_slots.empty()) {
            lines.push_back(hbox({text("Crew:   "), text(std::to_string(s->crew_slots.size()) + " slots")}));
        }
        detail = vbox(lines) | border;
    }

    std::string title = has_player
        ? "Ships (" + std::to_string(owned_count) + " owned / " + std::to_string(ships.size()) + " total)"
        : "Ships (" + std::to_string(ships.size()) + " total)";

    return vbox({
        hbox({
            text(title) | bold,
            filler(),
            text("[Up/Down] Navigate  [F] Filter") | dim,
        }),
        separator(),
        hbox({
            vbox(rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 40) | vscroll_indicator | yframe,
        }) | flex,
    });
}

// ---------------------------------------------------------------------------
// View: Sync — Detailed player data browser + ingress status
// ---------------------------------------------------------------------------

static const char* sync_view_labels[] = {
    "Officers", "Ships", "Resources", "Buildings", "Research", "Jobs", "Buffs"
};
static const int sync_view_count = 7;

static Element render_sync(AppState& state) {
    bool running = state.ingress_server.is_running();
    auto& pd = state.player_data;

    // --- Server status (compact) ---
    auto status_line = hbox({
        text(" Server: "),
        running ? text("RUNNING :" + std::to_string(state.ingress_server.port())) | color(Color::Green) | bold
                : text("STOPPED") | color(Color::Red) | bold,
        text("  "),
        text("[S] Toggle") | dim,
        filler(),
        text("Last sync: "),
        pd.last_sync != std::chrono::system_clock::time_point{}
            ? [&]() {
                auto tt = std::chrono::system_clock::to_time_t(pd.last_sync);
                std::tm tb{};
                localtime_r(&tt, &tb);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tb.tm_hour, tb.tm_min, tb.tm_sec);
                return text(std::string(buf)) | color(Color::Green);
            }()
            : text("Never") | dim,
    });

    // --- View mode tabs ---
    Elements view_tabs;
    for (int i = 0; i < sync_view_count; i++) {
        int count = 0;
        switch (i) {
            case 0: count = (int)pd.officers.size(); break;
            case 1: count = (int)pd.ships.size(); break;
            case 2: count = (int)pd.resources.size(); break;
            case 3: count = (int)pd.buildings.size(); break;
            case 4: count = (int)pd.researches.size(); break;
            case 5: count = (int)pd.jobs.size(); break;
            case 6: count = (int)pd.buffs.size(); break;
        }
        std::string label = std::string(sync_view_labels[i]) + "(" + std::to_string(count) + ")";
        auto tab = text(" " + label + " ");
        if (i == state.sync_view_mode) {
            tab = tab | bold | inverted;
        } else if (count > 0) {
            tab = tab | color(Color::Cyan);
        } else {
            tab = tab | dim;
        }
        view_tabs.push_back(tab);
    }

    // --- Data table for selected view ---
    Elements data_rows;

    auto add_header = [&](std::vector<std::pair<std::string, int>> cols) {
        Elements hdr_parts;
        for (auto& [name, width] : cols) {
            hdr_parts.push_back(text(name) | bold | size(WIDTH, EQUAL, width));
        }
        data_rows.push_back(hbox(std::move(hdr_parts)));
        data_rows.push_back(separator());
    };

    int max_rows = 0;

    switch (state.sync_view_mode) {
    case 0: { // Officers
        add_header({{"Name", 26}, {"Level", 7}, {"Rank", 6}, {"Shards", 8}, {"ID", 14}});
        auto sorted = pd.officers;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerOfficer& a, const PlayerOfficer& b) {
            return a.level > b.level || (a.level == b.level && a.rank > b.rank);
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& o = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = o.name.empty() ? ("OID:" + std::to_string(o.officer_id)) : o.name;
            if (name.size() > 24) name = name.substr(0, 22) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 26),
                text(std::to_string(o.level)) | bold | size(WIDTH, EQUAL, 7),
                text(std::to_string(o.rank)) | size(WIDTH, EQUAL, 6),
                text(std::to_string(o.shard_count)) | size(WIDTH, EQUAL, 8),
                text(std::to_string(o.officer_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 1: { // Ships
        add_header({{"Name", 26}, {"Tier", 6}, {"Level", 7}, {"Lvl%", 7}, {"Hull ID", 12}});
        auto sorted = pd.ships;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerShip& a, const PlayerShip& b) {
            return a.tier > b.tier || (a.tier == b.tier && a.level > b.level);
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& s = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = s.name.empty() ? ("Hull:" + std::to_string(s.hull_id)) : s.name;
            if (name.size() > 24) name = name.substr(0, 22) + "..";

            // Get hull type for color
            Color tcolor = Color::White;
            auto git = state.game_data.ships.find(s.hull_id);
            if (git != state.game_data.ships.end()) {
                switch (git->second.hull_type) {
                    case 0: tcolor = Color::Cyan; break;
                    case 1: tcolor = Color::Yellow; break;
                    case 2: tcolor = Color::Green; break;
                    case 3: tcolor = Color::Red; break;
                }
            }

            std::string pct_str = std::to_string((int)(s.level_percentage * 100)) + "%";
            auto row = hbox({
                text(name) | color(tcolor) | size(WIDTH, EQUAL, 26),
                text("T" + std::to_string(s.tier)) | bold | size(WIDTH, EQUAL, 6),
                text(std::to_string(s.level)) | size(WIDTH, EQUAL, 7),
                text(pct_str) | dim | size(WIDTH, EQUAL, 7),
                text(std::to_string(s.hull_id)) | dim | size(WIDTH, EQUAL, 12),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 2: { // Resources
        add_header({{"Name", 28}, {"Amount", 16}, {"ID", 14}});
        auto sorted = pd.resources;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerResource& a, const PlayerResource& b) {
            return a.amount > b.amount;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& r = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = r.name.empty() ? ("RID:" + std::to_string(r.resource_id)) : r.name;
            if (name.size() > 26) name = name.substr(0, 24) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 28),
                text(format_number(r.amount)) | bold | color(Color::Green) | size(WIDTH, EQUAL, 16),
                text(std::to_string(r.resource_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 3: { // Buildings
        add_header({{"Name", 30}, {"Level", 8}, {"ID", 14}});
        auto sorted = pd.buildings;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerBuilding& a, const PlayerBuilding& b) {
            return a.level > b.level;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& b = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = b.name.empty() ? ("BID:" + std::to_string(b.building_id)) : b.name;
            if (name.size() > 28) name = name.substr(0, 26) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 30),
                text(std::to_string(b.level)) | bold | size(WIDTH, EQUAL, 8),
                text(std::to_string(b.building_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 4: { // Research
        add_header({{"Name", 32}, {"Level", 8}, {"ID", 14}});
        auto sorted = pd.researches;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerResearch& a, const PlayerResearch& b) {
            return a.level > b.level;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& r = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = r.name.empty() ? ("RID:" + std::to_string(r.research_id)) : r.name;
            if (name.size() > 30) name = name.substr(0, 28) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 32),
                text(std::to_string(r.level)) | bold | size(WIDTH, EQUAL, 8),
                text(std::to_string(r.research_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 5: { // Jobs
        add_header({{"Type", 18}, {"Status", 10}, {"Time Left", 14}, {"Level", 7}});
        max_rows = (int)pd.jobs.size();
        for (int i = 0; i < (int)pd.jobs.size(); ++i) {
            auto& j = pd.jobs[i];
            bool sel = (i == state.sync_selected_row);
            std::string type = job_type_str(j.job_type);
            std::string status_str;
            Color status_color;
            if (j.completed) {
                status_str = "Done";
                status_color = Color::Green;
            } else {
                int remaining = job_remaining_seconds(j);
                if (remaining <= 0) {
                    status_str = "Ready";
                    status_color = Color::Green;
                } else {
                    status_str = "Active";
                    status_color = Color::Yellow;
                }
            }
            int remaining = j.completed ? 0 : job_remaining_seconds(j);
            std::string time_str = j.completed ? "-" : format_duration_short(remaining);

            auto row = hbox({
                text(type) | size(WIDTH, EQUAL, 18),
                text(status_str) | bold | color(status_color) | size(WIDTH, EQUAL, 10),
                text(time_str) | size(WIDTH, EQUAL, 14),
                text(std::to_string(j.level)) | dim | size(WIDTH, EQUAL, 7),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 6: { // Buffs
        add_header({{"Buff ID", 14}, {"Level", 8}, {"Status", 12}, {"Expires", 20}});
        max_rows = (int)pd.buffs.size();
        for (int i = 0; i < (int)pd.buffs.size(); ++i) {
            auto& b = pd.buffs[i];
            bool sel = (i == state.sync_selected_row);

            std::string status_str = b.expired ? "Expired" : "Active";
            Color status_color = b.expired ? Color::Red : Color::Green;

            std::string expire_str = "-";
            if (b.expiry_time > 0 && !b.expired) {
                auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                int remaining = (int)(b.expiry_time - now_epoch);
                expire_str = remaining > 0 ? format_duration_short(remaining) : "Expired";
                if (remaining <= 0) {
                    status_str = "Expired";
                    status_color = Color::Red;
                }
            }

            auto row = hbox({
                text(std::to_string(b.buff_id)) | size(WIDTH, EQUAL, 14),
                text(std::to_string(b.level)) | size(WIDTH, EQUAL, 8),
                text(status_str) | bold | color(status_color) | size(WIDTH, EQUAL, 12),
                text(expire_str) | size(WIDTH, EQUAL, 20),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    }

    if (data_rows.size() <= 2) { // Only header + separator
        data_rows.push_back(text("  No data. Start STFC with the community mod to sync.") | dim);
    }

    // Clamp selection
    if (max_rows > 0) {
        state.sync_selected_row = std::clamp(state.sync_selected_row, 0, max_rows - 1);
    }

    // --- Sync event log (compact, right panel) ---
    auto sync_log = state.ingress_server.get_sync_log();
    Elements log_lines;
    log_lines.push_back(text("Event Log") | bold);
    log_lines.push_back(separator());
    if (sync_log.empty()) {
        log_lines.push_back(text("No events yet") | dim);
    } else {
        int start = std::max(0, (int)sync_log.size() - 15);
        for (int i = (int)sync_log.size() - 1; i >= start; i--) {
            auto& ev = sync_log[i];
            auto time_t = std::chrono::system_clock::to_time_t(ev.timestamp);
            std::tm tm_buf{};
            localtime_r(&time_t, &tm_buf);
            char time_str[16];
            std::snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
                          tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

            Elements row;
            row.push_back(text(std::string(time_str) + " ") | dim);
            if (ev.success) {
                row.push_back(text("OK ") | color(Color::Green));
                row.push_back(text(ev.data_type + "(" + std::to_string(ev.record_count) + ")") | dim);
            } else {
                row.push_back(text("FAIL ") | color(Color::Red));
                row.push_back(text(ev.data_type) | dim);
            }
            log_lines.push_back(hbox(std::move(row)));
        }
    }

    return vbox({
        text("Mod Sync - Player Data Browser") | bold | center,
        separator(),
        status_line,
        separator(),
        hbox(view_tabs) | center,
        separator(),
        hbox({
            vbox(data_rows) | vscroll_indicator | yframe | flex,
            separator(),
            vbox(log_lines) | size(WIDTH, EQUAL, 38) | vscroll_indicator | yframe,
        }) | flex,
        separator(),
        hbox({
            text(" [S] Server  ") | dim,
            text("[Left/Right] View  ") | dim,
            text("[Up/Down] Navigate  ") | dim,
        }),
    });
}

// ---------------------------------------------------------------------------
// Help overlay
// ---------------------------------------------------------------------------

static Element render_help() {
    return vbox({
        text("STFC Tool v0.5 - Keyboard Reference") | bold | center,
        separator(),
        hbox({
            vbox({
                text("Global:") | bold | color(Color::Cyan),
                text("  [H]              Toggle help"),
                text("  [R]              Refresh game data"),
                text("  [S]              Toggle ingress server"),
                text("  [Q]              Quit (saves state)"),
                text("  [Tab/Shift+Tab]  Switch tabs"),
                text("  [PgUp/PgDn]      Scroll 10 rows"),
                text("  [Home/End]       Jump to top/bottom"),
                text("  Mouse wheel      Scroll up/down"),
                separatorEmpty(),
                text("Daily Planner:") | bold | color(Color::Cyan),
                text("  [Up/Down]  Navigate tasks"),
                text("  [Space]    Toggle task complete"),
                text("  [S]        Skip task"),
                text("  [C]        Show/hide completed"),
                separatorEmpty(),
                text("Weekly Planner:") | bold | color(Color::Cyan),
                text("  [Left/Right] Switch day"),
                text("  [Up/Down]  Navigate goals"),
                text("  [+/-]      Adjust goal progress"),
                text("  [Space]    Complete next task"),
            }) | flex,
            separator(),
            vbox({
                text("Crew Optimizer:") | bold | color(Color::Cyan),
                text("  [</> or ,/.]  Cycle scenario"),
                text("  [T]           Cycle ship type"),
                text("  [Enter]       Run optimizer"),
                text("  [Up/Down]     Navigate results"),
                separatorEmpty(),
                text("Loadout:") | bold | color(Color::Cyan),
                text("  [Up/Down]  Navigate docks"),
                text("  [</>]      Change dock scenario"),
                text("  [T]        Cycle ship type"),
                text("  [Enter]    Optimize all docks"),
                text("  [K]        Lock/unlock dock"),
                text("  [B]        Cycle BDA selection"),
                text("  [L]        Load saved loadout"),
                separatorEmpty(),
                text("Officers / Ships:") | bold | color(Color::Cyan),
                text("  [Up/Down]  Navigate list"),
                text("  [F]        Clear filter"),
                separatorEmpty(),
                text("Data:") | bold | color(Color::Cyan),
                text("  Cached in data/game_data/"),
                text("  Roster from roster.csv"),
                text("  Plans in data/player_data/"),
            }) | flex,
        }),
        separator(),
        text("Press [H] to close") | center | dim,
    });
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

static Element render_status_bar(AppState& state) {
    return hbox({
        text(" STFC Tool v0.5 ") | bold | bgcolor(Color::Blue) | color(Color::White),
        text(" "),
        text(state.get_status()) | flex,
        text(" "),
        text(" [R]efresh [S]ync [H]elp [Q]uit ") | dim,
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
        "Daily",
        "Weekly",
        "Crews",
        "Loadout",
        "Officers",
        "Ships",
        "Sync",
    };

    int selected_tab = 0;
    const int tab_count = (int)tab_labels.size();

    // Tab bar is a plain renderer — no Toggle component, so arrow keys are free
    // for per-tab use. Tab switching is handled exclusively via Tab/Shift+Tab
    // in the CatchEvent below.
    auto tab_bar = Renderer([&]() {
        Elements tabs;
        for (int i = 0; i < tab_count; i++) {
            auto label = text(" " + tab_labels[i] + " ");
            if (i == selected_tab) {
                label = label | bold | inverted;
            }
            tabs.push_back(label);
            if (i < tab_count - 1) tabs.push_back(text(" "));
        }
        return hbox(std::move(tabs));
    });

    // Main renderer
    auto main_renderer = Renderer(tab_bar, [&]() {
        // Help overlay takes over the whole screen
        if (state->show_help) {
            return vbox({
                render_help() | flex,
            }) | border;
        }

        Element content;
        switch (selected_tab) {
            case 0: content = render_overview(*state); break;
            case 1: content = render_daily_planner(*state); break;
            case 2: content = render_weekly_planner(*state); break;
            case 3: content = render_crew_optimizer(*state); break;
            case 4: content = render_loadout(*state); break;
            case 5: content = render_officers(*state); break;
            case 6: content = render_ships(*state); break;
            case 7: content = render_sync(*state); break;
            default: content = text("Unknown tab");
        }

        return vbox({
            tab_bar->Render() | center,
            separator(),
            content | flex,
            separator(),
            render_status_bar(*state),
        }) | border;
    });

    // Handle keyboard events
    auto main_component = CatchEvent(main_renderer, [&](Event event) {
        // Global: Help toggle (must come first so H works from anywhere)
        if (event == Event::Character('h') || event == Event::Character('H')) {
            state->show_help = !state->show_help;
            return true;
        }

        // If help is showing, block all other input
        if (state->show_help) {
            if (event == Event::Escape) {
                state->show_help = false;
                return true;
            }
            return true;
        }

        // Global: Tab / Shift+Tab to switch tabs
        if (event == Event::Tab) {
            selected_tab = (selected_tab + 1) % tab_count;
            return true;
        }
        if (event == Event::TabReverse) {
            selected_tab = (selected_tab - 1 + tab_count) % tab_count;
            return true;
        }

        // Global: Quit
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            state->save_plans();
            auto screen = ScreenInteractive::Active();
            if (screen) screen->Exit();
            return true;
        }

        // Global: Refresh game data
        if (event == Event::Character('r') || event == Event::Character('R')) {
            if (!state->loading) {
                state->loading = true;
                state->set_status("Fetching game data from api.spocks.club...");
                std::thread([state]() {
                    state->api_client.set_progress_callback(
                        [state](const std::string& step, int, int) {
                            state->set_status("Loading " + step + "...");
                        }
                    );
                    bool ok = state->api_client.fetch_all(state->game_data);
                    if (ok) {
                        state->set_status("Loaded " +
                            std::to_string(state->game_data.officers.size()) + " officers, " +
                            std::to_string(state->game_data.ships.size()) + " ships, " +
                            std::to_string(state->game_data.researches.size()) + " research nodes");
                        state->data_loaded = true;
                        // Re-resolve player names with fresh game data
                        resolve_player_names(state->player_data, state->game_data);
                        // Re-enrich planner with updated context
                        state->planner.enrich_plan_with_player_data(
                            state->daily_plan, state->player_data, state->game_data);
                    } else {
                        state->set_status("Failed to fetch game data.");
                    }
                    state->loading = false;
                }).detach();
            }
            return true;
        }

        // Global: Toggle ingress server
        if (event == Event::Character('s') || event == Event::Character('S')) {
            if (selected_tab != 1) {  // 's' on Daily tab means skip
                if (state->ingress_server.is_running()) {
                    state->ingress_server.stop();
                    state->set_status("Ingress server stopped.");
                } else {
                    state->ingress_server.set_data_callback([state](const std::string& data_type) {
                        state->set_status("Received sync data: " + data_type);
                        state->player_data = state->ingress_server.get_player_data();
                        // Resolve names against game data
                        if (state->data_loaded) {
                            resolve_player_names(state->player_data, state->game_data);
                            // Re-enrich planner with updated player data
                            state->planner.enrich_plan_with_player_data(
                                state->daily_plan, state->player_data, state->game_data);
                        }
                    });
                    if (state->ingress_server.start()) {
                        state->set_status("Ingress server started on port " +
                            std::to_string(state->ingress_server.port()));
                    } else {
                        state->set_status("Failed to start ingress server!");
                    }
                }
                return true;
            }
        }

        // === Daily Planner tab events ===
        if (selected_tab == 1) {
            auto& plan = state->daily_plan;

            if (event == Event::ArrowDown) {
                if (state->selected_daily_task < (int)plan.tasks.size() - 1) {
                    state->selected_daily_task++;
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_daily_task > 0) {
                    state->selected_daily_task--;
                }
                return true;
            }
            if (event == Event::Character(' ')) {
                if (state->selected_daily_task >= 0 && state->selected_daily_task < (int)plan.tasks.size()) {
                    state->planner.toggle_task(plan, plan.tasks[state->selected_daily_task].id);
                    state->save_plans();
                    state->status_message = "Task toggled. " + std::to_string(plan.completed_tasks()) + "/" + std::to_string(plan.total_tasks()) + " done.";
                }
                return true;
            }
            if (event == Event::Character('s') || event == Event::Character('S')) {
                if (state->selected_daily_task >= 0 && state->selected_daily_task < (int)plan.tasks.size()) {
                    state->planner.skip_task(plan, plan.tasks[state->selected_daily_task].id, "skipped by user");
                    state->save_plans();
                    state->status_message = "Task skipped.";
                }
                return true;
            }
            if (event == Event::Character('c') || event == Event::Character('C')) {
                state->show_completed = !state->show_completed;
                state->status_message = state->show_completed ? "Showing completed tasks" : "Hiding completed tasks";
                return true;
            }
        }

        // === Weekly Planner tab events ===
        if (selected_tab == 2) {
            auto& plan = state->weekly_plan;

            if (event == Event::ArrowLeft) {
                if (state->selected_weekly_day > 0) state->selected_weekly_day--;
                return true;
            }
            if (event == Event::ArrowRight) {
                if (state->selected_weekly_day < 6) state->selected_weekly_day++;
                return true;
            }
            if (event == Event::ArrowDown) {
                if (state->selected_weekly_goal < (int)plan.goals.size() - 1) {
                    state->selected_weekly_goal++;
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_weekly_goal > 0) state->selected_weekly_goal--;
                return true;
            }
            if (event == Event::Character('+') || event == Event::Character('=')) {
                if (state->selected_weekly_goal >= 0 && state->selected_weekly_goal < (int)plan.goals.size()) {
                    auto& g = plan.goals[state->selected_weekly_goal];
                    state->planner.update_goal_progress(plan, g.id, g.progress_current + 1);
                    state->save_plans();
                    state->status_message = g.title + ": " + std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total);
                }
                return true;
            }
            if (event == Event::Character('-')) {
                if (state->selected_weekly_goal >= 0 && state->selected_weekly_goal < (int)plan.goals.size()) {
                    auto& g = plan.goals[state->selected_weekly_goal];
                    int new_val = std::max(0, g.progress_current - 1);
                    state->planner.update_goal_progress(plan, g.id, new_val);
                    state->save_plans();
                    state->status_message = g.title + ": " + std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total);
                }
                return true;
            }
            // Space to toggle day task completion
            if (event == Event::Character(' ')) {
                if (state->selected_weekly_day >= 0 && state->selected_weekly_day < (int)plan.days.size()) {
                    // Toggle the first uncompleted task on the selected day
                    auto& day = plan.days[state->selected_weekly_day];
                    for (auto& t : day.tasks) {
                        if (!t.completed && !t.skipped) {
                            state->planner.toggle_task(day, t.id);
                            state->save_plans();
                            state->status_message = "Marked '" + t.title + "' done on " + std::string(short_dow(state->selected_weekly_day));
                            break;
                        }
                    }
                }
                return true;
            }
        }

        // === Crew Optimizer tab events ===
        if (selected_tab == 3) {
            if (event == Event::Character('<') || event == Event::Character(',')) {
                if (state->crew_scenario > 0) {
                    state->crew_scenario--;
                    state->crew_results.clear();
                    state->crew_bda_results.clear();
                    state->selected_crew = 0;
                }
                return true;
            }
            if (event == Event::Character('>') || event == Event::Character('.')) {
                if (state->crew_scenario < (int)all_dock_scenarios().size() - 1) {
                    state->crew_scenario++;
                    state->crew_results.clear();
                    state->crew_bda_results.clear();
                    state->selected_crew = 0;
                }
                return true;
            }
            if (event == Event::Character('t') || event == Event::Character('T')) {
                state->crew_ship_type = (state->crew_ship_type + 1) % 3;
                state->crew_results.clear();
                state->crew_bda_results.clear();
                state->selected_crew = 0;
                return true;
            }
            if (event == Event::Return) {
                state->status_message = "Running crew optimizer...";
                state->run_crew_optimizer();
                if (!state->crew_results.empty()) {
                    state->status_message = "Found " + std::to_string(state->crew_results.size()) +
                        " crews for " + scenario_label(all_dock_scenarios()[state->crew_scenario]);
                } else {
                    state->status_message = "No crews found.";
                }
                state->selected_crew = 0;
                return true;
            }
            if (event == Event::ArrowDown) {
                if (state->selected_crew < (int)state->crew_results.size() - 1) {
                    state->selected_crew++;
                    state->update_crew_bda();
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_crew > 0) {
                    state->selected_crew--;
                    state->update_crew_bda();
                }
                return true;
            }
        }

        // === Loadout tab events ===
        if (selected_tab == 4) {
            if (event == Event::ArrowDown) {
                if (state->selected_dock < 6) {
                    state->selected_dock++;
                    state->selected_dock_bda = 0;
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_dock > 0) {
                    state->selected_dock--;
                    state->selected_dock_bda = 0;
                }
                return true;
            }
            // Change dock scenario
            if (event == Event::Character('<') || event == Event::Character(',')) {
                auto& cfg = state->dock_configs[state->selected_dock];
                const auto& all = all_dock_scenarios();
                auto it = std::find(all.begin(), all.end(), cfg.scenario);
                if (it != all.begin()) {
                    cfg.scenario = *std::prev(it);
                    state->loadout_computed = false;
                    state->status_message = "Dock " + std::to_string(state->selected_dock + 1) +
                        " -> " + scenario_label(cfg.scenario);
                }
                return true;
            }
            if (event == Event::Character('>') || event == Event::Character('.')) {
                auto& cfg = state->dock_configs[state->selected_dock];
                const auto& all = all_dock_scenarios();
                auto it = std::find(all.begin(), all.end(), cfg.scenario);
                if (it != all.end() && std::next(it) != all.end()) {
                    cfg.scenario = *std::next(it);
                    state->loadout_computed = false;
                    state->status_message = "Dock " + std::to_string(state->selected_dock + 1) +
                        " -> " + scenario_label(cfg.scenario);
                }
                return true;
            }
            // Cycle ship type
            if (event == Event::Character('t') || event == Event::Character('T')) {
                state->crew_ship_type = (state->crew_ship_type + 1) % 3;
                state->loadout_computed = false;
                return true;
            }
            // Lock/unlock dock
            if (event == Event::Character('k') || event == Event::Character('K')) {
                auto& cfg = state->dock_configs[state->selected_dock];
                if (cfg.locked) {
                    cfg.locked = false;
                    cfg.locked_captain.clear();
                    cfg.locked_bridge.clear();
                    state->status_message = "Dock " + std::to_string(state->selected_dock + 1) + " unlocked.";
                } else if (state->loadout_computed &&
                           state->selected_dock < (int)state->loadout_result.docks.size()) {
                    // Lock with current assignment
                    const auto& dr = state->loadout_result.docks[state->selected_dock];
                    cfg.locked = true;
                    cfg.locked_captain = dr.captain;
                    cfg.locked_bridge = dr.bridge;
                    state->status_message = "Dock " + std::to_string(state->selected_dock + 1) +
                        " locked: " + dr.captain;
                }
                return true;
            }
            // Navigate BDA suggestions
            if (event == Event::Character('b') || event == Event::Character('B')) {
                if (state->loadout_computed &&
                    state->selected_dock < (int)state->loadout_result.docks.size()) {
                    int max_bda = (int)state->loadout_result.docks[state->selected_dock].bda_suggestions.size();
                    if (max_bda > 0) {
                        state->selected_dock_bda = (state->selected_dock_bda + 1) % max_bda;
                    }
                }
                return true;
            }
            // Run loadout optimizer
            if (event == Event::Return) {
                if (state->optimizer && !state->loadout_running) {
                    state->status_message = "Optimizing 7-dock loadout...";
                    state->run_loadout_optimizer();
                    state->status_message = "Loadout optimized! " +
                        std::to_string(state->loadout_result.total_officers_used) +
                        " officers assigned across " +
                        std::to_string(state->loadout_result.docks.size()) + " docks.";
                }
                return true;
            }
            // Load saved loadout
            if (event == Event::Character('l') || event == Event::Character('L')) {
                if (CrewOptimizer::load_loadout(state->loadout_result, ".stfc_loadout.json")) {
                    state->loadout_computed = true;
                    state->status_message = "Loaded saved loadout from .stfc_loadout.json";
                } else {
                    state->status_message = "No saved loadout found.";
                }
                return true;
            }
        }

        // === Officers tab events ===
        if (selected_tab == 5) {
            if (event == Event::ArrowDown) {
                int max_off = (int)state->game_data.officers.size() - 1;
                if (state->selected_officer < max_off) state->selected_officer++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_officer > 0) state->selected_officer--;
                return true;
            }
            if (event == Event::Character('f') || event == Event::Character('F')) {
                // TODO: open filter input — for now just clear filter
                state->officer_filter.clear();
                state->selected_officer = 0;
                state->status_message = "Filter cleared.";
                return true;
            }
        }

        // === Ships tab events ===
        if (selected_tab == 6) {
            if (event == Event::ArrowDown) {
                int max_ship = (int)state->game_data.ships.size() - 1;
                if (state->selected_ship < max_ship) state->selected_ship++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_ship > 0) state->selected_ship--;
                return true;
            }
            if (event == Event::Character('f') || event == Event::Character('F')) {
                state->ship_filter.clear();
                state->selected_ship = 0;
                state->status_message = "Filter cleared.";
                return true;
            }
        }

        // === Sync tab events ===
        if (selected_tab == 7) {
            if (event == Event::ArrowLeft) {
                if (state->sync_view_mode > 0) {
                    state->sync_view_mode--;
                    state->sync_selected_row = 0;
                }
                return true;
            }
            if (event == Event::ArrowRight) {
                if (state->sync_view_mode < sync_view_count - 1) {
                    state->sync_view_mode++;
                    state->sync_selected_row = 0;
                }
                return true;
            }
            if (event == Event::ArrowDown) {
                state->sync_selected_row++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->sync_selected_row > 0) state->sync_selected_row--;
                return true;
            }
        }

        // === Mouse wheel support (all tabs) ===
        if (event.is_mouse()) {
            auto& mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp) {
                switch (selected_tab) {
                    case 1: if (state->selected_daily_task > 0) state->selected_daily_task--; break;
                    case 2: if (state->selected_weekly_goal > 0) state->selected_weekly_goal--; break;
                    case 3: if (state->selected_crew > 0) { state->selected_crew--; state->update_crew_bda(); } break;
                    case 4: if (state->selected_dock > 0) { state->selected_dock--; state->selected_dock_bda = 0; } break;
                    case 5: if (state->selected_officer > 0) state->selected_officer--; break;
                    case 6: if (state->selected_ship > 0) state->selected_ship--; break;
                    case 7: if (state->sync_selected_row > 0) state->sync_selected_row--; break;
                    default: break;
                }
                return true;
            }
            if (mouse.button == Mouse::WheelDown) {
                switch (selected_tab) {
                    case 1: if (state->selected_daily_task < (int)state->daily_plan.tasks.size() - 1) state->selected_daily_task++; break;
                    case 2: if (state->selected_weekly_goal < (int)state->weekly_plan.goals.size() - 1) state->selected_weekly_goal++; break;
                    case 3: if (state->selected_crew < (int)state->crew_results.size() - 1) { state->selected_crew++; state->update_crew_bda(); } break;
                    case 4: if (state->selected_dock < 6) { state->selected_dock++; state->selected_dock_bda = 0; } break;
                    case 5: if (state->selected_officer < (int)state->game_data.officers.size() - 1) state->selected_officer++; break;
                    case 6: if (state->selected_ship < (int)state->game_data.ships.size() - 1) state->selected_ship++; break;
                    case 7: state->sync_selected_row++; break;
                    default: break;
                }
                return true;
            }
        }

        // === PageUp / PageDown (all scrollable tabs, jump 10 rows) ===
        if (event == Event::PageUp) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = std::max(0, state->selected_daily_task - 10); break;
                case 2: state->selected_weekly_goal = std::max(0, state->selected_weekly_goal - 10); break;
                case 3: state->selected_crew = std::max(0, state->selected_crew - 10); state->update_crew_bda(); break;
                case 4: state->selected_dock = std::max(0, state->selected_dock - 10); state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = std::max(0, state->selected_officer - 10); break;
                case 6: state->selected_ship = std::max(0, state->selected_ship - 10); break;
                case 7: state->sync_selected_row = std::max(0, state->sync_selected_row - 10); break;
                default: break;
            }
            return true;
        }
        if (event == Event::PageDown) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = std::min((int)state->daily_plan.tasks.size() - 1, state->selected_daily_task + 10); break;
                case 2: state->selected_weekly_goal = std::min((int)state->weekly_plan.goals.size() - 1, state->selected_weekly_goal + 10); break;
                case 3: state->selected_crew = std::min((int)state->crew_results.size() - 1, state->selected_crew + 10); state->update_crew_bda(); break;
                case 4: state->selected_dock = std::min(6, state->selected_dock + 10); state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = std::min((int)state->game_data.officers.size() - 1, state->selected_officer + 10); break;
                case 6: state->selected_ship = std::min((int)state->game_data.ships.size() - 1, state->selected_ship + 10); break;
                case 7: state->sync_selected_row += 10; break;
                default: break;
            }
            return true;
        }

        // === Home / End keys ===
        if (event == Event::Home) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = 0; break;
                case 2: state->selected_weekly_goal = 0; break;
                case 3: state->selected_crew = 0; state->update_crew_bda(); break;
                case 4: state->selected_dock = 0; state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = 0; break;
                case 6: state->selected_ship = 0; break;
                case 7: state->sync_selected_row = 0; break;
                default: break;
            }
            return true;
        }
        if (event == Event::End) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = std::max(0, (int)state->daily_plan.tasks.size() - 1); break;
                case 2: state->selected_weekly_goal = std::max(0, (int)state->weekly_plan.goals.size() - 1); break;
                case 3: state->selected_crew = std::max(0, (int)state->crew_results.size() - 1); state->update_crew_bda(); break;
                case 4: state->selected_dock = 6; state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = std::max(0, (int)state->game_data.officers.size() - 1); break;
                case 6: state->selected_ship = std::max(0, (int)state->game_data.ships.size() - 1); break;
                case 7: state->sync_selected_row = 999999; break;  // clamped in render
                default: break;
            }
            return true;
        }

        return false;
    });

    auto screen = ScreenInteractive::Fullscreen();
    screen.Loop(main_component);

    // Cleanup
    state->save_plans();
    state->ingress_server.stop();

    return 0;
}
