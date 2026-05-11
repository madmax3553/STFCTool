#include "core/strategic_prompt.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

#include "app/account_snapshot.h"
#include "app/action_planner.h"

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

static bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    auto h = haystack;
    auto n = needle;
    std::transform(h.begin(), h.end(), h.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

static nlohmann::json resources_to_prompt_json(const std::vector<PlannedResource>& resources) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& r : resources) {
        out.push_back({
            {"name", r.name},
            {"amount", r.amount},
            {"owned", r.owned},
            {"missing", r.missing},
        });
    }
    return out;
}

static nlohmann::json requirements_to_prompt_json(const std::vector<PlanBlocker>& requirements) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& r : requirements) {
        out.push_back({
            {"type", r.type},
            {"name", r.name},
            {"required", r.required_level},
            {"current", r.current_level},
            {"met", r.met},
        });
    }
    return out;
}

static nlohmann::json ship_context_json(const FullAccountSnapshot& snapshot) {
    auto ships = snapshot.ships;
    std::sort(ships.begin(), ships.end(), [](const ResolvedShip& a, const ResolvedShip& b) {
        if (a.grade != b.grade) return a.grade > b.grade;
        if (a.tier != b.tier) return a.tier > b.tier;
        return a.level > b.level;
    });

    nlohmann::json out = nlohmann::json::array();
    for (size_t i = 0; i < ships.size() && i < 12; ++i) {
        const auto& s = ships[i];
        out.push_back({
            {"name", s.name},
            {"grade", s.grade},
            {"tier", s.tier},
            {"level", s.level},
            {"hull_type", s.hull_type},
        });
    }
    return out;
}

static nlohmann::json research_option_json(const ResearchCandidate& c) {
    std::string state = "save";
    if (c.funding_unknown) state = "verify_cost";
    else if (c.can_start_now) state = "ready_now";
    else if (c.resources_available && c.prerequisites_met) state = "queue_ready";
    else if (!c.prerequisites_met) state = "blocked";
    else if (!c.missing_resources.empty()) state = "needs_resources";

    return {
        {"name", c.name},
        {"is_prime", contains_case_insensitive(c.name, "prime")},
        {"description", c.description},
        {"state", state},
        {"tree", c.research_tree_name},
        {"row", c.row},
        {"column", c.column},
        {"location", c.location},
        {"current_level", c.current_level},
        {"next_level", c.next_level},
        {"unlock_ops", c.unlock_level},
        {"research_time_seconds", c.research_time_seconds},
        {"funding_unknown", c.funding_unknown},
        {"percent_affordable", c.percent_affordable},
        {"costs", resources_to_prompt_json(c.costs)},
        {"missing_resources", resources_to_prompt_json(c.missing_resources)},
        {"requirements", requirements_to_prompt_json(c.requirements)},
    };
}

static nlohmann::json build_research_priority_data(const PlayerData& pd, const GameData& gd) {
    auto player = pd;
    resolve_player_names(player, gd);
    auto snapshot = build_full_snapshot(player, gd);
    auto candidates = analyze_research_candidates(snapshot, 45, "growth");

    std::vector<ResearchCandidate> actionable_options;
    std::vector<ResearchCandidate> verification_options;
    for (const auto& c : candidates) {
        if (!c.prerequisites_met) continue;
        if (c.funding_unknown) {
            verification_options.push_back(c);
            continue;
        }
        if (!c.resources_available && c.percent_affordable < 0.70) continue;
        actionable_options.push_back(c);
    }

    auto board_order = [](const ResearchCandidate& a, const ResearchCandidate& b) {
        if (a.research_tree_name != b.research_tree_name) {
            return a.research_tree_name < b.research_tree_name;
        }
        if (a.row != b.row) return a.row < b.row;
        if (a.column != b.column) return a.column < b.column;
        return a.name < b.name;
    };
    std::sort(actionable_options.begin(), actionable_options.end(), board_order);
    std::sort(verification_options.begin(), verification_options.end(), board_order);

    nlohmann::json actionable = nlohmann::json::array();
    for (size_t i = 0; i < actionable_options.size() && i < 80; ++i) {
        actionable.push_back(research_option_json(actionable_options[i]));
    }

    nlohmann::json verification = nlohmann::json::array();
    for (size_t i = 0; i < verification_options.size() && i < 25; ++i) {
        verification.push_back(research_option_json(verification_options[i]));
    }

    nlohmann::json data;
    data["account"] = {
        {"player_name", snapshot.player_name},
        {"ops_level", snapshot.ops_level},
        {"idle_research_slots", snapshot.idle_research_slots},
        {"active_jobs", summarize_active_jobs(player)},
        {"primary_ships", ship_context_json(snapshot)},
    };
    data["candidate_counts"] = {
        {"actionable_total", actionable_options.size()},
        {"actionable_included", actionable.size()},
        {"verification_total", verification_options.size()},
        {"verification_included", verification.size()},
    };
    data["candidate_filter"] =
        "Actionable options have prerequisites met and are ready or near-affordable. Verification options have prerequisites met but missing cost data; they are not automatic priorities. Both lists are sorted by tree/row/column for board context, not by priority.";
    data["actionable_research_options"] = std::move(actionable);
    data["verification_research_options"] = std::move(verification);
    return data;
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

LlmRequest build_research_priority_request(const PlayerData& pd, const GameData& gd) {
    LlmRequest req;
    req.system_prompt = R"(You are an expert Star Trek Fleet Command research advisor. Your job is to answer: "What research do I need to do today, in order of priority?"

Use the provided research data as evidence. Do not invent research nodes, costs, requirements, trees, or benefits.

PRIORITY LOGIC:
- Priority is account impact first, then feasibility. A research being fully funded or named Prime does not automatically make it high priority.
- Treat "Prime" as a risk flag as well as a possible benefit: Prime currencies/particles are often scarce. A Prime must justify why it matters for this account now.
- Use tree/row/column as board context. Within a tree, farther right and later rows often indicate later-stage research, but this is a clue, not proof. Always combine position with unlock Ops, benefit text, requirements, costs, and current account state.
- If a research benefit is grade-specific, era-specific, or resource-specific (examples: G3 materials, G4 station modules, Charged Nanoprobes, old refinery bonuses), compare it to the account's Ops level, ships, and likely current content before ranking it high.
- If you rank a low-unlock, left-side, or old-grade research above later/right-side options, explicitly explain why it still matters.
- Prefer research that unlocks progression, improves current combat/mining loops, reduces current bottlenecks, or supports ships/content the account actually uses.
- Rank from actionable_research_options first. Use verification_research_options only for "verify in game" recommendations or verification_needed entries.
- If funding data is missing or requirements are unclear, do not mark it as start-now.
- If candidate_counts shows included candidates are truncated, mention that limitation and avoid claiming the omitted candidates were evaluated.

Do not use hidden or made-up numeric scores. Every priority must cite concrete fields from the data: benefit, tree/row/column, Ops unlock, costs/resources, requirements, and account context.

Respond with ONLY valid JSON:
{
  "daily_summary": "2 sentence summary of the research situation.",
  "research_priorities": [
    {
      "priority": 1,
      "name": "exact research name",
      "action": "start now | queue next | save resources | verify in game",
      "why_this_priority": "Evidence-based explanation using the supplied fields.",
      "location": "tree/row/column from data",
      "resource_note": "cost or missing resource note",
      "confidence": "high | medium | low"
    }
  ],
  "defer": [
    {
      "name": "exact research name",
      "reason": "Why this is lower priority or likely wasteful now."
    }
  ],
  "verification_needed": [
    "Specific missing/ambiguous data to check in game."
  ]
})";
    req.temperature = 0.25;
    req.max_tokens = 8192;

    auto data = build_research_priority_data(pd, gd);

    std::ostringstream user;
    user << "### ACCOUNT CONTEXT\n";
    user << data["account"].dump(2) << "\n\n";
    user << "### CANDIDATE COUNTS\n";
    user << data["candidate_counts"].dump(2) << "\n\n";
    user << "### RESEARCH OPTION DATA\n";
    user << "Important: " << data["candidate_filter"].get<std::string>() << "\n";
    user << "The option order below is board/location order, not priority order.\n\n";
    user << "#### ACTIONABLE RESEARCH OPTIONS\n";
    user << data["actionable_research_options"].dump(2) << "\n\n";
    user << "#### VERIFY COST / REQUIREMENT OPTIONS\n";
    user << data["verification_research_options"].dump(2) << "\n\n";
    user << "### TASK\n";
    user << "Answer: What research do I need to do today in order of priority?\n";
    user << "Before ranking any Prime, test whether the benefit still matters for this account. "
         << "For example, a G3 station material Prime or Charged Nanoprobe refinery Prime should not be high priority for a late-game account unless the data shows a current bottleneck that makes it relevant.\n";
    req.user_prompt = user.str();

    req.response_schema =
        R"({"type":"object","properties":{"daily_summary":{"type":"string"},"research_priorities":{"type":"array","items":{"type":"object","properties":{"priority":{"type":"integer"},"name":{"type":"string"},"action":{"type":"string"},"why_this_priority":{"type":"string"},"location":{"type":"string"},"resource_note":{"type":"string"},"confidence":{"type":"string"}},"required":["priority","name","action","why_this_priority","location","resource_note","confidence"]}},"defer":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"reason":{"type":"string"}},"required":["name","reason"]}},"verification_needed":{"type":"array","items":{"type":"string"}}},"required":["daily_summary","research_priorities","defer","verification_needed"]})";
    return req;
}

} // namespace stfc
