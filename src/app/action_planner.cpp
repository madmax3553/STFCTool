#include "app/action_planner.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
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

std::string fallback_name(const std::string& name, const std::string& prefix, int64_t id) {
    return name.empty() ? prefix + std::to_string(id) : name;
}

std::string join_reasons(const std::vector<std::string>& reasons) {
    std::ostringstream out;
    for (size_t i = 0; i < reasons.size(); ++i) {
        if (i > 0) out << "; ";
        out << reasons[i];
    }
    return out.str();
}

std::map<int64_t, int64_t> resource_amounts(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, int64_t> out;
    for (const auto& r : snapshot.resources) out[r.id] = r.amount;
    return out;
}

std::map<int64_t, std::string> resource_names(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, std::string> out;
    for (const auto& r : snapshot.resources) out[r.id] = fallback_name(r.name, "Resource#", r.id);
    return out;
}

std::map<int64_t, int> research_levels(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, int> out;
    for (const auto& r : snapshot.research) out[r.id] = r.current_level;
    return out;
}

std::map<int64_t, std::string> research_names(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, std::string> out;
    for (const auto& r : snapshot.research) out[r.id] = fallback_name(r.name, "Research#", r.id);
    return out;
}

std::map<int64_t, int> building_levels(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, int> out;
    for (const auto& b : snapshot.buildings) out[b.id] = b.current_level;
    return out;
}

std::map<int64_t, std::string> building_names(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, std::string> out;
    for (const auto& b : snapshot.buildings) out[b.id] = fallback_name(b.name, "Building#", b.id);
    return out;
}

std::map<int64_t, int> tech_levels(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, int> out;
    for (const auto& t : snapshot.tech) out[t.tech_id] = t.level;
    return out;
}

std::map<int64_t, int> ship_levels(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, int> out;
    for (const auto& s : snapshot.ships) {
        auto it = out.find(s.hull_id);
        if (it == out.end() || s.level > it->second) out[s.hull_id] = s.level;
    }
    return out;
}

std::map<int64_t, std::string> ship_names(const FullAccountSnapshot& snapshot) {
    std::map<int64_t, std::string> out;
    for (const auto& s : snapshot.ships) {
        if (!s.name.empty()) out[s.hull_id] = s.name;
    }
    return out;
}

int lookup_level(const std::map<int64_t, int>& levels, int64_t id) {
    auto it = levels.find(id);
    return it == levels.end() ? 0 : it->second;
}

std::string lookup_name(const std::map<int64_t, std::string>& names,
                        const std::string& prefix,
                        int64_t id) {
    auto it = names.find(id);
    if (it == names.end() || it->second.empty()) return prefix + std::to_string(id);
    return it->second;
}

PlanBlocker evaluate_requirement(
    const ResearchRequirement& req,
    const std::map<int64_t, int>& b_levels,
    const std::map<int64_t, std::string>& b_names,
    const std::map<int64_t, int>& r_levels,
    const std::map<int64_t, std::string>& r_names,
    const std::map<int64_t, int>& t_levels,
    const std::map<int64_t, int>& s_levels,
    const std::map<int64_t, std::string>& s_names) {
    PlanBlocker blocker;
    blocker.id = req.requirement_id;
    blocker.required_level = req.requirement_level;

    switch (req.requirement_type) {
        case 1:
            blocker.type = "building";
            blocker.name = lookup_name(b_names, "Building#", req.requirement_id);
            blocker.current_level = lookup_level(b_levels, req.requirement_id);
            break;
        case 2:
            blocker.type = "research";
            blocker.name = lookup_name(r_names, "Research#", req.requirement_id);
            blocker.current_level = lookup_level(r_levels, req.requirement_id);
            break;
        case 3:
            blocker.type = "ship";
            blocker.name = lookup_name(s_names, "Ship#", req.requirement_id);
            blocker.current_level = lookup_level(s_levels, req.requirement_id);
            break;
        case 8:
        case 9:
        case 10:
            blocker.type = "tech";
            blocker.name = "Tech#" + std::to_string(req.requirement_id);
            blocker.current_level = lookup_level(t_levels, req.requirement_id);
            break;
        default:
            blocker.type = "unknown";
            blocker.name = "Requirement#" + std::to_string(req.requirement_id);
            blocker.current_level = 0;
            break;
    }

    blocker.met = blocker.current_level >= blocker.required_level;
    return blocker;
}

bool contains_word(const std::string& haystack, const std::string& needle) {
    auto lower_h = haystack;
    auto lower_n = needle;
    std::transform(lower_h.begin(), lower_h.end(), lower_h.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lower_n.begin(), lower_n.end(), lower_n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower_h.find(lower_n) != std::string::npos;
}

double keyword_score(const ResearchCandidate& c, const std::string& focus) {
    const std::string text = c.name + " " + c.description + " " + focus;
    double score = 0.0;

    if (contains_word(text, "ops") || contains_word(text, "operations") ||
        contains_word(text, "unlock") || contains_word(text, "r&d")) {
        score += 12.0;
    }
    if (contains_word(text, "research") || contains_word(text, "build") ||
        contains_word(text, "construction") || contains_word(text, "speed")) {
        score += 8.0;
    }
    if (contains_word(text, "warp") || contains_word(text, "impulse")) {
        score += 7.0;
    }
    if (contains_word(text, "mining") || contains_word(text, "cargo") ||
        contains_word(text, "protected")) {
        score += 7.0;
    }
    if (contains_word(text, "hostile") || contains_word(text, "armada") ||
        contains_word(text, "damage") || contains_word(text, "hull") ||
        contains_word(text, "shield")) {
        score += 6.0;
    }
    if (contains_word(focus, "mining") &&
        (contains_word(text, "mining") || contains_word(text, "cargo"))) {
        score += 12.0;
    }
    if ((contains_word(focus, "pvp") || contains_word(focus, "combat")) &&
        (contains_word(text, "damage") || contains_word(text, "shield") ||
         contains_word(text, "hull") || contains_word(text, "armor"))) {
        score += 10.0;
    }
    return score;
}

std::string build_candidate_reason(const ResearchCandidate& c,
                                   bool idle_research_slot,
                                   int time_budget_minutes) {
    std::vector<std::string> reasons;
    if (c.can_start_now) {
        reasons.push_back("can start now");
    } else if (c.resources_available && c.prerequisites_met) {
        reasons.push_back(idle_research_slot ? "ready when selected" : "resources ready, research queue busy");
    } else if (!c.prerequisites_met) {
        reasons.push_back("blocked by prerequisites");
    } else {
        reasons.push_back("good save target");
    }

    if (c.percent_affordable >= 1.0) {
        reasons.push_back("fully funded");
    } else if (c.percent_affordable >= 0.75) {
        reasons.push_back("near-affordable");
    }

    const int budget_seconds = std::max(1, time_budget_minutes) * 60;
    if (c.research_time_seconds > 0 && c.research_time_seconds <= budget_seconds) {
        reasons.push_back("fits time budget");
    } else if (c.research_time_seconds > 0 && c.research_time_seconds <= 8 * 3600) {
        reasons.push_back("short upgrade");
    }

    if (c.unlock_level > 0) {
        reasons.push_back("Ops " + std::to_string(c.unlock_level) + " research");
    }

    if (reasons.empty()) return "Ranked by affordability, prerequisites, and account impact.";
    return join_reasons(reasons);
}

json resource_to_json(const PlannedResource& r) {
    return {
        {"resource_id", r.resource_id},
        {"name", r.name},
        {"amount", r.amount},
        {"owned", r.owned},
        {"missing", r.missing},
    };
}

PlannedResource resource_from_json(const json& j) {
    PlannedResource r;
    r.resource_id = j.value("resource_id", (int64_t)0);
    r.name = j.value("name", "");
    r.amount = j.value("amount", (int64_t)0);
    r.owned = j.value("owned", (int64_t)0);
    r.missing = j.value("missing", (int64_t)0);
    return r;
}

json blocker_to_json(const PlanBlocker& b) {
    return {
        {"type", b.type},
        {"id", b.id},
        {"name", b.name},
        {"required_level", b.required_level},
        {"current_level", b.current_level},
        {"met", b.met},
    };
}

PlanBlocker blocker_from_json(const json& j) {
    PlanBlocker b;
    b.type = j.value("type", "");
    b.id = j.value("id", (int64_t)0);
    b.name = j.value("name", "");
    b.required_level = j.value("required_level", 0);
    b.current_level = j.value("current_level", 0);
    b.met = j.value("met", false);
    return b;
}

json candidate_to_json(const ResearchCandidate& c) {
    json costs = json::array();
    for (const auto& r : c.costs) costs.push_back(resource_to_json(r));

    json missing = json::array();
    for (const auto& r : c.missing_resources) missing.push_back(resource_to_json(r));

    json blockers = json::array();
    for (const auto& b : c.blockers) blockers.push_back(blocker_to_json(b));

    return {
        {"id", c.id},
        {"name", c.name},
        {"description", c.description},
        {"research_tree", c.research_tree},
        {"current_level", c.current_level},
        {"next_level", c.next_level},
        {"unlock_level", c.unlock_level},
        {"research_time_seconds", c.research_time_seconds},
        {"military_might", c.military_might},
        {"local_score", c.local_score},
        {"percent_affordable", c.percent_affordable},
        {"prerequisites_met", c.prerequisites_met},
        {"resources_available", c.resources_available},
        {"can_start_now", c.can_start_now},
        {"costs", costs},
        {"missing_resources", missing},
        {"blockers", blockers},
        {"reason", c.reason},
    };
}

ResearchCandidate candidate_from_json(const json& j) {
    ResearchCandidate c;
    c.id = j.value("id", (int64_t)0);
    c.name = j.value("name", "");
    c.description = j.value("description", "");
    c.research_tree = j.value("research_tree", (int64_t)0);
    c.current_level = j.value("current_level", 0);
    c.next_level = j.value("next_level", 0);
    c.unlock_level = j.value("unlock_level", 0);
    c.research_time_seconds = j.value("research_time_seconds", 0);
    c.military_might = j.value("military_might", (int64_t)0);
    c.local_score = j.value("local_score", 0.0);
    c.percent_affordable = j.value("percent_affordable", 0.0);
    c.prerequisites_met = j.value("prerequisites_met", false);
    c.resources_available = j.value("resources_available", false);
    c.can_start_now = j.value("can_start_now", false);
    c.reason = j.value("reason", "");

    if (j.contains("costs") && j["costs"].is_array()) {
        for (const auto& r : j["costs"]) c.costs.push_back(resource_from_json(r));
    }
    if (j.contains("missing_resources") && j["missing_resources"].is_array()) {
        for (const auto& r : j["missing_resources"]) c.missing_resources.push_back(resource_from_json(r));
    }
    if (j.contains("blockers") && j["blockers"].is_array()) {
        for (const auto& b : j["blockers"]) c.blockers.push_back(blocker_from_json(b));
    }
    return c;
}

json action_to_json(const PlanAction& a) {
    json spent = json::array();
    for (const auto& r : a.resources_spent) spent.push_back(resource_to_json(r));
    json missing = json::array();
    for (const auto& r : a.missing_resources) missing.push_back(resource_to_json(r));

    return {
        {"priority", a.priority},
        {"domain", a.domain},
        {"action", a.action},
        {"reason", a.reason},
        {"can_do_now", a.can_do_now},
        {"duration_seconds", a.duration_seconds},
        {"resources_spent", spent},
        {"missing_resources", missing},
    };
}

PlanAction action_from_json(const json& j) {
    PlanAction a;
    a.priority = j.value("priority", 0);
    a.domain = j.value("domain", "");
    a.action = j.value("action", "");
    a.reason = j.value("reason", "");
    a.can_do_now = j.value("can_do_now", false);
    a.duration_seconds = j.value("duration_seconds", 0);
    if (j.contains("resources_spent") && j["resources_spent"].is_array()) {
        for (const auto& r : j["resources_spent"]) a.resources_spent.push_back(resource_from_json(r));
    }
    if (j.contains("missing_resources") && j["missing_resources"].is_array()) {
        for (const auto& r : j["missing_resources"]) a.missing_resources.push_back(resource_from_json(r));
    }
    return a;
}

} // namespace

std::vector<ResearchCandidate> analyze_research_candidates(
    const FullAccountSnapshot& snapshot,
    int time_budget_minutes,
    const std::string& focus) {
    const auto amounts = resource_amounts(snapshot);
    const auto r_names = resource_names(snapshot);
    const auto res_levels = research_levels(snapshot);
    const auto res_names = research_names(snapshot);
    const auto b_levels = building_levels(snapshot);
    const auto b_names = building_names(snapshot);
    const auto t_levels = tech_levels(snapshot);
    const auto s_levels = ship_levels(snapshot);
    const auto s_names = ship_names(snapshot);
    const bool idle_research_slot = snapshot.idle_research_slots > 0;

    std::vector<ResearchCandidate> candidates;
    for (const auto& research : snapshot.research) {
        if (research.levels.empty()) continue;
        if (research.current_level < 0) continue;
        if (research.current_level >= static_cast<int>(research.levels.size())) continue;

        // Keep the prompt small later: only include visible and near-visible nodes.
        if (snapshot.ops_level > 0 && research.unlock_level > snapshot.ops_level + 2 &&
            research.current_level == 0) {
            continue;
        }

        const auto& next = research.levels[static_cast<size_t>(research.current_level)];
        ResearchCandidate c;
        c.id = research.id;
        c.name = fallback_name(research.name, "Research#", research.id);
        c.description = research.description;
        c.research_tree = research.research_tree;
        c.current_level = research.current_level;
        c.next_level = next.id > 0 ? next.id : research.current_level + 1;
        c.unlock_level = research.unlock_level;
        c.research_time_seconds = next.research_time_seconds;
        c.military_might = next.military_might;

        bool resource_ok = true;
        double affordable_sum = 0.0;
        int affordable_count = 0;
        for (const auto& cost : next.costs) {
            PlannedResource pr;
            pr.resource_id = cost.resource_id;
            pr.name = lookup_name(r_names, "Resource#", cost.resource_id);
            pr.amount = cost.amount;
            auto ait = amounts.find(cost.resource_id);
            pr.owned = ait == amounts.end() ? 0 : ait->second;
            pr.missing = std::max<int64_t>(0, pr.amount - pr.owned);
            if (pr.missing > 0) {
                resource_ok = false;
                c.missing_resources.push_back(pr);
            }
            c.costs.push_back(pr);

            if (pr.amount > 0) {
                affordable_sum += std::min(1.0, static_cast<double>(pr.owned) / static_cast<double>(pr.amount));
                affordable_count++;
            }
        }
        c.resources_available = resource_ok;
        c.percent_affordable = affordable_count == 0
            ? 1.0
            : affordable_sum / static_cast<double>(affordable_count);

        bool prereq_ok = true;
        if (snapshot.ops_level > 0 && research.unlock_level > snapshot.ops_level) {
            PlanBlocker blocker;
            blocker.type = "ops";
            blocker.name = "Operations";
            blocker.required_level = research.unlock_level;
            blocker.current_level = snapshot.ops_level;
            blocker.met = false;
            blocker.id = 0;
            c.blockers.push_back(std::move(blocker));
            prereq_ok = false;
        }

        for (const auto& req : next.requirements) {
            auto blocker = evaluate_requirement(req, b_levels, b_names,
                                                res_levels, res_names,
                                                t_levels, s_levels, s_names);
            if (!blocker.met) {
                prereq_ok = false;
                c.blockers.push_back(std::move(blocker));
            }
        }
        c.prerequisites_met = prereq_ok;
        c.can_start_now = prereq_ok && resource_ok && idle_research_slot;

        c.local_score = 50.0;
        c.local_score += c.resources_available ? 25.0 : (c.percent_affordable * 16.0);
        c.local_score += c.prerequisites_met ? 25.0 : std::max(-35.0, -12.0 * c.blockers.size());
        if (idle_research_slot) c.local_score += 8.0;
        if (c.current_level == 0) c.local_score += 4.0;
        if (c.unlock_level > 0 && snapshot.ops_level > 0) {
            const int delta = snapshot.ops_level - c.unlock_level;
            if (delta >= 0 && delta <= 3) c.local_score += 8.0;
            else if (delta > 3 && delta <= 10) c.local_score += 3.0;
            else if (delta < 0) c.local_score -= 14.0;
        }
        if (c.research_time_seconds > 0) {
            const int budget_seconds = std::max(1, time_budget_minutes) * 60;
            if (c.research_time_seconds <= budget_seconds) c.local_score += 8.0;
            else if (c.research_time_seconds <= 8 * 3600) c.local_score += 5.0;
            else if (c.research_time_seconds > 30 * 86400) c.local_score -= 8.0;
            else if (c.research_time_seconds > 7 * 86400) c.local_score -= 3.0;
        }
        if (c.military_might > 0) {
            c.local_score += std::min(8.0, std::log10(static_cast<double>(c.military_might) + 1.0));
        }
        c.local_score += keyword_score(c, focus);
        if (!c.resources_available && c.percent_affordable < 0.25) c.local_score -= 10.0;
        if (!c.prerequisites_met && c.blockers.size() > 2) c.local_score -= 8.0;

        c.reason = build_candidate_reason(c, idle_research_slot, time_budget_minutes);
        candidates.push_back(std::move(c));
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ResearchCandidate& a, const ResearchCandidate& b) {
                  if (a.can_start_now != b.can_start_now) return a.can_start_now > b.can_start_now;
                  if (a.local_score != b.local_score) return a.local_score > b.local_score;
                  return a.percent_affordable > b.percent_affordable;
              });
    return candidates;
}

ActionPlan generate_action_plan(const FullAccountSnapshot& snapshot,
                                int time_budget_minutes,
                                const std::string& focus) {
    ActionPlan plan;
    plan.generated_at = now_epoch();
    plan.last_sync = tp_epoch(snapshot.last_sync);
    plan.sync_age_seconds = plan.last_sync > 0 ? static_cast<int>(plan.generated_at - plan.last_sync) : -1;
    plan.time_budget_minutes = time_budget_minutes;
    plan.focus = focus.empty() ? "growth" : focus;

    if (snapshot.last_sync == std::chrono::system_clock::time_point{}) {
        plan.warnings.push_back("No live sync timestamp found; recommendations may be based on empty or stale player data.");
    } else if (plan.sync_age_seconds > 6 * 3600) {
        plan.warnings.push_back("Live sync is older than 6 hours; refresh sync before trusting resource availability.");
    }
    const bool has_positive_resource = std::any_of(
        snapshot.resources.begin(), snapshot.resources.end(),
        [](const ResolvedResource& r) { return r.amount > 0; });
    if (snapshot.resources.empty() || !has_positive_resource) {
        plan.warnings.push_back("No resource balances available; affordability checks cannot be trusted.");
    }
    if (snapshot.idle_research_slots <= 0) {
        plan.warnings.push_back("Research queues appear busy; ready items are save/queue-next targets.");
    }

    auto candidates = analyze_research_candidates(snapshot, time_budget_minutes, plan.focus);
    if (candidates.empty()) {
        plan.warnings.push_back("No research candidates could be built from the current snapshot.");
        return plan;
    }

    const size_t top_count = std::min<size_t>(10, candidates.size());
    plan.top_research.assign(candidates.begin(), candidates.begin() + top_count);

    int priority = 1;
    for (const auto& c : candidates) {
        if (!c.can_start_now) continue;
        PlanAction action;
        action.priority = priority++;
        action.domain = "research";
        action.action = "Start research: " + c.name + " L" + std::to_string(c.next_level);
        action.reason = c.reason;
        action.can_do_now = true;
        action.duration_seconds = c.research_time_seconds;
        action.resources_spent = c.costs;
        plan.do_now.push_back(std::move(action));
        if (plan.do_now.size() >= 5) break;
    }

    for (const auto& c : candidates) {
        if (c.missing_resources.empty()) continue;
        if (!c.prerequisites_met && c.percent_affordable < 0.8) continue;
        SaveForTarget target;
        target.target = "Research: " + c.name + " L" + std::to_string(c.next_level);
        target.reason = c.reason;
        target.missing_resources = c.missing_resources;
        plan.save_for.push_back(std::move(target));
        if (plan.save_for.size() >= 5) break;
    }

    for (auto it = candidates.rbegin(); it != candidates.rend() && plan.avoid.size() < 5; ++it) {
        if (it->can_start_now) continue;
        AvoidAction avoid;
        avoid.action = "Research: " + it->name + " L" + std::to_string(it->next_level);
        if (!it->prerequisites_met) {
            avoid.reason = "Blocked by prerequisites; do not reserve scarce resources for it yet.";
        } else if (it->percent_affordable < 0.25) {
            avoid.reason = "Too far from affordable compared with higher-ranked candidates.";
        } else {
            avoid.reason = "Lower local impact than the current top research candidates.";
        }
        plan.avoid.push_back(std::move(avoid));
    }

    return plan;
}

bool save_action_plan(const ActionPlan& plan, const std::string& path) {
    try {
        fs::create_directories(fs::path(path).parent_path());

        json j;
        j["generated_at"] = plan.generated_at;
        j["last_sync"] = plan.last_sync;
        j["sync_age_seconds"] = plan.sync_age_seconds;
        j["time_budget_minutes"] = plan.time_budget_minutes;
        j["focus"] = plan.focus;
        j["warnings"] = plan.warnings;

        j["top_research"] = json::array();
        for (const auto& c : plan.top_research) j["top_research"].push_back(candidate_to_json(c));

        j["do_now"] = json::array();
        for (const auto& a : plan.do_now) j["do_now"].push_back(action_to_json(a));

        j["save_for"] = json::array();
        for (const auto& s : plan.save_for) {
            json missing = json::array();
            for (const auto& r : s.missing_resources) missing.push_back(resource_to_json(r));
            j["save_for"].push_back({
                {"target", s.target},
                {"reason", s.reason},
                {"missing_resources", missing},
            });
        }

        j["avoid"] = json::array();
        for (const auto& a : plan.avoid) {
            j["avoid"].push_back({{"action", a.action}, {"reason", a.reason}});
        }

        std::ofstream out(path);
        if (!out) return false;
        out << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

bool load_action_plan(ActionPlan& plan, const std::string& path) {
    try {
        if (!fs::exists(path)) return false;
        std::ifstream in(path);
        if (!in) return false;
        auto j = json::parse(in);

        plan = ActionPlan{};
        plan.generated_at = j.value("generated_at", (int64_t)0);
        plan.last_sync = j.value("last_sync", (int64_t)0);
        plan.sync_age_seconds = j.value("sync_age_seconds", -1);
        plan.time_budget_minutes = j.value("time_budget_minutes", 45);
        plan.focus = j.value("focus", "growth");

        if (j.contains("warnings") && j["warnings"].is_array()) {
            for (const auto& w : j["warnings"]) {
                if (w.is_string()) plan.warnings.push_back(w.get<std::string>());
            }
        }
        if (j.contains("top_research") && j["top_research"].is_array()) {
            for (const auto& c : j["top_research"]) plan.top_research.push_back(candidate_from_json(c));
        }
        if (j.contains("do_now") && j["do_now"].is_array()) {
            for (const auto& a : j["do_now"]) plan.do_now.push_back(action_from_json(a));
        }
        if (j.contains("save_for") && j["save_for"].is_array()) {
            for (const auto& s : j["save_for"]) {
                SaveForTarget target;
                target.target = s.value("target", "");
                target.reason = s.value("reason", "");
                if (s.contains("missing_resources") && s["missing_resources"].is_array()) {
                    for (const auto& r : s["missing_resources"]) {
                        target.missing_resources.push_back(resource_from_json(r));
                    }
                }
                plan.save_for.push_back(std::move(target));
            }
        }
        if (j.contains("avoid") && j["avoid"].is_array()) {
            for (const auto& a : j["avoid"]) {
                AvoidAction avoid;
                avoid.action = a.value("action", "");
                avoid.reason = a.value("reason", "");
                plan.avoid.push_back(std::move(avoid));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace stfc
