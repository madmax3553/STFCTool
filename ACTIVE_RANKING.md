# STFC Active Ranking Model

Last reviewed: 2026-05-16
Account basis: Ops 66 live snapshot, action planner, loop ledger, ship roster, and resource inventory.

This file turns the loop ledger into a daily decision system. It separates passive/concurrent work from the one active avalanche lane.

## Time Lanes

Do not compare passive work to active work. They use different resources.

| Lane | Effort | Competes with active clicks? | Examples | Rule |
| --- | --- | --- | --- | --- |
| Claim lane | 5-15 minutes daily | Yes, if unfiltered | Completed jobs, refineries, stores, dailies, event claims | Triage; do not claim low-value stores by habit |
| Passive lane | 2-5 minutes setup, then background | No | Mining ore, mining Transogen, Latinum, Isogen, Away Teams, long timers | Keep ships/timers working while active block happens |
| Concurrent lane | 0-15 minutes, opportunistic | Sometimes | Alliance armadas, scheduled quick events, territory, solo outposts | Do only if it does not derail the active lane |
| Main active lane | 15-90 minutes | Yes | Hostile grinds, wave defense, solo armadas, FKR rep, Dauntless, Silent, GS-31 | Pick exactly one focus per session |

## Claim Triage

Claiming is not free if it takes 15 minutes. Claims must be filtered the same way active loops are filtered.

Use this order:

| Tier | Claim type | Claim now? | Reason |
| --- | --- | --- | --- |
| 1 | Completed research/building/ship jobs | Yes | Unlocks queues and updates the account state |
| 1 | Event rewards that expire soon or score current plan | Yes | Lost if ignored; can multiply planned work |
| 1 | Daily goals / loyalty / alliance help | Yes | Fast baseline income and queue acceleration |
| 1 | Current avalanche refineries | Yes | GS-31/S31, Dauntless/Aggregation, Transogen/Squall, selected artifact/Temporal lane |
| 2 | Passive economy claims with high value | Usually | Fleet Commanders, Syndicate, active refinery bundles, premium/rare chests |
| 2 | Armada / faction / specialty stores tied to a named purchase | Yes if target exists | Converts stored currency into selected progress |
| 3 | Mature specialty loop claims | Weekly or when convenient | Discovery, Franklin, Talios, Mantis, Stella, Reliant-style maintenance if no target |
| 4 | Legacy faction/store clutter | Skip daily | Augment data, old Rogue, old event residue, low-value faction data |
| 4 | Archived/exchange-only residue | Skip unless exchange is live | Old pass tokens, seasonal currency, Sarris/Fatu-Krey exchange residue |

Current claim skip rule:

- Do not spend daily claim time on Augment/data-style store claims unless a current target needs them.
- The account already has large legacy data/augment balances and Khan is not a current bottleneck.
- Faction store claims are worth doing only when they buy selected blueprints, officers, credits, or dailies. Generic data claims are not a daily priority.

## Output Need Ranking

Rank output need before ranking loops.

| Need | Meaning | Examples |
| --- | --- | --- |
| 5 | Blocks current progression or unlocks a new ceiling | GS-31 Recon Data if ESWD improves, a research prereq, a ship tier that opens higher targets |
| 4 | Compounds account efficiency | construction speed, refinery +1, artifact cost efficiency, Dauntless click reduction |
| 3 | Feeds a selected weekly target | known store purchase, specific officer shard, specific artifact level |
| 2 | Clears dailies or cheap scheduled value | quick event milestones, daily faction kills, quick Discovery event |
| 1 | Stockpile only | more generic currency with no spend target |
| 0 | Archived, exchange-only, or no current use | old pass residue, expired seasonal currency |

## Active ROI Score

Use this only for main active candidates.

Score out of 20:

- Output need: 0-5
- Permanent value: 0-4
- Scarcity / exclusivity: 0-3
- Event overlap: 0-3
- Feasibility: 0-3
- Concurrency bonus: 0-2
- Friction penalty: subtract 0-5 for repairs, travel, alliance dependency, or click load

Hard caps:

- Passive mining cannot be the main active lane unless an event makes it exceptional.
- FKR caps at low priority unless it has a named unlock, store purchase, credit need, daily, or event.
- Archived/exchange-only loops have zero active score.
- "More currency for later" caps at maintenance priority.
- A timed event is a multiplier, not a command, unless the event reward itself is the selected bottleneck.

## Current Candidate Ranking

This is the current Ops 66 read from the live data. Scores are estimates until the tool has repair-per-run, event objectives, artifact levels, and store targets.

| Candidate | Lane | Est. effort | Concurrent? | Output need | Estimated active score | Treatment |
| --- | --- | --- | --- | --- | --- | --- |
| Claim/sync/refineries/stores | Claim | 5-15m daily | Partial | 5 for Tier 1, 0-2 for clutter | N/A | Triage first; skip low-value stores |
| Research starts from action planner | Claim/queue | 1-5m when available | Yes | 5 | N/A | Start planned research before active grind |
| Passive mining setup | Passive | 2-5m per login | Yes | 3-5 depending shortage | N/A | Set miners before/after active block |
| Elite Solo Wave Defense / GS-31 | Main active | 25-45m | No | 5 | 15-18 | Top active investigation if wave clears are efficient |
| Dauntless / Aggregation | Main active | 15-30m | No | 4 | 12-15 | Second active lane if outputs are being spent |
| Formation Armadas / Artifacts | Concurrent/main active | 10-30m when group exists | Partial | 3-5 | 10-15 | High if a specific artifact target is selected |
| Silent / Temporal | Main active | 20-30m | No | 3 baseline, 5 with artifact target | 8-15 | Do after selecting the Temporal Artifact target |
| Anomalous Phenomena / Discovery | Scheduled quick event | 2-15m when live | Partial | 2 | 4-7 | Do only as a quick side event, not as the avalanche lane |
| FKR reputation | Main active | 20-60m | No | 1-2 baseline | 3-6 | Skip unless unlock/store/daily/event is named |
| Ore mining | Passive | 2m setup, hours passive | Yes | unknown | N/A | Passive only; mine if current ship/build/research cost needs ore |
| Uncommon Transogen mining | Passive | 2m setup, hours passive | Yes | 3 | N/A | Good passive target because planner has Transogen research signal |
| Reliant / mature specialty maintenance | Passive/claim | 0-5m | Yes | 1-2 | N/A | Claim/maintain only unless a named output is selected |

## Current Examples

### Anomalous Phenomena

This is not a main avalanche lane for this account.

Official Discovery notes describe Anomalous Phenomena / Chaotic Space as Discovery-related solo milestone events that can award Discovery Refinery tokens, which can convert to Minor Commendations, speedups, Discovery recruit tokens, and Spore Drive Components. The current account has U.S.S. Discovery L45/T9, so the ship-progression part is likely mature.

Decision:

- Do it if it takes a few minutes and gives easy speedups/tokens.
- Skip it if it takes the whole 15-minute window or interrupts GS-31/Dauntless/Silent/Formation work.
- It should not beat the main active lane unless the event reward is unusually good that day.

### FKR Reputation

FKR is not the active avalanche lane unless there is a named target.

Do FKR only if one is true:

- A faction rank unlock is selected.
- A faction credit or store purchase is selected.
- A daily/event requires it and can be cleared efficiently.
- The kills overlap another selected loop.

If FKR is required, target the faction that protects your long-term balance. With Federation far ahead, killing Federation hostiles is the likely correction path when reputation balance is the reason.

### Ore vs Uncommon Transogen

Both are passive unless a live event changes the math.

Mine ore when:

- The selected ship/building/research path is short ore.
- A material mining event overlaps without active babysitting.
- The next anchor upgrade is ore-bound.

Mine uncommon Transogen when:

- Serene Squall / Transogen research is selected.
- Refined Transogen or Black Market progression is the current bottleneck.
- You can park the Squall or a miner without stealing active time.

Current read:

- The action planner directly recommends Serene Squall Common Transogen Mining L2 and has a Transogen save target.
- That makes Transogen a better passive assignment than generic ore unless the next ship/building cost proves ore is short.
- Neither should consume the main active block.

## Daily Avalanche Loop

1. Claim only named, live-data-backed items, then sync. Skip low-value claim clutter.
2. Start approved research/building work.
3. Set passive miners based on current bottleneck.
4. Check events only for overlap.
5. Choose one main active lane:
   - GS-31 / Elite Solo Wave Defense if wave clears are efficient.
   - Dauntless / Aggregation if Aggregation outputs are current blockers.
   - Formation Armadas if an artifact target and alliance window exist.
   - Silent / Temporal only after selecting a Temporal Artifact target.
   - FKR only with a named faction unlock/store/daily/event reason.
6. Before logout, reset passive miners and claims.

## Planner Data Needed

To rank automatically, the tool needs:

- Output target per loop.
- Daily/weekly effort estimate.
- Active minutes required.
- Repair time/cost per run.
- Whether the loop can run concurrently.
- Current stockpile and spend target.
- Event overlap and reward tier.
- Lifecycle status: current, passive, scheduled, legacy, archived, verify.

## Source Anchors

- Local planner: `data/player_data/action_plan.json`
- Local account snapshot: `data/player_data/player_data.json`
- Loop lifecycle/output ledger: `LOOP_OUTPUTS.md`
- Official Discovery loop reference: https://startrekfleetcommand.com/news/the-uss-discovery/
- Official Anomalous Phenomena mechanics reference: https://startrekfleetcommand.com/news/anomaly-events-update/
