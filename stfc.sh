#!/usr/bin/env bash
# ============================================================================
# STFC Crew Optimizer — Interactive Dialog Launcher
# ============================================================================
# Uses 'dialog' for ncurses-style menus with interactive drill-down.
#
# Features:
#   - Run PvP / Hybrid crew optimization
#   - Drill into individual crews: score breakdown, synergies, penalties
#   - Below Deck Ability (BDA) suggestions for each crew
#   - Improvement analysis: what to level up, missing coverage, alt captains
#   - Officer browser: search/filter the full 266-officer roster
#
# Usage:
#   ./stfc.sh              # Launch interactive menu
#   ./stfc.sh --quick      # Skip menus, run with last-used settings
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_SCRIPT="${SCRIPT_DIR}/stfc_crew_optimizer.py"
RESULTS_FILE="${SCRIPT_DIR}/.stfc_last_results.txt"
JSON_FILE="${SCRIPT_DIR}/.stfc_last_results.json"
SETTINGS_FILE="${SCRIPT_DIR}/.stfc_settings"
TMPFILE="${SCRIPT_DIR}/.stfc_tmp_dlg.txt"
LOADOUT_FILE="${SCRIPT_DIR}/.stfc_loadout.json"

# Cleanup trap is set below with the dark theme setup

# ----------------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------------
if command -v dialog &>/dev/null; then
    DLG="dialog"
elif command -v whiptail &>/dev/null; then
    DLG="whiptail"
else
    echo "ERROR: Neither 'dialog' nor 'whiptail' found."
    echo "Install with: sudo apt install dialog"
    exit 1
fi

if ! command -v jq &>/dev/null; then
    echo "ERROR: 'jq' not found. Required for interactive menus."
    echo "Install with: sudo apt install jq"
    exit 1
fi

# Dialog dimensions (auto-detect terminal size)
TERM_HEIGHT=$(tput lines 2>/dev/null || echo 24)
TERM_WIDTH=$(tput cols 2>/dev/null || echo 80)
DLG_HEIGHT=$((TERM_HEIGHT - 4))
DLG_WIDTH=$((TERM_WIDTH - 4))
[ "$DLG_HEIGHT" -gt 45 ] && DLG_HEIGHT=45
[ "$DLG_WIDTH" -gt 110 ] && DLG_WIDTH=110
LIST_HEIGHT=$((DLG_HEIGHT - 8))

# Dark theme for dialog (matches repostatus style)
DIALOG_THEME=$(mktemp)
cat > "$DIALOG_THEME" << 'EOF'
# Dark theme for dialog - minimal bright elements
aspect = 0
separate_widget = ""
tab_len = 0
visit_items = OFF
use_shadow = ON
use_colors = ON

# Main screen - cyan text, terminal default background, NOT bold
screen_color = (CYAN,DEFAULT,OFF)
shadow_color = (DEFAULT,DEFAULT,OFF)

# Dialog box - cyan text, default bg, NOT bold to avoid white
dialog_color = (CYAN,DEFAULT,OFF)
title_color = (CYAN,DEFAULT,ON)
border_color = (CYAN,DEFAULT,OFF)
border2_color = (CYAN,DEFAULT,OFF)

# Buttons - inverted cyan when active
button_active_color = (BLACK,CYAN,OFF)
button_inactive_color = (CYAN,DEFAULT,OFF)
button_key_active_color = (BLACK,CYAN,OFF)
button_key_inactive_color = (CYAN,DEFAULT,OFF)
button_label_active_color = (BLACK,CYAN,OFF)
button_label_inactive_color = (CYAN,DEFAULT,OFF)

# Input/search boxes
inputbox_color = (CYAN,DEFAULT,OFF)
inputbox_border_color = (CYAN,DEFAULT,OFF)
inputbox_border2_color = (CYAN,DEFAULT,OFF)
searchbox_color = (CYAN,DEFAULT,OFF)
searchbox_title_color = (CYAN,DEFAULT,ON)
searchbox_border_color = (CYAN,DEFAULT,OFF)
searchbox_border2_color = (CYAN,DEFAULT,OFF)

# Menu
menubox_color = (CYAN,DEFAULT,OFF)
menubox_border_color = (CYAN,DEFAULT,OFF)
menubox_border2_color = (CYAN,DEFAULT,OFF)
item_color = (CYAN,DEFAULT,OFF)
item_selected_color = (BLACK,CYAN,OFF)

# Tags (the numbers/letters in menu)
tag_color = (CYAN,DEFAULT,OFF)
tag_selected_color = (BLACK,CYAN,OFF)
tag_key_color = (CYAN,DEFAULT,OFF)
tag_key_selected_color = (BLACK,CYAN,OFF)

# Checkboxes
check_color = (CYAN,DEFAULT,OFF)
check_selected_color = (BLACK,CYAN,OFF)

# Scroll arrows
uarrow_color = (CYAN,DEFAULT,OFF)
darrow_color = (CYAN,DEFAULT,OFF)

# Position indicator
position_indicator_color = (CYAN,DEFAULT,OFF)

# Help text
itemhelp_color = (CYAN,DEFAULT,OFF)

# Forms
form_active_text_color = (BLACK,CYAN,OFF)
form_text_color = (CYAN,DEFAULT,OFF)
form_item_readonly_color = (CYAN,DEFAULT,OFF)

# Gauge
gauge_color = (CYAN,DEFAULT,OFF)
EOF
export DIALOGRC="$DIALOG_THEME"

# Cleanup on exit (include theme tempfile)
trap 'rm -f "$TMPFILE" "$DIALOG_THEME"' EXIT

# ----------------------------------------------------------------------------
# Load/save settings
# ----------------------------------------------------------------------------
load_settings() {
    if [ -f "$SETTINGS_FILE" ]; then
        source "$SETTINGS_FILE"
    fi
    SHIP="${SHIP:-explorer}"
    MODE="${MODE:-both}"
    TOP_N="${TOP_N:-5}"
    SKIP_BATTLES="${SKIP_BATTLES:-yes}"
    SKIP_LEGACY="${SKIP_LEGACY:-yes}"
}

save_settings() {
    cat > "$SETTINGS_FILE" << EOF
SHIP="${SHIP}"
MODE="${MODE}"
TOP_N="${TOP_N}"
SKIP_BATTLES="${SKIP_BATTLES}"
SKIP_LEGACY="${SKIP_LEGACY}"
EOF
}

ship_display_name() {
    case "$1" in
        explorer)     echo "U.S.S. Enterprise-D (Explorer)" ;;
        battleship)   echo "Negh'Var (T8 Battleship)" ;;
        interceptor)  echo "Interceptor" ;;
        *)            echo "$1" ;;
    esac
}

ship_short_name() {
    case "$1" in
        explorer)     echo "Enterprise-D" ;;
        battleship)   echo "Negh'Var" ;;
        interceptor)  echo "Interceptor" ;;
        *)            echo "$1" ;;
    esac
}

# ----------------------------------------------------------------------------
# Settings dialogs (unchanged from before)
# ----------------------------------------------------------------------------
select_ship() {
    local de="off" db="off" di="off"
    case "$SHIP" in
        explorer)     de="on" ;;
        battleship)   db="on" ;;
        interceptor)  di="on" ;;
    esac

    local result
    result=$("$DLG" --title " SELECT SHIP " \
        --radiolist "\nChoose your ship class:\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "explorer"     "U.S.S. Enterprise-D  (Galaxy Class Explorer)"  "$de" \
        "battleship"   "Negh'Var             (T8 Battleship)"          "$db" \
        "interceptor"  "Interceptor          (Interceptor Class)"      "$di" \
        3>&1 1>&2 2>&3) || return 1
    SHIP="$result"
}

select_mode() {
    local d_both="off" d_pvp="off" d_hybrid="off" d_bc="off" d_pve="off"
    local d_boss="off" d_loot="off" d_armada="off" d_all="off"
    case "$MODE" in
        pvp)          d_pvp="on" ;;
        hybrid)       d_hybrid="on" ;;
        both)         d_both="on" ;;
        base_cracker) d_bc="on" ;;
        pve_hostile)  d_pve="on" ;;
        mission_boss) d_boss="on" ;;
        loot)         d_loot="on" ;;
        armada)       d_armada="on" ;;
        all)          d_all="on" ;;
    esac

    local result
    result=$("$DLG" --title " SELECT MODE " \
        --radiolist "\nChoose optimization mode:\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "both"         "PvP + Hybrid          (Full PvP analysis)"               "$d_both" \
        "pvp"          "PvP Only              (Pure player combat)"              "$d_pvp" \
        "hybrid"       "Hybrid PvE/PvP        (Grind + survive attacks)"        "$d_hybrid" \
        "base_cracker" "Base Cracker          (Station attack)"                 "$d_bc" \
        "pve_hostile"  "PvE Hostiles          (Hostile grinding)"               "$d_pve" \
        "mission_boss" "Mission Boss          (Story/event boss fights)"        "$d_boss" \
        "loot"         "Loot / Resources      (Mining, cargo, gathering)"       "$d_loot" \
        "armada"       "Armada                (Co-op armada bosses)"            "$d_armada" \
        "all"          "ALL Scenarios          (Run everything)"                "$d_all" \
        3>&1 1>&2 2>&3) || return 1
    MODE="$result"
}

select_top_n() {
    local result
    result=$("$DLG" --title " NUMBER OF RESULTS " \
        --inputbox "\nHow many top crew recommendations?\n" \
        10 50 "$TOP_N" \
        3>&1 1>&2 2>&3) || return 1

    if [[ "$result" =~ ^[0-9]+$ ]] && [ "$result" -ge 1 ] && [ "$result" -le 50 ]; then
        TOP_N="$result"
    else
        "$DLG" --title " ERROR " --msgbox "\nInvalid number. Must be 1-50.\nKeeping previous value: $TOP_N" 8 45
    fi
}

select_options() {
    local bf="off" lf="off"
    [ "$SKIP_BATTLES" = "no" ] && bf="on"
    [ "$SKIP_LEGACY" = "no" ] && lf="on"

    local result
    result=$("$DLG" --title " OUTPUT OPTIONS " \
        --checklist "\nSelect what to include in output:\n(Space to toggle, Enter to confirm)\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "battles"  "Detailed battle log analysis"          "$bf" \
        "legacy"   "Legacy per-scenario recommendations"   "$lf" \
        3>&1 1>&2 2>&3) || return 1

    SKIP_BATTLES="yes"
    SKIP_LEGACY="yes"
    echo "$result" | grep -q "battles" && SKIP_BATTLES="no"
    echo "$result" | grep -q "legacy" && SKIP_LEGACY="no"
}

confirm_and_run() {
    "$DLG" --title " CONFIRM SETTINGS " \
        --yesno "\n\
  Ship:      $(ship_display_name "$SHIP")\n\
  Mode:      $MODE\n\
  Top:       $TOP_N crews\n\
\n\
  Run the optimizer with these settings?" \
        13 55 || return 1
}

# ============================================================================
# CORE: Run optimizer and get JSON
# ============================================================================
run_optimizer_json() {
    local cmd_args=("--json" "crews" "--ship" "$SHIP" "--mode" "$MODE" "--top" "$TOP_N")

    "$DLG" --title " OPTIMIZING... " \
        --infobox "\n\
  Running STFC Crew Optimizer...\n\
\n\
  Ship: $(ship_display_name "$SHIP")\n\
  Mode: $MODE | Top $TOP_N\n\
\n\
  Analyzing 266 officers in all 3-officer combinations.\n\
  This takes a few seconds...\n" \
        12 60

    # Run optimizer — JSON to stdout, progress to stderr
    if ! python3 "$PYTHON_SCRIPT" "${cmd_args[@]}" > "$JSON_FILE" 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nOptimizer failed. Check Python script." 8 50
        return 1
    fi

    # Validate JSON
    if ! jq empty "$JSON_FILE" 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nInvalid JSON output from optimizer." 8 50
        return 1
    fi

    # Also run text output for "View Raw" option
    local txt_args=("--ship" "$SHIP" "--mode" "$MODE" "--top" "$TOP_N" "--skip-battles" "--skip-legacy")
    python3 "$PYTHON_SCRIPT" "${txt_args[@]}" > "$RESULTS_FILE" 2>&1 || true
}

# ============================================================================
# CREW LIST: Pick a crew to drill into
# ============================================================================
show_crew_list() {
    local scenario_key="$1"  # e.g. "pvp", "hybrid", "base_cracker", etc.
    local mode_label="$2"    # Display label like "PvP", "Hybrid", etc.

    # Read crews from .scenarios.<key>.crews (new structure)
    # Fall back to .<key>_crews for backward compat
    local jq_path
    if jq -e ".scenarios.${scenario_key}.crews" "$JSON_FILE" > /dev/null 2>&1; then
        jq_path=".scenarios.${scenario_key}.crews"
    elif jq -e ".${scenario_key}_crews" "$JSON_FILE" > /dev/null 2>&1; then
        jq_path=".${scenario_key}_crews"
    else
        "$DLG" --title " NO RESULTS " --msgbox "\nNo $mode_label crews found." 8 40
        return
    fi

    local crew_count
    crew_count=$(jq -r "$jq_path | length" "$JSON_FILE" 2>/dev/null)
    if [ -z "$crew_count" ] || [ "$crew_count" = "null" ] || [ "$crew_count" -eq 0 ]; then
        "$DLG" --title " NO RESULTS " --msgbox "\nNo $mode_label crews found." 8 40
        return
    fi

    while true; do
        # Build menu items from JSON
        local menu_args=()
        local i
        for ((i = 0; i < crew_count; i++)); do
            local rank score captain b1 b2
            rank=$(jq -r "${jq_path}[$i].rank" "$JSON_FILE")
            score=$(jq -r "${jq_path}[$i].score" "$JSON_FILE")
            captain=$(jq -r "${jq_path}[$i].captain.name" "$JSON_FILE")
            b1=$(jq -r "${jq_path}[$i].bridge[0].name" "$JSON_FILE")
            b2=$(jq -r "${jq_path}[$i].bridge[1].name" "$JSON_FILE")

            # Format score with commas
            local score_fmt
            score_fmt=$(printf "%'d" "$score" 2>/dev/null || echo "$score")

            menu_args+=("$i" "#${rank} ${captain} / ${b1} / ${b2}  [${score_fmt}]")
        done

        local choice
        choice=$("$DLG" --title " TOP $mode_label CREWS — $(ship_short_name "$SHIP") " \
            --cancel-label "Back" \
            --menu "\nSelect a crew to drill into:\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "${menu_args[@]}" \
            3>&1 1>&2 2>&3) || break

        # Drill into selected crew
        drill_into_crew "$jq_path" "$choice" "$mode_label" "$scenario_key"
    done
}

# ============================================================================
# DRILL-DOWN: Detailed view of a single crew
# ============================================================================
drill_into_crew() {
    local jq_path="$1"       # e.g. ".scenarios.pvp.crews"
    local idx="$2"
    local mode_label="$3"
    local scenario_key="$4"  # e.g. "pvp", "base_cracker"

    local captain b1 b2
    captain=$(jq -r "${jq_path}[$idx].captain.name" "$JSON_FILE")
    b1=$(jq -r "${jq_path}[$idx].bridge[0].name" "$JSON_FILE")
    b2=$(jq -r "${jq_path}[$idx].bridge[1].name" "$JSON_FILE")
    local crew_str="${captain}|${b1}|${b2}"

    while true; do
        local choice
        choice=$("$DLG" --title " ${captain} / ${b1} / ${b2} " \
            --cancel-label "Back" \
            --menu "\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "1" "Score Breakdown & Officer Details" \
            "2" "Below Deck (BDA) Suggestions" \
            "3" "How to Improve This Crew" \
            "4" "View Raw Score Data" \
            3>&1 1>&2 2>&3) || break

        case "$choice" in
            1) show_crew_details "$jq_path" "$idx" ;;
            2) show_bda_suggestions "$crew_str" "$scenario_key" ;;
            3) show_improvement_analysis "$crew_str" "$scenario_key" ;;
            4) show_raw_crew_json "$jq_path" "$idx" ;;
        esac
    done
}

# ============================================================================
# DETAILS: Full officer stats + score breakdown
# ============================================================================
show_crew_details() {
    local jq_path="$1"
    local idx="$2"

    local crew
    crew=$(jq "${jq_path}[$idx]" "$JSON_FILE")

    # Build the details text
    {
        local score rank
        score=$(echo "$crew" | jq -r '.score')
        rank=$(echo "$crew" | jq -r '.rank')
        local score_fmt
        score_fmt=$(printf "%'d" "$score" 2>/dev/null || echo "$score")

        echo "═══════════════════════════════════════════════════════════════════"
        echo "  CREW #${rank}  —  Score: ${score_fmt}"
        echo "═══════════════════════════════════════════════════════════════════"

        # Captain
        echo ""
        echo "  CAPTAIN: $(echo "$crew" | jq -r '.captain.name')"
        echo "  ─────────────────────────────────────────────────────────────"
        local clvl crnk catk cdef chp ccm
        clvl=$(echo "$crew" | jq -r '.captain.level')
        crnk=$(echo "$crew" | jq -r '.captain.rank')
        catk=$(echo "$crew" | jq -r '.captain.attack' | xargs printf "%'.0f")
        cdef=$(echo "$crew" | jq -r '.captain.defense' | xargs printf "%'.0f")
        chp=$(echo "$crew" | jq -r '.captain.health' | xargs printf "%'.0f")
        ccm=$(echo "$crew" | jq -r '.captain.cm_pct')
        echo "    Level $clvl | Rank $crnk | CM ${ccm}%"
        echo "    ATK $catk | DEF $cdef | HP $chp"
        local cdisplay
        cdisplay=$(echo "$crew" | jq -r '.captain.display')
        echo ""
        echo "    Abilities:"
        # Word-wrap at ~65 chars
        echo "    $cdisplay" | fold -s -w 65 | sed 's/^/    /'

        # Bridge officers
        local bi
        for bi in 0 1; do
            echo ""
            echo "  BRIDGE $((bi + 1)): $(echo "$crew" | jq -r ".bridge[$bi].name")"
            echo "  ─────────────────────────────────────────────────────────────"
            local blvl brnk batk bdef bhp boa
            blvl=$(echo "$crew" | jq -r ".bridge[$bi].level")
            brnk=$(echo "$crew" | jq -r ".bridge[$bi].rank")
            batk=$(echo "$crew" | jq -r ".bridge[$bi].attack" | xargs printf "%'.0f")
            bdef=$(echo "$crew" | jq -r ".bridge[$bi].defense" | xargs printf "%'.0f")
            bhp=$(echo "$crew" | jq -r ".bridge[$bi].health" | xargs printf "%'.0f")
            boa=$(echo "$crew" | jq -r ".bridge[$bi].oa_pct")
            echo "    Level $blvl | Rank $brnk | OA ${boa}%"
            echo "    ATK $batk | DEF $bdef | HP $bhp"
            local bdisplay
            bdisplay=$(echo "$crew" | jq -r ".bridge[$bi].display")
            echo ""
            echo "    Abilities:"
            echo "    $bdisplay" | fold -s -w 65 | sed 's/^/    /'
        done

        # Synergy notes
        local syn_count
        syn_count=$(echo "$crew" | jq '.synergy_notes | length')
        if [ "$syn_count" -gt 0 ]; then
            echo ""
            echo "  SYNERGIES:"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "$crew" | jq -r '.synergy_notes[]' | while read -r note; do
                echo "    + $note"
            done
        fi

        # Penalties
        local pen_count
        pen_count=$(echo "$crew" | jq '.penalties | length')
        if [ "$pen_count" -gt 0 ]; then
            echo ""
            echo "  WARNINGS:"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "$crew" | jq -r '.penalties[]' | while read -r pen; do
                echo "    - $pen"
            done
        fi

        # Score breakdown
        echo ""
        echo "  SCORE BREAKDOWN:"
        echo "  ─────────────────────────────────────────────────────────────"
        echo "$crew" | jq -r '.individual_scores | to_entries[] | "    \(.key): \(.value)"'

        local scb syb crb stb wcb dub amb snb
        scb=$(echo "$crew" | jq -r '.state_chain_bonus // 0')
        syb=$(echo "$crew" | jq -r '.synergy_bonus // 0')
        crb=$(echo "$crew" | jq -r '.crit_bonus // 0')
        stb=$(echo "$crew" | jq -r '.ship_type_bonus // 0')
        wcb=$(echo "$crew" | jq -r '.weakness_counter_bonus // 0')
        dub=$(echo "$crew" | jq -r '.dual_use_bonus // 0')
        amb=$(echo "$crew" | jq -r '.amplifier_bonus // 0')
        snb=$(echo "$crew" | jq -r '.scenario_bonus // 0')

        [ "$scb" != "0" ] && printf "    %-35s %'d\n" "State chain bonus:" "$scb"
        [ "$syb" != "0" ] && printf "    %-35s %'d\n" "Synergy coherence:" "$syb"
        [ "$crb" != "0" ] && printf "    %-35s %'d\n" "Crit coverage:" "$crb"
        [ "$stb" != "0" ] && printf "    %-35s %'d\n" "Ship specialization:" "$stb"
        [ "$wcb" != "0" ] && printf "    %-35s %'d\n" "Weakness counter:" "$wcb"
        [ "$dub" != "0" ] && printf "    %-35s %'d\n" "Dual-use bonus:" "$dub"
        [ "$amb" != "0" ] && printf "    %-35s %'d\n" "Amplifier bonus:" "$amb"
        [ "$snb" != "0" ] && printf "    %-35s %'d\n" "Scenario specialization:" "$snb"

        echo ""
    } > "$TMPFILE"

    "$DLG" --title " CREW DETAILS " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# BDA SUGGESTIONS: Best below-deck officers for this crew
# ============================================================================
show_bda_suggestions() {
    local crew_str="$1"
    local scenario_key="$2"  # e.g. "pvp", "base_cracker", etc.
    local mode_arg="$scenario_key"

    "$DLG" --title " LOADING... " \
        --infobox "\n  Finding best Below Deck officers for:\n  ${crew_str//|/ / }\n" 8 60

    local bda_json
    bda_json=$(python3 "$PYTHON_SCRIPT" --json bda --ship "$SHIP" --mode "$mode_arg" \
        --top 8 --crew "$crew_str" 2>/dev/null)

    if ! echo "$bda_json" | jq empty 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nFailed to get BDA suggestions." 8 45
        return
    fi

    local err
    err=$(echo "$bda_json" | jq -r '.error // empty')
    if [ -n "$err" ]; then
        "$DLG" --title " ERROR " --msgbox "\n$err" 8 60
        return
    fi

    local count
    count=$(echo "$bda_json" | jq '.bda_suggestions | length')

    while true; do
        # Build menu
        local menu_args=()
        local i
        for ((i = 0; i < count; i++)); do
            local name score reasons_str
            name=$(echo "$bda_json" | jq -r ".bda_suggestions[$i].name")
            score=$(echo "$bda_json" | jq -r ".bda_suggestions[$i].score")
            local score_fmt
            score_fmt=$(printf "%'d" "$score" 2>/dev/null || echo "$score")
            # First reason as summary
            reasons_str=$(echo "$bda_json" | jq -r ".bda_suggestions[$i].reasons[0] // \"\"")
            menu_args+=("$i" "${name}  [${score_fmt}]  ${reasons_str:0:40}")
        done

        local choice
        choice=$("$DLG" --title " BELOW DECK SUGGESTIONS — ${crew_str//|/ / } " \
            --cancel-label "Back" \
            --menu "\nBest officers to place Below Deck with this crew:\n(Their passive OA fires from below deck)\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "${menu_args[@]}" \
            3>&1 1>&2 2>&3) || break

        # Show BDA officer details
        show_bda_detail "$bda_json" "$choice"
    done
}

show_bda_detail() {
    local bda_json="$1"
    local idx="$2"

    local off
    off=$(echo "$bda_json" | jq ".bda_suggestions[$idx]")

    {
        local name level rnk atk def hp oa score
        name=$(echo "$off" | jq -r '.name')
        level=$(echo "$off" | jq -r '.level')
        rnk=$(echo "$off" | jq -r '.rank')
        atk=$(echo "$off" | jq -r '.attack' | xargs printf "%'.0f")
        def=$(echo "$off" | jq -r '.defense' | xargs printf "%'.0f")
        hp=$(echo "$off" | jq -r '.health' | xargs printf "%'.0f")
        oa=$(echo "$off" | jq -r '.oa_pct')
        score=$(echo "$off" | jq -r '.score')
        local score_fmt
        score_fmt=$(printf "%'d" "$score" 2>/dev/null || echo "$score")

        echo "═══════════════════════════════════════════════════════════════════"
        echo "  BELOW DECK: $name"
        echo "═══════════════════════════════════════════════════════════════════"
        echo ""
        echo "    Level $level | Rank $rnk | OA ${oa}%"
        echo "    ATK $atk | DEF $def | HP $hp"
        echo "    BDA Score: $score_fmt"
        echo ""

        local display
        display=$(echo "$off" | jq -r '.display')
        echo "  ABILITIES:"
        echo "  ─────────────────────────────────────────────────────────────"
        echo "    $display" | fold -s -w 65 | sed 's/^/    /'
        echo ""

        echo "  WHY THIS OFFICER:"
        echo "  ─────────────────────────────────────────────────────────────"
        echo "$off" | jq -r '.reasons[]' | while read -r reason; do
            echo "    + $reason"
        done
        echo ""
    } > "$TMPFILE"

    "$DLG" --title " BDA: $(echo "$off" | jq -r '.name') " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# IMPROVEMENT ANALYSIS
# ============================================================================
show_improvement_analysis() {
    local crew_str="$1"
    local scenario_key="$2"  # e.g. "pvp", "base_cracker", etc.
    local mode_arg="$scenario_key"

    "$DLG" --title " ANALYZING... " \
        --infobox "\n  Running improvement analysis for:\n  ${crew_str//|/ / }\n" 8 60

    local imp_json
    imp_json=$(python3 "$PYTHON_SCRIPT" --json improve --ship "$SHIP" --mode "$mode_arg" \
        --crew "$crew_str" 2>/dev/null)

    if ! echo "$imp_json" | jq empty 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nFailed to run improvement analysis." 8 50
        return
    fi

    local err
    err=$(echo "$imp_json" | jq -r '.error // empty')
    if [ -n "$err" ]; then
        "$DLG" --title " ERROR " --msgbox "\n$err" 8 60
        return
    fi

    {
        local cscore
        cscore=$(echo "$imp_json" | jq -r '.current_score')
        local cscore_fmt
        cscore_fmt=$(printf "%'d" "$cscore" 2>/dev/null || echo "$cscore")

        echo "═══════════════════════════════════════════════════════════════════"
        echo "  HOW TO IMPROVE: ${crew_str//|/ / }"
        echo "  Current Score: $cscore_fmt"
        echo "═══════════════════════════════════════════════════════════════════"

        # Level-up gains
        local lu_count
        lu_count=$(echo "$imp_json" | jq '.level_up_gains | length')
        if [ "$lu_count" -gt 0 ]; then
            echo ""
            echo "  LEVEL UP PRIORITIES:"
            echo "  ─────────────────────────────────────────────────────────────"
            local i
            for ((i = 0; i < lu_count; i++)); do
                local advice
                advice=$(echo "$imp_json" | jq -r ".level_up_gains[$i].advice")
                echo "    $((i + 1)). $advice"
            done
        else
            echo ""
            echo "  All officers are near max level — no level-up gains."
        fi

        # Missing coverage
        local mc_count
        mc_count=$(echo "$imp_json" | jq '.missing_coverage | length')
        if [ "$mc_count" -gt 0 ]; then
            echo ""
            echo "  MISSING COVERAGE:"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "$imp_json" | jq -r '.missing_coverage[]' | while read -r gap; do
                echo "    ! $gap"
            done
        else
            echo ""
            echo "  No major coverage gaps detected."
        fi

        # Better captains
        local bc_count
        bc_count=$(echo "$imp_json" | jq '.better_captains | length')
        if [ "$bc_count" -gt 0 ]; then
            echo ""
            echo "  ALTERNATIVE CAPTAINS (>5% improvement):"
            echo "  ─────────────────────────────────────────────────────────────"
            local i
            for ((i = 0; i < bc_count; i++)); do
                local aname alvl adelta apct
                aname=$(echo "$imp_json" | jq -r ".better_captains[$i].name")
                alvl=$(echo "$imp_json" | jq -r ".better_captains[$i].level")
                adelta=$(echo "$imp_json" | jq -r ".better_captains[$i].delta")
                apct=$(echo "$imp_json" | jq -r ".better_captains[$i].percent")
                local adelta_fmt
                adelta_fmt=$(printf "%'d" "$adelta" 2>/dev/null || echo "$adelta")
                echo "    $((i + 1)). $aname (Lvl $alvl) — +${adelta_fmt} score (+${apct}%)"
            done
        else
            echo ""
            echo "  This is already the optimal captain for this crew."
        fi

        echo ""
    } > "$TMPFILE"

    "$DLG" --title " IMPROVEMENT ANALYSIS " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# RAW JSON VIEW
# ============================================================================
show_raw_crew_json() {
    local jq_path="$1"
    local idx="$2"

    jq "${jq_path}[$idx]" "$JSON_FILE" > "$TMPFILE"

    "$DLG" --title " RAW JSON DATA " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# WEAKNESS PROFILE
# ============================================================================
show_weakness_profile() {
    if [ ! -f "$JSON_FILE" ]; then
        "$DLG" --title " NO DATA " --msgbox "\nRun the optimizer first." 8 40
        return
    fi

    {
        echo "═══════════════════════════════════════════════════════════════════"
        echo "  WEAKNESS PROFILE (from battle history)"
        echo "═══════════════════════════════════════════════════════════════════"
        echo ""

        local total losses cdg chd stl sp de
        total=$(jq -r '.weakness.total_battles // 0' "$JSON_FILE")
        losses=$(jq -r '.weakness.losses // 0' "$JSON_FILE")
        cdg=$(jq -r '.weakness.crit_damage_gap // 0' "$JSON_FILE")
        chd=$(jq -r '.weakness.crit_hit_disadvantage // 0' "$JSON_FILE")
        stl=$(jq -r '.weakness.shield_timing_loss // 0' "$JSON_FILE")
        sp=$(jq -r '.weakness.stat_paradox // 0' "$JSON_FILE")
        de=$(jq -r '.weakness.damage_escalation // 0' "$JSON_FILE")

        printf "    %-30s %s\n" "Battles analyzed:" "$total"
        printf "    %-30s %s\n" "Losses:" "$losses"
        echo ""
        printf "    %-30s %.1f/10\n" "Crit Damage Gap:" "$cdg"
        printf "    %-30s %.1f/10\n" "Crit Hit Disadvantage:" "$chd"
        printf "    %-30s %.1f/10\n" "Shield Timing Loss:" "$stl"
        printf "    %-30s %.1f/10\n" "Stat Paradox:" "$sp"
        printf "    %-30s %.1f/10\n" "Damage Escalation:" "$de"
        echo ""

        # Interpretation
        echo "  WHAT THIS MEANS:"
        echo "  ─────────────────────────────────────────────────────────────"
        if (( $(echo "$chd > 7" | bc -l 2>/dev/null || echo 0) )); then
            echo "    >>> CRITICAL: Enemies land massively more crits than you."
            echo "        Priority: officers with crit chance/damage abilities."
        fi
        if (( $(echo "$stl > 5" | bc -l 2>/dev/null || echo 0) )); then
            echo "    >>> Your shields drop early. Need shield-boosting officers."
        fi
        if (( $(echo "$sp > 5" | bc -l 2>/dev/null || echo 0) )); then
            echo "    >>> You lose even with better stats — crew abilities matter"
            echo "        more than raw numbers. Focus on synergy."
        fi
        echo ""
    } > "$TMPFILE"

    "$DLG" --title " WEAKNESS PROFILE " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# SHIP RECOMMENDATIONS: Show recommended ship for each scenario
# ============================================================================
show_ship_recommendations() {
    "$DLG" --title " LOADING... " \
        --infobox "\n  Fetching ship recommendations...\n" 6 45

    local rec_json
    rec_json=$(python3 "$PYTHON_SCRIPT" --json recommend --ship "$SHIP" --mode "$MODE" 2>/dev/null)

    if ! echo "$rec_json" | jq empty 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nFailed to get ship recommendations." 8 50
        return
    fi

    {
        echo "═══════════════════════════════════════════════════════════════════"
        echo "  SHIP RECOMMENDATIONS BY SCENARIO"
        echo "  Current ship: $(ship_display_name "$SHIP")"
        echo "═══════════════════════════════════════════════════════════════════"
        echo ""

        local keys
        keys=$(echo "$rec_json" | jq -r '.scenarios | keys[]' 2>/dev/null)
        for key in $keys; do
            local label best reason
            label=$(_scenario_menu_label "$key")
            best=$(echo "$rec_json" | jq -r ".scenarios.${key}.recommended_ship // \"unknown\"")
            reason=$(echo "$rec_json" | jq -r ".scenarios.${key}.reason // \"\"")

            local marker=""
            [ "$best" = "$SHIP" ] && marker=" [CURRENT]"

            echo "  $label"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "    Best ship:  ${best^}${marker}"
            echo "    Reason:     $reason"
            echo ""
        done
    } > "$TMPFILE"

    "$DLG" --title " SHIP RECOMMENDATIONS " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# SHIP COMPARISON: Side-by-side top crew per ship type for a scenario
# ============================================================================
show_ship_comparison() {
    # Let user pick which scenario to compare
    local scenario_keys
    scenario_keys=$(jq -r '.scenarios // {} | keys[]' "$JSON_FILE" 2>/dev/null)

    if [ -z "$scenario_keys" ]; then
        "$DLG" --title " NO DATA " --msgbox "\nNo scenario data. Run optimizer first." 8 45
        return
    fi

    local menu_args=()
    for key in $scenario_keys; do
        local label
        label=$(_scenario_menu_label "$key")
        menu_args+=("$key" "$label")
    done

    local scenario
    scenario=$("$DLG" --title " COMPARE SHIPS " \
        --cancel-label "Back" \
        --menu "\nSelect a scenario to compare all ship types:\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "${menu_args[@]}" \
        3>&1 1>&2 2>&3) || return

    "$DLG" --title " COMPARING... " \
        --infobox "\n  Comparing all ships for $(_scenario_menu_label "$scenario")...\n  This may take a moment.\n" 8 55

    local cmp_json
    cmp_json=$(python3 "$PYTHON_SCRIPT" --json compare --ship "$SHIP" --mode "$scenario" --top 1 2>/dev/null)

    if ! echo "$cmp_json" | jq empty 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nFailed to run ship comparison." 8 50
        return
    fi

    {
        local slabel
        slabel=$(_scenario_menu_label "$scenario")
        local rec_ship best_ship
        rec_ship=$(echo "$cmp_json" | jq -r ".scenarios.${scenario}.recommended_ship // \"\"" 2>/dev/null)
        best_ship=$(echo "$cmp_json" | jq -r ".scenarios.${scenario}.best_scoring_ship // \"\"" 2>/dev/null)

        echo "═══════════════════════════════════════════════════════════════════"
        echo "  SHIP COMPARISON: $slabel"
        if [ -n "$rec_ship" ]; then
            echo "  Recommended ship: ${rec_ship^}"
        fi
        if [ -n "$best_ship" ] && [ "$best_ship" != "$rec_ship" ]; then
            echo "  Best scoring ship: ${best_ship^}"
        fi
        echo "═══════════════════════════════════════════════════════════════════"
        echo ""

        local ships
        ships=$(echo "$cmp_json" | jq -r ".scenarios.${scenario}.ships | keys[]" 2>/dev/null)
        for ship in $ships; do
            local top_score captain b1 b2
            top_score=$(echo "$cmp_json" | jq -r ".scenarios.${scenario}.ships.${ship}.score // 0" 2>/dev/null)
            captain=$(echo "$cmp_json" | jq -r ".scenarios.${scenario}.ships.${ship}.captain // \"N/A\"" 2>/dev/null)
            b1=$(echo "$cmp_json" | jq -r ".scenarios.${scenario}.ships.${ship}.bridge[0] // \"N/A\"" 2>/dev/null)
            b2=$(echo "$cmp_json" | jq -r ".scenarios.${scenario}.ships.${ship}.bridge[1] // \"N/A\"" 2>/dev/null)

            local marker=""
            [ "$ship" = "$SHIP" ] && marker=" [CURRENT]"

            local score_fmt
            score_fmt=$(printf "%'d" "$top_score" 2>/dev/null || echo "$top_score")

            echo "  ${ship^}${marker}"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "    Top Score:  $score_fmt"
            echo "    Best Crew:  $captain / $b1 / $b2"
            echo ""
        done
    } > "$TMPFILE"

    "$DLG" --title " SHIP COMPARISON " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# OFFICER BROWSER
# ============================================================================
officer_browser() {
    while true; do
        local choice
        choice=$("$DLG" --title " OFFICER BROWSER " \
            --cancel-label "Back" \
            --menu "\nSearch and explore your 266-officer roster:\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "1" "Search by name or ability keyword" \
            "2" "Filter by state: Morale officers" \
            "3" "Filter by state: Breach officers" \
            "4" "Filter by state: Burning officers" \
            "5" "Filter by state: Assimilate officers" \
            "6" "Filter by state: Crit-related officers" \
            "7" "All officers (by attack, top 20)" \
            3>&1 1>&2 2>&3) || break

        case "$choice" in
            1) officer_search_query ;;
            2) officer_search_state "morale" "Morale" ;;
            3) officer_search_state "breach" "Breach" ;;
            4) officer_search_state "burning" "Burning" ;;
            5) officer_search_state "assimilate" "Assimilate" ;;
            6) officer_search_state "crit" "Crit-Related" ;;
            7) officer_search_all ;;
        esac
    done
}

officer_search_query() {
    local query
    query=$("$DLG" --title " SEARCH OFFICERS " \
        --inputbox "\nSearch by name or ability keyword:\n(e.g. 'kirk', 'morale', 'critical', 'explorer')\n" \
        12 55 "" \
        3>&1 1>&2 2>&3) || return

    if [ -z "$query" ]; then
        return
    fi

    "$DLG" --title " SEARCHING... " \
        --infobox "\n  Searching for '$query'...\n" 7 45

    local search_json
    search_json=$(python3 "$PYTHON_SCRIPT" --json search --ship "$SHIP" --top 20 \
        --query "$query" 2>/dev/null)

    show_officer_results "$search_json" "Search: $query"
}

officer_search_state() {
    local state="$1"
    local label="$2"

    "$DLG" --title " SEARCHING... " \
        --infobox "\n  Finding $label officers...\n" 7 45

    local search_json
    search_json=$(python3 "$PYTHON_SCRIPT" --json search --ship "$SHIP" --top 20 \
        --state-filter "$state" 2>/dev/null)

    show_officer_results "$search_json" "$label Officers"
}

officer_search_all() {
    "$DLG" --title " LOADING... " \
        --infobox "\n  Loading top officers by attack...\n" 7 45

    local search_json
    search_json=$(python3 "$PYTHON_SCRIPT" --json search --ship "$SHIP" --top 20 2>/dev/null)

    show_officer_results "$search_json" "All Officers (by ATK)"
}

show_officer_results() {
    local search_json="$1"
    local title="$2"

    if ! echo "$search_json" | jq empty 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nSearch failed." 8 40
        return
    fi

    local count
    count=$(echo "$search_json" | jq '.officers | length')

    if [ "$count" -eq 0 ]; then
        "$DLG" --title " NO RESULTS " --msgbox "\nNo officers matched your search." 8 45
        return
    fi

    while true; do
        local menu_args=()
        local i
        for ((i = 0; i < count; i++)); do
            local name atk tags_str
            name=$(echo "$search_json" | jq -r ".officers[$i].name")
            atk=$(echo "$search_json" | jq -r ".officers[$i].attack" | xargs printf "%'.0f")
            tags_str=$(echo "$search_json" | jq -r ".officers[$i].tags | join(\", \")")
            local is_bda
            is_bda=$(echo "$search_json" | jq -r ".officers[$i].is_bda")
            local bda_marker=""
            [ "$is_bda" = "true" ] && bda_marker="[BDA] "
            menu_args+=("$i" "${bda_marker}${name}  ATK:${atk}  [${tags_str:0:30}]")
        done

        local choice
        choice=$("$DLG" --title " $title ($count found) " \
            --cancel-label "Back" \
            --menu "\nSelect an officer for details:\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "${menu_args[@]}" \
            3>&1 1>&2 2>&3) || break

        show_officer_detail "$search_json" "$choice"
    done
}

show_officer_detail() {
    local search_json="$1"
    local idx="$2"

    local off
    off=$(echo "$search_json" | jq ".officers[$idx]")

    {
        local name level rnk atk def hp cm oa
        name=$(echo "$off" | jq -r '.name')
        level=$(echo "$off" | jq -r '.level')
        rnk=$(echo "$off" | jq -r '.rank')
        atk=$(echo "$off" | jq -r '.attack' | xargs printf "%'.0f")
        def=$(echo "$off" | jq -r '.defense' | xargs printf "%'.0f")
        hp=$(echo "$off" | jq -r '.health' | xargs printf "%'.0f")
        cm=$(echo "$off" | jq -r '.cm_pct')
        oa=$(echo "$off" | jq -r '.oa_pct')

        echo "═══════════════════════════════════════════════════════════════════"
        echo "  $name"
        echo "═══════════════════════════════════════════════════════════════════"
        echo ""
        echo "    Level $level | Rank $rnk"
        echo "    ATK $atk | DEF $def | HP $hp"
        echo "    CM ${cm}% | OA ${oa}%"
        echo ""

        # Tags
        local tags
        tags=$(echo "$off" | jq -r '.tags | join(", ")')
        [ -n "$tags" ] && echo "    Tags: $tags"

        # States
        local applied benefit
        applied=$(echo "$off" | jq -r '.states_applied | join(", ")')
        benefit=$(echo "$off" | jq -r '.states_benefit | join(", ")')
        [ -n "$applied" ] && echo "    Applies: $applied"
        [ -n "$benefit" ] && echo "    Benefits from: $benefit"

        # Flags
        local is_bda is_dual is_pvp is_pve is_ship
        is_bda=$(echo "$off" | jq -r '.is_bda')
        is_dual=$(echo "$off" | jq -r '.is_dual_use')
        is_pvp=$(echo "$off" | jq -r '.is_pvp_specific')
        is_pve=$(echo "$off" | jq -r '.is_pve_specific')
        is_ship=$(echo "$off" | jq -r '.is_ship_specific')

        echo ""
        echo "  FLAGS:"
        echo "  ─────────────────────────────────────────────────────────────"
        [ "$is_bda" = "true" ]  && echo "    [BDA]   Below Deck Ability (not a real Captain Maneuver)"
        [ "$is_dual" = "true" ] && echo "    [DUAL]  Works in both PvE and PvP"
        [ "$is_pvp" = "true" ]  && echo "    [PVP]   PvP-specific abilities"
        [ "$is_pve" = "true" ]  && echo "    [PVE]   PvE-only abilities"
        [ "$is_ship" = "true" ] && echo "    [SHIP]  Has $(echo "$SHIP" | tr '[:lower:]' '[:upper:]') abilities"

        echo ""
        echo "  ABILITIES:"
        echo "  ─────────────────────────────────────────────────────────────"
        local display
        display=$(echo "$off" | jq -r '.display')
        echo "    $display" | fold -s -w 65 | sed 's/^/    /'
        echo ""
    } > "$TMPFILE"

    "$DLG" --title " $name " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

# ============================================================================
# 7-DOCK LOADOUT SYSTEM
# ============================================================================
# Interactive dock loadout: assign scenarios to 7 docks, optimize with no
# officer duplication across docks. Support lock/re-roll/swap.

# Persistent dock scenario assignments (defaults)
DOCK_SCENARIOS=("pvp" "hybrid" "pve_hostile" "armada" "mining_speed" "mining_protected" "mining_general")
DOCK_LOCKED=(false false false false false false false)

_dock_scenario_picker() {
    # Let user pick a scenario for a dock slot
    local dock_num="$1"
    local current="$2"

    local menu_args=()
    local scenarios=(
        "pvp"              "PvP (Player Combat)"
        "hybrid"           "Hybrid PvE/PvP"
        "base_cracker"     "Base Cracker (Station Attack)"
        "pve_hostile"      "PvE Hostiles"
        "mission_boss"     "Mission Boss"
        "loot"             "Loot / Resources"
        "armada"           "Armada"
        "-"                "──── Mining Subcategories ────"
        "mining_speed"     "Mining Speed (All Resources)"
        "mining_protected" "Mining Protected Cargo"
        "mining_crystal"   "Mining Crystal"
        "mining_gas"       "Mining Gas"
        "mining_ore"       "Mining Ore"
        "mining_general"   "Mining General"
    )

    local result
    result=$("$DLG" --title " DOCK $dock_num — SELECT ROLE " \
        --cancel-label "Cancel" \
        --default-item "$current" \
        --menu "\nChoose the role/scenario for dock $dock_num:\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "${scenarios[@]}" \
        3>&1 1>&2 2>&3) || return 1

    if [ "$result" = "-" ]; then
        return 1
    fi
    echo "$result"
}

dock_loadout_configure() {
    # Let user configure the 7 docks: pick scenario for each
    while true; do
        local menu_args=()
        local i
        for ((i = 0; i < 7; i++)); do
            local dock_num=$((i + 1))
            local scenario="${DOCK_SCENARIOS[$i]}"
            local label
            label=$(_scenario_menu_label "$scenario")
            local lock_icon=""
            [ "${DOCK_LOCKED[$i]}" = "true" ] && lock_icon="[LOCKED] "
            menu_args+=("$dock_num" "${lock_icon}${label}")
        done

        menu_args+=("-" "─────────────────────────────────")
        menu_args+=("RUN" ">>> OPTIMIZE ALL DOCKS")

        local choice
        choice=$("$DLG" --title " DOCK LOADOUT — Configure Docks " \
            --cancel-label "Back" \
            --menu "\nAssign a role to each of your 7 docks.\nSelect a dock to change its role, then RUN to optimize.\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "${menu_args[@]}" \
            3>&1 1>&2 2>&3) || return

        case "$choice" in
            RUN)
                dock_loadout_run
                return
                ;;
            -)
                ;;
            *)
                # User selected a dock number (1-7)
                local idx=$((choice - 1))
                local new_scenario
                if new_scenario=$(_dock_scenario_picker "$choice" "${DOCK_SCENARIOS[$idx]}"); then
                    DOCK_SCENARIOS[$idx]="$new_scenario"
                fi
                ;;
        esac
    done
}

dock_loadout_run() {
    # Build the JSON dock config from DOCK_SCENARIOS array
    local dock_json="["
    local i
    for ((i = 0; i < 7; i++)); do
        local scenario="${DOCK_SCENARIOS[$i]}"
        local locked="${DOCK_LOCKED[$i]}"
        local locked_crew="null"

        # If locked and we have previous results, use the locked crew
        if [ "$locked" = "true" ] && [ -f "$LOADOUT_FILE" ]; then
            local prev_captain prev_b1 prev_b2
            prev_captain=$(jq -r ".docks[$i].crew.captain // \"\"" "$LOADOUT_FILE" 2>/dev/null)
            prev_b1=$(jq -r ".docks[$i].crew.bridge[0] // \"\"" "$LOADOUT_FILE" 2>/dev/null)
            prev_b2=$(jq -r ".docks[$i].crew.bridge[1] // \"\"" "$LOADOUT_FILE" 2>/dev/null)
            if [ -n "$prev_captain" ] && [ "$prev_captain" != "N/A" ]; then
                locked_crew="{\"captain\":\"$prev_captain\",\"bridge\":[\"$prev_b1\",\"$prev_b2\"]}"
            else
                locked="false"  # Can't lock without a crew
            fi
        else
            locked="false"
        fi

        [ "$i" -gt 0 ] && dock_json+=","
        dock_json+="{\"scenario\":\"$scenario\",\"ship_override\":null,\"locked\":$locked,\"locked_crew\":$locked_crew}"
    done
    dock_json+="]"

    "$DLG" --title " OPTIMIZING LOADOUT... " \
        --infobox "\n\
  Optimizing 7-dock loadout...\n\
\n\
  Ship: $(ship_display_name "$SHIP")\n\
  266 officers, no duplicates across docks.\n\
\n\
  This may take 15-30 seconds...\n" \
        12 60

    local result
    if ! result=$(python3 "$PYTHON_SCRIPT" --json loadout --ship "$SHIP" --docks "$dock_json" 2>/dev/null); then
        "$DLG" --title " ERROR " --msgbox "\nLoadout optimizer failed." 8 50
        return
    fi

    if ! echo "$result" | jq empty 2>/dev/null; then
        "$DLG" --title " ERROR " --msgbox "\nInvalid JSON from loadout optimizer." 8 50
        return
    fi

    local err
    err=$(echo "$result" | jq -r '.error // empty')
    if [ -n "$err" ]; then
        "$DLG" --title " ERROR " --msgbox "\n$err" 10 60
        return
    fi

    # Save loadout results
    echo "$result" > "$LOADOUT_FILE"

    # Show results
    dock_loadout_results
}

dock_loadout_results() {
    if [ ! -f "$LOADOUT_FILE" ]; then
        "$DLG" --title " NO DATA " --msgbox "\nNo loadout results. Run dock loadout first." 8 50
        return
    fi

    local dock_count
    dock_count=$(jq '.docks | length' "$LOADOUT_FILE" 2>/dev/null)

    while true; do
        local menu_args=()
        local i
        for ((i = 0; i < dock_count; i++)); do
            local dock_num=$((i + 1))
            local scenario captain score ship_used
            scenario=$(jq -r ".docks[$i].scenario" "$LOADOUT_FILE")
            captain=$(jq -r ".docks[$i].crew.captain // \"N/A\"" "$LOADOUT_FILE")
            score=$(jq -r ".docks[$i].crew.score // 0" "$LOADOUT_FILE")
            ship_used=$(jq -r ".docks[$i].ship_used // \"\"" "$LOADOUT_FILE")
            local b1 b2
            b1=$(jq -r ".docks[$i].crew.bridge[0] // \"\"" "$LOADOUT_FILE")
            b2=$(jq -r ".docks[$i].crew.bridge[1] // \"\"" "$LOADOUT_FILE")

            local label
            label=$(_scenario_menu_label "$scenario")
            local score_fmt
            score_fmt=$(printf "%'d" "$score" 2>/dev/null || echo "$score")
            local lock_icon=""
            [ "${DOCK_LOCKED[$i]}" = "true" ] && lock_icon="* "

            menu_args+=("$dock_num" "${lock_icon}${label}: ${captain} / ${b1} / ${b2}  [${score_fmt}]")
        done

        local total_used
        total_used=$(jq -r '.total_officers_used // 0' "$LOADOUT_FILE")

        menu_args+=("-" "─────────────────────────────────")
        menu_args+=("SUMMARY" "View Full Loadout Summary")
        menu_args+=("LOCK"    "Lock/Unlock Docks")
        menu_args+=("REROLL"  "Re-roll Unlocked Docks")
        menu_args+=("SWAP"    "Swap Officers Between Docks")

        local choice
        choice=$("$DLG" --title " DOCK LOADOUT — $total_used officers assigned " \
            --cancel-label "Back" \
            --menu "\nSelect a dock to drill in, or manage loadout:\n(* = locked dock)\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "${menu_args[@]}" \
            3>&1 1>&2 2>&3) || break

        case "$choice" in
            SUMMARY) dock_loadout_summary ;;
            LOCK)    dock_loadout_lock_toggle ;;
            REROLL)  dock_loadout_reroll ;;
            SWAP)    dock_loadout_swap ;;
            -)       ;;
            *)
                # Drill into a dock
                local idx=$((choice - 1))
                dock_loadout_drill "$idx"
                ;;
        esac
    done
}

dock_loadout_summary() {
    {
        echo "═══════════════════════════════════════════════════════════════════"
        echo "  7-DOCK LOADOUT SUMMARY"
        echo "  Ship: $(ship_display_name "$SHIP")"
        echo "═══════════════════════════════════════════════════════════════════"
        echo ""

        local dock_count
        dock_count=$(jq '.docks | length' "$LOADOUT_FILE")
        local i
        for ((i = 0; i < dock_count; i++)); do
            local dock_num=$((i + 1))
            local scenario captain b1 b2 score ship_used ship_rec
            scenario=$(jq -r ".docks[$i].scenario" "$LOADOUT_FILE")
            captain=$(jq -r ".docks[$i].crew.captain // \"N/A\"" "$LOADOUT_FILE")
            b1=$(jq -r ".docks[$i].crew.bridge[0] // \"\"" "$LOADOUT_FILE")
            b2=$(jq -r ".docks[$i].crew.bridge[1] // \"\"" "$LOADOUT_FILE")
            score=$(jq -r ".docks[$i].crew.score // 0" "$LOADOUT_FILE")
            ship_used=$(jq -r ".docks[$i].ship_used // \"\"" "$LOADOUT_FILE")
            ship_rec=$(jq -r ".docks[$i].ship_recommended // \"\"" "$LOADOUT_FILE")

            local label
            label=$(_scenario_menu_label "$scenario")
            local score_fmt
            score_fmt=$(printf "%'d" "$score" 2>/dev/null || echo "$score")
            local lock_str=""
            [ "${DOCK_LOCKED[$i]}" = "true" ] && lock_str=" [LOCKED]"

            echo "  DOCK $dock_num: $label${lock_str}"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "    Ship: ${ship_used^} (recommended: ${ship_rec^})"
            echo "    Captain: $captain"
            echo "    Bridge:  $b1 / $b2"
            echo "    Score:   $score_fmt"

            # BDA suggestions (top 1)
            local bda_count
            bda_count=$(jq -r ".docks[$i].bda_suggestions | length" "$LOADOUT_FILE" 2>/dev/null)
            if [ "$bda_count" -gt 0 ] 2>/dev/null; then
                local bda_name
                bda_name=$(jq -r ".docks[$i].bda_suggestions[0].name" "$LOADOUT_FILE")
                echo "    Top BDA: $bda_name"
            fi
            echo ""
        done

        local total_used
        total_used=$(jq -r '.total_officers_used // 0' "$LOADOUT_FILE")
        echo "  Total officers assigned: $total_used / 266"
        echo ""

        # List all excluded officers
        echo "  ASSIGNED OFFICERS:"
        echo "  ─────────────────────────────────────────────────────────────"
        jq -r '.excluded_officers[]' "$LOADOUT_FILE" 2>/dev/null | while read -r name; do
            echo "    - $name"
        done
        echo ""
    } > "$TMPFILE"

    "$DLG" --title " LOADOUT SUMMARY " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

dock_loadout_drill() {
    local idx="$1"
    local dock_num=$((idx + 1))

    local scenario captain b1 b2
    scenario=$(jq -r ".docks[$idx].scenario" "$LOADOUT_FILE")
    captain=$(jq -r ".docks[$idx].crew.captain // \"N/A\"" "$LOADOUT_FILE")
    b1=$(jq -r ".docks[$idx].crew.bridge[0] // \"\"" "$LOADOUT_FILE")
    b2=$(jq -r ".docks[$idx].crew.bridge[1] // \"\"" "$LOADOUT_FILE")
    local crew_str="${captain}|${b1}|${b2}"
    local label
    label=$(_scenario_menu_label "$scenario")

    while true; do
        local lock_str=""
        [ "${DOCK_LOCKED[$idx]}" = "true" ] && lock_str=" [LOCKED]"

        local choice
        choice=$("$DLG" --title " DOCK $dock_num: $label${lock_str} " \
            --cancel-label "Back" \
            --menu "\n  Captain: $captain\n  Bridge:  $b1 / $b2\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "1" "Score Breakdown & Officer Details" \
            "2" "Below Deck (BDA) Suggestions" \
            "3" "How to Improve This Crew" \
            "4" "Toggle Lock (currently: ${DOCK_LOCKED[$idx]})" \
            "5" "Change This Dock's Role" \
            3>&1 1>&2 2>&3) || break

        case "$choice" in
            1) dock_loadout_show_details "$idx" ;;
            2) show_bda_suggestions "$crew_str" "$scenario" ;;
            3) show_improvement_analysis "$crew_str" "$scenario" ;;
            4)
                if [ "${DOCK_LOCKED[$idx]}" = "true" ]; then
                    DOCK_LOCKED[$idx]="false"
                else
                    DOCK_LOCKED[$idx]="true"
                fi
                ;;
            5)
                local new_scenario
                if new_scenario=$(_dock_scenario_picker "$dock_num" "$scenario"); then
                    DOCK_SCENARIOS[$idx]="$new_scenario"
                    # Re-run the loadout
                    dock_loadout_run
                    return
                fi
                ;;
        esac
    done
}

dock_loadout_show_details() {
    local idx="$1"
    local dock_num=$((idx + 1))

    local dock
    dock=$(jq ".docks[$idx]" "$LOADOUT_FILE")

    {
        local scenario label captain b1 b2 score ship_used
        scenario=$(echo "$dock" | jq -r '.scenario')
        label=$(_scenario_menu_label "$scenario")
        captain=$(echo "$dock" | jq -r '.crew.captain // "N/A"')
        b1=$(echo "$dock" | jq -r '.crew.bridge[0] // ""')
        b2=$(echo "$dock" | jq -r '.crew.bridge[1] // ""')
        score=$(echo "$dock" | jq -r '.crew.score // 0')
        ship_used=$(echo "$dock" | jq -r '.ship_used // ""')
        local score_fmt
        score_fmt=$(printf "%'d" "$score" 2>/dev/null || echo "$score")

        echo "═══════════════════════════════════════════════════════════════════"
        echo "  DOCK $dock_num: $label  —  Score: $score_fmt"
        echo "  Ship: ${ship_used^}"
        echo "═══════════════════════════════════════════════════════════════════"
        echo ""
        echo "  Captain: $captain"
        echo "  Bridge:  $b1 / $b2"

        # Individual scores
        local ind_scores
        ind_scores=$(echo "$dock" | jq -r '.crew.individual_scores // {}')
        if [ "$ind_scores" != "{}" ] && [ "$ind_scores" != "null" ]; then
            echo ""
            echo "  INDIVIDUAL SCORES:"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "$ind_scores" | jq -r 'to_entries[] | "    \(.key): \(.value)"'
        fi

        # Synergy notes
        local syn_count
        syn_count=$(echo "$dock" | jq '.crew.synergy_notes // [] | length')
        if [ "$syn_count" -gt 0 ]; then
            echo ""
            echo "  SYNERGIES:"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "$dock" | jq -r '.crew.synergy_notes[]' | while read -r note; do
                echo "    + $note"
            done
        fi

        # Penalties
        local pen_count
        pen_count=$(echo "$dock" | jq '.crew.penalties // [] | length')
        if [ "$pen_count" -gt 0 ]; then
            echo ""
            echo "  WARNINGS:"
            echo "  ─────────────────────────────────────────────────────────────"
            echo "$dock" | jq -r '.crew.penalties[]' | while read -r pen; do
                echo "    - $pen"
            done
        fi

        # Scenario bonus
        local snb
        snb=$(echo "$dock" | jq -r '.crew.scenario_bonus // 0')
        if [ "$snb" != "0" ]; then
            echo ""
            printf "    %-35s %'d\n" "Scenario specialization:" "$snb"
        fi

        # BDA suggestions
        local bda_count
        bda_count=$(echo "$dock" | jq '.bda_suggestions | length')
        if [ "$bda_count" -gt 0 ]; then
            echo ""
            echo "  TOP BDA SUGGESTIONS:"
            echo "  ─────────────────────────────────────────────────────────────"
            local j
            for ((j = 0; j < bda_count; j++)); do
                local bda_name bda_score bda_reason
                bda_name=$(echo "$dock" | jq -r ".bda_suggestions[$j].name")
                bda_score=$(echo "$dock" | jq -r ".bda_suggestions[$j].score")
                bda_reason=$(echo "$dock" | jq -r ".bda_suggestions[$j].reasons[0] // \"\"")
                local bda_score_fmt
                bda_score_fmt=$(printf "%'d" "$bda_score" 2>/dev/null || echo "$bda_score")
                echo "    $((j + 1)). $bda_name  [${bda_score_fmt}]  $bda_reason"
            done
        fi

        echo ""
    } > "$TMPFILE"

    "$DLG" --title " DOCK $dock_num DETAILS " \
        --textbox "$TMPFILE" \
        "$DLG_HEIGHT" "$DLG_WIDTH"
}

dock_loadout_lock_toggle() {
    # Show checklist of docks to lock/unlock
    local menu_args=()
    local i
    for ((i = 0; i < 7; i++)); do
        local dock_num=$((i + 1))
        local scenario="${DOCK_SCENARIOS[$i]}"
        local label
        label=$(_scenario_menu_label "$scenario")
        local captain
        captain=$(jq -r ".docks[$i].crew.captain // \"N/A\"" "$LOADOUT_FILE" 2>/dev/null)
        local on_off="off"
        [ "${DOCK_LOCKED[$i]}" = "true" ] && on_off="on"
        menu_args+=("$dock_num" "$label — $captain" "$on_off")
    done

    local result
    result=$("$DLG" --title " LOCK/UNLOCK DOCKS " \
        --cancel-label "Cancel" \
        --checklist "\nChecked = LOCKED (won't change on re-roll).\nUnchecked = unlocked (will be re-optimized).\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "${menu_args[@]}" \
        3>&1 1>&2 2>&3) || return

    # Reset all to unlocked, then set selected as locked
    for ((i = 0; i < 7; i++)); do
        DOCK_LOCKED[$i]="false"
    done
    for num in $result; do
        local idx=$((num - 1))
        DOCK_LOCKED[$idx]="true"
    done
}

dock_loadout_reroll() {
    # Count how many are unlocked
    local unlocked_count=0
    local i
    for ((i = 0; i < 7; i++)); do
        [ "${DOCK_LOCKED[$i]}" = "false" ] && unlocked_count=$((unlocked_count + 1))
    done

    if [ "$unlocked_count" -eq 0 ]; then
        "$DLG" --title " ALL LOCKED " --msgbox "\nAll docks are locked. Unlock some to re-roll." 8 50
        return
    fi

    "$DLG" --title " RE-ROLL " \
        --yesno "\nRe-optimize $unlocked_count unlocked dock(s)?\n\nLocked docks keep their crews. Unlocked docks get\nnew optimal crews from remaining officers.\n" \
        12 55 || return

    dock_loadout_run
}

dock_loadout_swap() {
    # Pick two docks to swap their crew assignments
    local dock_count=7
    local menu_args=()
    local i
    for ((i = 0; i < dock_count; i++)); do
        local dock_num=$((i + 1))
        local scenario="${DOCK_SCENARIOS[$i]}"
        local label
        label=$(_scenario_menu_label "$scenario")
        local captain
        captain=$(jq -r ".docks[$i].crew.captain // \"N/A\"" "$LOADOUT_FILE" 2>/dev/null)
        menu_args+=("$dock_num" "$label — $captain")
    done

    local first
    first=$("$DLG" --title " SWAP — Select First Dock " \
        --cancel-label "Cancel" \
        --menu "\nSelect the first dock to swap:\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "${menu_args[@]}" \
        3>&1 1>&2 2>&3) || return

    local second
    second=$("$DLG" --title " SWAP — Select Second Dock " \
        --cancel-label "Cancel" \
        --menu "\nSelect the second dock to swap with Dock $first:\n" \
        "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
        "${menu_args[@]}" \
        3>&1 1>&2 2>&3) || return

    if [ "$first" = "$second" ]; then
        "$DLG" --title " SWAP " --msgbox "\nCan't swap a dock with itself." 8 45
        return
    fi

    local idx1=$((first - 1))
    local idx2=$((second - 1))

    # Swap scenarios
    local tmp="${DOCK_SCENARIOS[$idx1]}"
    DOCK_SCENARIOS[$idx1]="${DOCK_SCENARIOS[$idx2]}"
    DOCK_SCENARIOS[$idx2]="$tmp"

    # Swap lock states
    tmp="${DOCK_LOCKED[$idx1]}"
    DOCK_LOCKED[$idx1]="${DOCK_LOCKED[$idx2]}"
    DOCK_LOCKED[$idx2]="$tmp"

    # Re-run to re-optimize with swapped roles
    "$DLG" --title " SWAPPED " \
        --msgbox "\nDocks $first and $second swapped.\nRe-running optimization..." 8 45

    dock_loadout_run
}

# ============================================================================
# RESULTS HUB: After running optimizer, browse results interactively
# ============================================================================

# Mapping of scenario keys to short labels for the menu
_scenario_menu_label() {
    case "$1" in
        pvp)              echo "PvP (Player Combat)" ;;
        hybrid)           echo "Hybrid PvE/PvP" ;;
        base_cracker)     echo "Base Cracker (Station Attack)" ;;
        pve_hostile)      echo "PvE Hostiles" ;;
        mission_boss)     echo "Mission Boss" ;;
        loot)             echo "Loot / Resources" ;;
        armada)           echo "Armada" ;;
        mining_speed)     echo "Mining Speed (All)" ;;
        mining_protected) echo "Mining Protected Cargo" ;;
        mining_crystal)   echo "Mining Crystal" ;;
        mining_gas)       echo "Mining Gas" ;;
        mining_ore)       echo "Mining Ore" ;;
        mining_general)   echo "Mining General" ;;
        *)                echo "$1" ;;
    esac
}

results_hub() {
    if [ ! -f "$JSON_FILE" ]; then
        "$DLG" --title " NO DATA " --msgbox "\nNo results available.\nRun the optimizer first." 8 45
        return
    fi

    # Discover available scenarios from JSON
    local scenario_keys
    scenario_keys=$(jq -r '.scenarios // {} | keys[]' "$JSON_FILE" 2>/dev/null)

    if [ -z "$scenario_keys" ]; then
        # Fall back to old pvp_crews / hybrid_crews format
        local has_pvp="false" has_hybrid="false"
        local pvp_count=0 hybrid_count=0
        pvp_count=$(jq -r '.pvp_crews // [] | length' "$JSON_FILE" 2>/dev/null)
        hybrid_count=$(jq -r '.hybrid_crews // [] | length' "$JSON_FILE" 2>/dev/null)
        [ "$pvp_count" -gt 0 ] 2>/dev/null && has_pvp="true"
        [ "$hybrid_count" -gt 0 ] 2>/dev/null && has_hybrid="true"

        while true; do
            local menu_args=()
            if [ "$has_pvp" = "true" ]; then
                menu_args+=("pvp" "Browse PvP Crews ($pvp_count results)")
            fi
            if [ "$has_hybrid" = "true" ]; then
                menu_args+=("hybrid" "Browse Hybrid PvE/PvP Crews ($hybrid_count results)")
            fi
            menu_args+=("weakness" "View Weakness Profile")
            menu_args+=("raw" "View Raw Text Output")

            local choice
            choice=$("$DLG" --title " RESULTS — $(ship_short_name "$SHIP") / $MODE " \
                --cancel-label "Back" \
                --menu "\nBrowse your optimization results:\n" \
                "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
                "${menu_args[@]}" \
                3>&1 1>&2 2>&3) || break

            case "$choice" in
                pvp)      show_crew_list "pvp" "PvP" ;;
                hybrid)   show_crew_list "hybrid" "Hybrid" ;;
                weakness) show_weakness_profile ;;
                raw)
                    if [ -f "$RESULTS_FILE" ]; then
                        "$DLG" --title " RAW OUTPUT " \
                            --textbox "$RESULTS_FILE" \
                            "$DLG_HEIGHT" "$DLG_WIDTH"
                    fi
                    ;;
            esac
        done
        return
    fi

    # New format: .scenarios.{key}.crews
    while true; do
        local menu_args=()
        local key
        for key in $scenario_keys; do
            local count label
            count=$(jq -r ".scenarios.${key}.crews | length" "$JSON_FILE" 2>/dev/null)
            label=$(_scenario_menu_label "$key")
            if [ "$count" -gt 0 ] 2>/dev/null; then
                menu_args+=("$key" "$label ($count results)")
            fi
        done

        menu_args+=("-" "─────────────────────────────────")
        menu_args+=("recommend" "Ship Recommendations")
        menu_args+=("compare"   "Compare Ships (side-by-side)")
        menu_args+=("weakness"  "View Weakness Profile")
        menu_args+=("raw"       "View Raw Text Output")

        local choice
        choice=$("$DLG" --title " RESULTS — $(ship_short_name "$SHIP") / $MODE " \
            --cancel-label "Back" \
            --menu "\nBrowse your optimization results:\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "${menu_args[@]}" \
            3>&1 1>&2 2>&3) || break

        case "$choice" in
            weakness) show_weakness_profile ;;
            recommend) show_ship_recommendations ;;
            compare)   show_ship_comparison ;;
            raw)
                if [ -f "$RESULTS_FILE" ]; then
                    "$DLG" --title " RAW OUTPUT " \
                        --textbox "$RESULTS_FILE" \
                        "$DLG_HEIGHT" "$DLG_WIDTH"
                fi
                ;;
            -) ;;  # Separator
            *)
                # It's a scenario key
                local slabel
                slabel=$(_scenario_menu_label "$choice")
                show_crew_list "$choice" "$slabel"
                ;;
        esac
    done
}

# ============================================================================
# MAIN MENU
# ============================================================================
main_menu() {
    while true; do
        local ship_short
        ship_short=$(ship_short_name "$SHIP")

        local has_results="false"
        [ -f "$JSON_FILE" ] && has_results="true"

        local menu_args=()
        menu_args+=("1" ">>> Run Optimizer")
        if [ "$has_results" = "true" ]; then
            menu_args+=("2" ">>> Browse Results")
        fi
        menu_args+=("3" "Officer Browser")
        menu_args+=("8" ">>> 7-Dock Loadout")
        if [ -f "$LOADOUT_FILE" ]; then
            menu_args+=("9" ">>> Browse Loadout Results")
        fi
        menu_args+=("-" "─────────────────────────────────")
        menu_args+=("4" "Select Ship        [$SHIP]")
        menu_args+=("5" "Select Mode        [$MODE]")
        menu_args+=("6" "Number of Results  [$TOP_N]")
        menu_args+=("7" "Output Options")

        local choice
        choice=$("$DLG" --title " STFC CREW OPTIMIZER " \
            --cancel-label "Exit" \
            --menu "\n  Ship: $ship_short | Mode: $MODE | Top $TOP_N\n" \
            "$DLG_HEIGHT" "$DLG_WIDTH" "$LIST_HEIGHT" \
            "${menu_args[@]}" \
            3>&1 1>&2 2>&3) || break

        case "$choice" in
            1)
                if confirm_and_run; then
                    save_settings
                    run_optimizer_json
                    # Jump straight to results
                    results_hub
                fi
                ;;
            2) results_hub ;;
            3) officer_browser ;;
            4) select_ship && save_settings ;;
            5) select_mode && save_settings ;;
            6) select_top_n && save_settings ;;
            7) select_options && save_settings ;;
            8) dock_loadout_configure ;;
            9) dock_loadout_results ;;
            -) ;;  # Separator, no-op
        esac
    done
}

# ============================================================================
# Entry point
# ============================================================================
load_settings

# Quick mode: skip menus, just run
if [ "${1:-}" = "--quick" ]; then
    save_settings
    run_optimizer_json
    results_hub
    clear
    echo "STFC Crew Optimizer — done."
    exit 0
fi

main_menu

# Clean exit
clear
echo "STFC Crew Optimizer — done."
