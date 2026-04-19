# STFCTool — Architecture Plan

## What This Tool Is

A personalized strategy engine for Star Trek Fleet Command. Not a generic
crew optimizer — a system that ingests your actual account data, applies
math to organize and rank it, uses AI for focused sub-strategy analysis,
lets you make decisions, then synthesizes everything into an actionable plan.

## The Pipeline

```
┌──────────────────────────────────────────────────────────────┐
│ STAGE 1: DATA                                                │
│ Ingest all real account + game data                          │
│                                                              │
│ Runtime sources:                                             │
│   • api.spocks.club — officers, ships, research, buildings   │
│   • community mod sync — player state, events, buffs, jobs   │
│                                                              │
│ Development reference (not used at runtime):                 │
│   • "The Officer Tool" (StewieDoo xlsx) — curated officer    │
│     abilities, scores, pre-set crews, ship data.             │
│     Used to validate our scoring, seed structured data       │
│     files, and catch missing officers/effects/synergies.     │
│                                                              │
│ Output: AccountSnapshot                                      │
│   Complete picture of the player's current state              │
│   including active events and in-progress jobs               │
└───────────────────────┬──────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ STAGE 2: MATH                                                │
│ Organize, rank, and trim data per domain                     │
│                                                              │
│ Domains:                                                     │
│   1. Crews     — score crews per scenario, rank top options  │
│   2. Ships     — evaluate fleet, match ships to roles        │
│   3. Research  — calculate ROI / unlock priority             │
│   4. Officers  — rank-up / level-up priority                 │
│   5. Resources — current state, burn rate, bottlenecks       │
│   6. Events    — map event objectives to actionable tasks    │
│                                                              │
│ Purpose: Reduce 289 officers to 20 relevant ones,            │
│ reduce 100 research nodes to top 5, etc. so AI gets          │
│ focused inputs, not a data dump.                             │
│                                                              │
│ Output: DomainAnalysis (one per domain)                      │
│   Structured, ranked, trimmed data ready for AI prompts      │
└───────────────────────┬──────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ STAGE 3: AI SUB-STRATEGY                                     │
│ Scoped AI queries per domain                                 │
│                                                              │
│ Each domain gets its own focused prompt:                     │
│   • Here are your top 20 PvP officers, ranked. Best crew?   │
│   • Here are your top 5 research options with ROI. Priority? │
│   • Here's your fleet. Ship-to-role assignment?              │
│   • etc.                                                     │
│                                                              │
│ Key: prompts are SMALL because the math layer already        │
│ did the heavy lifting. AI reasons about strategy,            │
│ not data processing.                                         │
│                                                              │
│ Output: SubStrategyResult (one per domain)                   │
│   AI recommendations with reasoning                          │
└───────────────────────┬──────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ STAGE 4: USER DECISIONS                                      │
│ Human reviews and confirms/adjusts per domain                │
│                                                              │
│ UI presents sub-strategy results. User can:                  │
│   • Accept a crew recommendation                            │
│   • Swap an officer                                          │
│   • Override a research priority                             │
│   • Set their own focus (e.g., "I want to push mining")     │
│                                                              │
│ Output: UserDecisions                                        │
│   Confirmed choices across all domains                       │
│   Stored as structured data, not just UI state               │
└───────────────────────┬──────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ STAGE 5: STRATEGY SYNTHESIS → ACTION PLAN                    │
│ AI combines everything into "what to do now"                 │
│                                                              │
│ Input:                                                       │
│   • UserDecisions (confirmed crews, ships, priorities)       │
│   • Active events (from sync data)                           │
│   • Current jobs (what's already building/researching)       │
│   • Time context (day of week, event timers)                 │
│                                                              │
│ Output: ActionPlan                                           │
│   Prioritized, time-aware task list:                         │
│   "1. Start tritanium research (6h) — unlocks T4 warp       │
│    2. Run eclipse armada with [crew] on [ship]               │
│    3. Mine dilithium on dock 3 — event objective 2/5         │
│    4. Level Khan to 40 — improves PvP crew by 12%"          │
└──────────────────────────────────────────────────────────────┘
```

## Data Contracts Between Stages

### AccountSnapshot (Stage 1 → Stage 2)
Everything we know about the player right now:
- officers: id, name, level, rank, stats, abilities (structured)
- ships: name, hull type, tier, level, grade, abilities
- research: completed, in-progress, available
- buildings: levels, upgrade state
- resources: current amounts
- buffs: active buffs/boosts
- jobs: active build/research/upgrade queues
- events: active events with objectives and progress

### DomainAnalysis (Stage 2 → Stage 3)
One per domain. Each contains:
- ranked/scored items (top N relevant, not the full list)
- math justification (why item X ranks above item Y)
- constraints (officers already assigned, resources available)
- context from account state (level, bottlenecks)

### SubStrategyResult (Stage 3 → Stage 4)
One per domain. Each contains:
- recommendations (ordered list with reasoning)
- trade-offs explained ("crew A is better for damage, crew B is better for survivability")
- confidence level (high if META is clear, low if situational)

### UserDecisions (Stage 4 → Stage 5)
Structured choices:
- confirmed_crews: map<dock_id, {captain, bridge, bda, ship}>
- research_priority: ordered list
- officer_upgrades: ordered list
- resource_plan: allocations
- focus: user's stated priority (war/growth/mining/event)

### ActionPlan (Stage 5 output)
- tasks: ordered by priority, time-tagged
- each task has: action, reason, estimated impact, time required
- grouped by timeframe: "do now", "do today", "do this week"

## The Officer Tool (Development Reference)

The "STFC Officers Tool" by StewieDoo (xlsx) is NOT a runtime dependency.
It is a development reference used to:

1. **Validate scoring** — compare our optimizer output against curated
   StewieDoo ratings (Immediate Value, Upgrade Value, Overall Score)
2. **Seed structured data** — extract exact ability values, synergy
   percentages, status effect tags into our JSON data files instead of
   NLP-parsing description text
3. **Test crew quality** — the 345 pre-set crews serve as ground truth
   test cases for our optimizer
4. **Catch gaps** — identify officers, effects, or mechanics we've missed

Key sheets and what they provide:
- Officer Skills: structured abilities (R1-R5 values), CM values,
  synergy %, effect tags, stat bonuses, BDA data
- Pre-Set Lookup: 345 curated crews tagged by scenario
- RosterIndirect: StewieDoo scores + notes per officer
- Officer Stats: full stat tables per level (1-30)
- Ship Stats: ship ability values per level
- Officer Scores: per-scenario ratings

## What Exists vs What's Needed

### Stage 1 — DATA: ~90% done
Exists: api_client, ingress_server, models.h
Gap: Need to verify event data comes through sync; may need
     to parse event objectives from sync payload

### Stage 2 — MATH: ~30% done
Exists: crew_optimizer (flawed scoring), officer_groups (trimming)
Gaps:
  - crew scoring needs combat model (TODO_OPTIMIZER.md Phase 1)
  - no ship analysis module
  - no research ROI calculation
  - no officer development priority ranking
  - no resource analysis
  - no event objective mapping
  
### Stage 3 — AI SUB-STRATEGY: ~50% done
Exists: ai_crew_engine (group pipeline), crew_advisor (prompts),
        meta_cache (current META), account_state (snapshot builder)
Gaps:
  - only handles crew domain, not the other 5 domains
  - prompt templates needed for ship/research/officer/resource/event
  - sub-strategy result format not standardized

### Stage 4 — USER DECISIONS: ~10% done
Exists: loadout locking in UI, posture selection
Gaps:
  - decisions not stored as structured data
  - no decision model that flows forward
  - UI is coupled to FTXUI (decisions should be UI-agnostic)

### Stage 5 — STRATEGY SYNTHESIS: ~5% done
Exists: planner (generates tasks from templates)
Gaps:
  - planner doesn't consume AI output or user decisions
  - no synthesis prompt
  - no connection between stages 3/4 and the final plan

## Implementation Order

### Phase A: Stabilize Stage 1 + Stage 2
Fix what we have. Make the data layer reliable and the math layer
produce correct, structured output per domain.

1. Audit sync data — confirm what fields we actually receive,
   especially events
2. Define AccountSnapshot as a concrete struct (not ad-hoc)
3. Fix crew scoring (combat model from TODO_OPTIMIZER.md)
4. Build DomainAnalysis structs for each of the 6 domains
5. Implement math analyzers: ship, research, officer, resource, event

### Phase B: Standardize Stage 3
Make sub-strategy queries work consistently across all 6 domains.

1. Define SubStrategyResult as a standard format
2. Build prompt templates per domain
3. Reuse the group pipeline pattern for non-crew domains
4. Each domain: math output → focused prompt → structured AI response

### Phase C: Build Stage 4 + Stage 5
Connect user decisions to strategy synthesis.

1. Define UserDecisions struct
2. Build decision capture (UI-agnostic, stored to disk)
3. Build synthesis prompt (decisions + events + context → plan)
4. Replace template-based planner with AI-driven action plan

### Phase D: UI
Build the UI last, on top of clean BL interfaces.
The UI's only job: present stage outputs and capture user decisions.
