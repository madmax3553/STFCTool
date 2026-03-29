#include "core/planner.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>

// Use nlohmann json for persistence
#include "json.hpp"

namespace stfc {

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

const char* priority_str(TaskPriority p) {
    switch (p) {
        case TaskPriority::Critical: return "CRITICAL";
        case TaskPriority::High:     return "HIGH";
        case TaskPriority::Medium:   return "MEDIUM";
        case TaskPriority::Low:      return "LOW";
    }
    return "?";
}

const char* priority_icon(TaskPriority p) {
    switch (p) {
        case TaskPriority::Critical: return "!!!";
        case TaskPriority::High:     return "!! ";
        case TaskPriority::Medium:   return "!  ";
        case TaskPriority::Low:      return "   ";
    }
    return "   ";
}

const char* category_str(TaskCategory c) {
    switch (c) {
        case TaskCategory::Events:    return "Events";
        case TaskCategory::SpeedUps:  return "Speed-Ups";
        case TaskCategory::Ships:     return "Ships";
        case TaskCategory::Research:  return "Research";
        case TaskCategory::Officers:  return "Officers";
        case TaskCategory::Mining:    return "Mining";
        case TaskCategory::Combat:    return "Combat";
        case TaskCategory::Alliance:  return "Alliance";
        case TaskCategory::Store:     return "Store";
        case TaskCategory::Misc:      return "Misc";
    }
    return "?";
}

const char* category_icon(TaskCategory c) {
    switch (c) {
        case TaskCategory::Events:    return "[EVT]";
        case TaskCategory::SpeedUps:  return "[SPD]";
        case TaskCategory::Ships:     return "[SHP]";
        case TaskCategory::Research:  return "[RSC]";
        case TaskCategory::Officers:  return "[OFC]";
        case TaskCategory::Mining:    return "[MIN]";
        case TaskCategory::Combat:    return "[CMB]";
        case TaskCategory::Alliance:  return "[ALL]";
        case TaskCategory::Store:     return "[STR]";
        case TaskCategory::Misc:      return "[---]";
    }
    return "[?]";
}

// ---------------------------------------------------------------------------
// DailyPlan stats
// ---------------------------------------------------------------------------

int DailyPlan::completed_tasks() const {
    int n = 0;
    for (const auto& t : tasks) if (t.completed) ++n;
    return n;
}

int DailyPlan::skipped_tasks() const {
    int n = 0;
    for (const auto& t : tasks) if (t.skipped) ++n;
    return n;
}

int DailyPlan::remaining_tasks() const {
    int n = 0;
    for (const auto& t : tasks) if (!t.completed && !t.skipped) ++n;
    return n;
}

int DailyPlan::total_estimated_minutes() const {
    int n = 0;
    for (const auto& t : tasks) n += t.estimated_minutes;
    return n;
}

int DailyPlan::remaining_estimated_minutes() const {
    int n = 0;
    for (const auto& t : tasks) {
        if (!t.completed && !t.skipped) n += t.estimated_minutes;
    }
    return n;
}

double DailyPlan::completion_pct() const {
    if (tasks.empty()) return 100.0;
    int actionable = 0, done = 0;
    for (const auto& t : tasks) {
        if (!t.skipped) { ++actionable; if (t.completed) ++done; }
    }
    if (actionable == 0) return 100.0;
    return 100.0 * done / actionable;
}

// ---------------------------------------------------------------------------
// WeeklyPlan stats
// ---------------------------------------------------------------------------

int WeeklyPlan::completed_goals() const {
    int n = 0;
    for (const auto& g : goals) if (g.completed) ++n;
    return n;
}

double WeeklyPlan::goal_completion_pct() const {
    if (goals.empty()) return 100.0;
    return 100.0 * completed_goals() / goals.size();
}

// ---------------------------------------------------------------------------
// Date helpers
// ---------------------------------------------------------------------------

static std::string today_str() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return buf;
}

static int today_dow() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_wday;  // 0=Sun
}

static std::string monday_of_week() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int dow = t.tm_wday;
    int days_since_monday = (dow == 0) ? 6 : dow - 1;
    time_t monday = now - days_since_monday * 86400;
    localtime_r(&monday, &t);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return buf;
}

[[maybe_unused]]
static const char* dow_name(int dow) {
    static const char* names[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                   "Thursday", "Friday", "Saturday"};
    if (dow >= 0 && dow <= 6) return names[dow];
    return "?";
}

// ---------------------------------------------------------------------------
// Planner constructor
// ---------------------------------------------------------------------------

Planner::Planner() {
    init_daily_templates();
    init_weekly_templates();
}

// ---------------------------------------------------------------------------
// Daily task templates — real STFC G6 grind knowledge
// ---------------------------------------------------------------------------

void Planner::init_daily_templates() {
    // Two-axis priority: impact (account progression) + urgent (time-sensitive)
    // impact: 0-100, how much this moves your G6 account forward
    // urgent: shown as badge, NOT used as primary sort key
    // dynamic_boost: added later by enrich_plan_with_player_data()
    auto add = [this](
        const char* title, const char* desc,
        TaskCategory cat, int impact, bool urgent_flag,
        int minutes, const char* best_time,
        std::initializer_list<const char*> tags_init = {},
        int prog_total = 0) {

        PlannerTask t;
        t.id = next_task_id_++;
        t.title = title;
        t.description = desc;
        t.category = cat;
        t.impact_score = impact;
        t.urgent = urgent_flag;
        t.time_sensitive = urgent_flag;
        t.estimated_minutes = minutes;
        t.best_time = best_time;
        t.daily = true;
        t.g6_relevance = impact;  // compat
        t.priority = t.display_priority();
        t.progress_total = prog_total;
        for (auto tag : tags_init) t.tags.insert(tag);
        daily_templates_.push_back(std::move(t));
    };

    // =====================================================================
    // IMPACT TIERS (base scores, before dynamic boosts):
    //   100 = Core progression: research queue, building queue
    //    90 = Events (premium rewards source)
    //    85 = Officer rank-ups, ship tiering
    //    80 = Armadas (ship BPs, officer shards)
    //    75 = Daily goals, hostile grinding
    //    70 = Speed-up strategy, mining deployment
    //    65 = Generators/refinery collection
    //    60 = Alliance cooperation, PvP
    //    50 = Away missions
    //    45 = Alliance/faction stores
    //    40 = Free store packs, login rewards
    //    30 = Crew review/planning
    //
    // DYNAMIC BOOSTS (added by enrich_plan_with_player_data):
    //   +50 = Queue IDLE (research or build not running!)
    //   +30 = Officers ready to rank up
    //   +20 = Job just finished (collect & start next)
    //   +15 = Active event detected
    // =====================================================================

    // --- Progression core (impact 100) ---
    // These are #1 and #2 because idle queues = wasted time at G6

    add("Start/queue research",
        "Never let the research queue sit idle. At G6 research times are 10-30+ days. "
        "Queue the next research before current one finishes. Use speed-ups strategically. "
        "Focus: Combat > Galaxy > Station for PvP progression.",
        TaskCategory::Research, 100, false,
        3, "morning",
        {"daily", "research", "queue"});

    add("Start/queue building upgrades",
        "Two builder slots should always be active. At G6 builds are 7-20+ days. "
        "Prioritize: Ops prereqs > Research Lab > Drydock > Academy > Defense platforms.",
        TaskCategory::SpeedUps, 100, false,
        3, "morning",
        {"daily", "building", "queue"});

    // --- High-impact progression (impact 85-90) ---

    add("Check/progress active events + milestones",
        "Solo events, alliance events, and arcs run constantly. Check requirements and "
        "plan your dailies around maximizing event points. Events are the #1 source of "
        "premium rewards at G6. Claim completed milestones promptly.",
        TaskCategory::Events, 90, true,
        10, "morning",
        {"daily", "events", "arc", "milestones"});

    add("Use officer XP and promote officers",
        "Spend accumulated officer XP on priority officers. At G6, focus on officers "
        "you actually use in crews. Check if any officers are ready to rank up (promote). "
        "Promoting is gated by shards, not XP.",
        TaskCategory::Officers, 85, false,
        5, "anytime",
        {"daily", "officers", "xp", "promote"});

    add("Ship XP: level priority ship",
        "Accumulated ship XP should be spent on your priority ships. "
        "At G6: focus on your main PvP ship, then armada ship, then dailies ship. "
        "Don't spread XP thin across too many ships.",
        TaskCategory::Ships, 85, false,
        5, "anytime",
        {"daily", "ships", "xp"});

    // --- Core daily grind (impact 75-80) ---

    add("Run daily armada",
        "Alliance armadas give high-value loot: uncommon/rare ship BPs, officer shards, "
        "armada credits. At G6, run Eclipse or higher armadas. Coordinate with alliance.",
        TaskCategory::Combat, 80, false,
        15, "alliance schedule",
        {"daily", "armada", "alliance"});

    add("Complete daily goals (3/3)",
        "Three daily goals for rewards. Usually: kill hostiles, complete a mission, "
        "donate to alliance. These stack into weekly goals. Never skip.",
        TaskCategory::Combat, 75, false,
        15, "anytime",
        {"daily", "goals", "hostiles"}, 3);

    add("Kill daily hostile target count",
        "G6 hostile grinding is essential for XP, loot, and event points. "
        "Run your best PvE crew. Target hostiles at your max efficient level. "
        "Use auto-combat for efficiency.",
        TaskCategory::Combat, 75, false,
        30, "anytime",
        {"daily", "hostiles", "pve", "xp"});

    // --- Resource & strategy (impact 60-70) ---

    add("Use speed-ups strategically",
        "At G6, speed-ups are precious. Use them to finish research/builds just before "
        "events end or to hit milestones. Don't waste speed-ups on non-critical items. "
        "Save big speed-ups for events that reward based on speed-up usage.",
        TaskCategory::SpeedUps, 70, false,
        5, "when needed",
        {"daily", "speedups", "strategy"});

    add("Deploy mining ships (3-4 ships)",
        "At G6 you need millions of resources daily. Keep all survey ships on nodes 24/7. "
        "Focus on whatever resource your current research/build needs most. "
        "Protected cargo > raw volume at G6.",
        TaskCategory::Mining, 70, false,
        5, "morning",
        {"daily", "mining", "resources"});

    add("Collect generators + refinery",
        "Parsteel/Trit/Dil generators cap out over time. At G6 these are significant. "
        "Also check refinery: queue all 3 slots. Refined resources gate late-G6 content.",
        TaskCategory::Mining, 65, true,
        2, "morning",
        {"daily", "generators", "refinery", "resources"});

    add("Alliance: helps + gifts",
        "Send helps to reduce build/research time for alliance members. "
        "Open alliance gifts before they expire. Free resources and speed-ups. "
        "At G6, alliance cooperation is everything.",
        TaskCategory::Alliance, 60, true,
        3, "throughout day",
        {"daily", "alliance", "helps", "gifts"});

    add("PvP: defend or raid",
        "At G6 PvP is core gameplay. Either defend your mining nodes from raiders "
        "or go raiding. Use the best PvP crew from the optimizer. "
        "Pick battles wisely -- repair costs are steep at G6.",
        TaskCategory::Combat, 60, false,
        20, "peak hours",
        {"daily", "pvp", "combat"});

    // --- Housekeeping (impact 30-50) ---

    add("Away missions",
        "Away team missions give officer shards, speed-ups, and resources. "
        "At G6, scan for epic missions. Send highest-power away teams.",
        TaskCategory::Combat, 50, false,
        5, "anytime",
        {"daily", "missions", "away"});

    add("Check alliance + faction stores",
        "Alliance store refreshes with officer shards, speed-ups, and resources. "
        "Buy priority officer shards first, then speed-ups. Check faction stores too.",
        TaskCategory::Store, 45, true,
        3, "after reset",
        {"daily", "store", "alliance", "faction"});

    add("Claim daily login rewards",
        "Check the daily login calendar. Claim any milestone rewards. "
        "Missing a day breaks streak bonuses.",
        TaskCategory::Store, 40, true,
        1, "morning",
        {"daily", "login", "free"});

    add("Collect free store packs",
        "Free energy, speed-ups, and resource packs in the store reset daily. "
        "Check Featured, Resources, and Offers tabs. Don't leave free stuff on the table.",
        TaskCategory::Store, 40, true,
        2, "morning",
        {"daily", "store", "free"});

    add("Reorganize ship crews (if roster changed)",
        "If you unlocked new officers or leveled existing ones, re-run the crew optimizer. "
        "Update crews across all 7 docks.",
        TaskCategory::Officers, 30, false,
        10, "evening",
        {"daily", "crews", "optimizer"});
}

// ---------------------------------------------------------------------------
// Weekly goal templates — G6 progression targets
// ---------------------------------------------------------------------------

void Planner::init_weekly_templates() {
    auto add = [this](
        const char* title, const char* desc,
        TaskCategory cat, TaskPriority pri,
        int total, const char* metric,
        std::initializer_list<const char*> tags_init = {}) {

        WeeklyGoal g;
        g.id = next_goal_id_++;
        g.title = title;
        g.description = desc;
        g.category = cat;
        g.priority = pri;
        g.progress_total = total;
        g.metric = metric;
        for (auto tag : tags_init) g.tags.insert(tag);
        weekly_templates_.push_back(std::move(g));
    };

    // === Research & Building ===
    add("Complete 1+ research nodes",
        "At G6 research takes 10-30 days. Aim to finish at least one per week "
        "using speed-ups strategically. Combat and Galaxy research are priority.",
        TaskCategory::Research, TaskPriority::High,
        1, "research_completed",
        {"weekly", "research", "progression"});

    add("Make building progress (2+ levels)",
        "Keep both builder slots running. At G6 every building level matters "
        "for Ops prerequisites. Target 2+ level completions per week.",
        TaskCategory::SpeedUps, TaskPriority::High,
        2, "building_levels",
        {"weekly", "building", "progression"});

    // === Ships ===
    add("Gain 1+ ship tier on priority ship",
        "Ship tiering is the main power progression at G6. Focus resources "
        "on your main PvP ship first. Each tier takes significant components.",
        TaskCategory::Ships, TaskPriority::High,
        1, "ship_tiers",
        {"weekly", "ships", "tiering"});

    add("Level priority ship 5+ levels",
        "Ship XP from hostiles and scrapping. Focus on 1-2 priority ships. "
        "Don't spread thin.",
        TaskCategory::Ships, TaskPriority::Medium,
        5, "ship_levels",
        {"weekly", "ships", "leveling"});

    // === Officers ===
    add("Rank up 1+ officer",
        "Officer promotions require shards from events, faction stores, and packs. "
        "Focus on officers in your main PvP/PvE crews.",
        TaskCategory::Officers, TaskPriority::High,
        1, "officer_ranks",
        {"weekly", "officers", "ranking"});

    add("Level 3+ officers to new breakpoints",
        "Officer level breakpoints increase ability values. Target leveling "
        "officers that are close to the next breakpoint.",
        TaskCategory::Officers, TaskPriority::Medium,
        3, "officer_levels",
        {"weekly", "officers", "leveling"});

    // === Combat ===
    add("Complete all 7 daily goal sets",
        "Each day has 3 daily goals (21 total per week). Complete all 7 days "
        "for maximum weekly reward tier.",
        TaskCategory::Combat, TaskPriority::High,
        7, "daily_goal_sets",
        {"weekly", "dailies", "goals"});

    add("Run 5+ armadas",
        "Armada loot is critical for ship progression. Run at least 5 per week. "
        "Eclipse and higher armadas give the best G6 returns.",
        TaskCategory::Combat, TaskPriority::High,
        5, "armadas_run",
        {"weekly", "armadas", "loot"});

    add("Kill 200+ hostiles",
        "Hostile grinding is base XP income. At G6 target the highest level "
        "you can one-shot for efficiency. Auto-combat everything.",
        TaskCategory::Combat, TaskPriority::Medium,
        200, "hostiles_killed",
        {"weekly", "hostiles", "grinding"});

    // === Mining ===
    add("Mine 10M+ total resources",
        "Keep survey ships on nodes 24/7. At G6 you need enormous volumes "
        "of resources. Focus on whatever bottlenecks your current build/research.",
        TaskCategory::Mining, TaskPriority::Medium,
        10, "mining_millions",
        {"weekly", "mining", "resources"});

    add("Refine 5+ batches",
        "Refinery batches convert raw to refined resources. Keep all 3 slots "
        "running continuously. Refined resources gate late-G6 content.",
        TaskCategory::Mining, TaskPriority::Medium,
        5, "refinery_batches",
        {"weekly", "refinery", "resources"});

    // === Events ===
    add("Hit top milestone in active events",
        "Events rotate weekly. Plan resource spending and activity around "
        "hitting the highest milestone tier possible. Premium rewards are "
        "concentrated in top milestones.",
        TaskCategory::Events, TaskPriority::High,
        1, "event_milestones",
        {"weekly", "events", "milestones"});

    add("Complete current arc chapter",
        "Arc progression unlocks permanent bonuses and rewards. "
        "Work through arc requirements systematically each week.",
        TaskCategory::Events, TaskPriority::Medium,
        1, "arc_chapters",
        {"weekly", "arc", "story"});

    // === Alliance ===
    add("Participate in all alliance events",
        "Alliance events require coordinated effort. Participate in every "
        "one for personal and alliance rewards. Communication is key.",
        TaskCategory::Alliance, TaskPriority::Medium,
        1, "alliance_events",
        {"weekly", "alliance", "events"});

    add("Donate to alliance research",
        "Alliance research buffs benefit everyone. Donate excess resources "
        "to priority alliance research. Focus on the research that benefits "
        "your current focus (combat, mining, etc).",
        TaskCategory::Alliance, TaskPriority::Low,
        1, "alliance_donations",
        {"weekly", "alliance", "research"});

    // === Strategy ===
    add("Review and optimize crew loadouts",
        "Run the crew optimizer for all 7 docks. Update crews based on "
        "newly leveled officers or changed priorities. Test new combos.",
        TaskCategory::Officers, TaskPriority::Medium,
        1, "crew_reviews",
        {"weekly", "crews", "optimizer"});

    add("Plan next week's progression targets",
        "Evaluate what you accomplished this week. Set targets for next week: "
        "which research to start, which ship to tier, which officers to focus.",
        TaskCategory::Misc, TaskPriority::Low,
        1, "planning_sessions",
        {"weekly", "planning", "strategy"});
}

// ---------------------------------------------------------------------------
// Plan generation
// ---------------------------------------------------------------------------

DailyPlan Planner::generate_daily_plan() const {
    return generate_daily_plan_for(today_dow(), today_str());
}

DailyPlan Planner::generate_daily_plan_for(int day_of_week,
                                            const std::string& date) const {
    DailyPlan plan;
    plan.date = date;
    plan.day_of_week = day_of_week;

    // Copy all daily templates, filter by day_of_week if applicable
    for (const auto& tmpl : daily_templates_) {
        if (tmpl.day_of_week >= 0 && tmpl.day_of_week != day_of_week) continue;
        plan.tasks.push_back(tmpl);
    }

    // Sort by effective_score (impact + dynamic boost), highest first
    // Urgency is NOT a sort key — it's a badge shown in the UI
    std::sort(plan.tasks.begin(), plan.tasks.end(),
              [](const PlannerTask& a, const PlannerTask& b) {
                  int sa = a.effective_score();
                  int sb = b.effective_score();
                  if (sa != sb) return sa > sb;
                  // Tiebreaker: urgent tasks first among equal scores
                  if (a.urgent != b.urgent) return a.urgent > b.urgent;
                  return a.impact_score > b.impact_score;
              });

    return plan;
}

WeeklyPlan Planner::generate_weekly_plan() const {
    WeeklyPlan plan;
    plan.week_start = monday_of_week();

    // Generate 7 daily plans (Mon=1 .. Sun=0)
    for (int i = 0; i < 7; ++i) {
        int dow = (i + 1) % 7;  // Mon=1, Tue=2, ... Sat=6, Sun=0

        // Calculate date string for each day
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        int today = t.tm_wday;
        int days_since_monday = (today == 0) ? 6 : today - 1;
        time_t day_time = now + (i - days_since_monday) * 86400;
        localtime_r(&day_time, &t);
        char buf[16];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &t);

        plan.days.push_back(generate_daily_plan_for(dow, buf));
    }

    // Copy weekly goal templates
    plan.goals = weekly_templates_;

    return plan;
}

// ---------------------------------------------------------------------------
// Task management
// ---------------------------------------------------------------------------

void Planner::toggle_task(DailyPlan& plan, int task_id) {
    for (auto& t : plan.tasks) {
        if (t.id == task_id) {
            t.completed = !t.completed;
            if (t.completed) t.skipped = false;
            break;
        }
    }
}

void Planner::skip_task(DailyPlan& plan, int task_id, const std::string& reason) {
    for (auto& t : plan.tasks) {
        if (t.id == task_id) {
            t.skipped = !t.skipped;
            t.skip_reason = reason;
            if (t.skipped) t.completed = false;
            break;
        }
    }
}

void Planner::update_goal_progress(WeeklyPlan& plan, int goal_id, int progress) {
    for (auto& g : plan.goals) {
        if (g.id == goal_id) {
            g.progress_current = std::min(progress, g.progress_total);
            g.completed = (g.progress_current >= g.progress_total);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Enrich plan with player data context
// ---------------------------------------------------------------------------

void Planner::enrich_plan_with_player_data(DailyPlan& plan,
                                            const PlayerData& pd,
                                            const GameData& gd) const {
    // Pre-classify jobs by type
    std::vector<const PlayerJob*> research_jobs, building_jobs, ship_jobs, officer_jobs;
    for (const auto& j : pd.jobs) {
        if (j.completed) continue;
        switch (j.job_type) {
            case 1: research_jobs.push_back(&j); break;
            case 2: building_jobs.push_back(&j); break;
            case 3: case 4: ship_jobs.push_back(&j); break;
            case 5: officer_jobs.push_back(&j); break;
        }
    }

    // Find officers near rank-up: those with shard_count that might be enough
    // Typical shard requirements by rank: rank2=10, rank3=30, rank4=100, rank5=200, rank6=400
    static const int rank_shard_thresholds[] = {0, 0, 10, 30, 100, 200, 400};
    std::vector<std::string> rankup_hints;
    for (const auto& po : pd.officers) {
        if (po.rank < 1 || po.rank > 5) continue;
        int next_rank = po.rank + 1;
        if (next_rank > 6) continue;
        int needed = (next_rank <= 6) ? rank_shard_thresholds[next_rank] : 9999;
        if (po.shard_count >= needed && needed > 0) {
            rankup_hints.push_back(po.name + " (Rk" + std::to_string(po.rank) +
                                   " -> " + std::to_string(next_rank) +
                                   ", " + std::to_string(po.shard_count) + " shards)");
        }
    }

    // Identify key resource amounts (parsteel, tritanium, dilithium by name pattern)
    auto find_resource_amount = [&](const std::string& pattern) -> int64_t {
        for (const auto& r : pd.resources) {
            // Case-insensitive contains check
            std::string lower_name = r.name;
            for (auto& c : lower_name) c = static_cast<char>(std::tolower(c));
            std::string lower_pat = pattern;
            for (auto& c : lower_pat) c = static_cast<char>(std::tolower(c));
            if (lower_name.find(lower_pat) != std::string::npos) return r.amount;
        }
        return -1;
    };

    // Format resource amount with K/M/B suffix
    auto fmt_amount = [](int64_t v) -> std::string {
        if (v < 0) return "?";
        if (v >= 1000000000LL) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.1fB", v / 1e9); return buf;
        }
        if (v >= 1000000LL) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.1fM", v / 1e6); return buf;
        }
        if (v >= 1000LL) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.1fK", v / 1e3); return buf;
        }
        return std::to_string(v);
    };

    // Find top ships by tier
    std::vector<std::pair<std::string, int>> top_ships;
    for (const auto& ps : pd.ships) {
        top_ships.push_back({ps.name, ps.tier});
    }
    std::sort(top_ships.begin(), top_ships.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Now enrich each task based on its tags/category
    for (auto& task : plan.tasks) {
        task.context_hints.clear();
        task.has_active_job = false;
        task.queue_idle = false;
        task.dynamic_boost = 0;  // Reset boost each enrichment pass

        // --- Research tasks ---
        if (task.tags.count("research") || task.category == TaskCategory::Research) {
            if (research_jobs.empty()) {
                task.queue_idle = true;
                task.dynamic_boost += 50;  // IDLE QUEUE: massive boost to top of list
                task.context_hints.push_back("!! RESEARCH QUEUE IDLE - start something!");
            } else {
                for (const auto* j : research_jobs) {
                    task.has_active_job = true;
                    int remaining = job_remaining_seconds(*j);
                    std::string name = "Research";
                    // Try to resolve research name
                    if (j->research_id > 0) {
                        auto it = gd.researches.find(j->research_id);
                        if (it != gd.researches.end() && !it->second.name.empty()) {
                            name = it->second.name;
                            if (j->level > 0) name += " Lv" + std::to_string(j->level);
                        }
                    }
                    if (remaining <= 0) {
                        task.dynamic_boost += 20;  // Job done, collect & queue next
                        task.context_hints.push_back(name + " - DONE! Collect & start next");
                    } else {
                        task.context_hints.push_back(name + " - " + format_duration_short(remaining) + " left");
                    }
                }
            }
            // Show total research nodes
            if (!pd.researches.empty()) {
                task.context_hints.push_back("You have " + std::to_string(pd.researches.size()) + " researched nodes");
            }
        }

        // --- Building tasks ---
        if (task.tags.count("building") || task.category == TaskCategory::SpeedUps) {
            if (task.tags.count("building") || task.title.find("building") != std::string::npos
                || task.title.find("Building") != std::string::npos
                || task.title.find("queue") != std::string::npos) {
                if (building_jobs.empty()) {
                    task.queue_idle = true;
                    task.dynamic_boost += 50;  // IDLE QUEUE: massive boost
                    task.context_hints.push_back("!! BUILD QUEUE IDLE - start something!");
                } else {
                    for (const auto* j : building_jobs) {
                        task.has_active_job = true;
                        int remaining = job_remaining_seconds(*j);
                        std::string name = "Building";
                        if (remaining <= 0) {
                            task.dynamic_boost += 20;  // Job done
                            task.context_hints.push_back(name + " job - DONE! Collect & start next");
                        } else {
                            task.context_hints.push_back(name + " job - " + format_duration_short(remaining) + " left");
                        }
                    }
                }
                // Show ops level
                if (pd.ops_level > 0) {
                    task.context_hints.push_back("Ops Level: " + std::to_string(pd.ops_level));
                }
            }
        }

        // --- Officer tasks ---
        if (task.tags.count("officers") || task.category == TaskCategory::Officers) {
            if (!rankup_hints.empty()) {
                task.dynamic_boost += 30;  // Officers ready to promote!
                task.context_hints.push_back("Officers ready to rank up:");
                int shown = 0;
                for (const auto& h : rankup_hints) {
                    task.context_hints.push_back("  " + h);
                    if (++shown >= 5) {
                        if (rankup_hints.size() > 5) {
                            task.context_hints.push_back("  ...and " +
                                std::to_string(rankup_hints.size() - 5) + " more");
                        }
                        break;
                    }
                }
            } else if (!pd.officers.empty()) {
                task.context_hints.push_back("No officers ready for rank-up (need more shards)");
            }
            if (!pd.officers.empty()) {
                // Show highest level officers
                auto sorted = pd.officers;
                std::sort(sorted.begin(), sorted.end(),
                          [](const auto& a, const auto& b) { return a.level > b.level; });
                std::string top3;
                for (int i = 0; i < 3 && i < (int)sorted.size(); ++i) {
                    if (i > 0) top3 += ", ";
                    top3 += sorted[i].name + " Lv" + std::to_string(sorted[i].level);
                }
                task.context_hints.push_back("Top officers: " + top3);
            }
        }

        // --- Ship tasks ---
        if (task.tags.count("ships") || task.category == TaskCategory::Ships) {
            if (!ship_jobs.empty()) {
                for (const auto* j : ship_jobs) {
                    task.has_active_job = true;
                    int remaining = job_remaining_seconds(*j);
                    std::string label = (j->job_type == 3) ? "Ship Build" : "Ship Upgrade";
                    if (remaining > 0) {
                        task.context_hints.push_back(label + " - " + format_duration_short(remaining) + " left");
                    } else {
                        task.context_hints.push_back(label + " - DONE!");
                    }
                }
            }
            // Show top ships by tier
            if (!top_ships.empty()) {
                std::string top;
                for (int i = 0; i < 3 && i < (int)top_ships.size(); ++i) {
                    if (i > 0) top += ", ";
                    top += top_ships[i].first + " T" + std::to_string(top_ships[i].second);
                }
                task.context_hints.push_back("Top ships: " + top);
            }
        }

        // --- Mining/resource tasks ---
        if (task.tags.count("mining") || task.tags.count("resources") ||
            task.category == TaskCategory::Mining) {
            // Show key resource amounts
            int64_t parsteel = find_resource_amount("parsteel");
            int64_t tritanium = find_resource_amount("tritanium");
            int64_t dilithium = find_resource_amount("dilithium");
            if (parsteel >= 0 || tritanium >= 0 || dilithium >= 0) {
                std::string res_line;
                if (parsteel >= 0) res_line += "Par:" + fmt_amount(parsteel);
                if (tritanium >= 0) {
                    if (!res_line.empty()) res_line += "  ";
                    res_line += "Tri:" + fmt_amount(tritanium);
                }
                if (dilithium >= 0) {
                    if (!res_line.empty()) res_line += "  ";
                    res_line += "Dil:" + fmt_amount(dilithium);
                }
                task.context_hints.push_back(res_line);
            }
            // Show total resource types tracked
            if (!pd.resources.empty()) {
                task.context_hints.push_back(std::to_string(pd.resources.size()) + " resource types in inventory");
            }
        }

        // --- Speed-up tasks ---
        if (task.tags.count("speedups") || task.tags.count("strategy")) {
            // Show all active jobs that speed-ups could apply to
            int total_active = static_cast<int>(research_jobs.size() + building_jobs.size() + ship_jobs.size());
            if (total_active > 0) {
                task.context_hints.push_back(std::to_string(total_active) + " active job(s) that can be sped up");
                for (const auto* j : research_jobs) {
                    int rem = job_remaining_seconds(*j);
                    if (rem > 0) task.context_hints.push_back("  Research: " + format_duration_short(rem) + " left");
                }
                for (const auto* j : building_jobs) {
                    int rem = job_remaining_seconds(*j);
                    if (rem > 0) task.context_hints.push_back("  Building: " + format_duration_short(rem) + " left");
                }
            } else {
                task.context_hints.push_back("No active jobs - save speed-ups for later");
            }
        }
    }

    // Re-sort with dynamic boosts applied — tasks with idle queues / ready
    // rank-ups will now bubble to the top above their base impact tier
    std::sort(plan.tasks.begin(), plan.tasks.end(),
              [](const PlannerTask& a, const PlannerTask& b) {
                  int sa = a.effective_score();
                  int sb = b.effective_score();
                  if (sa != sb) return sa > sb;
                  if (a.urgent != b.urgent) return a.urgent > b.urgent;
                  return a.impact_score > b.impact_score;
              });

    // Update derived priority field for display/persistence
    for (auto& t : plan.tasks) {
        t.priority = t.display_priority();
    }
}

// ---------------------------------------------------------------------------
// JSON persistence
// ---------------------------------------------------------------------------

using json = nlohmann::json;

static json task_to_json(const PlannerTask& t) {
    json j;
    j["id"] = t.id;
    j["completed"] = t.completed;
    j["skipped"] = t.skipped;
    j["skip_reason"] = t.skip_reason;
    j["progress_current"] = t.progress_current;
    return j;
}

static void task_from_json(PlannerTask& t, const json& j) {
    if (j.contains("completed")) t.completed = j["completed"].get<bool>();
    if (j.contains("skipped")) t.skipped = j["skipped"].get<bool>();
    if (j.contains("skip_reason")) t.skip_reason = j["skip_reason"].get<std::string>();
    if (j.contains("progress_current")) t.progress_current = j["progress_current"].get<int>();
}

static json goal_to_json(const WeeklyGoal& g) {
    json j;
    j["id"] = g.id;
    j["progress_current"] = g.progress_current;
    j["completed"] = g.completed;
    return j;
}

static void goal_from_json(WeeklyGoal& g, const json& j) {
    if (j.contains("progress_current")) g.progress_current = j["progress_current"].get<int>();
    if (j.contains("completed")) g.completed = j["completed"].get<bool>();
}

bool Planner::save_daily(const DailyPlan& plan, const std::string& path) const {
    json j;
    j["date"] = plan.date;
    j["day_of_week"] = plan.day_of_week;
    json tasks_arr = json::array();
    for (const auto& t : plan.tasks) {
        tasks_arr.push_back(task_to_json(t));
    }
    j["tasks"] = tasks_arr;

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return f.good();
}

bool Planner::load_daily(DailyPlan& plan, const std::string& path) const {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        json j = json::parse(f);
        if (j.contains("date") && j["date"].get<std::string>() != plan.date) {
            return false;  // Stale data from different day
        }

        if (j.contains("tasks") && j["tasks"].is_array()) {
            // Build id -> saved state map
            std::map<int, json> saved;
            for (const auto& tj : j["tasks"]) {
                if (tj.contains("id")) saved[tj["id"].get<int>()] = tj;
            }
            // Apply saved state to matching tasks
            for (auto& t : plan.tasks) {
                auto it = saved.find(t.id);
                if (it != saved.end()) {
                    task_from_json(t, it->second);
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool Planner::save_weekly(const WeeklyPlan& plan, const std::string& path) const {
    json j;
    j["week_start"] = plan.week_start;

    // Save each day's task state
    json days_arr = json::array();
    for (const auto& day : plan.days) {
        json dj;
        dj["date"] = day.date;
        json tasks_arr = json::array();
        for (const auto& t : day.tasks) {
            tasks_arr.push_back(task_to_json(t));
        }
        dj["tasks"] = tasks_arr;
        days_arr.push_back(dj);
    }
    j["days"] = days_arr;

    // Save goals
    json goals_arr = json::array();
    for (const auto& g : plan.goals) {
        goals_arr.push_back(goal_to_json(g));
    }
    j["goals"] = goals_arr;

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return f.good();
}

bool Planner::load_weekly(WeeklyPlan& plan, const std::string& path) const {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        json j = json::parse(f);
        if (j.contains("week_start") &&
            j["week_start"].get<std::string>() != plan.week_start) {
            return false;  // Stale data from different week
        }

        // Load day states
        if (j.contains("days") && j["days"].is_array()) {
            auto& days_arr = j["days"];
            for (size_t d = 0; d < days_arr.size() && d < plan.days.size(); ++d) {
                auto& dj = days_arr[d];
                if (dj.contains("tasks") && dj["tasks"].is_array()) {
                    std::map<int, json> saved;
                    for (const auto& tj : dj["tasks"]) {
                        if (tj.contains("id")) saved[tj["id"].get<int>()] = tj;
                    }
                    for (auto& t : plan.days[d].tasks) {
                        auto it = saved.find(t.id);
                        if (it != saved.end()) {
                            task_from_json(t, it->second);
                        }
                    }
                }
            }
        }

        // Load goal states
        if (j.contains("goals") && j["goals"].is_array()) {
            std::map<int, json> saved;
            for (const auto& gj : j["goals"]) {
                if (gj.contains("id")) saved[gj["id"].get<int>()] = gj;
            }
            for (auto& g : plan.goals) {
                auto it = saved.find(g.id);
                if (it != saved.end()) {
                    goal_from_json(g, it->second);
                }
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

} // namespace stfc
