#include "core/planner.h"

#include <algorithm>
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
    auto add = [this](
        const char* title, const char* desc,
        TaskCategory cat, TaskPriority pri,
        int minutes, const char* best_time,
        bool time_sensitive, int g6_rel,
        std::initializer_list<const char*> tags_init = {},
        int prog_total = 0) {

        PlannerTask t;
        t.id = next_task_id_++;
        t.title = title;
        t.description = desc;
        t.category = cat;
        t.priority = pri;
        t.estimated_minutes = minutes;
        t.best_time = best_time;
        t.time_sensitive = time_sensitive;
        t.daily = true;
        t.g6_relevance = g6_rel;
        t.progress_total = prog_total;
        for (auto tag : tags_init) t.tags.insert(tag);
        daily_templates_.push_back(std::move(t));
    };

    // === CRITICAL: Time-sensitive daily resets ===

    add("Claim daily login rewards",
        "Check the daily login calendar. Claim any milestone rewards. "
        "Missing a day breaks streak bonuses.",
        TaskCategory::Store, TaskPriority::Critical,
        1, "morning", true, 95,
        {"daily", "login", "free"});

    add("Collect free store packs",
        "Free energy, speed-ups, and resource packs in the store reset daily. "
        "Check Featured, Resources, and Offers tabs. Don't leave free stuff on the table.",
        TaskCategory::Store, TaskPriority::Critical,
        2, "morning", true, 95,
        {"daily", "store", "free"});

    add("Collect all generator outputs",
        "Parsteel/Tritanium/Dilithium generators fill up over time. "
        "At G6 levels these are significant. Collect before they cap out.",
        TaskCategory::Mining, TaskPriority::Critical,
        1, "morning", true, 90,
        {"daily", "generators", "resources"});

    add("Start/queue research",
        "Never let the research queue sit idle. At G6 research times are 10-30+ days. "
        "Queue the next research before current one finishes. Use speed-ups strategically. "
        "Focus: Combat > Galaxy > Station for PvP progression.",
        TaskCategory::Research, TaskPriority::Critical,
        3, "morning", false, 100,
        {"daily", "research", "queue"});

    add("Start/queue building upgrades",
        "Two builder slots should always be active. At G6 builds are 7-20+ days. "
        "Prioritize: Ops prereqs > Research Lab > Drydock > Academy > Defense platforms.",
        TaskCategory::SpeedUps, TaskPriority::Critical,
        3, "morning", false, 100,
        {"daily", "building", "queue"});

    // === HIGH: Core daily grind ===

    add("Complete daily goals (3/3)",
        "Three daily goals for rewards. Usually: kill hostiles, complete a mission, "
        "donate to alliance. These stack into weekly goals. Never skip.",
        TaskCategory::Combat, TaskPriority::High,
        15, "anytime", false, 95,
        {"daily", "goals", "hostiles"}, 3);

    add("Kill daily hostile target count",
        "G6 hostile grinding is essential for XP, loot, and event points. "
        "Run your best PvE crew. Target hostiles at your max efficient level. "
        "Use auto-combat for efficiency.",
        TaskCategory::Combat, TaskPriority::High,
        30, "anytime", false, 90,
        {"daily", "hostiles", "pve", "xp"});

    add("Run daily armada",
        "Alliance armadas give high-value loot: uncommon/rare ship BPs, officer shards, "
        "armada credits. At G6, run Eclipse or higher armadas. Coordinate with alliance.",
        TaskCategory::Combat, TaskPriority::High,
        15, "alliance schedule", false, 90,
        {"daily", "armada", "alliance"});

    add("Send alliance helps",
        "Every help reduces build/research time for alliance members and earns loyalty. "
        "At G6, alliance cooperation is everything. Help everyone, tap the button often.",
        TaskCategory::Alliance, TaskPriority::High,
        2, "throughout day", false, 85,
        {"daily", "alliance", "helps"});

    add("Open alliance gifts",
        "Alliance gifts from chests and member purchases. Free resources, speed-ups, "
        "and officer shards. Check multiple times per day as they expire.",
        TaskCategory::Alliance, TaskPriority::High,
        2, "throughout day", true, 85,
        {"daily", "alliance", "gifts"});

    add("Deploy mining ships (3-4 ships)",
        "At G6 you need millions of resources daily. Keep all survey ships on nodes 24/7. "
        "Focus on whatever resource your current research/build needs most. "
        "Protected cargo > raw volume at G6.",
        TaskCategory::Mining, TaskPriority::High,
        10, "morning", false, 90,
        {"daily", "mining", "resources"});

    add("Check/progress active events",
        "Solo events, alliance events, and arcs run constantly. Check requirements and "
        "plan your dailies around maximizing event points. Events are the #1 source of "
        "premium rewards at G6.",
        TaskCategory::Events, TaskPriority::High,
        10, "morning", false, 95,
        {"daily", "events", "arc"});

    add("Use officer XP and promote officers",
        "Spend accumulated officer XP on priority officers. At G6, focus on officers "
        "you actually use in crews. Check if any officers are ready to rank up (promote). "
        "Promoting is gated by shards, not XP.",
        TaskCategory::Officers, TaskPriority::High,
        5, "anytime", false, 85,
        {"daily", "officers", "xp", "promote"});

    // === MEDIUM: Important but flexible ===

    add("Refine raw resources",
        "Refinery converts raw (G3) resources to refined (G4+). At G6 this is critical "
        "for ship tiering and research. Queue all 3 refinery slots. Check timers.",
        TaskCategory::Mining, TaskPriority::Medium,
        3, "anytime", false, 85,
        {"daily", "refinery", "resources"});

    add("Scan and complete away missions",
        "Away team missions give officer shards, speed-ups, and resources. "
        "At G6, scan for epic missions. Send highest-power away teams.",
        TaskCategory::Combat, TaskPriority::Medium,
        5, "anytime", false, 70,
        {"daily", "missions", "away"});

    add("Collect completed research/build rewards",
        "When research or buildings finish, collect them and immediately start the next. "
        "Dead queue time is wasted progression.",
        TaskCategory::Research, TaskPriority::Medium,
        2, "check periodically", true, 90,
        {"daily", "research", "building", "queue"});

    add("PvP daily: defend or raid",
        "At G6 PvP is core gameplay. Either defend your mining nodes from raiders "
        "or go raiding. Use the best PvP crew from the optimizer. "
        "Pick battles wisely -- repair costs are steep at G6.",
        TaskCategory::Combat, TaskPriority::Medium,
        20, "peak hours", false, 75,
        {"daily", "pvp", "combat"});

    add("Ship leveling: spend ship XP",
        "Accumulated ship XP should be spent on your priority ships. "
        "At G6: focus on your main PvP ship, then armada ship, then dailies ship. "
        "Don't spread XP thin across too many ships.",
        TaskCategory::Ships, TaskPriority::Medium,
        5, "anytime", false, 80,
        {"daily", "ships", "xp"});

    add("Check galaxy map for new systems/events",
        "New systems, armada targets, or special event nodes appear on the galaxy map. "
        "Scout for high-value mining nodes (4* crystal/gas/ore). "
        "Check for displaced hostiles during events.",
        TaskCategory::Misc, TaskPriority::Medium,
        5, "anytime", false, 60,
        {"daily", "exploration", "galaxy"});

    add("Claim event milestone rewards",
        "Events have milestone tiers. Claim completed milestones promptly. "
        "Plan which milestones are reachable and focus effort accordingly.",
        TaskCategory::Events, TaskPriority::Medium,
        3, "check periodically", true, 90,
        {"daily", "events", "milestones"});

    add("Use speed-ups strategically",
        "At G6, speed-ups are precious. Use them to finish research/builds just before "
        "events end or to hit milestones. Don't waste speed-ups on non-critical items. "
        "Save big speed-ups for events that reward based on speed-up usage.",
        TaskCategory::SpeedUps, TaskPriority::Medium,
        5, "when needed", false, 85,
        {"daily", "speedups", "strategy"});

    // === LOW: Nice to do, do if time allows ===

    add("Check alliance store",
        "Alliance store refreshes with officer shards, speed-ups, and resources. "
        "Buy priority officer shards first, then speed-ups.",
        TaskCategory::Store, TaskPriority::Low,
        3, "after reset", true, 70,
        {"daily", "store", "alliance"});

    add("Send gifts to alliance",
        "Costs nothing, builds goodwill. Gift alliance members when prompted.",
        TaskCategory::Alliance, TaskPriority::Low,
        1, "anytime", false, 50,
        {"daily", "alliance", "social"});

    add("Reorganize ship crews",
        "If you unlocked new officers or leveled existing ones, re-run the crew optimizer. "
        "Update crews across all 7 docks.",
        TaskCategory::Officers, TaskPriority::Low,
        10, "evening", false, 65,
        {"daily", "crews", "optimizer"});

    add("Review battle logs",
        "Check recent battle outcomes. Identify losses and why. "
        "Feed data into the crew optimizer's weakness analysis.",
        TaskCategory::Combat, TaskPriority::Low,
        5, "evening", false, 60,
        {"daily", "battlelogs", "analysis"});

    add("Scout enemy bases (PvP intel)",
        "If your alliance is planning operations, scout target bases. "
        "Note defense platform levels, active ships, and alliance tags.",
        TaskCategory::Combat, TaskPriority::Low,
        5, "anytime", false, 40,
        {"daily", "pvp", "scouting"});
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

    // Sort by priority (critical first), then by time sensitivity, then by g6_relevance
    std::sort(plan.tasks.begin(), plan.tasks.end(),
              [](const PlannerTask& a, const PlannerTask& b) {
                  if (a.priority != b.priority) return a.priority < b.priority;
                  if (a.time_sensitive != b.time_sensitive) return a.time_sensitive > b.time_sensitive;
                  return a.g6_relevance > b.g6_relevance;
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
