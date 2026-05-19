#include "app/account_snapshot.h"

#include <algorithm>
#include <set>

namespace stfc {

// ---------------------------------------------------------------------------
// Helper: resolve ability from game data OfficerAbility + translations
// ---------------------------------------------------------------------------

static ResolvedAbility resolve_ability(const OfficerAbility& src,
                                        const std::string& name,
                                        const std::string& description,
                                        int rank) {
    ResolvedAbility ra;
    ra.name = name;
    ra.description = description;
    ra.is_percentage = src.value_is_percentage;

    for (const auto& v : src.values) {
        ra.values.push_back(v.value);
        ra.chances.push_back(v.chance);
    }

    return ra;
}

// ---------------------------------------------------------------------------
// Resolve officers: merge all game data officers with player state
// ---------------------------------------------------------------------------

static void resolve_officers(FullAccountSnapshot& snap,
                              const PlayerData& pd,
                              const GameData& gd) {
    // Build lookup: officer_id -> PlayerOfficer
    std::map<int64_t, const PlayerOfficer*> player_map;
    for (const auto& po : pd.officers) {
        player_map[po.officer_id] = &po;
    }

    // Build trait lookup: officer_id -> traits
    std::map<int64_t, std::vector<PlayerTrait>> trait_map;
    for (const auto& t : pd.traits) {
        trait_map[t.officer_id].push_back(t);
    }

    // Iterate ALL known officers from game data
    for (const auto& [id, off] : gd.officers) {
        ResolvedOfficer ro;
        ro.id = id;
        ro.name = off.name;
        ro.short_name = off.short_name;
        ro.officer_class = off.officer_class;
        ro.rarity = off.rarity;
        ro.faction = off.faction;
        ro.group = off.group_name;
        ro.synergy_id = off.synergy_id;
        ro.description = off.description;
        ro.has_bda = off.has_bda;

        auto pit = player_map.find(id);
        if (pit != player_map.end()) {
            const auto& po = *pit->second;
            ro.owned = true;
            ro.level = po.level;
            ro.rank = po.rank;
            ro.shard_count = po.shard_count;
            ro.attack = po.attack;
            ro.defense = po.defense;
            ro.health = po.health;
        }
        // else: owned=false, level/rank/stats stay at 0

        // Resolve abilities
        ro.captain_maneuver = resolve_ability(
            off.captain_ability, off.name + " CM", "", ro.rank);
        ro.officer_ability = resolve_ability(
            off.ability, off.name + " OA", "", ro.rank);
        if (off.has_bda) {
            ro.below_decks = resolve_ability(
                off.below_decks_ability, off.name + " BDA", "", ro.rank);
        }

        // Traits
        auto tit = trait_map.find(id);
        if (tit != trait_map.end()) {
            ro.traits = tit->second;
        }

        snap.officers.push_back(std::move(ro));
    }

    snap.owned_officer_count = static_cast<int>(
        std::count_if(snap.officers.begin(), snap.officers.end(),
                      [](const ResolvedOfficer& o) { return o.owned; }));
}

// ---------------------------------------------------------------------------
// Resolve ships
// ---------------------------------------------------------------------------

static void resolve_ships(FullAccountSnapshot& snap,
                           const PlayerData& pd,
                           const GameData& gd) {
    for (const auto& ps : pd.ships) {
        ResolvedShip rs;
        rs.hull_id = ps.hull_id;
        rs.ship_id = ps.ship_id;
        rs.tier = ps.tier;
        rs.level = ps.level;
        rs.level_percentage = ps.level_percentage;
        rs.components = ps.components;
        rs.name = ps.name;

        auto it = gd.ships.find(ps.hull_id);
        if (it != gd.ships.end()) {
            const auto& ship = it->second;
            rs.hull_type = ship.hull_type;
            rs.rarity = ship.rarity;
            rs.grade = ship.grade;
            rs.faction = ship.faction;
            rs.max_tier = ship.max_tier;
            rs.max_level = ship.max_level;
            rs.ability_name = ship.ability_name;
            rs.ability_description = ship.ability_description;

            // Find stats at player's current level
            for (const auto& sl : ship.levels) {
                if (sl.level == ps.level) {
                    rs.shield = sl.shield;
                    rs.health = sl.health;
                    break;
                }
            }
        }

        snap.ships.push_back(std::move(rs));
    }
}

// ---------------------------------------------------------------------------
// Resolve research
// ---------------------------------------------------------------------------

static void resolve_research(FullAccountSnapshot& snap,
                              const PlayerData& pd,
                              const GameData& gd) {
    std::map<int64_t, const PlayerResearch*> player_map;
    for (const auto& pr : pd.researches) {
        player_map[pr.research_id] = &pr;
    }

    std::map<int64_t, int> buff_levels;
    for (const auto& pb : pd.buffs) {
        if (pb.expired || pb.level <= 0) continue;
        auto it = buff_levels.find(pb.buff_id);
        if (it == buff_levels.end() || pb.level > it->second) {
            buff_levels[pb.buff_id] = pb.level;
        }
    }

    for (const auto& [id, res] : gd.researches) {
        ResolvedResearch rr;
        rr.id = id;
        rr.name = res.name;
        rr.description = res.description;
        rr.research_tree = res.research_tree;
        rr.unlock_level = res.unlock_level;
        rr.view_level = res.view_level;
        rr.generation = res.generation;
        rr.row = res.row;
        rr.column = res.column;
        rr.doubler = res.doubler;
        rr.buffs = res.buffs;
        rr.levels = res.levels;
        rr.current_level = player_map.empty() ? -1 : 0;

        auto pit = player_map.find(id);
        if (pit != player_map.end()) {
            rr.current_level = pit->second->level;
            if (!pit->second->name.empty()) {
                rr.name = pit->second->name;
            }
        } else {
            int inferred_level = 0;
            for (const auto& buff : res.buffs) {
                auto bit = buff_levels.find(buff.id);
                if (bit != buff_levels.end()) {
                    inferred_level = std::max(inferred_level, bit->second);
                }
            }
            if (inferred_level > 0) {
                rr.current_level = std::min<int>(
                    inferred_level,
                    static_cast<int>(res.levels.size()));
                snap.research_state_inferred = true;
            }
        }

        snap.research.push_back(std::move(rr));
    }
}

// ---------------------------------------------------------------------------
// Resolve buildings
// ---------------------------------------------------------------------------

static void resolve_buildings(FullAccountSnapshot& snap,
                               const PlayerData& pd,
                               const GameData& gd) {
    std::set<int64_t> seen;
    for (const auto& pb : pd.buildings) {
        ResolvedBuilding rb;
        rb.id = pb.building_id;
        rb.current_level = pb.level;
        rb.name = pb.name;
        seen.insert(pb.building_id);

        auto it = gd.buildings.find(pb.building_id);
        if (it != gd.buildings.end()) {
            const auto& bld = it->second;
            if (bld.name.empty()) {
                // keep player-resolved name
            } else {
                rb.name = bld.name;
            }
            rb.description = bld.description;

            // Next level info (current_level is 0-indexed in some cases,
            // so we look for the level entry matching current_level + 1)
            int next = pb.level;  // levels array is 0-indexed: index=level
            if (next >= 0 && next < static_cast<int>(bld.levels.size())) {
                const auto& nl = bld.levels[next];
                rb.next_build_time_seconds = nl.build_time_seconds;
                rb.next_level_costs = nl.costs;
                rb.next_level_requirements = nl.requirements;
            }
        }

        snap.buildings.push_back(std::move(rb));
    }

    for (const auto& [id, bld] : gd.buildings) {
        if (seen.count(id)) continue;
        ResolvedBuilding rb;
        rb.id = id;
        rb.current_level = -1;
        rb.name = bld.name.empty() ? "Building#" + std::to_string(id) : bld.name;
        rb.description = bld.description;
        snap.buildings.push_back(std::move(rb));
    }
}

// ---------------------------------------------------------------------------
// Resolve resources
// ---------------------------------------------------------------------------

static void resolve_resources(FullAccountSnapshot& snap,
                               const PlayerData& pd,
                               const GameData& gd) {
    std::map<int64_t, const PlayerResource*> player_map;
    for (const auto& pr : pd.resources) {
        player_map[pr.resource_id] = &pr;
    }
    snap.resource_state_partial =
        player_map.count(2325683920) == 0 ||  // Parsteel
        player_map.count(743985951) == 0 ||   // Tritanium
        player_map.count(2614028847) == 0;    // Dilithium

    for (const auto& [id, res] : gd.resources) {
        ResolvedResource rr;
        rr.id = id;
        rr.name = res.name.empty() ? "Resource#" + std::to_string(id) : res.name;

        auto pit = player_map.find(id);
        if (pit != player_map.end()) {
            rr.amount = pit->second->amount;
            if (!pit->second->name.empty() && pit->second->name.find("Resource#") != 0) {
                rr.name = pit->second->name;
            }
        }

        snap.resources.push_back(std::move(rr));
    }

    // Preserve any player resources unknown to the current game cache.
    for (const auto& pr : pd.resources) {
        if (gd.resources.find(pr.resource_id) != gd.resources.end()) continue;
        ResolvedResource rr;
        rr.id = pr.resource_id;
        rr.amount = pr.amount;
        rr.name = pr.name.empty() ? "Resource#" + std::to_string(pr.resource_id) : pr.name;
        snap.resources.push_back(std::move(rr));
    }
}

// ---------------------------------------------------------------------------
// Resolve buffs
// ---------------------------------------------------------------------------

static void resolve_buffs(FullAccountSnapshot& snap,
                           const PlayerData& pd) {
    auto now = std::chrono::system_clock::now();
    auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    for (const auto& pb : pd.buffs) {
        ResolvedBuff rb;
        rb.buff_id = pb.buff_id;
        rb.level = pb.level;
        rb.expiry_time = pb.expiry_time;
        rb.permanent = !pb.expiry_time.has_value();

        if (pb.expiry_time.has_value()) {
            rb.expired = pb.expiry_time.value() <= now_epoch;
        } else {
            rb.expired = false;
        }

        snap.buffs.push_back(std::move(rb));
    }
}

// ---------------------------------------------------------------------------
// Resolve jobs + compute idle slots
// ---------------------------------------------------------------------------

static void resolve_jobs(FullAccountSnapshot& snap,
                          const PlayerData& pd,
                          const GameData& gd) {
    auto now = std::chrono::system_clock::now();
    int64_t now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    int active_research = 0;
    int active_building = 0;

    for (const auto& pj : pd.jobs) {
        if (pj.completed) continue;  // skip completed jobs
        if (!has_live_unfinished_job(pd, pj, now_epoch)) continue;

        ResolvedJob rj;
        rj.uuid = pj.uuid;
        rj.job_type = pj.job_type;
        if (pj.research_id != 0) {
            rj.job_type_name = "Research";
        } else if (pj.building_id != 0) {
            rj.job_type_name = "Building";
        } else {
            rj.job_type_name = job_type_str(pj.job_type);
        }
        rj.level = pj.level;
        rj.start_time = pj.start_time;
        rj.duration = pj.duration;
        rj.reduction = pj.reduction;
        rj.completed = pj.completed;

        // Resolve target name
        if (pj.research_id != 0) {
            auto it = gd.researches.find(pj.research_id);
            if (it != gd.researches.end()) {
                rj.target_name = it->second.name;
            }
            active_research++;
        } else if (pj.building_id != 0) {
            auto it = gd.buildings.find(pj.building_id);
            if (it != gd.buildings.end()) {
                rj.target_name = it->second.name;
            }
            active_building++;
        }

        if (rj.target_name.empty()) {
            rj.target_name = rj.job_type_name + " (ID unknown)";
        }

        snap.jobs.push_back(std::move(rj));
    }

    snap.active_job_count = static_cast<int>(snap.jobs.size());

    // Idle slot estimation is only meaningful if the sync stream supplied
    // a current unfinished job set. Missing job payloads are unknown, not idle.
    if (snap.jobs.empty()) {
        snap.idle_research_slots = -1;
        snap.idle_building_slots = -1;
    } else {
        // Simplification: actual slot count depends on ops level and VIP.
        snap.idle_research_slots = std::max(0, 2 - active_research);
        snap.idle_building_slots = std::max(0, 2 - active_building);
    }
}

// ---------------------------------------------------------------------------
// Resolve tech
// ---------------------------------------------------------------------------

static void resolve_tech(FullAccountSnapshot& snap,
                          const PlayerData& pd) {
    for (const auto& pt : pd.techs) {
        ResolvedTech rt;
        rt.tech_id = pt.tech_id;
        rt.tier = pt.tier;
        rt.level = pt.level;
        rt.shard_count = pt.shard_count;
        snap.tech.push_back(std::move(rt));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

FullAccountSnapshot build_full_snapshot(const PlayerData& player_data,
                                        const GameData& game_data) {
    FullAccountSnapshot snap;

    // Metadata
    snap.snapshot_time = std::chrono::system_clock::now();
    snap.last_sync = player_data.last_sync;
    snap.player_name = player_data.player_name;
    snap.ops_level = player_data.ops_level;

    // Resolve all entity types
    resolve_officers(snap, player_data, game_data);
    resolve_ships(snap, player_data, game_data);
    resolve_research(snap, player_data, game_data);
    resolve_buildings(snap, player_data, game_data);
    resolve_resources(snap, player_data, game_data);
    resolve_buffs(snap, player_data);
    resolve_jobs(snap, player_data, game_data);
    resolve_tech(snap, player_data);

    // Emerald chain
    snap.emerald_chain_level = player_data.emerald_chain.level;

    // Pass-through data (no game data to resolve against)
    snap.inventory = player_data.inventory;
    snap.slots = player_data.slots;
    snap.missions = player_data.missions;

    // Resolve events
    {
        auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (const auto& pe : player_data.events) {
            ResolvedEvent re;
            re.config_id = pe.config_id;
            re.source = pe.source;
            re.event_type = pe.event_type;
            re.category = pe.category;
            re.category_name = event_category_str(pe.category);
            re.placement_type = pe.placement_type;
            re.group_name = pe.group_name;
            re.schedule = pe.schedule;
            re.ranking = pe.ranking;
            re.entry_data = pe.entry_data;
            re.metadata = pe.metadata;
            re.segments = pe.segments;

            // Compute state
            if (pe.schedule.start > 0 && pe.schedule.end > 0) {
                if (now_epoch < pe.schedule.start) {
                    re.state = EventState::Upcoming;
                } else if (now_epoch < pe.schedule.end) {
                    re.state = EventState::Active;
                } else {
                    re.state = EventState::Ended;
                }
                re.remaining_seconds = static_cast<int>(pe.schedule.end - now_epoch);
            }

            re.total_reward_tiers = pe.reward_tier_count();

            if (re.state == EventState::Active) snap.active_event_count++;
            if (pe.has_manual_claimable_reward(now_epoch)) snap.claimable_event_count++;

            snap.events.push_back(std::move(re));
        }

        // Sort: active first, then by end time (soonest ending first)
        std::sort(snap.events.begin(), snap.events.end(),
            [](const ResolvedEvent& a, const ResolvedEvent& b) {
                if (a.state != b.state) {
                    // Active < Upcoming < Ended
                    return static_cast<int>(a.state) < static_cast<int>(b.state);
                }
                return a.remaining_seconds < b.remaining_seconds;
            });
    }

    return snap;
}

} // namespace stfc
