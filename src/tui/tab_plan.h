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
#include "app/strategy_recommender.h"
#include "tui/ui_common.h"

namespace stfc {

using namespace ftxui;

struct PlanTabState {
    int selected = 0;
    int pane = 2;
    int avalanche_selected = 0;
    int avoid_selected = 0;
    int avalanche_offset = 0;
    int avoid_offset = 0;
    int research_offset = 0;
};

inline constexpr int kPlanPaneAvalanche = 0;
inline constexpr int kPlanPaneAvoid = 1;
inline constexpr int kPlanPaneResearch = 2;
inline constexpr int kPlanPaneCount = 3;
inline constexpr int kPlanAvalanchePageSize = 7;
inline constexpr int kPlanAvoidPageSize = 3;
inline constexpr int kPlanResearchPageSize = 5;
inline constexpr int kPlanTableMinWidth = 160;

inline int plan_max_offset(int count, int page_size) {
    return std::max(0, count - page_size);
}

inline int plan_clamped_offset(int offset, int count, int page_size) {
    return std::clamp(offset, 0, plan_max_offset(count, page_size));
}

inline void plan_keep_item_visible(int& selected, int& offset, int count, int page_size) {
    if (count <= 0) {
        selected = 0;
        offset = 0;
        return;
    }

    selected = std::clamp(selected, 0, count - 1);
    if (selected < offset) {
        offset = selected;
    }
    if (selected >= offset + page_size) {
        offset = selected - page_size + 1;
    }
    offset = plan_clamped_offset(offset, count, page_size);
}

inline void plan_keep_research_visible(PlanTabState& ps, int research_count) {
    plan_keep_item_visible(ps.selected, ps.research_offset, research_count,
                           kPlanResearchPageSize);
}

inline void plan_clamp_state(PlanTabState& ps, int research_count,
                             int avalanche_count, int avoid_count) {
    ps.pane = std::clamp(ps.pane, 0, kPlanPaneCount - 1);
    plan_keep_item_visible(ps.avalanche_selected, ps.avalanche_offset, avalanche_count,
                           kPlanAvalanchePageSize);
    plan_keep_item_visible(ps.avoid_selected, ps.avoid_offset, avoid_count,
                           kPlanAvoidPageSize);
    plan_keep_research_visible(ps, research_count);
}

inline std::string plan_pane_name(int pane) {
    switch (pane) {
    case kPlanPaneAvalanche: return "Avalanche";
    case kPlanPaneAvoid: return "Avoid";
    case kPlanPaneResearch: return "Research";
    default: return "Plan";
    }
}

inline std::string plan_panel_title(const std::string& title, int pane,
                                    const PlanTabState& ps) {
    return ps.pane == pane ? "> " + title : title;
}

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
    return text(ui::trunc(value, 240)) | xflex;
}

inline Element plan_wide_table(Elements rows) {
    return vbox(std::move(rows)) | size(WIDTH, GREATER_THAN, kPlanTableMinWidth);
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
        out += b.name + " " +
               (b.current_level < 0 ? "unknown" : std::to_string(b.current_level)) +
               "/" + std::to_string(b.required_level);
    }
    return out;
}

inline std::string plan_requirement_detail(const std::vector<PlanBlocker>& requirements) {
    if (requirements.empty()) return "None listed";
    std::string out;
    for (size_t i = 0; i < requirements.size(); ++i) {
        if (i > 0) out += "; ";
        const auto& r = requirements[i];
        out += r.name + " " +
               (r.current_level < 0 ? "unknown" : std::to_string(r.current_level)) +
               "/" + std::to_string(r.required_level) +
               (r.met ? " met" : (r.current_level < 0 ? " unknown" : " missing"));
    }
    return out;
}

inline std::string plan_ready_text(const ResearchCandidate& r) {
    if (r.resource_balances_unknown) return "verify";
    if (r.funding_unknown) return "verify";
    if (r.can_start_now) return "ready";
    if (r.resources_available && r.prerequisites_met) return "queue";
    if (!r.prerequisites_met) return "blocked";
    return "save";
}

inline std::string plan_duration_short(int64_t seconds) {
    if (seconds <= 0) return "-";
    int64_t d = seconds / 86400;
    int64_t h = (seconds % 86400) / 3600;
    int64_t m = (seconds % 3600) / 60;
    if (d > 0) return std::to_string(d) + "d";
    if (h > 0) return std::to_string(h) + "h" + std::to_string(m) + "m";
    if (m > 0) return std::to_string(m) + "m";
    return std::to_string(seconds) + "s";
}

inline std::string plan_speedup_detail(const SpeedupCoverage& s) {
    if (!s.required) return "No timer listed.";
    std::string detail = "Timer " + plan_duration_short(s.required_seconds) + "; ";
    if (!s.evaluated) {
        detail += "coverage unverified";
        if (s.available_seconds > 0) {
            detail += " (" + plan_duration_short(s.available_seconds) + " seen)";
        }
        if (!s.warning.empty()) detail += ": " + s.warning;
        return detail;
    }
    detail += plan_duration_short(s.available_seconds) + " available";
    if (s.enough) {
        detail += "; covered.";
    } else {
        detail += "; short " + plan_duration_short(s.shortage_seconds) + ".";
    }
    return detail;
}

inline Color plan_ready_color(const ResearchCandidate& r) {
    if (r.resource_balances_unknown) return Color::Yellow;
    if (r.funding_unknown) return Color::Yellow;
    if (r.can_start_now) return Color::Green;
    if (r.resources_available && r.prerequisites_met) return Color::Yellow;
    if (!r.prerequisites_met) return Color::Red;
    return Color::Yellow;
}

inline std::string plan_bucket_text(const ResearchCandidate& r) {
    if (r.research_bucket == "prime") {
        if (r.funding_class == "purchase_or_event") return "Prime$";
        if (r.funding_class == "level_locked") return "PrimeL";
        return "Prime";
    }
    if (r.research_bucket == "specialty_ship") return "Spec";
    return "Daily";
}

inline Color plan_bucket_color(const ResearchCandidate& r) {
    if (r.research_bucket == "prime") return r.tracking_only ? Color::Yellow : Color::Magenta;
    if (r.research_bucket == "specialty_ship") return Color::Cyan;
    return Color::Green;
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
    if (r.resource_balances_unknown) return "sync res";
    if (r.funding_unknown) return "verify cost";
    if (!r.missing_resources.empty()) return "need " + plan_resource_summary(r.missing_resources);
    if (!r.costs.empty()) return plan_resource_summary(r.costs);
    return "-";
}

inline std::string plan_funding_detail(const ResearchCandidate& r) {
    if (r.resource_balances_unknown) {
        return "Core resource balances are missing from sync; verify the in-game cost and current balance before reserving resources.";
    }
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
        text(plan_cell("#", 4)) | bold | dim | size(WIDTH, EQUAL, 4),
        text(plan_cell("State", 9)) | bold | dim | size(WIDTH, EQUAL, 9),
        text(plan_cell("Cat", 9)) | bold | dim | size(WIDTH, EQUAL, 9),
        text(plan_cell("Research", 30)) | bold | dim | size(WIDTH, EQUAL, 30),
        text(plan_cell("Lvl", 7)) | bold | dim | size(WIDTH, EQUAL, 7),
        text(plan_cell("Where", 18)) | bold | dim | size(WIDTH, EQUAL, 18),
        plan_fill_cell("Need/Cost") | bold | dim,
        text(plan_cell("Time", 6)) | bold | dim | size(WIDTH, EQUAL, 6),
    });
}

inline std::string strategy_state_text(const StrategyRecommendation& rec) {
    if (!rec.can_do_now) return rec.lane;
    return rec.concurrent ? rec.lane + "+" : rec.lane;
}

inline std::string strategy_decision_text(const StrategyRecommendation& rec) {
    if (!rec.decision.empty()) return rec.decision;
    return rec.lane == "avoid" ? "no" : (rec.can_do_now ? "yes" : "no");
}

inline Color strategy_lane_color(const StrategyRecommendation& rec) {
    if (rec.lane == "active") return Color::Green;
    if (rec.lane == "queue") return Color::Cyan;
    if (rec.lane == "passive") return Color::Blue;
    if (rec.lane == "claim") return Color::Yellow;
    if (rec.lane == "avoid") return Color::Red;
    return Color::White;
}

inline Color strategy_decision_color(const StrategyRecommendation& rec) {
    const auto decision = strategy_decision_text(rec);
    if (decision == "yes") return Color::Green;
    if (decision == "fallback") return Color::Yellow;
    if (decision == "no") return Color::Red;
    return Color::White;
}

inline Element strategy_header() {
    return hbox({
        text(plan_cell("#", 4)) | bold | dim | size(WIDTH, EQUAL, 4),
        text(plan_cell("Do", 10)) | bold | dim | size(WIDTH, EQUAL, 10),
        text(plan_cell("Lane", 12)) | bold | dim | size(WIDTH, EQUAL, 12),
        text(plan_cell("Score", 8)) | bold | dim | size(WIDTH, EQUAL, 8),
        text(plan_cell("Time", 7)) | bold | dim | size(WIDTH, EQUAL, 7),
        plan_fill_cell("Action") | bold | dim,
    });
}

inline Element strategy_row(const StrategyRecommendation& rec, bool selected, bool focused) {
    auto row = hbox({
        text(plan_cell(std::to_string(rec.priority), 4)) | dim | size(WIDTH, EQUAL, 4),
        text(plan_cell(strategy_decision_text(rec), 10)) | bold | color(strategy_decision_color(rec)) | size(WIDTH, EQUAL, 10),
        text(plan_cell(strategy_state_text(rec), 12)) | bold | color(strategy_lane_color(rec)) | size(WIDTH, EQUAL, 12),
        text(plan_cell(std::to_string(rec.score), 8)) | size(WIDTH, EQUAL, 8),
        text(plan_cell(rec.estimated_minutes > 0 ? std::to_string(rec.estimated_minutes) + "m" : "-", 7)) | dim | size(WIDTH, EQUAL, 7),
        plan_fill_cell(rec.action.empty() ? rec.title : rec.action) | bold,
    });
    if (selected && focused) {
        row = row | inverted | focus;
    }
    return row;
}

inline Element plan_window_footer(int start, int end, int count, const std::string& hint) {
    return hbox({
        text("  Showing " + std::to_string(start + 1) + "-" + std::to_string(end) +
             " of " + std::to_string(count)) | dim,
        filler(),
        text(hint) | dim,
    });
}

inline Elements strategy_rows(const StrategyPlan& strategy, bool avoid,
                              int offset, int page_size,
                              int selected, bool focused) {
    Elements rows;
    rows.push_back(strategy_header());
    const auto& list = avoid ? strategy.avoid : strategy.do_now;
    int count = static_cast<int>(list.size());
    int start = plan_clamped_offset(offset, count, page_size);
    int end = std::min(count, start + page_size);
    for (int i = start; i < end; ++i) {
        rows.push_back(strategy_row(list[i], i == selected, focused));
    }
    if (rows.size() == 1) {
        rows.push_back(text(avoid ? "  No avoid rules generated" : "  Generate a plan with p") | dim);
    } else if (count > page_size) {
        rows.push_back(plan_window_footer(start, end, count, "j/k select"));
    }
    return rows;
}

inline std::string plan_tag_detail(const std::vector<std::string>& tags) {
    if (tags.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) out += ", ";
        out += tags[i];
    }
    return out;
}

inline std::string plan_step_detail(const StrategyStep& step) {
    std::string out = step.done ? "[yes] " : "[ ] ";
    if (!step.label.empty()) out += step.label + ": ";
    out += step.action.empty() ? "-" : step.action;
    if (!step.target.empty()) out += " Target: " + step.target + ".";
    if (!step.system.empty()) out += " System: " + step.system + ".";
    if (!step.ship.empty()) out += " Ship: " + step.ship + ".";
    if (!step.crew.empty()) out += " Crew: " + step.crew + ".";
    if (!step.note.empty()) out += " " + step.note;
    return out;
}

inline std::string plan_steps_detail(const std::vector<StrategyStep>& steps) {
    if (steps.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) out += "\n";
        out += std::to_string(i + 1) + ". " + plan_step_detail(steps[i]);
    }
    return out;
}

inline Elements plan_research_detail(const ActionPlan& plan, const PlanTabState& ps) {
    Elements detail;
    int research_count = static_cast<int>(plan.top_research.size());
    if (research_count <= 0) {
        detail.push_back(text("Select a research item") | dim);
        return detail;
    }

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
        text("   Cat ") | dim,
        text(plan_bucket_text(r)) | bold | color(plan_bucket_color(r)),
        text("   Time ") | dim,
        text(plan_duration_short(r.research_time_seconds)) | bold,
    }));

    Elements detail_left;
    Elements detail_right;
    plan_add_field(detail_left, "Where", r.location.empty() ? plan_place_short(r) : r.location);
    plan_add_field(detail_left, "Category", plan_bucket_text(r) + " / " + r.funding_class);
    plan_add_field(detail_left, "Funding", plan_funding_detail(r));
    plan_add_field(detail_left, "Speedups", plan_speedup_detail(r.speedups));
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
    return detail;
}

inline Elements plan_strategy_detail(const std::vector<StrategyRecommendation>& items,
                                     int selected, const std::string& empty_text) {
    Elements detail;
    if (items.empty()) {
        detail.push_back(text(empty_text) | dim);
        return detail;
    }

    selected = std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
    const auto& rec = items[static_cast<size_t>(selected)];
    detail.push_back(hbox({
        text("#" + std::to_string(rec.priority) + " ") | dim,
        paragraph(rec.title.empty() ? rec.action : rec.title) | bold | color(strategy_lane_color(rec)) | flex,
    }));
    detail.push_back(hbox({
        text(" Lane ") | dim,
        text(strategy_state_text(rec)) | bold | color(strategy_lane_color(rec)),
        text("   Do ") | dim,
        text(strategy_decision_text(rec)) | bold | color(strategy_decision_color(rec)),
        text("   Score ") | dim,
        text(std::to_string(rec.score)) | bold,
        text("   Time ") | dim,
        text(rec.estimated_minutes > 0 ? std::to_string(rec.estimated_minutes) + "m" : "-") | bold,
        text("   Now ") | dim,
        text(rec.can_do_now ? "yes" : "no") | bold | color(rec.can_do_now ? Color::Green : Color::Yellow),
    }));

    Elements detail_left;
    Elements detail_right;
    plan_add_field(detail_left, "Action", rec.action.empty() ? rec.title : rec.action);
    plan_add_field(detail_left, "Decision", strategy_decision_text(rec));
    plan_add_field(detail_left, "Tags", plan_tag_detail(rec.tags));
    plan_add_field(detail_left, "Concurrent", rec.concurrent ? "Can run alongside queue/passive work." : "Treat as a focused step.");
    plan_add_field(detail_right, "Why", rec.reason);
    plan_add_field(detail_right, "Steps", plan_steps_detail(rec.steps));
    plan_add_field(detail_right, "Use", rec.lane == "avoid"
        ? "Suppress this unless live data changes or the blocker is cleared."
        : "Work this item when its pane is selected; then sync and regenerate the plan.");
    detail.push_back(hbox({
        vbox(std::move(detail_left)) | flex,
        separator(),
        vbox(std::move(detail_right)) | flex,
    }) | flex);
    return detail;
}

inline Elements plan_detail_rows(const ActionPlan& plan, const StrategyPlan& strategy,
                                 const PlanTabState& ps) {
    if (ps.pane == kPlanPaneAvalanche) {
        return plan_strategy_detail(strategy.do_now, ps.avalanche_selected,
                                    "No avalanche items generated");
    }
    if (ps.pane == kPlanPaneAvoid) {
        return plan_strategy_detail(strategy.avoid, ps.avoid_selected,
                                    "No avoid rules generated");
    }
    return plan_research_detail(plan, ps);
}

inline Element render_plan_tab(const ActionPlan& plan, const StrategyPlan& strategy, PlanTabState& ps) {
    int research_count = static_cast<int>(plan.top_research.size());
    int avalanche_count = static_cast<int>(strategy.do_now.size());
    int avoid_count = static_cast<int>(strategy.avoid.size());
    plan_clamp_state(ps, research_count, avalanche_count, avoid_count);

    Elements research_rows;
    research_rows.push_back(plan_priority_header());
    int research_start = plan_clamped_offset(ps.research_offset, research_count,
                                             kPlanResearchPageSize);
    int research_end = std::min(research_count, research_start + kPlanResearchPageSize);
    for (int i = research_start; i < research_end; ++i) {
        const auto& r = plan.top_research[static_cast<size_t>(i)];
        auto row = hbox({
            text(plan_cell(std::to_string(i + 1), 4)) | dim | size(WIDTH, EQUAL, 4),
            text(plan_cell(plan_ready_text(r), 9)) | bold | color(plan_ready_color(r)) | size(WIDTH, EQUAL, 9),
            text(plan_cell(plan_bucket_text(r), 9)) | bold | color(plan_bucket_color(r)) | size(WIDTH, EQUAL, 9),
            text(plan_cell(r.name, 30)) | bold | size(WIDTH, EQUAL, 30),
            text(plan_cell(std::to_string(r.current_level) + ">" + std::to_string(r.next_level), 7)) | size(WIDTH, EQUAL, 7),
            text(plan_cell(plan_place_short(r), 18)) | size(WIDTH, EQUAL, 18),
            plan_fill_cell(plan_funding_summary(r)) | dim,
            text(plan_cell(plan_duration_short(r.research_time_seconds), 6)) | dim | size(WIDTH, EQUAL, 6),
        });
        if (i == ps.selected && ps.pane == kPlanPaneResearch) row = row | inverted | focus;
        research_rows.push_back(row);
    }
    if (research_rows.size() == 1) {
        research_rows.push_back(text("  Generate a plan with p") | dim);
    } else if (research_count > kPlanResearchPageSize) {
        research_rows.push_back(plan_window_footer(research_start, research_end,
                                                   research_count, "j/k select"));
    }

    Elements detail = plan_detail_rows(plan, strategy, ps);

    int64_t generated_at = strategy.generated_at > 0 ? strategy.generated_at : plan.generated_at;
    int sync_age_seconds = strategy.sync_age_seconds >= 0 ? strategy.sync_age_seconds : plan.sync_age_seconds;
    std::string focus = strategy.focus.empty() ? plan.focus : strategy.focus;
    size_t warning_count = strategy.generated_at > 0 ? strategy.warnings.size() : plan.warnings.size();
    std::string generated = generated_at > 0 ? ui::fmt_date(generated_at) : "never";
    std::string sync_age = sync_age_seconds >= 0 ? ui::fmt_dur(sync_age_seconds) + " old" : "no sync";
    auto summary = hbox({
        text(" Plan ") | bold | color(Color::Cyan),
        text(generated) | bold,
        text(" sync ") | dim,
        text(sync_age) | color(sync_age_seconds >= 0 && sync_age_seconds < 6 * 3600 ? Color::Green : Color::Yellow),
        text(" focus ") | dim,
        text(focus) | bold,
        text(" now ") | dim,
        text(std::to_string(strategy.do_now.size())) | bold | color(strategy.do_now.empty() ? Color::Yellow : Color::Green),
        text(" save ") | dim,
        text(std::to_string(plan.save_for.size())) | bold | color(plan.save_for.empty() ? Color::GrayDark : Color::Yellow),
        text(" warn ") | dim,
        text(std::to_string(warning_count)) | bold | color(warning_count == 0 ? Color::GrayDark : Color::Yellow),
        text(" pane ") | dim,
        text(plan_pane_name(ps.pane)) | bold,
        filler(),
        text("h/l pane  j/k scroll/select  p/P") | dim,
    });

    return vbox({
        summary,
        separator(),
        ui::panel(plan_panel_title("Avalanche Now", kPlanPaneAvalanche, ps),
                  plan_wide_table(strategy_rows(strategy, false, ps.avalanche_offset,
                                                kPlanAvalanchePageSize, ps.avalanche_selected,
                                                ps.pane == kPlanPaneAvalanche))) | size(HEIGHT, LESS_THAN, 12) | xframe | vscroll_indicator | yframe,
        separator(),
        ui::panel(plan_panel_title("Avoid / Suppress", kPlanPaneAvoid, ps),
                  plan_wide_table(strategy_rows(strategy, true, ps.avoid_offset,
                                                kPlanAvoidPageSize, ps.avoid_selected,
                                                ps.pane == kPlanPaneAvoid))) | size(HEIGHT, LESS_THAN, 8) | xframe | vscroll_indicator | yframe,
        separator(),
        ui::panel(plan_panel_title("Research Priority", kPlanPaneResearch, ps),
                  plan_wide_table(std::move(research_rows))) | size(HEIGHT, LESS_THAN, 10) | xframe | vscroll_indicator | yframe,
        separator(),
        ui::panel("Detail Pane", detail) | flex | vscroll_indicator | yframe,
    }) | flex;
}

inline void plan_move_research_selection(PlanTabState& ps, int research_count, int delta) {
    ps.selected += delta;
    plan_keep_research_visible(ps, research_count);
}

inline void plan_move_item_selection(int& selected, int& offset, int count,
                                     int page_size, int delta) {
    selected += delta;
    plan_keep_item_visible(selected, offset, count, page_size);
}

inline bool handle_plan_input(const Event& event, PlanTabState& ps, int research_count,
                              int avalanche_count, int avoid_count) {
    plan_clamp_state(ps, research_count, avalanche_count, avoid_count);

    if (event == Event::ArrowRight) {
        ps.pane = (ps.pane + 1) % kPlanPaneCount;
        return true;
    }
    if (event == Event::ArrowLeft) {
        ps.pane = (ps.pane + kPlanPaneCount - 1) % kPlanPaneCount;
        return true;
    }

    int* selected = nullptr;
    int* offset = nullptr;
    int item_count = 0;
    int page_size = 0;
    if (ps.pane == kPlanPaneAvalanche) {
        selected = &ps.avalanche_selected;
        offset = &ps.avalanche_offset;
        item_count = avalanche_count;
        page_size = kPlanAvalanchePageSize;
    } else if (ps.pane == kPlanPaneAvoid) {
        selected = &ps.avoid_selected;
        offset = &ps.avoid_offset;
        item_count = avoid_count;
        page_size = kPlanAvoidPageSize;
    }

    if (event == Event::ArrowDown) {
        if (selected && offset) {
            plan_move_item_selection(*selected, *offset, item_count, page_size, 1);
        } else {
            plan_move_research_selection(ps, research_count, 1);
        }
        return true;
    }
    if (event == Event::ArrowUp) {
        if (selected && offset) {
            plan_move_item_selection(*selected, *offset, item_count, page_size, -1);
        } else {
            plan_move_research_selection(ps, research_count, -1);
        }
        return true;
    }
    if (event == Event::PageDown) {
        if (selected && offset) {
            plan_move_item_selection(*selected, *offset, item_count, page_size, page_size);
        } else {
            plan_move_research_selection(ps, research_count, kPlanResearchPageSize);
        }
        return true;
    }
    if (event == Event::PageUp) {
        if (selected && offset) {
            plan_move_item_selection(*selected, *offset, item_count, page_size, -page_size);
        } else {
            plan_move_research_selection(ps, research_count, -kPlanResearchPageSize);
        }
        return true;
    }
    if (event == Event::Home) {
        if (selected && offset) {
            *selected = 0;
            plan_keep_item_visible(*selected, *offset, item_count, page_size);
        } else {
            ps.selected = 0;
            plan_keep_research_visible(ps, research_count);
        }
        return true;
    }
    if (event == Event::End) {
        if (selected && offset) {
            *selected = std::max(0, item_count - 1);
            plan_keep_item_visible(*selected, *offset, item_count, page_size);
        } else {
            ps.selected = std::max(0, research_count - 1);
            plan_keep_research_visible(ps, research_count);
        }
        return true;
    }
    return false;
}

} // namespace stfc
