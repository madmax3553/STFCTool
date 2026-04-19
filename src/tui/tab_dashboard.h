#pragma once
// ---------------------------------------------------------------------------
// Dashboard Tab — compact single-column for 80x24
// ---------------------------------------------------------------------------

#include <algorithm>
#include <map>
#include <climits>

#include "ftxui/dom/elements.hpp"
#include "data/models.h"
#include "tui/ui_common.h"

namespace stfc {

using namespace ftxui;

inline Element render_dashboard_tab(const PlayerData& pd, const GameData& gd, bool data_loaded) {

    // --- Identity line ---
    std::string name = pd.player_name.empty() ? "Unknown Commander" : pd.player_name;
    std::string ops = pd.ops_level > 0 ? "Ops " + std::to_string(pd.ops_level) : "";

    auto identity = hbox({
        text(name) | bold | color(Color::Cyan),
        ops.empty() ? text("") : text("  " + ops) | bold | color(Color::Yellow),
        filler(),
        text("Sync " + ui::fmt_time(pd.last_sync)) |
            (pd.last_sync != std::chrono::system_clock::time_point{} ? color(Color::Green) : dim),
    });

    // --- Active Jobs (compact, one line each, max 3) ---
    Elements job_lines;
    if (!pd.jobs.empty()) {
        int shown = 0;
        for (auto& j : pd.jobs) {
            if (j.completed || shown >= 3) continue;
            int rem = job_remaining_seconds(j);
            if (rem < -3600) continue;
            std::string type = job_type_str(j.job_type);
            std::string time_s = rem > 0 ? format_duration_short(rem) : "DONE";
            Color tc = rem > 0 ? (rem < 300 ? Color::Yellow : Color::White) : Color::Green;
            job_lines.push_back(hbox({
                text("  " + ui::pad(type, 14)),
                text(time_s) | bold | color(tc),
            }));
            shown++;
        }
    }
    if (job_lines.empty()) {
        job_lines.push_back(text("  Queue empty") | dim);
    }

    // --- Events summary ---
    Elements ev_lines;
    if (!pd.events.empty()) {
        int64_t now = ui::now_epoch();
        int active = 0, upcoming = 0, claimable = 0;
        int soonest = INT_MAX;
        std::string soonest_name;
        for (auto& e : pd.events) {
            bool is_active = e.schedule.start > 0 && e.schedule.end > 0 &&
                             now >= e.schedule.start && now < e.schedule.end;
            bool is_upcoming = e.schedule.start > 0 && now < e.schedule.start;
            if (is_active) {
                active++;
                int rem = (int)(e.schedule.end - now);
                if (rem < soonest) { soonest = rem; soonest_name = event_category_str(e.category); }
            }
            if (is_upcoming) upcoming++;
            if (e.entry_data.can_claim) claimable++;
        }
        ev_lines.push_back(hbox({
            text("  Active ") | dim, text(std::to_string(active)) | bold | color(active > 0 ? Color::Green : Color::GrayDark),
            text("  Upcoming ") | dim, text(std::to_string(upcoming)) | bold | color(upcoming > 0 ? Color::Yellow : Color::GrayDark),
            text("  Claimable ") | dim, text(std::to_string(claimable)) | bold | color(claimable > 0 ? Color::Green : Color::GrayDark),
        }));
        if (soonest < INT_MAX) {
            ev_lines.push_back(hbox({
                text("  Ending: ") | dim,
                text(ui::trunc(soonest_name, 20)) | color(Color::Yellow),
                text(" in " + ui::fmt_dur(soonest)) | dim,
            }));
        }
    } else {
        ev_lines.push_back(text("  No events synced") | dim);
    }

    // --- Fleet (top 4 ships, one line each) ---
    Elements fleet_lines;
    if (!pd.ships.empty()) {
        auto sorted = pd.ships;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerShip& a, const PlayerShip& b) {
            return a.tier > b.tier || (a.tier == b.tier && a.level > b.level);
        });
        for (int i = 0; i < std::min(4, (int)sorted.size()); i++) {
            auto& s = sorted[i];
            std::string sname = s.name.empty() ? "Hull#" + std::to_string(s.hull_id) : s.name;
            fleet_lines.push_back(hbox({
                text("  " + ui::pad(ui::trunc(sname, 20), 22)),
                text("T" + std::to_string(s.tier)) | bold | color(Color::Cyan),
                text(" Lv" + std::to_string(s.level)) | dim,
            }));
        }
        if ((int)sorted.size() > 4)
            fleet_lines.push_back(text("  +" + std::to_string(sorted.size() - 4) + " more") | dim);
    } else {
        fleet_lines.push_back(text("  No ships") | dim);
    }

    // --- Officers summary (one line) ---
    Elements off_lines;
    if (!pd.officers.empty()) {
        int max_lv = 0;
        std::map<int, int> rank_counts;
        for (auto& o : pd.officers) {
            rank_counts[o.rank]++;
            if (o.level > max_lv) max_lv = o.level;
        }
        std::string summary = "  " + std::to_string(pd.officers.size()) + " officers  MaxLv:" + std::to_string(max_lv);
        for (auto it = rank_counts.rbegin(); it != rank_counts.rend(); ++it) {
            summary += "  R" + std::to_string(it->first) + ":" + std::to_string(it->second);
        }
        off_lines.push_back(text(summary));
    } else {
        off_lines.push_back(text("  No officer data") | dim);
    }

    // --- Resources (top 4, compact) ---
    Elements res_lines;
    if (!pd.resources.empty()) {
        auto sorted = pd.resources;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerResource& a, const PlayerResource& b) {
            return a.amount > b.amount;
        });
        for (int i = 0; i < std::min(4, (int)sorted.size()); i++) {
            auto& r = sorted[i];
            if (r.amount == 0) break;
            std::string rname = r.name.empty() ? "ID:" + std::to_string(r.resource_id) : r.name;
            res_lines.push_back(hbox({
                text("  " + ui::pad(ui::trunc(rname, 20), 22)),
                text(ui::fmt_num(r.amount)) | bold | color(Color::Green),
            }));
        }
    } else {
        res_lines.push_back(text("  No resource data") | dim);
    }

    // --- Game data line ---
    auto gd_line = hbox({
        text("  Game: ") | dim,
        text(std::to_string(gd.officers.size())) | bold, text(" officers  ") | dim,
        text(std::to_string(gd.ships.size())) | bold, text(" ships  ") | dim,
        text(std::to_string(gd.researches.size())) | bold, text(" research") | dim,
    });

    // --- Assemble ---
    return vbox({
        identity,
        separator(),
        text(" Jobs") | bold | color(Color::Cyan),
        vbox(job_lines),
        separator(),
        text(" Events") | bold | color(Color::Cyan),
        vbox(ev_lines),
        separator(),
        text(" Fleet") | bold | color(Color::Cyan),
        vbox(fleet_lines),
        separator(),
        text(" Officers") | bold | color(Color::Cyan),
        vbox(off_lines),
        separator(),
        text(" Resources") | bold | color(Color::Cyan),
        vbox(res_lines),
        filler(),
        gd_line | dim,
    }) | flex;
}

} // namespace stfc
