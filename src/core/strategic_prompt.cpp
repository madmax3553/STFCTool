#include "core/strategic_prompt.h"

#include <algorithm>
#include <sstream>

namespace stfc {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string join_strings(const std::vector<std::string>& items, const std::string& sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) oss << sep;
        oss << items[i];
    }
    return oss.str();
}

static std::string summarize_active_jobs(const PlayerData& pd) {
    std::vector<std::string> lines;
    for (const auto& job : pd.jobs) {
        if (job.completed) continue;
        std::ostringstream line;
        line << job_type_str(job.job_type) << " L" << job.level
             << " (" << format_duration_short(job_remaining_seconds(job)) << " remaining)";
        lines.push_back(line.str());
    }
    return lines.empty() ? "none" : join_strings(lines, "; ");
}

static std::string summarize_primary_ships(const PlayerData& pd) {
    if (pd.ships.empty()) return "none";
    auto ships = pd.ships;
    std::sort(ships.begin(), ships.end(), [](const PlayerShip& a, const PlayerShip& b) {
        if (a.tier != b.tier) return a.tier > b.tier;
        return a.level > b.level;
    });
    std::vector<std::string> lines;
    for (int i = 0; i < std::min(5, (int)ships.size()); ++i) {
        const auto& s = ships[i];
        std::ostringstream line;
        line << s.name << " T" << s.tier << " L" << s.level;
        lines.push_back(line.str());
    }
    return join_strings(lines, ", ");
}

static std::string summarize_resources(const PlayerData& pd) {
    if (pd.resources.empty()) return "none";
    auto resources = pd.resources;
    std::sort(resources.begin(), resources.end(), [](const PlayerResource& a, const PlayerResource& b) {
        return a.amount > b.amount;
    });
    std::vector<std::string> lines;
    for (int i = 0; i < std::min(8, (int)resources.size()); ++i) {
        if (resources[i].amount <= 0) continue;
        std::ostringstream line;
        line << (resources[i].name.empty() ? ("Resource#" + std::to_string(resources[i].resource_id)) : resources[i].name)
             << ": " << resources[i].amount;
        lines.push_back(line.str());
    }
    return lines.empty() ? "none" : join_strings(lines, ", ");
}

// ---------------------------------------------------------------------------
// Data builder
// ---------------------------------------------------------------------------

nlohmann::json build_strategic_assessment_data(const PlayerData& pd) {
    nlohmann::json data;
    data["ops_level"] = pd.ops_level;
    data["active_jobs"] = summarize_active_jobs(pd);
    data["docks"] = "unknown from current sync data";
    data["primary_ships"] = summarize_primary_ships(pd);
    data["inventory_stacks"] = pd.inventory.size();
    data["resources"] = summarize_resources(pd);
    return data;
}

// ---------------------------------------------------------------------------
// Request builder
// ---------------------------------------------------------------------------

LlmRequest build_strategic_assessment_request(const PlayerData& pd) {
    LlmRequest req;
    req.system_prompt = R"(You are the STFC Strategic Command Intelligence. Your goal is to maximize the growth and event efficiency of a Star Trek Fleet Command account.

CORE LOGIC:
1. ROI (Return on Investment): Prioritize actions that create the most account progress with the least waste.
2. Efficiency: Do not recommend spending speed-ups, XP, or large resources unless there is a clear progression reason or the available data strongly supports it.
3. Urgency: Give higher priority to actions blocked by active timers or near-term progression bottlenecks.

CONSTRAINTS:
- Only reference data provided in the DATA sections.
- Do not assume event data exists if it is marked missing.
- If the user has a Personal Focus, align all objectives to that goal.
- If critical live data is missing, say so explicitly in the response reasoning.

Respond with ONLY valid JSON, no other text:
{
  "daily_summary": "A 2-sentence overview of today's account health.",
  "objectives": [
    {
      "priority": 1,
      "title": "Objective Title",
      "category": "Spending | Combat | Mining | Progression",
      "urgency_score": 1,
      "reasoning": "Why this is a good move based on current account data.",
      "expected_outcome": "What account progress this should unlock or improve.",
      "required_resources": ["List of critical materials, timers, or ships needed"]
    }
  ],
  "hoarding_advice": "What resource should I avoid spending today if the current data does not justify it.",
  "limitations": "What important missing data reduces confidence in the recommendation."
})";
    req.temperature = 0.3;
    req.max_tokens = 4096;

    auto data = build_strategic_assessment_data(pd);

    std::ostringstream user;
    user << "### DATA: ACCOUNT SNAPSHOT\n";
    user << "- Ops Level: " << pd.ops_level << "\n";
    user << "- Active Jobs: " << data["active_jobs"].get<std::string>() << "\n";
    user << "- Docks: " << data["docks"].get<std::string>() << "\n";
    user << "- Primary Ships: " << data["primary_ships"].get<std::string>() << "\n\n";

    user << "### DATA: ACTIVE EVENTS & MILESTONES\n";
    user << "Unavailable in current sync data. Do not assume live events or milestone thresholds.\n\n";

    user << "### DATA: INVENTORY SNAPSHOT\n";
    user << "- Inventory Items: " << pd.inventory.size() << " synced item stacks (not yet categorized into speedups/xp buckets)\n";
    user << "- Resources: " << data["resources"].get<std::string>() << "\n\n";

    user << "### USER PERSONAL FOCUS\n";
    user << "\"General account growth and efficiency\"\n\n";

    user << "### TASK\n";
    user << "Generate a strategic assessment using ONLY the available account data. If event-driven advice is not possible, prioritize safe progression and explain the limitation.\n";
    req.user_prompt = user.str();
    req.response_schema =
        R"({"type":"object","properties":{"daily_summary":{"type":"string"},"objectives":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"title":{"type":"string"},"category":{"type":"string"},"urgency_score":{"type":"integer"},"reasoning":{"type":"string"},"expected_outcome":{"type":"string"},"required_resources":{"type":"array","items":{"type":"string"}}},"required":["priority","title","category","urgency_score","reasoning","expected_outcome","required_resources"]}},"hoarding_advice":{"type":"string"},"limitations":{"type":"string"}},"required":["daily_summary","objectives","hoarding_advice","limitations"]})";
    return req;
}

} // namespace stfc
