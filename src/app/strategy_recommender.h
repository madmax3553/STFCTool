#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/account_snapshot.h"
#include "app/action_planner.h"
#include "data/models.h"

namespace stfc {

struct StrategyStep {
    bool done = false;
    std::string label;
    std::string action;
    std::string target;
    std::string system;
    std::string ship;
    std::string crew;
    std::string note;
};

struct StrategyRecommendation {
    int priority = 0;
    std::string lane;       // claim, queue, passive, active, avoid
    std::string decision;   // yes, no, fallback
    std::string title;
    std::string action;
    std::string reason;
    int score = 0;          // 0-100 local confidence/ROI score
    int estimated_minutes = 0;
    bool can_do_now = false;
    bool concurrent = false;
    std::vector<std::string> tags;
    std::vector<StrategyStep> steps;
};

struct StrategyPlan {
    int64_t generated_at = 0;
    int64_t last_sync = 0;
    int sync_age_seconds = -1;
    int time_budget_minutes = 45;
    std::string focus = "ops66_catchup";
    std::vector<std::string> warnings;
    std::vector<StrategyRecommendation> do_now;
    std::vector<StrategyRecommendation> avoid;
};

StrategyPlan generate_strategy_plan(const FullAccountSnapshot& snapshot,
                                    const PlayerData& player_data,
                                    const ActionPlan& action_plan,
                                    int time_budget_minutes = 45,
                                    const std::string& focus = "ops66_catchup");

bool save_strategy_plan(const StrategyPlan& plan,
                        const std::string& path = "data/player_data/strategy_plan.json");

bool load_strategy_plan(StrategyPlan& plan,
                        const std::string& path = "data/player_data/strategy_plan.json");

} // namespace stfc
