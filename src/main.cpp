#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <thread>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <mutex>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"

#include "data/models.h"
#include "data/api_client.h"
#include "data/ingress_server.h"
#include "util/csv_import.h"
#include "core/crew_optimizer.h"
#include "core/planner.h"
#include "core/ai_crew_engine.h"

using namespace ftxui;
namespace fs = std::filesystem;

namespace stfc {

namespace {

std::string mining_descriptor(const DockConfig& cfg) {
    MiningResource resource = cfg.mining_resource == MiningResource::None
        ? scenario_mining_resource(cfg.scenario) : cfg.mining_resource;
    MiningObjective objective = cfg.mining_objective == MiningObjective::None
        ? scenario_mining_objective(cfg.scenario) : cfg.mining_objective;
    if (resource == MiningResource::None && objective == MiningObjective::None) return "";
    return std::string(mining_resource_str(resource)) + " + " + mining_objective_str(objective);
}

std::string mining_descriptor(const DockResult& dr) {
    if (dr.mining_resource == MiningResource::None && dr.mining_objective == MiningObjective::None) return "";
    return std::string(mining_resource_str(dr.mining_resource)) + " + " + mining_objective_str(dr.mining_objective);
}

bool is_mining_scenario(Scenario s) {
    switch (s) {
        case Scenario::Loot:
        case Scenario::MiningSpeed:
        case Scenario::MiningProtected:
        case Scenario::MiningCrystal:
        case Scenario::MiningGas:
        case Scenario::MiningOre:
        case Scenario::MiningGeneral:
            return true;
        default:
            return false;
    }
}

enum class LoadoutPosture {
    WarLowAttention,
    WarActive,
    GrowthLowAttention,
    MiningPush,
    ArmadaFocus,
    GeneralDaily,
};

struct ShipRoleFit {
    std::string ship_name;
    std::string role;
    int score = 0;
    std::string reason;
};

struct AccountAnalysis {
    std::string posture_label;
    std::vector<std::string> summary_notes;
    std::vector<ShipRoleFit> top_ship_roles;
};

std::string posture_label(LoadoutPosture posture) {
    switch (posture) {
        case LoadoutPosture::WarLowAttention: return "War / Low Attention";
        case LoadoutPosture::WarActive: return "War / Active";
        case LoadoutPosture::GrowthLowAttention: return "Growth / Low Attention";
        case LoadoutPosture::MiningPush: return "Mining Push";
        case LoadoutPosture::ArmadaFocus: return "Armada Focus";
        case LoadoutPosture::GeneralDaily: return "General Daily";
    }
    return "General Daily";
}

std::vector<DockConfig> make_posture_docks(LoadoutPosture posture) {
    auto mk = [](Scenario s, int priority, const std::string& purpose) {
        DockConfig cfg;
        cfg.scenario = s;
        cfg.priority = priority;
        cfg.purpose = purpose;
        cfg.mining_resource = scenario_mining_resource(s);
        cfg.mining_objective = scenario_mining_objective(s);
        return cfg;
    };

    switch (posture) {
        case LoadoutPosture::WarLowAttention:
            return {
                mk(Scenario::PvP, 100, "Instant war-response deck"),
                mk(Scenario::Armada, 95, "Always-ready alliance armada deck"),
                mk(Scenario::MiningSpeed, 90, "Fast wartime miner"),
                mk(Scenario::MiningProtected, 88, "Safe unattended miner"),
                mk(Scenario::PvEHostile, 84, "Low-touch hostile grinder"),
                mk(Scenario::Hybrid, 78, "Flexible backup combat deck"),
                mk(Scenario::MissionBoss, 70, "Flex boss / specialty slot"),
            };
        case LoadoutPosture::WarActive:
            return {
                mk(Scenario::PvP, 100, "Primary war-response deck"),
                mk(Scenario::PvP, 96, "Secondary PvP deck"),
                mk(Scenario::Armada, 92, "Armada standby"),
                mk(Scenario::PvEHostile, 86, "Fast active grinder"),
                mk(Scenario::MiningSpeed, 82, "Quick refill miner"),
                mk(Scenario::Hybrid, 76, "Flexible combat swap deck"),
                mk(Scenario::MissionBoss, 70, "Boss / event flex slot"),
            };
        case LoadoutPosture::GrowthLowAttention:
            return {
                mk(Scenario::MiningSpeed, 100, "Primary economy miner"),
                mk(Scenario::MiningProtected, 95, "Safe long-run miner"),
                mk(Scenario::MiningGeneral, 90, "Balanced miner"),
                mk(Scenario::PvEHostile, 86, "Daily hostile grinder"),
                mk(Scenario::Armada, 80, "Alliance contribution deck"),
                mk(Scenario::PvP, 74, "Basic defense response"),
                mk(Scenario::MissionBoss, 68, "Mission / event flex slot"),
            };
        case LoadoutPosture::MiningPush:
            return {
                mk(Scenario::MiningSpeed, 100, "Primary speed miner"),
                mk(Scenario::MiningProtected, 96, "Protected miner"),
                mk(Scenario::MiningGeneral, 92, "General mining deck"),
                mk(Scenario::MiningGas, 88, "Gas specialist"),
                mk(Scenario::MiningOre, 84, "Ore specialist"),
                mk(Scenario::MiningCrystal, 80, "Crystal specialist"),
                mk(Scenario::PvP, 72, "Emergency war-response deck"),
            };
        case LoadoutPosture::ArmadaFocus:
            return {
                mk(Scenario::Armada, 100, "Primary armada deck"),
                mk(Scenario::Armada, 94, "Secondary armada deck"),
                mk(Scenario::PvP, 88, "War-response deck"),
                mk(Scenario::PvEHostile, 84, "Grinder between armadas"),
                mk(Scenario::MiningProtected, 78, "Safe unattended miner"),
                mk(Scenario::MiningSpeed, 74, "Quick refill miner"),
                mk(Scenario::Hybrid, 70, "General flex deck"),
            };
        case LoadoutPosture::GeneralDaily:
            return {
                mk(Scenario::PvP, 100, "Always-ready combat deck"),
                mk(Scenario::PvEHostile, 92, "Daily hostile grinder"),
                mk(Scenario::Armada, 88, "Alliance armada deck"),
                mk(Scenario::MiningSpeed, 84, "Fast miner"),
                mk(Scenario::MiningProtected, 80, "Safe miner"),
                mk(Scenario::Hybrid, 76, "Flexible backup deck"),
                mk(Scenario::MissionBoss, 70, "Boss / event flex slot"),
            };
    }

    return {};
}

int ship_role_score(const PlayerShip& ship, const GameData& game_data, Scenario scenario) {
    auto it = game_data.ships.find(ship.hull_id);
    if (it == game_data.ships.end()) return 0;

    const Ship& gs = it->second;
    std::string ship_name = ship.name.empty() ? gs.name : ship.name;
    std::string lower_name = ship_name;
    for (auto& c : lower_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    int score = ship.tier * 12 + ship.level + gs.rarity * 24;

    auto has_name = [&](const std::string& token) {
        return lower_name.find(token) != std::string::npos;
    };

    auto grade_band_bonus = [&](int target_grade) {
        int diff = std::abs(gs.grade - target_grade);
        if (diff == 0) return 140;
        if (diff == 1) return 80;
        if (diff == 2) return 30;
        return -120 * diff;
    };

    switch (scenario) {
        case Scenario::PvP:
        case Scenario::Hybrid:
        case Scenario::BaseCracker:
            score += grade_band_bonus(6);
            if (gs.hull_type == 0) score += 90;
            if (gs.hull_type == 3) score += 70;
            if (gs.hull_type == 2) score += 50;
            if (has_name("revenant") || has_name("cube")) score += 120;
            break;
        case Scenario::Armada:
        case Scenario::MissionBoss:
        case Scenario::PvEHostile:
            score += grade_band_bonus(6);
            if (gs.hull_type == 2) score += 95;
            if (gs.hull_type == 3) score += 70;
            if (gs.hull_type == 0) score += 50;
            if (has_name("protector") || has_name("voyager") || has_name("cerritos")) score += 120;
            break;
        case Scenario::MiningSpeed:
        case Scenario::MiningProtected:
        case Scenario::MiningCrystal:
        case Scenario::MiningGas:
        case Scenario::MiningOre:
        case Scenario::MiningGeneral:
        case Scenario::Loot:
            score += grade_band_bonus(6);
            if (gs.hull_type == 1) score += 120;
            if (gs.hull_type == 2) score += 30;
            if (has_name("selkie")) score += 240;
            if (has_name("meridian")) score += 140;
            if (has_name("feesha") || has_name("d'vor")) score -= 180;
            if (has_name("north star")) score -= 220;
            break;
    }

    return score;
}

AccountAnalysis analyze_account_posture(const PlayerData& player_data,
                                        const GameData& game_data,
                                        LoadoutPosture posture,
                                        const std::vector<DockConfig>& docks) {
    AccountAnalysis out;
    out.posture_label = posture_label(posture);

    int miner_count = 0;
    int survey_count = 0;
    for (const auto& ship : player_data.ships) {
        auto it = game_data.ships.find(ship.hull_id);
        if (it == game_data.ships.end()) continue;
        if (it->second.hull_type == 1) ++survey_count;
        if (ship_role_score(ship, game_data, Scenario::MiningGeneral) > 0) ++miner_count;
    }

    out.summary_notes.push_back("Posture: " + out.posture_label);
    if (player_data.ships.empty()) {
        out.summary_notes.push_back("No synced ships available; ship assignment will fall back to generic recommendations.");
        return out;
    }

    out.summary_notes.push_back("Owned ships: " + std::to_string(player_data.ships.size()) +
                                ", survey hulls: " + std::to_string(survey_count));
    if (survey_count < 2) {
        out.summary_notes.push_back("Only a small survey bench is synced, so miner role conflicts may reduce efficiency.");
    }

    std::set<std::string> used;
    for (const auto& dock : docks) {
        int best_score = -1;
        ShipRoleFit best;
        for (const auto& ship : player_data.ships) {
            std::string ship_name = ship.name.empty() ? ("Ship#" + std::to_string(ship.hull_id)) : ship.name;
            if (used.count(ship_name)) continue;
            int score = ship_role_score(ship, game_data, dock.scenario);
            if (score > best_score) {
                best_score = score;
                best.ship_name = ship_name;
                best.role = scenario_label(dock.scenario);
                best.score = score;
                best.reason = dock.purpose;
            }
        }
        if (best_score >= 0) {
            used.insert(best.ship_name);
            out.top_ship_roles.push_back(best);
        }
    }

    if (!out.top_ship_roles.empty()) {
        out.summary_notes.push_back("Top ship-role assignments are based on owned hull tier, level, rarity, and class fit.");
    }
    return out;
}

char rarity_letter(int rarity) {
    switch (rarity) {
        case 1: return 'C';
        case 2: return 'U';
        case 3: return 'R';
        case 4: return 'E';
        default: return ' ';
    }
}

double ability_pct(const OfficerAbility& ability, int rank) {
    if (ability.values.empty()) return 0.0;
    int idx = std::max(0, std::min(rank, static_cast<int>(ability.values.size()) - 1));
    return ability.values[idx].value;
}

std::string fmt_pct(double value) {
    std::ostringstream os;
    double pct = value * 100.0;
    if (std::abs(pct - std::round(pct)) < 0.0001) {
        os << static_cast<int>(std::round(pct)) << "%";
    } else {
        os << std::fixed << std::setprecision(1) << pct << "%";
    }
    return os.str();
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string resolve_officer_tooltip(const Officer& officer, int rank) {
    std::string text = officer.description;
    if (text.empty()) return text;

    const auto cap = officer.captain_ability.values;
    const auto abil = officer.ability.values;
    auto rank_idx = std::max(0, rank);

    auto cap_value = [&](int idx) {
        idx = std::max(0, std::min(idx, static_cast<int>(cap.size()) - 1));
        return cap.empty() ? 0.0 : cap[idx].value;
    };
    auto abil_value = [&](int idx) {
        idx = std::max(0, std::min(idx, static_cast<int>(abil.size()) - 1));
        return abil.empty() ? 0.0 : abil[idx].value;
    };

    text = replace_all(text, "{0:#,#%}", fmt_pct(cap_value(rank_idx)));
    text = replace_all(text, "{1:#,#%}", fmt_pct(cap_value(std::min(rank_idx + 1, std::max(0, (int)cap.size() - 1)))));
    text = replace_all(text, "{2:#,#%}", fmt_pct(abil_value(rank_idx)));
    text = replace_all(text, "{3:#,#%}", fmt_pct(abil_value(rank_idx)));
    text = replace_all(text, "{0:#.#%}", fmt_pct(cap_value(rank_idx)));
    text = replace_all(text, "{1:#.#%}", fmt_pct(cap_value(std::min(rank_idx + 1, std::max(0, (int)cap.size() - 1)))));
    text = replace_all(text, "{2:#.#%}", fmt_pct(abil_value(rank_idx)));
    text = replace_all(text, "{3:#.#%}", fmt_pct(abil_value(rank_idx)));
    return text;
}

// ---------------------------------------------------------------------------
// Helpers for transforming API description text into optimizer-compatible format
// ---------------------------------------------------------------------------

// Strip <color=#XXXXXX> and </color> tags from text.
std::string strip_color_tags(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            // Check for <color=...> or </color>
            if (text.compare(i, 7, "<color=") == 0) {
                auto end = text.find('>', i);
                if (end != std::string::npos) { i = end + 1; continue; }
            } else if (text.compare(i, 8, "</color>") == 0) {
                i += 8; continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// Lowercase a string in-place and return it.
std::string to_lower_str(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Collapse runs of whitespace (including \n within a section) to single spaces,
// trim leading/trailing whitespace.
std::string collapse_whitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool prev_space = true;  // treat start as space to trim leading
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prev_space) { out += ' '; prev_space = true; }
        } else {
            out += c;
            prev_space = false;
        }
    }
    // Trim trailing space
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Strip {N:...} format placeholders from text (for ship abilities where values are unavailable).
// Replaces patterns like {0:#.#%}, {0:#####%}, {0:#,##0}, {0:0.00#%}, {0:#}, {0:0#%}, {0:0.#}, {0}
// with a simple "?" placeholder so the text remains readable.
std::string strip_format_placeholders(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            // Look for closing brace
            auto end = text.find('}', i);
            if (end != std::string::npos && end - i < 30) {
                // Check it looks like a format placeholder: {digit...}
                bool is_placeholder = (i + 1 < end) &&
                    std::isdigit(static_cast<unsigned char>(text[i + 1]));
                if (is_placeholder) {
                    out += '?';
                    i = end + 1;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

// Classify a ship's ability based on its ability_name and ability_description text.
// Returns a tag string like "mining_parsteel", "mining_crystal", "combat_hostile", etc.
// Both inputs should be the raw translated strings (color tags OK, we strip them here).
std::string classify_ship_ability(const std::string& ability_name,
                                  const std::string& ability_description) {
    std::string name = to_lower_str(strip_color_tags(ability_name));
    std::string desc = to_lower_str(strip_color_tags(ability_description));

    // Mining classifications — check ability name and description
    if (name.find("mining") != std::string::npos ||
        desc.find("mining rate") != std::string::npos ||
        desc.find("mining speed") != std::string::npos ||
        desc.find("mining bonus") != std::string::npos) {
        // Specific resource?
        if (desc.find("parsteel") != std::string::npos) return "mining_parsteel";
        if (desc.find("tritanium") != std::string::npos) return "mining_tritanium";
        if (desc.find("dilithium") != std::string::npos) return "mining_dilithium";
        if (desc.find("crystal") != std::string::npos && desc.find("gas") != std::string::npos)
            return "mining_universal";  // mines multiple resources
        if (desc.find("crystal") != std::string::npos) return "mining_crystal";
        if (desc.find("gas") != std::string::npos) return "mining_gas";
        if (desc.find("ore") != std::string::npos) return "mining_ore";
        if (desc.find("latinum") != std::string::npos) return "mining_latinum";
        if (desc.find("isogen") != std::string::npos) return "mining_isogen";
        if (desc.find("transogen") != std::string::npos) return "mining_transogen";
        if (desc.find("corrupted") != std::string::npos) return "mining_data";
        return "mining_general";
    }

    // Loot/reward bonuses
    if (desc.find("more resources from hostiles") != std::string::npos ||
        desc.find("reward") != std::string::npos ||
        desc.find("loot") != std::string::npos) {
        return "loot";
    }

    // Hostile-specific combat
    if (desc.find("fighting hostiles") != std::string::npos ||
        desc.find("against hostiles") != std::string::npos ||
        desc.find("hostile") != std::string::npos) {
        if (desc.find("swarm") != std::string::npos) return "combat_swarm";
        if (desc.find("borg") != std::string::npos) return "combat_borg";
        if (desc.find("gorn") != std::string::npos) return "combat_gorn";
        if (desc.find("xindi") != std::string::npos) return "combat_xindi";
        if (desc.find("eclipse") != std::string::npos) return "combat_eclipse";
        return "combat_hostile";
    }

    // PvP-style combat (ship type advantages, station defense)
    if (desc.find("opponent") != std::string::npos ||
        desc.find("defending") != std::string::npos ||
        desc.find("weapon damage") != std::string::npos ||
        desc.find("shield piercing") != std::string::npos ||
        desc.find("armor piercing") != std::string::npos) {
        return "combat_pvp";
    }

    // Captain maneuver boost
    if (desc.find("captain maneuver") != std::string::npos) return "captain_boost";

    return "general";
}

// Build the optimizer-compatible description string from the API tooltip.
// The API tooltip has two ability blocks separated by "\n\n":
//   Block 0: CM ability name + effect text (or "Unfit To Lead" for BDA officers)
//   Block 1: OA ability name + effect text
// For BDA officers we also resolve the below_decks_ability description.
//
// Output format (lowercased, color-stripped):
//   Regular officers: "cm: <cm-text> oa: <oa-text>"
//   BDA officers:     "bda: <bda-text> oa: <oa-text>"
std::string build_optimizer_description(const Officer& officer, int rank) {
    // Resolve placeholders in the main tooltip
    std::string tooltip = resolve_officer_tooltip(officer, rank);

    // Strip color tags first
    tooltip = strip_color_tags(tooltip);

    // Split on double-newline to separate ability blocks
    std::string block0, block1;
    auto sep = tooltip.find("\n\n");
    if (sep != std::string::npos) {
        block0 = tooltip.substr(0, sep);
        block1 = tooltip.substr(sep + 2);
        // There might be more double-newlines (e.g., three blocks); take only up to next
        auto sep2 = block1.find("\n\n");
        if (sep2 != std::string::npos) block1 = block1.substr(0, sep2);
    } else {
        block0 = tooltip;
    }

    // Collapse whitespace within each block
    block0 = collapse_whitespace(block0);
    block1 = collapse_whitespace(block1);

    // For BDA officers, build the BDA text from the below_decks_ability description.
    // The BDA description might be a separate translation entry, but the tooltip
    // description for BDA officers typically has:
    //   Block 0: "Unfit To Lead — This Officer does not have a Captain's Maneuver."
    //   Block 1: The OA text
    // We need to get the BDA text. Since the main tooltip only has CM+OA and the
    // BDA text isn't there, we construct it from block0 (which is the BDA's resolved text
    // for officers that DO have BDA in the tooltip) or from the raw values.
    //
    // Actually: looking at the data more carefully, for BDA officers, the tooltip_description
    // field typically contains:
    //   Block 0: BDA ability name + effect text (NOT "Unfit To Lead")
    //   Block 1: OA ability name + effect text
    // The "Unfit To Lead" is in the captain_ability's separate loca_id description,
    // not in the officer's main tooltip.
    // So block0 IS the BDA text for BDA officers.

    std::string desc;
    if (officer.has_bda) {
        desc = "bda: " + block0 + " oa: " + block1;
    } else {
        desc = "cm: " + block0 + " oa: " + block1;
    }

    return to_lower_str(desc);
}

// Detect status effects from description text and set effect/causes_effect fields.
void parse_status_effects(const std::string& desc, std::string& effect,
                          bool& causes_effect) {
    effect.clear();
    causes_effect = false;

    // State patterns: look for keywords that indicate applying or benefiting from states.
    // The CSV uses a single "effect" column with the state name, and causes_effect=true
    // if the officer applies the state (vs. benefits from it).
    struct StateKeyword {
        const char* state;
        const char* apply_keyword;   // appears when officer APPLIES the state
        const char* benefit_keyword; // appears when officer BENEFITS from the state
    };

    // Apply keywords — officer causes the state
    static const char* morale_apply[] = {
        "inspire morale", "morale for", "apply morale", "cause morale", nullptr
    };
    static const char* breach_apply[] = {
        "hull breach for", "apply hull breach", "cause hull breach",
        "inflict hull breach", nullptr
    };
    static const char* burning_apply[] = {
        "burning for", "apply burning", "cause burning",
        "inflict burning", "burning to opponent", "burning to the opponent", nullptr
    };
    static const char* assimilate_apply[] = {
        "assimilate for", "apply assimilate", nullptr
    };

    // Benefit keywords — officer benefits from the state
    static const char* morale_benefit[] = {
        "ship has morale", "with morale", "has morale",
        "when morale", "while morale", nullptr
    };
    static const char* breach_benefit[] = {
        "has hull breach", "with hull breach", "opponent hull breach",
        "when hull breach", nullptr
    };
    static const char* burning_benefit[] = {
        "is burning", "has burning", "opponent burning",
        "afflicted by burning", "when burning", "whilst burning", nullptr
    };
    static const char* assimilate_benefit[] = {
        "with assimilate", "has assimilate", "when assimilate",
        "is assimilated", nullptr
    };

    auto check_keywords = [&](const char* state, const char* const* apply_kw,
                              const char* const* benefit_kw) {
        for (const char* const* p = apply_kw; *p; ++p) {
            if (desc.find(*p) != std::string::npos) {
                effect = state;
                causes_effect = true;
                return true;
            }
        }
        for (const char* const* p = benefit_kw; *p; ++p) {
            if (desc.find(*p) != std::string::npos) {
                effect = state;
                causes_effect = false;
                return true;
            }
        }
        return false;
    };

    // Check each state (order matters — first match wins)
    if (check_keywords("morale", morale_apply, morale_benefit)) return;
    if (check_keywords("breach", breach_apply, breach_benefit)) return;
    if (check_keywords("burning", burning_apply, burning_benefit)) return;
    if (check_keywords("assimilate", assimilate_apply, assimilate_benefit)) return;
}

std::vector<RosterOfficer> build_roster_from_sync(const PlayerData& player_data,
                                                  const GameData& game_data) {
    std::vector<RosterOfficer> result;
    result.reserve(player_data.officers.size());

    for (const auto& po : player_data.officers) {
        if (po.level <= 0) continue;

        auto it = game_data.officers.find(po.officer_id);
        if (it == game_data.officers.end()) continue;

        const auto& go = it->second;
        RosterOfficer ro;
        ro.name = po.name.empty() ? (go.name.empty() ? go.short_name : go.name) : po.name;
        ro.rarity = rarity_letter(go.rarity);
        ro.level = po.level;
        ro.rank = po.rank;

        // Resolve stats from game data at the player's level.
        // po.attack/defense/health from the mod are typically zero, so
        // always prefer the static stats table when available.
        if (!go.stats.empty() && po.level > 0) {
            int idx = std::min(po.level - 1, static_cast<int>(go.stats.size()) - 1);
            idx = std::max(0, idx);
            ro.attack = go.stats[idx].attack;
            ro.defense = go.stats[idx].defense;
            ro.health = go.stats[idx].health;
        } else {
            ro.attack = po.attack;
            ro.defense = po.defense;
            ro.health = po.health;
        }
        ro.group = go.group_name;
        ro.officer_class = go.officer_class;

        // --- Fix #2: Convert raw decimals to percentage values ---
        // The API returns values like 0.40 for 40%, but the optimizer expects 40.0.
        //
        // IMPORTANT: captain_ability.values and below_decks_ability.values are
        // placeholder-indexed (for tooltip template {0}, {1}, {2}...), NOT rank-indexed.
        // The primary CM/BDA value is always at index 0.
        // Only ability.values (OA) is rank-indexed.
        //
        // For BDA officers, cm_pct holds the BDA percentage from below_decks_ability.
        if (go.has_bda) {
            double bda_raw = ability_pct(go.below_decks_ability, 0);  // always index 0
            // BDA values: some are percentages (value_is_percentage=true, e.g., 0.40 → 40%),
            // some are absolute numbers (value_is_percentage=false, e.g., Apex Barrier points).
            // For percentage BDA values, multiply by 100 to match CSV format.
            // For absolute values, they can be very large (e.g., 100000 for "100,000%"
            // display), so keep them as-is to trigger is_bda() threshold.
            if (go.below_decks_ability.value_is_percentage) {
                ro.cm_pct = bda_raw * 100.0;
            } else {
                // Absolute BDA values — store raw to trigger is_bda() check
                ro.cm_pct = bda_raw;
            }
        } else {
            double cm_raw = ability_pct(go.captain_ability, 0);  // always index 0
            ro.cm_pct = go.captain_ability.value_is_percentage ? cm_raw * 100.0 : cm_raw;
        }

        double oa_raw = ability_pct(go.ability, po.rank);
        ro.oa_pct = go.ability.value_is_percentage ? oa_raw * 100.0 : oa_raw;

        // --- Fix #1: Build optimizer-compatible description with cm:/oa:/bda: markers ---
        ro.description = build_optimizer_description(go, po.rank);

        // --- Fix #4: Parse status effects from description text ---
        parse_status_effects(ro.description, ro.effect, ro.causes_effect);

        // --- Structured ability data for numeric scoring ---
        // OA: rank-indexed values and chances
        ro.api_oa_is_pct = go.ability.value_is_percentage;
        for (const auto& av : go.ability.values) {
            ro.api_oa_values.push_back(av.value);
            ro.api_oa_chances.push_back(av.chance);
        }

        // CM: placeholder-indexed values (primary at index 0)
        ro.api_cm_is_pct = go.captain_ability.value_is_percentage;
        for (const auto& av : go.captain_ability.values) {
            ro.api_cm_values.push_back(av.value);
            ro.api_cm_chances.push_back(av.chance);
        }

        // BDA: placeholder-indexed values (primary at index 0)
        if (go.has_bda) {
            ro.api_bda_is_pct = go.below_decks_ability.value_is_percentage;
            for (const auto& av : go.below_decks_ability.values) {
                ro.api_bda_values.push_back(av.value);
                ro.api_bda_chances.push_back(av.chance);
            }
        }

        result.push_back(std::move(ro));
    }

    return result;
}

std::vector<RosterOfficer> load_best_roster_source(const PlayerData& player_data,
                                                   const GameData& game_data) {
    if (!player_data.officers.empty() && !game_data.officers.empty()) {
        return build_roster_from_sync(player_data, game_data);
    }

    if (fs::exists("roster.csv")) {
        auto csv_roster = load_roster_csv("roster.csv");
        // Enrich CSV roster with officer_class from game data (CSV doesn't carry class)
        if (!game_data.officers.empty()) {
            std::unordered_map<std::string, int> name_to_class;
            for (const auto& [id, go] : game_data.officers) {
                if (go.officer_class >= 1 && go.officer_class <= 3)
                    name_to_class[go.name] = go.officer_class;
            }
            for (auto& ro : csv_roster) {
                auto it = name_to_class.find(ro.name);
                if (it != name_to_class.end() && ro.officer_class == 0)
                    ro.officer_class = it->second;
            }
        }
        return csv_roster;
    }

    return {};
}

std::vector<OwnedShipCandidate> build_owned_ship_candidates(const PlayerData& player_data,
                                                            const GameData& game_data) {
    std::vector<OwnedShipCandidate> ships;
    ships.reserve(player_data.ships.size());
    for (const auto& ps : player_data.ships) {
        auto it = game_data.ships.find(ps.hull_id);
        if (it == game_data.ships.end()) continue;

        OwnedShipCandidate ship;
        ship.name = ps.name.empty() ? it->second.name : ps.name;
        switch (it->second.hull_type) {
            case 0: ship.ship_type = ShipType::Interceptor; break;
            case 1: ship.ship_type = ShipType::Survey; break;
            case 2: ship.ship_type = ShipType::Explorer; break;
            case 3: ship.ship_type = ShipType::Battleship; break;
            default: ship.ship_type = ShipType::Explorer; break;
        }
        ship.tier = ps.tier;
        ship.level = ps.level;
        ship.grade = it->second.grade;
        ship.rarity = it->second.rarity;
        ship.ability_tag = classify_ship_ability(it->second.ability_name,
                                                  it->second.ability_description);
        ships.push_back(std::move(ship));
    }
    return ships;
}

} // namespace

// ---------------------------------------------------------------------------
// Dashboard state
// ---------------------------------------------------------------------------

struct AppState {
    GameData game_data;
    PlayerData player_data;
    ApiClient api_client;
    IngressServer ingress_server;

    int selected_tab_ = 0;  // unused — tab is tracked by local var in main()
    std::atomic<bool> data_loaded{false};
    std::atomic<bool> loading{false};
    bool show_help = false;
    std::mutex status_mutex;
    std::string status_message = "Press [R] to refresh game data, [H] for help";

    void set_status(const std::string& msg) {
        std::lock_guard<std::mutex> lk(status_mutex);
        status_message = msg;
    }
    std::string get_status() {
        std::lock_guard<std::mutex> lk(status_mutex);
        return status_message;
    }

    // Officer browser state
    int selected_officer = 0;
    std::string officer_filter;
    bool officer_filter_active = false;  // true = typing a filter

    // Ship browser state
    int selected_ship = 0;
    std::string ship_filter;
    bool ship_filter_active = false;     // true = typing a filter

    // Sync tab browser state
    int sync_view_mode = 0;   // 0=officers, 1=ships, 2=resources, 3=buildings, 4=research, 5=jobs, 6=buffs
    int sync_selected_row = 0;

    // Planner state
    Planner planner;
    DailyPlan daily_plan;
    WeeklyPlan weekly_plan;
    int selected_daily_task = 0;
    int selected_weekly_day = 0;   // 0=Mon..6=Sun
    int selected_weekly_goal = 0;
    bool show_completed = true;

    // Crew optimizer state
    std::vector<RosterOfficer> roster;
    std::unique_ptr<CrewOptimizer> optimizer;
    int crew_scenario = 0;         // index into all_dock_scenarios()
    int crew_ship_type = 0;        // 0=Explorer, 1=Battleship, 2=Interceptor
    std::vector<CrewResult> crew_results;
    std::vector<BdaSuggestion> crew_bda_results;  // BDA for selected crew
    int selected_crew = 0;
    int selected_crew_bda = 0;     // BDA selection within crew tab
    bool crew_loaded = false;

    // AI Advisor state
    AiCrewEngine ai_engine;
    AiCrewResult ai_crew_result;
    ProgressionAdvice ai_progression_result;
    MetaAnalysis ai_meta_result;
    LlmResponse ai_ask_result;
    std::string ai_stream_text;        // streaming response text
    std::atomic<bool> ai_running{false};
    int ai_mode = 0;                   // 0=groups, 1=crew(legacy), 2=progression, 3=meta, 4=ask
    std::string ai_question;           // free-form question text
    bool ai_question_active = false;   // true = typing a question
    int ai_selected_rec = 0;           // selected recommendation index (crew mode)
    int ai_selected_inv = 0;           // selected investment index (progression mode)
    int ai_selected_meta = 0;          // selected meta crew index
    bool ai_initialized = false;
    bool ai_initializing = false;   // true while background init is running

    // Group-based query pipeline state (NEW)
    GroupQueryPipelineResult ai_group_result;   // Results from group-based pipeline
    int ai_selected_group = 0;                  // Selected group in left panel
    int ai_selected_group_crew = 0;             // Selected crew within selected group
    std::string ai_group_progress;              // "Querying PvP Combat (3/8)..."
    std::atomic<bool> ai_cancel_groups{false};  // Cancel flag for group pipeline

    // META cache refresh state
    std::atomic<bool> ai_meta_refreshing{false};  // true while Gemini META refresh is running
    std::string ai_meta_progress;                  // "Refreshing PvP Combat (2/8)..."

    void ai_init() {
        std::string err = ai_engine.initialize();
        ai_initialized = true;
        ai_initializing = false;
        if (!err.empty()) {
            set_status("AI: " + err);
        } else {
            auto s = ai_engine.status();
            set_status("AI: " + s.provider + "/" + s.model +
                       (s.is_fallback ? " (fallback)" : "") +
                       (s.has_search ? " [search]" : ""));
        }
    }

    // Trigger AI initialization in background thread (lazy, non-blocking).
    // Safe to call multiple times — only the first call does anything.
    void ai_init_lazy() {
        if (ai_initialized || ai_initializing) return;
        ai_initializing = true;
        set_status("AI: Initializing...");
        std::thread([this]() {
            ai_init();
            auto screen = ScreenInteractive::Active();
            if (screen) screen->PostEvent(Event::Custom);
        }).detach();
    }

    // Loadout state
    std::vector<DockConfig> dock_configs;
    LoadoutResult loadout_result;
    int selected_dock = 0;         // 0-6 dock selection
    int selected_dock_bda = 0;     // BDA selection within a dock
    LoadoutPosture loadout_posture = LoadoutPosture::WarLowAttention;
    AccountAnalysis account_analysis;
    bool show_dock_modal = false;
    int dock_modal_field = 0;
    bool loadout_computed = false;
    std::atomic<bool> loadout_running{false};

    void init_dock_configs() {
        dock_configs = make_posture_docks(loadout_posture);
        if (dock_configs.size() < 7) dock_configs.resize(7);
        for (auto& cfg : dock_configs) {
            cfg.ship_override.clear();
            cfg.locked = false;
        }
        refresh_account_analysis();
    }

    void refresh_account_analysis() {
        account_analysis = analyze_account_posture(player_data, game_data, loadout_posture, dock_configs);
    }

    bool loadout_matches_current_roster(const LoadoutResult& result) const {
        std::set<std::string> roster_names;
        for (const auto& officer : roster) roster_names.insert(officer.name);
        if (roster_names.empty()) return false;

        for (const auto& dock : result.docks) {
            if (!dock.captain.empty() && dock.captain != "N/A" && !roster_names.count(dock.captain)) {
                return false;
            }
            for (const auto& bridge : dock.bridge) {
                if (!bridge.empty() && bridge != "N/A" && !roster_names.count(bridge)) {
                    return false;
                }
            }
        }
        return true;
    }

    void rebuild_optimizer_from_current_data() {
        roster = load_best_roster_source(player_data, game_data);
        if (!roster.empty()) {
            optimizer = std::make_unique<CrewOptimizer>(roster);
            crew_loaded = true;
            std::ofstream debug(".stfc_mining_debug.log");
            if (debug.is_open()) {
                optimizer->dump_mining_debug(debug);
            }
        } else {
            optimizer.reset();
            crew_loaded = false;
        }
        crew_results.clear();
        crew_bda_results.clear();
        loadout_computed = false;
        refresh_account_analysis();
    }

    AppState() : api_client("data/game_data"), ingress_server("data/player_data", 8270) {
        // On startup, only load from cache — don't make network calls.
        // User can press [R] to refresh from api.spocks.club.
        api_client.set_cache_only(true);

        // Auto-load cached game data if available
        if (fs::exists("data/game_data/officers.json")) {
            api_client.fetch_all(game_data);
            data_loaded = !game_data.officers.empty();
            if (data_loaded) {
                status_message = "Loaded " +
                    std::to_string(game_data.officers.size()) + " officers, " +
                    std::to_string(game_data.ships.size()) + " ships, " +
                    std::to_string(game_data.researches.size()) + " research. [R] to refresh, [H] help";
            }
        }

        // Load player data from cache and resolve names against game data
        player_data = ingress_server.get_player_data();
        if (data_loaded) {
            resolve_player_names(player_data, game_data);
        }

        // Generate today's plans
        daily_plan = planner.generate_daily_plan();
        weekly_plan = planner.generate_weekly_plan();

        // Load saved state if available
        planner.load_daily(daily_plan, "data/player_data/daily_plan.json");
        planner.load_weekly(weekly_plan, "data/player_data/weekly_plan.json");

        // Enrich daily plan with player data context
        if (data_loaded) {
            planner.enrich_plan_with_player_data(daily_plan, player_data, game_data);
        }

        rebuild_optimizer_from_current_data();

        // AI advisor initialization is deferred to first AI tab access
        // to avoid SSL/network calls during startup that can crash.
        // See ai_init() — called lazily from render_ai_advisor() and AI event handler.

        // Initialize dock configs and try loading saved loadout
        init_dock_configs();
        if (fs::exists(".stfc_loadout.json")) {
            if (CrewOptimizer::load_loadout(loadout_result, ".stfc_loadout.json")) {
                loadout_computed = loadout_matches_current_roster(loadout_result);
                if (!loadout_computed) {
                    loadout_result = LoadoutResult{};
                    status_message = "Saved loadout is stale for the current synced roster. Press Enter to recompute.";
                }
            }
        }
    }

    void save_plans() {
        fs::create_directories("data/player_data");
        planner.save_daily(daily_plan, "data/player_data/daily_plan.json");
        planner.save_weekly(weekly_plan, "data/player_data/weekly_plan.json");
    }

    void run_crew_optimizer() {
        if (!optimizer || loadout_running) return;
        const auto& scenarios = all_dock_scenarios();
        if (crew_scenario < 0 || crew_scenario >= (int)scenarios.size()) return;

        ShipType st = ShipType::Explorer;
        if (crew_ship_type == 1) st = ShipType::Battleship;
        if (crew_ship_type == 2) st = ShipType::Interceptor;
        if (crew_ship_type == 3) st = ShipType::Survey;

        optimizer->set_ship_type(st);
        crew_results = optimizer->find_best_crews(scenarios[crew_scenario], 5);
        crew_bda_results.clear();
        selected_crew_bda = 0;

        // Auto-compute BDA for the top result
        if (!crew_results.empty()) {
            const auto& top = crew_results[0].breakdown;
            crew_bda_results = optimizer->find_best_bda(
                top.captain, top.bridge, scenarios[crew_scenario], 3);
        }
    }

    void update_crew_bda() {
        if (!optimizer || crew_results.empty() || loadout_running) return;
        if (selected_crew < 0 || selected_crew >= (int)crew_results.size()) return;

        const auto& scenarios = all_dock_scenarios();
        const auto& bd = crew_results[selected_crew].breakdown;
        crew_bda_results = optimizer->find_best_bda(
            bd.captain, bd.bridge, scenarios[crew_scenario], 3);
        selected_crew_bda = 0;
    }

    std::string loadout_error;  // non-empty if last optimization failed

    void run_loadout_optimizer() {
        if (!optimizer || dock_configs.empty()) return;
        loadout_running = true;
        loadout_error.clear();

        try {
            // Set ship type for loadout
            ShipType st = ShipType::Explorer;
            if (crew_ship_type == 1) st = ShipType::Battleship;
            if (crew_ship_type == 2) st = ShipType::Interceptor;
            if (crew_ship_type == 3) st = ShipType::Survey;
            optimizer->set_ship_type(st);

            refresh_account_analysis();
            loadout_result = optimizer->optimize_dock_loadout(
                dock_configs,
                build_owned_ship_candidates(player_data, game_data),
                1);
            loadout_result.analysis_notes = account_analysis.summary_notes;
            loadout_computed = true;

            // Save loadout
            CrewOptimizer::save_loadout(loadout_result, ".stfc_loadout.json", st);
        } catch (const std::exception& e) {
            loadout_error = std::string("Loadout optimizer failed: ") + e.what();
            loadout_computed = false;
        } catch (...) {
            loadout_error = "Loadout optimizer failed: unknown exception";
            loadout_computed = false;
        }
        loadout_running = false;
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// strip_color_tags() is defined in the anonymous namespace above (line ~363)

static std::string format_number(int64_t n) {
    if (n >= 1000000000) return std::to_string(n / 1000000000) + "." + std::to_string((n % 1000000000) / 100000000) + "B";
    if (n >= 1000000) return std::to_string(n / 1000000) + "." + std::to_string((n % 1000000) / 100000) + "M";
    if (n >= 1000) return std::to_string(n / 1000) + "." + std::to_string((n % 1000) / 100) + "K";
    return std::to_string(n);
}

static Color priority_color(TaskPriority p) {
    switch (p) {
        case TaskPriority::Critical: return Color::Red;
        case TaskPriority::High:     return Color::Yellow;
        case TaskPriority::Medium:   return Color::Cyan;
        case TaskPriority::Low:      return Color::GrayDark;
    }
    return Color::White;
}

static Color category_color(TaskCategory c) {
    switch (c) {
        case TaskCategory::Events:    return Color::Magenta;
        case TaskCategory::SpeedUps:  return Color::Yellow;
        case TaskCategory::Ships:     return Color::Cyan;
        case TaskCategory::Research:  return Color::Blue;
        case TaskCategory::Officers:  return Color::Green;
        case TaskCategory::Mining:    return Color(Color::Gold1);
        case TaskCategory::Combat:    return Color::Red;
        case TaskCategory::Alliance:  return Color::MagentaLight;
        case TaskCategory::Store:     return Color::GreenLight;
        case TaskCategory::Misc:      return Color::GrayLight;
    }
    return Color::White;
}

// ---------------------------------------------------------------------------
// View: Overview — Player-centric dashboard
// ---------------------------------------------------------------------------

static Element render_overview(AppState& state) {
    auto& gd = state.game_data;
    auto& pd = state.player_data;
    bool has_player = !pd.officers.empty() || !pd.ships.empty() || !pd.resources.empty();

    // --- Player identity header ---
    std::string player_label = pd.player_name.empty() ? "Unknown Commander" : pd.player_name;
    std::string ops_label = pd.ops_level > 0 ? "Ops " + std::to_string(pd.ops_level) : "";

    // Last sync time
    std::string sync_label = "Never";
    if (pd.last_sync != std::chrono::system_clock::time_point{}) {
        auto tt = std::chrono::system_clock::to_time_t(pd.last_sync);
        std::tm tm_buf{};
        localtime_r(&tt, &tm_buf);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        sync_label = buf;
    }

    auto identity_box = vbox({
        hbox({
            text(player_label) | bold | color(Color::Cyan),
            ops_label.empty() ? text("") : hbox({text("  "), text(ops_label) | color(Color::Yellow) | bold}),
            filler(),
            text("Last sync: ") | dim,
            text(sync_label) | (pd.last_sync != std::chrono::system_clock::time_point{}
                ? color(Color::Green) : color(Color::GrayDark)),
        }),
    });

    // --- Resources panel (key resources with amounts) ---
    // Well-known resource IDs for STFC
    // We'll show all resources sorted by amount, highlighting the big 3 if found
    Elements resource_lines;
    resource_lines.push_back(text("Resources") | bold);
    resource_lines.push_back(separator());

    if (has_player && !pd.resources.empty()) {
        // Sort by amount descending, show top 12
        auto sorted_res = pd.resources;
        std::sort(sorted_res.begin(), sorted_res.end(),
            [](const PlayerResource& a, const PlayerResource& b) {
                return a.amount > b.amount;
            });

        int shown = 0;
        for (auto& r : sorted_res) {
            if (shown >= 12) break;
            if (r.amount == 0) continue;
            std::string name = r.name.empty() ? ("ID:" + std::to_string(r.resource_id)) : r.name;
            // Truncate long names
            if (name.size() > 22) name = name.substr(0, 20) + "..";
            resource_lines.push_back(hbox({
                text("  " + name) | size(WIDTH, EQUAL, 24),
                text(format_number(r.amount)) | bold | color(Color::Green),
            }));
            shown++;
        }
        if (shown == 0) {
            resource_lines.push_back(text("  No resources tracked") | dim);
        }
    } else {
        resource_lines.push_back(text("  No player data synced yet") | dim);
        resource_lines.push_back(text("  Start sync with [S]") | dim);
    }

    // --- Active Jobs panel ---
    Elements job_lines;
    job_lines.push_back(text("Active Jobs") | bold);
    job_lines.push_back(separator());

    if (has_player) {
        int active_count = 0;
        for (auto& j : pd.jobs) {
            if (j.completed) continue;
            int remaining = job_remaining_seconds(j);
            if (remaining < -3600) continue; // Skip jobs finished >1hr ago

            std::string type_str = job_type_str(j.job_type);
            std::string time_str = remaining > 0
                ? format_duration_short(remaining)
                : "DONE";
            Color time_color = remaining > 0
                ? (remaining < 300 ? Color::Yellow : Color::White)
                : Color::Green;

            job_lines.push_back(hbox({
                text("  " + type_str) | size(WIDTH, EQUAL, 18),
                text(time_str) | bold | color(time_color),
            }));
            active_count++;
        }
        if (active_count == 0) {
            job_lines.push_back(text("  No active jobs") | dim);
            job_lines.push_back(text("  Queue is empty!") | color(Color::Yellow));
        }
    } else {
        job_lines.push_back(text("  Awaiting sync data") | dim);
    }

    // --- Ship progression panel ---
    Elements ship_lines;
    ship_lines.push_back(text("Ship Fleet") | bold);
    ship_lines.push_back(separator());

    if (has_player && !pd.ships.empty()) {
        // Group by hull type, show top tier ships
        auto sorted_ships = pd.ships;
        std::sort(sorted_ships.begin(), sorted_ships.end(),
            [](const PlayerShip& a, const PlayerShip& b) {
                return a.tier > b.tier || (a.tier == b.tier && a.level > b.level);
            });

        ship_lines.push_back(hbox({
            text("  Total: ") | dim,
            text(std::to_string(pd.ships.size())) | bold,
            text(" ships") | dim,
        }));

        int shown = 0;
        for (auto& s : sorted_ships) {
            if (shown >= 6) break;
            std::string name = s.name.empty() ? ("Hull#" + std::to_string(s.hull_id)) : s.name;
            if (name.size() > 18) name = name.substr(0, 16) + "..";

            // Look up hull type from game data
            std::string type_tag = "";
            auto git = gd.ships.find(s.hull_id);
            if (git != gd.ships.end()) {
                type_tag = std::string(hull_type_str(git->second.hull_type)).substr(0, 3);
            }

            ship_lines.push_back(hbox({
                text("  " + name) | size(WIDTH, EQUAL, 20),
                text("T" + std::to_string(s.tier)) | bold | color(Color::Cyan),
                text(" Lv" + std::to_string(s.level)) | dim,
                type_tag.empty() ? text("") : hbox({text(" "), text(type_tag) | dim}),
            }));
            shown++;
        }
        if (pd.ships.size() > 6) {
            ship_lines.push_back(text("  +" + std::to_string(pd.ships.size() - 6) + " more...") | dim);
        }
    } else {
        ship_lines.push_back(text("  Awaiting sync data") | dim);
    }

    // --- Officer roster summary ---
    Elements officer_lines;
    officer_lines.push_back(text("Officer Roster") | bold);
    officer_lines.push_back(separator());

    if (has_player && !pd.officers.empty()) {
        officer_lines.push_back(hbox({
            text("  Total: ") | dim,
            text(std::to_string(pd.officers.size())) | bold,
            text(" officers") | dim,
        }));

        // Count by rank
        std::map<int, int> rank_counts;
        int max_level = 0;
        for (auto& o : pd.officers) {
            rank_counts[o.rank]++;
            if (o.level > max_level) max_level = o.level;
        }
        officer_lines.push_back(hbox({
            text("  Max level: ") | dim,
            text(std::to_string(max_level)) | bold | color(Color::Yellow),
        }));

        for (auto& [rank, count] : rank_counts) {
            officer_lines.push_back(hbox({
                text("  Rank " + std::to_string(rank) + ": ") | dim,
                text(std::to_string(count)) | bold,
            }));
        }
    } else {
        officer_lines.push_back(text("  Awaiting sync data") | dim);
    }

    // --- Daily progress summary ---
    auto& dp = state.daily_plan;
    std::string pct_str = std::to_string((int)dp.completion_pct()) + "%";
    auto daily_summary = vbox({
        text("Today's Progress") | bold,
        separator(),
        hbox({text("  Completed: "), text(std::to_string(dp.completed_tasks()) + "/" + std::to_string(dp.total_tasks())) | bold}),
        hbox({text("  Remaining: "), text(std::to_string(dp.remaining_tasks()) + " (" + std::to_string(dp.remaining_estimated_minutes()) + " min)")}),
        hbox({text("  Progress:  "), text(pct_str) | bold | color(dp.completion_pct() >= 80 ? Color::Green : dp.completion_pct() >= 50 ? Color::Yellow : Color::Red)}),
        gauge(dp.completion_pct() / 100.0) | color(Color::Green),
    });

    // --- Game data stats (compact) ---
    auto game_stats = vbox({
        text("Game Data") | bold,
        separator(),
        hbox({text("  Officers:  "), text(std::to_string(gd.officers.size())) | dim}),
        hbox({text("  Ships:     "), text(std::to_string(gd.ships.size())) | dim}),
        hbox({text("  Research:  "), text(std::to_string(gd.researches.size())) | dim}),
        hbox({text("  Buildings: "), text(std::to_string(gd.buildings.size())) | dim}),
        hbox({text("  Resources: "), text(std::to_string(gd.resources.size())) | dim}),
    });

    auto ingress_status = state.ingress_server.is_running()
        ? hbox({text(" INGRESS: "), text("RUNNING on port " + std::to_string(state.ingress_server.port())) | color(Color::Green)})
        : hbox({text(" INGRESS: "), text("STOPPED") | color(Color::GrayDark)});

    auto crew_status = state.crew_loaded
        ? hbox({text(" ROSTER: "), text(std::to_string(state.roster.size()) + " officers loaded") | color(Color::Green)})
        : hbox({text(" ROSTER: "), text("No roster — sync from Sync tab") | color(Color::Yellow)});

    return vbox({
        text("STFC Tool - Dashboard") | bold | center,
        separator(),
        identity_box,
        separator(),
        hbox({
            vbox({vbox(resource_lines), separator(), vbox(job_lines)}) | flex,
            separator(),
            vbox({vbox(ship_lines), separator(), vbox(officer_lines)}) | flex,
            separator(),
            vbox({daily_summary, separator(), game_stats}) | flex,
        }) | flex,
        separator(),
        hbox({ingress_status, text("  "), crew_status}),
    }) | vscroll_indicator | yframe;
}

// ---------------------------------------------------------------------------
// View: Daily Planner
// ---------------------------------------------------------------------------

static Element render_daily_planner(AppState& state) {
    auto& plan = state.daily_plan;

    // Header with date and progress
    bool has_player_data = !state.player_data.officers.empty() || !state.player_data.jobs.empty();
    auto header = vbox({
        hbox({
            text("Daily Planner") | bold,
            text(" - "),
            text(plan.date) | color(Color::Cyan),
            text("  "),
            has_player_data
                ? (text("[LIVE]") | color(Color::Green) | bold)
                : (text("[no sync data]") | color(Color::GrayDark)),
            filler(),
            text(std::to_string(plan.completed_tasks()) + "/" + std::to_string(plan.total_tasks()) + " done") | bold,
            text("  ~" + std::to_string(plan.remaining_estimated_minutes()) + " min left") | dim,
        }),
        gauge(plan.completion_pct() / 100.0) | color(Color::Green),
    });

    // Task list sorted by effective_score (impact + dynamic boost)
    // No category grouping — priority order IS the display order
    Elements task_rows;

    // Find the first incomplete task index (for "DO THIS FIRST" highlight)
    int first_incomplete = -1;
    for (size_t i = 0; i < plan.tasks.size(); ++i) {
        if (!plan.tasks[i].completed && !plan.tasks[i].skipped) {
            first_incomplete = static_cast<int>(i);
            break;
        }
    }

    for (size_t i = 0; i < plan.tasks.size(); ++i) {
        const auto& t = plan.tasks[i];

        // Skip completed if toggled
        if (!state.show_completed && t.completed) continue;
        if (t.skipped && !state.show_completed) continue;

        // Task row
        bool selected = ((int)i == state.selected_daily_task);
        bool is_top_task = ((int)i == first_incomplete);
        auto dp = t.display_priority();

        std::string check = t.completed ? "[x]" : (t.skipped ? "[-]" : "[ ]");
        std::string pri_icon = priority_icon(dp);
        std::string time_str = t.estimated_minutes > 0
            ? "~" + std::to_string(t.estimated_minutes) + "m"
            : "";

        auto task_text = t.completed
            ? text(t.title) | dim | strikethrough
            : (t.skipped
                ? text(t.title) | dim
                : text(t.title));

        // "DO THIS FIRST" marker for the top incomplete task
        auto top_marker = is_top_task
            ? (text(" >> DO THIS FIRST") | color(Color::Green) | bold)
            : text("");

        // Urgency badge: [!] shown separately from priority for time-sensitive tasks
        auto urgency_badge = (t.urgent && !t.completed && !t.skipped)
            ? (text("[!]") | color(Color::Yellow) | bold)
            : text("   ");

        auto row = hbox({
            text("  "),
            text(check) | color(t.completed ? Color::Green : (t.skipped ? Color::GrayDark : Color::White)),
            text(" "),
            text(pri_icon) | color(priority_color(dp)),
            text(" "),
            urgency_badge,
            text(" "),
            text(std::string(category_icon(t.category)) + " ") | color(category_color(t.category)) | dim,
            task_text | flex,
            // Show inline status from player data
            t.queue_idle
                ? (text(" IDLE!") | color(Color::Red) | bold)
                : (t.has_active_job
                    ? (text(" active") | color(Color::Green) | dim)
                    : text("")),
            top_marker,
            text(" "),
            text(time_str) | dim,
        });

        if (selected) {
            row = row | inverted | focus;
        }

        task_rows.push_back(row);
    }

    // Detail panel for selected task
    Element detail = text("");
    if (state.selected_daily_task >= 0 && state.selected_daily_task < (int)plan.tasks.size()) {
        const auto& t = plan.tasks[state.selected_daily_task];
        Elements detail_lines;
        auto dp = t.display_priority();
        detail_lines.push_back(text(t.title) | bold);
        detail_lines.push_back(separator());
        detail_lines.push_back(hbox({
            text("Priority: "),
            text(priority_str(dp)) | color(priority_color(dp)),
            text("  (impact:" + std::to_string(t.impact_score)),
            t.dynamic_boost > 0
                ? (text("+" + std::to_string(t.dynamic_boost)) | color(Color::Green))
                : text(""),
            text(")") | dim,
        }));
        detail_lines.push_back(hbox({text("Category: "), text(category_str(t.category)) | color(category_color(t.category))}));
        if (t.urgent) {
            detail_lines.push_back(text("[!] TIME SENSITIVE") | color(Color::Yellow) | bold);
        }
        if (t.estimated_minutes > 0) {
            detail_lines.push_back(hbox({text("Time:     ~"), text(std::to_string(t.estimated_minutes) + " min")}));
        }
        if (!t.best_time.empty()) {
            detail_lines.push_back(hbox({text("When:     "), text(t.best_time) | color(Color::Cyan)}));
        }
        detail_lines.push_back(separator());

        // Word-wrap description to ~50 chars
        std::string desc = t.description;
        while (!desc.empty()) {
            if (desc.size() <= 55) {
                detail_lines.push_back(text(desc) | dim);
                break;
            }
            size_t cut = desc.rfind(' ', 55);
            if (cut == std::string::npos || cut == 0) cut = 55;
            detail_lines.push_back(text(desc.substr(0, cut)) | dim);
            desc = desc.substr(cut + (desc[cut] == ' ' ? 1 : 0));
        }

        // Player data context hints
        if (!t.context_hints.empty()) {
            detail_lines.push_back(separator());
            detail_lines.push_back(text("Your Status:") | bold | color(Color::Cyan));
            for (const auto& hint : t.context_hints) {
                Color hint_color = Color::White;
                if (hint.find("IDLE") != std::string::npos || hint.find("!!") != std::string::npos) {
                    hint_color = Color::Red;
                } else if (hint.find("DONE") != std::string::npos) {
                    hint_color = Color::Green;
                } else if (hint.find("left") != std::string::npos) {
                    hint_color = Color::Yellow;
                }
                detail_lines.push_back(text("  " + hint) | color(hint_color));
            }
        }

        if (t.progress_total > 0) {
            detail_lines.push_back(separator());
            detail_lines.push_back(hbox({
                text("Progress: "),
                text(std::to_string(t.progress_current) + "/" + std::to_string(t.progress_total)),
            }));
            detail_lines.push_back(gauge((double)t.progress_current / t.progress_total));
        }

        if (t.skipped && !t.skip_reason.empty()) {
            detail_lines.push_back(separator());
            detail_lines.push_back(hbox({text("Skipped: "), text(t.skip_reason) | color(Color::Yellow)}));
        }

        detail = vbox(detail_lines) | border;
    }

    return vbox({
        header,
        separator(),
        hbox({
            vbox(task_rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 60),
        }) | flex,
        separator(),
        hbox({
            text(" [SPACE] Toggle  ") | dim,
            text("[S] Skip  ") | dim,
            text("[C] Show/Hide Done  ") | dim,
            text("[Up/Down] Navigate") | dim,
        }),
    });
}

// ---------------------------------------------------------------------------
// View: Weekly Planner
// ---------------------------------------------------------------------------

static const char* short_dow(int i) {
    // i = 0..6 mapping to Mon..Sun
    static const char* names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    if (i >= 0 && i <= 6) return names[i];
    return "?";
}

static Element render_weekly_planner(AppState& state) {
    auto& plan = state.weekly_plan;

    // Header
    auto header = vbox({
        hbox({
            text("Weekly Planner") | bold,
            text(" - Week of "),
            text(plan.week_start) | color(Color::Cyan),
            filler(),
            text(std::to_string(plan.completed_goals()) + "/" + std::to_string(plan.goals.size()) + " goals") | bold,
        }),
        gauge(plan.goal_completion_pct() / 100.0) | color(Color::Blue),
    });

    // Day tabs with progress
    Elements day_tabs;
    for (int i = 0; i < 7 && i < (int)plan.days.size(); ++i) {
        bool sel = (i == state.selected_weekly_day);
        auto& day = plan.days[i];
        std::string label = std::string(short_dow(i)) + " " +
            std::to_string(day.completed_tasks()) + "/" + std::to_string(day.total_tasks());

        auto tab = text(label);
        if (sel) {
            tab = tab | bold | inverted;
        } else if (day.completion_pct() >= 100) {
            tab = tab | color(Color::Green);
        } else if (day.completion_pct() >= 50) {
            tab = tab | color(Color::Yellow);
        }
        day_tabs.push_back(tab);
        if (i < 6) day_tabs.push_back(text(" | ") | dim);
    }

    // Selected day's task summary
    Elements day_tasks;
    if (state.selected_weekly_day >= 0 && state.selected_weekly_day < (int)plan.days.size()) {
        auto& day = plan.days[state.selected_weekly_day];
        day_tasks.push_back(hbox({
            text(std::string(short_dow(state.selected_weekly_day)) + " - " + day.date) | bold,
            filler(),
            text(std::to_string((int)day.completion_pct()) + "% done") | dim,
        }));
        day_tasks.push_back(gauge(day.completion_pct() / 100.0) | color(Color::Green));
        day_tasks.push_back(separatorEmpty());

        // Show tasks grouped by priority
        for (auto pri : {TaskPriority::Critical, TaskPriority::High, TaskPriority::Medium, TaskPriority::Low}) {
            bool any = false;
            for (const auto& t : day.tasks) {
                if (t.priority != pri) continue;
                if (!any) {
                    day_tasks.push_back(
                        text(std::string(priority_str(pri)) + ":") | bold | color(priority_color(pri))
                    );
                    any = true;
                }
                std::string check = t.completed ? "[x]" : (t.skipped ? "[-]" : "[ ]");
                auto label = t.completed
                    ? text(t.title) | dim | strikethrough
                    : (t.skipped ? text(t.title) | dim : text(t.title));
                day_tasks.push_back(hbox({
                    text("  " + check + " ") | color(t.completed ? Color::Green : Color::White),
                    label | flex,
                    text("~" + std::to_string(t.estimated_minutes) + "m") | dim,
                }));
            }
        }
    }

    // Weekly goals panel
    Elements goal_rows;
    goal_rows.push_back(text("Weekly Goals") | bold | color(Color::Blue));
    goal_rows.push_back(separator());

    for (size_t i = 0; i < plan.goals.size(); ++i) {
        const auto& g = plan.goals[i];
        bool sel = ((int)i == state.selected_weekly_goal);

        std::string check = g.completed ? "[x]" : "[ ]";
        double pct = (g.progress_total > 0) ? (double)g.progress_current / g.progress_total : 0;
        std::string prog_str = std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total);

        auto row = hbox({
            text(check + " ") | color(g.completed ? Color::Green : Color::White),
            text(category_icon(g.category)) | color(category_color(g.category)),
            text(" "),
            (g.completed ? text(g.title) | dim | strikethrough : text(g.title)) | flex,
            text(prog_str) | dim,
        });

        if (sel) row = row | inverted | focus;
        goal_rows.push_back(row);

        // Mini progress bar
        goal_rows.push_back(hbox({
            text("      "),
            gauge(pct) | size(WIDTH, EQUAL, 30) | color(g.completed ? Color::Green : Color::Cyan),
        }));
    }

    // Goal detail
    Element goal_detail = text("");
    if (state.selected_weekly_goal >= 0 && state.selected_weekly_goal < (int)plan.goals.size()) {
        const auto& g = plan.goals[state.selected_weekly_goal];
        Elements lines;
        lines.push_back(text(g.title) | bold);
        lines.push_back(separator());
        lines.push_back(hbox({text("Category: "), text(category_str(g.category)) | color(category_color(g.category))}));
        lines.push_back(hbox({text("Priority: "), text(priority_str(g.priority)) | color(priority_color(g.priority))}));
        lines.push_back(hbox({text("Progress: "), text(std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total))}));
        lines.push_back(separator());

        std::string desc = g.description;
        while (!desc.empty()) {
            if (desc.size() <= 50) { lines.push_back(text(desc) | dim); break; }
            size_t cut = desc.rfind(' ', 50);
            if (cut == std::string::npos || cut == 0) cut = 50;
            lines.push_back(text(desc.substr(0, cut)) | dim);
            desc = desc.substr(cut + (desc[cut] == ' ' ? 1 : 0));
        }

        goal_detail = vbox(lines) | border;
    }

    return vbox({
        header,
        separator(),
        hbox(day_tabs) | center,
        separator(),
        hbox({
            vbox(day_tasks) | vscroll_indicator | yframe | flex,
            separator(),
            vbox({
                vbox(goal_rows) | vscroll_indicator | yframe | flex,
                separator(),
                goal_detail,
            }) | flex,
        }) | flex,
        separator(),
        hbox({
            text(" [Left/Right] Day  ") | dim,
            text("[Up/Down] Goal  ") | dim,
            text("[+/-] Goal Progress  ") | dim,
        }),
    });
}

// ---------------------------------------------------------------------------
// View: Crew Optimizer
// ---------------------------------------------------------------------------

static Element render_crew_optimizer(AppState& state) {
    if (!state.crew_loaded) {
        return vbox({
            text("Crew Optimizer") | bold | center,
            separator(),
            text("No roster loaded.") | center,
            text("Use the Sync tab to sync your account from the community mod.") | center | dim,
            text("Or place roster.csv in the project root as a fallback.") | center | dim,
        });
    }

    const auto& scenarios = all_dock_scenarios();
    int safe_scenario = std::clamp(state.crew_scenario, 0, (int)scenarios.size() - 1);
    std::string scenario_name = scenario_label(scenarios[safe_scenario]);

    static const char* ship_names[] = {"Explorer", "Battleship", "Interceptor", "Survey"};
    int safe_ship = std::clamp(state.crew_ship_type, 0, 3);
    std::string ship_name = ship_names[safe_ship];

    auto header = vbox({
        text("Crew Optimizer") | bold | center,
        separator(),
        hbox({
            text("  Scenario: "),
            text(scenario_name) | bold | color(Color::Cyan),
            text("  [</>] change") | dim,
            filler(),
            text("  Ship: "),
            text(ship_name) | bold | color(Color::Yellow),
            text("  [T] cycle") | dim,
        }),
        hbox({
            text("  Roster: "),
            text(std::to_string(state.roster.size()) + " officers") | dim,
            filler(),
            text("  [Enter] Run Optimizer") | dim,
        }),
    });

    // Results
    Elements result_rows;
    if (state.crew_results.empty()) {
        result_rows.push_back(text("Press [Enter] to run the optimizer for this scenario.") | center | dim);
    } else {
        for (size_t i = 0; i < state.crew_results.size(); ++i) {
            const auto& r = state.crew_results[i];
            const auto& bd = r.breakdown;
            bool sel = ((int)i == state.selected_crew);

            std::string rank_str = "#" + std::to_string(i + 1);
            std::string score_str = std::to_string((int)r.score);

            auto row = vbox({
                hbox({
                    text(rank_str) | bold | color(i == 0 ? Color(Color::Gold1) : (i == 1 ? Color(Color::GrayLight) : Color(Color::GrayDark))),
                    text("  Score: "),
                    text(score_str) | bold,
                    filler(),
                    text("Captain: "),
                    text(bd.captain) | bold | color(Color::Yellow),
                }),
                hbox({
                    text("    Bridge: "),
                    text(bd.bridge.size() > 0 ? bd.bridge[0] : "?") | color(Color::Cyan),
                    text(" + "),
                    text(bd.bridge.size() > 1 ? bd.bridge[1] : "?") | color(Color::Cyan),
                }),
            });

            if (sel) {
                row = row | inverted | focus;
            }
            result_rows.push_back(row);
            if (i < state.crew_results.size() - 1) {
                result_rows.push_back(separatorLight());
            }
        }
    }

    // Detail panel for selected crew
    Element detail = text("");
    if (state.selected_crew >= 0 && state.selected_crew < (int)state.crew_results.size()) {
        const auto& r = state.crew_results[state.selected_crew];
        const auto& bd = r.breakdown;

        Elements lines;
        lines.push_back(text("Crew Breakdown") | bold);
        lines.push_back(separator());

        lines.push_back(text("Individual Scores:") | bold);
        for (const auto& [name, score] : bd.individual_scores) {
            bool is_captain = (name == bd.captain);
            lines.push_back(hbox({
                text("  "),
                text(is_captain ? "[CPT] " : "      "),
                text(name) | (is_captain ? bold : nothing),
                filler(),
                text(std::to_string((int)score)),
            }));
        }

        lines.push_back(separator());
        lines.push_back(text("Bonuses:") | bold);
        if (bd.synergy_bonus > 0)
            lines.push_back(hbox({text("  Synergy:       +"), text(std::to_string((int)bd.synergy_bonus)) | color(Color::Green)}));
        if (bd.bridge_synergy_bonus > 0) {
            std::string bars_l = (bd.bridge_synergy_bars_left == 2) ? "##" : (bd.bridge_synergy_bars_left == 1 ? "#" : "-");
            std::string bars_r = (bd.bridge_synergy_bars_right == 2) ? "##" : (bd.bridge_synergy_bars_right == 1 ? "#" : "-");
            lines.push_back(hbox({text("  Group Synergy: +"), text(std::to_string((int)bd.bridge_synergy_bonus)) | color(Color::Green),
                                  text(" [" + bars_l + "|" + bars_r + "] +" + std::to_string((int)bd.bridge_synergy_pct) + "% CM") | color(Color::Yellow)}));
        }
        if (bd.state_chain_bonus > 0)
            lines.push_back(hbox({text("  State Chain:   +"), text(std::to_string((int)bd.state_chain_bonus)) | color(Color::Green)}));
        if (bd.crit_bonus > 0)
            lines.push_back(hbox({text("  Crit Coverage: +"), text(std::to_string((int)bd.crit_bonus)) | color(Color::Green)}));
        if (bd.ship_type_bonus > 0)
            lines.push_back(hbox({text("  Ship Spec:     +"), text(std::to_string((int)bd.ship_type_bonus)) | color(Color::Green)}));
        if (bd.scenario_bonus > 0)
            lines.push_back(hbox({text("  Scenario:      +"), text(std::to_string((int)bd.scenario_bonus)) | color(Color::Green)}));
        if (bd.weakness_counter_bonus > 0)
            lines.push_back(hbox({text("  Weakness Cnt:  +"), text(std::to_string((int)bd.weakness_counter_bonus)) | color(Color::Green)}));
        if (bd.dual_use_bonus > 0)
            lines.push_back(hbox({text("  Dual Use:      +"), text(std::to_string((int)bd.dual_use_bonus)) | color(Color::Green)}));
        if (bd.amplifier_bonus > 0)
            lines.push_back(hbox({text("  Amplifier:     +"), text(std::to_string((int)bd.amplifier_bonus)) | color(Color::Green)}));

        if (!bd.penalties.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Penalties:") | bold | color(Color::Red));
            for (const auto& p : bd.penalties) {
                // Wrap penalty text
                std::string s = p;
                while (!s.empty()) {
                    if (s.size() <= 48) { lines.push_back(text("  " + s) | color(Color::Red) | dim); break; }
                    size_t cut = s.rfind(' ', 48);
                    if (cut == std::string::npos || cut == 0) cut = 48;
                    lines.push_back(text("  " + s.substr(0, cut)) | color(Color::Red) | dim);
                    s = s.substr(cut + 1);
                }
            }
        }

        if (!bd.synergy_notes.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Synergy Notes:") | bold | color(Color::Blue));
            for (const auto& note : bd.synergy_notes) {
                std::string s = note;
                while (!s.empty()) {
                    if (s.size() <= 48) { lines.push_back(text("  " + s) | color(Color::Blue) | dim); break; }
                    size_t cut = s.rfind(' ', 48);
                    if (cut == std::string::npos || cut == 0) cut = 48;
                    lines.push_back(text("  " + s.substr(0, cut)) | color(Color::Blue) | dim);
                    s = s.substr(cut + 1);
                }
            }
        }

        // BDA suggestions for this crew
        if (!state.crew_bda_results.empty()) {
            lines.push_back(separator());
            lines.push_back(text("BDA Suggestions:  [B] cycle") | bold | color(Color::Magenta));
            for (size_t bi = 0; bi < state.crew_bda_results.size(); ++bi) {
                const auto& bda = state.crew_bda_results[bi];
                bool bda_sel = ((int)bi == state.selected_crew_bda);
                auto bda_row = hbox({
                    text(bda_sel ? " >> " : "    "),
                    text("#" + std::to_string(bi + 1) + " "),
                    text(bda.name) | bold | color(Color::Cyan),
                    filler(),
                    text("OA:" + std::to_string((int)bda.oa_pct) + "%") | dim,
                    text("  Score:" + std::to_string((int)bda.score)) | dim,
                });
                if (bda_sel) bda_row = bda_row | bgcolor(Color::GrayDark);
                lines.push_back(bda_row);
            }

            // Detail panel for selected BDA
            int sbi = state.selected_crew_bda;
            if (sbi >= 0 && sbi < (int)state.crew_bda_results.size()) {
                const auto& bda = state.crew_bda_results[sbi];
                lines.push_back(separator());
                lines.push_back(text("BDA Details:") | bold | color(Color::Magenta));

                // Name, level, rank, type
                auto name_row_parts = Elements{
                    text("  "),
                    text(bda.name) | bold | color(Color::Cyan),
                    text("  Lv" + std::to_string(bda.level) + " Rk" + std::to_string(bda.rank)),
                };
                if (!bda.officer_type.empty()) {
                    name_row_parts.push_back(text("  [" + bda.officer_type + "]") | dim);
                }
                if (bda.is_dedicated_bda) {
                    name_row_parts.push_back(text("  [BDA]") | color(Color::Yellow));
                }
                lines.push_back(hbox(name_row_parts));

                // Stats
                lines.push_back(hbox({
                    text("  Atk:"),
                    text(std::to_string((int)bda.attack)) | dim,
                    text("  Def:"),
                    text(std::to_string((int)bda.defense)) | dim,
                    text("  HP:"),
                    text(std::to_string((int)bda.health)) | dim,
                }));

                // Tag labels
                Elements tag_parts;
                if (bda.cargo)              tag_parts.push_back(text(" [Cargo]") | color(Color::Green));
                if (bda.protected_cargo)    tag_parts.push_back(text(" [Protected]") | color(Color::GreenLight));
                if (bda.loot)               tag_parts.push_back(text(" [Loot]") | color(Color::Yellow));
                if (bda.mining)             tag_parts.push_back(text(" [Mining]") | color(Color::Blue));
                if (bda.crit_related)       tag_parts.push_back(text(" [Crit]") | color(Color::Red));
                if (bda.shield_related)     tag_parts.push_back(text(" [Shield]") | color(Color::Cyan));
                if (bda.mitigation_related) tag_parts.push_back(text(" [Mitigation]") | color(Color::CyanLight));
                if (bda.repair)             tag_parts.push_back(text(" [Repair]") | color(Color::MagentaLight));
                if (bda.armada)             tag_parts.push_back(text(" [Armada]") | color(Color::YellowLight));
                if (!tag_parts.empty()) {
                    tag_parts.insert(tag_parts.begin(), text(" "));
                    lines.push_back(hbox(tag_parts));
                }

                // Percentage values when non-zero
                Elements pct_parts;
                if (bda.oa_cargo_pct > 0.0)
                    pct_parts.push_back(text("  Cargo+" + std::to_string((int)(bda.oa_cargo_pct * 100)) + "%") | color(Color::Green));
                if (bda.oa_protected_cargo_pct > 0.0)
                    pct_parts.push_back(text("  ProtCargo+" + std::to_string((int)(bda.oa_protected_cargo_pct * 100)) + "%") | color(Color::GreenLight));
                if (bda.oa_mining_speed_pct > 0.0)
                    pct_parts.push_back(text("  Mining+" + std::to_string((int)(bda.oa_mining_speed_pct * 100)) + "%") | color(Color::Blue));
                if (!pct_parts.empty()) {
                    lines.push_back(hbox(pct_parts));
                }

                // BDA ability text
                if (!bda.bda_text.empty()) {
                    lines.push_back(hbox({
                        text("  BDA: ") | dim,
                        text(bda.bda_text) | color(Color::Magenta),
                    }));
                    if (bda.bda_value > 0.0) {
                        lines.push_back(text("       Value: " + std::to_string((int)(bda.bda_value * 100)) + "%") | dim);
                    }
                }

                // OA ability text
                if (!bda.oa_text.empty()) {
                    lines.push_back(hbox({
                        text("  OA: ") | dim,
                        text(bda.oa_text) | color(Color::CyanLight),
                    }));
                    if (bda.oa_value > 0.0) {
                        lines.push_back(text("       Value: " + std::to_string((int)(bda.oa_value * 100)) + "%") | dim);
                    }
                }

                // Scoring reasons
                for (const auto& reason : bda.reasons) {
                    lines.push_back(text("  " + reason) | dim | color(Color::Magenta));
                }
            }
        }

        detail = vbox(lines);
    }

    return vbox({
        header,
        separator(),
        hbox({
            vbox(result_rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 55) | vscroll_indicator | yframe,
        }) | flex,
    });
}

// ---------------------------------------------------------------------------
// View: Loadout (7-dock system)
// ---------------------------------------------------------------------------

static Element render_loadout(AppState& state) {
    if (!state.crew_loaded) {
        return vbox({
            text("7-Dock Loadout Optimizer") | bold | center,
            separator(),
            text("No roster loaded.") | center,
            text("Use the Sync tab to sync your account from the community mod.") | center | dim,
        });
    }

    // Header
    static const char* ship_names[] = {"Explorer", "Battleship", "Interceptor", "Survey"};
    int safe_ship = std::clamp(state.crew_ship_type, 0, 3);
    std::string ship_name = ship_names[safe_ship];

    Elements header_lines;
    header_lines.push_back(text("7-Dock Loadout Optimizer") | bold | center);
    header_lines.push_back(separator());
    header_lines.push_back(hbox({
        text("  Ship: "),
        text(ship_name) | bold | color(Color::Yellow),
        text("  [T] cycle") | dim,
        text("   Posture: ") | dim,
        text(state.account_analysis.posture_label.empty() ? posture_label(state.loadout_posture) : state.account_analysis.posture_label)
            | bold | color(Color::Cyan),
        text("  [P] cycle") | dim,
        filler(),
        text("  [G] Optimize All Docks") | dim,
        text("  [L] Load Saved") | dim,
    }));
    for (size_t i = 0; i < state.account_analysis.summary_notes.size() && i < 3; ++i) {
        header_lines.push_back(text("  " + state.account_analysis.summary_notes[i]) | dim);
    }
    if (!state.loadout_error.empty()) {
        header_lines.push_back(separator());
        header_lines.push_back(
            text("  ERROR: " + state.loadout_error)
                | color(Color::Red) | bold);
    }
    if (state.loadout_running) {
        header_lines.push_back(separator());
        header_lines.push_back(
            text("  Optimizing... (running in background)")
                | color(Color::Yellow) | bold);
    }
    auto header = vbox(header_lines);

    // Dock list (left panel)
    Elements dock_rows;
    for (int d = 0; d < 7; ++d) {
        bool sel = (d == state.selected_dock);
        auto& cfg = state.dock_configs[d];
        std::string label = "Dock " + std::to_string(d + 1) + ": ";
        std::string scen = scenario_label(cfg.scenario);

        Elements row_parts;
        row_parts.push_back(text(label) | bold);
        row_parts.push_back(text(scen) | color(Color::Cyan));
        std::string mining_desc = mining_descriptor(cfg);
        if (!mining_desc.empty()) {
            row_parts.push_back(text(" {" + mining_desc + "}") | color(Color::Green));
        }
        if (!cfg.purpose.empty()) {
            row_parts.push_back(text(" [" + cfg.purpose + "]") | dim);
        }

        if (cfg.locked) {
            row_parts.push_back(text(" [LOCKED]") | color(Color::Yellow) | dim);
        }

        // Show crew if computed
        if (state.loadout_computed && d < (int)state.loadout_result.docks.size()) {
            const auto& dr = state.loadout_result.docks[d];
            row_parts.push_back(filler());
            row_parts.push_back(text(dr.captain) | bold | color(Color::Yellow));
            if (dr.bridge.size() >= 2) {
                row_parts.push_back(text(" + ") | dim);
                row_parts.push_back(text(dr.bridge[0]) | color(Color::Cyan));
                row_parts.push_back(text(" + ") | dim);
                row_parts.push_back(text(dr.bridge[1]) | color(Color::Cyan));
            }
            row_parts.push_back(text("  "));
            row_parts.push_back(text(std::to_string((int)dr.score)) | dim);
        }

        auto row = hbox(row_parts);
        if (sel) row = row | inverted | focus;
        dock_rows.push_back(row);

        // Show BDA for selected dock
        if (sel && state.loadout_computed && d < (int)state.loadout_result.docks.size()) {
            const auto& dr = state.loadout_result.docks[d];
            if (!dr.bda_suggestions.empty()) {
                dock_rows.push_back(text("  BDA Suggestions:") | dim | color(Color::Magenta));
                for (size_t bi = 0; bi < dr.bda_suggestions.size(); ++bi) {
                    const auto& bda = dr.bda_suggestions[bi];
                    bool bda_sel = (sel && (int)bi == state.selected_dock_bda);
                    auto bda_row = hbox({
                        text("    #" + std::to_string(bi + 1) + " "),
                        text(bda.name) | bold | color(Color::Cyan),
                        filler(),
                        text("OA:" + std::to_string((int)bda.oa_pct) + "%") | dim,
                        text("  Score:" + std::to_string((int)bda.score)) | dim,
                    });
                    if (bda_sel) bda_row = bda_row | bgcolor(Color::GrayDark);
                    dock_rows.push_back(bda_row);
                }
            }
        }

        if (d < 6) dock_rows.push_back(separatorLight());
    }

    // Detail panel (right) — selected dock breakdown
    Element detail = text("");
    if (state.loadout_computed && state.selected_dock >= 0 &&
        state.selected_dock < (int)state.loadout_result.docks.size()) {
        const auto& dr = state.loadout_result.docks[state.selected_dock];
        const auto& bd = dr.breakdown;
        Elements lines;

        lines.push_back(hbox({
            text("Dock " + std::to_string(dr.dock_num)) | bold,
            text(" - "),
            text(dr.scenario_label_str) | color(Color::Cyan),
        }));
        lines.push_back(separator());

        lines.push_back(hbox({text("Ship: "), text(dr.ship_used) | bold | color(Color::Yellow)}));
        if (!dr.ship_recommended.empty() && dr.ship_recommended != dr.ship_used) {
            lines.push_back(hbox({text("Recommended: "), text(dr.ship_recommended) | dim}));
        }
        if (!dr.purpose.empty()) {
            lines.push_back(hbox({text("Purpose: "), text(dr.purpose) | color(Color::Blue)}));
        }
        std::string mining_desc = mining_descriptor(dr);
        if (!mining_desc.empty()) {
            lines.push_back(hbox({text("Mining:  "), text(mining_desc) | color(Color::Green)}));
        }
        if (dr.priority > 0) {
            lines.push_back(hbox({text("Priority: "), text(std::to_string(dr.priority)) | dim}));
        }
        lines.push_back(separator());

        lines.push_back(hbox({
            text("Captain: "),
            text(dr.captain) | bold | color(Color::Yellow),
        }));
        if (dr.bridge.size() >= 2) {
            lines.push_back(hbox({
                text("Bridge:  "),
                text(dr.bridge[0]) | color(Color::Cyan),
                text(" + "),
                text(dr.bridge[1]) | color(Color::Cyan),
            }));
        }
        lines.push_back(hbox({text("Score:   "), text(std::to_string((int)dr.score)) | bold}));

        if (dr.locked) {
            lines.push_back(text("[LOCKED - pre-assigned]") | color(Color::Yellow) | dim);
        }

        // Breakdown
        lines.push_back(separator());
        lines.push_back(text("Individual:") | bold);
        for (const auto& [name, score] : bd.individual_scores) {
            bool is_cpt = (name == bd.captain);
            lines.push_back(hbox({
                text("  "),
                text(is_cpt ? "[CPT] " : "      "),
                text(name) | (is_cpt ? bold : nothing),
                filler(),
                text(std::to_string((int)score)),
            }));
        }

        lines.push_back(separator());
        lines.push_back(text("Bonuses:") | bold);
        if (bd.raw_total > 0)
            lines.push_back(hbox({text("  Raw Total:   "), text(std::to_string((int)bd.raw_total)) | color(Color::Cyan)}));
        if (bd.synergy_bonus > 0)
            lines.push_back(hbox({text("  Synergy:     +"), text(std::to_string((int)bd.synergy_bonus)) | color(Color::Green)}));
        if (bd.bridge_synergy_bonus > 0) {
            std::string bars_l = (bd.bridge_synergy_bars_left == 2) ? "##" : (bd.bridge_synergy_bars_left == 1 ? "#" : "-");
            std::string bars_r = (bd.bridge_synergy_bars_right == 2) ? "##" : (bd.bridge_synergy_bars_right == 1 ? "#" : "-");
            lines.push_back(hbox({text("  Group Syn:   +"), text(std::to_string((int)bd.bridge_synergy_bonus)) | color(Color::Green),
                                  text(" [" + bars_l + "|" + bars_r + "] +" + std::to_string((int)bd.bridge_synergy_pct) + "%") | color(Color::Yellow)}));
        }
        if (bd.state_chain_bonus > 0)
            lines.push_back(hbox({text("  State Chain: +"), text(std::to_string((int)bd.state_chain_bonus)) | color(Color::Green)}));
        if (bd.crit_bonus > 0)
            lines.push_back(hbox({text("  Crit:        +"), text(std::to_string((int)bd.crit_bonus)) | color(Color::Green)}));
        if (bd.ship_type_bonus > 0)
            lines.push_back(hbox({text("  Ship Spec:   +"), text(std::to_string((int)bd.ship_type_bonus)) | color(Color::Green)}));
        if (bd.scenario_bonus > 0)
            lines.push_back(hbox({text("  Scenario:    +"), text(std::to_string((int)bd.scenario_bonus)) | color(Color::Green)}));
        if (bd.weakness_counter_bonus > 0)
            lines.push_back(hbox({text("  Weakness:    +"), text(std::to_string((int)bd.weakness_counter_bonus)) | color(Color::Green)}));
        if (bd.dual_use_bonus > 0)
            lines.push_back(hbox({text("  Dual-Use:    +"), text(std::to_string((int)bd.dual_use_bonus)) | color(Color::Green)}));
        if (bd.amplifier_bonus > 0)
            lines.push_back(hbox({text("  Amplifier:   +"), text(std::to_string((int)bd.amplifier_bonus)) | color(Color::Green)}));
        if (bd.penalty_total > 0)
            lines.push_back(hbox({text("  Penalties:   -"), text(std::to_string((int)bd.penalty_total)) | color(Color::Red)}));

        if (!bd.synergy_notes.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Why This Won:") | bold | color(Color::Green));
            for (const auto& note : bd.synergy_notes) {
                lines.push_back(text("  " + note) | color(Color::Green) | dim);
            }
        }

        if (!bd.penalties.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Penalties:") | bold | color(Color::Red));
            for (const auto& p : bd.penalties) {
                lines.push_back(text("  " + p) | color(Color::Red) | dim);
            }
        }

        // BDA detail for selected suggestion
        if (!dr.bda_suggestions.empty()) {
            lines.push_back(separator());
            lines.push_back(text("BDA Details:") | bold | color(Color::Magenta));
            int bi = state.selected_dock_bda;
            if (bi >= 0 && bi < (int)dr.bda_suggestions.size()) {
                const auto& bda = dr.bda_suggestions[bi];

                // Name, level, rank, type
                auto name_row_parts = Elements{
                    text("  "),
                    text(bda.name) | bold | color(Color::Cyan),
                    text("  Lv" + std::to_string(bda.level) + " Rk" + std::to_string(bda.rank)),
                };
                if (!bda.officer_type.empty()) {
                    name_row_parts.push_back(text("  [" + bda.officer_type + "]") | dim);
                }
                if (bda.is_dedicated_bda) {
                    name_row_parts.push_back(text("  [BDA]") | color(Color::Yellow));
                }
                lines.push_back(hbox(name_row_parts));

                // Stats
                lines.push_back(hbox({
                    text("  Atk:"),
                    text(std::to_string((int)bda.attack)) | dim,
                    text("  Def:"),
                    text(std::to_string((int)bda.defense)) | dim,
                    text("  HP:"),
                    text(std::to_string((int)bda.health)) | dim,
                }));

                // Tag labels
                Elements tag_parts;
                if (bda.cargo)              tag_parts.push_back(text(" [Cargo]") | color(Color::Green));
                if (bda.protected_cargo)    tag_parts.push_back(text(" [Protected]") | color(Color::GreenLight));
                if (bda.loot)               tag_parts.push_back(text(" [Loot]") | color(Color::Yellow));
                if (bda.mining)             tag_parts.push_back(text(" [Mining]") | color(Color::Blue));
                if (bda.crit_related)       tag_parts.push_back(text(" [Crit]") | color(Color::Red));
                if (bda.shield_related)     tag_parts.push_back(text(" [Shield]") | color(Color::Cyan));
                if (bda.mitigation_related) tag_parts.push_back(text(" [Mitigation]") | color(Color::CyanLight));
                if (bda.repair)             tag_parts.push_back(text(" [Repair]") | color(Color::MagentaLight));
                if (bda.armada)             tag_parts.push_back(text(" [Armada]") | color(Color::YellowLight));
                if (!tag_parts.empty()) {
                    tag_parts.insert(tag_parts.begin(), text(" "));
                    lines.push_back(hbox(tag_parts));
                }

                // Percentage values when non-zero
                Elements pct_parts;
                if (bda.oa_cargo_pct > 0.0)
                    pct_parts.push_back(text("  Cargo+" + std::to_string((int)(bda.oa_cargo_pct * 100)) + "%") | color(Color::Green));
                if (bda.oa_protected_cargo_pct > 0.0)
                    pct_parts.push_back(text("  ProtCargo+" + std::to_string((int)(bda.oa_protected_cargo_pct * 100)) + "%") | color(Color::GreenLight));
                if (bda.oa_mining_speed_pct > 0.0)
                    pct_parts.push_back(text("  Mining+" + std::to_string((int)(bda.oa_mining_speed_pct * 100)) + "%") | color(Color::Blue));
                if (!pct_parts.empty()) {
                    lines.push_back(hbox(pct_parts));
                }

                // BDA ability text
                if (!bda.bda_text.empty()) {
                    lines.push_back(hbox({
                        text("  BDA: ") | dim,
                        text(bda.bda_text) | color(Color::Magenta),
                    }));
                    if (bda.bda_value > 0.0) {
                        lines.push_back(text("       Value: " + std::to_string((int)(bda.bda_value * 100)) + "%") | dim);
                    }
                }

                // OA ability text
                if (!bda.oa_text.empty()) {
                    lines.push_back(hbox({
                        text("  OA: ") | dim,
                        text(bda.oa_text) | color(Color::CyanLight),
                    }));
                    if (bda.oa_value > 0.0) {
                        lines.push_back(text("       Value: " + std::to_string((int)(bda.oa_value * 100)) + "%") | dim);
                    }
                }

                // Scoring reasons
                for (const auto& reason : bda.reasons) {
                    lines.push_back(text("  " + reason) | dim | color(Color::Magenta));
                }
            }
        }

        // Excluded officers
        if (!state.loadout_result.excluded_officers.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Excluded:") | bold | dim);
            std::string excl;
            for (size_t ei = 0; ei < state.loadout_result.excluded_officers.size() && ei < 10; ++ei) {
                if (!excl.empty()) excl += ", ";
                excl += state.loadout_result.excluded_officers[ei];
            }
            if (state.loadout_result.excluded_officers.size() > 10) {
                excl += " +" + std::to_string(state.loadout_result.excluded_officers.size() - 10) + " more";
            }
            // Wrap to ~50 chars
            while (!excl.empty()) {
                if (excl.size() <= 50) { lines.push_back(text("  " + excl) | dim); break; }
                size_t cut = excl.rfind(',', 50);
                if (cut == std::string::npos || cut == 0) cut = 50;
                lines.push_back(text("  " + excl.substr(0, cut + 1)) | dim);
                excl = excl.substr(cut + 1);
                if (!excl.empty() && excl[0] == ' ') excl = excl.substr(1);
            }
        }

        if (!state.loadout_result.analysis_notes.empty()) {
            lines.push_back(separator());
            lines.push_back(text("Account Analysis:") | bold | color(Color::Green));
            for (const auto& note : state.loadout_result.analysis_notes) {
                lines.push_back(text("  " + note) | dim | color(Color::Green));
            }
        }

        detail = vbox(lines);
    } else if (!state.loadout_computed) {
        detail = vbox({
            text("Press [G] to optimize all 7 docks.") | center | dim,
            separatorEmpty(),
            text("Use [</>] to change dock scenario.") | center | dim,
            text("Each dock gets best available crew") | center | dim,
            text("from the remaining officer pool.") | center | dim,
        });
    }

    // Dock config editor line
    std::string dock_scenario_str;
    if (state.selected_dock >= 0 && state.selected_dock < (int)state.dock_configs.size()) {
        dock_scenario_str = scenario_label(state.dock_configs[state.selected_dock].scenario);
        std::string mining_desc = mining_descriptor(state.dock_configs[state.selected_dock]);
        if (!mining_desc.empty()) dock_scenario_str += " {" + mining_desc + "}";
    }

    Element body = vbox({
        header,
        separator(),
        hbox({
            vbox(dock_rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 55) | vscroll_indicator | yframe,
        }) | flex,
        separator(),
        hbox({
            text(" Dock " + std::to_string(state.selected_dock + 1) + ": ") | bold,
            text(dock_scenario_str) | color(Color::Cyan),
            filler(),
            text(" [Up/Dn] Dock  ") | dim,
            text("[</>] Scenario  ") | dim,
            text("[R/F] Resource  ") | dim,
            text("[O] Objective  ") | dim,
            text("[B] BDA nav  ") | dim,
            text("[K] Lock/Unlock") | dim,
        }),
    });

    if (!state.show_dock_modal || state.selected_dock < 0 || state.selected_dock >= (int)state.dock_configs.size()) {
        return body;
    }

    const auto& cfg = state.dock_configs[state.selected_dock];
    std::string mining_desc = mining_descriptor(cfg);
    Elements modal_lines;
    modal_lines.push_back(text("Dock Configuration") | bold | center);
    modal_lines.push_back(separator());
    modal_lines.push_back(text("Dock " + std::to_string(state.selected_dock + 1)) | bold);
    modal_lines.push_back(text("Scenario: " + std::string(scenario_label(cfg.scenario))) |
                          (state.dock_modal_field == 0 ? inverted : nothing));
    modal_lines.push_back(text("Mining Resource: " + std::string(mining_resource_str(cfg.mining_resource == MiningResource::None
                          ? scenario_mining_resource(cfg.scenario) : cfg.mining_resource))) |
                          (state.dock_modal_field == 1 ? inverted : nothing));
    modal_lines.push_back(text("Mining Objective: " + std::string(mining_objective_str(cfg.mining_objective == MiningObjective::None
                          ? scenario_mining_objective(cfg.scenario) : cfg.mining_objective))) |
                          (state.dock_modal_field == 2 ? inverted : nothing));
    modal_lines.push_back(text("Ship Override: " + (cfg.ship_override.empty() ? std::string("auto") : cfg.ship_override)) |
                          (state.dock_modal_field == 3 ? inverted : nothing));
    modal_lines.push_back(text("Locked: " + std::string(cfg.locked ? "yes" : "no")) |
                          (state.dock_modal_field == 4 ? inverted : nothing));
    if (!mining_desc.empty()) {
        modal_lines.push_back(separatorLight());
        modal_lines.push_back(text("Current mining intent: " + mining_desc) | color(Color::Green));
    }
    modal_lines.push_back(separator());
    modal_lines.push_back(text("[Up/Dn] field  [</>] change  [Enter/Esc] close") | dim | center);

    auto modal = window(text(" Edit Dock "), vbox(modal_lines)) | size(WIDTH, EQUAL, 52);
    return dbox({body, vbox({filler(), hbox({filler(), modal, filler()}), filler()})});
}

// ---------------------------------------------------------------------------
// View: Officers — with player data overlay (owned/unowned, levels, ranks)
// ---------------------------------------------------------------------------

static Element render_officers(AppState& state) {
    auto& gd = state.game_data;
    auto& pd = state.player_data;

    // Build player officer lookup by officer_id
    std::map<int64_t, const PlayerOfficer*> player_map;
    for (auto& po : pd.officers) {
        player_map[po.officer_id] = &po;
    }
    bool has_player = !player_map.empty();

    std::vector<const Officer*> officers;
    for (auto& [id, o] : gd.officers) {
        if (!state.officer_filter.empty()) {
            std::string name_lower = o.name;
            std::string filter_lower = state.officer_filter;
            for (auto& c : name_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : filter_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name_lower.find(filter_lower) == std::string::npos &&
                o.short_name.find(state.officer_filter) == std::string::npos) {
                continue;
            }
        }
        officers.push_back(&o);
    }

    // Sort: owned first (by level desc), then unowned alphabetically
    std::sort(officers.begin(), officers.end(), [&](const Officer* a, const Officer* b) {
        auto pa = player_map.find(a->id);
        auto pb = player_map.find(b->id);
        bool a_owned = (pa != player_map.end());
        bool b_owned = (pb != player_map.end());
        if (a_owned != b_owned) return a_owned > b_owned; // owned first
        if (a_owned && b_owned) {
            if (pa->second->level != pb->second->level) return pa->second->level > pb->second->level;
            if (pa->second->rank != pb->second->rank) return pa->second->rank > pb->second->rank;
        }
        return a->name < b->name;
    });

    if (officers.empty()) {
        return vbox({
            text("Officers") | bold | center,
            separator(),
            text("No officers loaded. Press [R] to refresh data.") | center,
        });
    }

    // Clamp selection
    state.selected_officer = std::clamp(state.selected_officer, 0, (int)officers.size() - 1);

    // Count owned
    int owned_count = 0;
    for (auto* o : officers) {
        if (player_map.count(o->id)) owned_count++;
    }

    // Build rows
    Elements rows;

    // Header row
    if (has_player) {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 24),
            text("Lv") | bold | size(WIDTH, EQUAL, 5),
            text("Rk") | bold | size(WIDTH, EQUAL, 5),
            text("Shards") | bold | size(WIDTH, EQUAL, 8),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("Class") | bold | size(WIDTH, EQUAL, 12),
            text("Group") | bold | size(WIDTH, EQUAL, 20),
            text("MaxRk") | bold | size(WIDTH, EQUAL, 7),
        }));
    } else {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 28),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("Class") | bold | size(WIDTH, EQUAL, 12),
            text("Group") | bold | size(WIDTH, EQUAL, 22),
            text("MaxRk") | bold | size(WIDTH, EQUAL, 7),
            text("Atk") | bold | size(WIDTH, EQUAL, 10),
            text("Def") | bold | size(WIDTH, EQUAL, 10),
            text("HP") | bold | size(WIDTH, EQUAL, 10),
        }));
    }
    rows.push_back(separator());

    for (int i = 0; i < (int)officers.size(); ++i) {
        auto* o = officers[i];
        bool sel = (i == state.selected_officer);
        auto pit = player_map.find(o->id);
        bool owned = (pit != player_map.end());

        Color rcolor = Color::White;
        switch (o->rarity) {
            case 1: rcolor = Color::GrayLight; break;
            case 2: rcolor = Color::Green; break;
            case 3: rcolor = Color::Blue; break;
            case 4: rcolor = Color::Magenta; break;
        }

        std::string name = o->name.empty() ? o->short_name : o->name;
        Element row;

        if (has_player) {
            std::string lv_str = owned ? std::to_string(pit->second->level) : "-";
            std::string rk_str = owned ? std::to_string(pit->second->rank) : "-";
            std::string shard_str = owned ? std::to_string(pit->second->shard_count) : "-";

            row = hbox({
                (owned ? text(name) : text(name) | dim) | size(WIDTH, EQUAL, 24),
                text(lv_str) | (owned ? bold : dim) | size(WIDTH, EQUAL, 5),
                text(rk_str) | (owned ? bold : dim) | size(WIDTH, EQUAL, 5),
                text(shard_str) | dim | size(WIDTH, EQUAL, 8),
                text(rarity_str(o->rarity)) | color(rcolor) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 10),
                text(officer_class_str(o->officer_class)) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 12),
                text(o->group_name) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 20),
                text(std::to_string(o->max_rank)) | dim | size(WIDTH, EQUAL, 7),
            });
        } else {
            std::string max_atk = "-", max_def = "-", max_hp = "-";
            if (!o->stats.empty()) {
                auto& last = o->stats.back();
                max_atk = format_number((int64_t)last.attack);
                max_def = format_number((int64_t)last.defense);
                max_hp = format_number((int64_t)last.health);
            }
            row = hbox({
                text(name) | size(WIDTH, EQUAL, 28),
                text(rarity_str(o->rarity)) | color(rcolor) | size(WIDTH, EQUAL, 10),
                text(officer_class_str(o->officer_class)) | size(WIDTH, EQUAL, 12),
                text(o->group_name) | size(WIDTH, EQUAL, 22),
                text(std::to_string(o->max_rank)) | size(WIDTH, EQUAL, 7),
                text(max_atk) | size(WIDTH, EQUAL, 10),
                text(max_def) | size(WIDTH, EQUAL, 10),
                text(max_hp) | size(WIDTH, EQUAL, 10),
            });
        }

        if (sel) row = row | inverted | focus;
        rows.push_back(row);
    }

    // Detail panel for selected officer
    Element detail = text("");
    if (state.selected_officer >= 0 && state.selected_officer < (int)officers.size()) {
        auto* o = officers[state.selected_officer];
        auto pit = player_map.find(o->id);
        bool owned = (pit != player_map.end());

        Elements lines;
        lines.push_back(text(o->name.empty() ? o->short_name : o->name) | bold);
        if (owned) {
            lines.push_back(text("OWNED") | bold | color(Color::Green));
        } else {
            lines.push_back(text("NOT OWNED") | dim);
        }
        lines.push_back(separator());
        lines.push_back(hbox({text("Rarity: "), text(rarity_str(o->rarity))}));
        lines.push_back(hbox({text("Class:  "), text(officer_class_str(o->officer_class))}));
        lines.push_back(hbox({text("Group:  "), text(o->group_name)}));
        lines.push_back(hbox({text("Ranks:  "), text(std::to_string(o->max_rank))}));

        if (owned) {
            lines.push_back(separator());
            lines.push_back(text("Player Stats:") | bold | color(Color::Cyan));
            lines.push_back(hbox({text("  Level:  "), text(std::to_string(pit->second->level)) | bold}));
            lines.push_back(hbox({text("  Rank:   "), text(std::to_string(pit->second->rank)) | bold}));
            lines.push_back(hbox({text("  Shards: "), text(std::to_string(pit->second->shard_count))}));
        }

        if (!o->stats.empty()) {
            auto& last = o->stats.back();
            lines.push_back(separator());
            lines.push_back(text("Max Stats (Game Data):") | bold);
            lines.push_back(hbox({text("  Atk: "), text(format_number((int64_t)last.attack)) | color(Color::Red)}));
            lines.push_back(hbox({text("  Def: "), text(format_number((int64_t)last.defense)) | color(Color::Cyan)}));
            lines.push_back(hbox({text("  HP:  "), text(format_number((int64_t)last.health)) | color(Color::Green)}));

            // If owned, also show current level stats
            if (owned && pit->second->level > 0 && pit->second->level <= (int)o->stats.size()) {
                auto& cur = o->stats[pit->second->level - 1];
                lines.push_back(separator());
                lines.push_back(text("Current Stats (Lv" + std::to_string(pit->second->level) + "):") | bold | color(Color::Cyan));
                lines.push_back(hbox({text("  Atk: "), text(format_number((int64_t)cur.attack)) | color(Color::Red)}));
                lines.push_back(hbox({text("  Def: "), text(format_number((int64_t)cur.defense)) | color(Color::Cyan)}));
                lines.push_back(hbox({text("  HP:  "), text(format_number((int64_t)cur.health)) | color(Color::Green)}));
            }
        }

        // ---- Resolved Abilities ----
        {
            int rank = owned ? pit->second->rank : 0;

            // Resolve tooltip: substitute {N:#,#%} placeholders with actual values
            std::string tooltip = resolve_officer_tooltip(*o, rank);
            tooltip = strip_color_tags(tooltip);

            // Split into blocks on double-newline
            std::string block0, block1;
            auto sep_pos = tooltip.find("\n\n");
            if (sep_pos != std::string::npos) {
                block0 = tooltip.substr(0, sep_pos);
                block1 = tooltip.substr(sep_pos + 2);
                auto sep2 = block1.find("\n\n");
                if (sep2 != std::string::npos) block1 = block1.substr(0, sep2);
            } else {
                block0 = tooltip;
            }

            // Extract ability name (first line of each block)
            auto first_line = [](const std::string& blk) -> std::string {
                auto nl = blk.find('\n');
                return nl != std::string::npos ? blk.substr(0, nl) : blk;
            };
            auto rest_lines = [](const std::string& blk) -> std::string {
                auto nl = blk.find('\n');
                if (nl == std::string::npos || nl + 1 >= blk.size()) return "";
                return blk.substr(nl + 1);
            };

            // Word-wrap helper for the detail panel width
            auto wrap_text = [](const std::string& txt, int width) -> Elements {
                Elements result;
                std::string remaining = txt;
                while (!remaining.empty()) {
                    if ((int)remaining.size() <= width) {
                        result.push_back(text(remaining) | dim);
                        break;
                    }
                    size_t cut = remaining.rfind(' ', width);
                    if (cut == std::string::npos || cut == 0) cut = width;
                    result.push_back(text(remaining.substr(0, cut)) | dim);
                    remaining = remaining.substr(cut + (remaining[cut] == ' ' ? 1 : 0));
                }
                return result;
            };

            // Captain Maneuver / Below Decks Ability
            lines.push_back(separator());
            if (o->has_bda) {
                // BDA officer: block0 = BDA text, no CM
                std::string bda_name = first_line(block0);
                std::string bda_desc = rest_lines(block0);
                double bda_raw = ability_pct(o->below_decks_ability, 0);
                std::string bda_val = o->below_decks_ability.value_is_percentage
                    ? fmt_pct(bda_raw) : std::to_string((int)bda_raw);

                lines.push_back(text("Below Decks Ability:") | bold | color(Color::Magenta));
                lines.push_back(hbox({text("  "), text(bda_name) | bold}));
                lines.push_back(hbox({text("  Value: "), text(bda_val) | bold | color(Color::Yellow)}));
                if (!bda_desc.empty()) {
                    auto wrapped = wrap_text("  " + bda_desc, 38);
                    for (auto& w : wrapped) lines.push_back(w);
                }

                // Also note "Unfit To Lead" for CM
                lines.push_back(text(""));
                lines.push_back(text("Captain Maneuver:") | bold | color(Color::Blue));
                lines.push_back(hbox({text("  "), text("Unfit To Lead") | dim}));
            } else {
                // Regular officer: block0 = CM text
                std::string cm_name = first_line(block0);
                std::string cm_desc = rest_lines(block0);
                double cm_raw = ability_pct(o->captain_ability, 0);
                std::string cm_val = o->captain_ability.value_is_percentage
                    ? fmt_pct(cm_raw) : std::to_string((int)cm_raw);

                lines.push_back(text("Captain Maneuver:") | bold | color(Color::Blue));
                lines.push_back(hbox({text("  "), text(cm_name) | bold}));
                lines.push_back(hbox({text("  Value: "), text(cm_val) | bold | color(Color::Yellow)}));
                if (!cm_desc.empty()) {
                    auto wrapped = wrap_text("  " + cm_desc, 38);
                    for (auto& w : wrapped) lines.push_back(w);
                }
            }

            // Officer Ability (block1)
            if (!block1.empty()) {
                std::string oa_name = first_line(block1);
                std::string oa_desc = rest_lines(block1);
                double oa_raw = ability_pct(o->ability, rank);
                std::string oa_val = o->ability.value_is_percentage
                    ? fmt_pct(oa_raw) : std::to_string((int)oa_raw);

                lines.push_back(text(""));
                lines.push_back(text("Officer Ability:") | bold | color(Color::Green));
                lines.push_back(hbox({text("  "), text(oa_name) | bold}));
                lines.push_back(hbox({text("  Value: "), text(oa_val) | bold | color(Color::Yellow)}));
                if (owned) {
                    lines.push_back(hbox({text("  (at rank "), text(std::to_string(rank)) | bold,
                                          text(")")}));
                }
                if (!oa_desc.empty()) {
                    auto wrapped = wrap_text("  " + oa_desc, 38);
                    for (auto& w : wrapped) lines.push_back(w);
                }
            }
        }
        detail = vbox(lines) | border;
    }

    std::string title = has_player
        ? "Officers (" + std::to_string(owned_count) + " owned / " + std::to_string(officers.size()) + " total)"
        : "Officers (" + std::to_string(officers.size()) + " total)";

    Elements header_elems;
    header_elems.push_back(text(title) | bold);
    if (state.officer_filter_active) {
        header_elems.push_back(text("  Filter: ") | dim);
        header_elems.push_back(text(state.officer_filter + "_") | bold | color(Color::Yellow));
    } else if (!state.officer_filter.empty()) {
        header_elems.push_back(text("  Filter: ") | dim);
        header_elems.push_back(text(state.officer_filter) | color(Color::Yellow));
    }
    header_elems.push_back(filler());
    header_elems.push_back(text("[Up/Down] Navigate  [F] Filter") | dim);

    return vbox({
        hbox(header_elems),
        separator(),
        hbox({
            vbox(rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 42) | vscroll_indicator | yframe,
        }) | flex,
    });
}

// ---------------------------------------------------------------------------
// View: Ships — with player data overlay (owned tier/level)
// ---------------------------------------------------------------------------

static Element render_ships(AppState& state) {
    auto& gd = state.game_data;
    auto& pd = state.player_data;

    // Build player ship lookup by hull_id (may have multiple instances of same hull)
    // Use a multimap since player can have multiple ships of same hull
    std::map<int64_t, std::vector<const PlayerShip*>> player_hull_map;
    for (auto& ps : pd.ships) {
        player_hull_map[ps.hull_id].push_back(&ps);
    }
    bool has_player = !pd.ships.empty();

    std::vector<const Ship*> ships;
    for (auto& [id, s] : gd.ships) {
        if (!state.ship_filter.empty()) {
            std::string name_lower = s.name;
            std::string filter_lower = state.ship_filter;
            for (auto& c : name_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : filter_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name_lower.find(filter_lower) == std::string::npos) continue;
        }
        ships.push_back(&s);
    }

    // Sort: owned first (by tier desc), then by grade/name
    std::sort(ships.begin(), ships.end(), [&](const Ship* a, const Ship* b) {
        bool a_owned = player_hull_map.count(a->id) > 0;
        bool b_owned = player_hull_map.count(b->id) > 0;
        if (a_owned != b_owned) return a_owned > b_owned;
        if (a_owned && b_owned) {
            auto& a_ps = player_hull_map[a->id];
            auto& b_ps = player_hull_map[b->id];
            int a_tier = a_ps.empty() ? 0 : a_ps[0]->tier;
            int b_tier = b_ps.empty() ? 0 : b_ps[0]->tier;
            if (a_tier != b_tier) return a_tier > b_tier;
        }
        if (a->grade != b->grade) return a->grade < b->grade;
        return a->name < b->name;
    });

    if (ships.empty()) {
        return vbox({
            text("Ships") | bold | center,
            separator(),
            text("No ships loaded. Press [R] to refresh data.") | center,
        });
    }

    // Clamp selection
    state.selected_ship = std::clamp(state.selected_ship, 0, (int)ships.size() - 1);

    // Count owned
    int owned_count = 0;
    for (auto* s : ships) {
        if (player_hull_map.count(s->id)) owned_count++;
    }

    // Build rows
    Elements rows;

    // Header row
    if (has_player) {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 24),
            text("Tier") | bold | size(WIDTH, EQUAL, 6),
            text("Lv") | bold | size(WIDTH, EQUAL, 5),
            text("Type") | bold | size(WIDTH, EQUAL, 14),
            text("Grade") | bold | size(WIDTH, EQUAL, 7),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("MaxT") | bold | size(WIDTH, EQUAL, 6),
            text("MaxLv") | bold | size(WIDTH, EQUAL, 7),
        }));
    } else {
        rows.push_back(hbox({
            text("Name") | bold | size(WIDTH, EQUAL, 28),
            text("Type") | bold | size(WIDTH, EQUAL, 14),
            text("Grade") | bold | size(WIDTH, EQUAL, 8),
            text("Rarity") | bold | size(WIDTH, EQUAL, 10),
            text("MaxTier") | bold | size(WIDTH, EQUAL, 9),
            text("MaxLv") | bold | size(WIDTH, EQUAL, 8),
            text("Build") | bold | size(WIDTH, EQUAL, 12),
        }));
    }
    rows.push_back(separator());

    for (int i = 0; i < (int)ships.size(); ++i) {
        auto* s = ships[i];
        bool sel = (i == state.selected_ship);
        auto pit = player_hull_map.find(s->id);
        bool owned = (pit != player_hull_map.end() && !pit->second.empty());

        Color tcolor = Color::White;
        switch (s->hull_type) {
            case 0: tcolor = Color::Cyan; break;
            case 1: tcolor = Color::Yellow; break;
            case 2: tcolor = Color::Green; break;
            case 3: tcolor = Color::Red; break;
        }

        Element row;
        if (has_player) {
            std::string tier_str = owned ? "T" + std::to_string(pit->second[0]->tier) : "-";
            std::string lv_str = owned ? std::to_string(pit->second[0]->level) : "-";

            row = hbox({
                (owned ? text(s->name) : text(s->name) | dim) | size(WIDTH, EQUAL, 24),
                text(tier_str) | (owned ? bold : dim) | color(owned ? Color::Cyan : Color::GrayDark) | size(WIDTH, EQUAL, 6),
                text(lv_str) | (owned ? bold : dim) | size(WIDTH, EQUAL, 5),
                text(hull_type_str(s->hull_type)) | color(tcolor) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 14),
                text("G" + std::to_string(s->grade)) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 7),
                text(rarity_str(s->rarity)) | (owned ? nothing : dim) | size(WIDTH, EQUAL, 10),
                text("T" + std::to_string(s->max_tier)) | dim | size(WIDTH, EQUAL, 6),
                text(std::to_string(s->max_level)) | dim | size(WIDTH, EQUAL, 7),
            });
        } else {
            int hours = s->build_time_seconds / 3600;
            int mins = (s->build_time_seconds % 3600) / 60;
            std::string build_time = std::to_string(hours) + "h " + std::to_string(mins) + "m";
            if (s->build_time_seconds == 0) build_time = "-";

            row = hbox({
                text(s->name) | size(WIDTH, EQUAL, 28),
                text(hull_type_str(s->hull_type)) | color(tcolor) | size(WIDTH, EQUAL, 14),
                text("G" + std::to_string(s->grade)) | size(WIDTH, EQUAL, 8),
                text(rarity_str(s->rarity)) | size(WIDTH, EQUAL, 10),
                text("T" + std::to_string(s->max_tier)) | size(WIDTH, EQUAL, 9),
                text(std::to_string(s->max_level)) | size(WIDTH, EQUAL, 8),
                text(build_time) | size(WIDTH, EQUAL, 12),
            });
        }

        if (sel) row = row | inverted | focus;
        rows.push_back(row);
    }

    // Detail panel for selected ship
    Element detail = text("");
    if (state.selected_ship >= 0 && state.selected_ship < (int)ships.size()) {
        auto* s = ships[state.selected_ship];
        auto pit = player_hull_map.find(s->id);
        bool owned = (pit != player_hull_map.end() && !pit->second.empty());

        Elements lines;
        lines.push_back(text(s->name) | bold);
        if (owned) {
            lines.push_back(text("OWNED") | bold | color(Color::Green));
        } else {
            lines.push_back(text("NOT BUILT") | dim);
        }
        lines.push_back(separator());
        lines.push_back(hbox({text("Type:   "), text(hull_type_str(s->hull_type))}));
        lines.push_back(hbox({text("Grade:  G"), text(std::to_string(s->grade))}));
        lines.push_back(hbox({text("Rarity: "), text(rarity_str(s->rarity))}));
        lines.push_back(hbox({text("MaxTier: T"), text(std::to_string(s->max_tier))}));
        lines.push_back(hbox({text("MaxLv:  "), text(std::to_string(s->max_level))}));

        if (owned) {
            lines.push_back(separator());
            lines.push_back(text("Player Ship:") | bold | color(Color::Cyan));
            for (auto* ps : pit->second) {
                lines.push_back(hbox({text("  Tier:  T"), text(std::to_string(ps->tier)) | bold}));
                lines.push_back(hbox({text("  Level: "), text(std::to_string(ps->level)) | bold}));
                int pct = (int)(ps->level_percentage * 100);
                lines.push_back(hbox({text("  Lvl%:  "), text(std::to_string(pct) + "%") | dim}));
                // Progress to max tier
                if (ps->tier < s->max_tier) {
                    lines.push_back(hbox({text("  Progress: T"), text(std::to_string(ps->tier)),
                        text("/T"), text(std::to_string(s->max_tier)) | color(Color::Yellow)}));
                    double tier_pct = (double)ps->tier / s->max_tier;
                    lines.push_back(hbox({text("  "), gauge(tier_pct) | size(WIDTH, EQUAL, 24) | color(Color::Cyan)}));
                } else {
                    lines.push_back(text("  MAX TIER") | bold | color(Color::Green));
                }
            }
        }

        if (s->build_time_seconds > 0) {
            lines.push_back(separator());
            int h = s->build_time_seconds / 3600;
            int m = (s->build_time_seconds % 3600) / 60;
            lines.push_back(hbox({text("Build:  "), text(std::to_string(h) + "h " + std::to_string(m) + "m")}));
        }
        if (!s->crew_slots.empty()) {
            lines.push_back(hbox({text("Crew:   "), text(std::to_string(s->crew_slots.size()) + " slots")}));
        }

        // ---- Ship Ability ----
        if (!s->ability_name.empty()) {
            lines.push_back(separator());

            // Clean up ability name (strip color tags)
            std::string aname = strip_color_tags(s->ability_name);

            // Classify the ability
            std::string tag = classify_ship_ability(s->ability_name, s->ability_description);

            // Color based on classification
            Color acolor = Color::White;
            if (tag.find("mining") != std::string::npos) acolor = Color::Yellow;
            else if (tag.find("combat") != std::string::npos) acolor = Color::Red;
            else if (tag.find("loot") != std::string::npos) acolor = Color::Green;
            else if (tag == "captain_boost") acolor = Color::Cyan;

            lines.push_back(text("Ship Ability:") | bold | color(acolor));
            lines.push_back(hbox({text("  "), text(aname) | bold}));

            // Show classification tag
            std::string tag_display = tag;
            for (auto& c : tag_display) if (c == '_') c = ' ';
            lines.push_back(hbox({text("  ["), text(tag_display) | color(acolor), text("]")}));

            // Show cleaned description text
            if (!s->ability_description.empty()) {
                std::string desc = strip_color_tags(s->ability_description);
                desc = strip_format_placeholders(desc);

                // Split on double-newline — first block is the ability, rest is flavor
                auto sep_pos = desc.find("\n\n");
                std::string ability_text = (sep_pos != std::string::npos)
                    ? desc.substr(0, sep_pos) : desc;

                // Extract just the effect text (skip ability name header line)
                auto nl = ability_text.find('\n');
                std::string effect_text = (nl != std::string::npos && nl + 1 < ability_text.size())
                    ? ability_text.substr(nl + 1) : ability_text;

                // Collapse whitespace and word-wrap
                effect_text = collapse_whitespace(effect_text);
                std::string remaining = effect_text;
                while (!remaining.empty()) {
                    if ((int)remaining.size() <= 36) {
                        lines.push_back(hbox({text("  "), text(remaining) | dim}));
                        break;
                    }
                    size_t cut = remaining.rfind(' ', 36);
                    if (cut == std::string::npos || cut == 0) cut = 36;
                    lines.push_back(hbox({text("  "), text(remaining.substr(0, cut)) | dim}));
                    remaining = remaining.substr(cut + (remaining[cut] == ' ' ? 1 : 0));
                }
            }
        }

        detail = vbox(lines) | border;
    }

    std::string title = has_player
        ? "Ships (" + std::to_string(owned_count) + " owned / " + std::to_string(ships.size()) + " total)"
        : "Ships (" + std::to_string(ships.size()) + " total)";

    Elements ship_header_elems;
    ship_header_elems.push_back(text(title) | bold);
    if (state.ship_filter_active) {
        ship_header_elems.push_back(text("  Filter: ") | dim);
        ship_header_elems.push_back(text(state.ship_filter + "_") | bold | color(Color::Yellow));
    } else if (!state.ship_filter.empty()) {
        ship_header_elems.push_back(text("  Filter: ") | dim);
        ship_header_elems.push_back(text(state.ship_filter) | color(Color::Yellow));
    }
    ship_header_elems.push_back(filler());
    ship_header_elems.push_back(text("[Up/Down] Navigate  [F] Filter") | dim);

    return vbox({
        hbox(ship_header_elems),
        separator(),
        hbox({
            vbox(rows) | vscroll_indicator | yframe | flex,
            separator(),
            detail | size(WIDTH, EQUAL, 40) | vscroll_indicator | yframe,
        }) | flex,
    });
}

// ---------------------------------------------------------------------------
// View: Sync — Detailed player data browser + ingress status
// ---------------------------------------------------------------------------

static const char* sync_view_labels[] = {
    "Officers", "Ships", "Resources", "Buildings", "Research", "Jobs", "Buffs"
};
static const int sync_view_count = 7;

static Element render_sync(AppState& state) {
    bool running = state.ingress_server.is_running();
    auto& pd = state.player_data;

    // --- Server status (compact) ---
    auto status_line = hbox({
        text(" Server: "),
        running ? text("RUNNING :" + std::to_string(state.ingress_server.port())) | color(Color::Green) | bold
                : text("STOPPED") | color(Color::Red) | bold,
        text("  "),
        text("[S] Toggle") | dim,
        filler(),
        text("Last sync: "),
        pd.last_sync != std::chrono::system_clock::time_point{}
            ? [&]() {
                auto tt = std::chrono::system_clock::to_time_t(pd.last_sync);
                std::tm tb{};
                localtime_r(&tt, &tb);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tb.tm_hour, tb.tm_min, tb.tm_sec);
                return text(std::string(buf)) | color(Color::Green);
            }()
            : text("Never") | dim,
    });

    // --- View mode tabs ---
    Elements view_tabs;
    for (int i = 0; i < sync_view_count; i++) {
        int count = 0;
        switch (i) {
            case 0: count = (int)pd.officers.size(); break;
            case 1: count = (int)pd.ships.size(); break;
            case 2: count = (int)pd.resources.size(); break;
            case 3: count = (int)pd.buildings.size(); break;
            case 4: count = (int)pd.researches.size(); break;
            case 5: count = (int)pd.jobs.size(); break;
            case 6: count = (int)pd.buffs.size(); break;
        }
        std::string label = std::string(sync_view_labels[i]) + "(" + std::to_string(count) + ")";
        auto tab = text(" " + label + " ");
        if (i == state.sync_view_mode) {
            tab = tab | bold | inverted;
        } else if (count > 0) {
            tab = tab | color(Color::Cyan);
        } else {
            tab = tab | dim;
        }
        view_tabs.push_back(tab);
    }

    // --- Data table for selected view ---
    Elements data_rows;

    auto add_header = [&](std::vector<std::pair<std::string, int>> cols) {
        Elements hdr_parts;
        for (auto& [name, width] : cols) {
            hdr_parts.push_back(text(name) | bold | size(WIDTH, EQUAL, width));
        }
        data_rows.push_back(hbox(std::move(hdr_parts)));
        data_rows.push_back(separator());
    };

    int max_rows = 0;

    switch (state.sync_view_mode) {
    case 0: { // Officers
        add_header({{"Name", 26}, {"Level", 7}, {"Rank", 6}, {"Shards", 8}, {"ID", 14}});
        auto sorted = pd.officers;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerOfficer& a, const PlayerOfficer& b) {
            return a.level > b.level || (a.level == b.level && a.rank > b.rank);
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& o = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = o.name.empty() ? ("OID:" + std::to_string(o.officer_id)) : o.name;
            if (name.size() > 24) name = name.substr(0, 22) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 26),
                text(std::to_string(o.level)) | bold | size(WIDTH, EQUAL, 7),
                text(std::to_string(o.rank)) | size(WIDTH, EQUAL, 6),
                text(std::to_string(o.shard_count)) | size(WIDTH, EQUAL, 8),
                text(std::to_string(o.officer_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 1: { // Ships
        add_header({{"Name", 26}, {"Tier", 6}, {"Level", 7}, {"Lvl%", 7}, {"Hull ID", 12}});
        auto sorted = pd.ships;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerShip& a, const PlayerShip& b) {
            return a.tier > b.tier || (a.tier == b.tier && a.level > b.level);
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& s = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = s.name.empty() ? ("Hull:" + std::to_string(s.hull_id)) : s.name;
            if (name.size() > 24) name = name.substr(0, 22) + "..";

            // Get hull type for color
            Color tcolor = Color::White;
            auto git = state.game_data.ships.find(s.hull_id);
            if (git != state.game_data.ships.end()) {
                switch (git->second.hull_type) {
                    case 0: tcolor = Color::Cyan; break;
                    case 1: tcolor = Color::Yellow; break;
                    case 2: tcolor = Color::Green; break;
                    case 3: tcolor = Color::Red; break;
                }
            }

            std::string pct_str = std::to_string((int)(s.level_percentage * 100)) + "%";
            auto row = hbox({
                text(name) | color(tcolor) | size(WIDTH, EQUAL, 26),
                text("T" + std::to_string(s.tier)) | bold | size(WIDTH, EQUAL, 6),
                text(std::to_string(s.level)) | size(WIDTH, EQUAL, 7),
                text(pct_str) | dim | size(WIDTH, EQUAL, 7),
                text(std::to_string(s.hull_id)) | dim | size(WIDTH, EQUAL, 12),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 2: { // Resources
        add_header({{"Name", 28}, {"Amount", 16}, {"ID", 14}});
        auto sorted = pd.resources;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerResource& a, const PlayerResource& b) {
            return a.amount > b.amount;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& r = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = r.name.empty() ? ("RID:" + std::to_string(r.resource_id)) : r.name;
            if (name.size() > 26) name = name.substr(0, 24) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 28),
                text(format_number(r.amount)) | bold | color(Color::Green) | size(WIDTH, EQUAL, 16),
                text(std::to_string(r.resource_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 3: { // Buildings
        add_header({{"Name", 30}, {"Level", 8}, {"ID", 14}});
        auto sorted = pd.buildings;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerBuilding& a, const PlayerBuilding& b) {
            return a.level > b.level;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& b = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = b.name.empty() ? ("BID:" + std::to_string(b.building_id)) : b.name;
            if (name.size() > 28) name = name.substr(0, 26) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 30),
                text(std::to_string(b.level)) | bold | size(WIDTH, EQUAL, 8),
                text(std::to_string(b.building_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 4: { // Research
        add_header({{"Name", 32}, {"Level", 8}, {"ID", 14}});
        auto sorted = pd.researches;
        std::sort(sorted.begin(), sorted.end(), [](const PlayerResearch& a, const PlayerResearch& b) {
            return a.level > b.level;
        });
        max_rows = (int)sorted.size();
        for (int i = 0; i < (int)sorted.size(); ++i) {
            auto& r = sorted[i];
            bool sel = (i == state.sync_selected_row);
            std::string name = r.name.empty() ? ("RID:" + std::to_string(r.research_id)) : r.name;
            if (name.size() > 30) name = name.substr(0, 28) + "..";
            auto row = hbox({
                text(name) | size(WIDTH, EQUAL, 32),
                text(std::to_string(r.level)) | bold | size(WIDTH, EQUAL, 8),
                text(std::to_string(r.research_id)) | dim | size(WIDTH, EQUAL, 14),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 5: { // Jobs
        add_header({{"Type", 18}, {"Status", 10}, {"Time Left", 14}, {"Level", 7}});
        max_rows = (int)pd.jobs.size();
        for (int i = 0; i < (int)pd.jobs.size(); ++i) {
            auto& j = pd.jobs[i];
            bool sel = (i == state.sync_selected_row);
            std::string type = job_type_str(j.job_type);
            std::string status_str;
            Color status_color;
            if (j.completed) {
                status_str = "Done";
                status_color = Color::Green;
            } else {
                int remaining = job_remaining_seconds(j);
                if (remaining <= 0) {
                    status_str = "Ready";
                    status_color = Color::Green;
                } else {
                    status_str = "Active";
                    status_color = Color::Yellow;
                }
            }
            int remaining = j.completed ? 0 : job_remaining_seconds(j);
            std::string time_str = j.completed ? "-" : format_duration_short(remaining);

            auto row = hbox({
                text(type) | size(WIDTH, EQUAL, 18),
                text(status_str) | bold | color(status_color) | size(WIDTH, EQUAL, 10),
                text(time_str) | size(WIDTH, EQUAL, 14),
                text(std::to_string(j.level)) | dim | size(WIDTH, EQUAL, 7),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    case 6: { // Buffs
        add_header({{"Buff ID", 14}, {"Level", 8}, {"Status", 12}, {"Expires", 20}});
        max_rows = (int)pd.buffs.size();
        for (int i = 0; i < (int)pd.buffs.size(); ++i) {
            auto& b = pd.buffs[i];
            bool sel = (i == state.sync_selected_row);

            std::string status_str = b.expired ? "Expired" : "Active";
            Color status_color = b.expired ? Color::Red : Color::Green;

            std::string expire_str = "-";
            if (b.expiry_time > 0 && !b.expired) {
                auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                int remaining = (int)(b.expiry_time - now_epoch);
                expire_str = remaining > 0 ? format_duration_short(remaining) : "Expired";
                if (remaining <= 0) {
                    status_str = "Expired";
                    status_color = Color::Red;
                }
            }

            auto row = hbox({
                text(std::to_string(b.buff_id)) | size(WIDTH, EQUAL, 14),
                text(std::to_string(b.level)) | size(WIDTH, EQUAL, 8),
                text(status_str) | bold | color(status_color) | size(WIDTH, EQUAL, 12),
                text(expire_str) | size(WIDTH, EQUAL, 20),
            });
            if (sel) row = row | inverted | focus;
            data_rows.push_back(row);
        }
        break;
    }
    }

    if (data_rows.size() <= 2) { // Only header + separator
        data_rows.push_back(text("  No data. Start STFC with the community mod to sync.") | dim);
    }

    // Clamp selection
    if (max_rows > 0) {
        state.sync_selected_row = std::clamp(state.sync_selected_row, 0, max_rows - 1);
    }

    // --- Sync event log (compact, right panel) ---
    auto sync_log = state.ingress_server.get_sync_log();
    Elements log_lines;
    log_lines.push_back(text("Event Log") | bold);
    log_lines.push_back(separator());
    if (sync_log.empty()) {
        log_lines.push_back(text("No events yet") | dim);
    } else {
        int start = std::max(0, (int)sync_log.size() - 15);
        for (int i = (int)sync_log.size() - 1; i >= start; i--) {
            auto& ev = sync_log[i];
            auto time_t = std::chrono::system_clock::to_time_t(ev.timestamp);
            std::tm tm_buf{};
            localtime_r(&time_t, &tm_buf);
            char time_str[16];
            std::snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
                          tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

            Elements row;
            row.push_back(text(std::string(time_str) + " ") | dim);
            if (ev.success) {
                row.push_back(text("OK ") | color(Color::Green));
                row.push_back(text(ev.data_type + "(" + std::to_string(ev.record_count) + ")") | dim);
            } else {
                row.push_back(text("FAIL ") | color(Color::Red));
                row.push_back(text(ev.data_type) | dim);
            }
            log_lines.push_back(hbox(std::move(row)));
        }
    }

    return vbox({
        text("Mod Sync - Player Data Browser") | bold | center,
        separator(),
        status_line,
        separator(),
        hbox(view_tabs) | center,
        separator(),
        hbox({
            vbox(data_rows) | vscroll_indicator | yframe | flex,
            separator(),
            vbox(log_lines) | size(WIDTH, EQUAL, 38) | vscroll_indicator | yframe,
        }) | flex,
        separator(),
        hbox({
            text(" [S] Server  ") | dim,
            text("[Left/Right] View  ") | dim,
            text("[Up/Down] Navigate  ") | dim,
        }),
    });
}

// ---------------------------------------------------------------------------
// Help overlay
// ---------------------------------------------------------------------------

static Element render_help() {
    return vbox({
        text("STFC Tool v0.5 - Keyboard Reference") | bold | center,
        separator(),
        hbox({
            vbox({
                text("Global:") | bold | color(Color::Cyan),
                text("  [H]              Toggle help"),
                text("  [R]              Refresh game data"),
                text("  [S]              Toggle ingress server"),
                text("  [Q]              Quit (saves state)"),
                text("  [Tab/Shift+Tab]  Switch tabs"),
                text("  [PgUp/PgDn]      Scroll 10 rows"),
                text("  [Home/End]       Jump to top/bottom"),
                text("  Mouse wheel      Scroll up/down"),
                separatorEmpty(),
                text("Daily Planner:") | bold | color(Color::Cyan),
                text("  [Up/Down]  Navigate tasks"),
                text("  [Space]    Toggle task complete"),
                text("  [S]        Skip task"),
                text("  [C]        Show/hide completed"),
                separatorEmpty(),
                text("Weekly Planner:") | bold | color(Color::Cyan),
                text("  [Left/Right] Switch day"),
                text("  [Up/Down]  Navigate goals"),
                text("  [+/-]      Adjust goal progress"),
                text("  [Space]    Complete next task"),
            }) | flex,
            separator(),
            vbox({
                text("Crew Optimizer:") | bold | color(Color::Cyan),
                text("  [</> or ,/.]  Cycle scenario"),
                text("  [T]           Cycle ship type"),
                text("  [Enter]       Run optimizer"),
                text("  [Up/Down]     Navigate results"),
                text("  [B]           Cycle BDA selection"),
                separatorEmpty(),
                text("Loadout:") | bold | color(Color::Cyan),
                text("  [Up/Down]  Navigate docks"),
                text("  [</>]      Change dock scenario"),
                text("  [T]        Cycle ship type"),
                text("  [Enter]    Edit dock configuration"),
                text("  [G]        Optimize all docks"),
                text("  [K]        Lock/unlock dock"),
                text("  [B]        Cycle BDA selection"),
                text("  [L]        Load saved loadout"),
                separatorEmpty(),
                text("Officers / Ships:") | bold | color(Color::Cyan),
                text("  [Up/Down]  Navigate list"),
                text("  [F]        Filter (type to search, Esc clears)"),
                separatorEmpty(),
                text("Data:") | bold | color(Color::Cyan),
                text("  Cached in data/game_data/"),
                text("  Roster from community mod sync (Sync tab)"),
                text("  Plans in data/player_data/"),
            }) | flex,
        }),
        separator(),
        text("Press [H] to close") | center | dim,
    });
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------
// View: AI Advisor
// ---------------------------------------------------------------------------

// Helper: wrap text into lines of at most `width` characters
static Elements wrap_text(const std::string& s, int width, Decorator dec = nothing) {
    Elements result;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        while ((int)line.size() > width) {
            size_t cut = line.rfind(' ', width);
            if (cut == std::string::npos || cut == 0) cut = width;
            result.push_back(text(line.substr(0, cut)) | dec);
            line = line.substr(cut + (line[cut] == ' ' ? 1 : 0));
        }
        result.push_back(text(line) | dec);
    }
    return result;
}

static Element render_ai_advisor(AppState& state) {
    // Lazy initialization: first time the AI tab is rendered, init the engine
    // in a background thread. This avoids SSL/network calls during startup.
    if (!state.ai_initialized && !state.ai_initializing) {
        state.ai_init_lazy();
    }

    // Show initializing state
    if (state.ai_initializing) {
        return vbox({
            text("AI Advisor") | bold | center,
            separator(),
            text("  Initializing AI engine...") | dim | center,
            filler(),
        }) | flex;
    }

    static const char* mode_names[] = {"Groups", "Crew Recs", "Progression", "META Analysis", "Ask"};
    int safe_mode = std::clamp(state.ai_mode, 0, 4);

    // --- AI Status bar ---
    auto ai_stat = state.ai_engine.status();
    Color status_color = ai_stat.available ? Color::Green : Color::Red;
    std::string status_text_str = ai_stat.available
        ? (ai_stat.provider + "/" + ai_stat.model +
           (ai_stat.is_fallback ? " (fallback)" : "") +
           (ai_stat.has_search ? " [search]" : "") +
           (ai_stat.has_streaming ? " [stream]" : ""))
        : ("Unavailable" + (ai_stat.error.empty() ? "" : ": " + ai_stat.error));

    auto header = vbox({
        text("AI Advisor") | bold | center,
        separator(),
        hbox({
            text("  Status: "),
            text(status_text_str) | bold | color(status_color),
            filler(),
            text("  Tunnel: "),
            text(ai_stat.tunnel_status.empty() ? "N/A" : ai_stat.tunnel_status) | dim,
        }),
        hbox({
            text("  Mode: "),
            text(mode_names[safe_mode]) | bold | color(Color::Cyan),
            text("  [</>] change") | dim,
            filler(),
            text("  META: "),
            text(state.ai_engine.meta_cache().age_str()) | dim | color(state.ai_engine.meta_cache().empty() ? Color::Yellow : Color::Green),
            text("  [M] Refresh") | dim,
        }),
        hbox({
            filler(),
            text(state.ai_running ? "  [Running...]" : "  [Enter] Run") | dim,
            text("  [I] Re-init") | dim,
        }),
    });

    // --- Loading state ---
    if (state.ai_running || state.ai_meta_refreshing) {
        Elements stream_lines;
        std::string stream_copy;
        {
            std::lock_guard<std::mutex> lk(state.status_mutex);
            stream_copy = state.ai_stream_text;
        }

        if (state.ai_meta_refreshing) {
            // META cache refresh in progress
            stream_lines.push_back(text("Refreshing META cache from Gemini...") | bold | center | color(Color::Cyan));
            if (!state.ai_meta_progress.empty()) {
                stream_lines.push_back(text(state.ai_meta_progress) | center);
            }
            stream_lines.push_back(text("(Press Escape to cancel)") | dim | center);
            if (!stream_copy.empty()) {
                stream_lines.push_back(text("") );
                std::string preview = stream_copy.substr(0, std::min(stream_copy.size(), size_t(500)));
                if (stream_copy.size() > 500) preview += "...";
                auto plines = wrap_text(preview, 90, dim);
                for (auto& l : plines) stream_lines.push_back(std::move(l));
            }
        } else if (stream_copy.empty()) {
            if (safe_mode == 0 && !state.ai_group_progress.empty()) {
                // Groups mode — show per-group progress
                stream_lines.push_back(text(state.ai_group_progress) | bold | center);
                stream_lines.push_back(text("(Press Escape to cancel)") | dim | center);
            } else {
                stream_lines.push_back(text("Waiting for AI response...") | dim | center);
                stream_lines.push_back(text("(First query may take 1-3 minutes for model cold-start)") | dim | center);
            }
        } else if (safe_mode == 4) {
            // "Ask" mode — show raw streaming text (human-readable)
            auto lines = wrap_text(stream_copy, 90);
            for (auto& l : lines) stream_lines.push_back(std::move(l));
        } else {
            // Structured modes (crew/progression/meta) — show progress, not raw JSON
            size_t token_est = 0;
            for (char c : stream_copy) if (c == ' ' || c == '\n' || c == '{' || c == '"') token_est++;
            stream_lines.push_back(text("AI is generating response...") | bold | center);
            stream_lines.push_back(text("") );
            stream_lines.push_back(hbox({
                filler(),
                text("Received ~" + std::to_string(token_est) + " tokens (" +
                     std::to_string(stream_copy.size()) + " chars)") | dim,
                filler(),
            }));
            stream_lines.push_back(text("") );
            // Show a small preview of the beginning
            std::string preview = stream_copy.substr(0, std::min(stream_copy.size(), size_t(200)));
            if (stream_copy.size() > 200) preview += "...";
            stream_lines.push_back(text("Preview:") | dim);
            auto plines = wrap_text(preview, 90, dim);
            for (auto& l : plines) stream_lines.push_back(std::move(l));
        }
        return vbox({
            header,
            separator(),
            vbox(stream_lines) | flex | vscroll_indicator | yframe,
        });
    }

    // --- Content by mode ---
    Element content;

    if (safe_mode == 0) {
        // Groups mode — per-group AI crew recommendations
        if (!state.ai_group_result.ok() && state.ai_group_result.group_results.empty()) {
            if (!state.ai_group_result.error.empty()) {
                content = vbox({
                    text("Error: " + state.ai_group_result.error) | color(Color::Red),
                    text("Press [Enter] to retry.") | dim,
                });
            } else {
                content = vbox({
                    text("Press [Enter] to run group-based AI crew analysis.") | center | dim,
                    text("") ,
                    text("Officers are split into focused groups (PvP, PvE, Base, Armada, etc.)") | center | dim,
                    text("and each group is queried separately for better results.") | center | dim,
                    text("") ,
                    text("Rate results with [+] Good / [-] Bad to improve future queries.") | center | dim,
                });
            }
        } else {
            // Left panel: group list
            Elements left_rows;
            for (size_t i = 0; i < state.ai_group_result.group_results.size(); ++i) {
                const auto& gr = state.ai_group_result.group_results[i];
                bool sel = ((int)i == state.ai_selected_group);

                // Rating indicator
                std::string rating_str;
                Color rating_color = Color::GrayDark;
                if (gr.rating == AiRating::Good) { rating_str = " [+]"; rating_color = Color::Green; }
                else if (gr.rating == AiRating::Bad) { rating_str = " [-]"; rating_color = Color::Red; }

                // Status indicator
                Color status_color = gr.ok() ? Color::Green : (gr.error.empty() ? Color::GrayLight : Color::Red);
                std::string crew_count_str = gr.ok() ? std::to_string(gr.crews.size()) + " crews" :
                    (!gr.error.empty() ? "error" : "pending");

                auto row = hbox({
                    text(sel ? "> " : "  "),
                    text(gr.group_name) | bold | color(Color::Cyan),
                    text(" (" + std::to_string(gr.officer_count) + " officers)") | dim,
                    filler(),
                    text(crew_count_str) | color(status_color),
                    text(rating_str) | bold | color(rating_color),
                });
                if (sel) row = row | inverted | focus;
                left_rows.push_back(row);
                if (i < state.ai_group_result.group_results.size() - 1)
                    left_rows.push_back(separatorLight());
            }

            // Right panel: selected group's crew details
            Element detail = text("");
            int sgi = state.ai_selected_group;
            if (sgi >= 0 && sgi < (int)state.ai_group_result.group_results.size()) {
                const auto& gr = state.ai_group_result.group_results[sgi];
                Elements lines;

                if (gr.ok()) {
                    lines.push_back(text(gr.group_name + " Crews") | bold);
                    lines.push_back(separator());

                    for (size_t ci = 0; ci < gr.crews.size(); ++ci) {
                        const auto& crew = gr.crews[ci];
                        bool crew_sel = ((int)ci == state.ai_selected_group_crew);

                        lines.push_back(hbox({
                            text(crew_sel ? "> " : "  "),
                            text("#" + std::to_string(ci + 1)) | bold |
                                color(ci == 0 ? Color(Color::Gold1) : Color(Color::GrayLight)),
                            text("  Captain: "),
                            text(crew.captain) | bold | color(Color::Yellow),
                            filler(),
                            text("Conf: " + std::to_string((int)(crew.confidence * 100)) + "%") | dim,
                        }));
                        lines.push_back(hbox({
                            text("      Bridge: "),
                            text(crew.bridge.size() > 0 ? crew.bridge[0] : "?") | color(Color::Cyan),
                            text(" + "),
                            text(crew.bridge.size() > 1 ? crew.bridge[1] : "?") | color(Color::Cyan),
                        }));

                        // Show reasoning for selected crew
                        if (crew_sel && !crew.reasoning.empty()) {
                            lines.push_back(separator());
                            auto wrapped = wrap_text(crew.reasoning, 48);
                            for (auto& w : wrapped) lines.push_back(std::move(w));
                        }

                        if (!crew.below_decks.empty() && crew_sel) {
                            lines.push_back(text("    Below Decks:") | bold | color(Color::Magenta));
                            for (const auto& bd : crew.below_decks) {
                                lines.push_back(text("      " + bd) | color(Color::Cyan));
                            }
                        }

                        if (ci < gr.crews.size() - 1)
                            lines.push_back(separatorLight());
                    }
                } else if (!gr.error.empty()) {
                    lines.push_back(text(gr.group_name) | bold);
                    lines.push_back(separator());
                    lines.push_back(text("Error: " + gr.error) | color(Color::Red));
                } else {
                    lines.push_back(text(gr.group_name) | bold);
                    lines.push_back(separator());
                    lines.push_back(text("No results yet.") | dim);
                }

                detail = vbox(lines);
            }

            // Pipeline progress footer
            Elements footer;
            if (state.ai_group_result.groups_total > 0) {
                footer.push_back(hbox({
                    text("  Groups: " + std::to_string(state.ai_group_result.groups_completed) +
                         "/" + std::to_string(state.ai_group_result.groups_total) +
                         " queried, " + std::to_string(state.ai_group_result.groups_succeeded) + " OK") | dim,
                    filler(),
                    text(!state.ai_group_result.model_used.empty() ?
                         "Model: " + state.ai_group_result.model_used : "") | dim,
                }));
            }
            footer.push_back(hbox({
                text("  [Up/Down] groups  [Left/Right] crews  [+] Good  [-] Bad  [Enter] Re-run") | dim,
            }));

            content = vbox({
                hbox({
                    vbox(left_rows) | vscroll_indicator | yframe | size(WIDTH, EQUAL, 45),
                    separator(),
                    detail | flex | vscroll_indicator | yframe,
                }) | flex,
                separator(),
                vbox(footer),
            });
        }
    } else if (safe_mode == 1) {
        // Crew Recommendations
        if (!state.ai_crew_result.ok()) {
            if (!state.ai_crew_result.error.empty()) {
                content = vbox({
                    text("Error: " + state.ai_crew_result.error) | color(Color::Red),
                    text("Press [Enter] to retry.") | dim,
                });
            } else {
                content = text("Press [Enter] to get AI crew recommendations for the current scenario.") | center | dim;
            }
        } else {
            Elements left_rows;
            for (size_t i = 0; i < state.ai_crew_result.recommendations.size(); ++i) {
                const auto& rec = state.ai_crew_result.recommendations[i];
                bool sel = ((int)i == state.ai_selected_rec);

                auto row = vbox({
                    hbox({
                        text("#" + std::to_string(i + 1)) | bold |
                            color(i == 0 ? Color(Color::Gold1) : Color(Color::GrayLight)),
                        text("  Captain: "),
                        text(rec.captain) | bold | color(Color::Yellow),
                        filler(),
                        text("Conf: " + std::to_string((int)(rec.confidence * 100)) + "%") | dim,
                    }),
                    hbox({
                        text("    Bridge: "),
                        text(rec.bridge.size() > 0 ? rec.bridge[0] : "?") | color(Color::Cyan),
                        text(" + "),
                        text(rec.bridge.size() > 1 ? rec.bridge[1] : "?") | color(Color::Cyan),
                    }),
                });
                if (sel) row = row | inverted | focus;
                left_rows.push_back(row);
                if (i < state.ai_crew_result.recommendations.size() - 1)
                    left_rows.push_back(separatorLight());
            }

            // Detail for selected
            Element detail = text("");
            int sri = state.ai_selected_rec;
            if (sri >= 0 && sri < (int)state.ai_crew_result.recommendations.size()) {
                const auto& rec = state.ai_crew_result.recommendations[sri];
                Elements lines;
                lines.push_back(text("AI Reasoning") | bold);
                lines.push_back(separator());

                auto wrapped = wrap_text(rec.reasoning, 50);
                for (auto& w : wrapped) lines.push_back(std::move(w));

                if (!rec.ship_advice.empty()) {
                    lines.push_back(separator());
                    lines.push_back(hbox({text("Ship: ") | bold, text(rec.ship_advice) | color(Color::Yellow)}));
                }

                if (!rec.below_decks.empty()) {
                    lines.push_back(separator());
                    lines.push_back(text("Below Decks:") | bold | color(Color::Magenta));
                    for (const auto& bd : rec.below_decks) {
                        lines.push_back(text("  " + bd) | color(Color::Cyan));
                    }
                }

                if (!rec.warnings.empty()) {
                    lines.push_back(separator());
                    lines.push_back(text("Warnings:") | bold | color(Color::Red));
                    for (const auto& w : rec.warnings) {
                        auto wl = wrap_text("  " + w, 50, color(Color::Red) | dim);
                        for (auto& l : wl) lines.push_back(std::move(l));
                    }
                }

                detail = vbox(lines);
            }

            content = hbox({
                vbox(left_rows) | vscroll_indicator | yframe | flex,
                separator(),
                detail | size(WIDTH, EQUAL, 55) | vscroll_indicator | yframe,
            });

            // Show model info
            Elements footer;
            if (!state.ai_crew_result.model_used.empty())
                footer.push_back(text("  Model: " + state.ai_crew_result.model_used) | dim);
            if (state.ai_crew_result.search_grounded)
                footer.push_back(text("  [Web Search Grounded]") | color(Color::Green) | dim);
            if (!footer.empty()) {
                content = vbox({content | flex, separator(), hbox(footer)});
            }
        }
    } else if (safe_mode == 2) {
        // Progression Advice
        if (!state.ai_progression_result.ok()) {
            if (!state.ai_progression_result.error.empty()) {
                content = vbox({
                    text("Error: " + state.ai_progression_result.error) | color(Color::Red),
                    text("Press [Enter] to retry.") | dim,
                });
            } else {
                content = text("Press [Enter] to get AI progression advice.") | center | dim;
            }
        } else {
            Elements left_rows;
            for (size_t i = 0; i < state.ai_progression_result.investments.size(); ++i) {
                const auto& inv = state.ai_progression_result.investments[i];
                bool sel = ((int)i == state.ai_selected_inv);

                Color prio_color = inv.priority <= 1 ? Color::Red
                    : (inv.priority <= 3 ? Color::Yellow : Color::GrayLight);

                auto row = vbox({
                    hbox({
                        text("P" + std::to_string(inv.priority)) | bold | color(prio_color),
                        text("  [" + inv.category + "] ") | dim,
                        text(inv.target) | bold | color(Color::Cyan),
                    }),
                    hbox({
                        text("    " + inv.action) | dim,
                    }),
                });
                if (sel) row = row | inverted | focus;
                left_rows.push_back(row);
                if (i < state.ai_progression_result.investments.size() - 1)
                    left_rows.push_back(separatorLight());
            }

            // Detail for selected
            Element detail = text("");
            int sii = state.ai_selected_inv;
            if (sii >= 0 && sii < (int)state.ai_progression_result.investments.size()) {
                const auto& inv = state.ai_progression_result.investments[sii];
                Elements lines;
                lines.push_back(text("Investment Detail") | bold);
                lines.push_back(separator());
                lines.push_back(hbox({text("Category: ") | bold, text(inv.category)}));
                lines.push_back(hbox({text("Target:   ") | bold, text(inv.target) | color(Color::Cyan)}));
                lines.push_back(hbox({text("Action:   ") | bold, text(inv.action)}));
                lines.push_back(separator());
                lines.push_back(text("Reason:") | bold);
                auto wrapped = wrap_text(inv.reason, 50);
                for (auto& w : wrapped) lines.push_back(std::move(w));
                detail = vbox(lines);
            }

            // Summary
            Elements summary_lines;
            if (!state.ai_progression_result.summary.empty()) {
                summary_lines.push_back(separator());
                summary_lines.push_back(text("Summary:") | bold);
                auto wrapped = wrap_text(state.ai_progression_result.summary, 90, dim);
                for (auto& w : wrapped) summary_lines.push_back(std::move(w));
            }

            content = vbox({
                hbox({
                    vbox(left_rows) | vscroll_indicator | yframe | flex,
                    separator(),
                    detail | size(WIDTH, EQUAL, 55) | vscroll_indicator | yframe,
                }) | flex,
                vbox(summary_lines),
            });
        }
    } else if (safe_mode == 3) {
        // META Analysis
        if (!state.ai_meta_result.ok()) {
            if (!state.ai_meta_result.error.empty()) {
                content = vbox({
                    text("Error: " + state.ai_meta_result.error) | color(Color::Red),
                    text("Press [Enter] to retry.") | dim,
                });
            } else {
                content = text("Press [Enter] to analyze the current META for your scenario.") | center | dim;
            }
        } else {
            Elements left_rows;
            for (size_t i = 0; i < state.ai_meta_result.top_crews.size(); ++i) {
                const auto& mc = state.ai_meta_result.top_crews[i];
                bool sel = ((int)i == state.ai_selected_meta);

                // Readiness indicator based on player_has vs missing
                std::string readiness;
                Color readiness_color = Color::GrayLight;
                if (!mc.player_has.empty() || !mc.missing.empty()) {
                    int total = (int)mc.player_has.size() + (int)mc.missing.size();
                    int owned = (int)mc.player_has.size();
                    readiness = " [" + std::to_string(owned) + "/" + std::to_string(total) + "]";
                    if (mc.missing.empty())
                        readiness_color = Color::Green;
                    else if (owned >= total / 2)
                        readiness_color = Color::Yellow;
                    else
                        readiness_color = Color::RedLight;
                }

                auto row = vbox({
                    hbox({
                        text("#" + std::to_string(i + 1)) | bold |
                            color(i == 0 ? Color(Color::Gold1) : Color(Color::GrayLight)),
                        text("  ") ,
                        text(mc.captain) | bold | color(Color::Yellow),
                        text("  for "),
                        text(mc.scenario) | color(Color::Cyan),
                        text(readiness) | color(readiness_color),
                    }),
                    hbox({
                        text("    Bridge: "),
                        text(mc.bridge.size() > 0 ? mc.bridge[0] : "?") | color(Color::Cyan),
                        text(" + "),
                        text(mc.bridge.size() > 1 ? mc.bridge[1] : "?") | color(Color::Cyan),
                    }),
                });
                if (sel) row = row | inverted | focus;
                left_rows.push_back(row);
                if (i < state.ai_meta_result.top_crews.size() - 1)
                    left_rows.push_back(separatorLight());
            }

            // Detail for selected
            Element detail = text("");
            int smi = state.ai_selected_meta;
            if (smi >= 0 && smi < (int)state.ai_meta_result.top_crews.size()) {
                const auto& mc = state.ai_meta_result.top_crews[smi];
                Elements lines;
                lines.push_back(text("META Crew Detail") | bold);
                lines.push_back(separator());

                // Show player ownership status
                if (!mc.player_has.empty()) {
                    lines.push_back(text("You have:") | bold | color(Color::Green));
                    for (const auto& name : mc.player_has)
                        lines.push_back(text("  + " + name) | color(Color::Green));
                }
                if (!mc.missing.empty()) {
                    lines.push_back(text("Missing:") | bold | color(Color::RedLight));
                    for (const auto& name : mc.missing)
                        lines.push_back(text("  - " + name) | color(Color::RedLight));
                }
                if (!mc.substitutes.empty()) {
                    lines.push_back(text("Substitutes:") | bold | color(Color::Yellow));
                    for (const auto& [miss, sub] : mc.substitutes)
                        lines.push_back(text("  " + miss + " -> " + sub) | color(Color::Yellow));
                }
                if (!mc.player_has.empty() || !mc.missing.empty())
                    lines.push_back(separator());

                auto wrapped = wrap_text(mc.explanation, 50);
                for (auto& w : wrapped) lines.push_back(std::move(w));
                detail = vbox(lines);
            }

            // META summary + sources
            Elements summary_lines;
            if (!state.ai_meta_result.meta_summary.empty()) {
                summary_lines.push_back(separator());
                summary_lines.push_back(text("META Summary:") | bold);
                auto wrapped = wrap_text(state.ai_meta_result.meta_summary, 90, dim);
                for (auto& w : wrapped) summary_lines.push_back(std::move(w));
            }
            if (!state.ai_meta_result.sources.empty()) {
                summary_lines.push_back(separator());
                summary_lines.push_back(text("Sources:") | bold | dim);
                for (const auto& src : state.ai_meta_result.sources) {
                    summary_lines.push_back(text("  " + src) | dim | color(Color::Blue));
                }
            }

            content = vbox({
                hbox({
                    vbox(left_rows) | vscroll_indicator | yframe | flex,
                    separator(),
                    detail | size(WIDTH, EQUAL, 55) | vscroll_indicator | yframe,
                }) | flex,
                vbox(summary_lines),
            });
        }
    } else if (safe_mode == 4) {
        // Ask mode (free-form question)
        Elements ask_lines;
        ask_lines.push_back(hbox({
            text("  Question: "),
            text(state.ai_question.empty() ? "(type to enter question)" : state.ai_question)
                | (state.ai_question_active ? (bold | color(Color::White)) : dim),
            text(state.ai_question_active ? "_" : "") | blink,
        }));
        ask_lines.push_back(text("  [A] Start typing  [Enter] Ask  [Esc] Cancel input") | dim);
        ask_lines.push_back(separator());

        if (!state.ai_ask_result.content.empty()) {
            auto wrapped = wrap_text(state.ai_ask_result.content, 90);
            for (auto& w : wrapped) ask_lines.push_back(std::move(w));
            if (!state.ai_ask_result.model_used.empty()) {
                ask_lines.push_back(separator());
                ask_lines.push_back(text("  Model: " + state.ai_ask_result.model_used) | dim);
            }
        } else if (!state.ai_ask_result.error.empty()) {
            ask_lines.push_back(text("Error: " + state.ai_ask_result.error) | color(Color::Red));
        } else {
            ask_lines.push_back(text("Ask any STFC question with your account context.") | center | dim);
        }

        content = vbox(ask_lines);
    }

    return vbox({
        header,
        separator(),
        content | flex | vscroll_indicator | yframe,
    });
}

// ---------------------------------------------------------------------------

static Element render_status_bar(AppState& state) {
    return hbox({
        text(" STFC Tool v0.5 ") | bold | bgcolor(Color::Blue) | color(Color::White),
        text(" "),
        text(state.get_status()) | flex,
        text(" "),
        text(" [R]efresh [S]ync [H]elp [Q]uit ") | dim,
    });
}

} // namespace stfc

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    using namespace stfc;

    auto state = std::make_shared<AppState>();

    // Tab labels
    std::vector<std::string> tab_labels = {
        "Overview",
        "Daily",
        "Weekly",
        "Crews",
        "Loadout",
        "Officers",
        "Ships",
        "Sync",
        "AI",
    };

    int selected_tab = 0;
    const int tab_count = (int)tab_labels.size();

    // Tab bar is a plain renderer — no Toggle component, so arrow keys are free
    // for per-tab use. Tab switching is handled exclusively via Tab/Shift+Tab
    // in the CatchEvent below.
    auto tab_bar = Renderer([&]() {
        Elements tabs;
        for (int i = 0; i < tab_count; i++) {
            auto label = text(" " + tab_labels[i] + " ");
            if (i == selected_tab) {
                label = label | bold | inverted;
            }
            tabs.push_back(label);
            if (i < tab_count - 1) tabs.push_back(text(" "));
        }
        return hbox(std::move(tabs));
    });

    // Main renderer
    auto main_renderer = Renderer(tab_bar, [&]() {
        // Help overlay takes over the whole screen
        if (state->show_help) {
            return vbox({
                render_help() | flex,
            }) | border;
        }

        Element content;
        switch (selected_tab) {
            case 0: content = render_overview(*state); break;
            case 1: content = render_daily_planner(*state); break;
            case 2: content = render_weekly_planner(*state); break;
            case 3: content = render_crew_optimizer(*state); break;
            case 4: content = render_loadout(*state); break;
            case 5: content = render_officers(*state); break;
            case 6: content = render_ships(*state); break;
            case 7: content = render_sync(*state); break;
            case 8: content = render_ai_advisor(*state); break;
            default: content = text("Unknown tab");
        }

        return vbox({
            tab_bar->Render() | center,
            separator(),
            content | flex,
            separator(),
            render_status_bar(*state),
        }) | border;
    });

    // Handle keyboard events
    auto main_component = CatchEvent(main_renderer, [&](Event event) {
        // If a filter input is active, route events to the filter handler
        // before any global key handlers (so typing 'h', 'q', etc. goes to filter)
        bool filter_active = (selected_tab == 5 && state->officer_filter_active) ||
                             (selected_tab == 6 && state->ship_filter_active);

        // Global: Help toggle (must come first so H works from anywhere)
        if (!filter_active && (event == Event::Character('h') || event == Event::Character('H'))) {
            state->show_help = !state->show_help;
            return true;
        }

        // If help is showing, block all other input
        if (state->show_help) {
            if (event == Event::Escape) {
                state->show_help = false;
                return true;
            }
            return true;
        }

        // Global: Tab / Shift+Tab to switch tabs (also exits filter mode)
        if (event == Event::Tab) {
            state->officer_filter_active = false;
            state->ship_filter_active = false;
            selected_tab = (selected_tab + 1) % tab_count;
            return true;
        }
        if (event == Event::TabReverse) {
            state->officer_filter_active = false;
            state->ship_filter_active = false;
            selected_tab = (selected_tab - 1 + tab_count) % tab_count;
            return true;
        }

        // Global: Quit
        if (!filter_active && (event == Event::Character('q') || event == Event::Character('Q'))) {
            state->save_plans();
            auto screen = ScreenInteractive::Active();
            if (screen) screen->Exit();
            return true;
        }

        // Global: Refresh game data
        if (!filter_active && (event == Event::Character('r') || event == Event::Character('R'))) {
            if (!state->loading) {
                state->loading = true;
                state->set_status("Fetching game data from api.spocks.club...");
                std::thread([state]() {
                    state->api_client.set_cache_only(false);
                    state->api_client.set_force_refresh(true);
                    state->api_client.set_progress_callback(
                        [state](const std::string& step, int, int) {
                            state->set_status("Loading " + step + "...");
                        }
                    );
                    bool ok = state->api_client.fetch_all(state->game_data);
                    if (ok) {
                        state->set_status("Loaded " +
                            std::to_string(state->game_data.officers.size()) + " officers, " +
                            std::to_string(state->game_data.ships.size()) + " ships, " +
                            std::to_string(state->game_data.researches.size()) + " research nodes");
                        state->data_loaded = true;
                        // Re-resolve player names with fresh game data
                        resolve_player_names(state->player_data, state->game_data);
                        state->rebuild_optimizer_from_current_data();
                        // Re-enrich planner with updated context
                        state->planner.enrich_plan_with_player_data(
                            state->daily_plan, state->player_data, state->game_data);
                    } else {
                        state->set_status("Failed to fetch game data.");
                    }
                    state->loading = false;
                }).detach();
            }
            return true;
        }

        // Global: Toggle ingress server
        if (!filter_active && (event == Event::Character('s') || event == Event::Character('S'))) {
            if (selected_tab != 1) {  // 's' on Daily tab means skip
                if (state->ingress_server.is_running()) {
                    state->ingress_server.stop();
                    state->set_status("Ingress server stopped.");
                } else {
                    state->ingress_server.set_data_callback([state](const std::string& data_type) {
                        state->set_status("Received sync data: " + data_type);
                        state->player_data = state->ingress_server.get_player_data();
                        // Resolve names against game data
                        if (state->data_loaded) {
                            resolve_player_names(state->player_data, state->game_data);
                            state->rebuild_optimizer_from_current_data();
                            // Re-enrich planner with updated player data
                            state->planner.enrich_plan_with_player_data(
                                state->daily_plan, state->player_data, state->game_data);
                        }
                    });
                    if (state->ingress_server.start()) {
                        state->set_status("Ingress server started on port " +
                            std::to_string(state->ingress_server.port()));
                    } else {
                        state->set_status("Failed to start ingress server!");
                    }
                }
                return true;
            }
        }

        // === Daily Planner tab events ===
        if (selected_tab == 1) {
            auto& plan = state->daily_plan;

            if (event == Event::ArrowDown) {
                if (state->selected_daily_task < (int)plan.tasks.size() - 1) {
                    state->selected_daily_task++;
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_daily_task > 0) {
                    state->selected_daily_task--;
                }
                return true;
            }
            if (event == Event::Character(' ')) {
                if (state->selected_daily_task >= 0 && state->selected_daily_task < (int)plan.tasks.size()) {
                    state->planner.toggle_task(plan, plan.tasks[state->selected_daily_task].id);
                    state->save_plans();
                    state->status_message = "Task toggled. " + std::to_string(plan.completed_tasks()) + "/" + std::to_string(plan.total_tasks()) + " done.";
                }
                return true;
            }
            if (event == Event::Character('s') || event == Event::Character('S')) {
                if (state->selected_daily_task >= 0 && state->selected_daily_task < (int)plan.tasks.size()) {
                    state->planner.skip_task(plan, plan.tasks[state->selected_daily_task].id, "skipped by user");
                    state->save_plans();
                    state->status_message = "Task skipped.";
                }
                return true;
            }
            if (event == Event::Character('c') || event == Event::Character('C')) {
                state->show_completed = !state->show_completed;
                state->status_message = state->show_completed ? "Showing completed tasks" : "Hiding completed tasks";
                return true;
            }
        }

        // === Weekly Planner tab events ===
        if (selected_tab == 2) {
            auto& plan = state->weekly_plan;

            if (event == Event::ArrowLeft) {
                if (state->selected_weekly_day > 0) state->selected_weekly_day--;
                return true;
            }
            if (event == Event::ArrowRight) {
                if (state->selected_weekly_day < 6) state->selected_weekly_day++;
                return true;
            }
            if (event == Event::ArrowDown) {
                if (state->selected_weekly_goal < (int)plan.goals.size() - 1) {
                    state->selected_weekly_goal++;
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_weekly_goal > 0) state->selected_weekly_goal--;
                return true;
            }
            if (event == Event::Character('+') || event == Event::Character('=')) {
                if (state->selected_weekly_goal >= 0 && state->selected_weekly_goal < (int)plan.goals.size()) {
                    auto& g = plan.goals[state->selected_weekly_goal];
                    state->planner.update_goal_progress(plan, g.id, g.progress_current + 1);
                    state->save_plans();
                    state->status_message = g.title + ": " + std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total);
                }
                return true;
            }
            if (event == Event::Character('-')) {
                if (state->selected_weekly_goal >= 0 && state->selected_weekly_goal < (int)plan.goals.size()) {
                    auto& g = plan.goals[state->selected_weekly_goal];
                    int new_val = std::max(0, g.progress_current - 1);
                    state->planner.update_goal_progress(plan, g.id, new_val);
                    state->save_plans();
                    state->status_message = g.title + ": " + std::to_string(g.progress_current) + "/" + std::to_string(g.progress_total);
                }
                return true;
            }
            // Space to toggle day task completion
            if (event == Event::Character(' ')) {
                if (state->selected_weekly_day >= 0 && state->selected_weekly_day < (int)plan.days.size()) {
                    // Toggle the first uncompleted task on the selected day
                    auto& day = plan.days[state->selected_weekly_day];
                    for (auto& t : day.tasks) {
                        if (!t.completed && !t.skipped) {
                            state->planner.toggle_task(day, t.id);
                            state->save_plans();
                            state->status_message = "Marked '" + t.title + "' done on " + std::string(short_dow(state->selected_weekly_day));
                            break;
                        }
                    }
                }
                return true;
            }
        }

        // === Crew Optimizer tab events ===
        if (selected_tab == 3) {
            if (event == Event::Character('<') || event == Event::Character(',')) {
                if (state->crew_scenario > 0) {
                    state->crew_scenario--;
                    state->selected_crew = 0;
                    if (!state->crew_results.empty()) {
                        // Auto-recompute since user already had results showing
                        state->status_message = "Running crew optimizer...";
                        state->run_crew_optimizer();
                        if (!state->crew_results.empty()) {
                            state->status_message = "Found " + std::to_string(state->crew_results.size()) +
                                " crews for " + scenario_label(all_dock_scenarios()[state->crew_scenario]);
                        } else {
                            state->status_message = "No crews found.";
                        }
                    }
                }
                return true;
            }
            if (event == Event::Character('>') || event == Event::Character('.')) {
                if (state->crew_scenario < (int)all_dock_scenarios().size() - 1) {
                    state->crew_scenario++;
                    state->selected_crew = 0;
                    if (!state->crew_results.empty()) {
                        state->status_message = "Running crew optimizer...";
                        state->run_crew_optimizer();
                        if (!state->crew_results.empty()) {
                            state->status_message = "Found " + std::to_string(state->crew_results.size()) +
                                " crews for " + scenario_label(all_dock_scenarios()[state->crew_scenario]);
                        } else {
                            state->status_message = "No crews found.";
                        }
                    }
                }
                return true;
            }
            if (event == Event::Character('t') || event == Event::Character('T')) {
                state->crew_ship_type = (state->crew_ship_type + 1) % 4;
                state->selected_crew = 0;
                if (!state->crew_results.empty()) {
                    state->status_message = "Running crew optimizer...";
                    state->run_crew_optimizer();
                    if (!state->crew_results.empty()) {
                        state->status_message = "Found " + std::to_string(state->crew_results.size()) +
                            " crews for " + scenario_label(all_dock_scenarios()[state->crew_scenario]);
                    } else {
                        state->status_message = "No crews found.";
                    }
                }
                return true;
            }
            if (event == Event::Return) {
                state->status_message = "Running crew optimizer...";
                state->run_crew_optimizer();
                if (!state->crew_results.empty()) {
                    state->status_message = "Found " + std::to_string(state->crew_results.size()) +
                        " crews for " + scenario_label(all_dock_scenarios()[state->crew_scenario]);
                } else {
                    state->status_message = "No crews found.";
                }
                state->selected_crew = 0;
                return true;
            }
            if (event == Event::ArrowDown) {
                if (state->selected_crew < (int)state->crew_results.size() - 1) {
                    state->selected_crew++;
                    state->update_crew_bda();
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_crew > 0) {
                    state->selected_crew--;
                    state->update_crew_bda();
                }
                return true;
            }
            // Navigate BDA suggestions
            if (event == Event::Character('b') || event == Event::Character('B')) {
                int max_bda = (int)state->crew_bda_results.size();
                if (max_bda > 0) {
                    state->selected_crew_bda = (state->selected_crew_bda + 1) % max_bda;
                }
                return true;
            }
        }

        // === AI Advisor tab events ===
        if (selected_tab == 8) {
            // Text input mode for Ask
            if (state->ai_question_active) {
                if (event == Event::Escape) {
                    state->ai_question_active = false;
                    return true;
                }
                if (event == Event::Return) {
                    state->ai_question_active = false;
                    // Fall through to run the query below
                } else if (event == Event::Backspace) {
                    if (!state->ai_question.empty()) state->ai_question.pop_back();
                    return true;
                } else if (event.is_character()) {
                    state->ai_question += event.character();
                    return true;
                }
                // If we didn't return, it was Enter — fall through
            }

            // Mode cycling
            if (event == Event::Character('<') || event == Event::Character(',')) {
                state->ai_mode = (state->ai_mode + 4) % 5;  // wrap backwards (5 modes)
                return true;
            }
            if (event == Event::Character('>') || event == Event::Character('.')) {
                state->ai_mode = (state->ai_mode + 1) % 5;
                return true;
            }

            // Cancel running group pipeline
            if (event == Event::Escape && state->ai_running && state->ai_mode == 0) {
                state->ai_cancel_groups = true;
                state->set_status("Cancelling group pipeline...");
                return true;
            }

            // Rating keys (Groups mode only)
            if (state->ai_mode == 0 && !state->ai_running) {
                if (event == Event::Character('+') || event == Event::Character('=') ||
                    event == Event::Character('g') || event == Event::Character('G')) {
                    int sgi = state->ai_selected_group;
                    if (sgi >= 0 && sgi < (int)state->ai_group_result.group_results.size()) {
                        auto& gr = state->ai_group_result.group_results[sgi];
                        if (!gr.history_id.empty()) {
                            gr.rating = AiRating::Good;
                            state->ai_engine.rate_result(gr.history_id, AiRating::Good);
                            state->set_status("Rated " + gr.group_name + " as Good [+]");
                        }
                    }
                    return true;
                }
                if (event == Event::Character('-') || event == Event::Character('_') ||
                    event == Event::Character('b') || event == Event::Character('B')) {
                    int sgi = state->ai_selected_group;
                    if (sgi >= 0 && sgi < (int)state->ai_group_result.group_results.size()) {
                        auto& gr = state->ai_group_result.group_results[sgi];
                        if (!gr.history_id.empty()) {
                            gr.rating = AiRating::Bad;
                            state->ai_engine.rate_result(gr.history_id, AiRating::Bad);
                            state->set_status("Rated " + gr.group_name + " as Bad [-]");
                        }
                    }
                    return true;
                }
            }

            // Navigate results
            if (event == Event::ArrowDown) {
                if (state->ai_mode == 0) {
                    int max = (int)state->ai_group_result.group_results.size() - 1;
                    if (state->ai_selected_group < max) {
                        state->ai_selected_group++;
                        state->ai_selected_group_crew = 0;
                    }
                } else if (state->ai_mode == 1) {
                    int max = (int)state->ai_crew_result.recommendations.size() - 1;
                    if (state->ai_selected_rec < max) state->ai_selected_rec++;
                } else if (state->ai_mode == 2) {
                    int max = (int)state->ai_progression_result.investments.size() - 1;
                    if (state->ai_selected_inv < max) state->ai_selected_inv++;
                } else if (state->ai_mode == 3) {
                    int max = (int)state->ai_meta_result.top_crews.size() - 1;
                    if (state->ai_selected_meta < max) state->ai_selected_meta++;
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->ai_mode == 0) {
                    if (state->ai_selected_group > 0) {
                        state->ai_selected_group--;
                        state->ai_selected_group_crew = 0;
                    }
                } else if (state->ai_mode == 1) {
                    if (state->ai_selected_rec > 0) state->ai_selected_rec--;
                } else if (state->ai_mode == 2) {
                    if (state->ai_selected_inv > 0) state->ai_selected_inv--;
                } else if (state->ai_mode == 3) {
                    if (state->ai_selected_meta > 0) state->ai_selected_meta--;
                }
                return true;
            }
            // Left/Right navigate crews within a group (Groups mode)
            if (state->ai_mode == 0) {
                if (event == Event::ArrowRight) {
                    int sgi = state->ai_selected_group;
                    if (sgi >= 0 && sgi < (int)state->ai_group_result.group_results.size()) {
                        int max = (int)state->ai_group_result.group_results[sgi].crews.size() - 1;
                        if (state->ai_selected_group_crew < max) state->ai_selected_group_crew++;
                    }
                    return true;
                }
                if (event == Event::ArrowLeft) {
                    if (state->ai_selected_group_crew > 0) state->ai_selected_group_crew--;
                    return true;
                }
            }

            // Start typing a question (Ask mode)
            if (event == Event::Character('a') || event == Event::Character('A')) {
                if (state->ai_mode == 4 && !state->ai_running) {
                    state->ai_question_active = true;
                    state->ai_question.clear();
                    return true;
                }
            }

            // Re-initialize AI engine
            if (event == Event::Character('i') || event == Event::Character('I')) {
                if (!state->ai_running) {
                    state->set_status("Re-initializing AI...");
                    std::thread([state]() {
                        auto err = state->ai_engine.reinitialize();
                        if (err.empty()) {
                            auto s = state->ai_engine.status();
                            state->set_status("AI re-initialized: " + s.provider + "/" + s.model);
                        } else {
                            state->set_status("AI re-init failed: " + err);
                        }
                        auto screen = ScreenInteractive::Active();
                        if (screen) screen->PostEvent(Event::Custom);
                    }).detach();
                }
                return true;
            }

            // Refresh META cache (Gemini web-search-grounded)
            if (event == Event::Character('m') || event == Event::Character('M')) {
                if (!state->ai_running && !state->ai_meta_refreshing) {
                    if (!state->ai_engine.is_available()) {
                        state->set_status("AI not available. Press [I] to re-initialize.");
                        return true;
                    }
                    if (!state->optimizer) {
                        state->set_status("No roster loaded — need officer names for META matching.");
                        return true;
                    }

                    state->ai_meta_refreshing = true;
                    state->ai_meta_progress = "Starting META refresh...";
                    {
                        std::lock_guard<std::mutex> lk(state->status_mutex);
                        state->ai_stream_text.clear();
                    }
                    state->ai_cancel_groups = false;
                    state->set_status("Refreshing META cache from Gemini...");

                    std::thread([state]() {
                        // Build known officer name list
                        const auto& officers = state->optimizer->officers();
                        std::vector<std::string> known_names;
                        known_names.reserve(officers.size());
                        for (const auto& off : officers) {
                            known_names.push_back(off.name);
                        }

                        auto stream_cb = [state](const std::string& chunk) {
                            std::lock_guard<std::mutex> lk(state->status_mutex);
                            state->ai_stream_text += chunk;
                            auto screen = ScreenInteractive::Active();
                            if (screen) screen->PostEvent(Event::Custom);
                        };

                        auto progress_cb = [state](int current, int total, const std::string& group_name) {
                            state->ai_meta_progress = "Refreshing " + group_name + " (" +
                                std::to_string(current) + "/" + std::to_string(total) + ")...";
                            auto screen = ScreenInteractive::Active();
                            if (screen) screen->PostEvent(Event::Custom);
                        };

                        auto err = state->ai_engine.refresh_meta_cache(
                            known_names, stream_cb, progress_cb, &state->ai_cancel_groups);

                        {
                            std::lock_guard<std::mutex> lk(state->status_mutex);
                            state->ai_stream_text.clear();
                        }
                        state->ai_meta_progress.clear();
                        state->ai_meta_refreshing = false;

                        if (err.empty()) {
                            auto age = state->ai_engine.meta_cache().age_str();
                            int total_officers = 0;
                            for (const auto& [k, v] : state->ai_engine.meta_cache().groups) {
                                total_officers += static_cast<int>(v.top_officers.size());
                            }
                            state->set_status("META cache refreshed (" + age + ", " +
                                std::to_string(total_officers) + " META officers matched)");
                        } else {
                            state->set_status("META refresh error: " + err);
                        }

                        auto screen = ScreenInteractive::Active();
                        if (screen) screen->PostEvent(Event::Custom);
                    }).detach();
                }
                return true;
            }

            // Cancel META refresh
            if (event == Event::Escape && state->ai_meta_refreshing) {
                state->ai_cancel_groups = true;
                state->set_status("Cancelling META refresh...");
                return true;
            }

            // Run AI query
            if (event == Event::Return && !state->ai_running && !state->ai_meta_refreshing) {
                if (!state->ai_engine.is_available()) {
                    state->set_status("AI not available. Press [I] to re-initialize.");
                    return true;
                }
                if (!state->optimizer) {
                    state->set_status("No roster loaded — AI needs officer data.");
                    return true;
                }

                state->ai_running = true;
                {
                    std::lock_guard<std::mutex> lk(state->status_mutex);
                    state->ai_stream_text.clear();
                }
                int mode = state->ai_mode;

                // Stream callback: accumulates text for live display
                auto stream_cb = [state](const std::string& chunk) {
                    std::lock_guard<std::mutex> lk(state->status_mutex);
                    state->ai_stream_text += chunk;
                    // Trigger screen redraw on each chunk
                    auto screen = ScreenInteractive::Active();
                    if (screen) screen->PostEvent(Event::Custom);
                };

                const auto& scenarios = all_dock_scenarios();
                int safe_scenario = std::clamp(state->crew_scenario, 0, (int)scenarios.size() - 1);
                ShipType st = ShipType::Explorer;
                if (state->crew_ship_type == 1) st = ShipType::Battleship;
                if (state->crew_ship_type == 2) st = ShipType::Interceptor;
                if (state->crew_ship_type == 3) st = ShipType::Survey;

                std::string question_copy = state->ai_question;

                state->set_status("AI query running (" + std::string(mode == 0 ? "groups" : mode == 1 ? "crew" : mode == 2 ? "progression" : mode == 3 ? "META" : "ask") + ")...");

                std::thread([state, mode, safe_scenario, st, stream_cb, question_copy]() {
                    const auto& scenarios = all_dock_scenarios();
                    const auto& officers = state->optimizer->officers();

                    if (mode == 0) {
                        // Group-based pipeline
                        state->ai_cancel_groups = false;
                        state->ai_group_progress = "Starting group pipeline...";
                        auto progress_cb = [state](int current, int total, const std::string& group_name) {
                            state->ai_group_progress = "Querying " + group_name + " (" +
                                std::to_string(current + 1) + "/" + std::to_string(total) + ")...";
                            auto screen = ScreenInteractive::Active();
                            if (screen) screen->PostEvent(Event::Custom);
                        };
                        state->ai_group_result = state->ai_engine.query_by_groups(
                            officers, stream_cb, progress_cb, &state->ai_cancel_groups);
                        state->ai_selected_group = 0;
                        state->ai_selected_group_crew = 0;
                        state->ai_group_progress.clear();
                        if (state->ai_group_result.ok()) {
                            state->set_status("AI: " + std::to_string(state->ai_group_result.groups_succeeded) +
                                "/" + std::to_string(state->ai_group_result.groups_total) + " groups succeeded.");
                        } else {
                            state->set_status("AI error: " + state->ai_group_result.error);
                        }
                    } else if (mode == 1) {
                        state->ai_crew_result = state->ai_engine.recommend_crews(
                            state->player_data, state->game_data, officers,
                            scenarios[safe_scenario], st, 3, {}, stream_cb);
                        state->ai_selected_rec = 0;
                        if (state->ai_crew_result.ok()) {
                            state->set_status("AI: " + std::to_string(state->ai_crew_result.recommendations.size()) + " crew recommendations.");
                        } else {
                            state->set_status("AI error: " + state->ai_crew_result.error);
                        }
                    } else if (mode == 2) {
                        state->ai_progression_result = state->ai_engine.advise_progression(
                            state->player_data, state->game_data, officers, "", stream_cb);
                        state->ai_selected_inv = 0;
                        if (state->ai_progression_result.ok()) {
                            state->set_status("AI: " + std::to_string(state->ai_progression_result.investments.size()) + " investment suggestions.");
                        } else {
                            state->set_status("AI error: " + state->ai_progression_result.error);
                        }
                    } else if (mode == 3) {
                        state->ai_meta_result = state->ai_engine.analyze_meta(
                            scenarios[safe_scenario],
                            state->player_data, state->game_data, officers,
                            state->optimizer.get(), stream_cb);
                        state->ai_selected_meta = 0;
                        if (state->ai_meta_result.ok()) {
                            state->set_status("AI: META analysis complete.");
                        } else {
                            state->set_status("AI error: " + state->ai_meta_result.error);
                        }
                    } else {
                        if (question_copy.empty()) {
                            state->ai_ask_result = LlmResponse{};
                            state->ai_ask_result.error = "No question entered. Press [A] to type a question.";
                            state->set_status("AI: No question entered.");
                        } else {
                            state->ai_ask_result = state->ai_engine.ask_question(
                                question_copy,
                                state->player_data, state->game_data, officers, stream_cb);
                            if (state->ai_ask_result.error.empty()) {
                                state->set_status("AI: Answer received.");
                            } else {
                                state->set_status("AI error: " + state->ai_ask_result.error);
                            }
                        }
                    }

                    // Clear streaming text before marking as done —
                    // prevents flash of raw text between streaming and parsed display
                    {
                        std::lock_guard<std::mutex> lk(state->status_mutex);
                        state->ai_stream_text.clear();
                    }
                    state->ai_running = false;
                    auto screen = ScreenInteractive::Active();
                    if (screen) screen->PostEvent(Event::Custom);
                }).detach();

                return true;
            }
        }

        // === Loadout tab events ===
        if (selected_tab == 4) {
            if (state->show_dock_modal) {
                auto& cfg = state->dock_configs[state->selected_dock];
                if (event == Event::Escape || event == Event::Return) {
                    state->show_dock_modal = false;
                    state->status_message = "Closed dock editor.";
                    return true;
                }
                if (event == Event::ArrowDown) {
                    state->dock_modal_field = std::min(4, state->dock_modal_field + 1);
                    return true;
                }
                if (event == Event::ArrowUp) {
                    state->dock_modal_field = std::max(0, state->dock_modal_field - 1);
                    return true;
                }
                if (event == Event::Character('<') || event == Event::Character(',') ||
                    event == Event::Character('>') || event == Event::Character('.')) {
                    int dir = (event == Event::Character('>') || event == Event::Character('.')) ? 1 : -1;
                    if (state->dock_modal_field == 0) {
                        const auto& all = all_dock_scenarios();
                        auto it = std::find(all.begin(), all.end(), cfg.scenario);
                        if (it != all.end()) {
                            int idx = static_cast<int>(std::distance(all.begin(), it));
                            idx = std::clamp(idx + dir, 0, (int)all.size() - 1);
                            cfg.scenario = all[idx];
                            cfg.mining_resource = scenario_mining_resource(cfg.scenario);
                            cfg.mining_objective = scenario_mining_objective(cfg.scenario);
                        }
                    } else if (state->dock_modal_field == 1 && is_mining_scenario(cfg.scenario)) {
                        int resource = static_cast<int>(cfg.mining_resource == MiningResource::None
                            ? scenario_mining_resource(cfg.scenario) : cfg.mining_resource);
                        resource += dir;
                        if (resource < 1) resource = 7;
                        if (resource > 7) resource = 1;
                        cfg.mining_resource = static_cast<MiningResource>(resource);
                    } else if (state->dock_modal_field == 2 && is_mining_scenario(cfg.scenario)) {
                        int objective = static_cast<int>(cfg.mining_objective == MiningObjective::None
                            ? scenario_mining_objective(cfg.scenario) : cfg.mining_objective);
                        objective += dir;
                        if (objective < 1) objective = 3;
                        if (objective > 3) objective = 1;
                        cfg.mining_objective = static_cast<MiningObjective>(objective);
                    } else if (state->dock_modal_field == 4) {
                        cfg.locked = !cfg.locked;
                        if (!cfg.locked) {
                            cfg.locked_captain.clear();
                            cfg.locked_bridge.clear();
                        }
                    }
                    state->loadout_computed = false;
                    return true;
                }
                return true;
            }
            if (event == Event::ArrowDown) {
                if (state->selected_dock < 6) {
                    state->selected_dock++;
                    state->selected_dock_bda = 0;
                }
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_dock > 0) {
                    state->selected_dock--;
                    state->selected_dock_bda = 0;
                }
                return true;
            }
            if (event == Event::Return) {
                state->show_dock_modal = true;
                state->dock_modal_field = 0;
                state->status_message = "Editing dock configuration.";
                return true;
            }
            // Cycle ship type
            if (event == Event::Character('t') || event == Event::Character('T')) {
                state->crew_ship_type = (state->crew_ship_type + 1) % 4;
                state->loadout_computed = false;
                return true;
            }
            if (event == Event::Character('p') || event == Event::Character('P')) {
                int posture = static_cast<int>(state->loadout_posture);
                posture = (posture + 1) % 6;
                state->loadout_posture = static_cast<LoadoutPosture>(posture);
                state->init_dock_configs();
                state->loadout_computed = false;
                state->status_message = "Loadout posture -> " + posture_label(state->loadout_posture);
                return true;
            }
            // Lock/unlock dock
            if (event == Event::Character('k') || event == Event::Character('K')) {
                auto& cfg = state->dock_configs[state->selected_dock];
                if (cfg.locked) {
                    cfg.locked = false;
                    cfg.locked_captain.clear();
                    cfg.locked_bridge.clear();
                    state->status_message = "Dock " + std::to_string(state->selected_dock + 1) + " unlocked.";
                } else if (state->loadout_computed &&
                           state->selected_dock < (int)state->loadout_result.docks.size()) {
                    // Lock with current assignment
                    const auto& dr = state->loadout_result.docks[state->selected_dock];
                    cfg.locked = true;
                    cfg.locked_captain = dr.captain;
                    cfg.locked_bridge = dr.bridge;
                    state->status_message = "Dock " + std::to_string(state->selected_dock + 1) +
                        " locked: " + dr.captain;
                }
                return true;
            }
            // Navigate BDA suggestions
            if (event == Event::Character('b') || event == Event::Character('B')) {
                if (state->loadout_computed &&
                    state->selected_dock < (int)state->loadout_result.docks.size()) {
                    int max_bda = (int)state->loadout_result.docks[state->selected_dock].bda_suggestions.size();
                    if (max_bda > 0) {
                        state->selected_dock_bda = (state->selected_dock_bda + 1) % max_bda;
                    }
                }
                return true;
            }
            // Run loadout optimizer (background thread)
            if (event == Event::Character('g') || event == Event::Character('G')) {
                if (state->optimizer && !state->loadout_running) {
                    state->set_status("Optimizing 7-dock loadout (background)...");
                    state->loadout_running = true;
                    std::thread([state]() {
                        state->run_loadout_optimizer();
                        if (!state->loadout_error.empty()) {
                            state->set_status(state->loadout_error);
                        } else {
                            state->set_status("Loadout optimized! " +
                                std::to_string(state->loadout_result.total_officers_used) +
                                " officers assigned across " +
                                std::to_string(state->loadout_result.docks.size()) + " docks.");
                        }
                        // Trigger screen redraw
                        auto screen = ScreenInteractive::Active();
                        if (screen) screen->PostEvent(Event::Custom);
                    }).detach();
                }
                return true;
            }
            // Load saved loadout
            if (event == Event::Character('l') || event == Event::Character('L')) {
                if (CrewOptimizer::load_loadout(state->loadout_result, ".stfc_loadout.json")) {
                    state->loadout_computed = state->loadout_matches_current_roster(state->loadout_result);
                    if (!state->loadout_computed) {
                        state->loadout_result = LoadoutResult{};
                    }
                    state->status_message = state->loadout_computed
                        ? "Loaded saved loadout from .stfc_loadout.json"
                        : "Saved loadout is stale for the current synced roster. Press Enter to recompute.";
                } else {
                    state->status_message = "No saved loadout found.";
                }
                return true;
            }
        }

        // === Officers tab events ===
        if (selected_tab == 5) {
            // Filter input mode: capture keystrokes as filter text
            if (state->officer_filter_active) {
                if (event == Event::Escape) {
                    state->officer_filter_active = false;
                    state->officer_filter.clear();
                    state->selected_officer = 0;
                    state->status_message = "Filter cleared.";
                    return true;
                }
                if (event == Event::Return) {
                    state->officer_filter_active = false;
                    state->status_message = "Filter applied: " + state->officer_filter;
                    return true;
                }
                if (event == Event::Backspace) {
                    if (!state->officer_filter.empty()) {
                        state->officer_filter.pop_back();
                        state->selected_officer = 0;
                    }
                    state->status_message = "Filter: " + state->officer_filter + "_";
                    return true;
                }
                if (event.is_character()) {
                    state->officer_filter += event.character();
                    state->selected_officer = 0;
                    state->status_message = "Filter: " + state->officer_filter + "_";
                    return true;
                }
                // Allow arrow keys to still navigate while filtering
                if (event == Event::ArrowDown) {
                    int max_off = (int)state->game_data.officers.size() - 1;
                    if (state->selected_officer < max_off) state->selected_officer++;
                    return true;
                }
                if (event == Event::ArrowUp) {
                    if (state->selected_officer > 0) state->selected_officer--;
                    return true;
                }
                return true;  // Consume all other events in filter mode
            }
            if (event == Event::ArrowDown) {
                int max_off = (int)state->game_data.officers.size() - 1;
                if (state->selected_officer < max_off) state->selected_officer++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_officer > 0) state->selected_officer--;
                return true;
            }
            if (event == Event::Character('f') || event == Event::Character('F')) {
                state->officer_filter_active = true;
                state->officer_filter.clear();
                state->selected_officer = 0;
                state->status_message = "Filter: _ (type to search, Enter to apply, Esc to clear)";
                return true;
            }
        }

        // === Ships tab events ===
        if (selected_tab == 6) {
            // Filter input mode: capture keystrokes as filter text
            if (state->ship_filter_active) {
                if (event == Event::Escape) {
                    state->ship_filter_active = false;
                    state->ship_filter.clear();
                    state->selected_ship = 0;
                    state->status_message = "Filter cleared.";
                    return true;
                }
                if (event == Event::Return) {
                    state->ship_filter_active = false;
                    state->status_message = "Filter applied: " + state->ship_filter;
                    return true;
                }
                if (event == Event::Backspace) {
                    if (!state->ship_filter.empty()) {
                        state->ship_filter.pop_back();
                        state->selected_ship = 0;
                    }
                    state->status_message = "Filter: " + state->ship_filter + "_";
                    return true;
                }
                if (event.is_character()) {
                    state->ship_filter += event.character();
                    state->selected_ship = 0;
                    state->status_message = "Filter: " + state->ship_filter + "_";
                    return true;
                }
                // Allow arrow keys to still navigate while filtering
                if (event == Event::ArrowDown) {
                    int max_ship = (int)state->game_data.ships.size() - 1;
                    if (state->selected_ship < max_ship) state->selected_ship++;
                    return true;
                }
                if (event == Event::ArrowUp) {
                    if (state->selected_ship > 0) state->selected_ship--;
                    return true;
                }
                return true;  // Consume all other events in filter mode
            }
            if (event == Event::ArrowDown) {
                int max_ship = (int)state->game_data.ships.size() - 1;
                if (state->selected_ship < max_ship) state->selected_ship++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->selected_ship > 0) state->selected_ship--;
                return true;
            }
            if (event == Event::Character('f') || event == Event::Character('F')) {
                state->ship_filter_active = true;
                state->ship_filter.clear();
                state->selected_ship = 0;
                state->status_message = "Filter: _ (type to search, Enter to apply, Esc to clear)";
                return true;
            }
        }

        // === Sync tab events ===
        if (selected_tab == 7) {
            if (event == Event::ArrowLeft) {
                if (state->sync_view_mode > 0) {
                    state->sync_view_mode--;
                    state->sync_selected_row = 0;
                }
                return true;
            }
            if (event == Event::ArrowRight) {
                if (state->sync_view_mode < sync_view_count - 1) {
                    state->sync_view_mode++;
                    state->sync_selected_row = 0;
                }
                return true;
            }
            if (event == Event::ArrowDown) {
                state->sync_selected_row++;
                return true;
            }
            if (event == Event::ArrowUp) {
                if (state->sync_selected_row > 0) state->sync_selected_row--;
                return true;
            }
        }

        // === Mouse wheel support (all tabs) ===
        if (event.is_mouse()) {
            auto& mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp) {
                switch (selected_tab) {
                    case 1: if (state->selected_daily_task > 0) state->selected_daily_task--; break;
                    case 2: if (state->selected_weekly_goal > 0) state->selected_weekly_goal--; break;
                    case 3: if (state->selected_crew > 0) { state->selected_crew--; state->update_crew_bda(); } break;
                    case 4: if (state->selected_dock > 0) { state->selected_dock--; state->selected_dock_bda = 0; } break;
                    case 5: if (state->selected_officer > 0) state->selected_officer--; break;
                    case 6: if (state->selected_ship > 0) state->selected_ship--; break;
                    case 7: if (state->sync_selected_row > 0) state->sync_selected_row--; break;
                    default: break;
                }
                return true;
            }
            if (mouse.button == Mouse::WheelDown) {
                switch (selected_tab) {
                    case 1: if (state->selected_daily_task < (int)state->daily_plan.tasks.size() - 1) state->selected_daily_task++; break;
                    case 2: if (state->selected_weekly_goal < (int)state->weekly_plan.goals.size() - 1) state->selected_weekly_goal++; break;
                    case 3: if (state->selected_crew < (int)state->crew_results.size() - 1) { state->selected_crew++; state->update_crew_bda(); } break;
                    case 4: if (state->selected_dock < 6) { state->selected_dock++; state->selected_dock_bda = 0; } break;
                    case 5: if (state->selected_officer < (int)state->game_data.officers.size() - 1) state->selected_officer++; break;
                    case 6: if (state->selected_ship < (int)state->game_data.ships.size() - 1) state->selected_ship++; break;
                    case 7: state->sync_selected_row++; break;
                    default: break;
                }
                return true;
            }
        }

        // === PageUp / PageDown (all scrollable tabs, jump 10 rows) ===
        if (event == Event::PageUp) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = std::max(0, state->selected_daily_task - 10); break;
                case 2: state->selected_weekly_goal = std::max(0, state->selected_weekly_goal - 10); break;
                case 3: state->selected_crew = std::max(0, state->selected_crew - 10); state->update_crew_bda(); break;
                case 4: state->selected_dock = std::max(0, state->selected_dock - 10); state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = std::max(0, state->selected_officer - 10); break;
                case 6: state->selected_ship = std::max(0, state->selected_ship - 10); break;
                case 7: state->sync_selected_row = std::max(0, state->sync_selected_row - 10); break;
                default: break;
            }
            return true;
        }
        if (event == Event::PageDown) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = std::min((int)state->daily_plan.tasks.size() - 1, state->selected_daily_task + 10); break;
                case 2: state->selected_weekly_goal = std::min((int)state->weekly_plan.goals.size() - 1, state->selected_weekly_goal + 10); break;
                case 3: state->selected_crew = std::min((int)state->crew_results.size() - 1, state->selected_crew + 10); state->update_crew_bda(); break;
                case 4: state->selected_dock = std::min(6, state->selected_dock + 10); state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = std::min((int)state->game_data.officers.size() - 1, state->selected_officer + 10); break;
                case 6: state->selected_ship = std::min((int)state->game_data.ships.size() - 1, state->selected_ship + 10); break;
                case 7: state->sync_selected_row += 10; break;
                default: break;
            }
            return true;
        }

        // === Home / End keys ===
        if (event == Event::Home) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = 0; break;
                case 2: state->selected_weekly_goal = 0; break;
                case 3: state->selected_crew = 0; state->update_crew_bda(); break;
                case 4: state->selected_dock = 0; state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = 0; break;
                case 6: state->selected_ship = 0; break;
                case 7: state->sync_selected_row = 0; break;
                default: break;
            }
            return true;
        }
        if (event == Event::End) {
            switch (selected_tab) {
                case 1: state->selected_daily_task = std::max(0, (int)state->daily_plan.tasks.size() - 1); break;
                case 2: state->selected_weekly_goal = std::max(0, (int)state->weekly_plan.goals.size() - 1); break;
                case 3: state->selected_crew = std::max(0, (int)state->crew_results.size() - 1); state->update_crew_bda(); break;
                case 4: state->selected_dock = 6; state->selected_dock_bda = 0; break;
                case 5: state->selected_officer = std::max(0, (int)state->game_data.officers.size() - 1); break;
                case 6: state->selected_ship = std::max(0, (int)state->game_data.ships.size() - 1); break;
                case 7: state->sync_selected_row = 999999; break;  // clamped in render
                default: break;
            }
            return true;
        }

        return false;
    });

    auto screen = ScreenInteractive::Fullscreen();
    screen.Loop(main_component);

    // Cleanup
    state->save_plans();
    state->ai_engine.shutdown();
    state->ingress_server.stop();

    return 0;
}
