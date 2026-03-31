# Optimizer Scoring Rewrite — TODO

## Problem Statement

The scoring model has 131 individual bonus additions and 40 crew-level additions.
Every mechanic gets an arbitrary point value (+8000 for this, +12000 for that) and
the optimizer picks whichever crew accumulates the most points. This is not
"calculating the best crew" — it's a pile of hand-tuned magic numbers that will
always produce surprising results when the numbers interact.

The optimizer should **calculate** which crew wins a fight, not assign points to
traits and hope the sum produces the right answer.

## Phase 1: Replace Point Accumulation with Calculated Outcomes

### 1.1 — Build a Simplified Combat Model
- Model a PvP fight as N rounds of: attack → mitigation → damage → state effects
- Each officer contributes stats (attack/defense/health) plus ability modifiers
- The "score" of a crew is the **expected damage output minus expected damage taken**
  over a representative fight (e.g., 8 rounds)
- This replaces all the individual +8000/+12000 bonuses with actual math

### 1.2 — Mitigation Delta as a Calculation, Not a Bonus
- Current: `if (off.armor_piercing) ability_score += 12000` — arbitrary
- Target: Calculate effective mitigation % for opponent (baseline 71.2% cap),
  then calculate how much piercing/accuracy reduces it. The damage difference
  IS the score contribution — no magic number needed
- Piercing officers naturally score higher because they produce more damage

### 1.3 — CM + Synergy as a Multiplier, Not Additive Bonus
- Current: CM bonus + flat synergy incentive added to total — competes with
  other additive bonuses and the relative weight is hand-tuned
- Target: CM is a **multiplier** on the crew's combat output. Synergy multiplies
  the CM. The result is naturally weighted by how much the CM actually helps.
  A 70% AllStats CM with 40% synergy literally produces 1.98x stats — that
  should dominate because the math says so, not because we assigned it 120K points

### 1.4 — State Effects as Damage Modifiers
- Current: +50K for burning, +30K for morale, +8K for state application — arbitrary
- Target: Model what each state actually does:
  - Burning: X% damage per round for Y rounds = calculable total damage
  - Breach: reduces opponent armor by Z% = calculable mitigation reduction
  - Morale: reduces opponent attack by W% = calculable damage reduction
- The score contribution comes from the combat model, not a lookup table

### 1.5 — Proc Reliability as Expected Value
- Current: `if (off.proc_guaranteed) ability_score += 8000` — arbitrary
- Target: Multiply state effect value by proc probability.
  Guaranteed proc = 1.0x, 50% chance = 0.5x. The math handles it.

## Phase 2: Simplify Crew-Level Scoring

### 2.1 — Remove Archetype Detection Bonuses
- Current: +50K for "apex barrier crew", +60K for "apex shred crew", etc.
- Target: These archetypes should emerge from the combat model. An apex barrier
  crew scores well because the barrier absorbs damage in the simulation. We
  don't need to separately detect and reward the archetype.

### 2.2 — Remove Coverage Bonuses
- Current: +60K for "piercing coverage", +30K for "crit coverage", etc.
- Target: If 2 officers provide piercing, the combat model already reflects
  the increased damage from reduced mitigation. Double-counting with a
  coverage bonus inflates the score.

### 2.3 — State Chain as Combat Model Output
- Current: Separate `apply_state_chain()` with per-state values and beneficiary
  multipliers and proc reliability multipliers
- Target: The combat model applies states round-by-round. If officer A applies
  burning and officer B benefits from burning, that shows up as increased damage
  in the simulation. No separate chain bonus needed.

### 2.4 — Coherence / Spread Penalties as Natural Outcomes
- Current: +40K for "full coherence on burning", -10% for "state spread"
- Target: A focused crew naturally scores better in the combat model because
  all abilities reinforce the same damage path. An unfocused crew wastes
  ability slots on states nobody benefits from — the model sees lower output.

## Phase 3: Keep What Works, Remove What Doesn't

### Things to Keep
- **Synergy group/class mechanics** — these are game rules, not scoring opinions
- **Ship-lock detection** — an ability that doesn't work on your ship is worth 0
- **BDA captain penalty** — a captain with no CM is objectively worse
- **Mining scenario scoring** — mining is non-combat, the additive model is fine
- **Non-mining officer penalty in mining** — these officers literally don't help
- **CM scope classification** — knowing what a CM does is prerequisite to modeling it

### Things to Remove (Once Combat Model Exists)
- All hardcoded point values for traits (+8000, +12000, etc.)
- Coverage bonuses (piercing, crit, ship-type)
- Archetype detection bonuses (apex barrier crew, generalist crew)
- State chain / coherence as separate scoring functions
- State value differentiation tables (burning=50K, morale=30K)
- Proc reliability as flat bonus (should be expected value multiplier)

## Phase 4: Data Pipeline Improvements

### 4.1 — Use API `chance` Field
- `AbilityValue::chance` exists in models.h but is never used
- This is the proc probability — feed it into the combat model

### 4.2 — Extract Numeric Ability Values
- Many abilities have specific percentages ("increase attack by 30%")
- Parse these into structured data instead of pattern-matching keywords
- The combat model needs numbers, not booleans

### 4.3 — Opponent Modeling
- Current: scores a crew in isolation
- Target: score a crew against a reference opponent (average stats, average
  mitigation, etc.) so the combat model has something to fight against

## Outstanding Non-Scoring Issues

- Inspect live sync + Spocks merged officer records and identify why resolved
  CM/OA semantics are still missing in optimizer input
- Fix merged roster construction so resolved Spocks tooltip text and ability
  values populate optimizer officer descriptions reliably
- Add clearer user-facing score breakdown for ship, crew, synergy, scenario,
  and BDA contributions
