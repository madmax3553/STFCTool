# STFCTool TUI Design — Whiteboard
## Version 0.6 UI Rebuild

### Design Principles
- **Compact for 80x24**: no bordered panels, use color headers + separators for structure
- **Vim-style navigation**: hjkl, g/G, Ctrl+d/u, 1-3 tabs, ? help, q quit
- **Stewiedoo layout logic**: dock-centric crew planning, officer roster with abilities
- **Data-first**: every section shows real synced data, no placeholder text

### Color Palette
```
Cyan        — Accent, headers, selected items
Green       — Active, positive, available, owned
Yellow      — Warning, upcoming, in-progress
Red         — Ended, errors, penalties
Magenta     — Events, special categories
Gray/Dim    — Inactive, unowned, secondary info
White       — Normal text
Gold        — Ranks, scores
```

---

## Tab Structure

### Tab Bar (top, always visible)
```
╭──────────────────────────────────────────────────────────╮
│ [Dashboard]  [Events]  [Officers]  [Ships]  [Sync]       │
╰──────────────────────────────────────────────────────────╯
```
- Active tab: bold + cyan underline
- Inactive: dim
- Tab/Shift+Tab to switch

---

## Tab 1: Dashboard

Three-column layout. Each section is a bordered panel.

```
╭─ Dashboard ──────────────────────────────────────────────────────────────────╮
│                                                                              │
│  Commander: [Name]  Ops 66  Last Sync: 21:26:32                             │
│                                                                              │
│  ╭─ Fleet ─────────────╮ ╭─ Resources ────────╮ ╭─ Events ──────────────╮  │
│  │ 70 ships             │ │ Tritanium    1.2B  │ │ Active:  12  ■■■■■■  │  │
│  │                      │ │ Dilithium    456M  │ │ Upcoming: 8  ■■■■    │  │
│  │ T1 Enterprise  T9 42 │ │ Parsteel     2.3B  │ │ Claimable: 3 !!!     │  │
│  │ T1 Cerritos    T8 40 │ │ Latinum      12.4M │ │                      │  │
│  │ T1 Voyager     T7 38 │ │ Ore          890M  │ │ Ending Soon:         │  │
│  │ T1 Revenant    T7 36 │ │ Crystal      1.1B  │ │  Solo Event  2h 14m  │  │
│  │ T1 Amalgam     T6 35 │ │ Gas          670M  │ │  Alliance    4h 30m  │  │
│  │ +65 more             │ │ +4607 more         │ │  Battle Pass 1d 3h   │  │
│  ╰──────────────────────╯ ╰────────────────────╯ ╰──────────────────────╯  │
│                                                                              │
│  ╭─ Officers ──────────╮ ╭─ Active Jobs ──────╮ ╭─ Research ────────────╮  │
│  │ 290 officers         │ │ Building  2h 14m   │ │ 1424 researched       │  │
│  │ Rank 5: 45           │ │ Research  45m      │ │                       │  │
│  │ Rank 4: 89           │ │ Ship Rep  DONE     │ │ Idle slots: 2         │  │
│  │ Rank 3: 112          │ │                    │ │                       │  │
│  │ Max Lv: 65           │ │ Idle: 4 slots      │ │                       │  │
│  ╰──────────────────────╯ ╰────────────────────╯ ╰───────────────────────╯  │
│                                                                              │
│  ╭─ Sync Status ────────────────────────────────────────────────────────╮   │
│  │ Server: ● RUNNING :8270    Game Data: 289 officers, 186 ships       │   │
│  ╰──────────────────────────────────────────────────────────────────────╯   │
╰──────────────────────────────────────────────────────────────────────────────╯
```

**Key**: Each box is an FTXUI `border()` panel. The layout uses `hbox` of 3 flex panels per row, `vbox` for 2 rows + status bar.

---

## Tab 2: Events

Split: scrollable event list (left 65%) + detail panel (right 35%)

```
╭─ Events ─────────────────────────────────────────────────────────────────────╮
│                                                                              │
│  Active: 12  Upcoming: 8  Claimable: 3    Filter: [All ▼]                   │
│                                                                              │
│  ╭─ Event List ───────────────────────────╮ ╭─ Details ──────────────────╮  │
│  │ State  Category       Time    Score    │ │                            │  │
│  │ ────── ──────────── ─────── ────────── │ │ Solo Event                 │  │
│  │ ● ACT  Solo Event    2h 14m  1,234    │ │                            │  │
│  │ ● ACT  Alliance Task 4h 30m    567    │ │ Schedule                   │  │
│  │ ● ACT  Field Train   6h 12m      0    │ │  Start: 2026-04-18 14:00   │  │
│  │ ● ACT  Loop Museum   8h 45m    890    │ │  End:   2026-04-18 23:00   │  │
│  │ ▲ SOON Battle Pass   1d 3h       -    │ │  Round: 3                  │  │
│  │ ▲ SOON Server Clash  2d 6h       -    │ │                            │  │
│  │ ▲ SOON Meta Event    3d 1h       -    │ │ Progress                   │  │
│  │ ✗ END  Solo Event      -     9,012    │ │  Score: 1,234              │  │
│  │ ✗ END  Alliance       -      2,345    │ │  Rank:  #42                │  │
│  │                                        │ │  Registered: Yes           │  │
│  │ [↑↓] Navigate  [F] Filter             │ │                            │  │
│  ╰────────────────────────────────────────╯ │ Rewards                    │  │
│                                              │  12 tiers                  │  │
│                                              │  Milestone placement       │  │
│                                              ╰────────────────────────────╯  │
╰──────────────────────────────────────────────────────────────────────────────╯
```

**State indicators**: `●` green for active, `▲` yellow for upcoming, `✗` red/dim for ended
**Claimable**: row gets a `!` marker or highlight

---

## Tab 3: Officers

Stewiedoo-inspired: roster list (left) + detail card (right)

```
╭─ Officers ── 290 owned ──────────────────────────────────────────────────────╮
│  Filter: [___________]                                                       │
│                                                                              │
│  ╭─ Roster ──────────────────────────────╮ ╭─ Officer Card ──────────────╮  │
│  │ Name              Lv Rk Shards Class  │ │                             │  │
│  │ ──────────────── ─── ── ────── ────── │ │ ╭───────────────────────╮   │  │
│  │ ■ Kirk              65  5    0 CMD    │ │ │ KIRK                  │   │  │
│  │ ■ Spock             65  5    0 SCI    │ │ │ Legendary • Command   │   │  │
│  │ ■ Uhura             62  5   12 CMD    │ │ │ Discovery Crew        │   │  │
│  │ ■ Bones             60  4   45 SCI    │ │ ╰───────────────────────╯   │  │
│  │ ■ Scotty            58  4   23 ENG    │ │                             │  │
│  │ ■ Sulu              55  4    8 CMD    │ │ Stats (Lv65 Rk5)            │  │
│  │ □ Khan               -  -    - CMD    │ │  Atk: 2,340  Def: 1,890    │  │
│  │ □ Nero               -  -    - CMD    │ │  HP:  3,120                 │  │
│  │                                       │ │                             │  │
│  │ ■ = owned  □ = not owned              │ │ Captain Maneuver            │  │
│  ╰───────────────────────────────────────╯ │  Inspirational              │  │
│                                             │  +15% all crew attack      │  │
│                                             │                             │  │
│                                             │ Officer Ability (Rk5)       │  │
│                                             │  Leadership                 │  │
│                                             │  +12% bridge crew defense   │  │
│                                             ╰─────────────────────────────╯  │
╰──────────────────────────────────────────────────────────────────────────────╯
```

---

## Tab 4: Ships

Similar split: fleet list + ship detail card

```
╭─ Ships ── 70 owned ─────────────────────────────────────────────────────────╮
│  Filter: [___________]                                                       │
│                                                                              │
│  ╭─ Fleet ───────────────────────────────╮ ╭─ Ship Card ─────────────────╮  │
│  │ Name            Tier Lv Type    Grade │ │                             │  │
│  │ ──────────────  ──── ── ──────  ───── │ │ ╭───────────────────────╮   │  │
│  │ ■ Enterprise     T9  42 Explorer  G7  │ │ │ USS ENTERPRISE        │   │  │
│  │ ■ Cerritos       T8  40 Explorer  G6  │ │ │ Explorer • Grade 7    │   │  │
│  │ ■ Revenant       T7  36 Battlshp  G7  │ │ │ Legendary             │   │  │
│  │ ■ Amalgam        T6  35 Survey    G6  │ │ ╰───────────────────────╯   │  │
│  │ ■ Meridian       T5  30 Survey    G5  │ │                             │  │
│  │ □ ISS Jellyfish   -   - Explorer  G7  │ │ Tier: T9/T9  Level: 42/50  │  │
│  │                                       │ │ ████████████████░░░░  84%   │  │
│  │                                       │ │                             │  │
│  ╰───────────────────────────────────────╯ │ Ability: Quantum Drive      │  │
│                                             │  [mining speed]             │  │
│                                             │  Increase mining rate by    │  │
│                                             │  45% for all resources      │  │
│                                             │                             │  │
│                                             │ Crew Slots: 3 bridge + 4   │  │
│                                             ╰─────────────────────────────╯  │
╰──────────────────────────────────────────────────────────────────────────────╯
```

---

## Tab 5: Sync

Compact data browser with sub-view tabs and sync log

```
╭─ Sync ───────────────────────────────────────────────────────────────────────╮
│                                                                              │
│  Server: ● RUNNING :8270              Last Sync: 21:26:32   [S] Toggle       │
│                                                                              │
│  [Officers(290)] [Ships(70)] [Resources(4614)] [Buildings(108)]              │
│  [Research(1424)] [Jobs(1)] [Buffs(2183)] [Events(205)]                      │
│                                                                              │
│  ╭─ Officers ────────────────────────────╮ ╭─ Sync Log ──────────────────╮  │
│  │ Name              Lv Rk Shards  ID    │ │ 21:26 OK officer(290)       │  │
│  │ ──────────────── ─── ── ────── ────── │ │ 21:26 OK ship(70)           │  │
│  │ Kirk              65  5    0  12345   │ │ 21:26 OK resource(4614)     │  │
│  │ Spock             65  5    0  12346   │ │ 21:26 OK platform_event(48) │  │
│  │ Uhura             62  5   12  12347   │ │ 21:25 OK buff(2183)         │  │
│  │ ...                                   │ │ 21:24 OK research(1424)     │  │
│  ╰───────────────────────────────────────╯ ╰──────────────────────────────╯  │
╰──────────────────────────────────────────────────────────────────────────────╯
```

---

## Keybindings (Global — vim-style)

| Key | Action |
|-----|--------|
| 1-3 | Switch to tab N |
| Tab / Shift+Tab | Cycle tabs |
| ? | Toggle help overlay |
| r | Refresh game data from API |
| s | Toggle sync server |
| q | Quit |

## Keybindings (Per-tab — vim-style)

| Key | Tab | Action |
|-----|-----|--------|
| j/k | Events/Sync | Navigate list down/up |
| h/l | Sync | Switch sub-view prev/next |
| g/G | All lists | Jump to top/bottom |
| Ctrl+d/u | All lists | Half-page down/up |
| f | Events | Cycle filter (All/Active/Claimable/Upcoming) |

---

## Implementation Notes

### FTXUI Patterns to Use
- No `border()`/`window()` on content panels — wastes lines at 80x24
- Color headers + `separator()` for visual structure
- `color()` and `bold` for state indicators
- `vscroll_indicator | yframe` for scrollable lists
- `focus` on selected row for auto-scroll
- `hbox({content | flex, separator(), sidebar})` for split layouts
- `size(WIDTH, EQUAL, N)` for fixed-width columns

### Data Flow
1. `AppState` constructor loads cached game_data + player_data
2. `resolve_player_names()` matches IDs to names
3. Tab renders read from `state.player_data` and `state.game_data`
4. Sync server updates `player_data` on incoming sync, triggers UI refresh
5. [R] refreshes game_data from api.spocks.club in background thread

### File Structure
```
src/main.cpp              — AppState + main loop + tab routing (~300 lines)
src/tui/ui_common.h       — Formatting, colors, panel builders
src/tui/tab_dashboard.h   — Dashboard tab
src/tui/tab_events.h      — Events tab
src/tui/tab_officers.h    — Officers tab (future)
src/tui/tab_ships.h       — Ships tab (future)
src/tui/tab_sync.h        — Sync tab
```
