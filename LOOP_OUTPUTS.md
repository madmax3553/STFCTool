# STFC Loop Outputs Ledger

Last reviewed: 2026-05-16
Account basis: Ops 66 live snapshot, action planner, ship roster, resource inventory, and current local game-data translations.

This file is the loop ontology for the avalanche plan. Its job is to answer:

- What does this loop output?
- What permanent system does that output improve?
- When is the loop worth active clicks?
- When is it only maintenance?

Use `ACTIVE_RANKING.md` to score these loops by output need, active effort, and concurrency.

## How To Use This

A loop is active only when its output feeds the current selected bottleneck. If the output is merely "more currency for later," the loop is maintenance unless today's event makes it unusually efficient.

Priority test:

1. Does the loop raise combat ceiling, reduce repair/click cost, unlock research, unlock a ship tier, or improve account-wide efficiency?
2. Is that output needed this week?
3. Can the loop be done without starving the main research/building/anchor-ship lane?
4. Does an event score the same activity without changing the plan?

If the answer to all four is yes, it can become the main active block. If not, it is a claim, passive, weekly, or skip.

## Current Account Signals

These are the live signals that matter for loop triage.

- GS-31 is L15/T3 with only 100 GS-31 Recon Data and 638 GS-31 Parts. Section 31 is underbuilt relative to older specialty loops.
- U.S.S. Dauntless is L20/T4 with Aggregation currencies on hand, so the Aggregation lane needs a target check.
- Serene Squall is L40/T8, and the action planner is actively recommending Transogen mining/refinery research.
- NSEA Protector is L70/T14 in the synced data. If it is now maxed in game, the Mirror loop becomes maintenance unless Omega Mirror Dust is feeding selected research.
- Mantis, Voyager, Franklin-A, Defiant, Talios, Monaveen, Borg Cube, Gorn Eviscerator, and SS Revenant are already mature enough that they need a named bottleneck before receiving active-click priority.
- Maverick/Warp Dive resources are present: Maverick Renown, Maverick Credits, Maverick Task Key, Maverick Periphery Intel, and Conqueror Borg solo directives. This is a real current progression loop, not just residue.
- DTI/Relativity resources are present: DTI Reputation, DTI credits, DTI Security Access Card, DTI Plaque, and Relativity Safety Bypass resources.
- Arena, Incursion, Alliance Tournament, Ferengi Exchange, Battle Pass, Flash Pass, and multiple seasonal currencies are present. These should be treated as event/opportunity loops, not default daily grinds.
- Sarris/Fatu-Krey resources are present, but official STFC notes said Sarris Invasions and Fatu-Krey content moved to exchange handling after December 2025. Treat this as exchange-only unless it reappears in live events.
- The current planner does not show FKR reputation as the primary blocker.

## Complete Loop Inventory

This is the account-wide inventory. "Loop" here means a repeatable or recurring activity that produces a currency, unlock, or permanent upgrade path. One-time event residue is called out separately so it does not masquerade as a daily priority.

Lifecycle status:

- Current: available as a normal ongoing loop.
- Passive: available, but should usually be claim/mining/background only.
- Scheduled: recurring, event, alliance, or calendar-driven.
- Legacy: older loop still present, but unlikely to be best active time without a named target.
- Archived / exchange-only: not a normal active loop; spend/exchange residue only unless Scopely reintroduces it.
- Verify: present in resources/data, but live availability must be checked in game before planning active time.

Ops/scaling notes are intentionally conservative. If exact thresholds are not confirmed, the table names the scaling driver rather than pretending precision.

| Family | Loop | Lifecycle | Ops gate / scaling modifier | Primary outputs | Permanent use | Current treatment |
| --- | --- | --- | --- | --- | --- | --- |
| Core | Claim/sync/daily goals | Current | Daily goals, events, and packs scale by Ops bracket/account state | Daily resources, speedups, fresh planner state | Keeps all decisions accurate | Always first |
| Core | Research queue | Current | Nodes have Ops/building/research prereqs; value scales by current bottleneck | Combat, economy, refinery, ship, and efficiency unlocks | Main compounding power | Always active if selected node passes bottleneck test |
| Core | Building queue | Current | Building levels are direct Ops prereqs and unlock gates | Ops prereqs, stations, refineries, buildings | Unlocks future power and claims | Active only for prereqs/efficiency |
| Core | Store/refinery claims | Current | Most claims scale by Ops, faction rank, building level, ship tier, or research | Parts, shards, currencies, prime particles, favors | Converts prior loop work | Claim; do not let it define active time |
| Core | Events/Battle Pass/Flash Pass | Scheduled | Event brackets and milestones scale by Ops/server bracket | Event currency, shards, particles, speedups | Multiplies already-planned work | Active only on overlap |
| Core | Away Teams | Current | Mission pools and rewards scale by unlocks, officers, traits, and level | Credits, officer shards, speedups, traits | Passive officer/economy sourcing | Passive |
| Core | Scrapping | Current | Output scales by ship grade/tier/level and scrapping research | Materials, parts, speedup conversion | Backfills materials and research needs | Active only if planned into event/research |
| Core | Officer sourcing | Current | Token pools and stores scale by unlocks, Ops, and event/store availability | Recruit tokens, transporter patterns, shards | Crew depth, below-deck strength | Target specific officers |
| Core | Fleet Commanders | Current | Claims/skill paths scale by commander level, shards, rare skill points | FC credits, skill points, shards | Global buffs and loop bonuses | Spend only toward selected skill path |
| Faction | FKR hostiles | Current | Daily targets and hostile access scale by Ops/warp; rep needs scale by faction rank | Federation/Klingon/Romulan rep, messages, dailies | Faction store, ships, credits, dailies | Active only for named unlock/store/event |
| Faction | FKR armadas | Current | Armada level rewards and store bundles scale by Ops, faction rank, and armada tier | Faction armada credits, faction store resources | Faction BPs, officers, materials | Active only if store output is selected |
| Faction | FKR faction store | Current | Store unlocks scale by faction rank and Ops | Credits, BPs, officers, rep-related bundles | Ship/officer sourcing | Claim/buy selected targets |
| Faction | Ex-Borg / Freebooter | Current | Favors/store unlocks scale by Ex-Borg rank and Ops | Ex-Borg rep, credits, favors, emblems | Ex-Borg store and efficiency favors | Active near selected favor/store target |
| Faction | Bajoran / Cardassian | Current | Store/refinery value scales by Ops, rep, and building/ship unlocks | Bajoran rep/credits/badges, Cardassian loot, Ketracel-style resources | Bajoran store, Defiant/Dominion support | Maintenance unless store target selected |
| Faction | Rogue / Outlaw | Legacy | Store and research access scale by Rogue rep/Ops | Rogue rep/credits, Eclipse output, Outlaw research credits | Rogue store, Stella, outlaw research | Maintenance unless target selected |
| Faction | Augment | Legacy | Store output scales mostly by Augment rep; low Ops-era loop | Data, Augment credits, Augment points, Khan sourcing | Augment store/officers | Legacy maintenance |
| Faction | Independent / archives | Verify | Archive/store systems vary by event/building unlock | Independent credits, archive schematics, special store currencies | Independent store/building support | Claim or target only |
| Faction | Maverick / Warp Dive | Current | Ops 55+; Warp Dive Bar level unlocks task keys and scales store rewards | Maverick Renown/Credits, Task Keys, Periphery Intel, store scaling | Maverick research/store, Warp Dive Bar, Conqueror Borg support | Real current loop; investigate after GS-31/Dauntless |
| Hostile | Swarm / Franklin | Legacy | Hostile/daily bracket scales by Ops; Franklin-A tier affects efficiency | Biominerals, advanced biotics | Franklin-A, swarm dailies | Maintenance |
| Hostile | Borg probes / Vi'Dar / Talios | Legacy | Probe level/refinery/store value scales by Ops, Talios tier, Borg research | Nanoprobes, Borg store resources | Talios, Borg store, Borg research | Maintenance unless nanoprobe target selected |
| Hostile | Borg Cube | Current | Borg Cube starts Ops 28+; cube output/power scales with cube tier and faction ships | Vinculum, technological distinctiveness, cube resources | Cube research/refinery | Target only |
| Hostile | Mantis / Actian | Legacy | Output scales by Mantis tier and refinery/research | Actian Venom, Actian Keys | Syndicate/officer sourcing | Maintenance with current stockpile |
| Hostile | Xindi / NX-01 | Current | Output scales by NX-01 tier, Xindi hostile level, research | Xindi Scraps, Bounty, Directives, NX resources | NX upgrades/research | Target only |
| Hostile | Voyager / Hirogen / Species 8472 | Current | Output scales by Voyager tier, DQ access, research, and artifact exchanges | Hirogen Relics, Deuterium, Biotoxins, Anomaly Samples | Voyager, Delta Quadrant, isolytic artifacts | Target only |
| Hostile | Silent / Temporal | Current | Hostile bands and Temporal Artifact access scale by Ops/unlocks | Temporal Disruptors, Temporal Wreckage, Temporal Artifacts | Temporal artifact power | Active only after artifact target |
| Hostile | Gorn / Eviscerator | Current | Ops 40+ ship; refinery and ship-part outputs scale by Eviscerator tier and Ops | Apex/Volatile Isomatter, Apex Recruits, Eviscerator parts | Eviscerator, ship parts, artifact | Target only |
| Hostile | Mirror / NSEA | Current | Ops 40+ NSEA; reach/rewards scale by NSEA tier, Mirror research, refinery | Trellium, Mirror Dust, Omega Mirror Dust, Beryllium | Mirror research, NSEA | Maintenance unless Mirror target |
| Hostile | Aggregation / Dauntless | Current | Ops 45+ Dauntless; daily strongbox and activations scale by Dauntless tier/research | Banners, Smuggled Studies, Recon Locus Schematics, Prototype Data | Aggregation research, Dauntless, tech | High investigation priority |
| Hostile | Serene Squall / Transogen | Current | Ops 30+ Squall; strongbox/refinery and loop efficiency scale by Squall tier and Ops grade | Raw/refined Transogen, Forge resources, Schematics | Transogen research, Squall, loop efficiencies | Current planner-backed lane |
| Hostile | Monaveen / Texas-class | Current | Ops 40+ ship; refinery/economy value scales by Monaveen tier/research | Monaveen parts, Texas hostile output, economy rewards | Monaveen economy research | Maintenance unless economy node selected |
| Hostile | Q Continuum / Revenant | Current | Output scales by Revenant tier, Chaos Tech level, Q hostile access | Chaos Modules, Chaosmatter, Revenant parts | Chaos Tech, Revenant | Maintenance unless tech target |
| Hostile | V'Ger / First Contact | Current / scheduled | Ops/event gated; also appears as Maverick rotating task objective | V'Ger Amity/particles, First Contact resources, task scoring | First Contact progression, Maverick tasks | Event/task target only |
| Hostile | Augment Exiles | Current / scheduled | Ops/event gated; appears as Maverick rotating task objective | Augment Exile hostile output, task scoring | Aggregation/Maverick related progression | Event/task target only |
| Hostile | Fatu-Krey / Sarris | Archived / exchange-only | Former event/invasion loop; exchange handling after December 2025 per official close-out | Fatu-Krey Scraps/Eye Patches, Sarris Dominion Salvage, Galactic Acclaim | Exchange/officer/event rewards | Exchange-only unless live event returns |
| Hostile | Doomsday | Legacy / scheduled | Old event/directive loop; availability must be verified in current event calendar | Doomsday directives/transwarp cells | Legacy event/refinery rewards | Event-only |
| Hostile | Invading Entities / Galaxy Quest | Scheduled | Event/weekly loop; officer value scales by Nesmith rank and current content | VHS, Sarris salvage, Nesmith shards | Nesmith and Galaxy Quest rewards | Weekly/opportunistic |
| Armada | Generic armadas | Current | Reward bundles scale by armada level, player Ops, and contribution | UC/R/E armada credits, tactical cores | Alliance store/materials/officers | Active only for selected store output |
| Armada | Formation Armadas | Current | Artifact pool and rewards scale by formation level/Ops; Tuvok boosts tokens | Artifact tokens/shards, premium tokens | Artifact Gallery | Active if named artifact target |
| Armada | Eclipse armadas | Legacy | Output scales by armada level, Stella tier, Rogue research/rep | Eclipse credits/codes, Rogue output, Stella particles | Rogue/Stella progression | Maintenance |
| Armada | Dominion Solo Armadas | Current | Rewards scale by armada rarity/level, Defiant support, research, Ops | Dominion credits, edicts, Kemocite, Tactical Mandates | Defiant/Dominion store | Target only |
| Armada | Borg Solo Armadas | Current | Rewards scale by rarity/level, cube/Talios/Borg research, Ops | Borg solo credits/directives, cortical subunits | Borg Cube/research/store | Target only |
| Armada | Conqueror Borg Solo Armadas | Current | Ops 55+ via Warp Dive; requires triangle fleet; rewards via Maverick tasks/store | Maverick task progress, high-end Borg solo rewards | Maverick/Warp Dive Bar | Target if fleet clears efficiently |
| Armada | G6 / Galactic Solo Armadas | Current | Ops 61+ era; outputs scale by G6 bracket, directive rarity, clear level | Galactic solo directives/credits, G6 solo credits | G6 materials/economy | Target only |
| Armada | Borg Mega / Expansion Cube | Verify | Directive loop present in inventory; live source/timing must be checked | Borg Mega/Expansion directives, cube-related rewards | Borg high-end loop | Target only after live verification |
| Armada | Vindicator armada support | Current | Ops 40+; upgrades past T6 require Ops 51+, past T12 require Ops 61+ | Armada survivability, Vindicator refinery value | Higher armada ceiling | Target only if armadas are blocker |
| Wave | Wave Defense | Current / scheduled | System brackets and rewards scale by Ops band; old S31/Borg ranges exist | Section 31/Borg defense rewards, favors | Wave favors and progression | Organized/event only |
| Wave | Solo Wave Defense | Current | Rewards scale by wave tier, Ops bracket, S31 favors, Apex Shred | Section 31 currencies, Apex Shred favors | S31 favors and Apex performance | Target only |
| Wave | Elite Solo Wave Defense | Current | Rewards scale by wave cleared; exclusive GS-31 Recon Data | GS-31 Recon Data, S31 Credits, S31 Mirror Credits, Access Keys | GS-31 upgrades/refinery | Highest investigation priority |
| Outpost | Solo Outposts | Current / scheduled | Ops 51+; rewards scale by outpost type/level and event cycle | Outpost credits, components, control cores, medals | Outpost refinery/buffs | Event/current-target only |
| Outpost | Retaliation/outpost defense | Current / scheduled | Scales by outpost/event level and temporary buff cycle | Retaliation plunder, outpost medals, temporary buffs | Outpost loop and event scoring | Event/current-target only |
| Mining | Standard material mining | Passive | Node grade and refinery payouts scale by Ops/refinery/research | Raw/refined ore, gas, crystal | Ships/buildings/research | Passive unless event/immediate bottleneck |
| Mining | 6-star mining | Passive | Ops 61+ era; refinery and usage scale by Ops 61-70 brackets | G6 materials, Sigma era support | Ops 66+ growth | Passive; active only on event/bottleneck |
| Mining | Sigma resources | Passive | G6/G7 era; use scales by Ops and building/research costs | Sigma parsteel/tritanium/dilithium | G6/G7 building/research | Passive |
| Mining | Latinum / D'Vor / Feesha | Passive | Output scales by D'Vor/Feesha tier, research, node type | Raw/concentrated Latinum | Speedup/economy flexibility | Passive |
| Mining | Isogen / Territory / Meridian | Passive / scheduled | Output scales by node, territory service, Meridian tier/research | Raw/refined Isogen, territory currencies | Territory and research | Passive/alliance need |
| Mining | Data / Botany Bay | Legacy / passive | Low Ops-era loop; store output scales by Augment rep | Corrupted/decoded/encrypted data, Augment store inputs | Augment store | Legacy/passive |
| Mining | Trellium | Passive | Mirror node type and refinery output scale by NSEA/Mirror research/Ops | Raw/Liquid Trellium A/D/Omega | Mirror/NSEA research | Passive if Mirror target |
| Mining | Transogen | Passive / current | Output scales by Squall tier/research and Transogen grade | Raw/refined Transogen | Squall/research | Passive or active if research bottleneck |
| Economy | Territory Capture | Scheduled | Alliance territory services scale by territory/building/service level | Isogen, territory particles/currencies, alliance value | Territory research/alliance buffs | Alliance schedule only |
| Economy | Amalgam / raiding | Current / optional | Output scales by Amalgam tier, target cargo, research | Plundered Cargo, Amalgam parts/tokens | Raiding economy | Optional, not catch-up default |
| Economy | Discovery / Mycelium | Passive | Utility scales by Discovery tier, research, cultivated mycelium | Spores, cultivated mycelium, refinery tokens | Summon/jump mobility | Claim/use when it saves time |
| Economy | Shipyard / Automated Shipyards | Verify / scheduled | Directive/store loop; rewards likely Ops/shipyard bracketed | Shipyard directives/credits | Shipyard store/officers/materials | Event/target only |
| Economy | Ferengi Exchange | Scheduled | Monthly/event store; value scales by available exchange table, not active power | Monthly exchange tokens, opportunity chips, pass points | Exchange store/rewards | Claim/exchange, not active grind |
| Economy | Arena | Scheduled | Brackets/rewards scale by Ops/power/event schedule | Arena emblems, repair tokens | Arena store/rewards | Event/scheduled only |
| Economy | Incursions | Scheduled | Server bracket/PvP event; rewards scale by event tier and participation | Incursion coins/conduits/trophies | Incursion store/events | Scheduled PvP only |
| Economy | Alliance Tournament | Scheduled | Alliance/event bracket scaling | Tournament credits/refresh/qualifier credits | Alliance tournament store/events | Scheduled only |
| Economy | Officer Depot / Flash Pass | Scheduled | Pass/event table scaling; not a repeatable grind outside event | Officer depot tokens/pass points | Officer sourcing | Event/pass only |
| Economy | Dabo / holiday / anniversary stores | Archived / exchange-only | Time-limited stores; value ends when store closes | Limited tokens and store currencies | Short-term event rewards | Spend/exchange before expiry |
| Building | Armory | Current | Ops 25+; building level scales fleet hull/shield/mitigation and refit unlocks | Hull/shield/mitigation buffs, active refits | Fleet-wide combat power | Upgrade when materials and events align |
| Building | Artifact Gallery | Current | Artifact availability scales by Ops; levels scale permanent buffs | Artifact unlocks/levels | Permanent account power | Target named artifacts |
| Building | DTI Headquarters / Relativity | Current | DTI/building/reputation and Relativity tier scale outputs | DTI credits/reputation, Relativity bypasses | Temporal/Relativity progression | Target if Relativity/DTI node selected |
| Building | Warp Dive Bar | Current | Ops 55+; level 20/30/40 unlock more Maverick solo task keys; store scaling improves with level | Maverick task keys, store scaling, Parsteel/Sigma efficiency | Maverick loop and Conqueror Borg power | Investigate because current loop is active-era |
| Building | Nova Squadron | Current | Building/research value scales by Nova Squadron level and Starfleet Honors efficiency | Starfleet Honors, Nova particles | Building/research efficiency and buffs | Target if planner selects node |
| Building | Archive / Independent Archives | Verify | Building/store availability and output must be checked in game | Archive schematics/data | Archive-related buffs/rewards | Target only |
| Building | Cloaking/refits | Current | Access scales by ship/refit unlock, cloak research, store availability | Cloaking shards, refit shards, cloak research | PvP/utility/refit actives | Target only |
| Tech | Forbidden Tech | Current | Upgrade/tier costs and power scale by tech rarity/level and ship use | Fragments, rods, reactors, catalysts, protomatter | Ship equipment power | Upgrade selected tech only |
| Tech | Chaos Tech | Current | Upgrade/tier costs and power scale by tech rarity/level and loop match | Chaos Modules, Chaosmatter, fragments, rods, catalysts | Combat states/loop bonuses | Upgrade selected tech only |
| Tech | Prototype Tech | Current | Aggregation-sourced; value scales by device level/rarity and ship role | Prototype protomatter/catalysts/materials | Newer equipment/progression | Target via Aggregation/Dauntless |
| Legacy/Event | Campaigns and passes | Archived / scheduled | Current while event is live; archived residue after pass ends | Battle pass, flash pass, campaign merits, event tickets | Time-limited rewards | Event overlap only |
| Legacy/Event | Seasonal/event residue | Archived / exchange-only | No active loop after store/event closes | Store tokens, trophies, relics, holiday currencies | Exchange if store exists | Do not treat as a loop |

## Archive Policy For The Planner

The planner should not recommend active clicks for archived or exchange-only loops.

Archived / exchange-only means:

- The live activity is no longer a normal recurring loop.
- The account only has leftover currency, exchange bundles, or event residue.
- The only valid action is claim/exchange/spend residue if the store is available.

Known archive/exchange calls from this snapshot:

| Loop / currency family | Evidence | Planner action |
| --- | --- | --- |
| Sarris Invasions / Fatu-Krey | Official close-out says Sarris and Fatu-Krey resources move through exchange handling after December 2025 | Mark archived/exchange-only unless it returns in live events |
| Expired battle passes / flash passes | Inventory contains old pass unlocks and pass points | Treat as residue; never active-click priority |
| Holiday / anniversary / seasonal stores | Inventory contains many named limited-store tokens | Exchange if store exists; otherwise ignore |
| Old campaign/event trophies | Inventory contains trophies, campaign merits, and one-off event currencies | Ignore unless a live exchange exists |

Legacy is different from archived:

- Legacy loops may still exist and pay out normally.
- They are low ROI only because the account has outgrown them or the relevant ship is mature.
- They can become active again if a specific store purchase, officer, research, event, or daily target makes the output useful.

## Core Account Loops

| Loop | Main clicks | Outputs | Permanent use | Active when | Maintenance when |
| --- | --- | --- | --- | --- | --- |
| Claim/sync/daily reset | Claim jobs, dailies, chests, stores, then re-sync | Daily resources, speedups, currencies, job completion, fresh planner data | Keeps every later choice accurate | Always first | Never skip unless no login |
| Research queue | Start selected research | Might, combat stats, cost efficiency, refinery bonuses, loop unlocks | Main compounding account power | Always if research passes bottleneck test | Idle only if saving resources for a higher node |
| Building queue | Start selected building | Ops prereqs, station power, refinery/building unlocks | Unlocks future research, ships, refinery, drydock, buildings | When building is a prereq or efficiency node | Skip cosmetic/power-only buildings during catch-up |
| Event stacking | Match planned actions to event scoring | Event currency, officer shards, particles, speedups, materials | Turns necessary work into extra rewards | When event overlaps research, ship, loop, armada, or dailies | Skip if it demands unrelated spending |
| Store/refinery claims | Claim daily/weekly/refinery bundles | Loop currencies, parts, officers, materials, primes, artifacts | Converts prior work into progress | Always claim high-value free/earned outputs | Do not let claim availability decide active clicks |
| Daily goals | Clear required hostile, mining, faction, help, and spend goals | Daily rewards, loyalty, faction standing, officer XP, resources | Baseline account income | Do efficiently after main target is known | Do not over-farm beyond completion unless it overlaps |
| Away missions | Assign officers, claim results | Officer shards, traits, speedups, faction/reputation resources | Passive sourcing | When a useful reward or trait mission is available | Low attention if rewards are generic |
| Alliance participation | Helps, donations, territory/alliance tasks | Alliance credits, helps, event contribution, territory value | Speeds queues and unlocks alliance rewards | When it takes seconds or scores useful events | Avoid turning donations into random spend |

## Combat Hostile Loops

| Loop | Main clicks | Outputs | Permanent use | Active when | Maintenance when |
| --- | --- | --- | --- | --- | --- |
| FKR hostiles | Kill Federation/Klingon/Romulan hostiles | FKR reputation, faction messages, faction dailies, ship XP, officer XP | Faction store access, faction credits, faction ship sourcing | A named faction tier, store claim, daily, faction credit need, or event is selected | Balancing reputation for its own sake |
| Ex-Borg / Freebooter | Kill Freebooter-style hostiles and claim Ex-Borg loop | Ex-Borg reputation/credits, faction tokens, research/refinery support | Ex-Borg store, favors, efficiency, specialty sourcing | A specific Ex-Borg favor or store claim is selected | Claims only if no selected target |
| Augment / Botany Bay | Mine data, hit Augment hostiles, claim Augment store | Data, Augment credits, Khan shards, Augment store currency | Older officer and store sourcing | Only if Khan/Augment store output is still targeted | Legacy maintenance at Ops 66 |
| Swarm / Franklin | Kill swarm, claim Franklin loop | Swarm biominerals, advanced biotics, Franklin/Franklin-A progression | Daily goals, Franklin-A utility, older sourcing | Event/daily overlap or if Franklin-A still gates content | Likely maintenance with Franklin-A L60/T12 |
| Borg probes / Vi'Dar / Talios | Kill Borg probes, refine nanoprobes | Inert/active/charged nanoprobes, Talios parts, Borg store output | Talios progression, Borg store, Borg research | If Talios, Borg store, or nanoprobes block selected research | Likely maintenance with Talios L60/T12 and large nanoprobe stocks |
| Mantis / Actian | Kill Actians, refine Actian Venom/keys | Actian Venom, Actian Keys, Mantis refinery, Syndicate/officer sourcing | Syndicate XP, SNW officer shards, Mantis progression | If Syndicate/officer output is a current bottleneck or event scores it | Mature maintenance with Mantis L60/T12 and 33.8M Actian Venom |
| Xindi / NX-01 | Kill Xindi hostiles, use NX loop | Xindi Scraps, Xindi Directives, Xindi Bounty, NX materials | NX-01 progression, Xindi research/refinery | If NX tier/research unlocks current hostile reach or event scoring | Maintenance if NX is not the selected anchor |
| Voyager / Hirogen / Species 8472 | Kill Hirogen, use Voyager abilities, refine Delta outputs | Hirogen Relics, Deuterium, Exotic Biotoxins, Anomaly Samples, Voyager parts, Isolytic Artifact sourcing | Voyager upgrades, Delta Quadrant reach, isolytic artifacts | If an isolytic artifact or Voyager tier is selected | Maintenance with Voyager L60/T12 unless artifact/refinery target is named |
| Silent Hostiles / Temporal | Kill Silent Hostiles, spend Temporal Disruptors | Temporal Disruptor Parts, Temporal Wreckage, Temporal Remnants, Temporal Artifact shards | Temporal Artifacts: isolytic damage, building efficiency, apex barrier, research/ship efficiencies | A specific Temporal Artifact is selected and useful now | Generic hostile farming with no artifact target |
| Gorn / Eviscerator | Kill Gorn Hunter hostiles with Eviscerator | Apex Isomatter, Eviscerator parts, Apex Recruits, Apex ship parts, Cephalocasque artifact shards | Anti-Gorn reach, ship parts, PvP isolytic defense artifact, alliance Spatial Rip utility | If Gorn loop access, Eviscerator tier, ship parts, or Cephalocasque is targeted | Maintenance if Gorn access is solved and parts are not the bottleneck |
| Mirror / NSEA | Mine/kill in Mirror Universe with NSEA | Raw Omega-Trellium, Liquid Omega-Trellium, Omega Mirror Dust, Mirror Dust, Beryllium Spheres, Omega-13 support | Mirror Research, NSEA tiering, prime particles, NSEA refits | Omega Mirror Dust or Beryllium Spheres feed selected research/tier | Maintenance if NSEA is maxed and no Mirror Research target is selected |
| Aggregation / Dauntless | Kill Aggregation hostiles, use Seek and Destroy | Aggregation Banners, Smuggled Studies, Dauntless Prototype Data, Dauntless parts, System Scans, Prototype Tech materials, Forbidden/Chaos Tech materials | Reduces hostile click burden, improves hostile farming, Aggregation research, tech progression | If Aggregation research/refinery outputs are bottlenecks or hostile cap/click burden is the real problem | Maintenance claims if outputs are stockpiled |
| Serene Squall / Transogen | Mine Transogen and fight Suliban chasers | Raw/refined Transogen, Forge Dust/Foundation/Manipulator, Black Market Schematics, Squall parts, artifact choice tokens | Transogen research, Squall upgrades, loop efficiency research, artifact choice sourcing | Current planner says Transogen research is useful; active when Black Market Schematics/refined Transogen block nodes | Passive mining/claims if no research target remains |
| Monaveen / Texas-class | Kill Texas-class hostiles and refine Monaveen loop | Monaveen parts, Monaveen refinery currencies, economy/material payouts | Fleet-wide economy research and material support | If Monaveen research/refinery output is selected | Maintenance with Monaveen L60/T12 unless a specific economy node is picked |
| Q Continuum / SS Revenant | Kill Q Continuum hostiles with Revenant lane | Chaos Modules, Chaosmatter, Revenant parts, Chaos Tech support | Chaos Tech upgrades, Revenant progression, Q-loop combat reach | If a Chaos Tech or Revenant tier directly improves current content | Maintenance with SS Revenant L75/T15 unless Chaos Tech is selected |
| Galaxy Quest / Nesmith | Do Galaxy Quest hostile/event/refinery loop | Galaxy Quest VHS, Galaxy Quest Elite BP, Nesmith shards, Sarris Dominion Salvage | Nesmith progression, Apex Barrier / Invading Entity utility | If Nesmith is required for current Apex/Invading Entity crew or event rewards are exceptional | Do not max Nesmith just because the weekly exists |

## Armada, Wave, And Outpost Loops

| Loop | Main clicks | Outputs | Permanent use | Active when | Maintenance when |
| --- | --- | --- | --- | --- | --- |
| Generic alliance armadas | Spend UC/R/E armada directives | Armada credits, tactical cores, alliance store currency, ship BPs/officer sourcing | Alliance store, materials, ship/officer sourcing | Directives and repair cost are reasonable and store output is selected | Random armadas that do not feed a target |
| Formation Armadas | Run multi-node Formation Armadas | Artifact Tokens, Premium Artifact Tokens, artifact shards, Isolytic Artifact Tokens | Artifact Gallery permanent account power | A specific artifact improves current bottleneck; Tuvok boosts token yield | Token stockpile with no artifact target |
| Eclipse / Stella | Run Eclipse hostiles/armadas with Stella | Eclipse Security Codes, Rogue Credits, Stella particles, Rogue reputation | Rogue store, Stella efficiency, older officer/material sourcing | If Rogue/Stella output is still a blocker or event overlaps | Maintenance with Stella L45/T9 unless Rogue target is named |
| Dominion Solo Armadas / Defiant | Spend Dominion solo directives | Dominion Solo Armada Credits, Edicts, Kemocite, Tactical Mandates, Defiant support | Defiant loop, Dominion store, armada efficiency, solo armada rewards | If Dominion solo credit output or Defiant support improves current armada ceiling | Maintenance if directives/credits are stockpiled and no store target exists |
| Borg Solo Armadas / Cube | Spend Borg solo directives | Borg Solo Armada Credits, Borg Type 03 credits, Borg Polygon credits, cortical subunits, Borg tech resources | Borg Cube, Borg research, Borg store, solo armada progression | If Borg Cube/research/refinery or solo-armada ceiling is selected | Maintenance if Cube is mature and credits are not being spent |
| Conqueror Borg Solo Armadas / Maverick | Spend Conqueror Borg directives, use required triangle fleet | Maverick Credits/Renown via tasks, Borg/solo rewards, Warp Dive Bar progression | Maverick faction/store, Warp Dive Bar, high-end solo armada power | If current fleet can clear efficiently and Maverick/Warp Dive output is selected | Skip if repairs are high or research/building lane gets starved |
| G6 / Galactic Solo Armadas | Spend Galactic Solo directives | Galactic solo credits, G6 solo credits, armada materials | G6 economy and ship/resource progression | If rewards feed current G6 material or research bottleneck | Maintenance if directives are scarce and output is unfocused |
| Solo Wave Defense | Run three-ship wave defense | Section 31 currencies, Victory-style rewards, Apex Shred favors | Section 31 favors, wave performance, Apex mechanics | If Apex Shred/favors improve the next wave or event scores it | Skip if wave clear is inefficient |
| Elite Solo Wave Defense | Run Elite Solo Wave Defense with 3 ships | Section 31 Access Keys, Section 31 Credits, Section 31 Mirror Credits, GS-31 Recon Data | GS-31 refinery, GS-31 upgrades, Apex Raider performance | High-priority investigation because GS-31 is underbuilt | Skip if current wave rewards do not justify repairs/clicks |
| Solo Outposts | Spend Outpost Strike Directives and hold/defeat outposts | Outpost Credits, Outpost Components, Control Cores, temporary buffs | Outpost refinery, temporary fleet buffs, event/task progression | If rewards feed current outpost/refinery target or alliance event | Event-only if temporary buffs do not matter |
| Wave Defense / Alliance Defense | Defend objective with alliance or solo structure | Section 31/Borg-style defense rewards, favors, event points | Favors, directives, team progression | If alliance is organized and rewards feed current lane | Skip chaotic runs with poor return |
| Vindicator armada support | Use Vindicator in armadas | Better armada survivability/taunt, Vindicator parts/refinery output | Higher armada ceiling and cleaner group clears | If armadas are the selected bottleneck and Vindicator tier changes survival | Maintenance with Vindicator L40/T8 unless armada ceiling is blocked |

## Mining, Economy, And Passive Loops

| Loop | Main clicks | Outputs | Permanent use | Active when | Maintenance when |
| --- | --- | --- | --- | --- | --- |
| 6-star material mining | Mine raw G6 crystal/gas/ore | Raw/refined 6-star materials | Ops 66 ships, buildings, research | Passive before logout; active only for an event or immediate bottleneck | Never babysit if active hostile/armada loop is higher ROI |
| Sigma resource mining | Mine Sigma parsteel/tritanium/dilithium | Raw Sigma resources | G6/G7 era construction/research | Passive when queues need Sigma | Maintenance if stockpiled |
| Latinum / D'Vor / Feesha | Mine raw/concentrated latinum | Latinum, concentrated latinum, D'Vor/Feesha output | Speedups, store flexibility, economy | Passive long-mining or event overlap | Do not spend main active block babysitting |
| Isogen / Meridian / Territory | Mine isogen and claim territory outputs | Raw Isogen, refined isogen, territory particles/currencies | Territory research, alliance territory, Meridian support | Alliance territory need or research target | Passive only if no urgent target |
| Discovery / Mycelium | Claim/refine mycelium and use Discovery | Mycelium Spores, Cultivated Mycelium, Discovery tokens | Summoning, jump mobility, Discovery utility/research | If mobility saves meaningful time or event requires it | Maintenance claims |
| Amalgam / raiding | Raid bases and claim Amalgam refinery | Plundered Cargo, Amalgam parts/refinery tokens | Economy, plunder conversion, PvP/raiding support | Only if raiding is planned and protected time exists | Skip during catch-up if it distracts from main lane |
| Trellium mining | Mine Mirror Trellium | Raw Trellium-A/D/Omega, Liquid Trellium, Mirror Dust | Mirror research and NSEA upgrades | If Mirror Research/NSEA bottleneck exists | Passive/maintenance otherwise |
| Transogen mining | Mine raw Transogen with Squall | Raw/refined Transogen, Black Market-related progression | Squall research/refinery and loop efficiency | Current planner supports this if active research target remains | Passive mining if Schematics/refined output is not blocking |

## Specialty Ship And Support Loops

| Ship / system | Output | Permanent use | Current account read |
| --- | --- | --- | --- |
| GS-31 | GS-31 Recon Data, GS-31 Parts, recycle tokens, Section 31 conversion | Apex Raider and Elite Solo Wave Defense progression | Underbuilt at L15/T3; investigate first |
| U.S.S. Dauntless | Prototype Data, System Scans, Seek and Destroy activations, Aggregation refinery claims | Reduces hostile click burden and improves Aggregation progression | L20/T4; possible high-ROI catch-up lane |
| Serene Squall | Transogen, Squall parts, Black Market Schematics, artifact choice tokens | Transogen research, loop efficiency, artifact sourcing | L40/T8; planner has direct research signal |
| NSEA Protector | Omega-Trellium, Omega Mirror Dust, Beryllium Spheres, Omega-13 | Mirror research, NSEA progression, Mirror reach | L70/T14 in sync; likely maintenance if now maxed |
| Gorn Eviscerator | Apex Isomatter, Eviscerator parts, Apex Recruits, ship parts, Spatial Rip | Anti-Gorn loop, ship parts, alliance mobility | L65/T13; active only if Gorn/Eviscerator output selected |
| SS Revenant | Revenant parts, Q Continuum/Chaos loop output | Q hostile reach, Chaos Tech lane | L75/T15; likely mature |
| Borg Cube | Borg solo outputs, Cube research/parts, cutting beam utility | Solo armadas, Borg research, combat utility | L65/T13; active if Borg/Cube research target exists |
| Defiant | Dominion solo outputs, armada support buffs | Solo armada and Dominion progression | L60/T12; maintenance unless solo armada ceiling is selected |
| Monaveen | Texas hostile output, Monaveen parts, economy research | Fleet-wide economy/material advantages | L60/T12; maintenance unless economy node target exists |
| Mantis | Actian Venom, Syndicate/officer loop | Syndicate XP and SNW officer sourcing | L60/T12; stockpiled, likely maintenance |
| Voyager | Hirogen/Species 8472 output, Voyager parts, Isolytic Artifact lane | Delta Quadrant and Isolytic Artifacts | L60/T12; active only for named artifact/refinery target |
| Talios / Vi'Dar | Nanoprobes and Borg store output | Borg store/research/Talios progression | Talios L60/T12; maintenance unless nanoprobe research target exists |
| Franklin-A | Swarm biominerals/advanced biotics | Swarm dailies and legacy sourcing | L60/T12; maintenance |
| Stella | Eclipse codes, Rogue credits, Stella particles | Rogue store and Eclipse armadas | L45/T9; maintenance unless Rogue target exists |
| Cerritos | Cerritos support ability/refinery outputs | Armada/hostile support and specialty research | L60/T12; use for support, not main grind |
| Titan / Titan-A | Support buffs, Titan-A parts | Fleet support and specialty research | L25/T5; upgrade only if support breakpoint is named |
| Vindicator | Armada taunt/survivability, Vindicator parts | Armada ceiling | L40/T8; active only if armadas are main blocker |
| Relativity | Safety/temporal support resources | High-end utility/research | L60/T13; check only if a Relativity node is selected |
| Amalgam | Plundered Cargo and raiding economy | PvP economy and conversion | L45/T9; optional, not catch-up default |
| D'Vor Feesha | Concentrated Latinum | Speedup/economy flexibility | L60/T12; passive |
| Meridian | Isogen and territory economy | Territory/research support | L45/T9; passive/alliance need |
| Selkie | G6 mining and survey utility | G6 material flow | L45/T9 and L15/T3; passive mining role |

## Permanent Progression Systems Fed By Loops

| System | Fed by | Outputs / value | Active-click rule |
| --- | --- | --- | --- |
| Artifact Gallery | Formation Armadas, Voyager, Serene Squall, special events | Permanent fleet/account buffs, cost efficiency, isolytic/apex stats | High priority when a named artifact improves the current bottleneck |
| Temporal Artifacts | Silent Hostiles, Temporal Disruptors, store/event unlocks | Isolytic damage, building cost efficiency, apex barrier, specialty efficiency | Active only after choosing the artifact target |
| Forbidden Tech | Aggregation, events, refinery, older loops | Ship equipment buffs, hostile/armada/PvP specialization | Upgrade when it improves selected content, not random might |
| Chaos Tech | Q/Revenant, Aggregation, events | Combat states, loot boosts, loop-specific boosts | Active if a tech improves current hostile/armada loop |
| Fleet Commanders | Daily/prime/event claims, commander chests | Skill points, shards, global buffs | Spend only toward selected skill path |
| Syndicate | Mantis, events, daily claims | Syndicate XP/level, passive account buffs | Claim steadily; active grind only if near meaningful level |
| Officers | Recruit tokens, event stores, loop stores | Crew quality, below-deck power, loot multipliers | Target officers that unlock a current loop/crew, not collection filling |
| Refits | Stores, event currency, loop exchanges | Active abilities, loop improvements, support buffs | Buy only if used in selected active loop |
| Faction stores | FKR, Ex-Borg, Rogue, Section 31, Maverick, Dominion/Borg stores | Ship BPs, officer shards, credits, favors, parts, primes | Grind the source only when a store purchase is selected |

## Current Priority Interpretation

The strongest investigation order right now is:

1. Elite Solo Wave Defense / GS-31
   - Underbuilt ship, exclusive GS-31 Recon Data source, direct Section 31 permanent outputs.
2. Dauntless / Aggregation
   - Could reduce hostile click burden and improve daily sourcing if Aggregation outputs are being spent.
3. Serene Squall / Transogen
   - Planner already recommends Transogen research; this has a current data-backed signal.
4. Formation Armadas / Artifacts
   - Permanent account power, and Tuvok is max rank for artifact token boosts.
5. Silent Hostiles / Temporal Artifacts
   - High only after selecting the Temporal Artifact that improves the current bottleneck.
6. NSEA / Mirror
   - Maintenance unless Omega Mirror Dust/Beryllium/Mirror Research is the selected bottleneck.
7. FKR reputation
   - Active only for a named faction unlock, faction credit need, store purchase, daily, or event.
8. Nesmith / Galaxy Quest weekly
   - Opportunistic unless Nesmith is required for a current Apex Barrier or Invading Entity crew.

## Data Gaps To Close Next

The planner needs these fields before it can rank loops automatically:

- Artifact levels and next shard costs.
- Current refinery claim options and cooldowns.
- Current event objective names, scoring, and reward tiers.
- Current highest efficient hostile and armada clear for each major loop.
- Repair cost/time per loop.
- Current store purchase targets by faction/loop.
- Ship next-tier costs and the content breakpoint unlocked by each tier.
- Loop lifecycle status from live event/store availability: current, passive, scheduled, legacy, archived, or verify.
- Ops bracket and scaling source for each reward bundle: Ops level, ship tier, faction rank, building level, event bracket, or contribution tier.

## Source Anchors

Local sources:

- `data/player_data/player_data.json`
- `data/player_data/action_plan.json`
- `data/game_data/translations_resources.json`
- `data/game_data/translations_ships.json`
- `data/game_data/translations_officers.json`

Official STFC references checked:

- Formation Armadas and Artifact loot: https://startrekfleetcommand.com/news/formation-armada-loot-and-new-primes/
- Silent/Temporal Artifacts: https://startrekfleetcommand.com/news/additions-to-the-artifact-gallery/
- Elite Solo Wave Defense and GS-31 Recon Data: https://startrekfleetcommand.com/news/battle-tactics-elite-solo-wave-defense/
- GS-31 refinery: https://startrekfleetcommand.com/news/gs-31-ship-parts-refinery-the-best-source-of-ship-parts/
- Dauntless and Aggregation refinery: https://startrekfleetcommand.com/news/the-u-s-s-dauntless-revolutionizing-the-hostile-hunt/
- NSEA Protector and Mirror outputs: https://startrekfleetcommand.com/news/nsea-protector-new-ship-in-star-trek-fleet-command/
- Serene Squall and Transogen outputs: https://startrekfleetcommand.com/news/serene-squall-buffs-research-refinery/
- Voyager loop: https://startrekfleetcommand.com/news/update-55-voyager-part-2/
- Gorn Eviscerator loop: https://startrekfleetcommand.com/news/new-ship-the-gorn-eviscerator/
- Mantis loop: https://startrekfleetcommand.com/news/update-45-patch-notes/
- Monaveen loop: https://startrekfleetcommand.com/news/update-58-lower-decks-ii-part-1/
- Solo Outposts: https://startrekfleetcommand.com/news/seize-and-defend-new-solo-outposts/
- Conqueror Borg Solo Armadas: https://startrekfleetcommand.com/news/update-88-exploring-conqueror-borg-solo-armadas/
- Maverick Faction: https://startrekfleetcommand.com/news/update-88-first-look-the-maverick-faction/
- Warp Dive Bar: https://startrekfleetcommand.com/news/update-88-spotlight-the-warp-dive-bar/
- DTI Headquarters: https://startrekfleetcommand.com/news/the-department-of-temporal-investigations/
- Armory: https://startrekfleetcommand.com/news/introducing-the-armory/
- Infinite Incursions: https://startrekfleetcommand.com/news/introducing-infinite-incursions/
- Fatu-Krey / Sarris exchange status: https://startrekfleetcommand.com/news/sarris-invasions-closing-out-and-looking-ahead/
