#pragma once
// ---------------------------------------------------------------------------
// Events Tab — btm/Charm style with list + detail split
// ---------------------------------------------------------------------------

#include <algorithm>
#include <vector>
#include <climits>

#include "ftxui/dom/elements.hpp"
#include "ftxui/component/event.hpp"
#include "data/models.h"
#include "tui/ui_common.h"

namespace stfc {

using namespace ftxui;

struct EventsTabState {
    int selected = 0;
    int filter = 0;   // 0=all, 1=active, 2=claimable, 3=upcoming
};

inline Element render_events_tab(const PlayerData& pd, EventsTabState& es) {
    int64_t now = ui::now_epoch();

    // Classify and filter events
    struct EV {
        const PlayerEvent* e;
        int state;   // 0=active, 1=upcoming, 2=ended
        int remain;
    };
    std::vector<EV> views;

    int n_active = 0, n_upcoming = 0, n_claim = 0;

    for (const auto& e : pd.events) {
        int st = 2, rem = 0;
        if (e.schedule.start > 0 && e.schedule.end > 0) {
            if (now >= e.schedule.start && now < e.schedule.end) {
                st = 0; rem = (int)(e.schedule.end - now); n_active++;
            } else if (now < e.schedule.start) {
                st = 1; rem = (int)(e.schedule.start - now); n_upcoming++;
            }
        }
        if (e.entry_data.can_claim) n_claim++;

        // Filter
        if (es.filter == 1 && st != 0) continue;
        if (es.filter == 2 && !e.entry_data.can_claim) continue;
        if (es.filter == 3 && st != 1) continue;

        views.push_back({&e, st, rem});
    }

    // Sort: active first (soonest ending), upcoming, ended
    std::sort(views.begin(), views.end(), [](const EV& a, const EV& b) {
        if (a.state != b.state) return a.state < b.state;
        if (a.state < 2) return a.remain < b.remain;
        return a.e->schedule.end > b.e->schedule.end;
    });

    int count = (int)views.size();
    if (count > 0) es.selected = std::clamp(es.selected, 0, count - 1);

    // --- Summary bar ---
    static const char* filt_labels[] = {"All", "Active", "Claimable", "Upcoming"};
    auto summary = hbox({
        text(" ") ,
        text(std::to_string(pd.events.size())) | bold | color(Color::Cyan),
        text(" events  "),
        text("Active:") | dim, text(std::to_string(n_active)) | bold | color(n_active > 0 ? Color::Green : Color::GrayDark),
        text("  Upcoming:") | dim, text(std::to_string(n_upcoming)) | bold | color(n_upcoming > 0 ? Color::Yellow : Color::GrayDark),
        text("  Claimable:") | dim, text(std::to_string(n_claim)) | bold | color(n_claim > 0 ? Color::Green : Color::GrayDark),
        filler(),
        text("[F] ") | dim, text(filt_labels[es.filter]) | bold | color(Color::Cyan),
    });

    // --- Event list rows ---
    Elements rows;
    for (int i = 0; i < count; i++) {
        const auto& v = views[i];
        const auto& e = *v.e;

        // State indicator
        std::string st_icon;
        Color st_color;
        switch (v.state) {
            case 0: st_icon = "● ACT "; st_color = Color::Green; break;
            case 1: st_icon = "▲ SOON"; st_color = Color::Yellow; break;
            default: st_icon = "✗ END "; st_color = Color::GrayDark; break;
        }

        std::string cat = ui::trunc(event_category_str(e.category), 14);
        std::string time_s = v.remain > 0 ? ui::fmt_dur(v.remain) : "-";
        std::string score_s = e.ranking.score > 0 ? ui::fmt_num((int64_t)e.ranking.score) : "-";
        std::string claim_s = e.entry_data.can_claim ? "!" : " ";

        auto row = hbox({
            text(st_icon) | color(st_color) | size(WIDTH, EQUAL, 7),
            text(cat) | size(WIDTH, EQUAL, 16),
            text(time_s) | size(WIDTH, EQUAL, 10),
            text(score_s) | size(WIDTH, EQUAL, 9),
            text(claim_s) | bold | color(Color::Green) | size(WIDTH, EQUAL, 2),
        });
        if (i == es.selected) row = row | inverted | focus;
        rows.push_back(row);
    }

    if (rows.empty()) {
        rows.push_back(text("  No events match filter") | dim);
    }

    auto header = hbox({
        text("State") | bold | size(WIDTH, EQUAL, 7),
        text("Category") | bold | size(WIDTH, EQUAL, 16),
        text("Time") | bold | size(WIDTH, EQUAL, 10),
        text("Score") | bold | size(WIDTH, EQUAL, 9),
        text("!") | bold | size(WIDTH, EQUAL, 2),
    });

    auto list_panel = vbox({
        header,
        separator(),
        vbox(rows) | vscroll_indicator | yframe | flex,
    });

    // --- Detail panel ---
    Elements detail;
    if (count > 0 && es.selected < count) {
        const auto& e = *views[es.selected].e;
        const auto& v = views[es.selected];

        detail.push_back(text(event_category_str(e.category)) | bold | color(Color::Cyan));
        detail.push_back(text(e.config_id) | dim);
        detail.push_back(separator());

        // State
        std::string state_str = v.state == 0 ? "ACTIVE" : (v.state == 1 ? "UPCOMING" : "ENDED");
        Color sc = v.state == 0 ? Color::Green : (v.state == 1 ? Color::Yellow : Color::Red);
        detail.push_back(hbox({text(" State: ") | dim, text(state_str) | bold | color(sc)}));

        // Schedule
        detail.push_back(separator());
        detail.push_back(text("Schedule") | bold);
        if (e.schedule.start > 0)
            detail.push_back(hbox({text("  Start: ") | dim, text(ui::fmt_date(e.schedule.start))}));
        if (e.schedule.end > 0)
            detail.push_back(hbox({text("  End:   ") | dim, text(ui::fmt_date(e.schedule.end))}));
        if (v.remain > 0)
            detail.push_back(hbox({text("  Remaining: ") | dim, text(ui::fmt_dur(v.remain)) | bold | color(sc)}));
        if (e.schedule.round_number > 0)
            detail.push_back(hbox({text("  Round: ") | dim, text(std::to_string(e.schedule.round_number))}));

        // Progress
        detail.push_back(separator());
        detail.push_back(text("Progress") | bold);
        detail.push_back(hbox({text("  Score: ") | dim,
            text(e.ranking.score > 0 ? ui::fmt_num((int64_t)e.ranking.score) : "0") | bold}));
        if (e.ranking.position > 0)
            detail.push_back(hbox({text("  Rank:  ") | dim,
                text("#" + std::to_string(e.ranking.position)) | bold | color(Color::Yellow)}));
        detail.push_back(hbox({text("  Registered: ") | dim,
            text(e.entry_data.is_registered ? "Yes" : "No")}));
        if (e.entry_data.can_claim)
            detail.push_back(text("  CLAIMABLE") | bold | color(Color::Green));

        // Metadata
        detail.push_back(separator());
        detail.push_back(text("Details") | bold);
        detail.push_back(hbox({text("  Type: ") | dim, text(e.event_type)}));
        if (!e.group_name.empty())
            detail.push_back(hbox({text("  Group: ") | dim, text(e.group_name)}));
        std::string pt = e.placement_type == 0 ? "Position" : (e.placement_type == 1 ? "Milestone" : "Relative");
        detail.push_back(hbox({text("  Placement: ") | dim, text(pt)}));
        if (e.metadata.is_cross_server)
            detail.push_back(text("  Cross-server") | color(Color::Magenta));
        int tiers = 0;
        for (auto& seg : e.segments) tiers += (int)seg.rewards.size();
        if (tiers > 0)
            detail.push_back(hbox({text("  Reward tiers: ") | dim, text(std::to_string(tiers)) | bold}));
    } else {
        detail.push_back(text("Select an event") | dim);
    }

    return vbox({
        summary,
        separator(),
        hbox({
            vbox({header, separator(), vbox(rows) | vscroll_indicator | yframe}) | flex,
            separator(),
            vbox(detail) | size(WIDTH, EQUAL, 34) | vscroll_indicator | yframe,
        }) | flex,
        hbox({text(" j/k:navigate  ") | dim, text("f:filter  ") | dim, text("g/G:top/end  ") | dim, text("Ctrl+d/u:page") | dim}),
    });
}

inline bool handle_events_input(const Event& event, EventsTabState& es, int event_count) {
    if (event == Event::ArrowDown) { es.selected = std::min(es.selected + 1, std::max(0, event_count - 1)); return true; }
    if (event == Event::ArrowUp) { es.selected = std::max(0, es.selected - 1); return true; }
    if (event == Event::Character('f') || event == Event::Character('F')) { es.filter = (es.filter + 1) % 4; es.selected = 0; return true; }
    if (event == Event::PageDown) { es.selected = std::min(es.selected + 10, std::max(0, event_count - 1)); return true; }
    if (event == Event::PageUp) { es.selected = std::max(0, es.selected - 10); return true; }
    if (event == Event::Home) { es.selected = 0; return true; }
    if (event == Event::End) { es.selected = std::max(0, event_count - 1); return true; }
    return false;
}

} // namespace stfc
