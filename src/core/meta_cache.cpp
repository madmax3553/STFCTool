#include "core/meta_cache.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <set>

#include "json.hpp"

using json = nlohmann::json;

namespace stfc {

// ===========================================================================
// MetaCache age helpers
// ===========================================================================

double MetaCache::age_hours() const {
    if (last_refresh == 0) return -1.0;
    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return static_cast<double>(epoch - last_refresh) / 3600.0;
}

std::string MetaCache::age_str() const {
    double hours = age_hours();
    if (hours < 0) return "never";
    if (hours < 1.0) return std::to_string(static_cast<int>(hours * 60)) + "m ago";
    if (hours < 48.0) return std::to_string(static_cast<int>(hours)) + "h ago";
    return std::to_string(static_cast<int>(hours / 24.0)) + "d ago";
}

// ===========================================================================
// JSON persistence
// ===========================================================================

MetaCache load_meta_cache(const std::string& path) {
    MetaCache cache;

    std::ifstream f(path);
    if (!f.is_open()) return cache;

    try {
        json j = json::parse(f);

        if (j.contains("last_refresh") && j["last_refresh"].is_number()) {
            cache.last_refresh = j["last_refresh"].get<int64_t>();
        }

        if (j.contains("groups") && j["groups"].is_object()) {
            for (auto& [key, val] : j["groups"].items()) {
                MetaGroupEntry entry;
                entry.group = key;
                if (val.contains("top_officers") && val["top_officers"].is_array()) {
                    for (const auto& name : val["top_officers"]) {
                        if (name.is_string()) entry.top_officers.push_back(name.get<std::string>());
                    }
                }
                if (val.contains("top_crews_desc") && val["top_crews_desc"].is_array()) {
                    for (const auto& d : val["top_crews_desc"]) {
                        if (d.is_string()) entry.top_crews_desc.push_back(d.get<std::string>());
                    }
                }
                if (val.contains("meta_summary") && val["meta_summary"].is_string()) {
                    entry.meta_summary = val["meta_summary"].get<std::string>();
                }
                if (val.contains("timestamp") && val["timestamp"].is_number()) {
                    entry.timestamp = val["timestamp"].get<int64_t>();
                }
                if (val.contains("model_used") && val["model_used"].is_string()) {
                    entry.model_used = val["model_used"].get<std::string>();
                }
                cache.groups[key] = std::move(entry);
            }
        }
    } catch (...) {}

    return cache;
}

bool save_meta_cache(const MetaCache& cache, const std::string& path) {
    json j;
    j["last_refresh"] = cache.last_refresh;

    json groups_j = json::object();
    for (const auto& [key, entry] : cache.groups) {
        json ej;
        ej["top_officers"] = entry.top_officers;
        ej["top_crews_desc"] = entry.top_crews_desc;
        ej["meta_summary"] = entry.meta_summary;
        ej["timestamp"] = entry.timestamp;
        ej["model_used"] = entry.model_used;
        groups_j[key] = ej;
    }
    j["groups"] = groups_j;

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}

// ===========================================================================
// Build Gemini META query prompt
// ===========================================================================

std::string build_meta_query_prompt(const std::string& group_name,
                                     const std::string& group_description) {
    std::ostringstream ss;
    ss << "You are an expert at Star Trek Fleet Command (STFC), a MOBILE GAME by Scopely.\n\n"
       << "List the current META top 15-20 officers for: " << group_name << "\n"
       << "Context: " << group_description << "\n\n"
       << "For EACH officer, provide:\n"
       << "1. Their EXACT in-game name (as it appears in STFC)\n"
       << "2. Whether they are best as Captain (CM) or Bridge (OA)\n"
       << "3. One sentence on why they're META for this role\n\n"
       << "Also list the top 3-5 crew combinations (captain + 2 bridge) for " << group_name << ".\n\n"
       << "IMPORTANT:\n"
       << "- Use EXACT in-game officer names (e.g., 'PIC Worf' not just 'Worf', "
       << "'SNW La'an' not just 'La'an', 'Five of Eleven' not '5 of 11')\n"
       << "- Include officers from ALL eras and factions (TOS, TNG, DS9, SNW, PIC, Discovery, etc.)\n"
       << "- Focus on the CURRENT META (2024-2025 updates)\n\n"
       << "Return as JSON:\n"
       << R"({"officers":["exact name 1","exact name 2",...],"crews":[{"captain":"name","bridge":["name","name"],"why":"brief reason"}],"summary":"1-2 sentence META overview"})";
    return ss.str();
}

// ===========================================================================
// Parse Gemini META response → extract officer names
// ===========================================================================

// Lowercase helper
static std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

// Fuzzy match: check if a known officer name appears in the text
// Handles common variations: "PIC Worf" vs "Pic Worf" vs "PIC WORF"
static bool fuzzy_name_match(const std::string& candidate_lower,
                              const std::string& known_lower) {
    if (candidate_lower == known_lower) return true;
    // Check if candidate contains the known name (handles prefixes/suffixes)
    if (candidate_lower.find(known_lower) != std::string::npos) return true;
    if (known_lower.find(candidate_lower) != std::string::npos &&
        candidate_lower.size() >= 4) return true;  // don't match very short substrings
    return false;
}

std::vector<std::string> parse_meta_officer_names(
    const std::string& response,
    const std::vector<std::string>& known_officers)
{
    std::set<std::string> found;  // Use set for dedup

    // Build lowercase lookup
    std::vector<std::pair<std::string, std::string>> known_lc;  // (original, lowercase)
    for (const auto& name : known_officers) {
        known_lc.emplace_back(name, to_lower(name));
    }

    // Strategy 1: Try JSON parse first
    try {
        json j = json::parse(response);
        // Look for officers array
        for (const auto& key : {"officers", "top_officers", "names"}) {
            if (j.contains(key) && j[key].is_array()) {
                for (const auto& name : j[key]) {
                    if (!name.is_string()) continue;
                    std::string name_str = name.get<std::string>();
                    std::string name_lower = to_lower(name_str);
                    // Match against known officers
                    for (const auto& [orig, orig_lower] : known_lc) {
                        if (fuzzy_name_match(name_lower, orig_lower)) {
                            found.insert(orig);
                            break;
                        }
                    }
                }
            }
        }
        // Also check crews for names
        for (const auto& key : {"crews", "top_crews"}) {
            if (j.contains(key) && j[key].is_array()) {
                for (const auto& crew : j[key]) {
                    if (!crew.is_object()) continue;
                    // Captain
                    if (crew.contains("captain") && crew["captain"].is_string()) {
                        std::string cap_lower = to_lower(crew["captain"].get<std::string>());
                        for (const auto& [orig, orig_lower] : known_lc) {
                            if (fuzzy_name_match(cap_lower, orig_lower)) {
                                found.insert(orig);
                                break;
                            }
                        }
                    }
                    // Bridge
                    if (crew.contains("bridge") && crew["bridge"].is_array()) {
                        for (const auto& b : crew["bridge"]) {
                            if (!b.is_string()) continue;
                            std::string b_lower = to_lower(b.get<std::string>());
                            for (const auto& [orig, orig_lower] : known_lc) {
                                if (fuzzy_name_match(b_lower, orig_lower)) {
                                    found.insert(orig);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!found.empty()) return {found.begin(), found.end()};
    } catch (...) {}

    // Strategy 2: Free text — scan for known officer names in the response
    std::string resp_lower = to_lower(response);
    for (const auto& [orig, orig_lower] : known_lc) {
        // Only match names >= 4 chars to avoid false positives
        if (orig_lower.size() >= 4 && resp_lower.find(orig_lower) != std::string::npos) {
            found.insert(orig);
        }
    }

    return {found.begin(), found.end()};
}

} // namespace stfc
