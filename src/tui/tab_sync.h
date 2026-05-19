#pragma once
// ---------------------------------------------------------------------------
// Sync Tab — btm/Charm style data browser + sync log
// ---------------------------------------------------------------------------

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/component/event.hpp"
#include "data/models.h"
#include "data/ingress_server.h"
#include "tui/ui_common.h"

namespace stfc {

using namespace ftxui;

struct SyncTabState {
    int view = 0;       // 0-7
    int selected = 0;
};

struct SyncJobCounts {
    int live = 0;
    int raw = 0;
    int ignored = 0;
};

inline bool sync_job_is_live(const PlayerData& pd, const PlayerJob& job, int64_t now) {
    if (has_actionable_completed_job_claim(pd, job, now)) return true;
    return has_live_unfinished_job(pd, job, now);
}

inline SyncJobCounts sync_job_counts(const PlayerData& pd, int64_t now) {
    SyncJobCounts counts;
    counts.raw = static_cast<int>(pd.jobs.size());
    for (const auto& job : pd.jobs) {
        if (sync_job_is_live(pd, job, now)) counts.live++;
    }
    counts.ignored = counts.raw - counts.live;
    return counts;
}

inline std::string sync_job_kind(const PlayerJob& job) {
    if (job.research_id != 0) return "Research";
    if (job.building_id != 0) return "Building";
    return job_type_str(job.job_type);
}

inline std::string sync_job_target(const PlayerJob& job, const GameData& gd) {
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
    if (!job.uuid.empty()) return "Job " + job.uuid.substr(0, std::min<size_t>(8, job.uuid.size()));
    return "Unknown target";
}

inline int sync_job_rank(const PlayerData& pd, const PlayerJob& job, int64_t now) {
    if (has_actionable_completed_job_claim(pd, job, now)) return 0;
    if (!job.completed) {
        if (unfinished_job_is_stale(pd, job, now)) return 4;
        int remaining = job_remaining_seconds(job);
        if (remaining <= 0) return 1;
        if (remaining > 0) return 2;
    }
    if (completed_job_already_reflected(pd, job)) return 3;
    return 4;
}

inline std::string sync_job_status(const PlayerData& pd, const PlayerJob& job,
                                   int64_t now, Color& color_out) {
    if (has_actionable_completed_job_claim(pd, job, now)) {
        color_out = Color::Green;
        return "Claim";
    }
    if (job.completed && completed_job_already_reflected(pd, job)) {
        color_out = Color::GrayDark;
        return "Reflected";
    }
    if (job.completed) {
        color_out = Color::GrayDark;
        return "Stale";
    }

    if (unfinished_job_is_stale(pd, job, now)) {
        color_out = Color::GrayDark;
        return "Stale";
    }

    int remaining = job_remaining_seconds(job);
    if (remaining <= 0) {
        color_out = Color::Green;
        return "Ready";
    }
    color_out = Color::Yellow;
    return "Active";
}

inline Element render_sync_tab(const PlayerData& pd, const GameData& gd,
                                IngressServer& server, SyncTabState& ss) {
    static const char* labels[] = {
        "Officers", "Ships", "Resources", "Buildings", "Research", "Jobs", "Buffs", "Events"
    };

    bool running = server.is_running();

    // Status bar
    auto status = hbox({
        text(" Server: "),
        running ? text("● RUNNING :" + std::to_string(server.port()) + " ") | bold | color(Color::Green)
                : text("○ STOPPED ") | color(Color::Red),
        filler(),
        text("Sync: " + ui::fmt_time(pd.last_sync) + " ") |
            (pd.last_sync != std::chrono::system_clock::time_point{} ? color(Color::Green) : dim),
        text(" [S] Toggle ") | dim,
    });

    // Sub-view tabs
    int64_t now = ui::now_epoch();
    SyncJobCounts job_counts = sync_job_counts(pd, now);
    std::string job_sync_label = pd.last_job_sync == std::chrono::system_clock::time_point{}
        ? "never"
        : ui::fmt_time(pd.last_job_sync);
    int counts[] = {
        (int)pd.officers.size(), (int)pd.ships.size(), (int)pd.resources.size(),
        (int)pd.buildings.size(), (int)pd.researches.size(), job_counts.live,
        (int)pd.buffs.size(), (int)pd.events.size()
    };

    Elements tabs;
    for (int i = 0; i < 8; i++) {
        std::string label = std::string(labels[i]) + "(" + std::to_string(counts[i]);
        if (i == 5 && job_counts.raw != job_counts.live) {
            label += "/" + std::to_string(job_counts.raw);
        }
        label += ")";
        auto tab = text(" " + label + " ");
        if (i == ss.view) tab = tab | bold | inverted;
        else if (counts[i] > 0) tab = tab | color(Color::Cyan);
        else tab = tab | dim;
        tabs.push_back(tab);
    }

    // Data rows
    Elements rows;
    int max_rows = 0;
    bool custom_empty_message = false;

    switch (ss.view) {
    case 0: { // Officers
        rows.push_back(ui::tbl_header({{"Name", 24}, {"Lv", 5}, {"Rk", 4}, {"Shards", 7}, {"ID", 12}}));
        auto sorted = pd.officers;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerOfficer& a, const PlayerOfficer& b) {
            return a.level > b.level || (a.level == b.level && a.rank > b.rank);
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < max_rows; i++) {
            auto& o = sorted[i];
            std::string name = o.name.empty() ? "OID:" + std::to_string(o.officer_id) : o.name;
            auto row = hbox({
                text(ui::trunc(name, 22)) | size(WIDTH, EQUAL, 24),
                text(std::to_string(o.level)) | bold | size(WIDTH, EQUAL, 5),
                text(std::to_string(o.rank)) | size(WIDTH, EQUAL, 4),
                text(std::to_string(o.shard_count)) | dim | size(WIDTH, EQUAL, 7),
                text(std::to_string(o.officer_id)) | dim | size(WIDTH, EQUAL, 12),
            });
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    case 1: { // Ships
        rows.push_back(ui::tbl_header({{"Name", 24}, {"Tier", 5}, {"Lv", 5}, {"Hull", 10}}));
        auto sorted = pd.ships;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerShip& a, const PlayerShip& b) {
            return a.tier > b.tier || (a.tier == b.tier && a.level > b.level);
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < max_rows; i++) {
            auto& s = sorted[i];
            std::string name = s.name.empty() ? "Hull:" + std::to_string(s.hull_id) : s.name;
            auto row = hbox({
                text(ui::trunc(name, 22)) | size(WIDTH, EQUAL, 24),
                text("T" + std::to_string(s.tier)) | bold | color(Color::Cyan) | size(WIDTH, EQUAL, 5),
                text(std::to_string(s.level)) | size(WIDTH, EQUAL, 5),
                text(std::to_string(s.hull_id)) | dim | size(WIDTH, EQUAL, 10),
            });
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    case 2: { // Resources
        rows.push_back(ui::tbl_header({{"Name", 24}, {"Amount", 14}, {"ID", 10}}));
        auto sorted = pd.resources;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerResource& a, const PlayerResource& b) {
            return a.amount > b.amount;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < max_rows; i++) {
            auto& r = sorted[i];
            std::string name = r.name.empty() ? "RID:" + std::to_string(r.resource_id) : r.name;
            auto row = hbox({
                text(ui::trunc(name, 22)) | size(WIDTH, EQUAL, 24),
                text(ui::fmt_num(r.amount)) | bold | color(Color::Green) | size(WIDTH, EQUAL, 14),
                text(std::to_string(r.resource_id)) | dim | size(WIDTH, EQUAL, 10),
            });
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    case 3: { // Buildings
        rows.push_back(ui::tbl_header({{"Name", 28}, {"Level", 7}, {"ID", 10}}));
        auto sorted = pd.buildings;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerBuilding& a, const PlayerBuilding& b) {
            return a.level > b.level;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < max_rows; i++) {
            auto& b = sorted[i];
            std::string name = b.name.empty() ? "BID:" + std::to_string(b.building_id) : b.name;
            auto row = hbox({
                text(ui::trunc(name, 26)) | size(WIDTH, EQUAL, 28),
                text(std::to_string(b.level)) | bold | size(WIDTH, EQUAL, 7),
                text(std::to_string(b.building_id)) | dim | size(WIDTH, EQUAL, 10),
            });
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    case 4: { // Research
        rows.push_back(ui::tbl_header({{"Name", 28}, {"Level", 7}, {"ID", 10}}));
        auto sorted = pd.researches;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerResearch& a, const PlayerResearch& b) {
            return a.level > b.level;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < max_rows; i++) {
            auto& r = sorted[i];
            std::string name = r.name.empty() ? "RID:" + std::to_string(r.research_id) : r.name;
            auto row = hbox({
                text(ui::trunc(name, 26)) | size(WIDTH, EQUAL, 28),
                text(std::to_string(r.level)) | bold | size(WIDTH, EQUAL, 7),
                text(std::to_string(r.research_id)) | dim | size(WIDTH, EQUAL, 10),
            });
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    case 5: { // Jobs
        rows.push_back(ui::tbl_header({{"Type", 12}, {"Status", 10}, {"Time", 12}, {"Target", 34}, {"Raw", 5}}));
        if (job_counts.raw == 0) {
            rows.push_back(text("  No job payload has been received by this sync target.") |
                           color(Color::Yellow));
            rows.push_back(text("  Treat building/research/ship queue state as manual/unknown.") | dim);
            custom_empty_message = true;
        }
        rows.push_back(text("  Last job payload: " + job_sync_label) | dim);
        if (job_counts.raw > 0 && job_counts.ignored > 0) {
            rows.push_back(hbox({
                text("  Live queue " + std::to_string(job_counts.live) + " / raw records " +
                     std::to_string(job_counts.raw) + "; stale/reflected rows are ignored, active queues are unknown without a fresh job payload") | dim,
            }));
        }

        auto sorted = pd.jobs;
        std::stable_sort(sorted.begin(), sorted.end(),
            [&pd, now](const PlayerJob& a, const PlayerJob& b) {
                int ra = sync_job_rank(pd, a, now);
                int rb = sync_job_rank(pd, b, now);
                if (ra != rb) return ra < rb;
                if (!a.completed && !b.completed) {
                    return job_remaining_seconds(a) < job_remaining_seconds(b);
                }
                return a.start_time > b.start_time;
            });

        max_rows = (int)sorted.size();
        for (int i = 0; i < max_rows; i++) {
            auto& j = sorted[i];
            Color sc = Color::White;
            std::string st = sync_job_status(pd, j, now, sc);
            int rem = j.completed ? 0 : job_remaining_seconds(j);
            bool live = sync_job_is_live(pd, j, now);
            auto row = hbox({
                text(ui::trunc(sync_job_kind(j), 10)) | size(WIDTH, EQUAL, 12),
                text(st) | bold | color(sc) | size(WIDTH, EQUAL, 10),
                text(j.completed ? "-" : format_duration_short(rem)) | size(WIDTH, EQUAL, 12),
                text(ui::trunc(sync_job_target(j, gd), 32)) | size(WIDTH, EQUAL, 34),
                text(live ? "live" : "raw") | dim | size(WIDTH, EQUAL, 5),
            });
            if (!live) row = row | dim;
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    case 6: { // Buffs
        rows.push_back(ui::tbl_header({{"ID", 12}, {"Lv", 5}, {"Status", 9}, {"Expires", 16}}));
        max_rows = (int)pd.buffs.size();
        for (int i = 0; i < max_rows; i++) {
            auto& b = pd.buffs[i];
            std::string st = b.expired ? "Expired" : "Active";
            Color sc = b.expired ? Color::Red : Color::Green;
            std::string exp = b.expiry_time.has_value() ? "" : "Perm";
            if (b.expiry_time.has_value() && !b.expired) {
                int rem = (int)(b.expiry_time.value() - ui::now_epoch());
                exp = rem > 0 ? format_duration_short(rem) : "Expired";
                if (rem <= 0) { st = "Expired"; sc = Color::Red; }
            }
            auto row = hbox({
                text(std::to_string(b.buff_id)) | size(WIDTH, EQUAL, 12),
                text(std::to_string(b.level)) | size(WIDTH, EQUAL, 5),
                text(st) | bold | color(sc) | size(WIDTH, EQUAL, 9),
                text(exp) | size(WIDTH, EQUAL, 16),
            });
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    case 7: { // Events
        rows.push_back(ui::tbl_header({{"Category", 16}, {"State", 8}, {"Score", 10}, {"Time", 10}, {"!", 2}}));
        int64_t now = ui::now_epoch();
        auto sorted = pd.events;
        std::sort(sorted.begin(), sorted.end(), [now](const PlayerEvent& a, const PlayerEvent& b) {
            auto sp = [now](const PlayerEvent& e) {
                if (e.schedule.start > 0 && e.schedule.end > 0) {
                    if (now >= e.schedule.start && now < e.schedule.end) return 0;
                    if (now < e.schedule.start) return 1;
                }
                return 2;
            };
            int sa = sp(a), sb = sp(b);
            if (sa != sb) return sa < sb;
            return a.schedule.end < b.schedule.end;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < max_rows; i++) {
            auto& e = sorted[i];
            std::string st; Color sc; int rem = 0;
            if (e.schedule.start > 0 && e.schedule.end > 0) {
                if (now >= e.schedule.start && now < e.schedule.end) { st = "Active"; sc = Color::Green; rem = (int)(e.schedule.end - now); }
                else if (now < e.schedule.start) { st = "Soon"; sc = Color::Yellow; rem = (int)(e.schedule.start - now); }
                else { st = "Ended"; sc = Color::GrayDark; }
            } else { st = "-"; sc = Color::GrayDark; }
            auto row = hbox({
                text(ui::trunc(event_category_str(e.category), 14)) | size(WIDTH, EQUAL, 16),
                text(st) | bold | color(sc) | size(WIDTH, EQUAL, 8),
                text(e.ranking.score > 0 ? ui::fmt_num((int64_t)e.ranking.score) : "-") | size(WIDTH, EQUAL, 10),
                text(rem > 0 ? ui::fmt_dur(rem) : "-") | size(WIDTH, EQUAL, 10),
                text(e.has_manual_claimable_reward(now) ? "!" : " ") | bold | color(Color::Green) | size(WIDTH, EQUAL, 2),
            });
            if (i == ss.selected) row = row | inverted | focus;
            rows.push_back(row);
        }
        break;
    }
    }

    if (max_rows == 0 && !custom_empty_message) {
        rows.push_back(text("  No data — sync from STFC with community mod") | dim);
    }
    if (max_rows > 0) ss.selected = std::clamp(ss.selected, 0, max_rows - 1);

    // Sync log
    const auto& sync_log = server.get_sync_log();
    Elements log_lines;
    if (sync_log.empty()) {
        log_lines.push_back(text("No sync events") | dim);
    } else {
        int start = std::max(0, (int)sync_log.size() - 12);
        for (int i = (int)sync_log.size() - 1; i >= start; i--) {
            auto& ev = sync_log[i];
            auto tt = std::chrono::system_clock::to_time_t(ev.timestamp);
            std::tm tb{}; localtime_r(&tt, &tb);
            char ts[16]; std::snprintf(ts, sizeof(ts), "%02d:%02d", tb.tm_hour, tb.tm_min);
            Elements r;
            r.push_back(text(std::string(ts) + " ") | dim);
            if (ev.success) {
                r.push_back(text("OK ") | color(Color::Green));
                r.push_back(text(ev.data_type + "(" + std::to_string(ev.record_count) + ")") | dim);
            } else {
                r.push_back(text("ERR ") | color(Color::Red));
                r.push_back(text(ev.data_type) | dim);
            }
            log_lines.push_back(hbox(std::move(r)));
        }
    }

    return vbox({
        status,
        separator(),
        hbox(tabs) | center,
        separator(),
        hbox({
            vbox(rows) | vscroll_indicator | yframe | flex,
            separator(),
            vbox({text(" Sync Log") | bold | color(Color::Cyan), separator(), vbox(log_lines)}) | size(WIDTH, EQUAL, 32),
        }) | flex,
        hbox({text(" h/l:view  ") | dim, text("j/k:navigate  ") | dim, text("s:server") | dim}),
    });
}

inline bool handle_sync_input(const Event& event, SyncTabState& ss) {
    if (event == Event::ArrowLeft) { ss.view = std::max(0, ss.view - 1); ss.selected = 0; return true; }
    if (event == Event::ArrowRight) { ss.view = std::min(7, ss.view + 1); ss.selected = 0; return true; }
    if (event == Event::ArrowDown) { ss.selected++; return true; }
    if (event == Event::ArrowUp) { ss.selected = std::max(0, ss.selected - 1); return true; }
    if (event == Event::PageDown) { ss.selected += 10; return true; }
    if (event == Event::PageUp) { ss.selected = std::max(0, ss.selected - 10); return true; }
    if (event == Event::Home) { ss.selected = 0; return true; }
    if (event == Event::End) { ss.selected = 9999; return true; }
    return false;
}

} // namespace stfc
