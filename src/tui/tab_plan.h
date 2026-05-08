#pragma once
// ---------------------------------------------------------------------------
// Plan Tab — live action planner output
// ---------------------------------------------------------------------------

#include <algorithm>
#include <string>

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

inline Element render_plan_tab(const ActionPlan& plan, PlanTabState&) {
    Elements warnings;
    if (plan.warnings.empty()) {
        warnings.push_back(text("  No warnings") | dim);
    } else {
        for (const auto& w : plan.warnings) {
            warnings.push_back(text("  " + ui::trunc(w, 74)) | color(Color::Yellow));
        }
    }

    Elements do_now;
    if (plan.do_now.empty()) {
        do_now.push_back(text("  Nothing ready. Generate a plan after live sync or check Save For.") | dim);
    } else {
        for (const auto& a : plan.do_now) {
            auto row = hbox({
                text("  " + std::to_string(a.priority) + ". ") | dim,
                text(ui::trunc(a.action, 36)) | bold | color(Color::Green) | size(WIDTH, EQUAL, 40),
                text(ui::fmt_dur(a.duration_seconds)) | size(WIDTH, EQUAL, 10),
                text(ui::trunc(a.reason, 28)) | dim,
            });
            do_now.push_back(row);
        }
    }

    Elements research_rows;
    research_rows.push_back(ui::tbl_header({{"#", 4}, {"Research", 28}, {"Lvl", 7}, {"Ready", 7}, {"Missing", 25}}));
    int rank = 1;
    for (const auto& r : plan.top_research) {
        if (rank > 5) break;
        std::string ready = r.can_start_now ? "yes" : (r.resources_available && r.prerequisites_met ? "queue" : "no");
        Color ready_color = r.can_start_now ? Color::Green : (r.resources_available ? Color::Yellow : Color::Red);
        auto row = hbox({
            text(std::to_string(rank)) | dim | size(WIDTH, EQUAL, 4),
            text(ui::trunc(r.name, 26)) | bold | size(WIDTH, EQUAL, 28),
            text(std::to_string(r.current_level) + ">" + std::to_string(r.next_level)) | size(WIDTH, EQUAL, 7),
            text(ready) | bold | color(ready_color) | size(WIDTH, EQUAL, 7),
            text(ui::trunc(plan_resource_summary(r.missing_resources), 24)) | dim | size(WIDTH, EQUAL, 25),
        });
        research_rows.push_back(row);
        std::string detail = !r.description.empty() ? r.description : r.reason;
        if (!detail.empty()) {
            research_rows.push_back(text("    " + ui::trunc(detail, 78)) | dim);
        }
        rank++;
    }
    if (rank == 1) {
        research_rows.push_back(text("  Generate a plan with p") | dim);
    }

    Elements save_rows;
    if (plan.save_for.empty()) {
        save_rows.push_back(text("  No save targets yet") | dim);
    } else {
        for (size_t i = 0; i < plan.save_for.size() && i < 4; ++i) {
            const auto& s = plan.save_for[i];
            save_rows.push_back(hbox({
                text("  " + std::to_string(i + 1) + ". ") | dim,
                text(ui::trunc(s.target, 34)) | color(Color::Yellow) | size(WIDTH, EQUAL, 38),
                text(ui::trunc(plan_resource_summary(s.missing_resources), 30)) | bold,
            }));
        }
    }

    Elements avoid_rows;
    if (plan.avoid.empty()) {
        avoid_rows.push_back(text("  No avoid list yet") | dim);
    } else {
        for (size_t i = 0; i < plan.avoid.size() && i < 3; ++i) {
            const auto& a = plan.avoid[i];
            avoid_rows.push_back(hbox({
                text("  " + ui::trunc(a.action, 34)) | color(Color::Red) | size(WIDTH, EQUAL, 38),
                text(ui::trunc(a.reason, 34)) | dim,
            }));
        }
    }

    std::string generated = plan.generated_at > 0 ? ui::fmt_date(plan.generated_at) : "never";
    std::string sync_age = plan.sync_age_seconds >= 0 ? ui::fmt_dur(plan.sync_age_seconds) + " old" : "no sync";
    auto summary = hbox({
        text(" Plan ") | bold | color(Color::Cyan),
        text("generated: ") | dim,
        text(generated) | bold,
        text("  sync: ") | dim,
        text(sync_age) | color(plan.sync_age_seconds >= 0 && plan.sync_age_seconds < 6 * 3600 ? Color::Green : Color::Yellow),
        text("  focus: ") | dim,
        text(plan.focus) | bold,
        filler(),
        text("p:generate  P:regen") | dim,
    });

    return vbox({
        summary,
        separator(),
        ui::panel("Do Now", do_now),
        separator(),
        ui::panel("Top 5 Research", research_rows),
        separator(),
        hbox({
            ui::panel("Save For", save_rows) | flex,
            separator(),
            ui::panel("Avoid", avoid_rows) | flex,
        }),
        separator(),
        ui::panel("Warnings", warnings),
    }) | flex;
}

} // namespace stfc
