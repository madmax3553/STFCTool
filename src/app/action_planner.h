#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/account_snapshot.h"

namespace stfc {

struct PlannedResource {
    int64_t resource_id = 0;
    std::string name;
    int64_t amount = 0;
    int64_t owned = 0;
    int64_t missing = 0;
};

struct PlanBlocker {
    std::string type;
    int64_t id = 0;
    std::string name;
    int required_level = 0;
    int current_level = 0;
    bool met = false;
};

struct ResearchCandidate {
    int64_t id = 0;
    std::string name;
    std::string description;
    int64_t research_tree = 0;
    int current_level = 0;
    int next_level = 0;
    int unlock_level = 0;
    int research_time_seconds = 0;
    int64_t military_might = 0;
    double local_score = 0.0;
    double percent_affordable = 0.0;
    bool prerequisites_met = false;
    bool resources_available = false;
    bool can_start_now = false;
    std::vector<PlannedResource> costs;
    std::vector<PlannedResource> missing_resources;
    std::vector<PlanBlocker> blockers;
    std::string reason;
};

struct PlanAction {
    int priority = 0;
    std::string domain;
    std::string action;
    std::string reason;
    bool can_do_now = false;
    int duration_seconds = 0;
    std::vector<PlannedResource> resources_spent;
    std::vector<PlannedResource> missing_resources;
};

struct SaveForTarget {
    std::string target;
    std::string reason;
    std::vector<PlannedResource> missing_resources;
};

struct AvoidAction {
    std::string action;
    std::string reason;
};

struct ActionPlan {
    int64_t generated_at = 0;
    int64_t last_sync = 0;
    int sync_age_seconds = -1;
    int time_budget_minutes = 45;
    std::string focus = "growth";
    std::vector<std::string> warnings;
    std::vector<ResearchCandidate> top_research;
    std::vector<PlanAction> do_now;
    std::vector<SaveForTarget> save_for;
    std::vector<AvoidAction> avoid;
};

std::vector<ResearchCandidate> analyze_research_candidates(
    const FullAccountSnapshot& snapshot,
    int time_budget_minutes = 45,
    const std::string& focus = "growth");

ActionPlan generate_action_plan(const FullAccountSnapshot& snapshot,
                                int time_budget_minutes = 45,
                                const std::string& focus = "growth");

bool save_action_plan(const ActionPlan& plan,
                      const std::string& path = "data/player_data/action_plan.json");

bool load_action_plan(ActionPlan& plan,
                      const std::string& path = "data/player_data/action_plan.json");

} // namespace stfc
