#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <ctime>
#include <functional>

namespace stfc {

// ---------------------------------------------------------------------------
// Task priority & category
// ---------------------------------------------------------------------------

enum class TaskPriority { Critical, High, Medium, Low };

const char* priority_str(TaskPriority p);
const char* priority_icon(TaskPriority p);

enum class TaskCategory {
    Events,         // Arc, solo, alliance events
    SpeedUps,       // Speed-up management & building/research queues
    Ships,          // Ship progression, tiering, leveling
    Research,       // Research queue management
    Officers,       // Officer leveling, ranking, shards
    Mining,         // Resource farming, refining
    Combat,         // Dailies: hostiles, armadas, missions
    Alliance,       // Alliance help, gifts, diplomacy
    Store,          // Store resets, free claims
    Misc,           // Everything else
};

const char* category_str(TaskCategory c);
const char* category_icon(TaskCategory c);

// ---------------------------------------------------------------------------
// Task definition — a single actionable item in the planner
// ---------------------------------------------------------------------------

struct PlannerTask {
    int id = 0;
    std::string title;
    std::string description;       // Detailed how-to / why
    TaskCategory category = TaskCategory::Misc;
    TaskPriority priority = TaskPriority::Medium;

    bool completed = false;
    bool skipped = false;
    std::string skip_reason;

    // Timing
    int estimated_minutes = 0;     // How long this takes
    std::string best_time;         // e.g. "morning", "during event", "after reset"
    bool time_sensitive = false;   // Must be done at specific time (resets, events)

    // Recurrence
    bool daily = false;            // Resets every day
    bool weekly = false;           // Resets every week
    int day_of_week = -1;          // 0=Sun..6=Sat for weekly tasks (-1=any)
    bool event_only = false;       // Only during active events

    // Dependencies
    std::vector<int> depends_on;   // IDs of tasks that must complete first

    // Progress tracking (for multi-step tasks)
    int progress_current = 0;
    int progress_total = 0;        // 0 = single-step task

    // G6 relevance score (0-100, higher = more important at G6)
    int g6_relevance = 50;

    // Tags for filtering
    std::set<std::string> tags;
};

// ---------------------------------------------------------------------------
// Daily plan — a day's worth of tasks, ordered by priority
// ---------------------------------------------------------------------------

struct DailyPlan {
    std::string date;              // YYYY-MM-DD
    int day_of_week = 0;           // 0=Sun..6=Sat
    std::vector<PlannerTask> tasks;

    // Summary stats
    int total_tasks() const { return static_cast<int>(tasks.size()); }
    int completed_tasks() const;
    int skipped_tasks() const;
    int remaining_tasks() const;
    int total_estimated_minutes() const;
    int remaining_estimated_minutes() const;
    double completion_pct() const;
};

// ---------------------------------------------------------------------------
// Weekly plan — 7 daily plans + weekly-only goals
// ---------------------------------------------------------------------------

struct WeeklyGoal {
    int id = 0;
    std::string title;
    std::string description;
    TaskCategory category = TaskCategory::Misc;
    TaskPriority priority = TaskPriority::Medium;

    int progress_current = 0;
    int progress_total = 1;
    bool completed = false;

    // Target tracking
    std::string metric;            // e.g. "ship_tier", "research_count", "officer_level"
    std::string target_name;       // e.g. "USS Enterprise", "Shield tech", "Kirk"
    int target_value = 0;

    std::set<std::string> tags;
};

struct WeeklyPlan {
    std::string week_start;        // YYYY-MM-DD (Monday)
    std::vector<DailyPlan> days;   // [0]=Mon .. [6]=Sun
    std::vector<WeeklyGoal> goals;

    int completed_goals() const;
    double goal_completion_pct() const;
};

// ---------------------------------------------------------------------------
// Planner engine — generates and manages daily/weekly plans
// ---------------------------------------------------------------------------

class Planner {
public:
    Planner();

    // Generate today's daily plan
    DailyPlan generate_daily_plan() const;

    // Generate this week's plan
    WeeklyPlan generate_weekly_plan() const;

    // Generate a daily plan for a specific day of week (0=Sun..6=Sat)
    DailyPlan generate_daily_plan_for(int day_of_week, const std::string& date) const;

    // Task management
    void toggle_task(DailyPlan& plan, int task_id);
    void skip_task(DailyPlan& plan, int task_id, const std::string& reason);
    void update_goal_progress(WeeklyPlan& plan, int goal_id, int progress);

    // Persistence
    bool save_daily(const DailyPlan& plan, const std::string& path) const;
    bool load_daily(DailyPlan& plan, const std::string& path) const;
    bool save_weekly(const WeeklyPlan& plan, const std::string& path) const;
    bool load_weekly(WeeklyPlan& plan, const std::string& path) const;

    // Access all known task templates
    const std::vector<PlannerTask>& all_daily_tasks() const { return daily_templates_; }
    const std::vector<WeeklyGoal>& all_weekly_goals() const { return weekly_templates_; }

private:
    void init_daily_templates();
    void init_weekly_templates();

    std::vector<PlannerTask> daily_templates_;
    std::vector<WeeklyGoal> weekly_templates_;
    int next_task_id_ = 1;
    int next_goal_id_ = 1;
};

} // namespace stfc
