#pragma once
// ---------------------------------------------------------------------------
// Plan Tab — live action planner output
// ---------------------------------------------------------------------------

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "app/action_planner.h"
#include "tui/ui_common.h"

namespace stfc {

using namespace ftxui;

struct PlanTabState {
    int selected = 0;
};

inline std::string plan_resource_summary(const std::vector<PlannedResource>& resources) {
    if (resources.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < resources.size() && i < 2; ++i) {
        if (i > 0) out += ", ";
        const auto& r = resources[i];
        int64_t amount = r.missing > 0 ? r.missing : r.amount;
        out += ui::trunc(r.name, 14) + " " + ui::fmt_num(amount);
    }
    if (resources.size() > 2) out += " +" + std::to_string(resources.size() - 2);
    return out;
}

inline std::string plan_cell(const std::string& value, int width) {
    return ui::pad(ui::trunc(value, std::max(1, width - 1)), width);
}

inline Element plan_fill_cell(const std::string& value) {
    return text(ui::trunc(value, 160)) | xflex;
}

inline void plan_add_field(Elements& out, const std::string& label, const std::string& value) {
    out.push_back(hbox({
        text(" " + ui::pad(label + ":", 14)) | bold | color(Color::Cyan),
        paragraph(value.empty() ? "-" : value) | flex,
    }));
}

inline std::string plan_resource_detail(const std::vector<PlannedResource>& resources) {
    if (resources.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < resources.size(); ++i) {
        if (i > 0) out += "; ";
        const auto& r = resources[i];
        out += r.name + ": ";
        if (r.missing > 0) {
            out += "need " + ui::fmt_num(r.missing) + " more (" +
                   ui::fmt_num(r.owned) + "/" + ui::fmt_num(r.amount) + ")";
        } else {
            out += ui::fmt_num(r.amount);
            if (r.owned > 0) out += " owned " + ui::fmt_num(r.owned);
        }
    }
    return out;
}

inline std::string plan_blocker_detail(const std::vector<PlanBlocker>& blockers) {
    if (blockers.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < blockers.size(); ++i) {
        if (i > 0) out += "; ";
        const auto& b = blockers[i];
        out += b.name + " " + std::to_string(b.current_level) + "/" +
               std::to_string(b.required_level);
    }
    return out;
}

inline std::string plan_requirement_detail(const std::vector<PlanBlocker>& requirements) {
    if (requirements.empty()) return "None listed";
    std::string out;
    for (size_t i = 0; i < requirements.size(); ++i) {
        if (i > 0) out += "; ";
        const auto& r = requirements[i];
        out += r.name + " " + std::to_string(r.current_level) + "/" +
               std::to_string(r.required_level) + (r.met ? " met" : " missing");
    }
    return out;
}

inline std::string plan_ready_text(const ResearchCandidate& r) {
    if (r.funding_unknown) return "verify";
    if (r.can_start_now) return "ready";
    if (r.resources_available && r.prerequisites_met) return "queue";
    if (!r.prerequisites_met) return "blocked";
    return "save";
}

inline std::string plan_duration_short(int seconds) {
    if (seconds <= 0) return "-";
    int d = seconds / 86400;
    int h = (seconds % 86400) / 3600;
    int m = (seconds % 3600) / 60;
    if (d > 0) return std::to_string(d) + "d";
    if (h > 0) return std::to_string(h) + "h" + std::to_string(m) + "m";
    if (m > 0) return std::to_string(m) + "m";
    return std::to_string(seconds) + "s";
}

inline Color plan_ready_color(const ResearchCandidate& r) {
    if (r.funding_unknown) return Color::Yellow;
    if (r.can_start_now) return Color::Green;
    if (r.resources_available && r.prerequisites_met) return Color::Yellow;
    if (!r.prerequisites_met) return Color::Red;
    return Color::Yellow;
}

inline std::string plan_tree_short(const ResearchCandidate& r) {
    std::string tree = r.research_tree_name;
    const std::string tree_suffix = " tree";
    const std::string screen_suffix = " screen";
    if (tree.size() > tree_suffix.size() &&
        tree.compare(tree.size() - tree_suffix.size(), tree_suffix.size(), tree_suffix) == 0) {
        tree.erase(tree.size() - tree_suffix.size());
    }
    if (tree.size() > screen_suffix.size() &&
        tree.compare(tree.size() - screen_suffix.size(), screen_suffix.size(), screen_suffix) == 0) {
        tree.erase(tree.size() - screen_suffix.size());
    }
    return tree.empty() ? "-" : tree;
}

inline std::string plan_place_short(const ResearchCandidate& r) {
    std::string place = plan_tree_short(r);
    if (r.row > 0 && r.column > 0) {
        if (place != "-") place += " ";
        place += "R" + std::to_string(r.row) + " C" + std::to_string(r.column);
    }
    if (place == "-" && !r.location.empty()) place = r.location;
    return place;
}

inline std::string plan_funding_summary(const ResearchCandidate& r) {
    if (r.funding_unknown) return "verify cost";
    if (!r.missing_resources.empty()) return "need " + plan_resource_summary(r.missing_resources);
    if (!r.costs.empty()) return plan_resource_summary(r.costs);
    return "-";
}

inline std::string plan_funding_detail(const ResearchCandidate& r) {
    if (r.funding_unknown) {
        std::string funding = "No resource cost is listed in cached data; verify the in-game funding requirement before starting.";
        if (r.hard_currency_cost > 0) {
            funding += " Hard currency cost: " + ui::fmt_num(r.hard_currency_cost) + ".";
        }
        return funding;
    }

    std::string funding = "Cost: " + plan_resource_detail(r.costs);
    if (!r.missing_resources.empty()) {
        funding += "; Missing: " + plan_resource_detail(r.missing_resources);
    } else if (r.can_start_now || r.resources_available) {
        funding += "; resources available.";
    }
    return funding;
}

inline Element plan_priority_header() {
    return hbox({
        text(plan_cell("#", 3)) | bold | dim | size(WIDTH, EQUAL, 3),
        text(plan_cell("State", 7)) | bold | dim | size(WIDTH, EQUAL, 7),
        text(plan_cell("Research", 22)) | bold | dim | size(WIDTH, EQUAL, 22),
        text(plan_cell("Lvl", 6)) | bold | dim | size(WIDTH, EQUAL, 6),
        text(plan_cell("Where", 17)) | bold | dim | size(WIDTH, EQUAL, 17),
        text(plan_cell("Need/Cost", 17)) | bold | dim | size(WIDTH, EQUAL, 17),
        text(plan_cell("Time", 6)) | bold | dim | size(WIDTH, EQUAL, 6),
        plan_fill_cell("Why") | bold | dim,
    });
}

inline Element render_plan_tab(const ActionPlan& plan, PlanTabState& ps) {
    int research_count = static_cast<int>(plan.top_research.size());
    if (research_count > 0) {
        ps.selected = std::clamp(ps.selected, 0, research_count - 1);
    } else {
        ps.selected = 0;
    }

    Elements research_rows;
    research_rows.push_back(plan_priority_header());
    for (int i = 0; i < research_count; ++i) {
        const auto& r = plan.top_research[static_cast<size_t>(i)];
        auto row = hbox({
            text(plan_cell(std::to_string(i + 1), 3)) | dim | size(WIDTH, EQUAL, 3),
            text(plan_cell(plan_ready_text(r), 7)) | bold | color(plan_ready_color(r)) | size(WIDTH, EQUAL, 7),
            text(plan_cell(r.name, 22)) | bold | size(WIDTH, EQUAL, 22),
            text(plan_cell(std::to_string(r.current_level) + ">" + std::to_string(r.next_level), 6)) | size(WIDTH, EQUAL, 6),
            text(plan_cell(plan_place_short(r), 17)) | size(WIDTH, EQUAL, 17),
            text(plan_cell(plan_funding_summary(r), 17)) | dim | size(WIDTH, EQUAL, 17),
            text(plan_cell(plan_duration_short(r.research_time_seconds), 6)) | dim | size(WIDTH, EQUAL, 6),
            plan_fill_cell(r.reason) | dim,
        });
        if (i == ps.selected) row = row | inverted | focus;
        research_rows.push_back(row);
    }
    if (research_rows.size() == 1) {
        research_rows.push_back(text("  Generate a plan with p") | dim);
    }

    Elements detail;
    if (research_count > 0) {
        const auto& r = plan.top_research[static_cast<size_t>(ps.selected)];
        detail.push_back(hbox({
            text("#" + std::to_string(ps.selected + 1) + " ") | dim,
            paragraph(r.name + " L" + std::to_string(r.next_level)) | bold | color(Color::Cyan) | flex,
        }));
        detail.push_back(hbox({
            text(" Current ") | dim,
            text(std::to_string(r.current_level)) | bold,
            text(" -> ") | dim,
            text(std::to_string(r.next_level)) | bold,
            text("   State ") | dim,
            text(plan_ready_text(r)) | bold | color(plan_ready_color(r)),
            text("   Time ") | dim,
            text(plan_duration_short(r.research_time_seconds)) | bold,
        }));

        Elements detail_left;
        Elements detail_right;
        plan_add_field(detail_left, "Where", r.location.empty() ? plan_place_short(r) : r.location);
        plan_add_field(detail_left, "Funding", plan_funding_detail(r));
        plan_add_field(detail_left, "Requirements", plan_requirement_detail(r.requirements));
        if (!r.blockers.empty()) {
            plan_add_field(detail_left, "Blocked By", plan_blocker_detail(r.blockers));
        }
        plan_add_field(detail_right, "Benefit", r.description);
        plan_add_field(detail_right, "Why", r.reason);
        detail.push_back(hbox({
            vbox(std::move(detail_left)) | flex,
            separator(),
            vbox(std::move(detail_right)) | flex,
        }) | flex);
    } else {
        detail.push_back(text("Select a research item") | dim);
    }

    std::string generated = plan.generated_at > 0 ? ui::fmt_date(plan.generated_at) : "never";
    std::string sync_age = plan.sync_age_seconds >= 0 ? ui::fmt_dur(plan.sync_age_seconds) + " old" : "no sync";
    auto summary = hbox({
        text(" Plan ") | bold | color(Color::Cyan),
        text(generated) | bold,
        text(" sync ") | dim,
        text(sync_age) | color(plan.sync_age_seconds >= 0 && plan.sync_age_seconds < 6 * 3600 ? Color::Green : Color::Yellow),
        text(" focus ") | dim,
        text(plan.focus) | bold,
        text(" ready ") | dim,
        text(std::to_string(plan.do_now.size())) | bold | color(plan.do_now.empty() ? Color::Yellow : Color::Green),
        text(" save ") | dim,
        text(std::to_string(plan.save_for.size())) | bold | color(plan.save_for.empty() ? Color::GrayDark : Color::Yellow),
        text(" warn ") | dim,
        text(std::to_string(plan.warnings.size())) | bold | color(plan.warnings.empty() ? Color::GrayDark : Color::Yellow),
        filler(),
        text("j/k p/P") | dim,
    });

    return vbox({
        summary,
        separator(),
        ui::panel("Research Priority", research_rows) | size(HEIGHT, LESS_THAN, 10) | vscroll_indicator | yframe,
        separator(),
        ui::panel("Selected Research", detail) | flex | vscroll_indicator | yframe,
    }) | flex;
}

inline bool handle_plan_input(const Event& event, PlanTabState& ps, int research_count) {
    if (event == Event::ArrowDown) {
        ps.selected = std::min(ps.selected + 1, std::max(0, research_count - 1));
        return true;
    }
    if (event == Event::ArrowUp) {
        ps.selected = std::max(0, ps.selected - 1);
        return true;
    }
    if (event == Event::PageDown) {
        ps.selected = std::min(ps.selected + 5, std::max(0, research_count - 1));
        return true;
    }
    if (event == Event::PageUp) {
        ps.selected = std::max(0, ps.selected - 5);
        return true;
    }
    if (event == Event::Home) {
        ps.selected = 0;
        return true;
    }
    if (event == Event::End) {
        ps.selected = std::max(0, research_count - 1);
        return true;
    }
    return false;
}

} // namespace stfc
