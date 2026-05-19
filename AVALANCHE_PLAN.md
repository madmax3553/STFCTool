# STFC Avalanche Plan

Last reviewed: 2026-05-16
Live sync basis: Ops 66 account data, action planner output, ship roster, resources, events, and any fresh job records. Active building/research/ship queues remain manual/unknown when the local sync target has not received a recent `job` payload.

## Current Thesis

Do not move to Ops 67 yet.

The account is in a catch-up state, not a push-up state. The current best use of limited active time is to concentrate on the next explicit bottleneck, not to balance visible numbers for their own sake.

Active-time ranking now lives in `ACTIVE_RANKING.md`. Use that file to separate claim/passive/concurrent work from the one main avalanche lane. The live-data implementation is documented in `PROGRAMMATIC_RECOMMENDER.md`.

The primary avalanche lane is:

1. Research and building efficiency that compounds future catch-up
2. Event objectives that overlap planned work
3. One current ship upgrade path that removes a real bottleneck
4. One specialty loop that unlocks or accelerates current progression
5. FKR reputation only when it is tied to a named unlock, faction credit need, daily/event objective, or store breakpoint

Everything else is maintenance unless it directly supports those five items.

## Correction: Rep Is A Lever, Not The Goal

Balancing Federation/Klingon/Romulan reputation is not automatically high ROI.

FKR grinding is worth focused active time only when at least one of these is true:

- A specific ship, blueprint bundle, faction store claim, research, or daily claim is locked behind the next reputation tier.
- A faction credit shortage is blocking the selected ship path.
- An event scores FKR hostile kills or FKR reputation gain.
- Dailies require it and the hostile kills can be completed efficiently.
- The hostile grind also advances another selected lane.

If none of those are true, FKR balance is a maintenance task, not the main avalanche lane.

The visible imbalance still matters because it shows Federation has the most cushion and Romulan/Klingon are lagging. It does not prove that reputation should beat research, events, Silent/Section 31, Transogen, or another named bottleneck.

## What Makes An Ops 66 Strong

Raw power is a weak ranking signal. It can be inflated by low-impact research, old ships, unused buildings, or broad spending that does not improve daily reach.

A strong Ops 66 should be judged by working power: how much useful progress the account can convert from limited time, materials, officers, and events.

Use this scorecard instead of raw power.

### Ops 66 Scorecard

Total: 100 points.

1. Core progression uptime: 20 points
   - Research queue stays active.
   - Building queue stays active.
   - Speedups are used to finish meaningful work during events, not to rush low-value tasks.
   - Strong account: queues are rarely idle and research/building choices match current blockers.
   - Weak account: queues idle, or speedups go into whatever is available.

2. Research quality: 20 points
   - Current-era, current-use research beats old-grade or cosmetic power.
   - Compounding efficiency beats narrow one-off gains unless the one-off unlocks a bottleneck.
   - Strong account: research improves current combat, mining, refinery, build, or loop throughput.
   - Weak account: research is chosen because it is affordable, Prime, or high might.

3. Fleet ceiling and coverage: 20 points
   - One strong anchor ship matters more than many mediocre ships.
   - Coverage still matters after the anchor is established: dailies, armadas, mining, and specialty loops need functional ships.
   - Strong account: one main combat ship pushes the highest efficient content, with enough support ships to run daily loops.
   - Weak account: materials are spread across ships without unlocking higher targets, better refineries, lower repairs, or event reach.

4. Officer and crew depth: 15 points
   - A great ship without the right crew is not a great ship.
   - Below-deck officers matter because they let the same ship do more work with fewer repairs.
   - Strong account: crews exist for PvE, armadas, solo armadas, PvP defense, mining, and loop-specific loot.
   - Weak account: one good crew cannibalizes every activity and forces constant swapping.

5. Economic engine: 15 points
   - Refineries, armadas, specialty currencies, and passive mining determine how fast upgrades continue.
   - Strong account: bottleneck materials are being produced before they are urgently needed.
   - Weak account: active time is spent chasing emergency materials that should have been passively stocked.

6. Event discipline: 10 points
   - Events are multipliers, not commands.
   - Strong account: planned research, upgrades, hostiles, armadas, and mining are timed into event scoring.
   - Weak account: events redirect the account into unrelated spend or random loops.

### Anchor Ship vs Balanced Fleet

The right model is not "one ship only" or "balance every ship." The right model is anchor plus minimum viable bench.

Anchor ship:

- Gets the majority of scarce ship XP, parts, materials, and officer attention.
- Should be the ship that increases the account's ceiling: higher hostile targets, better event scoring, better armadas, lower repairs, or PvP survivability.
- Should keep receiving resources only while the next tier or level changes what the account can actually do.

Minimum viable bench:

- One daily hostile/loot ship.
- One armada or solo armada setup.
- One or two miners that support the current material bottleneck.
- Specialty ships only where the refinery or loop is still strategically useful.
- A PvP defender only if PvP is a real server/account requirement.

Rule:

- Push the anchor until the next upgrade no longer unlocks better content or lower cost.
- Then fund the weakest bench slot that blocks daily/event/refinery progress.
- Do not level side ships just to make the fleet look balanced.

### Ship Upgrade Test

Before upgrading any ship, answer these questions.

1. What does the next tier/level unlock?
   - higher hostile target
   - higher armada tier
   - better refinery yield
   - lower repair burden
   - required event scoring
   - required research/building/faction path

2. How often will that unlock be used?
   - daily
   - weekly
   - event-only
   - rarely

3. What scarce resource does it consume?
   - 6-star materials
   - ship parts
   - Prime currency
   - faction credits
   - ship XP
   - officers

4. What does that resource not get to do if spent here?
   - anchor combat ship
   - compounding research
   - refinery upgrade
   - specialty loop unlock
   - future Ops prerequisite

5. Is the account blocked by this ship, or is this just a power increase?

Only upgrade when the answer is "this removes a blocker" or "this materially improves the daily/event loop."

### Current Account Interpretation

Based on the live planner output, the account does not currently prove that FKR reputation balance is the main blocker.

The stronger evidence is:

- Research/building efficiency matters because the account is Ops 66 and queue uptime is high leverage.
- Current research options include short, fully funded compounding or loop-supporting items.
- Serene Squall/Transogen has an explicit research signal.
- Prime currencies are scarce enough that Prime choices need justification.
- Ship recommendations need a stricter unlock test before committing to Negh'Var, Serene Squall, NSEA Protector, SS Revenant, or another path.

Working hypothesis:

- The next plan should rank bottlenecks, not loops.
- The best loop is whichever moves the highest-ranked bottleneck today.
- FKR becomes primary only if the bottleneck list shows faction reputation, faction credits, or a faction store unlock above research/building/specialty progression.

### Investigation Needed Before Final Ship Priority

To choose between SS Revenant, NSEA Protector, Negh'Var, Serene Squall, or another ship path, collect or compute these facts.

1. Current ceiling
   - Highest hostile level killed efficiently.
   - Highest armada / solo armada completed efficiently.
   - Repair time and cost for the main daily grind.
   - Whether any daily/event targets are currently painful or impossible.

2. Upgrade unlocks
   - What the next tier of each candidate ship actually changes.
   - Whether it unlocks a new target band, refinery claim, research path, or just adds power.
   - Material cost to the next breakpoint, not just next component.

3. Bottleneck resources
   - Which 6-star material is shortest.
   - Which ship parts are shortest.
   - Which Prime currencies are scarce.
   - Which refinery outputs are not keeping up with planned spending.

4. Event conversion
   - Which planned actions score the most events.
   - Which events only look valuable because they invite unrelated spending.
   - Which loop gives useful points without changing the plan.

5. Officer constraint
   - Whether the proposed anchor ship has the crew it needs.
   - Whether moving officers to that ship weakens armadas, mining, or specialty loops too much.

Decision rule:

- If a ship upgrade raises the current ceiling or removes a repeated repair/event blocker, it can be the anchor.
- If it only adds power while another path improves research, refinery, or daily conversion, it waits.

## Loop ROI Ledger

The program should not rank loops until it knows what each loop pays into. A loop is worth active clicks only when its output feeds a current bottleneck or a high-value permanent unlock.

Detailed loop/output mapping now lives in `LOOP_OUTPUTS.md`. Use that ledger as the source of truth when deciding whether a loop is active, maintenance, event-only, or skip.

Current rule:

- Every loop must answer: "What permanent thing am I buying, upgrading, or unlocking?"
- If the answer is only "more currency for later," the loop is maintenance unless an event makes it efficient today.

### Formation Armadas

What the loop pays into:

- Artifact Tokens, Premium Artifact Tokens, Artifact Shards, and artifact progression.
- Rare Formation Armadas can also feed higher-value artifact and advanced currency paths.

Why it can be high ROI:

- Artifacts are permanent account-wide power.
- Some artifacts directly reduce ship component costs or improve research/building efficiency.
- This account owns Tuvok at rank 5, and Tuvok increases Artifact Tokens gained from Formation Armadas.

Live account signal:

- Formation Armada Directive (Uncommon): 5,815
- Formation Armada Directive (Rare): 70
- Artifact Token: 189,998
- Premium Artifact Token: 895
- Isolytic Artifact Token: 2,362
- Artifact Gallery Schematics: 46,112
- Rare Artifact Choice Token: 51
- Uncommon Artifact Choice Token: 39

Preliminary priority:

- Medium-high if there are unmaxed artifacts that improve ship cost efficiency, research/building efficiency, hostile efficiency, or current combat ceiling.
- Low-maintenance if tokens/shards are stockpiled and no artifact target is selected.

Decision question:

- Which specific artifacts are unmaxed, and do any of them improve the current bottleneck?

### Silent Hostiles / Temporal Artifacts

What the loop pays into:

- Temporal Disruptor Parts and reward chests.
- Temporal Artifact progression and advanced upgrade paths.

Why it can be high ROI:

- Temporal artifacts are permanent power.
- Silent Hostiles are not just "another hostile loop"; they feed a specific artifact system.

Live account signal:

- Temporal Disruptor: 421,014
- 4-star Temporal Disruptor Parts: 82,813
- 5-star Temporal Disruptor Parts: 12,231 and 107,096
- 6-star Temporal Disruptor Parts: 70,579 and 112,308
- Silent Nebula Token: 8

Preliminary priority:

- Medium if a specific Temporal Artifact is underbuilt and useful now.
- Low if current Temporal Artifact chests are not blocking anything and the run is not event-scored.

Decision question:

- Which Temporal Artifacts are currently available/upgradable, and which one improves the Ops 66 bottleneck?

### Elite Solo Wave Defense / GS-31

What the loop pays into:

- Section 31 Access Keys, Section 31 Credits, Section 31 Mirror Credits, GS-31 Recon Data, and GS-31 progression.

Why it can be high ROI:

- GS-31 Recon Data is sourced through Elite Solo Wave Defense.
- GS-31 directly improves Apex Shred against Apex Raider / Solo Wave Defense hostiles.
- Section 31 store outputs include favors, chaos tech, artifacts, officers, currencies, refits, and ship-parts conversion paths.

Live account signal:

- GS-31: L15/T3
- GS-31 Parts: 638
- GS-31 Recon Data: 100
- GS-31 Recycle Token: 2,600
- Section 31 Reputation: 150,500
- Section 31 Access Key: 6,950
- Section 31 Credits: 4,515
- Section 31 Mirror Credits: 230
- Section 31 Private Cipher: 4,005
- Section 31 Cipher: 21,403
- S31 Georgiou owned, but only L5/R1.
- Rachel Garrett owned L10/R2.
- Zeph owned L5/R1.

Preliminary priority:

- High investigation priority.
- This looks more underdeveloped than the older maxed specialty loops and may be the kind of lane that actually raises working power.
- It should become a primary loop only if the account can complete enough Elite Solo Wave Defense to convert clicks into GS-31/S31 progress without excessive repairs.

Decision question:

- What wave can be cleared now, what rewards does that wave produce, and does the next GS-31 tier improve the next clear?

### Dauntless / Aggregation

What the loop pays into:

- Aggregation loop progression, Dauntless parts, Dauntless Prototype Data, System Scans, additional Aggregation Refinery claims, building materials, research dust, Forbidden Tech materials, and Prototype Tech materials.

Why it can be high ROI:

- Dauntless reduces hostile-grind friction through Seek and Destroy.
- Each tier improves the Aggregation Refinery daily bundle.
- It is a long-term progression ship for the Ops 45-70 band.

Live account signal:

- U.S.S. Dauntless: L20/T4
- U.S.S. Dauntless Parts: 415
- U.S.S. Dauntless Prototype Data: 3,430
- Dauntless System Scans: 1,100
- Aggregation Intel Chip: 231
- Aggregation Smuggled Studies: 5,857
- Common Aggregation Banner: 28,900
- Uncommon Aggregation Banner: 641,350
- Rare Aggregation Banner: 128,800

Preliminary priority:

- Medium-high investigation priority.
- If Aggregation Refinery outputs are current bottlenecks, Dauntless may beat generic hostile grinding because it reduces clicks and increases daily sourcing.
- If Aggregation outputs are not being spent, Dauntless is maintenance.

Decision question:

- Are Aggregation Refinery outputs blocking research, buildings, forbidden tech, prototype tech, or ship upgrades this week?

### NSEA Protector / Mirror Universe

What the loop pays into:

- Raw Omega-Trellium, Liquid Omega Trellium, Omega Mirror Dust, Mirror Research, NSEA upgrades, Prime particles, NSEA refit paths, Mirror Universe reach, and Omega-13 support.

Why it can be high ROI:

- NSEA opens Mirror Universe systems and Mirror Research expansion paths.
- Omega Mirror Dust is exclusive to the NSEA/Mirror loop.
- NSEA also improves Mirror hostile damage and raw Trellium mining.

Live account signal:

- NSEA Protector: L70/T14 in synced data.
- Raw Omega-Trellium: 83,350,123
- Liquid Omega-Trellium: 3,046,800
- Omega Mirror Dust: 8,355
- Beryllium Sphere: 1,130,411
- Omega-13 Matter: 5,766

Preliminary priority:

- Maintenance unless Mirror Research, final NSEA tier, Prime particles, or Omega Mirror Dust is the selected bottleneck.
- If NSEA is actually maxed in game after the last sync, the loop should drop further unless the output is being spent.

Decision question:

- Is Omega Mirror Dust or final NSEA progression blocking a specific Mirror Research node or account-wide efficiency gain?

### Nesmith / Galaxy Quest Weekly

What the loop pays into:

- Nesmith officer progression and Galaxy Quest related rewards.

Why it can be high ROI:

- Nesmith improves Apex Barrier against hostiles and has an Invading Entity role.
- He can matter if current content requires Apex Barrier or Invading Entity performance.

Live account signal:

- Nesmith: L10/R2
- Galaxy Quest VHS: 5,454
- Galaxy Quest Elite BP: 1
- Sarris Dominion Salvage: 860

Preliminary priority:

- Event-opportunistic unless a current target needs Nesmith specifically.
- Do not max him just because the weekly exists.

Decision question:

- Is Nesmith used in the current best crew for a selected bottleneck, or is this just a completion project?

### Current Preliminary Loop Ranking

This is not final, but it is a better starting point than "do some of everything."

1. Elite Solo Wave Defense / GS-31
   - Highest investigation priority because GS-31 is underbuilt and Section 31 has multiple permanent outputs.

2. Dauntless / Aggregation
   - Potentially high because it reduces click burden and improves daily sourcing, but only if Aggregation outputs are currently useful.

3. Formation Armadas
   - Potentially high because artifacts are permanent, especially if key cost-efficiency artifacts are unfinished.

4. Silent Hostiles / Temporal Artifacts
   - Worth doing if a specific Temporal Artifact is selected; otherwise maintenance/event-only.

5. NSEA / Mirror Universe
   - Maintenance unless Omega Mirror Dust, Mirror Research, or final NSEA progression is the current blocker.

6. Nesmith weekly
   - Opportunistic unless Nesmith is needed for a selected Apex Barrier / Invading Entity bottleneck.

Program requirement:

- Add loop metadata so the planner can say: "This loop is high ROI because it advances Artifact X / Ship Y / Research Z," not just "this loop has resources available."

## Today's Checklist

Run this exact order on the next login.

- [ ] Claim completed ship jobs.
- [ ] Claim completed/expired Daily Goals.
- [ ] Re-sync the tool.
- [ ] Check events for overlap with approved research, planned ship upgrades, Silent/Section 31, armadas, Transogen, or FKR only if FKR has a current unlock/value target.
- [ ] Start the highest approved short research that is still available.
- [ ] Spend the main active block on the highest overlap activity from the conflict rules below.
- [ ] Do FKR only if it has a current unlock, event, daily, or faction credit reason.
- [ ] Put miners out before logging off; do not spend the active session babysitting mining unless an event makes it clearly superior.

If time is only 15 minutes:

1. Claim/sync.
2. Start a short approved research.
3. Do the best event-overlap activity. If there is no overlap, do the current named bottleneck, not generic rep balance.

If time is 45 minutes:

1. Claim/sync/event check: 5 minutes.
2. Approved research or ship upgrade action: as needed.
3. Main active block: 25-30 minutes on the highest ROI overlap activity.
4. Secondary block: 10-15 minutes only if it clears a daily/event or feeds a selected bottleneck.
5. Passive mining setup before logout.

If time is 90 minutes:

1. Do the full 45-minute loop.
2. Repeat the main active block if it still has event or bottleneck value.
3. Only add extra loops if they score an event or clear a daily.

## Snapshot

- Ops level: 66
- Faction reputation is badly uneven:
  - Federation: 226.8B
  - Klingon: 34.5B
  - Romulan: 15.1B
- Federation is far ahead, so Federation hostiles are the preferred target if FKR grinding is needed. This is a target-selection rule, not proof that FKR grinding is the main goal.
- Completed jobs should only be claimed when the tool can identify an actionable unreflected job. Completed records already reflected in research/building state are stale noise.
- Event data exists, but objective names and scoring are not resolved well enough yet for the tool to give a trustworthy "do event X" answer without checking the in-game event screen.

## Daily Operating Loop

Use this order when game time is limited.

### 1. Claim, Sync, Event Check

Timebox only named claims. Claiming is not free if it consumes the first active block.

Do:

- Claim completed jobs only when the tool names an actionable unreflected job.
- Claim Daily Goals only when the tool or current screen confirms unclaimed rewards.
- Claim event rewards only when the tool identifies manual claimable rewards, not raw stale claim flags.
- Claim current avalanche refineries and stores only when they are named: GS-31/S31, Dauntless/Aggregation, Transogen/Squall, selected artifact/Temporal lane.
- Re-sync the tool.
- Open the event screen and look only for objectives that score from:
  - approved research starts
  - the selected ship upgrade path
  - the selected specialty loop
  - Silent / Section 31 if it is the current loop
  - armadas already worth doing
  - FKR hostile kills or FKR reputation gain only when tied to a current target

Skip:

- Standalone spend events.
- PvP/base-hit events unless already planned.
- Mining events that would consume active time instead of passive ship time.
- Events requiring unrelated ship upgrades.
- Low-value store clutter, including legacy data/Augment-style claims, unless a current target needs them.

Rule: if the event does not overlap a selected bottleneck, it is noise.

## Conflict Rules

Use these when the game presents competing choices.

- Planned research event beats generic hostile grinding.
- Planned ship upgrade event beats generic hostile grinding.
- A loop tied to an unlock beats a loop done only for balance.
- FKR hostile event beats generic hostile event only if FKR has a current unlock, daily, faction credit, or store reason.
- Silent / Section 31 beats FKR balance if it is tied to current event scoring or a current Section 31 bottleneck.
- Research event is worth doing only with the approved queue.
- Ship upgrade event is worth doing only with a candidate that has passed the Ship Upgrade Test.
- Mining event is passive only unless it has unusually high rewards and can be done without cutting the main active block.
- Armada event is worth doing only if directives and repair time are reasonable and the reward feeds catch-up.
- Spend event is skipped unless the spend was already planned before checking the event.
- Ops/building push is skipped unless it directly removes a catch-up blocker.

### 2. Main Active Block: Current Bottleneck

Timebox: 25-30 minutes.

Choose the block in this order:

1. Event overlap with an already-approved research, upgrade, hostile, or armada action.
2. A named unlock or material bottleneck for the selected ship/specialty path.
3. Daily completion that does not pull resources away from the selected path.
4. FKR reputation grind only if it has a current unlock, credit, store, daily, or event reason.

If FKR is the chosen block:

- Kill Federation hostiles to raise Klingon and Romulan reputation.
- Use SS Revenant as the main hostile grinder if repair cost and time are acceptable.
- Use Ghalenar on the bridge when the purpose is FKR reputation gain.

If FKR is not the chosen block:

- Do not grind reputation just to make the faction numbers prettier.
- Spend active time on the selected event/unlock/material bottleneck.

Crew direction:

- If dying or repairs are too heavy: use a survival hostile crew and only add loop-specific officers if the kill rate stays acceptable.
- If an event rewards hostile kills but not rep, prioritize kill count efficiency over rep bonus.

Stop condition:

- Stop when dailies/event scoring are done, repairs become inefficient, or the session timebox ends.

### 3. Secondary Active Block

Timebox: 10-15 minutes, only after the main block.

Do this when:

- There is an event/daily objective for it.
- The main block is complete.
- You still have active time and repairs are reasonable.

Why:

- Section 31 appears underdeveloped compared with older maxed loops.
- You have Section 31 access resources available, so the loop is relevant.

Stop condition:

- Stop when the event/daily target is complete.
- Do not turn this into an endless side loop.

### 4. Armadas

Do only when stacked with value.

Valid reasons:

- Event scoring.
- Alliance Store/refinery value.
- Daily completion.
- Helping the chosen catch-up lane.

Invalid reasons:

- Spending directives just because they exist.
- Chasing old loops with no current bottleneck.

### 5. Passive Mining

Mining should mostly happen while away or overnight.

Preferred passive targets:

- 6-star raw materials needed for G6 catch-up.
- Transogen if supporting Serene Squall and Black Market progression.

Rule: active clicks should not go into mining unless the event return clearly beats hostile grinding.

## Research Queue

This is the working order, adjusted from the raw action planner output to match the catch-up avalanche.

1. Custom Construction Tools L5
   - Fully funded.
   - Short timer.
   - Construction speed compounds every building catch-up step.

2. Prime Armada Refinery L1
   - Stronger long-term catch-up value than Prime Fleet Commander Gift if Prime Valor is still available.
   - Only choose this over Gift if Prime Valor has not already been spent.

3. Revised Colloquium L1
   - Fully funded.
   - Short timer.
   - Improves Nova Squadron building efficiency.

4. Serene Squall Common Transogen Mining L2
   - Fully funded.
   - Directly supports the selected specialty lane.

5. Omega Rapid Scrapping L6
   - Fully funded.
   - Short timer.
   - Good account efficiency, but below construction and the main catch-up lane.

Hold or defer:

- Prime Fleet Commander Gift, unless choosing it instead of Prime Armada Refinery.
- G3 Prime Station Efficiency.
- Prime Station Siege.
- Prime Dominion Solo Raiding.
- Prime Charged Nanoprobes Refining.

## Provisional Ship Upgrade Candidates

These are candidates, not final commitments. They must pass the Ship Upgrade Test above before receiving scarce materials.

### Combat / FKR Candidate

Candidate:

- Negh'Var L40/T8 to L45/T9.

Reason:

- It is the cleanest single G6 FKR combat catch-up target visible in the roster.
- It avoids spreading materials across multiple faction ships.
- This is a ship-path recommendation, not a reason to grind reputation unless reputation or faction credits become the blocker.

Gate:

- Upgrade only if it unlocks a real combat, faction-store, research, event, or efficiency breakpoint that the current anchor ship does not already cover.
- If SS Revenant, NSEA Protector, or another current anchor already clears the needed content efficiently, Negh'Var waits.

Stop condition:

- Stop at L45/T9 and reassess.
- Do not also push Titan, Northcutt, Vor'cha, Nova, or other side ships during this phase.

### Specialty Candidate

Candidate:

- Serene Squall L40/T8 to L45/T9.

Reason:

- It supports the Transogen/Black Market lane.
- Current research recommendations already point at Serene Squall Transogen mining.
- It gives a focused specialty progression target instead of rotating through old loops.

Stop condition:

- Stop at L45/T9 and reassess.
- If the upgrade cost blocks core G6 combat catch-up, hold the ship and continue research/mining only.

## Maintenance Loops

These are not primary active-time loops right now.

Do only for dailies, refinery timing, or events:

- Monaveen
- Mantis / Actian
- Voyager
- Talios
- Defiant
- Franklin-A
- Older specialty refineries

Reason:

- Several of these ships are already maxed or have meaningful stockpiles.
- The opportunity cost is high if they consume time that should be moving the selected bottleneck.

## Ignore List

For this phase, avoid:

- Ops 67 push.
- Random ship upgrades.
- Mining as active game time.
- PvP/base raiding spend.
- Prime Station Siege.
- G3 Prime Station Efficiency.
- Prime Dominion Solo Raiding unless Dominion becomes the chosen primary lane.
- Event leaderboards that do not overlap the avalanche lane.
- Any "just because available" loop.

## Weekly Targets

Run this for seven days, then re-check.

Track:

- Current named bottleneck and whether it moved.
- Research completed from the approved queue.
- Event objectives completed without off-plan spend.
- Whether Negh'Var reached L45/T9.
- Whether Serene Squall reached L45/T9.
- FKR reputation gain only if FKR had a current target.
- Which events were skipped and whether skipping them hurt.

Success criteria:

- The selected bottleneck moved meaningfully.
- No major resources are spent outside the two selected ship paths.
- Active play feels narrower: fewer loops, fewer random clicks, more repeated progress.

## Tool Improvements Needed

To make this plan sharper, the project should improve these areas:

1. Event objective resolution
   - The tool sees event records, but not enough readable scoring detail.
   - Needed output: "do this event because it overlaps FKR hostile kills" vs "skip this event."

2. Ship upgrade cost projection
   - Needed output: exact material bottleneck for Negh'Var L40/T8 to L45/T9 and Serene Squall L40/T8 to L45/T9.

3. FKR reputation simulator
   - Needed output: recommended hostile faction target based on current faction cushions and lock risk.

4. Daily loop generator
   - Needed output: a 45-minute checklist from current live data:
     - claim
     - event match
     - hostile target
     - ship
     - crew
     - research
     - stop rules

5. Maintenance loop suppression
   - Needed output: flag loops as "maintenance only" when ships are maxed or inventory stockpiles are high.

## Current One-Sentence Plan

Stay Ops 66, finish compounding research and selected ship/specialty bottlenecks, use events only when they overlap planned work, and do FKR reputation only when it unlocks or funds something specific.
