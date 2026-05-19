# Live Action Planner TODO

## Core Problem

The tool does not yet do the main job: ingest the player's current live account
state, reduce it into small focused analyses, and produce a practical todo list
for limited play time.

The target output is not "more data" or "one giant AI prompt." The target output
is a ranked action list:

- What should I do first with the time I have today?
- What are my top 5 researches right now?
- Which researches can I start with current resources?
- Which researches should I save for, and exactly which resources are missing?
- Which events/resources, and any available job records, change the priority?
- What should I ignore because it is low efficiency right now?

## Product Shape

### Inputs

- Live player data from community sync:
  - officers
  - ships
  - research owned/completed
  - buildings
  - resources
  - inventory
  - buffs
  - opportunistic job records where available
  - active upgrade queues only when a fresh job payload is received
  - active events
- Game reference data from Spocks cache/API:
  - research definitions, levels, costs, prerequisites, durations
  - building definitions, levels, costs, prerequisites, durations
  - ship definitions and upgrade costs
  - officer definitions and upgrade inputs where available
- User constraints:
  - available play time today
  - current focus, such as growth, PvP, mining, events, ops push
  - optional "do not spend" reserves

### Outputs

- A short "Do Now" list for the next session.
- A "Top 5 Researches" list with:
  - priority rank
  - reason
  - can start now: yes/no
  - missing resources
  - prerequisites blocking it
  - expected benefit
  - estimated time/cost pressure
- A "Save For" list showing the resources that matter most.
- An "Avoid/Waste" list for tasks that are available but inefficient.
- A machine-readable JSON result that the TUI can render without parsing prose.

## Architecture Rule

Do not send the full account dump to AI. The code should do deterministic
filtering and math first, then send small prompts.

Flow:

1. Ingest live account data.
2. Build a resolved account snapshot.
3. Run local analyzers per domain.
4. Send small domain prompts only when strategy judgment is needed.
5. Merge domain results into a final action plan.
6. Render the todo list in the TUI and save it to disk.

## Phase 1: Reliable Live Data Snapshot

### 1.1 Harden Sync Ingestion

- Require a sync token by default.
- Bind ingress to localhost by default.
- Add a clear sync status: never synced, stale, fresh, currently receiving.
- Preserve the last successful complete sync timestamp.
- Detect partial syncs and show which domains are missing.
- Keep raw sync payloads for debugging, but do not depend on them at runtime.

### 1.2 Build `FullAccountSnapshot`

- Use `src/app/account_snapshot.*` as the canonical resolved account model.
- Ensure every player item has resolved names and relevant game metadata.
- Include current resources and any available job records in the snapshot.
- Include active events and claimable rewards in the snapshot.
- Include derived fields:
  - ops level
  - free research/build queues only when queue data is supplied; otherwise mark unknown
  - resources by category
  - active buffs affecting build/research/mining
  - current blockers

### 1.3 Snapshot Validation

- Add a validation report:
  - missing live sync domains
  - stale game cache
  - unresolved IDs
  - zero or suspicious resource balances
  - no active events
- The planner should refuse to make confident recommendations when critical
  data is missing.

## Phase 2: Deterministic Research Analyzer

This is the first high-value domain because it directly answers "what should I
research next?"

### 2.1 Research Candidate Builder

- Build all research nodes that are:
  - not completed
  - visible or soon unlockable
  - prerequisites met or one prerequisite away
  - relevant to current ops level
- For each candidate, attach:
  - current level
  - next level
  - costs
  - duration
  - prerequisites
  - tree/category
  - benefit text

### 2.2 Resource Affordability Check

- Compare each candidate cost against current resources.
- Mark:
  - `can_start_now`
  - `missing_resources`
  - `percent_affordable`
  - `largest_blocker`
  - `spend_risk`, if it consumes a scarce resource needed elsewhere

### 2.3 Local Research Scoring

- Score candidates before AI sees them.
- Initial scoring factors:
  - unlocks new building/ship/research path
  - improves daily efficiency
  - improves event scoring
  - improves combat/mining bottleneck
  - short duration/high impact
  - affordable now
  - near-affordable with clear save target
  - prerequisite for ops progression
- Penalize:
  - unaffordable with no realistic path
  - irrelevant low-level cleanup
  - expensive marginal gains
  - blocked by missing building/research prerequisites

### 2.4 Research Analysis JSON

Produce a compact object like:

```json
{
  "top_research": [
    {
      "id": 123,
      "name": "Research Name",
      "tree": "Combat",
      "current_level": 4,
      "next_level": 5,
      "local_score": 87.5,
      "can_start_now": true,
      "costs": [{"resource": "Tritanium", "amount": 1000000, "owned": 2500000}],
      "missing_resources": [],
      "prerequisites": [],
      "reason": "Unlocks higher warp range and supports current ops push"
    }
  ]
}
```

## Phase 3: Small Domain Prompts

### 3.1 Research Prompt

- Input only the top 10-15 locally scored research candidates.
- Include current resources summary and user focus.
- Ask for top 5 with tradeoffs.
- Require JSON output.
- Do not ask the model to calculate affordability; the code already did that.

Prompt goal:

```text
Given these locally scored research candidates and current resource constraints,
choose the top 5 actions for a player with limited time. Prefer high-impact,
affordable or near-affordable research. Explain what to start now, what to save
for, and what to ignore.
```

### 3.2 Resource Prompt

- Input only missing-resource totals from top research/build/ship candidates.
- Ask which resources to save and which spending to avoid.
- Output a save plan, not generic farming advice.

### 3.3 Event Prompt

- Input only active/soon events with objectives and remaining time.
- Ask which available actions overlap with event scoring.
- Output time-sensitive tasks.

### 3.4 Final Synthesis Prompt

- Input only:
  - research top 5
  - resource save plan
  - active event opportunities
  - current active jobs when synced, otherwise queue state unknown
  - user time budget
- Output:
  - do now
  - do today
  - save for
  - avoid

## Phase 4: Action Plan Model

Create a stable action-plan schema:

```json
{
  "generated_at": 1770000000,
  "sync_age_seconds": 120,
  "time_budget_minutes": 45,
  "focus": "growth",
  "do_now": [
    {
      "priority": 1,
      "action": "Start research: X",
      "domain": "research",
      "reason": "Highest affordable unlock",
      "duration_seconds": 3600,
      "can_do_now": true,
      "resources_spent": [{"resource": "Dilithium", "amount": 500000}],
      "missing_resources": []
    }
  ],
  "save_for": [
    {
      "target": "Research: Y",
      "missing_resources": [{"resource": "Tritanium", "amount": 1200000}],
      "reason": "Best blocked upgrade"
    }
  ],
  "avoid": [
    {
      "action": "Research: Z",
      "reason": "Available but low impact compared with current bottlenecks"
    }
  ]
}
```

## Phase 5: TUI Integration

### 5.1 New Planner Tab

- Add a fourth tab: `Plan`.
- Show:
  - sync freshness
  - top 3 do-now actions
  - top 5 researches
  - save-for resources
  - avoid list
- Add key bindings:
  - `p`: generate plan
  - `P`: force regenerate from latest sync
  - `t`: edit time budget
  - `f`: cycle focus

### 5.2 Persistence

- Save latest plan to `data/player_data/action_plan.json`.
- Load latest plan on startup.
- Show stale warning when plan is older than last sync.

## Phase 6: Tests

### 6.1 Unit Tests

- Snapshot validation handles missing domains.
- Research candidate builder excludes completed research.
- Affordability calculation catches missing resources.
- Local scoring ranks affordable unlocks above blocked low-value nodes.
- Action plan JSON round-trips.

### 6.2 Smoke Tests

- Cached data can produce a plan without network.
- Live sync data with resources can produce top 5 research recommendations.
- Missing resources appear in `save_for`.
- Stale sync produces a warning instead of confident advice.

## Implementation Order

1. Fix sync security and freshness reporting.
2. Make `FullAccountSnapshot` complete enough for research/resource planning.
3. Implement research candidate extraction and affordability checks.
4. Implement local research scoring and JSON output.
5. Add a small research prompt that consumes only top candidates.
6. Implement action-plan schema and persistence.
7. Add TUI `Plan` tab.
8. Add resource and event analyzers.
9. Add final synthesis prompt.
10. Replace the current template planner with the live action planner.

## Definition of Done

The feature is done when a freshly synced account can answer:

- "What are my top 5 researches right now?"
- "Which can I start immediately?"
- "Which resources am I short on?"
- "What should I save for?"
- "What are the best tasks for the next 30-60 minutes?"
- "What should I avoid because it is inefficient right now?"

The answer must be generated from live account data, not static templates or a
full-account AI dump.
