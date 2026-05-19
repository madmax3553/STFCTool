# Programmatic Avalanche Recommender

Purpose: turn live account sync data into a ranked "what should I do right now" list that updates as claims are collected, resources change, events rotate, and ship/research/build options open or close.

## Current Implementation

The recommender is implemented in:

- `src/app/strategy_recommender.h`
- `src/app/strategy_recommender.cpp`
- output: `data/player_data/strategy_plan.json`

It runs after the existing live action planner:

1. Build `FullAccountSnapshot` from `player_data.json` and cached game data.
2. Generate `ActionPlan` for research/resource affordability.
3. Generate `StrategyPlan` from the snapshot, raw player data, and action plan.
4. Show `StrategyPlan.do_now` in the Plan tab under "Avalanche Now".
5. Show suppressed traps in "Avoid / Suppress".

Pressing `p` or `P` in the app regenerates both plans. Startup also regenerates the strategy plan if it is missing, empty, older than the action plan, or tied to a different sync.

## Recommendation Lanes

Every recommendation has a lane:

- `claim`: bounded high-signal claims only.
- `queue`: start or preserve ship, building, and research queues.
- `passive`: set-and-leave actions that can run while active play happens.
- `active`: the one click-heavy focus block.
- `avoid`: suppressions for loops that look busy but are not currently high ROI.

The UI uses these lanes to separate "do this now" from "do not spend active time here."

## Avalanche Rules

The scoring intentionally favors concentrated ROI:

- Completed jobs and claimable event rewards come first only when live data proves they are actionable.
- Generic claim sweeps are not emitted as recommendations. A claim row must name a live-data-backed target.
- Ship spend checks are the anchor at Ops 66. GS-31 is promoted first when it
  is owned and underbuilt, Dauntless is the fallback, and maxed anchors such as
  NSEA/SS Revenant are suppressed unless a named target appears.
- Research starts are treated as queue filler unless they are short, speedup
  covered, or directly support the selected ship lane. Scarce special-currency
  spends can still be suppressed.
- Passive mining is recommended only as setup, not as the active focus.
- Active time is assigned to one main lane first: currently GS-31 / elite solo progression, with Dauntless as fallback.
- FKR reputation, Anomalous Phenomenon, mature Discovery-style loops, and Formation armadas are suppressed unless live data shows a named target or event overlap.

## Job Sync Limitation

Community Mod still has a job sync path and this project can parse those
payloads. However, job records are only useful when the local target receives a
fresh `job` payload. Missing or stale active jobs must be treated as unknown
queue state, not as empty queues. Stale raw job rows are ignored and the
recommender warns the user to verify queue occupancy in game.

## Research Confidence Guards

Research recommendations are split into categories before they can become
actions:

- `Daily`: normal research funded by standard resources and refinery materials.
- `Spec`: specialty-loop, artifact, refit, shard, Transogen, schematic, or other
  non-standard research currencies.
- `Prime`: Prime research, with purchase/event-gated and material-tier-gated
  items tracked as watchlist items instead of everyday ROI.

When explicit research sync is missing, levels are inferred only from active
research buff IDs where possible. Missing station buildings are shown as
`unknown`, not level 0. If core resource balances such as Parsteel, Tritanium,
or Dilithium are absent, funding becomes verify-only: no research start, save
target, or passive mining recommendation is emitted from those balances.

Spend actions now carry speedup coverage:

- Research timers compare against synced general job speedups only.
- Repair, assignment, countdown, armada, outpost, alliance, and other special
  speedups are excluded from research/building/ship coverage.
- A spend action is not blocked solely because speedups are short, but the plan
  states whether the timer is covered, verified short, or unverified because
  the resource sync is incomplete.
- Building and ship spend recommendations should reuse the same
  `SpeedupCoverage` object when those domains become first-class plan actions.

## What Makes It Adaptive

The plan changes when live sync changes:

- Actionable completed jobs disappear after claim and the claim recommendation drops.
- New resources can make a research startable.
- A scarce spend can move from "avoid" to "do now" when its target becomes the selected bottleneck.
- Event text can raise a loop if it overlaps the active lane.
- Stale sync produces warnings instead of pretending the recommendations are fresh.

## Next Improvements

Move hardcoded loop knowledge into editable strategy data:

- `data/strategy/loop_catalog.json`: loop outputs, lifecycle, effort, passive/active lane, archive status.
- `data/player_data/strategy_state.json`: selected goal, claim budget, current active focus, user-specific suppressions.
- A score breakdown per recommendation: output need, active effort, concurrency, lifecycle, event overlap, and target dependency.

This will let the recommender answer "why this and not that" without changing C++ every time the strategic thesis changes.
