# STFC Crew Optimizer

A Python tool that analyzes your STFC officer roster and generates optimized crew recommendations for different game scenarios.

## Features

- **9 Scenario Optimizations:**
  - PvP (General)
  - PvP (Interceptor)
  - PvP (Explorer)
  - PvP (Battleship)
  - PvE (General)
  - Mission Bosses
  - Armada
  - Rep Grinding
  - Loot Farming

- **Smart Officer Scoring:** Each officer is scored based on:
  - Base stats (Attack, Defense, Health)
  - Officer Ability percentages
  - Special abilities and effects (Hull Breach, Burning, Assimilate, etc.)
  - Whether the officer is marked as "in-use"
  - Scenario-specific bonuses

- **Output Formats:**
  - Console output (immediate, colored)
  - HTML report (visual, shareable)

## Usage

### Basic Usage

```bash
python3 stfc_crew_optimizer.py
```

This will:
1. Load your officer roster from the CSV
2. Analyze all 266 officers
3. Generate recommendations for all 9 scenarios
4. Display results in console
5. Generate an HTML report

### Output

#### Console Output
Shows the top 3 officers for each scenario with:
- Officer name and level
- Attack, Defense, Health stats
- CM% and OA% bonuses
- Ability preview
- Optimization score

#### HTML Report
A visual report saved to `stfc_crew_recommendations.html` with:
- Dark theme UI
- Medal indicators (🥇🥈🥉)
- Complete stats for each officer
- All 9 scenarios

## Understanding the Scores

The scoring algorithm considers:

### PvP (General)
- **High Attack** multiplier (1.5x)
- **Bonus** for control abilities (Breach, Burning, Assimilate)
- **Bonus** if officer is marked as actively used (Y)

### PvP (Interceptor)
- **Highest Attack** multiplier (1.8x)
- **Big bonus** for Breach effect or Interceptor-focused abilities
- **Extra bonus** if actively used

### PvP (Explorer)
- **Balanced** (Attack 1.3x + Defense 0.5x)
- **Bonus** for Morale effects
- **Bonus** if marked as used

### PvP (Battleship)
- **Tanky focus** (Attack 1.2x + Defense 0.8x + Health 0.3x)
- **Bonus** for Battleship-specific abilities

### PvE (General)
- **Balanced** with loot emphasis
- **2.4x bonus** for officers with loot/mining/resource abilities

### Mission Bosses
- **Balanced** (Attack 1.0x + Defense 1.0x + Health 0.5x)
- **Bonus** for penetration/accuracy abilities

### Armada
- **Burst damage** (Attack 1.3x + Health 0.3x)
- **3x bonus** if abilities mention Armada
- **1.5x bonus** for crit-focused abilities

### Rep Grinding
- **Resource focus** with 2.0x bonus for reputation abilities
- **1.3x bonus** for speed/warp abilities
- **Low attack/defense weighting**

### Loot Farming
- **Resource maximization** (2.5x bonus)
- Favors cargo, mining, protected cargo, protected abilities
- High defense weighting for sustained farming

## Your Current Best Officers

### Universal Top Tier
- **Borg Queen** - Dominates PvP, Armada, and Mission Bosses
- **Khan** - Strong general PvP and Armada officer
- **Benjamin Sisko** - Great for Armada
- **PIC Hugh** - Excellent for PvE and Loot farming

### Scenario-Specific Stars
- **Gorkon** - Best for PvP Interceptor (2nd slot)
- **Five Of Eleven** - Best for Mission Bosses defense
- **Mavery** - Great for Mining/PvE
- **Ghrush** - Top for Rep Grinding and Loot

## Comparing to Your Loss

You lost with:
- Mirror Picard (36,568 ATK)
- Mirror Ezri (9,852 ATK)
- Mirror Troi (9,852 ATK)

Opponent had:
- Gul Dukat (41,138 ATK) - **+12.5% attack**
- Andy Billups (8,813 ATK)
- Garak (9,094 ATK)

**Recommendation:** Switch to recommended PvP crew instead of Mirror officers. Your mirror officers are PvE-focused but weak for player combat.

## Tips

1. **Always Update CSV** - Re-export your roster from STFC when you level/unlock new officers
2. **Check Officer Effects** - Look at the "Abilities" column for synergies
3. **Use Scenario Reports** - Different scenarios have completely different optimal crews
4. **Monitor Updates** - STFC balance changes may shift recommendations
5. **Build Context** - Some officers work better on specific ship types

## File Structure

```
stfc_crew_optimizer.py          # Main optimizer script
stfc_crew_recommendations.html  # Generated visual report
STFC_CREW_OPTIMIZER_README.md   # This file
Copy of 1.8.M84...Roster.csv    # Your officer roster (CSV input)
```

## Requirements

- Python 3.6+
- CSV file with your STFC officer roster

No external dependencies!

## FAQ

**Q: How often should I update this?**
A: Every time you level officers or unlock new ones. Re-export your roster from STFC and re-run the tool.

**Q: Why is Officer X scoring low for PvP?**
A: They likely lack attack stats or control abilities (Breach, Burning, Assimilate). PvP favors burst damage and CC.

**Q: Can I modify the scoring?**
A: Yes! Edit the `score_officer()` method in the Python script to adjust multipliers for your preferences.

**Q: What if I disagree with the rankings?**
A: The tool provides suggestions. Factor in:
- Your ship type
- Your opponent's build
- Officer synergies not shown in raw stats
- Personal playstyle

**Q: Should I follow these exactly?**
A: Use as a guide, but meta changes and personal preference matter. Test recommended crews before committing.
