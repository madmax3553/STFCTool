#pragma once
// ---------------------------------------------------------------------------
// Dashboard Tab — compact single-column for 80x24
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <map>
#include <climits>

#include "ftxui/dom/elements.hpp"
#include "data/models.h"
#include "tui/ui_common.h"

namespace stfc {

using namespace ftxui;

inline bool dashboard_event_is_background(const PlayerEvent& e) {
    return e.category == EventCategory::FieldTraining ||
           e.category == EventCategory::FtCategory ||
           e.category == EventCategory::Cutscenes ||
           e.category == EventCategory::MinigameCategory ||
           e.category == EventCategory::MinigameStage ||
           e.category == EventCategory::LoopMuseum ||
           e.category == EventCategory::LoopMuseumTask;
}

inline std::string dashboard_event_label(const PlayerEvent& e, const GameData& gd) {
    if (!e.group_name.empty()) return e.group_name;

    std::string scope = e.source.find("alliance") != std::string::npos ? "alliance" : "solo";
    std::string exact_key = "config_id:" + e.config_id;
    auto exact = gd.event_label_overrides.find(exact_key);
    if (exact != gd.event_label_overrides.end() && !exact->second.empty()) return exact->second;

    std::string family_key = "category:" + std::to_string(static_cast<int>(e.category)) +
        "|priority:" + std::to_string(e.metadata.priority) + "|scope:" + scope;
    auto family = gd.event_label_overrides.find(family_key);
    if (family != gd.event_label_overrides.end() && !family->second.empty()) return family->second;

    std::string label = scope == "alliance" ? "Alliance" : "Solo";
    int64_t duration = e.schedule.end - e.schedule.start;
    if (!e.schedule.term.empty() && e.schedule.term != "None") {
        std::string term = e.schedule.term;
        term[0] = static_cast<char>(std::toupper(term[0]));
        label += " " + term;
    } else if (duration >= 20LL * 24 * 3600) {
        label += " Multiweek";
    } else if (duration >= 6LL * 24 * 3600) {
        label += " Weekly";
    } else if (duration >= 20LL * 3600) {
        label += " Daily";
    }

    std::string category = event_category_str(e.category);
    if (category == "Unknown") category = "Category " + std::to_string(static_cast<int>(e.category));
    if (category == "Special Event") category = "Special";
    label += " " + category;
    if (e.metadata.priority > 0) {
        label += " #" + std::to_string(e.metadata.priority >= 100000
            ? e.metadata.priority % 1000
            : e.metadata.priority);
    }
    return label;
}

inline std::string dashboard_job_kind(const PlayerJob& job) {
    if (job.research_id != 0) return "Research";
    if (job.building_id != 0) return "Building";
    return job_type_str(job.job_type);
}

inline std::string dashboard_job_target(const PlayerJob& job, const GameData& gd) {
    if (job.research_id != 0) {
        auto it = gd.researches.find(job.research_id);
        std::string name = it != gd.researches.end() && !it->second.name.empty()
            ? it->second.name
            : "Research#" + std::to_string(job.research_id);
        return name + " L" + std::to_string(job.level);
    }
    if (job.building_id != 0) {
        auto it = gd.buildings.find(job.building_id);
        std::string name = it != gd.buildings.end() && !it->second.name.empty()
            ? it->second.name
            : "Building#" + std::to_string(job.building_id);
        return name + " L" + std::to_string(job.level);
    }
    return "Level " + std::to_string(job.level);
}

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
    int64_t job_now = ui::now_epoch();
    int completed_recent = 0;
    int stale_unfinished = 0;
    for (auto& j : pd.jobs) {
        if (has_actionable_completed_job_claim(pd, j, job_now)) completed_recent++;
        if (unfinished_job_is_stale(pd, j, job_now)) stale_unfinished++;
    }
    if (completed_recent > 0) {
        job_lines.push_back(hbox({
            text("  Claimable      "),
            text(std::to_string(completed_recent) + " completed") | bold | color(Color::Green),
        }));
    }
    if (!pd.jobs.empty()) {
        int shown = 0;
        for (auto& j : pd.jobs) {
            if (j.completed || shown >= 3) continue;
            if (!has_live_unfinished_job(pd, j, job_now)) continue;
            int rem = job_remaining_seconds(j);
            std::string type = dashboard_job_kind(j);
            std::string time_s = rem > 0 ? format_duration_short(rem) : "READY";
            Color tc = rem > 0 ? (rem < 300 ? Color::Yellow : Color::White) : Color::Green;
            job_lines.push_back(hbox({
                text("  " + ui::pad(ui::trunc(type, 10), 12)),
                text(ui::pad(time_s, 9)) | bold | color(tc),
                text(ui::trunc(dashboard_job_target(j, gd), 28)) | dim,
            }));
            shown++;
        }
    }
    if (job_lines.empty()) {
        bool has_job_sync = pd.last_job_sync != std::chrono::system_clock::time_point{};
        std::string last_job = has_job_sync ? " last " + ui::fmt_time(pd.last_job_sync) : "";
        if (stale_unfinished > 0) {
            job_lines.push_back(text("  Active queues unknown: " + std::to_string(stale_unfinished) +
                                     " stale raw job record(s)" + last_job) | color(Color::Yellow));
        } else {
            job_lines.push_back(text("  Active queues unknown; no recent job payload" + last_job) | color(Color::Yellow));
        }
    }

    // --- Events summary ---
    Elements ev_lines;
    if (!pd.events.empty()) {
        int64_t now = ui::now_epoch();
        int active = 0, upcoming = 0, claimable = 0;
        int soonest = INT_MAX;
        std::string soonest_name;
        for (auto& e : pd.events) {
            if (dashboard_event_is_background(e)) continue;
            bool is_active = e.schedule.start > 0 && e.schedule.end > 0 &&
                             now >= e.schedule.start && now < e.schedule.end;
            bool is_upcoming = e.schedule.start > 0 && now < e.schedule.start;
            if (is_active) {
                active++;
                int rem = (int)(e.schedule.end - now);
                if (rem < soonest) { soonest = rem; soonest_name = dashboard_event_label(e, gd); }
            }
            if (is_upcoming) upcoming++;
            if (e.has_manual_claimable_reward(now)) claimable++;
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
