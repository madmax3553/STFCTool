#include "app/strategy_recommender.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace stfc {

namespace {

int64_t now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t tp_epoch(std::chrono::system_clock::time_point tp) {
    if (tp == std::chrono::system_clock::time_point{}) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(
        tp.time_since_epoch()).count();
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool contains_word(const std::string& haystack, const std::string& needle) {
    return lower_copy(haystack).find(lower_copy(needle)) != std::string::npos;
}

bool contains_any(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (contains_word(haystack, needle)) return true;
    }
    return false;
}

bool is_background_event(EventCategory category) {
    return category == EventCategory::FieldTraining ||
           category == EventCategory::FtCategory ||
           category == EventCategory::Cutscenes ||
           category == EventCategory::MinigameCategory ||
           category == EventCategory::MinigameStage ||
           category == EventCategory::LoopMuseum ||
           category == EventCategory::LoopMuseumTask;
}

std::string join_tags(const std::vector<std::string>& tags) {
    std::ostringstream out;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) out << ",";
        out << tags[i];
    }
    return out.str();
}

bool tag_seen(std::set<std::string>& seen, const std::vector<std::string>& tags) {
    const std::string key = join_tags(tags);
    if (seen.count(key)) return true;
    seen.insert(key);
    return false;
}

void assign_priorities(std::vector<StrategyRecommendation>& recs) {
    std::stable_sort(recs.begin(), recs.end(),
                     [](const StrategyRecommendation& a, const StrategyRecommendation& b) {
                         if (a.score != b.score) return a.score > b.score;
                         return a.estimated_minutes < b.estimated_minutes;
                     });
    int priority = 1;
    for (auto& rec : recs) rec.priority = priority++;
}

StrategyRecommendation make_rec(const std::string& lane,
                                const std::string& title,
                                const std::string& action,
                                const std::string& reason,
                                int score,
                                int minutes,
                                bool can_do_now,
                                bool concurrent,
                                std::vector<std::string> tags) {
    StrategyRecommendation rec;
    rec.lane = lane;
    rec.decision = lane == "avoid" ? "no" : (can_do_now ? "yes" : "no");
    rec.title = title;
    rec.action = action;
    rec.reason = reason;
    rec.score = score;
    rec.estimated_minutes = minutes;
    rec.can_do_now = can_do_now;
    rec.concurrent = concurrent;
    rec.tags = std::move(tags);
    return rec;
}

StrategyStep make_step(const std::string& label,
                       const std::string& action,
                       const std::string& target = "",
                       const std::string& system = "",
                       const std::string& ship = "",
                       const std::string& crew = "",
                       const std::string& note = "") {
    StrategyStep step;
    step.label = label;
    step.action = action;
    step.target = target;
    step.system = system;
    step.ship = ship;
    step.crew = crew;
    step.note = note;
    return step;
}

StrategyRecommendation with_steps(StrategyRecommendation rec,
                                  std::vector<StrategyStep> steps,
                                  const std::string& decision = "") {
    rec.steps = std::move(steps);
    if (!decision.empty()) rec.decision = decision;
    return rec;
}

int completed_job_count(const PlayerData& player_data, int64_t now) {
    return static_cast<int>(std::count_if(
        player_data.jobs.begin(), player_data.jobs.end(),
        [&player_data, now](const PlayerJob& job) {
            return has_actionable_completed_job_claim(player_data, job, now);
        }));
}

int stale_completed_job_count(const PlayerData& player_data, int64_t now) {
    const int64_t recent_cutoff = now - 24 * 3600;
    return static_cast<int>(std::count_if(
        player_data.jobs.begin(), player_data.jobs.end(),
        [&player_data, recent_cutoff](const PlayerJob& job) {
            return job.completed &&
                   (job.start_time < recent_cutoff ||
                    completed_job_already_reflected(player_data, job));
        }));
}

int stale_unfinished_job_count(const PlayerData& player_data, int64_t now) {
    return static_cast<int>(std::count_if(
        player_data.jobs.begin(), player_data.jobs.end(),
        [&player_data, now](const PlayerJob& job) {
            return unfinished_job_is_stale(player_data, job, now);
        }));
}

int live_unfinished_job_count(const PlayerData& player_data, int64_t now) {
    return static_cast<int>(std::count_if(
        player_data.jobs.begin(), player_data.jobs.end(),
        [&player_data, now](const PlayerJob& job) {
            return has_live_unfinished_job(player_data, job, now);
        }));
}

int manual_claimable_event_count(const PlayerData& player_data, int64_t now) {
    return static_cast<int>(std::count_if(
        player_data.events.begin(), player_data.events.end(),
        [now](const PlayerEvent& event) {
            return !is_background_event(event.category) &&
                   event.has_manual_claimable_reward(now);
        }));
}

bool action_plan_has_signal(const ActionPlan& plan,
                            const std::vector<std::string>& needles) {
    for (const auto& action : plan.do_now) {
        const std::string text = action.action + " " + action.reason + " " + action.location;
        if (contains_any(text, needles)) return true;
        for (const auto& r : action.resources_spent) {
            if (contains_any(r.name, needles)) return true;
        }
        for (const auto& r : action.missing_resources) {
            if (contains_any(r.name, needles)) return true;
        }
    }
    for (const auto& target : plan.save_for) {
        std::string text = target.target + " " + target.reason + " " + target.location;
        for (const auto& r : target.missing_resources) text += " " + r.name;
        if (contains_any(text, needles)) return true;
    }
    for (const auto& r : plan.top_research) {
        std::string text = r.name + " " + r.description + " " + r.reason + " " + r.location;
        for (const auto& cost : r.costs) text += " " + cost.name;
        for (const auto& missing : r.missing_resources) text += " " + missing.name;
        if (contains_any(text, needles)) return true;
    }
    return false;
}

bool active_event_has_signal(const FullAccountSnapshot& snapshot,
                             const std::vector<std::string>& needles) {
    for (const auto& event : snapshot.events) {
        if (event.state != EventState::Active) continue;
        if (is_background_event(event.category)) continue;
        const std::string text = event.config_id + " " + event.source + " " +
                                 event.event_type + " " + event.group_name + " " +
                                 event.category_name;
        if (contains_any(text, needles)) return true;
    }
    return false;
}

bool action_uses_resource(const PlanAction& action,
                          const std::vector<std::string>& needles) {
    for (const auto& resource : action.resources_spent) {
        if (contains_any(resource.name, needles)) return true;
    }
    return false;
}

const ResolvedShip* find_ship(const FullAccountSnapshot& snapshot,
                              const std::vector<std::string>& needles) {
    for (const auto& ship : snapshot.ships) {
        if (contains_any(ship.name, needles)) return &ship;
    }
    return nullptr;
}

bool ship_is_maxed(const ResolvedShip& ship) {
    return ship.max_tier > 0 && ship.max_level > 0 &&
           ship.tier >= ship.max_tier && ship.level >= ship.max_level;
}

std::string ship_progress_label(const ResolvedShip& ship) {
    std::ostringstream out;
    out << ship.name << " T" << ship.tier;
    if (ship.max_tier > 0) out << "/" << ship.max_tier;
    out << " L" << ship.level;
    if (ship.max_level > 0) out << "/" << ship.max_level;
    return out.str();
}

int64_t resource_amount_matching(const FullAccountSnapshot& snapshot,
                                 const std::vector<std::string>& needles) {
    int64_t total = 0;
    for (const auto& resource : snapshot.resources) {
        if (resource.amount <= 0) continue;
        if (contains_any(resource.name, needles)) total += resource.amount;
    }
    return total;
}

std::string amount_note(const std::string& label, int64_t amount) {
    if (amount <= 0) return "";
    return "; synced " + label + ": " + std::to_string(amount);
}

bool research_is_ship_support(const ResearchCandidate& research) {
    const std::string text = research.name + " " + research.description + " " +
                             research.reason + " " + research.location;
    return contains_any(text, {"ship", "weapon", "hull", "shield", "hostile",
                               "armada", "isolytic", "apex", "transogen",
                               "serene squall", "gs-31", "dauntless"});
}

bool research_is_short_or_speedup_covered(const ResearchCandidate& research) {
    if (research.research_time_seconds > 0 && research.research_time_seconds <= 8 * 3600) {
        return true;
    }
    return research.speedups.required && research.speedups.evaluated && research.speedups.enough;
}

const ResearchCandidate* best_queue_research(const ActionPlan& action_plan) {
    for (const auto& research : action_plan.top_research) {
        if (!research.can_start_now || research.tracking_only) continue;
        if (research_is_short_or_speedup_covered(research)) {
            return &research;
        }
    }
    for (const auto& research : action_plan.top_research) {
        if (!research.can_start_now || research.tracking_only) continue;
        const bool long_speedup_short =
            research.research_time_seconds > 8 * 3600 &&
            research.speedups.required &&
            research.speedups.evaluated &&
            !research.speedups.enough;
        if (!long_speedup_short && research_is_ship_support(research)) {
            return &research;
        }
    }
    return nullptr;
}

bool should_hold_action(const PlanAction& action) {
    const std::string text = action.action + " " + action.reason;

    // Prime Valor is a scarce route-setting currency in the current account.
    // Only spend it automatically on direct armada/ops-catchup unlocks.
    if (action_uses_resource(action, {"Prime Valor Emblem"})) {
        if (!contains_any(text, {"Prime Armada Refinery",
                                 "Prime Station Siege",
                                 "Prime Dominion Solo Raiding",
                                 "Discovery Warp Range"})) {
            return true;
        }
    }
    return false;
}

json step_to_json(const StrategyStep& step) {
    return {
        {"done", step.done},
        {"label", step.label},
        {"action", step.action},
        {"target", step.target},
        {"system", step.system},
        {"ship", step.ship},
        {"crew", step.crew},
        {"note", step.note},
    };
}

StrategyStep step_from_json(const json& j) {
    StrategyStep step;
    step.done = j.value("done", false);
    step.label = j.value("label", "");
    step.action = j.value("action", "");
    step.target = j.value("target", "");
    step.system = j.value("system", "");
    step.ship = j.value("ship", "");
    step.crew = j.value("crew", "");
    step.note = j.value("note", "");
    return step;
}

json rec_to_json(const StrategyRecommendation& rec) {
    json steps = json::array();
    for (const auto& step : rec.steps) steps.push_back(step_to_json(step));

    return {
        {"priority", rec.priority},
        {"lane", rec.lane},
        {"decision", rec.decision},
        {"title", rec.title},
        {"action", rec.action},
        {"reason", rec.reason},
        {"score", rec.score},
        {"estimated_minutes", rec.estimated_minutes},
        {"can_do_now", rec.can_do_now},
        {"concurrent", rec.concurrent},
        {"tags", rec.tags},
        {"steps", steps},
    };
}

StrategyRecommendation rec_from_json(const json& j) {
    StrategyRecommendation rec;
    rec.priority = j.value("priority", 0);
    rec.lane = j.value("lane", "");
    rec.decision = j.value("decision", "");
    rec.title = j.value("title", "");
    rec.action = j.value("action", "");
    rec.reason = j.value("reason", "");
    rec.score = j.value("score", 0);
    rec.estimated_minutes = j.value("estimated_minutes", 0);
    rec.can_do_now = j.value("can_do_now", false);
    rec.concurrent = j.value("concurrent", false);
    if (j.contains("tags") && j["tags"].is_array()) {
        for (const auto& tag : j["tags"]) {
            if (tag.is_string()) rec.tags.push_back(tag.get<std::string>());
        }
    }
    if (j.contains("steps") && j["steps"].is_array()) {
        for (const auto& step : j["steps"]) rec.steps.push_back(step_from_json(step));
    }
    if (rec.decision.empty()) {
        rec.decision = rec.lane == "avoid" ? "no" : (rec.can_do_now ? "yes" : "no");
    }
    return rec;
}

} // namespace

StrategyPlan generate_strategy_plan(const FullAccountSnapshot& snapshot,
                                    const PlayerData& player_data,
                                    const ActionPlan& action_plan,
                                    int time_budget_minutes,
                                    const std::string& focus) {
    StrategyPlan plan;
    plan.generated_at = now_epoch();
    plan.last_sync = tp_epoch(snapshot.last_sync);
    plan.sync_age_seconds = plan.last_sync > 0 ? static_cast<int>(plan.generated_at - plan.last_sync) : -1;
    plan.time_budget_minutes = time_budget_minutes;
    plan.focus = focus.empty() ? "ops66_catchup" : focus;

    if (snapshot.last_sync == std::chrono::system_clock::time_point{}) {
        plan.warnings.push_back("No live sync timestamp found; strategy recommendations are incomplete.");
    } else if (plan.sync_age_seconds > 6 * 3600) {
        plan.warnings.push_back("Live sync is older than 6 hours; refresh before trusting claim and resource state.");
    }
    if (!action_plan.warnings.empty()) {
        plan.warnings.insert(plan.warnings.end(),
                             action_plan.warnings.begin(),
                             action_plan.warnings.end());
    }

    std::set<std::string> seen;
    auto add_now = [&](StrategyRecommendation rec) {
        if (tag_seen(seen, rec.tags)) return;
        plan.do_now.push_back(std::move(rec));
    };
    auto add_avoid = [&](StrategyRecommendation rec) {
        if (tag_seen(seen, rec.tags)) return;
        plan.avoid.push_back(std::move(rec));
    };

    const int done_jobs = completed_job_count(player_data, plan.generated_at);
    const int stale_done_jobs = stale_completed_job_count(player_data, plan.generated_at);
    const int stale_open_jobs = stale_unfinished_job_count(player_data, plan.generated_at);
    const int live_open_jobs = live_unfinished_job_count(player_data, plan.generated_at);
    if (stale_done_jobs > 0) {
        plan.warnings.push_back("Ignored " + std::to_string(stale_done_jobs) +
                                " stale completed job record(s); refresh sync if jobs look wrong.");
    }
    if (stale_open_jobs > 0) {
        plan.warnings.push_back("Ignored " + std::to_string(stale_open_jobs) +
                                " stale unfinished job record(s); current job queues may be missing from sync.");
    }
    if (live_open_jobs == 0) {
        plan.warnings.push_back("No current job payload is available; verify building/research/ship queue occupancy in game.");
    }
    if (done_jobs > 0) {
        add_now(with_steps(make_rec(
            "claim",
            "Claim completed jobs",
            "Claim the " + std::to_string(done_jobs) +
                " completed job(s), then leave the claim pass.",
            "Completed jobs unlock the next queue decision. This is a bounded claim, not a full store sweep.",
            100, 2, true, false,
            {"claim", "jobs"}),
            {
                make_step("Claim", "Claim only completed jobs shown by live sync."),
                make_step("Sync", "Refresh sync after claiming so queue state and recommendations update."),
            },
            "yes"));
    }

    const int manual_event_claims = manual_claimable_event_count(player_data, plan.generated_at);
    if (manual_event_claims > 0) {
        add_now(with_steps(make_rec(
            "claim",
            "Claim live event rewards",
            "Claim only the " + std::to_string(manual_event_claims) +
                " event reward(s) marked claimable.",
            "Claimable live-event rewards can change the next best action; avoid old stores unless tied to the focus.",
            96, 3, true, false,
            {"claim", "events"}),
            {
                make_step("Claim", "Claim only live event rewards marked claimable."),
                make_step("Replan", "Regenerate the plan after claim because rewards may unblock the ship lane."),
            },
            "yes"));
    }

    bool ship_project_added = false;
    const auto* gs31 = find_ship(snapshot, {"GS-31"});
    const auto* dauntless = find_ship(snapshot, {"Dauntless"});
    const auto* nsea = find_ship(snapshot, {"NSEA Protector"});
    const auto* revenant = find_ship(snapshot, {"SS Revenant"});

    if (gs31 && !ship_is_maxed(*gs31)) {
        const int64_t gs31_parts = resource_amount_matching(snapshot, {"GS-31 Parts"});
        const int64_t gs31_recon = resource_amount_matching(snapshot, {"GS-31 Recon Data"});
        add_now(with_steps(make_rec(
            "queue",
            "Ship spend: " + ship_progress_label(*gs31),
            "Spend ship XP, GS-31 parts, and tier materials on GS-31 until the next level/tier is blocked; then sync.",
            "Ships are the anchor spend at Ops 66. Live roster shows " +
                ship_progress_label(*gs31) +
                ", so GS-31 catch-up should be checked before starting long research" +
                amount_note("GS-31 parts", gs31_parts) +
                amount_note("GS-31 Recon Data", gs31_recon) + ".",
            98, 3, true, false,
            {"queue", "ship", "gs31"}),
            {
                make_step("Event check",
                          "Check Events for ship upgrade, Section 31, Wave Defense, or hostile scoring before spending."),
                make_step("Spend",
                          "Level/tier GS-31 until the next requirement blocks.",
                          "GS-31 level/tier", "", "GS-31"),
                make_step("Active source",
                          "If blocked by Recon Data or Section 31 currency, run the GS-31 active lane.",
                          "GS-31 Recon Data",
                          "Highest Elite Solo Wave Defense bracket you can clear efficiently",
                          "GS-31 plus strongest solo ships",
                          "Use Crew Advisor for solo/wave/hostile survivability crew"),
                make_step("Sync", "Refresh sync after upgrades or a wave-defense block changes."),
            },
            "yes"));
        ship_project_added = true;
    }

    if (dauntless && !ship_is_maxed(*dauntless)) {
        const int64_t dauntless_parts = resource_amount_matching(snapshot, {"Dauntless Parts"});
        const int64_t prototype_data = resource_amount_matching(snapshot, {"Dauntless Prototype Data"});
        add_now(with_steps(make_rec(
            "queue",
            std::string(ship_project_added ? "Fallback ship: " : "Ship spend: ") +
                ship_progress_label(*dauntless),
            "If GS-31 is blocked, spend available Dauntless parts/prototype data and tier materials on Dauntless; then sync.",
            "Dauntless is the next ship catch-up lane because it reduces hostile-grind friction and feeds Aggregation progression. Live roster shows " +
                ship_progress_label(*dauntless) +
                amount_note("Dauntless parts", dauntless_parts) +
                amount_note("Prototype Data", prototype_data) + ".",
            ship_project_added ? 88 : 95, 3, true, false,
            {"queue", "ship", "dauntless"}),
            {
                make_step("Gate",
                          "Use this only if GS-31 is blocked by missing currency, directives, repairs, or event mismatch."),
                make_step("Event check",
                          "Check Events for Aggregation, Dauntless, hostile, or ship upgrade scoring."),
                make_step("Spend",
                          "Level/tier Dauntless with available parts, Prototype Data, and tier materials until blocked.",
                          "Dauntless level/tier", "", "U.S.S. Dauntless"),
                make_step("Active source",
                          "If blocked by Aggregation outputs, run Aggregation hostiles/refinery before unrelated grinds.",
                          "Dauntless parts / Prototype Data",
                          "Highest Aggregation system you can clear efficiently",
                          "U.S.S. Dauntless",
                          "Use Crew Advisor for hostile grind crew"),
                make_step("Sync", "Refresh sync after spending or after the active block."),
            },
            ship_project_added ? "fallback" : "yes"));
        ship_project_added = true;
    }

    if (nsea && ship_is_maxed(*nsea)) {
        add_avoid(with_steps(make_rec(
            "avoid",
            "Do not spend into NSEA by default",
            "Treat NSEA/Mirror as maintenance unless a named Mirror Research or Omega Mirror Dust target is selected.",
            "Live roster shows " + ship_progress_label(*nsea) +
                ", so this ship is already capped in the synced data.",
            88, 0, false, false,
            {"avoid", "ship", "nsea"}),
            {
                make_step("No-go", "Do not choose NSEA/Mirror as the active lane by default."),
                make_step("Reopen only if", "A named Mirror Research, Omega Mirror Dust, or NSEA refit target becomes the blocker."),
            },
            "no"));
    }
    if (revenant && ship_is_maxed(*revenant)) {
        add_avoid(with_steps(make_rec(
            "avoid",
            "Do not spend into SS Revenant by default",
            "Treat Revenant as a completed anchor unless a new tier/currency target appears.",
            "Live roster shows " + ship_progress_label(*revenant) +
                ", so it should not displace GS-31/Dauntless catch-up.",
            80, 0, false, false,
            {"avoid", "ship", "revenant"}),
            {
                make_step("No-go", "Do not spend active time or scarce ship resources on Revenant by default."),
                make_step("Reopen only if", "A new uncapped Revenant target appears in live data."),
            },
            "no"));
    }

    int research_added = 0;
    const auto* queue_research = best_queue_research(action_plan);
    if (queue_research) {
        add_now(with_steps(make_rec(
            "queue",
            "Queue filler research: " + queue_research->name + " L" +
                std::to_string(queue_research->next_level),
            "Start this research only after the ship spend check is blocked or complete.",
            queue_research->reason.empty()
                ? "This is a short or ship-supporting queue filler; it should not displace the ship anchor."
                : queue_research->reason + "; research is secondary to the selected ship spend.",
            ship_project_added ? 68 : 86,
            queue_research->research_time_seconds > 0 &&
                queue_research->research_time_seconds < 300 ? 1 : 3,
            true, false,
            {"queue", "research", queue_research->name}),
            {
                make_step("Gate", "Start only after the selected ship spend is blocked or complete."),
                make_step("Event check", "Check Events for research spend/scoring before starting."),
                make_step("Start", "Start " + queue_research->name + " L" +
                          std::to_string(queue_research->next_level) + "."),
                make_step("Sync", "Refresh sync after starting research."),
            },
            ship_project_added ? "fallback" : "yes"));
        research_added++;
    }

    for (const auto& action : action_plan.do_now) {
        if (!action.can_do_now || research_added >= 1) continue;
        const bool long_speedup_short =
            action.duration_seconds > 8 * 3600 &&
            action.speedups.required &&
            action.speedups.evaluated &&
            !action.speedups.enough;
        if (ship_project_added && long_speedup_short) {
            add_avoid(with_steps(make_rec(
                "avoid",
                "Hold long research spend",
                "Do not start yet: " + action.action,
                "This is a long timer with verified speedup shortage. Check the ship spend first, then use research as queue filler.",
                84, 0, false, false,
                {"avoid", "research", action.action}),
                {
                    make_step("No-go", "Do not start this long research before the ship lane is checked."),
                    make_step("Reopen only if", "Ship spend is blocked and the timer can be afforded without starving speedups."),
                },
                "no"));
            continue;
        }
        if (should_hold_action(action)) {
            add_avoid(with_steps(make_rec(
                "avoid",
                "Hold scarce research spend",
                "Do not start: " + action.action,
                "It spends scarce special currency that is currently better preserved for armada/refinery catch-up targets.",
                95, 0, false, false,
                {"avoid", "scarce", action.action}),
                {
                    make_step("No-go", "Do not spend the scarce currency yet."),
                    make_step("Reopen only if", "The action becomes the named blocker for the active ship or armada lane."),
                },
                "no"));
            continue;
        }

        add_now(with_steps(make_rec(
            "queue",
            action.action,
            action.action,
            action.reason.empty()
                ? "The live action planner says this is affordable and startable."
                : action.reason,
            ship_project_added ? 64 : (research_added == 0 ? 86 : 76),
            action.duration_seconds > 0 && action.duration_seconds < 300 ? 1 : 3,
            true, false,
            {"queue", action.action}),
            {
                make_step("Gate", "Use this only if no higher ship spend is available."),
                make_step("Event check", "Check Events for research spend/scoring before starting."),
                make_step("Start", action.action),
                make_step("Sync", "Refresh sync after starting."),
            },
            ship_project_added ? "fallback" : "yes"));
        research_added++;
        if (research_added >= 1) break;
    }

    const bool transogen_signal = action_plan_has_signal(
        action_plan, {"Transogen", "Serene Squall", "Black Market Schematic", "Squall"});
    if (transogen_signal) {
        add_now(with_steps(make_rec(
            "passive",
            "Set Transogen passive mining",
            "Put the Squall or best survey on Transogen before starting active play.",
            "The live plan has a Transogen/Squall research signal, so passive mining helps the current bottleneck without spending active attention.",
            86, 2, true, true,
            {"passive", "transogen"}),
            {
                make_step("Event check", "Check Events for mining, Transogen, or research overlap."),
                make_step("Deploy miner",
                          "Send Serene Squall or best survey to a Transogen node and leave it running.",
                          "Raw Transogen",
                          "Highest safe Transogen system",
                          "Serene Squall or best survey",
                          "Mining speed/protected cargo crew"),
                make_step("Do not babysit", "Return to the selected active lane after the miner is set."),
            },
            "yes"));
    } else if (!snapshot.resource_state_partial && !action_plan.save_for.empty()) {
        add_now(with_steps(make_rec(
            "passive",
            "Set bottleneck passive mining",
            "Mine the top missing research/build resource, then leave it running.",
            "Passive mining should serve the selected save target; do not spend active time on it unless an event requires it.",
            72, 2, true, true,
            {"passive", "mining"}),
            {
                make_step("Identify", "Use the top save target's missing resource as the mining target."),
                make_step("Deploy miner", "Send best survey and leave it running.", "", "Highest safe matching node", "Best survey", "Mining speed/protected cargo crew"),
                make_step("Return", "Do not spend the active block babysitting the node."),
            },
            "yes"));
    } else {
        add_avoid(with_steps(make_rec(
            "avoid",
            "Skip speculative mining",
            "Do not spend active attention choosing a mining target until resource sync identifies a real bottleneck.",
            "Core balances or save targets are missing, so a mining recommendation would be a guess.",
            84, 0, false, false,
            {"avoid", "mining"}),
            {
                make_step("No-go", "Do not choose a mining target by feel."),
                make_step("Reopen only if", "Live data names a missing ship/building/research resource or a mining event overlaps."),
            },
            "no"));
    }

    const bool armada_event = active_event_has_signal(
        snapshot, {"armada", "solo", "wave", "defense"});
    add_now(with_steps(make_rec(
        "active",
        "Main active: GS-31 path",
        "Spend the active block on Elite Solo Wave Defense or elite solo armadas toward GS-31.",
        armada_event
            ? "This is the top active lane and it appears to overlap current event text."
            : "This is the top active lane because it builds a durable account unlock instead of spreading clicks across mature loops.",
        armada_event ? 90 : 82,
        std::max(15, std::min(45, time_budget_minutes - 10)),
        true, false,
        {"active", "gs31"}),
        {
            make_step("Event check",
                      "Check Events for Wave Defense, Section 31, solo armada, hostile, or ship-upgrade scoring."),
            make_step("Run active block",
                      "Run Elite Solo Wave Defense first if entries and repairs are acceptable.",
                      "GS-31 Recon Data / S31 credits",
                      "Highest Elite Solo Wave Defense bracket you can clear efficiently",
                      "GS-31 plus strongest solo ships",
                      "Use Crew Advisor for solo/wave survivability and damage"),
            make_step("Fallback within lane",
                      "If Wave Defense entries are blocked, run elite solo armadas that feed the same GS-31/S31 path.",
                      "GS-31/S31 progress",
                      "Highest efficient elite solo armada target",
                      "Best solo armada triangle",
                      "Use Crew Advisor for solo armada crew"),
            make_step("Stop rule", "Stop when repairs, entries, or rewards stop advancing GS-31 efficiently; sync and replan."),
        },
        "yes"));

    add_now(with_steps(make_rec(
        "active",
        "Fallback active: Dauntless",
        "If GS-31 entries/directives are blocked, switch the same active block to Dauntless/Aggregation.",
        "This is the next best concentrated active lane; use it as a fallback, not as a parallel grind.",
        70, std::max(10, std::min(30, time_budget_minutes / 2)),
        true, false,
        {"active", "dauntless"}),
        {
            make_step("Gate", "Use only when the GS-31 active block is blocked or inefficient."),
            make_step("Event check", "Check Events for Aggregation, Dauntless, hostile, or ship-upgrade scoring."),
            make_step("Grind",
                      "Grind Aggregation hostiles/refinery only until the Dauntless bottleneck moves.",
                      "Dauntless parts / Prototype Data / Aggregation outputs",
                      "Highest Aggregation system you can clear efficiently",
                      "U.S.S. Dauntless",
                      "Use Crew Advisor for hostile grind crew"),
            make_step("Sync", "Refresh after the active block so the ship-spend recommendation updates."),
        },
        "fallback"));

    const bool artifact_event = active_event_has_signal(
        snapshot, {"artifact", "formation", "armada"});
    if (artifact_event) {
        add_now(with_steps(make_rec(
            "active",
            "Event overlap: Formation artifacts",
            "Run Formation armadas only if they score the live artifact/event objective.",
            "Artifacts are useful, but they should not displace the main active lane unless the event stack pays twice.",
            68, 10, true, false,
            {"active", "formation", "event"}),
            {
                make_step("Gate", "Run only for a live artifact/formation event overlap."),
                make_step("Run", "Run the lowest-cost Formation armadas that complete the event objective.", "Artifact progress", "Formation armada target", "Best formation fleet", "Use Crew Advisor for armada crew"),
                make_step("Stop", "Stop after event milestones or artifact target are reached."),
            },
            "fallback"));
    } else {
        add_avoid(with_steps(make_rec(
            "avoid",
            "Do not default to Formation",
            "Skip Formation armadas unless they advance a named artifact target or live event.",
            "Artifacts matter, but the current avalanche needs a named target before this becomes the main click sink.",
            72, 0, false, false,
            {"avoid", "formation"}),
            {
                make_step("No-go", "Do not run Formation armadas as generic busywork."),
                make_step("Reopen only if", "A live event or named artifact target overlaps the current ship lane."),
            },
            "no"));
    }

    add_avoid(with_steps(make_rec(
        "avoid",
        "Skip full store clutter",
        "Do not claim old data, legacy faction-store clutter, Augment-style residue, or archived loop residue.",
        "A full claim sweep can consume the same time as a focused active block without changing the recommendation.",
        90, 0, false, false,
        {"avoid", "claims"}),
        {
            make_step("No-go", "Do not do a full claim sweep."),
            make_step("Allowed", "Only claim stores/refineries explicitly named by the active lane or event overlap."),
        },
        "no"));

    const bool faction_signal = action_plan_has_signal(
        action_plan, {"faction", "klingon", "romulan", "federation", "fkr"}) ||
        active_event_has_signal(snapshot, {"faction", "klingon", "romulan", "federation", "fkr"});
    if (!faction_signal) {
        add_avoid(with_steps(make_rec(
            "avoid",
            "Do not grind FKR by default",
            "Skip FKR reputation grinding unless a named ship/research/store buy is currently blocked.",
            "Reputation balance is not an output by itself. It only wins when it unlocks a specific higher-ROI target.",
            86, 0, false, false,
            {"avoid", "fkr"}),
            {
                make_step("No-go", "Do not grind FKR reputation just to rebalance reputation."),
                make_step("Reopen only if", "A named ship, research, credit purchase, or faction store unlock blocks the selected lane."),
            },
            "no"));
    }

    const bool anomaly_event = active_event_has_signal(
        snapshot, {"anomalous", "phenomenon", "discovery", "nesmith", "nsea"});
    if (!anomaly_event) {
        add_avoid(with_steps(make_rec(
            "avoid",
            "Skip Anomalous Phenomenon",
            "Do not spend active time on Anomalous Phenomenon/Discovery unless the current event stack pays out.",
            "With the mature Discovery/NSEA lane, this is maintenance unless a live event or named target changes the math.",
            82, 0, false, false,
            {"avoid", "anomaly"}),
            {
                make_step("No-go", "Do not spend the active block on Anomalous Phenomenon by default."),
                make_step("Reopen only if", "A live event or named Discovery/NSEA target makes the clicks pay twice."),
            },
            "no"));
    }

    assign_priorities(plan.do_now);
    assign_priorities(plan.avoid);

    if (plan.do_now.size() > 10) plan.do_now.resize(10);
    if (plan.avoid.size() > 8) plan.avoid.resize(8);
    return plan;
}

bool save_strategy_plan(const StrategyPlan& plan, const std::string& path) {
    try {
        fs::create_directories(fs::path(path).parent_path());

        json j;
        j["generated_at"] = plan.generated_at;
        j["last_sync"] = plan.last_sync;
        j["sync_age_seconds"] = plan.sync_age_seconds;
        j["time_budget_minutes"] = plan.time_budget_minutes;
        j["focus"] = plan.focus;
        j["warnings"] = plan.warnings;

        j["do_now"] = json::array();
        for (const auto& rec : plan.do_now) j["do_now"].push_back(rec_to_json(rec));

        j["avoid"] = json::array();
        for (const auto& rec : plan.avoid) j["avoid"].push_back(rec_to_json(rec));

        std::ofstream out(path);
        if (!out) return false;
        out << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool load_strategy_plan(StrategyPlan& plan, const std::string& path) {
    try {
        if (!fs::exists(path)) return false;
        std::ifstream in(path);
        if (!in) return false;
        auto j = json::parse(in);

        plan = StrategyPlan{};
        plan.generated_at = j.value("generated_at", (int64_t)0);
        plan.last_sync = j.value("last_sync", (int64_t)0);
        plan.sync_age_seconds = j.value("sync_age_seconds", -1);
        plan.time_budget_minutes = j.value("time_budget_minutes", 45);
        plan.focus = j.value("focus", "ops66_catchup");

        if (j.contains("warnings") && j["warnings"].is_array()) {
            for (const auto& warning : j["warnings"]) {
                if (warning.is_string()) plan.warnings.push_back(warning.get<std::string>());
            }
        }
        if (j.contains("do_now") && j["do_now"].is_array()) {
            for (const auto& rec : j["do_now"]) plan.do_now.push_back(rec_from_json(rec));
        }
        if (j.contains("avoid") && j["avoid"].is_array()) {
            for (const auto& rec : j["avoid"]) plan.avoid.push_back(rec_from_json(rec));
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace stfc
