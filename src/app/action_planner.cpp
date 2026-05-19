#include "app/action_planner.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
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

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string compact_duration(int64_t seconds) {
    if (seconds <= 0) return "0m";
    const int64_t days = seconds / 86400;
    seconds %= 86400;
    const int64_t hours = seconds / 3600;
    seconds %= 3600;
    const int64_t minutes = seconds / 60;

    std::ostringstream out;
    if (days > 0) {
        out << days << "d";
        if (hours > 0) out << " " << hours << "h";
        return out.str();
    }
    if (hours > 0) {
        out << hours << "h";
        if (minutes > 0) out << " " << minutes << "m";
        return out.str();
    }
    if (minutes > 0) return std::to_string(minutes) + "m";
    return std::to_string(seconds) + "s";
}

int64_t parse_general_speedup_seconds(const std::string& name) {
    if (name.empty()) return 0;
    const auto lower = lower_copy(name);
    if (lower.find("speedup") == std::string::npos &&
        lower.find("speed up") == std::string::npos &&
        lower.find("speed-up") == std::string::npos) {
        return 0;
    }
    if (lower.find("repair") != std::string::npos ||
        lower.find("assignment") != std::string::npos ||
        lower.find("countdown") != std::string::npos ||
        lower.find("armada") != std::string::npos ||
        lower.find("outpost") != std::string::npos ||
        lower.find("alliance") != std::string::npos ||
        lower.find("asb") != std::string::npos ||
        lower.find("caa credit") != std::string::npos) {
        return 0;
    }

    static const std::regex duration_re(
        R"((\d+)\s*(m|min|mins|minute|minutes|h|hr|hrs|hour|hours|d|day|days)\b)",
        std::regex_constants::icase);
    std::smatch match;
    if (!std::regex_search(name, match, duration_re)) return 0;

    int64_t amount = 0;
    try {
        amount = std::stoll(match[1].str());
    } catch (...) {
        return 0;
    }
    const auto unit = lower_copy(match[2].str());
    if (unit == "m" || unit == "min" || unit == "mins" ||
        unit == "minute" || unit == "minutes") {
        return amount * 60;
    }
    if (unit == "h" || unit == "hr" || unit == "hrs" ||
        unit == "hour" || unit == "hours") {
        return amount * 3600;
    }
    if (unit == "d" || unit == "day" || unit == "days") {
        return amount * 86400;
    }
    return 0;
}

struct SpeedupInventorySummary {
    bool definitions_available = false;
    bool resource_balances_reliable = false;
    bool has_positive_speedups = false;
    int definition_count = 0;
    int positive_stack_count = 0;
    int64_t available_seconds = 0;
};

SpeedupInventorySummary summarize_speedups(const FullAccountSnapshot& snapshot) {
    SpeedupInventorySummary summary;
    summary.resource_balances_reliable = !snapshot.resource_state_partial;

    std::map<int64_t, int64_t> seconds_by_id;
    std::set<int64_t> counted_resource_ids;
    for (const auto& resource : snapshot.resources) {
        const int64_t seconds = parse_general_speedup_seconds(resource.name);
        if (seconds <= 0) continue;
        seconds_by_id[resource.id] = seconds;
        summary.definitions_available = true;
        summary.definition_count++;
        if (resource.amount > 0) {
            summary.available_seconds += seconds * resource.amount;
            summary.has_positive_speedups = true;
            summary.positive_stack_count++;
            counted_resource_ids.insert(resource.id);
        }
    }

    for (const auto& item : snapshot.inventory) {
        auto it = seconds_by_id.find(item.ref_id);
        if (it == seconds_by_id.end() || item.count <= 0) continue;
        if (counted_resource_ids.count(item.ref_id)) continue;
        summary.available_seconds += it->second * item.count;
        summary.has_positive_speedups = true;
        summary.positive_stack_count++;
    }

    return summary;
}

SpeedupCoverage speedup_coverage_for(int duration_seconds,
                                     const SpeedupInventorySummary& summary) {
    SpeedupCoverage coverage;
    if (duration_seconds <= 0) {
        coverage.required = false;
        coverage.evaluated = true;
        coverage.enough = true;
        coverage.source = "no timer";
        return coverage;
    }

    coverage.required = true;
    coverage.required_seconds = duration_seconds;
    coverage.available_seconds = summary.available_seconds;
    coverage.source = summary.positive_stack_count > 0
        ? "resource/inventory sync"
        : "resource sync";

    if (!summary.definitions_available) {
        coverage.evaluated = false;
        coverage.warning = "speedup item definitions are missing from cached game data";
        return coverage;
    }

    if (summary.resource_balances_reliable || summary.available_seconds >= coverage.required_seconds) {
        coverage.evaluated = true;
        coverage.enough = coverage.available_seconds >= coverage.required_seconds;
        coverage.shortage_seconds = coverage.enough
            ? 0
            : coverage.required_seconds - coverage.available_seconds;
        return coverage;
    }

    coverage.evaluated = false;
    coverage.shortage_seconds = std::max<int64_t>(
        0, coverage.required_seconds - coverage.available_seconds);
    coverage.warning = "speedup balances are not fully synced";
    return coverage;
}

std::string speedup_reason_fragment(const SpeedupCoverage& coverage) {
    if (!coverage.required) return "";
    if (!coverage.evaluated) {
        return "speedup coverage not verified";
    }
    if (coverage.enough) {
        return "speedups cover " + compact_duration(coverage.required_seconds) + " timer";
    }
    return "speedups short by " + compact_duration(coverage.shortage_seconds);
}

std::string research_tree_name(int64_t tree_id) {
    switch (tree_id) {
        case 1870147103: return "Combat tree";
        case 1868126734: return "Starship tree";
        default: break;
    }
    return "Research screen";
}

std::string research_location(int64_t tree_id, int row, int column, int unlock_level) {
    std::ostringstream out;
    out << research_tree_name(tree_id);
    if (row > 0 || column > 0) {
        out << ", row " << row << ", column " << column;
    }
    if (unlock_level > 0) {
        out << ", Ops " << unlock_level << "+";
    }
    return out.str();
}

std::string location_key(int64_t tree_id, int row, int column) {
    return std::to_string(tree_id) + ":" + std::to_string(row) + ":" + std::to_string(column);
}

std::string with_neighbor_hint(std::string location,
                               const std::string& previous,
                               const std::string& next) {
    if (!previous.empty() && !next.empty()) {
        location += "; between " + previous + " and " + next;
    } else if (!previous.empty()) {
        location += "; after " + previous;
    } else if (!next.empty()) {
        location += "; before " + next;
    }
    return location;
}

bool can_afford_with_remaining(const ResearchCandidate& c,
                               const std::map<int64_t, int64_t>& remaining) {
    if (c.funding_unknown || c.costs.empty()) return false;
    for (const auto& cost : c.costs) {
        auto it = remaining.find(cost.resource_id);
        const int64_t owned = it == remaining.end() ? 0 : it->second;
        if (owned < cost.amount) return false;
    }
    return true;
}

void reserve_costs(const ResearchCandidate& c, std::map<int64_t, int64_t>& remaining) {
    for (const auto& cost : c.costs) {
        auto& amount = remaining[cost.resource_id];
        amount = std::max<int64_t>(0, amount - cost.amount);
    }
}

std::string build_candidate_reason(const ResearchCandidate& c,
                                   int idle_research_slots,
                                   int time_budget_minutes);
std::string classify_research_bucket(const ResearchCandidate& c);
std::string classify_funding_class(const ResearchCandidate& c);
bool is_tracking_only_research(const ResearchCandidate& c);

void refresh_candidate_budget(ResearchCandidate& c,
                              const std::map<int64_t, int64_t>& remaining,
                              int idle_research_slots,
                              int time_budget_minutes,
                              bool resource_balances_reliable,
                              bool note_after_priorities) {
    const bool idle_research_slot = idle_research_slots > 0;
    c.missing_resources.clear();
    bool resource_ok = true;
    double affordable_sum = 0.0;
    int affordable_count = 0;
    for (auto& cost : c.costs) {
        auto it = remaining.find(cost.resource_id);
        cost.owned = it == remaining.end() ? 0 : it->second;
        cost.missing = std::max<int64_t>(0, cost.amount - cost.owned);
        if (cost.missing > 0) {
            resource_ok = false;
            c.missing_resources.push_back(cost);
        }
        if (cost.amount > 0) {
            affordable_sum += std::min(1.0, static_cast<double>(cost.owned) / static_cast<double>(cost.amount));
            affordable_count++;
        }
    }

    c.resources_available = resource_ok && !c.funding_unknown && !c.costs.empty();
    c.percent_affordable = c.funding_unknown
        ? 0.0
        : (affordable_count == 0 ? 1.0 : affordable_sum / static_cast<double>(affordable_count));
    c.resource_balances_unknown = !resource_balances_reliable;
    c.research_bucket = classify_research_bucket(c);
    c.funding_class = classify_funding_class(c);
    c.tracking_only = is_tracking_only_research(c);
    c.resources_available = c.resources_available && resource_balances_reliable;
    c.can_start_now = c.prerequisites_met && c.resources_available && idle_research_slot;
    c.reason = build_candidate_reason(c, idle_research_slots, time_budget_minutes);
    if (note_after_priorities && !c.resources_available && !c.funding_unknown &&
        c.prerequisites_met && !c.missing_resources.empty()) {
        c.reason = "after higher priorities, needs resources; " + c.reason;
    }
}

bool add_candidate_once(std::vector<ResearchCandidate>& selected,
                        std::set<int64_t>& selected_ids,
                        ResearchCandidate candidate) {
    if (selected_ids.count(candidate.id)) return false;
    selected_ids.insert(candidate.id);
    selected.push_back(std::move(candidate));
    return true;
}

bool is_prime_research(const ResearchCandidate& c) {
    return contains_word(c.name, "prime");
}

bool is_bulk_resource_name(const std::string& name);

bool resource_contains_any(const ResearchCandidate& c,
                           const std::vector<std::string>& needles,
                           bool missing_only = false) {
    for (const auto& cost : c.costs) {
        if (missing_only && cost.missing <= 0) continue;
        for (const auto& needle : needles) {
            if (contains_word(cost.name, needle)) return true;
        }
    }
    return false;
}

bool is_standard_research_resource(const PlannedResource& r) {
    return is_bulk_resource_name(r.name) ||
           contains_word(r.name, "refined ore") ||
           contains_word(r.name, "refined gas") ||
           contains_word(r.name, "refined crystal") ||
           contains_word(r.name, "raw ore") ||
           contains_word(r.name, "raw gas") ||
           contains_word(r.name, "raw crystal") ||
           contains_word(r.name, "isogen") ||
           contains_word(r.name, "faction credit") ||
           contains_word(r.name, "solo armada") ||
           contains_word(r.name, "armada directive");
}

bool has_only_standard_research_resources(const ResearchCandidate& c) {
    if (c.costs.empty()) return false;
    return std::all_of(c.costs.begin(), c.costs.end(), is_standard_research_resource);
}

bool has_nonstandard_research_resource(const ResearchCandidate& c) {
    return std::any_of(c.costs.begin(), c.costs.end(),
                       [](const PlannedResource& r) {
                           return !is_standard_research_resource(r);
                       });
}

bool is_specialty_ship_research(const ResearchCandidate& c) {
    const std::string text = c.name + " " + c.description + " " + c.research_tree_name;
    if (has_nonstandard_research_resource(c)) return true;
    if (resource_contains_any(c, {"Black Market Schematics", "Transogen", "Nanoprobe",
                                  "Nexus Particle", "Shard", "Refit"})) {
        return true;
    }
    return contains_word(text, "serene squall") ||
           contains_word(text, "reliant") ||
           contains_word(text, "voyager") ||
           contains_word(text, "talios") ||
           contains_word(text, "vi'dar") ||
           contains_word(text, "vidar") ||
           contains_word(text, "monaveen") ||
           contains_word(text, "cerritos") ||
           contains_word(text, "defiant") ||
           contains_word(text, "nova squadron") ||
           contains_word(text, "forbidden tech") ||
           contains_word(text, "refit");
}

std::string classify_research_bucket(const ResearchCandidate& c) {
    if (is_prime_research(c) || resource_contains_any(c, {"Prime "})) return "prime";
    if (!c.costs.empty() && is_specialty_ship_research(c) &&
        !has_only_standard_research_resources(c)) {
        return "specialty_ship";
    }
    return "daily";
}

std::string classify_funding_class(const ResearchCandidate& c) {
    if (c.funding_unknown) return "unknown";
    if (resource_contains_any(c, {"5★ Epic", "5* Epic", "G5 Epic", "Grade 5 Epic",
                                  "6★ Epic", "6* Epic", "G6 Epic", "Grade 6 Epic"}, true)) {
        return "level_locked";
    }
    if (c.research_bucket == "prime" &&
        (resource_contains_any(c, {"Particle", "Medallion", "Emblem", "Prime Key", "Particle Key"}, true) ||
         has_nonstandard_research_resource(c)) &&
        !has_only_standard_research_resources(c)) {
        return "purchase_or_event";
    }
    if (c.research_bucket == "specialty_ship" ||
        resource_contains_any(c, {"Black Market Schematics", "Transogen", "Nanoprobe",
                                  "Nexus Particle", "Shard", "Refit"})) {
        return "f2p_grindable";
    }
    if (has_only_standard_research_resources(c)) return "standard";
    return "f2p_grindable";
}

bool is_tracking_only_research(const ResearchCandidate& c) {
    if (c.resource_balances_unknown) return true;
    if (c.funding_unknown) return true;
    return (c.research_bucket == "prime" &&
            (c.funding_class == "purchase_or_event" ||
             c.funding_class == "level_locked") &&
            !c.resources_available);
}

int research_bucket_rank(const ResearchCandidate& c) {
    if (c.research_bucket == "daily") return 0;
    if (c.research_bucket == "specialty_ship") return 1;
    if (c.research_bucket == "prime") return 2;
    return 3;
}

bool is_low_impact_research(const ResearchCandidate& c) {
    const std::string text = c.name + " " + c.description;
    if (contains_word(c.name, "mining xp")) return true;
    if (contains_word(text, "ship xp") && contains_word(text, "mining")) return true;
    if (contains_word(c.name, "avatar") || contains_word(c.name, "frame")) return true;
    return false;
}

bool is_bulk_resource_name(const std::string& name) {
    return contains_word(name, "parsteel") ||
           contains_word(name, "tritanium") ||
           contains_word(name, "dilithium");
}

double scarce_resource_penalty(const ResearchCandidate& c) {
    double penalty = 0.0;
    for (const auto& cost : c.costs) {
        if (cost.amount <= 0 || cost.owned <= 0 || is_bulk_resource_name(cost.name)) continue;
        const double share = static_cast<double>(cost.amount) / static_cast<double>(cost.owned);
        if (share >= 0.90) penalty = std::max(penalty, 8.0);
        else if (share >= 0.75) penalty = std::max(penalty, 5.0);
    }
    return penalty;
}

double keyword_score(const ResearchCandidate& c, const std::string& focus) {
    const std::string text = c.name + " " + c.description + " " + focus;
    double score = 0.0;

    if (contains_word(text, "ops") || contains_word(text, "operations") ||
        contains_word(text, "unlock") || contains_word(text, "r&d")) {
        score += 12.0;
    }
    if (is_prime_research(c) && !c.tracking_only && !is_low_impact_research(c)) {
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
                                   int idle_research_slots,
                                   int time_budget_minutes) {
    std::vector<std::string> reasons;
    const bool idle_research_slot = idle_research_slots > 0;
    if (c.funding_unknown) {
        reasons.push_back("funding requirements not listed in cache");
    }
    if (c.resource_balances_unknown) {
        reasons.push_back("resource balances missing from sync");
    }
    if (c.research_bucket == "specialty_ship") {
        reasons.push_back("specialty ship funded");
    } else if (c.research_bucket == "prime") {
        if (c.funding_class == "purchase_or_event") {
            reasons.push_back("Prime watchlist: purchase/event-gated currency");
        } else if (c.funding_class == "level_locked") {
            reasons.push_back("Prime watchlist: material tier gated");
        } else {
            reasons.push_back("Prime research");
        }
    }
    if (c.tracking_only) {
        reasons.push_back("track separately from everyday ROI");
    }
    if (std::any_of(c.blockers.begin(), c.blockers.end(),
                    [](const PlanBlocker& b) { return b.current_level < 0; })) {
        reasons.push_back("requirement level missing from sync");
    }

    if (c.can_start_now) {
        reasons.push_back("can start now");
    } else if (c.resources_available && c.prerequisites_met) {
        if (idle_research_slots < 0) {
            reasons.push_back("resources ready; research queue state unknown");
        } else {
            reasons.push_back(idle_research_slot ? "ready when selected" : "resources ready, research queue busy");
        }
    } else if (!c.prerequisites_met) {
        reasons.push_back("blocked by prerequisites");
    } else if (!c.funding_unknown && !c.resource_balances_unknown) {
        reasons.push_back("good save target");
    }

    if (!c.funding_unknown && !c.resource_balances_unknown && c.percent_affordable >= 1.0) {
        reasons.push_back("fully funded");
    } else if (!c.funding_unknown && !c.resource_balances_unknown && c.percent_affordable >= 0.75) {
        reasons.push_back("near-affordable");
    }

    const int budget_seconds = std::max(1, time_budget_minutes) * 60;
    if (c.research_time_seconds > 0 && c.research_time_seconds <= budget_seconds) {
        reasons.push_back("fits time budget");
    } else if (c.research_time_seconds > 0 && c.research_time_seconds <= 8 * 3600) {
        reasons.push_back("short upgrade");
    }
    const auto speedup_note = speedup_reason_fragment(c.speedups);
    if (!speedup_note.empty()) {
        reasons.push_back(speedup_note);
    }

    if (c.unlock_level > 0) {
        reasons.push_back("Ops " + std::to_string(c.unlock_level) + " research");
    }
    if (is_low_impact_research(c)) {
        reasons.push_back("lower-impact utility");
    }
    if (scarce_resource_penalty(c) > 0.0 && c.resources_available) {
        reasons.push_back("uses scarce special currency");
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

json speedup_to_json(const SpeedupCoverage& s) {
    return {
        {"required", s.required},
        {"evaluated", s.evaluated},
        {"enough", s.enough},
        {"required_seconds", s.required_seconds},
        {"available_seconds", s.available_seconds},
        {"shortage_seconds", s.shortage_seconds},
        {"source", s.source},
        {"warning", s.warning},
    };
}

SpeedupCoverage speedup_from_json(const json& j) {
    SpeedupCoverage s;
    s.required = j.value("required", false);
    s.evaluated = j.value("evaluated", false);
    s.enough = j.value("enough", false);
    s.required_seconds = j.value("required_seconds", (int64_t)0);
    s.available_seconds = j.value("available_seconds", (int64_t)0);
    s.shortage_seconds = j.value("shortage_seconds", (int64_t)0);
    s.source = j.value("source", "");
    s.warning = j.value("warning", "");
    return s;
}

json candidate_to_json(const ResearchCandidate& c) {
    json costs = json::array();
    for (const auto& r : c.costs) costs.push_back(resource_to_json(r));

    json missing = json::array();
    for (const auto& r : c.missing_resources) missing.push_back(resource_to_json(r));

    json requirements = json::array();
    for (const auto& b : c.requirements) requirements.push_back(blocker_to_json(b));

    json blockers = json::array();
    for (const auto& b : c.blockers) blockers.push_back(blocker_to_json(b));

    return {
        {"id", c.id},
        {"name", c.name},
        {"description", c.description},
        {"research_tree", c.research_tree},
        {"research_tree_name", c.research_tree_name},
        {"row", c.row},
        {"column", c.column},
        {"location", c.location},
        {"current_level", c.current_level},
        {"next_level", c.next_level},
        {"unlock_level", c.unlock_level},
        {"research_time_seconds", c.research_time_seconds},
        {"hard_currency_cost", c.hard_currency_cost},
        {"military_might", c.military_might},
        {"local_score", c.local_score},
        {"percent_affordable", c.percent_affordable},
        {"funding_unknown", c.funding_unknown},
        {"resource_balances_unknown", c.resource_balances_unknown},
        {"prerequisites_met", c.prerequisites_met},
        {"resources_available", c.resources_available},
        {"can_start_now", c.can_start_now},
        {"research_bucket", c.research_bucket},
        {"funding_class", c.funding_class},
        {"tracking_only", c.tracking_only},
        {"costs", costs},
        {"missing_resources", missing},
        {"requirements", requirements},
        {"blockers", blockers},
        {"speedups", speedup_to_json(c.speedups)},
        {"reason", c.reason},
    };
}

ResearchCandidate candidate_from_json(const json& j) {
    ResearchCandidate c;
    c.id = j.value("id", (int64_t)0);
    c.name = j.value("name", "");
    c.description = j.value("description", "");
    c.research_tree = j.value("research_tree", (int64_t)0);
    c.research_tree_name = j.value("research_tree_name", "");
    c.row = j.value("row", 0);
    c.column = j.value("column", 0);
    c.location = j.value("location", "");
    c.current_level = j.value("current_level", 0);
    c.next_level = j.value("next_level", 0);
    c.unlock_level = j.value("unlock_level", 0);
    c.research_time_seconds = j.value("research_time_seconds", 0);
    c.hard_currency_cost = j.value("hard_currency_cost", 0);
    c.military_might = j.value("military_might", (int64_t)0);
    c.local_score = j.value("local_score", 0.0);
    c.percent_affordable = j.value("percent_affordable", 0.0);
    c.funding_unknown = j.value("funding_unknown", false);
    c.resource_balances_unknown = j.value("resource_balances_unknown", false);
    c.prerequisites_met = j.value("prerequisites_met", false);
    c.resources_available = j.value("resources_available", false);
    c.can_start_now = j.value("can_start_now", false);
    c.research_bucket = j.value("research_bucket", "daily");
    c.funding_class = j.value("funding_class", "standard");
    c.tracking_only = j.value("tracking_only", false);
    if (j.contains("speedups") && j["speedups"].is_object()) {
        c.speedups = speedup_from_json(j["speedups"]);
    }
    c.reason = j.value("reason", "");
    if (c.research_tree_name.empty() && c.research_tree != 0) {
        c.research_tree_name = research_tree_name(c.research_tree);
    }
    if (c.location.empty() && c.research_tree != 0) {
        c.location = research_location(c.research_tree, c.row, c.column, c.unlock_level);
    }

    if (j.contains("costs") && j["costs"].is_array()) {
        for (const auto& r : j["costs"]) c.costs.push_back(resource_from_json(r));
    }
    if (j.contains("missing_resources") && j["missing_resources"].is_array()) {
        for (const auto& r : j["missing_resources"]) c.missing_resources.push_back(resource_from_json(r));
    }
    if (j.contains("requirements") && j["requirements"].is_array()) {
        for (const auto& b : j["requirements"]) c.requirements.push_back(blocker_from_json(b));
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
        {"location", a.location},
        {"reason", a.reason},
        {"can_do_now", a.can_do_now},
        {"duration_seconds", a.duration_seconds},
        {"speedups", speedup_to_json(a.speedups)},
        {"resources_spent", spent},
        {"missing_resources", missing},
    };
}

PlanAction action_from_json(const json& j) {
    PlanAction a;
    a.priority = j.value("priority", 0);
    a.domain = j.value("domain", "");
    a.action = j.value("action", "");
    a.location = j.value("location", "");
    a.reason = j.value("reason", "");
    a.can_do_now = j.value("can_do_now", false);
    a.duration_seconds = j.value("duration_seconds", 0);
    if (j.contains("speedups") && j["speedups"].is_object()) {
        a.speedups = speedup_from_json(j["speedups"]);
    }
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
    const int idle_research_slots = snapshot.idle_research_slots;
    const bool idle_research_slot = idle_research_slots > 0;
    const bool resource_balances_reliable = !snapshot.resource_state_partial;
    const auto speedup_summary = summarize_speedups(snapshot);
    std::map<std::string, std::string> names_by_location;
    for (const auto& research : snapshot.research) {
        if (research.row <= 0 || research.column <= 0) continue;
        names_by_location[location_key(research.research_tree, research.row, research.column)] =
            fallback_name(research.name, "Research#", research.id);
    }

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
        c.research_tree_name = research_tree_name(research.research_tree);
        c.row = research.row;
        c.column = research.column;
        c.location = research_location(
            research.research_tree, research.row, research.column, research.unlock_level);
        const auto previous = names_by_location.find(
            location_key(research.research_tree, research.row, research.column - 1));
        const auto next_neighbor = names_by_location.find(
            location_key(research.research_tree, research.row, research.column + 1));
        c.location = with_neighbor_hint(
            c.location,
            previous == names_by_location.end() ? "" : previous->second,
            next_neighbor == names_by_location.end() ? "" : next_neighbor->second);
        c.current_level = research.current_level;
        c.next_level = next.id > 0 ? next.id : research.current_level + 1;
        c.unlock_level = research.unlock_level;
        c.research_time_seconds = next.research_time_seconds;
        c.speedups = speedup_coverage_for(c.research_time_seconds, speedup_summary);
        c.hard_currency_cost = next.hard_currency_cost;
        c.military_might = next.military_might;
        c.funding_unknown = next.costs.empty();

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
        c.resources_available = resource_ok && !c.funding_unknown;
        c.percent_affordable = c.funding_unknown
            ? 0.0
            : (affordable_count == 0 ? 1.0 : affordable_sum / static_cast<double>(affordable_count));
        c.resource_balances_unknown = !resource_balances_reliable;
        if (!resource_balances_reliable) {
            c.resources_available = false;
        }
        c.research_bucket = classify_research_bucket(c);
        c.funding_class = classify_funding_class(c);
        c.tracking_only = is_tracking_only_research(c);

        bool prereq_ok = true;
        if (snapshot.ops_level > 0 && research.unlock_level > snapshot.ops_level) {
            PlanBlocker blocker;
            blocker.type = "ops";
            blocker.name = "Operations";
            blocker.required_level = research.unlock_level;
            blocker.current_level = snapshot.ops_level;
            blocker.met = false;
            blocker.id = 0;
            c.requirements.push_back(blocker);
            c.blockers.push_back(std::move(blocker));
            prereq_ok = false;
        } else if (snapshot.ops_level > 0 && research.unlock_level > 0) {
            PlanBlocker requirement;
            requirement.type = "ops";
            requirement.name = "Operations";
            requirement.required_level = research.unlock_level;
            requirement.current_level = snapshot.ops_level;
            requirement.met = true;
            requirement.id = 0;
            c.requirements.push_back(std::move(requirement));
        }

        for (const auto& req : next.requirements) {
            auto requirement = evaluate_requirement(req, b_levels, b_names,
                                                    res_levels, res_names,
                                                    t_levels, s_levels, s_names);
            if (!requirement.met) {
                prereq_ok = false;
                c.blockers.push_back(requirement);
            }
            c.requirements.push_back(std::move(requirement));
        }
        c.prerequisites_met = prereq_ok;
        c.can_start_now = prereq_ok && c.resources_available && idle_research_slot;

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
        if (c.speedups.required && c.research_time_seconds > std::max(1, time_budget_minutes) * 60) {
            if (c.speedups.evaluated && !c.speedups.enough) c.local_score -= 4.0;
            else if (!c.speedups.evaluated && c.research_time_seconds > 8 * 3600) c.local_score -= 2.0;
        }
        if (c.military_might > 0) {
            c.local_score += std::min(8.0, std::log10(static_cast<double>(c.military_might) + 1.0));
        }
        c.local_score += keyword_score(c, focus);
        if (c.funding_unknown) c.local_score -= 30.0;
        if (is_low_impact_research(c)) c.local_score -= 28.0;
        c.local_score -= scarce_resource_penalty(c);
        if (c.research_bucket == "daily") c.local_score += 6.0;
        else if (c.research_bucket == "specialty_ship") c.local_score += 2.0;
        else if (c.research_bucket == "prime") c.local_score -= 8.0;
        if (c.tracking_only) c.local_score -= 45.0;
        if (c.funding_class == "purchase_or_event" && !c.resources_available) c.local_score -= 20.0;
        if (std::any_of(c.blockers.begin(), c.blockers.end(),
                        [](const PlanBlocker& b) { return b.current_level < 0; })) {
            c.local_score -= 12.0;
        }
        if (!c.resources_available && c.percent_affordable < 0.25) c.local_score -= 10.0;
        if (!c.prerequisites_met && c.blockers.size() > 2) c.local_score -= 8.0;

        c.reason = build_candidate_reason(c, idle_research_slots, time_budget_minutes);
        candidates.push_back(std::move(c));
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ResearchCandidate& a, const ResearchCandidate& b) {
                  int ra = research_bucket_rank(a);
                  int rb = research_bucket_rank(b);
                  if (ra != rb) return ra < rb;
                  if (a.can_start_now != b.can_start_now) return a.can_start_now > b.can_start_now;
                  if (a.tracking_only != b.tracking_only) return !a.tracking_only;
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
    if (snapshot.resource_state_partial) {
        plan.warnings.push_back("Core resource balances are missing from sync; funding and save targets are verify-only until a full resource sync arrives.");
    }
    auto building_known = [&snapshot](int64_t id) {
        return std::any_of(snapshot.buildings.begin(), snapshot.buildings.end(),
                           [id](const ResolvedBuilding& b) {
                               return b.id == id && b.current_level >= 0;
                           });
    };
    if (!building_known(0) || !building_known(25)) {
        plan.warnings.push_back("Station building sync is incomplete; building-gated research is marked unknown until the next full module sync.");
    }
    if (snapshot.research_state_inferred) {
        plan.warnings.push_back("Explicit research sync is missing; research levels are inferred from active buffs where possible.");
    }
    if (snapshot.idle_research_slots < 0) {
        plan.warnings.push_back("Research queue state is unknown because no current job payload was received; ready items are queue-next targets until verified in game.");
    } else if (snapshot.idle_research_slots <= 0) {
        plan.warnings.push_back("Research queues appear busy; ready items are save/queue-next targets.");
    }
    const auto speedup_summary = summarize_speedups(snapshot);
    if (!speedup_summary.definitions_available) {
        plan.warnings.push_back("Speedup item definitions are missing from cached game data; research/building/ship timers cannot be checked.");
    } else if (!speedup_summary.resource_balances_reliable &&
               speedup_summary.available_seconds <= 0) {
        plan.warnings.push_back("Speedup balances are not fully synced; spend actions show speedup coverage as unverified until a full resource sync arrives.");
    }

    auto candidates = analyze_research_candidates(snapshot, time_budget_minutes, plan.focus);
    if (candidates.empty()) {
        plan.warnings.push_back("No research candidates could be built from the current snapshot.");
        return plan;
    }

    const size_t top_count = std::min<size_t>(10, candidates.size());
    const int idle_research_slots = snapshot.idle_research_slots;
    const bool resource_balances_reliable = !snapshot.resource_state_partial;
    auto remaining_after_starts = resource_amounts(snapshot);
    std::set<int64_t> selected_ids;
    std::map<int64_t, int> selected_by_tree;

    auto add_ready = [&](const ResearchCandidate& candidate, bool enforce_tree_cap) {
        if (plan.top_research.size() >= 5 || selected_ids.count(candidate.id)) return false;
        if (!candidate.can_start_now) return false;
        if (enforce_tree_cap && selected_by_tree[candidate.research_tree] >= 3) return false;

        ResearchCandidate pick = candidate;
        refresh_candidate_budget(pick, remaining_after_starts, idle_research_slots,
                                 time_budget_minutes, resource_balances_reliable, false);
        if (!pick.can_start_now || !can_afford_with_remaining(pick, remaining_after_starts)) {
            return false;
        }

        reserve_costs(pick, remaining_after_starts);
        if (!add_candidate_once(plan.top_research, selected_ids, std::move(pick))) return false;
        selected_by_tree[candidate.research_tree]++;
        return true;
    };

    for (const auto& c : candidates) {
        add_ready(c, true);
        if (plan.top_research.size() >= 5) break;
    }
    for (const auto& c : candidates) {
        add_ready(c, false);
        if (plan.top_research.size() >= 5) break;
    }

    auto add_bucket = [&](const std::string& bucket, size_t limit, bool include_tracking) {
        size_t added = 0;
        for (const auto& c : candidates) {
            if (plan.top_research.size() >= top_count || added >= limit) break;
            if (selected_ids.count(c.id)) continue;
            if (c.research_bucket != bucket) continue;
            if (!include_tracking && c.tracking_only) continue;
            if (selected_by_tree[c.research_tree] >= 4) continue;

            ResearchCandidate pick = c;
            refresh_candidate_budget(pick, remaining_after_starts, idle_research_slots,
                                     time_budget_minutes, resource_balances_reliable, true);
            if (pick.tracking_only && !include_tracking) continue;
            if (add_candidate_once(plan.top_research, selected_ids, std::move(pick))) {
                selected_by_tree[c.research_tree]++;
                added++;
            }
        }
    };

    add_bucket("daily", 5, false);
    add_bucket("specialty_ship", 3, false);
    add_bucket("prime", 3, resource_balances_reliable);

    auto fill_remaining = [&](bool include_tracking) {
        for (const auto& c : candidates) {
            if (plan.top_research.size() >= top_count) break;
            if (selected_ids.count(c.id)) continue;
            ResearchCandidate pick = c;
            refresh_candidate_budget(pick, remaining_after_starts, idle_research_slots,
                                     time_budget_minutes, resource_balances_reliable, true);
            if (pick.tracking_only && !include_tracking) continue;
            if (add_candidate_once(plan.top_research, selected_ids, std::move(pick))) {
                selected_by_tree[c.research_tree]++;
            }
        }
    };

    fill_remaining(false);
    fill_remaining(true);

    std::stable_sort(plan.top_research.begin(), plan.top_research.end(),
                     [](const ResearchCandidate& a, const ResearchCandidate& b) {
                         if (a.tracking_only != b.tracking_only) return !a.tracking_only;
                         if (a.can_start_now != b.can_start_now) return a.can_start_now > b.can_start_now;
                         int ra = research_bucket_rank(a);
                         int rb = research_bucket_rank(b);
                         if (ra != rb) return ra < rb;
                         if (a.local_score != b.local_score) return a.local_score > b.local_score;
                         return a.percent_affordable > b.percent_affordable;
                     });

    int priority = 1;
    std::set<int64_t> planned_start_ids;
    for (const auto& c : plan.top_research) {
        if (!c.can_start_now) continue;
        if (c.tracking_only) continue;
        PlanAction action;
        action.priority = priority++;
        action.domain = "research";
        action.action = "Start research: " + c.name + " L" + std::to_string(c.next_level);
        action.location = c.location;
        action.reason = c.reason;
        action.can_do_now = true;
        action.duration_seconds = c.research_time_seconds;
        action.speedups = c.speedups;
        action.resources_spent = c.costs;
        plan.do_now.push_back(std::move(action));
        planned_start_ids.insert(c.id);
        if (plan.do_now.size() >= 5) break;
    }

    for (const auto& c : candidates) {
        if (planned_start_ids.count(c.id)) continue;
        ResearchCandidate target_candidate = c;
        refresh_candidate_budget(target_candidate, remaining_after_starts, idle_research_slots,
                                 time_budget_minutes, resource_balances_reliable, true);
        if (target_candidate.tracking_only) continue;
        if (target_candidate.missing_resources.empty()) continue;
        if (!target_candidate.prerequisites_met && target_candidate.percent_affordable < 0.8) continue;
        SaveForTarget target;
        target.target = "Research: " + target_candidate.name + " L" + std::to_string(target_candidate.next_level);
        target.location = target_candidate.location;
        target.reason = target_candidate.reason;
        target.missing_resources = target_candidate.missing_resources;
        plan.save_for.push_back(std::move(target));
        if (plan.save_for.size() >= 5) break;
    }

    for (auto it = candidates.rbegin(); it != candidates.rend() && plan.avoid.size() < 5; ++it) {
        if (it->can_start_now) continue;
        AvoidAction avoid;
        avoid.action = "Research: " + it->name + " L" + std::to_string(it->next_level);
        avoid.location = it->location;
        if (it->funding_unknown) {
            avoid.reason = "Funding requirements are not present in cached data; verify in game before prioritizing.";
        } else if (!it->prerequisites_met) {
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
                {"location", s.location},
                {"reason", s.reason},
                {"missing_resources", missing},
            });
        }

        j["avoid"] = json::array();
        for (const auto& a : plan.avoid) {
            j["avoid"].push_back({
                {"action", a.action},
                {"location", a.location},
                {"reason", a.reason},
            });
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
                target.location = s.value("location", "");
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
                avoid.location = a.value("location", "");
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
