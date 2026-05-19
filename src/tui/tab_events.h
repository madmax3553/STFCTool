#pragma once
// ---------------------------------------------------------------------------
// Events Tab — btm/Charm style with list + detail split
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
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

inline std::string event_category_label(EventCategory cat) {
    std::string label = event_category_str(cat);
    if (label == "Unknown") {
        label = "Category " + std::to_string(static_cast<int>(cat));
    }
    return label;
}

inline std::string event_category_compact(EventCategory cat) {
    switch (cat) {
        case EventCategory::Standard: return "Solo";
        case EventCategory::DailyGoals: return "Daily Goals";
        case EventCategory::DailyMilestone: return "Daily MS";
        case EventCategory::SpecialEvent: return "Special";
        case EventCategory::LoopMuseumTask: return "Museum Task";
        case EventCategory::MetaEventObjective: return "Meta Obj";
        case EventCategory::ProgressionReward: return "Progression";
        default: return event_category_label(cat);
    }
}

inline bool event_is_background(const PlayerEvent& e) {
    return e.category == EventCategory::FieldTraining ||
           e.category == EventCategory::FtCategory ||
           e.category == EventCategory::Cutscenes ||
           e.category == EventCategory::MinigameCategory ||
           e.category == EventCategory::MinigameStage ||
           e.category == EventCategory::LoopMuseum ||
           e.category == EventCategory::LoopMuseumTask;
}

inline std::string event_scope_token(const PlayerEvent& e) {
    return e.source.find("alliance") != std::string::npos ? "alliance" : "solo";
}

inline std::string event_scope_label(const PlayerEvent& e) {
    return event_scope_token(e) == "alliance" ? "Alliance" : "Solo";
}

inline std::string event_term_label(const PlayerEvent& e) {
    if (!e.schedule.term.empty() && e.schedule.term != "None") {
        std::string term = e.schedule.term;
        term[0] = static_cast<char>(std::toupper(term[0]));
        return term;
    }

    int64_t duration = e.schedule.end - e.schedule.start;
    if (duration >= 20LL * 24 * 3600) return "Multiweek";
    if (duration >= 6LL * 24 * 3600) return "Weekly";
    if (duration >= 20LL * 3600) return "Daily";
    return "";
}

inline std::string event_priority_code(const PlayerEvent& e) {
    if (e.metadata.priority <= 0) return "";
    int64_t priority = e.metadata.priority;
    if (priority >= 100000) {
        std::ostringstream out;
        out << std::setw(3) << std::setfill('0') << (priority % 1000);
        return out.str();
    }
    if (priority >= 30000) return std::to_string(priority % 1000);
    return std::to_string(priority);
}

inline std::string event_scoring_hint(const PlayerEvent& e) {
    if (e.metadata.scoring_info.empty()) return "";
    std::string out = "i";
    int added = 0;
    for (const auto& info : e.metadata.scoring_info) {
        if (info.icon.empty()) continue;
        if (out.find(info.icon) != std::string::npos) continue;
        if (added > 0) out += "/";
        out += info.icon;
        added++;
        if (added == 2) break;
    }
    return added > 0 ? out : "";
}

inline std::string event_label_override_key(const PlayerEvent& e) {
    return "category:" + std::to_string(static_cast<int>(e.category)) +
           "|priority:" + std::to_string(e.metadata.priority) +
           "|scope:" + event_scope_token(e);
}

inline std::string event_label_override(const PlayerEvent& e, const GameData& gd) {
    const std::vector<std::string> keys = {
        "config_id:" + e.config_id,
        event_label_override_key(e),
        "priority:" + std::to_string(e.metadata.priority) + "|scope:" + event_scope_token(e),
        "priority:" + std::to_string(e.metadata.priority),
        "category:" + std::to_string(static_cast<int>(e.category)),
    };
    for (const auto& key : keys) {
        auto it = gd.event_label_overrides.find(key);
        if (it != gd.event_label_overrides.end() && !it->second.empty()) return it->second;
    }
    return "";
}

inline std::string event_display_label(const PlayerEvent& e, const GameData& gd) {
    if (!e.group_name.empty()) return e.group_name;

    std::string override = event_label_override(e, gd);
    if (!override.empty()) return override;

    std::string label = event_scope_label(e);
    std::string term = event_term_label(e);
    if (!term.empty()) label += " " + term;
    label += " " + event_category_compact(e.category);

    std::string scoring = event_scoring_hint(e);
    if (!scoring.empty()) label += " " + scoring;

    std::string code = event_priority_code(e);
    if (!code.empty()) label += " #" + code;
    return label;
}

inline bool event_uses_fallback_label(const PlayerEvent& e, const GameData& gd) {
    return e.group_name.empty() && event_label_override(e, gd).empty();
}

inline bool event_has_chest_rewards(const PlayerEvent& e) {
    for (const auto& seg : e.segments) {
        for (const auto& reward : seg.rewards) {
            if (reward.type.rfind("chest_", 0) == 0) return true;
        }
    }
    return false;
}

inline std::string event_reward_label(const EventReward& reward) {
    if (reward.type.rfind("chest_", 0) == 0) {
        return "Chest " + reward.type.substr(6);
    }
    if (reward.type.rfind("resource_", 0) == 0) {
        return "Resource " + reward.type.substr(9);
    }
    return reward.type.empty() ? "Reward" : reward.type;
}

inline std::string event_reward_threshold(const EventReward& reward) {
    if (reward.position.empty()) return "-";
    if (reward.position.size() == 1 || reward.position.front() == reward.position.back()) {
        return ui::fmt_num(reward.position.front());
    }
    return ui::fmt_num(reward.position.front()) + "-" + ui::fmt_num(reward.position.back());
}

inline std::string event_scoring_summary(const PlayerEvent& e) {
    if (e.metadata.scoring_info.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < e.metadata.scoring_info.size(); ++i) {
        if (i > 0) out += ", ";
        const auto& info = e.metadata.scoring_info[i];
        out += "Objective " + std::to_string(info.id);
        if (!info.icon.empty()) out += " icon " + info.icon;
    }
    return out;
}

inline Element render_events_tab(const PlayerData& pd, const GameData& gd, EventsTabState& es) {
    int64_t now = ui::now_epoch();

    // Classify, filter, and group repeated platform-event rows.
    struct EVGroup {
        const PlayerEvent* e = nullptr;
        int state = 2;   // 0=active, 1=upcoming, 2=ended
        int remain = 0;
        bool background = false;
        int count = 0;
        int claimable = 0;
        double score = 0.0;
    };
    std::map<std::string, EVGroup> grouped;

    int n_active = 0, n_upcoming = 0, n_claim = 0, n_background = 0;

    for (const auto& e : pd.events) {
        int st = 2, rem = 0;
        bool background = event_is_background(e);
        if (e.schedule.start > 0 && e.schedule.end > 0) {
            if (now >= e.schedule.start && now < e.schedule.end) {
                st = 0; rem = (int)(e.schedule.end - now);
                if (!background) n_active++;
            } else if (now < e.schedule.start) {
                st = 1; rem = (int)(e.schedule.start - now);
                if (!background) n_upcoming++;
            }
        }
        bool manual_claim = e.has_manual_claimable_reward(now);
        if (background) n_background++;
        if (manual_claim) n_claim++;

        // Filter
        if (es.filter == 1 && (st != 0 || background)) continue;
        if (es.filter == 2 && !manual_claim) continue;
        if (es.filter == 3 && (st != 1 || background)) continue;

        std::string key = std::to_string(static_cast<int>(e.category)) + "|" +
                          e.source + "|" +
                          std::to_string(st) + "|" +
                          std::to_string(e.schedule.start) + "|" +
                          std::to_string(e.schedule.end) + "|" +
                          std::to_string(background ? 1 : 0);
        auto& group = grouped[key];
        if (group.e == nullptr) {
            group.e = &e;
            group.state = st;
            group.remain = rem;
            group.background = background;
        }
        group.count++;
        if (manual_claim) group.claimable++;
        group.score = std::max(group.score, e.ranking.score);
    }

    std::vector<EVGroup> views;
    for (auto& [_, group] : grouped) views.push_back(group);

    // Sort: actionable events first, then background/evergreen records.
    std::sort(views.begin(), views.end(), [](const EVGroup& a, const EVGroup& b) {
        if (a.background != b.background) return !a.background;
        if (a.state != b.state) return a.state < b.state;
        if (a.state < 2) return a.remain < b.remain;
        return a.e->schedule.end > b.e->schedule.end;
    });

    int count = (int)views.size();
    if (count > 0) es.selected = std::clamp(es.selected, 0, count - 1);

    // --- Summary bar ---
    static const char* filt_labels[] = {"All", "Active", "Claimable", "Upcoming"};
    const auto& event_status = gd.event_data_status;
    std::string data_state = "unchecked";
    if (event_status.checked) {
        if (event_status.cache_age_hours < 0) {
            data_state = "missing";
        } else if (event_status.stale) {
            data_state = "stale-cache";
        } else {
            data_state = "fresh-cache";
        }
    }
    Color data_color = !event_status.checked ? Color::GrayDark :
        (event_status.stale ? Color::Yellow : Color::Green);
    std::string age_s = event_status.cache_age_hours >= 0
        ? std::to_string(event_status.cache_age_hours) + "h"
        : "-";
    auto summary = hbox({
        text(" ") ,
        text(std::to_string(pd.events.size())) | bold | color(Color::Cyan),
        text(" events  "),
        text("Active:") | dim, text(std::to_string(n_active)) | bold | color(n_active > 0 ? Color::Green : Color::GrayDark),
        text("  Upcoming:") | dim, text(std::to_string(n_upcoming)) | bold | color(n_upcoming > 0 ? Color::Yellow : Color::GrayDark),
        text("  Claimable:") | dim, text(std::to_string(n_claim)) | bold | color(n_claim > 0 ? Color::Green : Color::GrayDark),
        text("  Background:") | dim, text(std::to_string(n_background)) | bold | color(Color::GrayDark),
        text("  Data:") | dim, text(data_state) | bold | color(data_color),
        text("(" + age_s + ")") | dim,
        text("  *label") | color(event_status.fallback_names ? Color::Yellow : Color::GrayDark),
        text(" !rewards") | color(event_status.reward_definitions_available ? Color::GrayDark : Color::Yellow),
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
        if (v.background) {
            st_icon = "· BG  ";
            st_color = Color::GrayDark;
        }

        std::string cat = event_display_label(e, gd);
        if (v.count > 1) cat += " x" + std::to_string(v.count);
        cat = ui::trunc(cat, 18);
        std::string time_s = v.remain > 0 ? ui::fmt_dur(v.remain) : "-";
        std::string score_s = v.score > 0 ? ui::fmt_num((int64_t)v.score) : "-";
        std::string flags_s;
        if (v.claimable > 0) flags_s += "C" + std::to_string(v.claimable);
        if (event_status.fallback_names && event_uses_fallback_label(e, gd)) flags_s += "*";
        if (!event_status.reward_definitions_available && event_has_chest_rewards(e)) {
            flags_s += "!";
        }
        if (flags_s.empty()) flags_s = " ";

        auto row = hbox({
            text(st_icon) | color(st_color) | size(WIDTH, EQUAL, 7),
            text(cat) | size(WIDTH, EQUAL, 20),
            text(time_s) | size(WIDTH, EQUAL, 10),
            text(score_s) | size(WIDTH, EQUAL, 9),
            text(flags_s) | bold | color(flags_s.find('!') != std::string::npos ? Color::Yellow : Color::Green) | size(WIDTH, EQUAL, 7),
        });
        if (i == es.selected) row = row | inverted | focus;
        rows.push_back(row);
    }

    if (rows.empty()) {
        rows.push_back(text("  No events match filter") | dim);
    }

    auto header = hbox({
        text("State") | bold | size(WIDTH, EQUAL, 7),
        text("Event") | bold | size(WIDTH, EQUAL, 20),
        text("Time") | bold | size(WIDTH, EQUAL, 10),
        text("Score") | bold | size(WIDTH, EQUAL, 9),
        text("Flags") | bold | size(WIDTH, EQUAL, 7),
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

        detail.push_back(text(event_display_label(e, gd)) | bold | color(Color::Cyan));
        detail.push_back(text(e.config_id) | dim);
        if (event_status.fallback_names && event_uses_fallback_label(e, gd)) {
            detail.push_back(paragraph("* Derived label; live sync did not include a localized event title.") | color(Color::Yellow));
        }
        if (!event_status.reward_definitions_available && event_has_chest_rewards(e)) {
            detail.push_back(paragraph("! Chest contents unresolved; showing reward IDs until a chest definition cache is available.") | color(Color::Yellow));
        }
        if (event_status.stale) {
            detail.push_back(paragraph("! Event helper cache is stale or unavailable; recommendations should treat payouts as fallback data.") | color(Color::Yellow));
        }
        detail.push_back(separator());

        // State
        std::string state_str = v.background ? "BACKGROUND" :
            (v.state == 0 ? "ACTIVE" : (v.state == 1 ? "UPCOMING" : "ENDED"));
        Color sc = v.background ? Color::GrayDark :
            (v.state == 0 ? Color::Green : (v.state == 1 ? Color::Yellow : Color::Red));
        detail.push_back(hbox({text(" State: ") | dim, text(state_str) | bold | color(sc)}));
        if (v.count > 1) {
            detail.push_back(hbox({text(" Grouped: ") | dim, text(std::to_string(v.count) + " similar records") | bold}));
        }

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
        detail.push_back(hbox({text("  Scoring: ") | dim, paragraph(event_scoring_summary(e)) | flex}));
        detail.push_back(hbox({text("  Score: ") | dim,
            text(v.score > 0 ? ui::fmt_num((int64_t)v.score) : "0") | bold}));
        if (e.ranking.position > 0)
            detail.push_back(hbox({text("  Rank:  ") | dim,
                text("#" + std::to_string(e.ranking.position)) | bold | color(Color::Yellow)}));
        detail.push_back(hbox({text("  Registered: ") | dim,
            text(e.entry_data.is_registered ? "Yes" : "No")}));
        if (v.claimable > 0)
            detail.push_back(text("  CLAIMABLE x" + std::to_string(v.claimable)) | bold | color(Color::Green));

        // Metadata
        detail.push_back(separator());
        detail.push_back(text("Details") | bold);
        detail.push_back(hbox({text("  Category: ") | dim, text(event_category_label(e.category))}));
        detail.push_back(hbox({text("  Source: ") | dim, text(e.source.empty() ? "-" : e.source)}));
        detail.push_back(hbox({text("  Type: ") | dim, text(e.event_type)}));
        detail.push_back(hbox({text("  Label key: ") | dim, paragraph(event_label_override_key(e)) | flex}));
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
        int shown_rewards = 0;
        for (auto& seg : e.segments) {
            for (auto& reward : seg.rewards) {
                if (shown_rewards >= 5) break;
                detail.push_back(hbox({
                    text("  @") | dim,
                    text(event_reward_threshold(reward)) | bold,
                    text(" -> ") | dim,
                    paragraph(std::to_string(reward.amount) + "x " + event_reward_label(reward)) | flex,
                }));
                shown_rewards++;
            }
            if (shown_rewards >= 5) break;
        }
        if (tiers > shown_rewards) {
            detail.push_back(text("  +" + std::to_string(tiers - shown_rewards) + " more reward tiers") | dim);
        }

        detail.push_back(separator());
        detail.push_back(text("Data Cache") | bold);
        detail.push_back(hbox({text("  Source: ") | dim, text(event_status.source.empty() ? "-" : event_status.source)}));
        detail.push_back(hbox({text("  State:  ") | dim,
            text(data_state + " age " + age_s) | bold | color(data_color)}));
        if (event_status.consumable_count > 0) {
            detail.push_back(hbox({text("  Consumables: ") | dim,
                text(std::to_string(event_status.consumable_count))}));
        }
        if (!event_status.warning.empty()) {
            detail.push_back(paragraph("  " + event_status.warning) | dim);
        }
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
