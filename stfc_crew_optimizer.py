#!/usr/bin/env python3
"""
STFC Crew Optimizer
Analyzes officer roster, battle logs, and generates optimized crews for different scenarios.
Enhanced with synergy-aware PvP crew evaluation and battle log analysis.
"""

import csv
import glob
import json
import os
import sys
from itertools import combinations
from typing import List, Dict, Tuple, Optional, Set
from datetime import datetime


# ---------------------------------------------------------------------------
# Battle Log Analyzer
# ---------------------------------------------------------------------------


class BattleLogAnalyzer:
    """Parses STFC battle log CSVs and extracts combat metrics."""

    def __init__(self):
        self.battles: List[Dict] = []

    def load_log(self, csv_path: str) -> Dict:
        """Load and analyze a single battle log file.

        Returns a battle summary dict with player/enemy stats, round-by-round
        damage, critical hit data, shield depletion timing, and more.
        """
        with open(csv_path, "r", encoding="utf-8") as f:
            reader = csv.reader(f, delimiter="\t")
            rows = list(reader)

        if len(rows) < 11:
            return {}

        # --- Header rows (lines 1-3 in file, indices 0-2) ---
        player_row = rows[1] if len(rows) > 1 else []
        enemy_row = rows[2] if len(rows) > 2 else []

        player_info = self._parse_combatant_header(player_row)
        enemy_info = self._parse_combatant_header(enemy_row)

        # --- Fleet stat rows (lines 8-9, indices 7-8) ---
        player_fleet = self._parse_fleet_stats(rows[7]) if len(rows) > 7 else {}
        enemy_fleet = self._parse_fleet_stats(rows[8]) if len(rows) > 8 else {}

        # --- Combat event rows (line 12+, index 11+) ---
        events = []
        for row in rows[11:]:
            if len(row) < 15:
                continue
            event = self._parse_event(row)
            if event:
                events.append(event)

        # --- Derived analysis ---
        analysis = self._analyze_events(events, player_info, enemy_info)

        battle = {
            "file": os.path.basename(csv_path),
            "timestamp": player_info.get("timestamp", ""),
            "player": player_info,
            "enemy": enemy_info,
            "player_fleet": player_fleet,
            "enemy_fleet": enemy_fleet,
            "events": events,
            "analysis": analysis,
        }
        self.battles.append(battle)
        return battle

    def load_all_logs(
        self, log_dir: str = "logs", extra_files: Optional[List[str]] = None
    ):
        """Load all battle log CSVs from a directory and optional extra file paths.
        Deduplicates by (player_name, enemy_name, timestamp)."""
        loaded = []
        seen_keys = set()
        all_paths = []
        if os.path.isdir(log_dir):
            all_paths.extend(sorted(glob.glob(os.path.join(log_dir, "*.csv"))))
        if extra_files:
            all_paths.extend(p for p in extra_files if os.path.isfile(p))

        for path in all_paths:
            b = self.load_log(path)
            if b:
                key = (
                    b.get("player", {}).get("name", ""),
                    b.get("enemy", {}).get("name", ""),
                    b.get("player", {}).get("timestamp", ""),
                )
                if key not in seen_keys:
                    seen_keys.add(key)
                    loaded.append(b)
                else:
                    # Remove the duplicate from self.battles
                    self.battles.pop()
        return loaded

    # -- internal parsers ---------------------------------------------------

    @staticmethod
    def _safe_int(val, default=0):
        try:
            return int(float(val))
        except (ValueError, TypeError):
            return default

    @staticmethod
    def _parse_combatant_header(row: list) -> Dict:
        if len(row) < 16:
            return {}
        si = BattleLogAnalyzer._safe_int
        return {
            "name": str(row[0]).strip(),
            "level": si(row[1]),
            "outcome": str(row[2]).strip(),
            "ship": str(row[3]).strip(),
            "ship_level": si(row[4]),
            "ship_strength": si(row[5]),
            "officers": [str(row[7]).strip(), str(row[8]).strip(), str(row[9]).strip()],
            "hull_health": si(row[10]),
            "hull_remaining": si(row[11]),
            "shield_health": si(row[12]),
            "shield_remaining": si(row[13]),
            "location": str(row[14]).strip(),
            "timestamp": str(row[15]).strip(),
        }

    @staticmethod
    def _safe_float(val, default=0.0):
        try:
            cleaned = str(val).replace(",", "").strip()
            if cleaned in ("", "--", "NO", "YES"):
                return default
            return float(cleaned)
        except (ValueError, TypeError):
            return default

    def _parse_fleet_stats(self, row: list) -> Dict:
        if len(row) < 30:
            return {}
        sf = self._safe_float
        return {
            "label": str(row[0]).strip(),
            "attack": sf(row[1]),
            "defense": sf(row[2]),
            "health": sf(row[3]),
            "ship_ability": str(row[4]).strip(),
            "captain_maneuver": str(row[5]).strip(),
            "oa_1": str(row[6]).strip(),
            "oa_2": str(row[7]).strip(),
            "oa_3": str(row[8]).strip(),
            "officer_atk_bonus": sf(row[9]),
            "damage_per_round": sf(row[10]),
            "armour_pierce": sf(row[11]),
            "shield_pierce": sf(row[12]),
            "accuracy": sf(row[13]),
            "crit_chance": sf(row[14]),
            "crit_damage": sf(row[15]),
            "officer_def_bonus": sf(row[16]),
            "armour": sf(row[17]),
            "shield_deflection": sf(row[18]),
            "dodge": sf(row[19]),
            "officer_hp_bonus": sf(row[20]),
            "shield_health": sf(row[21]),
            "hull_health": sf(row[22]),
        }

    def _parse_event(self, row: list) -> Optional[Dict]:
        sf = self._safe_float
        round_num = sf(row[0])
        if round_num == 0 and str(row[0]).strip() not in ("0",):
            return None
        event_type = str(row[2]).strip() if len(row) > 2 else ""
        return {
            "round": int(round_num),
            "event_id": int(sf(row[1])),
            "type": event_type,
            "attacker": str(row[3]).strip() if len(row) > 3 else "",
            "attacker_ship": str(row[5]).strip() if len(row) > 5 else "",
            "target": str(row[7]).strip() if len(row) > 7 else "",
            "target_ship": str(row[9]).strip() if len(row) > 9 else "",
            "critical": str(row[13]).strip().upper() == "YES"
            if len(row) > 13
            else False,
            "hull_damage": sf(row[14]) if len(row) > 14 else 0,
            "shield_damage": sf(row[15]) if len(row) > 15 else 0,
            "mitigated": sf(row[16]) if len(row) > 16 else 0,
            "mitigated_isolytic": sf(row[17]) if len(row) > 17 else 0,
            "mitigated_apex": sf(row[18]) if len(row) > 18 else 0,
            "total_damage": sf(row[19]) if len(row) > 19 else 0,
            "total_isolytic": sf(row[20]) if len(row) > 20 else 0,
            "ability_type": str(row[21]).strip() if len(row) > 21 else "",
            "ability_value": sf(row[22]) if len(row) > 22 else 0,
            "ability_name": str(row[23]).strip() if len(row) > 23 else "",
            "ability_owner": str(row[24]).strip() if len(row) > 24 else "",
            "charging_pct": sf(row[27]) if len(row) > 27 else 0,
        }

    def _analyze_events(self, events: list, player: Dict, enemy: Dict) -> Dict:
        """Derive combat metrics from the raw events."""
        player_name = player.get("name", "")
        enemy_name = enemy.get("name", "")

        attacks = [e for e in events if e["type"] == "Attack"]
        player_attacks = [a for a in attacks if a["attacker"] == player_name]
        enemy_attacks = [a for a in attacks if a["attacker"] == enemy_name]

        player_crits = [a for a in player_attacks if a["critical"]]
        enemy_crits = [a for a in enemy_attacks if a["critical"]]

        total_rounds = max((e["round"] for e in events), default=0)

        player_total_dmg = sum(
            a["hull_damage"] + a["shield_damage"] for a in player_attacks
        )
        enemy_total_dmg = sum(
            a["hull_damage"] + a["shield_damage"] for a in enemy_attacks
        )

        player_total_hull_dmg = sum(a["hull_damage"] for a in player_attacks)
        enemy_total_hull_dmg = sum(a["hull_damage"] for a in enemy_attacks)

        player_crit_dmg = sum(
            a["hull_damage"] + a["shield_damage"] for a in player_crits
        )
        enemy_crit_dmg = sum(a["hull_damage"] + a["shield_damage"] for a in enemy_crits)

        # Shield depletion round
        shield_events = [e for e in events if e["type"] == "Shield Depleted"]
        player_shield_lost_round = None
        enemy_shield_lost_round = None
        for se in shield_events:
            if se["attacker"] == player_name:
                player_shield_lost_round = se["round"]
            elif se["attacker"] == enemy_name:
                enemy_shield_lost_round = se["round"]

        # Officer abilities fired
        abilities = [e for e in events if e["type"] == "Officer Ability"]
        player_abilities = [a for a in abilities if a["attacker"] == player_name]
        enemy_abilities = [a for a in abilities if a["attacker"] == enemy_name]

        # Forbidden tech
        ftech = [e for e in events if "Forbidden Tech" in e["type"]]
        player_ftech = [a for a in ftech if a["attacker"] == player_name]
        enemy_ftech = [a for a in ftech if a["attacker"] == enemy_name]

        # Damage per round breakdown
        player_dpr = {}
        enemy_dpr = {}
        for a in player_attacks:
            r = a["round"]
            player_dpr[r] = player_dpr.get(r, 0) + a["hull_damage"] + a["shield_damage"]
        for a in enemy_attacks:
            r = a["round"]
            enemy_dpr[r] = enemy_dpr.get(r, 0) + a["hull_damage"] + a["shield_damage"]

        return {
            "total_rounds": total_rounds,
            "player_attacks": len(player_attacks),
            "enemy_attacks": len(enemy_attacks),
            "player_crits": len(player_crits),
            "enemy_crits": len(enemy_crits),
            "player_crit_rate": len(player_crits) / max(len(player_attacks), 1),
            "enemy_crit_rate": len(enemy_crits) / max(len(enemy_attacks), 1),
            "player_total_dmg": player_total_dmg,
            "enemy_total_dmg": enemy_total_dmg,
            "player_total_hull_dmg": player_total_hull_dmg,
            "enemy_total_hull_dmg": enemy_total_hull_dmg,
            "player_crit_dmg": player_crit_dmg,
            "enemy_crit_dmg": enemy_crit_dmg,
            "player_crit_dmg_pct": player_crit_dmg / max(player_total_dmg, 1),
            "enemy_crit_dmg_pct": enemy_crit_dmg / max(enemy_total_dmg, 1),
            "player_shield_lost_round": player_shield_lost_round,
            "enemy_shield_lost_round": enemy_shield_lost_round,
            "player_avg_dmg_per_hit": player_total_dmg / max(len(player_attacks), 1),
            "enemy_avg_dmg_per_hit": enemy_total_dmg / max(len(enemy_attacks), 1),
            "player_avg_dpr": player_total_dmg / max(total_rounds, 1),
            "enemy_avg_dpr": enemy_total_dmg / max(total_rounds, 1),
            "player_abilities_fired": [
                {
                    "name": a["ability_name"],
                    "owner": a["ability_owner"],
                    "value": a["ability_value"],
                }
                for a in player_abilities
            ],
            "enemy_abilities_fired": [
                {
                    "name": a["ability_name"],
                    "owner": a["ability_owner"],
                    "value": a["ability_value"],
                }
                for a in enemy_abilities
            ],
            "player_ftech": [
                {"name": a["ability_name"], "value": a["ability_value"]}
                for a in player_ftech
            ],
            "enemy_ftech": [
                {"name": a["ability_name"], "value": a["ability_value"]}
                for a in enemy_ftech
            ],
            "player_dpr": player_dpr,
            "enemy_dpr": enemy_dpr,
            "outcome": player.get("outcome", "UNKNOWN"),
        }

    # -- pretty print -------------------------------------------------------

    def print_battle_summary(self, battle: Dict):
        """Print a human-readable battle summary to stdout."""
        p = battle["player"]
        e = battle["enemy"]
        pf = battle["player_fleet"]
        ef = battle["enemy_fleet"]
        a = battle["analysis"]

        print(f"\n{'=' * 100}")
        print(f"  BATTLE ANALYSIS: {p['name']} vs {e['name']}")
        print(f"  {p.get('timestamp', battle.get('timestamp', ''))}")
        print(f"{'=' * 100}")

        # Outcome
        outcome_str = "VICTORY" if a["outcome"] == "VICTORY" else "DEFEAT"
        print(f"\n  Result: {outcome_str} | Rounds: {a['total_rounds']}")

        # Combatant overview
        print(f"\n  {'':30s} {'YOU':>15s}   {'ENEMY':>15s}   {'DIFF':>10s}")
        print(f"  {'-' * 75}")

        def cmp_row(label, pval, eval_, fmt="{:>15,.0f}", higher_better=True):
            pstr = fmt.format(pval)
            estr = fmt.format(eval_)
            if pval == eval_:
                diff = "   EQUAL"
            elif (pval > eval_) == higher_better:
                diff = "  +YOU"
            else:
                diff = "  +ENEMY"
            print(f"  {label:30s} {pstr}   {estr}   {diff}")

        print(f"  {'Player':30s} {p['name']:>15s}   {e['name']:>15s}")
        print(f"  {'Ship':30s} {p['ship']:>15s}   {e['ship']:>15s}")
        cmp_row("Ship Level", p.get("ship_level", 0), e.get("ship_level", 0))
        cmp_row("Ship Strength", p.get("ship_strength", 0), e.get("ship_strength", 0))

        if pf and ef:
            print(f"\n  --- Fleet Stats ---")
            cmp_row("Attack", pf.get("attack", 0), ef.get("attack", 0))
            cmp_row("Defense", pf.get("defense", 0), ef.get("defense", 0))
            cmp_row("Health", pf.get("health", 0), ef.get("health", 0))
            cmp_row(
                "Damage/Round",
                pf.get("damage_per_round", 0),
                ef.get("damage_per_round", 0),
            )
            cmp_row("Crit Chance", pf.get("crit_chance", 0), ef.get("crit_chance", 0))
            cmp_row(
                "Crit Damage",
                pf.get("crit_damage", 0),
                ef.get("crit_damage", 0),
                fmt="{:>15.2f}",
            )
            cmp_row(
                "Armour Pierce", pf.get("armour_pierce", 0), ef.get("armour_pierce", 0)
            )
            cmp_row("Armour", pf.get("armour", 0), ef.get("armour", 0))
            cmp_row(
                "Shield Deflection",
                pf.get("shield_deflection", 0),
                ef.get("shield_deflection", 0),
            )
            cmp_row("Dodge", pf.get("dodge", 0), ef.get("dodge", 0))

        print(f"\n  --- Crew ---")
        print(f"  {'Your Crew':30s} {', '.join(p.get('officers', []))}")
        print(f"  {'Enemy Crew':30s} {', '.join(e.get('officers', []))}")

        if pf and ef:
            print(f"  {'Your Captain Maneuver':30s} {pf.get('captain_maneuver', '')}")
            print(f"  {'Enemy Captain Maneuver':30s} {ef.get('captain_maneuver', '')}")

        print(f"\n  --- Combat Performance ---")
        cmp_row("Total Attacks", a["player_attacks"], a["enemy_attacks"])
        cmp_row("Total Damage Dealt", a["player_total_dmg"], a["enemy_total_dmg"])
        cmp_row(
            "Avg Damage/Hit", a["player_avg_dmg_per_hit"], a["enemy_avg_dmg_per_hit"]
        )
        cmp_row("Avg Damage/Round", a["player_avg_dpr"], a["enemy_avg_dpr"])
        cmp_row("Critical Hits", a["player_crits"], a["enemy_crits"])
        cmp_row(
            "Crit Rate",
            a["player_crit_rate"] * 100,
            a["enemy_crit_rate"] * 100,
            fmt="{:>14.1f}%",
        )
        cmp_row("Crit Damage Total", a["player_crit_dmg"], a["enemy_crit_dmg"])
        cmp_row(
            "Crit Dmg % of Total",
            a["player_crit_dmg_pct"] * 100,
            a["enemy_crit_dmg_pct"] * 100,
            fmt="{:>14.1f}%",
        )

        p_shield = a["player_shield_lost_round"]
        e_shield = a["enemy_shield_lost_round"]
        print(
            f"  {'Shield Lost (round)':30s} {'R' + str(p_shield) if p_shield else 'Never':>15s}   {'R' + str(e_shield) if e_shield else 'Never':>15s}"
        )

        hull_r = p.get("hull_remaining", 0)
        hull_e = e.get("hull_remaining", 0)
        hull_t_p = p.get("hull_health", 1)
        hull_t_e = e.get("hull_health", 1)
        print(f"  {'Hull Remaining':30s} {hull_r:>15,.0f}   {hull_e:>15,.0f}")
        print(
            f"  {'Hull Remaining %':30s} {hull_r / hull_t_p * 100:>14.1f}%   {hull_e / hull_t_e * 100:>14.1f}%"
        )

        # Key findings / why you lost (or won)
        print(f"\n  --- Key Findings ---")
        findings = self._generate_findings(battle)
        for i, finding in enumerate(findings, 1):
            print(f"  {i}. {finding}")

        return findings

    def _generate_findings(self, battle: Dict) -> List[str]:
        """Generate human-readable findings about what decided the battle."""
        a = battle["analysis"]
        pf = battle["player_fleet"]
        ef = battle["enemy_fleet"]
        p = battle["player"]
        e = battle["enemy"]
        findings = []

        lost = a["outcome"] == "DEFEAT"

        # Critical damage comparison
        p_cd = pf.get("crit_damage", 0) if pf else 0
        e_cd = ef.get("crit_damage", 0) if ef else 0
        if e_cd > p_cd and lost:
            gap_pct = (e_cd - p_cd) / max(p_cd, 0.01) * 100
            findings.append(
                f"CRITICAL DAMAGE GAP: Enemy had {e_cd:.2f}x vs your {p_cd:.2f}x "
                f"({gap_pct:.0f}% advantage). This is the #1 factor in PvP burst damage."
            )

        # Crit hit count
        if a["enemy_crits"] > a["player_crits"] and lost:
            findings.append(
                f"CRITICAL HITS: Enemy landed {a['enemy_crits']} crits vs your "
                f"{a['player_crits']}. Crit damage accounted for "
                f"{a['enemy_crit_dmg_pct'] * 100:.1f}% of their total damage."
            )

        # Shield depletion timing
        p_shield = a["player_shield_lost_round"]
        e_shield = a["enemy_shield_lost_round"]
        if p_shield and e_shield and p_shield < e_shield and lost:
            findings.append(
                f"SHIELD TIMING: Your shields fell Round {p_shield}, enemy's Round "
                f"{e_shield} ({e_shield - p_shield} rounds of unshielded hull damage)."
            )
        elif p_shield and not e_shield and lost:
            findings.append(
                f"SHIELD TIMING: Your shields fell Round {p_shield} but enemy's "
                f"shields never depleted."
            )

        # Stat advantage that didn't help
        if pf and ef:
            p_atk = pf.get("attack", 0)
            e_atk = ef.get("attack", 0)
            p_def = pf.get("defense", 0)
            e_def = ef.get("defense", 0)
            p_hp = pf.get("health", 0)
            e_hp = ef.get("health", 0)
            if lost and p_atk > e_atk and p_def > e_def:
                findings.append(
                    f"STAT PARADOX: You had superior attack ({p_atk:,.0f} vs "
                    f"{e_atk:,.0f}) AND defense ({p_def:,.0f} vs {e_def:,.0f}) "
                    f"but still lost. Crew synergy and ability effects outweighed raw stats."
                )

        # Crew analysis
        p_officers = p.get("officers", [])
        e_officers = e.get("officers", [])
        if lost and p_officers and e_officers:
            findings.append(
                f"YOUR CREW: {', '.join(p_officers)} -- review whether these officers "
                f"have PvP-specific abilities or if they're better suited for PvE."
            )
            findings.append(
                f"ENEMY CREW: {', '.join(e_officers)} -- analyze their synergy chain "
                f"and what states/effects they apply to understand their advantage."
            )

        # Enemy abilities fired
        enemy_abs = a.get("enemy_abilities_fired", [])
        if enemy_abs:
            unique_abs = {}
            for ab in enemy_abs:
                key = f"{ab['name']} ({ab['owner']})"
                unique_abs[key] = unique_abs.get(key, 0) + 1
            ab_strs = [f"{k} x{v}" for k, v in unique_abs.items()]
            findings.append(f"ENEMY ABILITIES: {'; '.join(ab_strs)}")

        # Damage per round trend
        if a["enemy_dpr"]:
            rounds_sorted = sorted(a["enemy_dpr"].keys())
            if len(rounds_sorted) >= 4:
                first_half = rounds_sorted[: len(rounds_sorted) // 2]
                second_half = rounds_sorted[len(rounds_sorted) // 2 :]
                avg_first = sum(a["enemy_dpr"][r] for r in first_half) / len(first_half)
                avg_second = sum(a["enemy_dpr"][r] for r in second_half) / len(
                    second_half
                )
                if avg_second > avg_first * 1.3:
                    findings.append(
                        f"DAMAGE ESCALATION: Enemy damage ramped from avg "
                        f"{avg_first:,.0f}/round (early) to {avg_second:,.0f}/round "
                        f"(late) -- {avg_second / max(avg_first, 1):.1f}x increase. "
                        f"Their crew abilities compound over time."
                    )

        if not findings:
            findings.append("No major anomalies detected in this battle.")

        return findings

    def get_weakness_profile(self) -> Dict:
        """Across all loaded battles, summarize what keeps beating the player.

        Returns a dict of weakness categories with severity scores (0-10) that
        the crew evaluator can use to bias scoring.
        """
        profile = {
            "crit_damage_gap": 0.0,
            "crit_hit_disadvantage": 0.0,
            "shield_timing_loss": 0.0,
            "stat_paradox": 0.0,
            "state_vulnerability": 0.0,  # breach/burning/assimilate applied to us
            "damage_escalation": 0.0,
            "losses": 0,
            "total_battles": len(self.battles),
        }

        for b in self.battles:
            a = b["analysis"]
            pf = b["player_fleet"]
            ef = b["enemy_fleet"]
            if a["outcome"] != "DEFEAT":
                continue

            profile["losses"] += 1

            # Crit damage gap severity
            p_cd = pf.get("crit_damage", 0) if pf else 0
            e_cd = ef.get("crit_damage", 0) if ef else 0
            if e_cd > p_cd:
                gap = (e_cd - p_cd) / max(p_cd, 0.01)
                profile["crit_damage_gap"] = max(
                    profile["crit_damage_gap"], min(gap * 10, 10)
                )

            # Crit hit count disadvantage
            if a["enemy_crits"] > a["player_crits"]:
                ratio = a["enemy_crits"] / max(a["player_crits"], 1)
                profile["crit_hit_disadvantage"] = max(
                    profile["crit_hit_disadvantage"], min(ratio * 2, 10)
                )

            # Shield timing
            ps = a["player_shield_lost_round"]
            es = a["enemy_shield_lost_round"]
            if ps and es and ps < es:
                diff = es - ps
                profile["shield_timing_loss"] = max(
                    profile["shield_timing_loss"], min(diff * 2, 10)
                )
            elif ps and not es:
                profile["shield_timing_loss"] = max(profile["shield_timing_loss"], 8)

            # Stat paradox (lost despite better stats)
            if pf and ef:
                if pf.get("attack", 0) > ef.get("attack", 0) and pf.get(
                    "defense", 0
                ) > ef.get("defense", 0):
                    profile["stat_paradox"] = max(profile["stat_paradox"], 8)

            # Damage escalation
            if a["enemy_dpr"]:
                rounds_sorted = sorted(a["enemy_dpr"].keys())
                if len(rounds_sorted) >= 4:
                    first_half = rounds_sorted[: len(rounds_sorted) // 2]
                    second_half = rounds_sorted[len(rounds_sorted) // 2 :]
                    avg_first = sum(a["enemy_dpr"][r] for r in first_half) / max(
                        len(first_half), 1
                    )
                    avg_second = sum(a["enemy_dpr"][r] for r in second_half) / max(
                        len(second_half), 1
                    )
                    if avg_first > 0 and avg_second > avg_first * 1.3:
                        escalation = avg_second / avg_first
                        profile["damage_escalation"] = max(
                            profile["damage_escalation"], min(escalation * 2, 10)
                        )

        return profile


# ---------------------------------------------------------------------------
# PvP Crew Evaluator (synergy-aware)
# ---------------------------------------------------------------------------


class PvPCrewEvaluator:
    """Evaluates 3-officer crew combinations for various STFC scenarios,
    considering synergy chains, state effects, critical damage, and
    weaknesses identified from battle log analysis.

    Supported scenarios:
      pvp         — Pure player-vs-player combat
      hybrid      — Dual-purpose PvE grinding + PvP survivability
      base_cracker — Attacking player stations/bases
      pve_hostile — Grinding hostile NPCs (dailies, systems)
      mission_boss — Story/event boss fights (high HP single target)
      loot        — Mining, cargo runs, resource gathering
      armada      — Armada boss fights (co-op)
    """

    ALL_SCENARIOS = [
        "pvp",
        "hybrid",
        "base_cracker",
        "pve_hostile",
        "mission_boss",
        "loot",
        "armada",
    ]

    # Extended scenarios including mining subcategories (for dock loadout)
    MINING_SUB_SCENARIOS = [
        "mining_speed",
        "mining_protected",
        "mining_crystal",
        "mining_gas",
        "mining_ore",
        "mining_general",
    ]

    # All scenarios including mining subs (for dock loadout UI)
    ALL_DOCK_SCENARIOS = ALL_SCENARIOS + MINING_SUB_SCENARIOS

    SCENARIO_LABELS = {
        "pvp": "PvP (Player Combat)",
        "hybrid": "Hybrid PvE/PvP",
        "base_cracker": "Base Cracker (Station Attack)",
        "pve_hostile": "PvE Hostiles",
        "mission_boss": "Mission Boss",
        "loot": "Loot / Resource Gathering",
        "armada": "Armada",
        # Mining subcategories
        "mining_speed": "Mining Speed (All Resources)",
        "mining_protected": "Mining Protected Cargo",
        "mining_crystal": "Mining Crystal",
        "mining_gas": "Mining Gas",
        "mining_ore": "Mining Ore",
        "mining_general": "Mining General",
    }

    # Ship recommendation per scenario (default best, with reasons)
    SHIP_RECOMMENDATIONS = {
        "pvp": {
            "best": "explorer",
            "reason": "Explorers have the highest base HP and defense, giving survivability advantage in PvP",
            "viable": ["explorer", "battleship", "interceptor"],
        },
        "hybrid": {
            "best": "explorer",
            "reason": "Explorers are the safest hybrid choice — strong PvE and can survive PvP encounters",
            "viable": ["explorer", "battleship"],
        },
        "base_cracker": {
            "best": "interceptor",
            "reason": "Interceptors deal the most burst damage, ideal for breaking station defenses quickly",
            "viable": ["interceptor", "battleship", "explorer"],
        },
        "pve_hostile": {
            "best": "explorer",
            "reason": "Explorers have the best sustained survivability for hostile grinding",
            "viable": ["explorer", "battleship", "interceptor"],
        },
        "mission_boss": {
            "best": "battleship",
            "reason": "Battleships have the highest raw damage output for single-target boss fights",
            "viable": ["battleship", "explorer", "interceptor"],
        },
        "loot": {
            "best": "explorer",
            "reason": "Explorers are safest for mining/cargo — high defense means you survive when jumped",
            "viable": ["explorer", "battleship", "interceptor"],
        },
        "armada": {
            "best": "explorer",
            "reason": "Explorers provide good balanced stats for sustained armada boss fights",
            "viable": ["explorer", "battleship", "interceptor"],
        },
        # Mining sub-scenarios all use survey/explorer for mining
        "mining_speed": {
            "best": "explorer",
            "reason": "Explorers are safest for extended mining operations",
            "viable": ["explorer"],
        },
        "mining_protected": {
            "best": "explorer",
            "reason": "Explorers survive ganks while protecting cargo",
            "viable": ["explorer"],
        },
        "mining_crystal": {
            "best": "explorer",
            "reason": "Explorers for crystal mining with defense against raiders",
            "viable": ["explorer"],
        },
        "mining_gas": {
            "best": "explorer",
            "reason": "Explorers for gas mining with defense against raiders",
            "viable": ["explorer"],
        },
        "mining_ore": {
            "best": "explorer",
            "reason": "Explorers for ore mining with defense against raiders",
            "viable": ["explorer"],
        },
        "mining_general": {
            "best": "explorer",
            "reason": "Explorers are the safest all-around mining ship",
            "viable": ["explorer"],
        },
    }

    # State categories and which officers apply/benefit from them
    STATE_KEYWORDS = {
        "morale": {
            "apply": ["morale for", "inspire morale", "apply morale"],
            "benefit": [
                "when.*morale",
                "with morale",
                "has morale",
                "ship has morale",
                "ship.*morale",
            ],
        },
        "breach": {
            "apply": [
                "hull breach for",
                "apply hull breach",
                "cause hull breach",
                "inflict hull breach",
            ],
            "benefit": [
                "has hull breach",
                "with hull breach",
                "opponent.*hull breach",
                "hull breach,",
            ],
        },
        "burning": {
            "apply": [
                "burning for",
                "apply burning",
                "cause burning",
                "inflict burning",
            ],
            "benefit": ["is burning", "has burning", "opponent.*burning", "burning,"],
        },
        "assimilate": {
            "apply": ["assimilate for", "apply assimilate"],
            "benefit": ["with assimilate", "has assimilate"],
        },
    }

    # Ship types and their "other" types (for penalty detection)
    SHIP_TYPES = {
        "explorer": {"keywords": ["explorer"], "others": ["interceptor", "battleship"]},
        "battleship": {
            "keywords": ["battleship"],
            "others": ["interceptor", "explorer"],
        },
        "interceptor": {
            "keywords": ["interceptor"],
            "others": ["battleship", "explorer"],
        },
    }

    def __init__(
        self,
        officers: List[Dict],
        weakness_profile: Optional[Dict] = None,
        ship_type: str = "explorer",
    ):
        self.officers = officers
        self.weakness = weakness_profile or {}
        self.ship_type = ship_type.lower()
        self._classify_officers()

    def _classify_officers(self):
        """Tag each officer with PvP-relevant classifications."""
        ship_info = self.SHIP_TYPES.get(self.ship_type, self.SHIP_TYPES["explorer"])
        ship_keywords = ship_info["keywords"]

        for off in self.officers:
            display = off.get("display", "")
            effect = off.get("effect", "")
            cause = off.get("cause", "").lower()

            off["_pvp_tags"] = set()
            off["_states_applied"] = set()
            off["_states_benefit"] = set()
            off["_is_ship_specific"] = False  # Matches current ship type
            off["_is_pvp_specific"] = False
            off["_is_pve_specific"] = False
            off["_is_dual_use"] = False
            off["_crit_related"] = False
            off["_shield_related"] = False
            off["_shots_related"] = False
            off["_mitigation_related"] = False
            off["_isolytic_related"] = False
            off["_weapon_delay"] = False
            off["_ability_amplifier"] = False
            off["_stat_booster"] = False

            # --- Scenario-specific tags ---
            off["_base_attack"] = False  # Attacking stations
            off["_base_defend"] = False  # Defending stations
            off["_pve_hostile"] = False  # Hostile NPC combat
            off["_mission_boss"] = False  # Mission/yellow hostiles
            off["_mining"] = False  # Mining speed/efficiency
            off["_cargo"] = False  # Cargo/protected cargo
            off["_loot"] = False  # Loot rewards
            off["_warp"] = False  # Warp speed/range
            off["_armada"] = False  # Armada combat
            off["_armada_solo"] = False  # Solo armada specifically
            off["_repair"] = False  # Ship repair
            off["_apex"] = False  # Apex barrier/shred
            off["_non_armada_only"] = False  # Explicitly non-armada hostiles only

            # --- Mining subcategory tags (for dock loadout) ---
            off["_mining_crystal"] = False  # Crystal-specific mining
            off["_mining_gas"] = False  # Gas-specific mining
            off["_mining_ore"] = False  # Ore-specific mining
            off["_mining_speed"] = False  # Mining speed (all resources)
            off["_protected_cargo"] = False  # Protected cargo capacity
            off["_node_defense"] = False  # Defense while mining on a node

            # Base cracker / station combat
            base_attack_kw = [
                "attacking a station",
                "attack.*station",
                "defence platform",
                "defense platform",
                "station combat",
                "station and ship mitigation",
                "damage to defence",
                "damage to defense",
            ]
            base_defend_kw = [
                "defending the station",
                "defending a station",
            ]
            if any(kw in display for kw in base_attack_kw):
                off["_base_attack"] = True
            if any(kw in display for kw in base_defend_kw):
                off["_base_defend"] = True

            # PvE hostile combat
            pve_hostile_kw = [
                "hostile",
                "non-player",
                "hosilte",  # common typo
            ]
            if any(kw in display for kw in pve_hostile_kw):
                off["_pve_hostile"] = True

            # Non-armada only (these work vs hostiles but NOT in armadas)
            if "non-armada" in display or "non armada" in display:
                off["_non_armada_only"] = True

            # Mission boss (yellow hostiles / mission hostiles)
            if "mission" in display:
                off["_mission_boss"] = True
            # Mission bosses are also hostiles — officers that work vs
            # "non-player" targets or "hostiles" are effective here too

            # Mining / resource gathering
            if "mining" in display:
                off["_mining"] = True
            if any(
                kw in display
                for kw in [
                    "cargo",
                    "protected cargo",
                ]
            ):
                off["_cargo"] = True
            if "loot" in display or "reward" in display:
                off["_loot"] = True
            if any(
                kw in display
                for kw in [
                    "warp range",
                    "warp speed",
                    "warp distance",
                ]
            ):
                off["_warp"] = True

            # Mining subcategories (for dock loadout)
            if any(kw in display for kw in ["crystal", "raw crystal"]):
                off["_mining_crystal"] = True
            if any(kw in display for kw in ["gas", "raw gas"]):
                off["_mining_gas"] = True
            if any(kw in display for kw in ["ore", "raw ore"]):
                off["_mining_ore"] = True
            if any(
                kw in display
                for kw in [
                    "mining speed",
                    "mining rate",
                    "mining efficiency",
                ]
            ):
                off["_mining_speed"] = True
            if any(kw in display for kw in ["protected cargo", "protect cargo"]):
                off["_protected_cargo"] = True
            if any(
                kw in display
                for kw in [
                    "while mining",
                    "on a mining node",
                    "defending a mining",
                    "mining defense",
                    "mining node",
                ]
            ):
                off["_node_defense"] = True

            # Armada
            if "armada" in display:
                off["_armada"] = True
            if "solo armada" in display:
                off["_armada_solo"] = True

            # Repair
            if "repair" in display:
                off["_repair"] = True

            # Apex mechanics
            if any(kw in display for kw in ["apex barrier", "apex shred", "apex"]):
                off["_apex"] = True

            # PvP specific
            if any(
                kw in display
                for kw in ["player", "pvp", "against player", "fighting player"]
            ):
                off["_is_pvp_specific"] = True
                off["_pvp_tags"].add("pvp")

            # PvE specific (hostile-only, mining, etc.)
            pve_only_keywords = [
                "hostile",
                "mining",
                "cargo",
                "resources",
                "warp range",
                "warp speed",
                "non-player",
                "reputation",
            ]
            if (
                any(kw in display for kw in pve_only_keywords)
                and not off["_is_pvp_specific"]
            ):
                off["_is_pve_specific"] = True

            # Dual-use: abilities that work against both or have no restriction
            # Check if display mentions BOTH player and hostile contexts,
            # or has universal effects (no target restriction)
            has_player_ref = any(kw in display for kw in ["player", "pvp"])
            has_hostile_ref = any(
                kw in display for kw in ["hostile", "non-player", "armada"]
            )
            # Universal: no target restriction mentioned at all
            has_no_target_lock = not has_player_ref and not has_hostile_ref
            if (has_player_ref and has_hostile_ref) or has_no_target_lock:
                off["_is_dual_use"] = True

            # Ability amplifier (like Pike's "increase effectiveness of all officer abilities")
            if any(
                kw in display
                for kw in [
                    "effectiveness of all officer",
                    "increase.*officer abilit",
                    "all officer stats",
                    "all officers",
                ]
            ):
                off["_ability_amplifier"] = True

            # Generic stat booster (officer stats, all stats, etc.)
            if any(
                kw in display
                for kw in [
                    "officer stats",
                    "all officer",
                    "officer attack",
                    "officer defence",
                    "officer defense",
                    "officer health",
                ]
            ):
                off["_stat_booster"] = True

            # Ship-type specific (matches whatever ship we're evaluating for)
            if any(kw in display for kw in ship_keywords):
                off["_is_ship_specific"] = True
                off["_pvp_tags"].add(self.ship_type)

            # State classification
            for state, patterns in self.STATE_KEYWORDS.items():
                if state in effect:
                    off["_pvp_tags"].add(state)
                    if cause == "y":
                        off["_states_applied"].add(state)
                for pat in patterns["apply"]:
                    if pat in display:
                        off["_states_applied"].add(state)
                        off["_pvp_tags"].add(state)
                for pat in patterns["benefit"]:
                    # Simple substring check; regex would be better but keep it lightweight
                    if pat.replace(".*", "") in display:
                        off["_states_benefit"].add(state)
                        off["_pvp_tags"].add(state)

            # Critical hit related
            if any(kw in display for kw in ["critical hit", "critical damage", "crit"]):
                off["_crit_related"] = True
                off["_pvp_tags"].add("crit")

            # Shield related
            if any(kw in display for kw in ["shield", "shp"]):
                off["_shield_related"] = True

            # Weapon shots
            if any(
                kw in display for kw in ["shots", "number of shots", "weapon shots"]
            ):
                off["_shots_related"] = True
                off["_pvp_tags"].add("shots")

            # Mitigation
            if any(
                kw in display
                for kw in ["mitigation", "armour", "armor", "dodge", "deflection"]
            ):
                off["_mitigation_related"] = True

            # Isolytic
            if any(kw in display for kw in ["isolytic", "apex"]):
                off["_isolytic_related"] = True
                off["_pvp_tags"].add("isolytic")

            # Weapon delay
            if "delay" in display:
                off["_weapon_delay"] = True
                off["_pvp_tags"].add("delay")

    def score_individual(self, officer: Dict) -> float:
        """Base score for an individual officer in PvP context for current ship type."""
        atk = officer["attack"]
        defense = officer["defense"]
        health = officer["health"]
        score = 0.0

        # Base stat score (normalized — raw stats matter but aren't dominant)
        stat_score = atk * 0.5 + defense * 0.3 + health * 0.2
        score += stat_score

        # PvP-specific bonus (big deal — PvE officers are dead weight in PvP)
        if officer["_is_pvp_specific"]:
            score *= 1.4

        # Ship-type-specific bonus
        if officer["_is_ship_specific"]:
            score *= 1.5

        # State applicator bonus (can trigger synergy chains)
        if officer["_states_applied"]:
            score *= 1.3

        # Crit-related bonus (counters the #1 weakness)
        if officer["_crit_related"]:
            crit_weight = 1.3
            if self.weakness.get("crit_damage_gap", 0) > 5:
                crit_weight = 1.6  # Extra weight if crit gap is a known problem
            score *= crit_weight

        # Weapon shots (more shots = more crit chances)
        if officer["_shots_related"]:
            score *= 1.25

        # Isolytic (modern PvP mechanic)
        if officer["_isolytic_related"]:
            score *= 1.2

        # Weapon delay (very strong in PvP)
        if officer["_weapon_delay"]:
            score *= 1.3

        # Use flag from spreadsheet (player has marked as usable)
        if officer.get("use") == "Y":
            score *= 1.15

        return score

    def _cm_is_bda(self, officer: Dict) -> bool:
        """Check if an officer's CM slot is actually a Below Deck Ability (BDA).
        BDA officers typically have absurdly high CM% values (100000%+) or their
        display text starts with 'bda:'."""
        display = officer.get("display", "")
        if display.startswith("bda:"):
            return True
        if officer["cm_pct"] >= 10000:
            return True
        return False

    def _cm_works_on_ship(self, officer: Dict) -> bool:
        """Check if the captain maneuver works on the current ship type
        (not locked to a different ship type)."""
        display = officer.get("display", "")
        # Extract the CM portion (before "oa:" or "bda:")
        cm_text = display
        for delimiter in [" oa:", " bda:"]:
            idx = cm_text.find(delimiter)
            if idx > 0:
                cm_text = cm_text[:idx]
                break

        # Check if CM is locked to a DIFFERENT ship type
        ship_info = self.SHIP_TYPES.get(self.ship_type, self.SHIP_TYPES["explorer"])
        for other_type in ship_info["others"]:
            if (
                f"on an {other_type}" in cm_text
                or f"on a {other_type}" in cm_text
                or f"on {other_type}" in cm_text
                or f"while on {other_type}" in cm_text
            ):
                return False
        return True

    def _oa_works_on_ship(self, officer: Dict) -> bool:
        """Check if the officer ability works on the current ship type
        (not locked to a different ship type)."""
        d = officer.get("display", "")
        oa_text = ""
        for delimiter in ["oa:", "bda:"]:
            idx = d.find(delimiter)
            if idx >= 0:
                oa_text = d[idx:]
                break
        if not oa_text:
            return True  # No OA text = no lock

        ship_info = self.SHIP_TYPES.get(self.ship_type, self.SHIP_TYPES["explorer"])
        for other_type in ship_info["others"]:
            if (
                f"on an {other_type}" in oa_text
                or f"on a {other_type}" in oa_text
                or f"on {other_type}" in oa_text
                or f"while on {other_type}" in oa_text
            ):
                return False
        return True

    def score_crew(
        self, captain: Dict, bridge1: Dict, bridge2: Dict
    ) -> Tuple[float, Dict]:
        """Score a 3-officer crew combination for PvP on the current ship type.

        Returns (total_score, breakdown_dict) where breakdown explains
        why this crew scores the way it does.
        """
        ship_label = self.ship_type.capitalize()
        crew = [captain, bridge1, bridge2]
        breakdown = {
            "captain": captain["name"],
            "bridge": [bridge1["name"], bridge2["name"]],
            "individual_scores": {},
            "synergy_bonus": 0,
            "state_chain_bonus": 0,
            "crit_bonus": 0,
            "ship_type_bonus": 0,
            "weakness_counter_bonus": 0,
            "penalties": [],
            "synergy_notes": [],
        }

        # --- Individual scores ---
        total = 0.0
        for off in crew:
            ind_score = self.score_individual(off)
            breakdown["individual_scores"][off["name"]] = ind_score
            total += ind_score

        # --- Captain Maneuver value ---
        # Captain's CM% matters more since it fires every round.
        # But BDA officers have inflated CM% that isn't a real captain maneuver.
        if self._cm_is_bda(captain):
            # BDA captains get no CM bonus and a penalty (wasting the captain slot)
            penalty = total * 0.25
            total -= penalty
            breakdown["penalties"].append(
                f"'{captain['name']}' has a BDA (Below Deck Ability), not a Captain "
                f"Maneuver — wasted captain slot (-{penalty:,.0f})"
            )
        elif not self._cm_works_on_ship(captain):
            # CM is locked to another ship type
            penalty = total * 0.2
            total -= penalty
            breakdown["penalties"].append(
                f"'{captain['name']}' CM is locked to non-{ship_label} ship type (-{penalty:,.0f})"
            )
        else:
            # Real CM that works on our ship — reward it
            cm_bonus = min(captain["cm_pct"], 500) * 50  # Cap at 500% to avoid outliers
            total += cm_bonus

        # --- State synergy chain ---
        # Check if the crew has a state applicator AND officers who benefit from that state
        all_applied = set()
        all_benefit = set()
        for off in crew:
            all_applied |= off["_states_applied"]
            all_benefit |= off["_states_benefit"]

        # States where we both apply AND benefit = synergy chain
        synergy_states = all_applied & all_benefit
        if synergy_states:
            chain_bonus = len(synergy_states) * 50000
            # Count how many officers benefit from the applied states
            beneficiaries = 0
            for off in crew:
                if off["_states_benefit"] & all_applied:
                    beneficiaries += 1
            chain_bonus *= 1 + beneficiaries * 0.5
            total += chain_bonus
            breakdown["state_chain_bonus"] = chain_bonus
            breakdown["synergy_notes"].append(
                f"State synergy chain: {', '.join(synergy_states)} "
                f"({beneficiaries} officers benefit)"
            )
        elif not all_applied:
            # No state applicator = huge penalty (conditional abilities won't fire)
            penalty = total * 0.3
            total -= penalty
            breakdown["penalties"].append(
                f"No state applicator (-{penalty:,.0f}): conditional abilities may not trigger"
            )

        # --- Shared state coherence ---
        # Officers that all work with the SAME state are better than a mix
        state_counts = {}
        for off in crew:
            for state in off["_pvp_tags"] & {
                "morale",
                "breach",
                "burning",
                "assimilate",
            }:
                state_counts[state] = state_counts.get(state, 0) + 1
        if state_counts:
            best_state_count = max(state_counts.values())
            if best_state_count >= 3:
                coherence_bonus = 40000
                total += coherence_bonus
                best_state = [
                    s for s, c in state_counts.items() if c == best_state_count
                ][0]
                breakdown["synergy_bonus"] += coherence_bonus
                breakdown["synergy_notes"].append(
                    f"Full crew coherence on '{best_state}' (all 3 officers)"
                )
            elif best_state_count == 2:
                coherence_bonus = 15000
                total += coherence_bonus
                best_state = [
                    s for s, c in state_counts.items() if c == best_state_count
                ][0]
                breakdown["synergy_bonus"] += coherence_bonus
                breakdown["synergy_notes"].append(
                    f"Partial crew coherence on '{best_state}' (2/3 officers)"
                )

        # --- Critical hit coverage ---
        crit_officers = sum(1 for off in crew if off["_crit_related"])
        if crit_officers >= 2:
            crit_bonus = 30000
            total += crit_bonus
            breakdown["crit_bonus"] = crit_bonus
            breakdown["synergy_notes"].append(
                f"Strong crit coverage ({crit_officers}/3 officers affect crit)"
            )

        # --- Ship-type-specific coverage ---
        ship_officers = sum(1 for off in crew if off["_is_ship_specific"])
        if ship_officers >= 2:
            exp_bonus = 25000
            total += exp_bonus
            breakdown["ship_type_bonus"] = exp_bonus
            breakdown["synergy_notes"].append(
                f"{ship_label}-specialized ({ship_officers}/3 officers have {ship_label} abilities)"
            )

        # --- Weakness counter bonuses (from battle log analysis) ---
        weakness_bonus = 0
        if self.weakness:
            # Counter crit damage gap
            if self.weakness.get("crit_damage_gap", 0) > 3:
                # Reward crews that reduce enemy crit chance/damage
                crit_reducers = 0
                for off in crew:
                    d = off.get("display", "")
                    if any(
                        kw in d
                        for kw in [
                            "decrease.*critical",
                            "reduce.*critical",
                            "critical hit chance",
                            "critical hit damage",
                            "opponent's critical",
                            "decreases opponent",
                            "reduces.*crit",
                        ]
                    ):
                        # Simple substring fallback
                        if "decrease" in d and "critical" in d:
                            crit_reducers += 1
                        elif "reduce" in d and "critical" in d:
                            crit_reducers += 1
                        elif "opponent" in d and "critical" in d:
                            crit_reducers += 1
                if crit_reducers > 0:
                    bonus = (
                        crit_reducers * 20000 * (self.weakness["crit_damage_gap"] / 10)
                    )
                    weakness_bonus += bonus
                    breakdown["synergy_notes"].append(
                        f"Counters crit gap weakness ({crit_reducers} crit reducers)"
                    )

            # Counter shield timing loss
            if self.weakness.get("shield_timing_loss", 0) > 3:
                shield_helpers = sum(1 for off in crew if off["_shield_related"])
                if shield_helpers > 0:
                    bonus = (
                        shield_helpers
                        * 10000
                        * (self.weakness["shield_timing_loss"] / 10)
                    )
                    weakness_bonus += bonus

            # Counter damage escalation
            if self.weakness.get("damage_escalation", 0) > 3:
                mitigators = sum(1 for off in crew if off["_mitigation_related"])
                if mitigators > 0:
                    bonus = mitigators * 10000
                    weakness_bonus += bonus

        total += weakness_bonus
        breakdown["weakness_counter_bonus"] = weakness_bonus

        # --- Penalty: PvE-only officers in PvP ---
        for off in crew:
            d = off.get("display", "")
            if any(
                kw in d
                for kw in [
                    "hostile",
                    "mining",
                    "cargo",
                    "resources",
                    "armada only",
                    "non-player",
                    "reputation",
                    "warp range",
                    "warp speed",
                ]
            ):
                if not off["_is_pvp_specific"]:
                    penalty_amt = total * 0.15
                    total -= penalty_amt
                    breakdown["penalties"].append(
                        f"PvE officer '{off['name']}' in PvP crew (-{penalty_amt:,.0f})"
                    )

        # --- Penalty: Bridge officer OA locked to non-matching ship type ---
        for off in [bridge1, bridge2]:
            if not self._oa_works_on_ship(off):
                penalty_amt = total * 0.15
                total -= penalty_amt
                breakdown["penalties"].append(
                    f"'{off['name']}' OA is locked to non-{ship_label} ship type (-{penalty_amt:,.0f})"
                )

        return total, breakdown

    def find_best_crews(
        self,
        ship_type: str = "explorer",
        top_n: int = 5,
        excluded_officers: Optional[Set[str]] = None,
    ) -> List[Tuple[float, Dict]]:
        """Evaluate all valid 3-officer combinations and return the top N.

        To keep runtime reasonable, first shortlists the top ~40 individual
        scorers, then evaluates all C(40,3) = 9,880 combinations.

        Args:
            excluded_officers: Set of officer names to exclude (for multi-dock loadouts).
        """
        exclude = excluded_officers or set()
        # Pre-filter: take top individual scorers
        scored_individuals = []
        for off in self.officers:
            if off["name"] in exclude:
                continue
            s = self.score_individual(off)
            scored_individuals.append((s, off))
        scored_individuals.sort(key=lambda x: x[0], reverse=True)

        # Take top 40 candidates (balances thoroughness vs runtime)
        candidates = [off for _, off in scored_individuals[:40]]

        print(
            f"  Evaluating {len(candidates)} candidate officers in all 3-officer combinations..."
        )
        print(
            f"  Total combinations to evaluate: {len(candidates) * (len(candidates) - 1) * (len(candidates) - 2) // 6:,}"
        )

        results = []
        for combo in combinations(candidates, 3):
            # Try each officer as captain (captain's CM fires every round)
            for captain_idx in range(3):
                captain = combo[captain_idx]
                bridge = [combo[j] for j in range(3) if j != captain_idx]
                score, breakdown = self.score_crew(captain, bridge[0], bridge[1])
                results.append((score, breakdown))

        results.sort(key=lambda x: x[0], reverse=True)

        # Deduplicate (same 3 names in different order)
        seen = set()
        unique_results = []
        for score, bd in results:
            key = frozenset([bd["captain"]] + bd["bridge"])
            if key not in seen:
                seen.add(key)
                unique_results.append((score, bd))
            if len(unique_results) >= top_n:
                break

        return unique_results

    # ------------------------------------------------------------------
    # Hybrid PvE/PvP scoring — dual-purpose crew evaluation
    # ------------------------------------------------------------------

    def score_hybrid_individual(self, officer: Dict) -> float:
        """Score an individual officer for hybrid PvE/PvP dual-use value.

        Philosophy: the ideal hybrid officer has abilities that fire against
        BOTH hostiles and players (or have no target restriction at all).
        Officers locked to only PvE or only PvP are penalized because they
        go dead in half the use cases.

        The reference crew is Jimbob1325's Pike/Moreau/Chen — a PvE grinding
        crew that happened to also work in PvP because none of their abilities
        were target-locked.
        """
        atk = officer["attack"]
        defense = officer["defense"]
        health = officer["health"]
        score = 0.0

        # Base stat score — balanced weighting for hybrid (need survivability
        # for PvE grinding AND PvP defense)
        stat_score = atk * 0.4 + defense * 0.35 + health * 0.25
        score += stat_score

        # --- Dual-use bonus (the core hybrid reward) ---
        # Officers whose abilities work everywhere get the biggest boost
        if officer["_is_dual_use"]:
            score *= 1.5

        # --- Ability amplifier bonus (Pike archetype) ---
        # "Increase effectiveness of all officer abilities" works in every
        # game mode — the ultimate hybrid captain ability
        if officer["_ability_amplifier"]:
            score *= 1.6

        # --- Generic stat booster bonus ---
        # Officers that boost raw officer stats work everywhere
        if officer["_stat_booster"]:
            score *= 1.3

        # --- Ship-type-specific bonus (matches our ship) ---
        if officer["_is_ship_specific"]:
            score *= 1.35

        # --- State applicator bonus (synergy chains work in both modes) ---
        if officer["_states_applied"]:
            score *= 1.25

        # --- Crit-related bonus (works in both PvE and PvP) ---
        if officer["_crit_related"]:
            crit_weight = 1.25
            # Extra weight if battle logs show a crit gap weakness
            if self.weakness.get("crit_damage_gap", 0) > 5:
                crit_weight = 1.45
            score *= crit_weight

        # --- Shield-related bonus (survivability in both modes) ---
        if officer["_shield_related"]:
            score *= 1.15

        # --- Weapon shots bonus (more shots = better in all combat) ---
        if officer["_shots_related"]:
            score *= 1.15

        # --- Mitigation bonus (survivability) ---
        if officer["_mitigation_related"]:
            score *= 1.1

        # --- Use flag from spreadsheet ---
        if officer.get("use") == "Y":
            score *= 1.15

        # --- PENALTIES ---

        # PvE-only officer: abilities are useless in PvP (half the hybrid value)
        if officer["_is_pve_specific"] and not officer["_is_dual_use"]:
            score *= 0.45  # Harsh penalty — dead weight when jumped by a player

        # PvP-only officer: abilities are useless in PvE (half the hybrid value)
        if officer["_is_pvp_specific"] and not officer["_is_dual_use"]:
            score *= (
                0.55  # Slightly less harsh — PvP abilities still help when attacked
            )

        return score

    def score_hybrid_crew(
        self, captain: Dict, bridge1: Dict, bridge2: Dict
    ) -> Tuple[float, Dict]:
        """Score a 3-officer crew for hybrid PvE/PvP use on the current ship type.

        The ideal hybrid crew:
        - Has a universal ability amplifier as captain (like Pike's CM)
        - All officers have abilities that work in both PvE and PvP
        - Synergy chains still fire (state applicator + beneficiary)
        - Crit coverage for PvP survivability
        - No single-mode dead weight
        """
        ship_label = self.ship_type.capitalize()
        crew = [captain, bridge1, bridge2]
        breakdown = {
            "captain": captain["name"],
            "bridge": [bridge1["name"], bridge2["name"]],
            "individual_scores": {},
            "synergy_bonus": 0,
            "state_chain_bonus": 0,
            "crit_bonus": 0,
            "ship_type_bonus": 0,
            "dual_use_bonus": 0,
            "amplifier_bonus": 0,
            "weakness_counter_bonus": 0,
            "penalties": [],
            "synergy_notes": [],
        }

        # --- Individual scores ---
        total = 0.0
        for off in crew:
            ind_score = self.score_hybrid_individual(off)
            breakdown["individual_scores"][off["name"]] = ind_score
            total += ind_score

        # --- Captain Maneuver value ---
        if self._cm_is_bda(captain):
            penalty = total * 0.25
            total -= penalty
            breakdown["penalties"].append(
                f"'{captain['name']}' has a BDA, not a Captain Maneuver — "
                f"wasted captain slot (-{penalty:,.0f})"
            )
        elif not self._cm_works_on_ship(captain):
            penalty = total * 0.2
            total -= penalty
            breakdown["penalties"].append(
                f"'{captain['name']}' CM is locked to non-{ship_label} ship type (-{penalty:,.0f})"
            )
        else:
            cm_bonus = min(captain["cm_pct"], 500) * 50
            total += cm_bonus

        # --- Ability amplifier as captain bonus ---
        # Pike-style captains are the gold standard for hybrid: their CM
        # boosts ALL officer abilities, which means bridge officers' PvE
        # abilities hit harder AND their PvP abilities hit harder.
        if captain.get("_ability_amplifier"):
            amp_bonus = 60000
            total += amp_bonus
            breakdown["amplifier_bonus"] = amp_bonus
            breakdown["synergy_notes"].append(
                f"Ability amplifier captain '{captain['name']}' — "
                f"boosts all bridge abilities in both PvE and PvP (+{amp_bonus:,})"
            )

        # --- Dual-use crew coherence ---
        # Reward crews where ALL officers are dual-use (no dead weight in either mode)
        dual_use_count = sum(1 for off in crew if off["_is_dual_use"])
        if dual_use_count == 3:
            du_bonus = 50000
            total += du_bonus
            breakdown["dual_use_bonus"] = du_bonus
            breakdown["synergy_notes"].append(
                f"Full dual-use crew — all 3 officers work in both PvE and PvP (+{du_bonus:,})"
            )
        elif dual_use_count == 2:
            du_bonus = 20000
            total += du_bonus
            breakdown["dual_use_bonus"] = du_bonus
            breakdown["synergy_notes"].append(
                f"Partial dual-use crew — 2/3 officers work in both modes (+{du_bonus:,})"
            )

        # --- State synergy chain (same as PvP — works in both modes) ---
        all_applied = set()
        all_benefit = set()
        for off in crew:
            all_applied |= off["_states_applied"]
            all_benefit |= off["_states_benefit"]

        synergy_states = all_applied & all_benefit
        if synergy_states:
            chain_bonus = len(synergy_states) * 40000  # Slightly less than PvP-only
            beneficiaries = 0
            for off in crew:
                if off["_states_benefit"] & all_applied:
                    beneficiaries += 1
            chain_bonus *= 1 + beneficiaries * 0.5
            total += chain_bonus
            breakdown["state_chain_bonus"] = chain_bonus
            breakdown["synergy_notes"].append(
                f"State synergy chain: {', '.join(synergy_states)} "
                f"({beneficiaries} officers benefit) (+{chain_bonus:,})"
            )

        # --- Shared state coherence ---
        state_counts = {}
        for off in crew:
            for state in off["_pvp_tags"] & {
                "morale",
                "breach",
                "burning",
                "assimilate",
            }:
                state_counts[state] = state_counts.get(state, 0) + 1
        if state_counts:
            best_state_count = max(state_counts.values())
            if best_state_count >= 3:
                coherence_bonus = 30000
                total += coherence_bonus
                best_state = [
                    s for s, c in state_counts.items() if c == best_state_count
                ][0]
                breakdown["synergy_bonus"] += coherence_bonus
                breakdown["synergy_notes"].append(
                    f"Full crew coherence on '{best_state}' (all 3 officers) (+{coherence_bonus:,})"
                )
            elif best_state_count == 2:
                coherence_bonus = 12000
                total += coherence_bonus
                best_state = [
                    s for s, c in state_counts.items() if c == best_state_count
                ][0]
                breakdown["synergy_bonus"] += coherence_bonus
                breakdown["synergy_notes"].append(
                    f"Partial crew coherence on '{best_state}' (2/3 officers) (+{coherence_bonus:,})"
                )

        # --- Critical hit coverage ---
        crit_officers = sum(1 for off in crew if off["_crit_related"])
        if crit_officers >= 2:
            crit_bonus = 25000
            total += crit_bonus
            breakdown["crit_bonus"] = crit_bonus
            breakdown["synergy_notes"].append(
                f"Strong crit coverage ({crit_officers}/3 officers affect crit) (+{crit_bonus:,})"
            )

        # --- Ship-type-specific coverage ---
        ship_officers = sum(1 for off in crew if off["_is_ship_specific"])
        if ship_officers >= 2:
            exp_bonus = 20000
            total += exp_bonus
            breakdown["ship_type_bonus"] = exp_bonus
            breakdown["synergy_notes"].append(
                f"{ship_label}-specialized ({ship_officers}/3 officers) (+{exp_bonus:,})"
            )

        # --- Weakness counter bonuses (from battle log analysis) ---
        weakness_bonus = 0
        if self.weakness:
            if self.weakness.get("crit_damage_gap", 0) > 3:
                crit_reducers = 0
                for off in crew:
                    d = off.get("display", "")
                    if (
                        ("decrease" in d and "critical" in d)
                        or ("reduce" in d and "critical" in d)
                        or ("opponent" in d and "critical" in d)
                    ):
                        crit_reducers += 1
                if crit_reducers > 0:
                    bonus = (
                        crit_reducers * 15000 * (self.weakness["crit_damage_gap"] / 10)
                    )
                    weakness_bonus += bonus
                    breakdown["synergy_notes"].append(
                        f"Counters crit gap weakness ({crit_reducers} crit reducers) (+{bonus:,.0f})"
                    )

            if self.weakness.get("shield_timing_loss", 0) > 3:
                shield_helpers = sum(1 for off in crew if off["_shield_related"])
                if shield_helpers > 0:
                    bonus = (
                        shield_helpers
                        * 8000
                        * (self.weakness["shield_timing_loss"] / 10)
                    )
                    weakness_bonus += bonus

        total += weakness_bonus
        breakdown["weakness_counter_bonus"] = weakness_bonus

        # --- PENALTIES ---

        # Single-mode officers in hybrid crew
        for off in crew:
            if off["_is_pve_specific"] and not off["_is_dual_use"]:
                penalty_amt = total * 0.12
                total -= penalty_amt
                breakdown["penalties"].append(
                    f"PvE-only officer '{off['name']}' — dead weight in PvP (-{penalty_amt:,.0f})"
                )
            elif off["_is_pvp_specific"] and not off["_is_dual_use"]:
                penalty_amt = total * 0.10
                total -= penalty_amt
                breakdown["penalties"].append(
                    f"PvP-only officer '{off['name']}' — dead weight in PvE (-{penalty_amt:,.0f})"
                )

        # Bridge officer OA locked to non-matching ship type
        for off in [bridge1, bridge2]:
            if not self._oa_works_on_ship(off):
                penalty_amt = total * 0.15
                total -= penalty_amt
                breakdown["penalties"].append(
                    f"'{off['name']}' OA locked to non-{ship_label} ship type (-{penalty_amt:,.0f})"
                )

        return total, breakdown

    def find_best_hybrid_crews(
        self,
        ship_type: str = "explorer",
        top_n: int = 5,
        excluded_officers: Optional[Set[str]] = None,
    ) -> List[Tuple[float, Dict]]:
        """Evaluate all valid 3-officer combinations for hybrid PvE/PvP and
        return the top N.

        Uses the hybrid individual scorer for pre-filtering and the hybrid
        crew scorer for full evaluation.

        Args:
            excluded_officers: Set of officer names to exclude (for multi-dock loadouts).
        """
        exclude = excluded_officers or set()
        # Pre-filter: take top individual scorers using hybrid scoring
        scored_individuals = []
        for off in self.officers:
            if off["name"] in exclude:
                continue
            s = self.score_hybrid_individual(off)
            scored_individuals.append((s, off))
        scored_individuals.sort(key=lambda x: x[0], reverse=True)

        # Take top 40 candidates
        candidates = [off for _, off in scored_individuals[:40]]

        print(
            f"  Evaluating {len(candidates)} candidate officers in all 3-officer hybrid combinations..."
        )
        print(
            f"  Total combinations to evaluate: "
            f"{len(candidates) * (len(candidates) - 1) * (len(candidates) - 2) // 6:,}"
        )

        results = []
        for combo in combinations(candidates, 3):
            for captain_idx in range(3):
                captain = combo[captain_idx]
                bridge = [combo[j] for j in range(3) if j != captain_idx]
                score, breakdown = self.score_hybrid_crew(captain, bridge[0], bridge[1])
                results.append((score, breakdown))

        results.sort(key=lambda x: x[0], reverse=True)

        # Deduplicate (same 3 names in different order)
        seen = set()
        unique_results = []
        for score, bd in results:
            key = frozenset([bd["captain"]] + bd["bridge"])
            if key not in seen:
                seen.add(key)
                unique_results.append((score, bd))
            if len(unique_results) >= top_n:
                break

        return unique_results

    # ------------------------------------------------------------------
    # Generalized scenario scoring
    # ------------------------------------------------------------------

    def score_scenario_individual(self, officer: Dict, scenario: str) -> float:
        """Score an individual officer for a given scenario.

        Dispatches to the existing PvP/Hybrid scorers for those scenarios,
        and uses scenario-specific logic for the new ones.
        """
        if scenario == "pvp":
            return self.score_individual(officer)
        elif scenario == "hybrid":
            return self.score_hybrid_individual(officer)

        display = officer.get("display", "")
        atk = officer["attack"]
        defense = officer["defense"]
        health = officer["health"]
        score = 0.0

        # Base stat score (universal)
        stat_score = atk * 0.5 + defense * 0.3 + health * 0.2
        score += stat_score

        if scenario == "base_cracker":
            # Station attack specialists are kings
            if officer.get("_base_attack"):
                score *= 2.5
            # Station defenders are useless for attacking
            if officer.get("_base_defend"):
                score *= 0.1

            # Crit is important for bursting stations
            if officer.get("_crit_related"):
                score *= 1.4
            # Shots increase = more damage to platforms
            if officer.get("_shots_related"):
                score *= 1.3
            # Weapon delay is huge in station combat
            if officer.get("_weapon_delay"):
                score *= 1.4
            # Apex shred is valuable
            if officer.get("_apex") and "shred" in display:
                score *= 1.3
            # Shield piercing helps vs stations
            if "shield piercing" in display or "armor piercing" in display:
                score *= 1.2
            # PvP officers also work (stations are player-owned)
            if officer.get("_is_pvp_specific"):
                score *= 1.2
            # Pure PvE/hostile officers are mostly useless
            if officer.get("_pve_hostile") and not officer.get("_base_attack"):
                if not officer.get("_is_pvp_specific"):
                    score *= 0.4
            # Mining/cargo officers = dead weight
            if officer.get("_mining") or officer.get("_cargo"):
                score *= 0.2

        elif scenario == "pve_hostile":
            # Hostile-specific officers are ideal
            if officer.get("_pve_hostile"):
                score *= 2.0
            # Mitigation reduces incoming hostile damage
            if officer.get("_mitigation_related"):
                score *= 1.5
            # Shield helps survivability for long grinding
            if officer.get("_shield_related"):
                score *= 1.3
            # Crit for faster kills
            if officer.get("_crit_related"):
                score *= 1.3
            # Repair for sustained grinding
            if officer.get("_repair"):
                score *= 1.2
            # Stat booster = universal benefit
            if officer.get("_stat_booster"):
                score *= 1.3
            # Ability amplifier (like Pike) is great for PvE
            if officer.get("_ability_amplifier"):
                score *= 1.5
            # Loot bonus while grinding is nice
            if officer.get("_loot"):
                score *= 1.15
            # PvP-only officers are wasted
            if officer.get("_is_pvp_specific") and not officer.get("_pve_hostile"):
                score *= 0.3
            # Mining officers are wrong scenario
            if officer.get("_mining"):
                score *= 0.2

        elif scenario == "mission_boss":
            # Mission-specific officers are ideal (very rare — Tasha Yar, Data)
            if officer.get("_mission_boss"):
                score *= 2.0
            # Non-player / hostile officers work well (bosses are hostiles)
            if officer.get("_pve_hostile"):
                score *= 1.8
            # High damage output matters (boss has huge HP)
            score += atk * 0.5  # Extra weight on attack
            # Crit for damage spikes
            if officer.get("_crit_related"):
                score *= 1.5
            # Isolytic cascade for big damage
            if officer.get("_isolytic_related"):
                score *= 1.4
            # Shots = more damage
            if officer.get("_shots_related"):
                score *= 1.3
            # Mitigation for surviving boss hits
            if officer.get("_mitigation_related"):
                score *= 1.3
            # Shield for survivability
            if officer.get("_shield_related"):
                score *= 1.2
            # Ability amplifier
            if officer.get("_ability_amplifier"):
                score *= 1.4
            # Stat booster
            if officer.get("_stat_booster"):
                score *= 1.3
            # Armada-only officers don't work on missions
            if officer.get("_armada") and not officer.get("_pve_hostile"):
                score *= 0.4
            # Mining/cargo = dead weight
            if officer.get("_mining") or officer.get("_cargo"):
                score *= 0.1

        elif scenario == "loot":
            # Mining speed is the #1 priority for mining
            if officer.get("_mining"):
                score *= 3.0
            # Cargo capacity/protection
            if officer.get("_cargo"):
                score *= 2.5
            # Loot boost
            if officer.get("_loot"):
                score *= 2.5
            # Warp for getting to/from mining locations
            if officer.get("_warp"):
                score *= 1.5
            # Defense matters (you get jumped while mining)
            score += defense * 0.5
            score += health * 0.3
            # Shield for surviving attacks at mining nodes
            if officer.get("_shield_related"):
                score *= 1.2
            # Mitigation for node defense
            if officer.get("_mitigation_related"):
                score *= 1.2
            # PvP survivability is a bonus (you get attacked while mining)
            if officer.get("_is_pvp_specific"):
                score *= 1.1
            # Pure combat officers without resource bonuses are less useful
            if (
                not officer.get("_mining")
                and not officer.get("_cargo")
                and not officer.get("_loot")
                and not officer.get("_warp")
            ):
                # Still useful for surviving attacks, but not primary
                score *= 0.4

        elif scenario == "armada":
            # Armada-specific officers are ideal
            if officer.get("_armada"):
                score *= 2.5
            # Solo armada specific
            if officer.get("_armada_solo"):
                score *= 1.3
            # Non-armada-only officers are penalized (they explicitly don't work)
            if officer.get("_non_armada_only"):
                score *= 0.2
            # Hostile officers that also work on armadas
            if officer.get("_pve_hostile") and not officer.get("_non_armada_only"):
                score *= 1.3
            # High damage output for boss DPS
            score += atk * 0.4
            # Crit for damage spikes
            if officer.get("_crit_related"):
                score *= 1.4
            # Isolytic cascade (big damage mechanic)
            if officer.get("_isolytic_related"):
                score *= 1.4
            # Shots = more damage
            if officer.get("_shots_related"):
                score *= 1.3
            # Mitigation for surviving armada hits
            if officer.get("_mitigation_related"):
                score *= 1.3
            # Ability amplifier
            if officer.get("_ability_amplifier"):
                score *= 1.3
            # Stat booster
            if officer.get("_stat_booster"):
                score *= 1.2
            # PvP-only officers are mostly useless
            if officer.get("_is_pvp_specific") and not officer.get("_armada"):
                score *= 0.3
            # Mining/cargo = dead weight
            if officer.get("_mining") or officer.get("_cargo"):
                score *= 0.1

        # ------ Mining sub-scenarios (for dock loadout) ------
        elif scenario == "mining_speed":
            # Pure mining speed — get resources as fast as possible
            if officer.get("_mining_speed"):
                score *= 4.0
            elif officer.get("_mining"):
                score *= 2.5
            if officer.get("_cargo"):
                score *= 1.5
            if officer.get("_warp"):
                score *= 1.2
            # Node defense is a nice bonus
            if officer.get("_node_defense"):
                score *= 1.3
            # Non-mining officers are nearly useless
            if (
                not officer.get("_mining")
                and not officer.get("_mining_speed")
                and not officer.get("_cargo")
            ):
                score *= 0.2

        elif scenario == "mining_protected":
            # Maximize protected cargo — keep what you mine
            if officer.get("_protected_cargo"):
                score *= 4.0
            elif officer.get("_cargo"):
                score *= 2.5
            if officer.get("_mining"):
                score *= 1.5
            # Node defense helps you keep cargo
            if officer.get("_node_defense"):
                score *= 1.5
            # Shield/mitigation helps survive raids
            if officer.get("_shield_related"):
                score *= 1.3
            if officer.get("_mitigation_related"):
                score *= 1.3
            # Non-mining/cargo officers are nearly useless
            if (
                not officer.get("_protected_cargo")
                and not officer.get("_cargo")
                and not officer.get("_mining")
                and not officer.get("_node_defense")
            ):
                score *= 0.2

        elif scenario == "mining_crystal":
            # Crystal-specific mining
            if officer.get("_mining_crystal"):
                score *= 4.0
            elif officer.get("_mining"):
                score *= 2.0
            if officer.get("_mining_speed"):
                score *= 1.5
            if officer.get("_cargo"):
                score *= 1.3
            if officer.get("_protected_cargo"):
                score *= 1.3
            if officer.get("_node_defense"):
                score *= 1.2
            if (
                not officer.get("_mining")
                and not officer.get("_mining_crystal")
                and not officer.get("_cargo")
            ):
                score *= 0.2

        elif scenario == "mining_gas":
            # Gas-specific mining
            if officer.get("_mining_gas"):
                score *= 4.0
            elif officer.get("_mining"):
                score *= 2.0
            if officer.get("_mining_speed"):
                score *= 1.5
            if officer.get("_cargo"):
                score *= 1.3
            if officer.get("_protected_cargo"):
                score *= 1.3
            if officer.get("_node_defense"):
                score *= 1.2
            if (
                not officer.get("_mining")
                and not officer.get("_mining_gas")
                and not officer.get("_cargo")
            ):
                score *= 0.2

        elif scenario == "mining_ore":
            # Ore-specific mining
            if officer.get("_mining_ore"):
                score *= 4.0
            elif officer.get("_mining"):
                score *= 2.0
            if officer.get("_mining_speed"):
                score *= 1.5
            if officer.get("_cargo"):
                score *= 1.3
            if officer.get("_protected_cargo"):
                score *= 1.3
            if officer.get("_node_defense"):
                score *= 1.2
            if (
                not officer.get("_mining")
                and not officer.get("_mining_ore")
                and not officer.get("_cargo")
            ):
                score *= 0.2

        elif scenario == "mining_general":
            # General-purpose mining — balanced across all mining categories
            if officer.get("_mining"):
                score *= 3.0
            if officer.get("_mining_speed"):
                score *= 1.5
            if officer.get("_cargo"):
                score *= 2.0
            if officer.get("_protected_cargo"):
                score *= 1.5
            if officer.get("_node_defense"):
                score *= 1.3
            if officer.get("_warp"):
                score *= 1.2
            # Generalist mining bonus for having multiple subcategory tags
            mining_tags = sum(
                1
                for tag in [
                    "_mining_crystal",
                    "_mining_gas",
                    "_mining_ore",
                    "_mining_speed",
                    "_protected_cargo",
                ]
                if officer.get(tag)
            )
            if mining_tags >= 2:
                score *= 1.0 + mining_tags * 0.2
            # Non-mining officers are nearly useless
            if (
                not officer.get("_mining")
                and not officer.get("_cargo")
                and not officer.get("_node_defense")
            ):
                score *= 0.2

        # Universal bonuses
        # Ship-type match bonus
        if officer.get("_is_ship_specific"):
            score *= 1.3

        # Use flag from spreadsheet
        if officer.get("use") == "Y":
            score *= 1.1

        return score

    def score_scenario_crew(
        self, captain: Dict, bridge1: Dict, bridge2: Dict, scenario: str
    ) -> Tuple[float, Dict]:
        """Score a 3-officer crew for a given scenario.

        For pvp and hybrid, dispatches to the existing methods.
        For other scenarios, uses generalized crew scoring.
        """
        if scenario == "pvp":
            return self.score_crew(captain, bridge1, bridge2)
        elif scenario == "hybrid":
            return self.score_hybrid_crew(captain, bridge1, bridge2)

        ship_label = self.ship_type.capitalize()
        crew = [captain, bridge1, bridge2]
        breakdown = {
            "captain": captain["name"],
            "bridge": [bridge1["name"], bridge2["name"]],
            "individual_scores": {},
            "synergy_bonus": 0,
            "state_chain_bonus": 0,
            "crit_bonus": 0,
            "ship_type_bonus": 0,
            "scenario_bonus": 0,
            "weakness_counter_bonus": 0,
            "dual_use_bonus": 0,
            "amplifier_bonus": 0,
            "penalties": [],
            "synergy_notes": [],
        }

        # --- Individual scores ---
        total = 0.0
        for off in crew:
            ind_score = self.score_scenario_individual(off, scenario)
            breakdown["individual_scores"][off["name"]] = ind_score
            total += ind_score

        # --- Captain Maneuver value ---
        if self._cm_is_bda(captain):
            penalty = total * 0.25
            total -= penalty
            breakdown["penalties"].append(
                f"'{captain['name']}' has a BDA, not a Captain Maneuver — "
                f"wasted captain slot (-{penalty:,.0f})"
            )
        elif not self._cm_works_on_ship(captain):
            penalty = total * 0.2
            total -= penalty
            breakdown["penalties"].append(
                f"'{captain['name']}' CM locked to non-{ship_label} ship type (-{penalty:,.0f})"
            )
        else:
            cm_bonus = min(captain["cm_pct"], 500) * 50
            total += cm_bonus

        # --- Ability amplifier as captain ---
        if captain.get("_ability_amplifier"):
            amp_bonus = 50000
            total += amp_bonus
            breakdown["amplifier_bonus"] = amp_bonus
            breakdown["synergy_notes"].append(
                f"Ability amplifier captain '{captain['name']}' boosts all bridge abilities (+{amp_bonus:,})"
            )

        # --- State synergy chain ---
        all_applied = set()
        all_benefit = set()
        for off in crew:
            all_applied |= off.get("_states_applied", set())
            all_benefit |= off.get("_states_benefit", set())

        synergy_states = all_applied & all_benefit
        if synergy_states:
            chain_bonus = len(synergy_states) * 40000
            beneficiaries = 0
            for off in crew:
                if off.get("_states_benefit", set()) & all_applied:
                    beneficiaries += 1
            chain_bonus *= 1 + beneficiaries * 0.5
            total += chain_bonus
            breakdown["state_chain_bonus"] = chain_bonus
            breakdown["synergy_notes"].append(
                f"State synergy chain: {', '.join(synergy_states)} "
                f"({beneficiaries} officers benefit)"
            )

        # --- Shared state coherence ---
        state_counts = {}
        for off in crew:
            for state in off.get("_pvp_tags", set()) & {
                "morale",
                "breach",
                "burning",
                "assimilate",
            }:
                state_counts[state] = state_counts.get(state, 0) + 1
        if state_counts:
            best_state_count = max(state_counts.values())
            best_state = [s for s, c in state_counts.items() if c == best_state_count][
                0
            ]
            if best_state_count >= 3:
                coherence_bonus = 35000
                total += coherence_bonus
                breakdown["synergy_bonus"] += coherence_bonus
                breakdown["synergy_notes"].append(
                    f"Full crew coherence on '{best_state}' (all 3 officers)"
                )
            elif best_state_count == 2:
                coherence_bonus = 12000
                total += coherence_bonus
                breakdown["synergy_bonus"] += coherence_bonus
                breakdown["synergy_notes"].append(
                    f"Partial crew coherence on '{best_state}' (2/3 officers)"
                )

        # --- Crit coverage ---
        crit_officers = sum(1 for off in crew if off.get("_crit_related"))
        if crit_officers >= 2:
            crit_bonus = 25000
            total += crit_bonus
            breakdown["crit_bonus"] = crit_bonus
            breakdown["synergy_notes"].append(
                f"Strong crit coverage ({crit_officers}/3 officers)"
            )

        # --- Ship-type coverage ---
        ship_officers = sum(1 for off in crew if off.get("_is_ship_specific"))
        if ship_officers >= 2:
            ship_bonus = 20000
            total += ship_bonus
            breakdown["ship_type_bonus"] = ship_bonus
            breakdown["synergy_notes"].append(
                f"{ship_label}-specialized ({ship_officers}/3 officers)"
            )

        # --- Scenario-specific crew bonuses ---
        scenario_bonus = 0

        if scenario == "base_cracker":
            # Reward crews where multiple officers are station attackers
            base_attackers = sum(1 for off in crew if off.get("_base_attack"))
            if base_attackers >= 2:
                bonus = base_attackers * 30000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Station attack crew ({base_attackers}/3 officers specialize in base cracking)"
                )
            # Weapon delay synergy
            delay_officers = sum(1 for off in crew if off.get("_weapon_delay"))
            if delay_officers >= 1:
                bonus = delay_officers * 15000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Weapon delay ({delay_officers} officer(s) delay enemy weapons)"
                )

        elif scenario == "pve_hostile":
            # Reward full PvE crews
            pve_count = sum(1 for off in crew if off.get("_pve_hostile"))
            if pve_count >= 2:
                bonus = pve_count * 20000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"PvE-focused crew ({pve_count}/3 officers have hostile abilities)"
                )
            # Repair synergy
            repair_count = sum(1 for off in crew if off.get("_repair"))
            if repair_count >= 1:
                bonus = repair_count * 10000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Repair capability ({repair_count} officer(s))"
                )

        elif scenario == "mission_boss":
            # Mission-specific officers are rare and valuable
            mission_count = sum(1 for off in crew if off.get("_mission_boss"))
            if mission_count >= 1:
                bonus = mission_count * 25000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Mission specialist ({mission_count} officer(s) with mission abilities)"
                )
            # High combined attack stat bonus (bosses need burst damage)
            total_atk = sum(off["attack"] for off in crew)
            if total_atk > 150000:
                bonus = int((total_atk - 150000) * 0.1)
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"High combined ATK ({total_atk:,.0f}) for boss damage"
                )
            # Isolytic synergy
            iso_count = sum(1 for off in crew if off.get("_isolytic_related"))
            if iso_count >= 2:
                bonus = iso_count * 15000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Isolytic synergy ({iso_count} officers)"
                )

        elif scenario == "loot":
            # Mining crew bonus
            mining_count = sum(1 for off in crew if off.get("_mining"))
            if mining_count >= 2:
                bonus = mining_count * 30000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Mining crew ({mining_count}/3 officers boost mining)"
                )
            # Cargo bonus
            cargo_count = sum(1 for off in crew if off.get("_cargo"))
            if cargo_count >= 1:
                bonus = cargo_count * 20000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Cargo management ({cargo_count} officer(s))"
                )
            # Loot bonuses
            loot_count = sum(1 for off in crew if off.get("_loot"))
            if loot_count >= 1:
                bonus = loot_count * 20000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Loot bonus ({loot_count} officer(s) increase loot)"
                )
            # Survivability: defense is important (you get attacked while mining)
            total_def = sum(off["defense"] for off in crew)
            total_hp = sum(off["health"] for off in crew)
            surv_bonus = int(total_def * 0.1 + total_hp * 0.05)
            scenario_bonus += surv_bonus

        elif scenario == "armada":
            # Armada specialists
            armada_count = sum(1 for off in crew if off.get("_armada"))
            if armada_count >= 2:
                bonus = armada_count * 25000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Armada crew ({armada_count}/3 officers have armada abilities)"
                )
            # Isolytic synergy for armada damage
            iso_count = sum(1 for off in crew if off.get("_isolytic_related"))
            if iso_count >= 2:
                bonus = iso_count * 15000
                scenario_bonus += bonus
                breakdown["synergy_notes"].append(
                    f"Isolytic synergy ({iso_count} officers)"
                )

        total += scenario_bonus
        breakdown["scenario_bonus"] = scenario_bonus

        # --- Weakness counter bonuses (from battle log — mainly useful for PvP scenarios) ---
        weakness_bonus = 0
        if self.weakness and scenario in ("pvp", "base_cracker"):
            if self.weakness.get("crit_damage_gap", 0) > 3:
                crit_reducers = 0
                for off in crew:
                    d = off.get("display", "")
                    if (
                        ("decrease" in d and "critical" in d)
                        or ("reduce" in d and "critical" in d)
                        or ("opponent" in d and "critical" in d)
                    ):
                        crit_reducers += 1
                if crit_reducers > 0:
                    bonus = (
                        crit_reducers * 15000 * (self.weakness["crit_damage_gap"] / 10)
                    )
                    weakness_bonus += bonus

            if self.weakness.get("shield_timing_loss", 0) > 3:
                shield_helpers = sum(1 for off in crew if off.get("_shield_related"))
                if shield_helpers > 0:
                    bonus = (
                        shield_helpers
                        * 8000
                        * (self.weakness["shield_timing_loss"] / 10)
                    )
                    weakness_bonus += bonus

        total += weakness_bonus
        breakdown["weakness_counter_bonus"] = weakness_bonus

        # --- Penalties ---

        # Wrong-scenario officers
        if scenario == "base_cracker":
            for off in crew:
                if off.get("_mining") or off.get("_cargo"):
                    penalty_amt = total * 0.15
                    total -= penalty_amt
                    breakdown["penalties"].append(
                        f"'{off['name']}' is a mining/cargo officer — useless in station combat (-{penalty_amt:,.0f})"
                    )
                if off.get("_base_defend") and not off.get("_base_attack"):
                    penalty_amt = total * 0.2
                    total -= penalty_amt
                    breakdown["penalties"].append(
                        f"'{off['name']}' is a station DEFENDER — wrong role for attacking (-{penalty_amt:,.0f})"
                    )

        elif scenario == "armada":
            for off in crew:
                if off.get("_non_armada_only"):
                    penalty_amt = total * 0.3
                    total -= penalty_amt
                    breakdown["penalties"].append(
                        f"'{off['name']}' explicitly does NOT work in armadas (-{penalty_amt:,.0f})"
                    )
                if off.get("_mining") or off.get("_cargo"):
                    penalty_amt = total * 0.15
                    total -= penalty_amt
                    breakdown["penalties"].append(
                        f"'{off['name']}' is a mining/cargo officer — useless in armada (-{penalty_amt:,.0f})"
                    )

        elif scenario == "loot":
            for off in crew:
                if (
                    off.get("_is_pvp_specific")
                    and not off.get("_mining")
                    and not off.get("_cargo")
                    and not off.get("_loot")
                ):
                    penalty_amt = total * 0.1
                    total -= penalty_amt
                    breakdown["penalties"].append(
                        f"'{off['name']}' is PvP-only — limited value while mining (-{penalty_amt:,.0f})"
                    )

        # Bridge officer OA locked to wrong ship type
        for off in [bridge1, bridge2]:
            if not self._oa_works_on_ship(off):
                penalty_amt = total * 0.15
                total -= penalty_amt
                breakdown["penalties"].append(
                    f"'{off['name']}' OA locked to non-{ship_label} ship type (-{penalty_amt:,.0f})"
                )

        return total, breakdown

    def find_best_scenario_crews(
        self,
        scenario: str,
        ship_type: str = "explorer",
        top_n: int = 5,
        excluded_officers: Optional[Set[str]] = None,
    ) -> List[Tuple[float, Dict]]:
        """Evaluate all valid 3-officer combinations for a given scenario.

        For pvp/hybrid, delegates to existing methods.
        For other scenarios, uses the generalized scorer.

        Args:
            excluded_officers: Set of officer names to exclude (for multi-dock loadouts).
        """
        if scenario == "pvp":
            return self.find_best_crews(
                ship_type=ship_type, top_n=top_n, excluded_officers=excluded_officers
            )
        elif scenario == "hybrid":
            return self.find_best_hybrid_crews(
                ship_type=ship_type, top_n=top_n, excluded_officers=excluded_officers
            )

        exclude = excluded_officers or set()
        # Pre-filter: top 40 individuals for this scenario
        scored_individuals = []
        for off in self.officers:
            if off["name"] in exclude:
                continue
            s = self.score_scenario_individual(off, scenario)
            scored_individuals.append((s, off))
        scored_individuals.sort(key=lambda x: x[0], reverse=True)

        candidates = [off for _, off in scored_individuals[:40]]

        print(
            f"  Evaluating {len(candidates)} candidate officers in all 3-officer combinations..."
        )
        print(
            f"  Total combinations to evaluate: "
            f"{len(candidates) * (len(candidates) - 1) * (len(candidates) - 2) // 6:,}"
        )

        results = []
        for combo in combinations(candidates, 3):
            for captain_idx in range(3):
                captain = combo[captain_idx]
                bridge = [combo[j] for j in range(3) if j != captain_idx]
                score, breakdown = self.score_scenario_crew(
                    captain, bridge[0], bridge[1], scenario
                )
                results.append((score, breakdown))

        results.sort(key=lambda x: x[0], reverse=True)

        # Deduplicate
        seen = set()
        unique_results = []
        for score, bd in results:
            key = frozenset([bd["captain"]] + bd["bridge"])
            if key not in seen:
                seen.add(key)
                unique_results.append((score, bd))
            if len(unique_results) >= top_n:
                break

        return unique_results

    def recommend_ship(self, scenario: str) -> Dict:
        """Recommend the best ship for a given scenario.

        Returns a dict with:
          best: recommended ship type
          reason: why this ship is recommended
          comparison: scores for all viable ships (if we run the evaluator on each)
        """
        rec = self.SHIP_RECOMMENDATIONS.get(
            scenario,
            {
                "best": "explorer",
                "reason": "Default recommendation",
                "viable": ["explorer", "battleship", "interceptor"],
            },
        )

        return {
            "scenario": scenario,
            "scenario_label": self.SCENARIO_LABELS.get(scenario, scenario),
            "recommended_ship": rec["best"],
            "reason": rec["reason"],
            "viable_ships": rec["viable"],
        }

    def compare_ships(self, scenario: str, top_n: int = 3) -> Dict:
        """Compare crew optimization across all ship types for a scenario.

        Returns the best crew for each ship type so the user can see the
        trade-offs.
        """
        results = {}
        original_ship = self.ship_type

        for ship in ["explorer", "battleship", "interceptor"]:
            self.ship_type = ship
            self._classify_officers()  # Re-tag for new ship type
            crews = self.find_best_scenario_crews(scenario, ship_type=ship, top_n=1)
            if crews:
                score, bd = crews[0]
                results[ship] = {
                    "score": round(score),
                    "captain": bd["captain"],
                    "bridge": bd["bridge"],
                    "synergy_notes": bd.get("synergy_notes", []),
                }
            else:
                results[ship] = {"score": 0, "captain": "N/A", "bridge": []}

        # Restore original ship type
        self.ship_type = original_ship
        self._classify_officers()

        # Determine best
        best_ship = max(results, key=lambda s: results[s]["score"])

        rec = self.SHIP_RECOMMENDATIONS.get(scenario, {})
        return {
            "scenario": scenario,
            "scenario_label": self.SCENARIO_LABELS.get(scenario, scenario),
            "recommended_ship": rec.get("best", "explorer"),
            "recommended_reason": rec.get("reason", ""),
            "best_scoring_ship": best_ship,
            "ships": results,
        }

    # ------------------------------------------------------------------
    # 7-Dock Loadout Optimizer
    # ------------------------------------------------------------------

    def optimize_dock_loadout(
        self,
        dock_configs: List[Dict],
        top_n: int = 1,
    ) -> Dict:
        """Optimize crews for multiple docks with no officer duplication.

        Each dock config is a dict:
            {"scenario": str, "ship_override": str or None, "locked": bool,
             "locked_crew": {"captain": str, "bridge": [str, str]} or None}

        Processing order matters: earlier docks get priority on the best officers.
        Locked docks use their pre-assigned crew and reserve those officers.

        Returns:
            {
                "docks": [
                    {
                        "dock_num": int (1-based),
                        "scenario": str,
                        "scenario_label": str,
                        "ship_recommended": str,
                        "ship_used": str,
                        "locked": bool,
                        "crew": {"captain": str, "bridge": [str, str], "score": float, ...},
                        "bda_suggestions": [...],
                    },
                    ...
                ],
                "excluded_officers": [str],  # all assigned officers
                "total_officers_used": int,
            }
        """
        excluded = set()
        dock_results = []

        for i, dock in enumerate(dock_configs):
            dock_num = i + 1
            scenario = dock.get("scenario", "pvp")
            ship_override = dock.get("ship_override")
            locked = dock.get("locked", False)
            locked_crew = dock.get("locked_crew")

            # Determine ship type
            rec = self.SHIP_RECOMMENDATIONS.get(scenario, {})
            ship_recommended = rec.get("best", "explorer")
            ship_used = ship_override if ship_override else ship_recommended

            label = self.SCENARIO_LABELS.get(scenario, scenario)

            dock_result = {
                "dock_num": dock_num,
                "scenario": scenario,
                "scenario_label": label,
                "ship_recommended": ship_recommended,
                "ship_used": ship_used,
                "locked": locked,
                "crew": None,
                "bda_suggestions": [],
            }

            if locked and locked_crew:
                # Locked dock — use the pre-assigned crew, just reserve the officers
                captain_name = locked_crew.get("captain", "")
                bridge_names = locked_crew.get("bridge", [])
                all_names = {captain_name} | set(bridge_names)

                # Score the locked crew for display
                crew_objs = [o for o in self.officers if o["name"] in all_names]
                if len(crew_objs) == 3:
                    captain_obj = next(
                        (o for o in crew_objs if o["name"] == captain_name), None
                    )
                    bridge_objs = [o for o in crew_objs if o["name"] != captain_name]
                    if captain_obj and len(bridge_objs) == 2:
                        score, bd = self.score_scenario_crew(
                            captain_obj, bridge_objs[0], bridge_objs[1], scenario
                        )
                        dock_result["crew"] = {
                            "captain": captain_name,
                            "bridge": bridge_names,
                            "score": round(score),
                            "breakdown": bd,
                        }
                else:
                    # Could not find all officers — still lock them out
                    dock_result["crew"] = {
                        "captain": captain_name,
                        "bridge": bridge_names,
                        "score": 0,
                        "breakdown": {},
                    }
                excluded |= all_names
            else:
                # Optimize this dock — switch ship type if needed
                original_ship = self.ship_type
                if ship_used != self.ship_type:
                    self.ship_type = ship_used
                    self._classify_officers()

                crews = self.find_best_scenario_crews(
                    scenario,
                    ship_type=ship_used,
                    top_n=top_n,
                    excluded_officers=excluded,
                )

                if crews:
                    best_score, best_bd = crews[0]
                    captain_name = best_bd["captain"]
                    bridge_names = best_bd["bridge"]
                    dock_result["crew"] = {
                        "captain": captain_name,
                        "bridge": bridge_names,
                        "score": round(best_score),
                        "breakdown": best_bd,
                    }
                    excluded |= {captain_name} | set(bridge_names)

                    # BDA suggestions (exclude all assigned officers)
                    bda = self.find_best_bda(
                        captain_name,
                        bridge_names,
                        mode=scenario,
                        top_n=3,
                        excluded_officers=excluded,
                    )
                    dock_result["bda_suggestions"] = bda
                else:
                    dock_result["crew"] = {
                        "captain": "N/A",
                        "bridge": [],
                        "score": 0,
                        "breakdown": {},
                    }

                # Restore ship type
                if self.ship_type != original_ship:
                    self.ship_type = original_ship
                    self._classify_officers()

            dock_results.append(dock_result)

        return {
            "docks": dock_results,
            "excluded_officers": sorted(excluded),
            "total_officers_used": len(excluded),
        }

    # ------------------------------------------------------------------
    # Below Deck Ability (BDA) suggestions
    # ------------------------------------------------------------------

    def find_best_bda(
        self,
        captain_name: str,
        bridge_names: List[str],
        mode: str = "pvp",
        top_n: int = 5,
        excluded_officers: Optional[Set[str]] = None,
    ) -> List[Dict]:
        """Find the best Below Deck officers to complement a given 3-officer crew.

        BDA officers sit below deck — their passive ability fires but they
        don't occupy a bridge slot.  Good BDA picks:
          - Complement the crew's state chain (apply a state the crew benefits from,
            or benefit from a state the crew already applies)
          - Cover weaknesses the crew doesn't address (crit, shield, mitigation)
          - Have high OA% (that's what fires from below deck)
          - Are NOT already on the bridge
          - Are NOT in the excluded set (for multi-dock loadouts)
        """
        crew_names = {captain_name} | set(bridge_names)
        exclude = (excluded_officers or set()) | crew_names
        crew_objs = [o for o in self.officers if o["name"] in crew_names]

        # Collect crew's state profile
        crew_applied = set()
        crew_benefit = set()
        crew_tags = set()
        for off in crew_objs:
            crew_applied |= off.get("_states_applied", set())
            crew_benefit |= off.get("_states_benefit", set())
            crew_tags |= off.get("_pvp_tags", set())

        has_crit = any(o.get("_crit_related") for o in crew_objs)
        has_shield = any(o.get("_shield_related") for o in crew_objs)
        has_mitigation = any(o.get("_mitigation_related") for o in crew_objs)

        candidates = []
        for off in self.officers:
            if off["name"] in exclude:
                continue
            # BDA officers fire their OA, so OA% matters
            score = 0.0
            reasons = []

            # Base: OA percentage value
            oa = off.get("oa_pct", 0)
            if oa >= 10000:
                # This IS a BDA officer (has a BDA instead of CM) — they're designed for below deck
                score += 5000
                reasons.append("Designed as BDA officer")
            score += min(oa, 500) * 10  # Cap normal OA contribution

            # State synergy: BDA applies a state the crew benefits from
            bda_applied = off.get("_states_applied", set())
            bda_benefit = off.get("_states_benefit", set())
            chain_add = bda_applied & crew_benefit
            if chain_add:
                score += len(chain_add) * 20000
                reasons.append(f"Applies {', '.join(chain_add)} (crew benefits)")

            # State synergy: BDA benefits from a state the crew applies
            chain_receive = bda_benefit & crew_applied
            if chain_receive:
                score += len(chain_receive) * 15000
                reasons.append(
                    f"Benefits from {', '.join(chain_receive)} (crew applies)"
                )

            # Cover missing capabilities
            if not has_crit and off.get("_crit_related"):
                score += 10000
                reasons.append("Adds crit coverage (crew lacks it)")
            if not has_shield and off.get("_shield_related"):
                score += 8000
                reasons.append("Adds shield support (crew lacks it)")
            if not has_mitigation and off.get("_mitigation_related"):
                score += 8000
                reasons.append("Adds mitigation (crew lacks it)")

            # Stat contribution (BDA officers add partial stats)
            stat_score = (
                off["attack"] * 0.2 + off["defense"] * 0.15 + off["health"] * 0.1
            )
            score += stat_score

            # Ship-type match
            if off.get("_is_ship_specific"):
                score *= 1.2
                reasons.append(f"Has {self.ship_type} abilities")

            # OA must work on our ship
            if not self._oa_works_on_ship(off):
                score *= 0.3
                reasons.append(f"OA locked to wrong ship type")

            # Dual-use bonus in hybrid mode
            if mode == "hybrid" and off.get("_is_dual_use"):
                score *= 1.15
                reasons.append("Works in both PvE and PvP")

            if not reasons:
                reasons.append("General stat contribution")

            candidates.append(
                {
                    "name": off["name"],
                    "level": off["level"],
                    "rank": off["rank"],
                    "attack": off["attack"],
                    "defense": off["defense"],
                    "health": off["health"],
                    "oa_pct": off.get("oa_pct", 0),
                    "display": off.get("display", ""),
                    "score": round(score),
                    "reasons": reasons,
                }
            )

        candidates.sort(key=lambda x: x["score"], reverse=True)
        return candidates[:top_n]

    # ------------------------------------------------------------------
    # Improvement analysis (what-if scoring)
    # ------------------------------------------------------------------

    def get_improvement_analysis(
        self,
        captain_name: str,
        bridge_names: List[str],
        mode: str = "pvp",
    ) -> Dict:
        """Analyze what would improve a given crew's score.

        Returns actionable advice:
          - Which officer gains the most from leveling up
          - What stats are the bottleneck
          - Which roster gaps are holding the crew back
        """
        crew_names_list = [captain_name] + list(bridge_names)
        crew_objs = [
            next((o for o in self.officers if o["name"] == name), None)
            for name in crew_names_list
        ]
        crew_objs = [o for o in crew_objs if o is not None]

        if len(crew_objs) < 3:
            return {"error": "Could not find all crew members in roster"}

        # Current score
        captain = crew_objs[0]
        bridge = crew_objs[1:]
        if mode == "hybrid":
            current_score, current_bd = self.score_hybrid_crew(
                captain, bridge[0], bridge[1]
            )
        else:
            current_score, current_bd = self.score_crew(captain, bridge[0], bridge[1])

        improvements = []

        # What-if: each officer leveled to max (level 60)
        for off in crew_objs:
            if off["level"] >= 55:
                continue  # Already near max
            # Estimate stat growth: ~2% per level
            level_gap = 60 - off["level"]
            growth_factor = 1 + (level_gap * 0.02)
            original = {
                "attack": off["attack"],
                "defense": off["defense"],
                "health": off["health"],
                "level": off["level"],
            }

            # Temporarily boost stats
            off["attack"] *= growth_factor
            off["defense"] *= growth_factor
            off["health"] *= growth_factor
            off["level"] = 60

            # Re-score
            if mode == "hybrid":
                new_score, _ = self.score_hybrid_crew(captain, bridge[0], bridge[1])
            else:
                new_score, _ = self.score_crew(captain, bridge[0], bridge[1])

            delta = new_score - current_score
            if delta > 0:
                improvements.append(
                    {
                        "type": "level_up",
                        "officer": off["name"],
                        "current_level": original["level"],
                        "target_level": 60,
                        "score_increase": round(delta),
                        "percent_increase": round(
                            delta / max(current_score, 1) * 100, 1
                        ),
                        "advice": f"Level {off['name']} from {original['level']} to 60 "
                        f"(+{delta:,.0f} score, +{delta / max(current_score, 1) * 100:.1f}%)",
                    }
                )

            # Restore original stats
            off["attack"] = original["attack"]
            off["defense"] = original["defense"]
            off["health"] = original["health"]
            off["level"] = original["level"]

        improvements.sort(key=lambda x: x["score_increase"], reverse=True)

        # Identify missing coverage
        missing = []
        if not any(o.get("_crit_related") for o in crew_objs):
            missing.append("No crit-related officer — enemy crits are your #1 weakness")
        if not any(o.get("_shield_related") for o in crew_objs):
            missing.append("No shield support — shields fall early in combat")
        if not any(o.get("_states_applied") for o in crew_objs):
            missing.append("No state applicator — conditional abilities won't trigger")
        if not any(o.get("_mitigation_related") for o in crew_objs):
            missing.append("No mitigation officer — taking full damage every round")

        # Check for state chain completeness
        applied = set()
        benefit = set()
        for off in crew_objs:
            applied |= off.get("_states_applied", set())
            benefit |= off.get("_states_benefit", set())
        incomplete = benefit - applied
        if incomplete:
            missing.append(
                f"Officers benefit from {', '.join(incomplete)} but nobody applies it — "
                f"chain is broken"
            )

        # Alternative captain suggestion
        alt_captains = []
        for off in self.officers:
            if off["name"] in crew_names_list:
                continue
            if self._cm_is_bda(off):
                continue
            if not self._cm_works_on_ship(off):
                continue
            # Try this officer as captain with existing bridge
            if mode == "hybrid":
                alt_score, _ = self.score_hybrid_crew(off, bridge[0], bridge[1])
            else:
                alt_score, _ = self.score_crew(off, bridge[0], bridge[1])
            if alt_score > current_score * 1.05:  # >5% improvement
                alt_captains.append(
                    {
                        "name": off["name"],
                        "level": off["level"],
                        "score": round(alt_score),
                        "delta": round(alt_score - current_score),
                        "percent": round(
                            (alt_score - current_score) / max(current_score, 1) * 100, 1
                        ),
                    }
                )
        alt_captains.sort(key=lambda x: x["delta"], reverse=True)

        return {
            "current_score": round(current_score),
            "crew": crew_names_list,
            "level_up_gains": improvements,
            "missing_coverage": missing,
            "better_captains": alt_captains[:3],
        }

    # ------------------------------------------------------------------
    # Officer search/browser
    # ------------------------------------------------------------------

    def search_officers(
        self, query: str = "", state: str = "", ship_filter: str = "", top_n: int = 20
    ) -> List[Dict]:
        """Search and filter the officer roster.

        Args:
            query:  Text to search in officer name or display (abilities) text
            state:  Filter by state tag (morale, breach, burning, assimilate, crit, etc.)
            ship_filter:  Filter by ship-specific officers (explorer, battleship, interceptor)
            top_n:  Max results to return
        """
        results = []
        query_lower = query.lower().strip()
        state_lower = state.lower().strip()
        ship_lower = ship_filter.lower().strip()

        for off in self.officers:
            # Text search
            if query_lower:
                name_match = query_lower in off["name"].lower()
                display_match = query_lower in off.get("display", "").lower()
                if not name_match and not display_match:
                    continue

            # State filter
            if state_lower:
                tags = off.get("_pvp_tags", set())
                if state_lower not in tags:
                    # Also check applied/benefit
                    if state_lower not in off.get(
                        "_states_applied", set()
                    ) and state_lower not in off.get("_states_benefit", set()):
                        continue

            # Ship filter
            if ship_lower:
                d = off.get("display", "")
                if ship_lower not in d:
                    continue

            results.append(
                {
                    "name": off["name"],
                    "level": off["level"],
                    "rank": off["rank"],
                    "attack": off["attack"],
                    "defense": off["defense"],
                    "health": off["health"],
                    "cm_pct": off.get("cm_pct", 0),
                    "oa_pct": off.get("oa_pct", 0),
                    "display": off.get("display", ""),
                    "tags": sorted(off.get("_pvp_tags", set())),
                    "states_applied": sorted(off.get("_states_applied", set())),
                    "states_benefit": sorted(off.get("_states_benefit", set())),
                    "is_bda": self._cm_is_bda(off),
                    "is_ship_specific": off.get("_is_ship_specific", False),
                    "is_dual_use": off.get("_is_dual_use", False),
                    "is_pvp_specific": off.get("_is_pvp_specific", False),
                    "is_pve_specific": off.get("_is_pve_specific", False),
                }
            )

        # Sort by attack descending as default
        results.sort(key=lambda x: x["attack"], reverse=True)
        return results[:top_n]


# ---------------------------------------------------------------------------
# Original STFCCrewOptimizer (preserved)
# ---------------------------------------------------------------------------


class STFCCrewOptimizer:
    def __init__(self, csv_path: str):
        self.officers = []
        self.load_officers(csv_path)

    def load_officers(self, csv_path: str):
        """Load officers from CSV (data starts at row 20)"""
        with open(csv_path, "r", encoding="utf-8") as f:
            # Skip first 19 rows
            for _ in range(19):
                next(f)

            reader = csv.reader(f)
            for row in reader:
                if len(row) < 12:
                    continue

                try:
                    officer_name = row[2] if len(row) > 2 else ""
                    officer_name = str(officer_name).strip()

                    if not officer_name or officer_name == "":
                        continue

                    attack = (
                        float(str(row[5]).replace(",", "") or 0) if len(row) > 5 else 0
                    )
                    if attack <= 0:
                        continue

                    officer = {
                        "name": officer_name,
                        "level": int(float(str(row[3] or 0))) if len(row) > 3 else 0,
                        "rank": int(float(str(row[4] or 0))) if len(row) > 4 else 0,
                        "attack": attack,
                        "defense": float(str(row[6]).replace(",", "") or 0)
                        if len(row) > 6
                        else 0,
                        "health": float(str(row[7]).replace(",", "") or 0)
                        if len(row) > 7
                        else 0,
                        "cm_pct": self.parse_percent(row[9] if len(row) > 9 else "0%"),
                        "oa_pct": self.parse_percent(
                            row[10] if len(row) > 10 else "0%"
                        ),
                        "effect": str(row[11] if len(row) > 11 else "").lower(),
                        "cause": str(row[12] if len(row) > 12 else "").lower(),
                        "use": str(row[13] if len(row) > 13 else "").strip().upper(),
                        "display": str(row[14] if len(row) > 14 else "").lower(),
                    }
                    self.officers.append(officer)
                except (ValueError, TypeError, IndexError):
                    continue

    def parse_percent(self, value: str) -> float:
        """Parse percentage string to float"""
        if not value:
            return 0
        try:
            cleaned = str(value).replace("%", "").replace(",", "").strip()
            return float(cleaned) if cleaned else 0
        except:
            return 0

    def score_officer(self, officer: Dict, scenario: str) -> float:
        """Score an officer based on scenario"""
        attack = officer["attack"]
        defense = officer["defense"]
        health = officer["health"]
        cm_pct = officer["cm_pct"]
        oa_pct = officer["oa_pct"]
        effect = officer["effect"]
        use = officer["use"]
        display = officer["display"]

        score = 0

        if scenario == "pvp":
            score = attack * 1.5 + (oa_pct * 0.01)
            if any(x in effect for x in ["breach", "burning", "assimilate"]):
                score *= 1.3
            if use == "Y":
                score *= 1.2

        elif scenario == "pvp_interceptor":
            score = attack * 1.8 + (oa_pct * 0.015)
            if "breach" in effect or "interceptor" in display:
                score *= 1.5
            if use == "Y":
                score *= 1.3

        elif scenario == "pvp_explorer":
            score = attack * 1.3 + defense * 0.5 + (oa_pct * 0.01)
            if "explorer" in display or "morale" in effect:
                score *= 1.3
            if use == "Y":
                score *= 1.2

        elif scenario == "pvp_battleship":
            score = attack * 1.2 + defense * 0.8 + health * 0.3
            if "battleship" in display:
                score *= 1.3
            if use == "Y":
                score *= 1.2

        elif scenario == "pve":
            score = attack * 1.2 + defense * 0.3 + health * 0.2
            if any(x in display for x in ["loot", "mining", "resources"]):
                score *= 1.4

        elif scenario == "mission_boss":
            score = attack * 1.0 + defense * 1.0 + health * 0.5
            if any(x in display for x in ["penetration", "accuracy"]):
                score *= 1.2

        elif scenario == "armada":
            score = attack * 1.3 + health * 0.3
            if "armada" in display:
                score *= 1.5
            if "critical" in display:
                score *= 1.2

        elif scenario == "rep_grinding":
            score = attack * 0.5 + defense * 0.3 + health * 0.2
            if any(x in display for x in ["reputation", "faction"]):
                score *= 2.0
            if any(x in display for x in ["speed", "warp"]):
                score *= 1.3

        elif scenario == "loot":
            score = attack * 0.5 + defense * 0.2
            if any(
                x in display
                for x in ["loot", "resources", "mining", "cargo", "protected"]
            ):
                score *= 2.5

        return score

    def get_best_crew(self, scenario: str, count: int = 3) -> List[Dict]:
        """Get the best officers for a scenario"""
        scored = []
        for officer in self.officers:
            score = self.score_officer(officer, scenario)
            scored.append({**officer, "score": score})

        scored.sort(key=lambda x: x["score"], reverse=True)
        return scored[:count]

    def generate_all_recommendations(self) -> Dict:
        """Generate recommendations for all scenarios"""
        scenarios = [
            ("PvP (General)", "pvp"),
            ("PvP (Interceptor)", "pvp_interceptor"),
            ("PvP (Explorer)", "pvp_explorer"),
            ("PvP (Battleship)", "pvp_battleship"),
            ("PvE (General)", "pve"),
            ("Mission Bosses", "mission_boss"),
            ("Armada", "armada"),
            ("Rep Grinding", "rep_grinding"),
            ("Loot Farming", "loot"),
        ]

        recommendations = {}
        for display_name, scenario_key in scenarios:
            recommendations[display_name] = self.get_best_crew(scenario_key, count=3)

        return recommendations

    def generate_html_report(self, output_path: str = "stfc_crew_recommendations.html"):
        """Generate an HTML report"""
        recommendations = self.generate_all_recommendations()

        html = f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>STFC Crew Optimizer Report</title>
    <style>
        body {{
            font-family: Arial, sans-serif;
            background: #1a1a1a;
            color: #fff;
            margin: 0;
            padding: 20px;
            line-height: 1.6;
        }}
        .container {{
            max-width: 1200px;
            margin: 0 auto;
        }}
        h1 {{
            text-align: center;
            color: #00d4ff;
            border-bottom: 3px solid #00d4ff;
            padding-bottom: 10px;
        }}
        .timestamp {{
            text-align: center;
            color: #888;
            margin-bottom: 30px;
            font-size: 12px;
        }}
        .scenario-section {{
            background: #2a2a2a;
            border-left: 5px solid #00d4ff;
            margin-bottom: 30px;
            padding: 20px;
            border-radius: 5px;
        }}
        .scenario-title {{
            font-size: 18px;
            font-weight: bold;
            color: #00d4ff;
            margin-bottom: 15px;
        }}
        .crew {{
            background: #3a3a3a;
            padding: 15px;
            margin-bottom: 15px;
            border-radius: 3px;
            border-left: 3px solid #ffa500;
        }}
        .crew-rank {{
            font-weight: bold;
            color: #ffa500;
            font-size: 16px;
        }}
        .officer-name {{
            font-size: 16px;
            font-weight: bold;
            color: #00ff00;
            margin: 5px 0;
        }}
        .officer-level {{
            font-size: 12px;
            color: #aaa;
        }}
        .stats {{
            display: grid;
            grid-template-columns: 1fr 1fr 1fr 1fr;
            gap: 10px;
            margin: 10px 0;
            font-size: 12px;
        }}
        .stat {{
            background: #2a2a2a;
            padding: 8px;
            border-radius: 3px;
        }}
        .stat-label {{
            color: #888;
            font-size: 10px;
            text-transform: uppercase;
        }}
        .stat-value {{
            color: #00ff00;
            font-weight: bold;
        }}
        .score {{
            color: #ffcc00;
            margin-top: 10px;
            font-weight: bold;
        }}
        .crew:nth-child(1) {{ border-left-color: #ffd700; }}
        .crew:nth-child(2) {{ border-left-color: #c0c0c0; }}
        .crew:nth-child(3) {{ border-left-color: #cd7f32; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>STFC Crew Optimizer</h1>
        <div class="timestamp">Report generated: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</div>
"""

        for scenario, crew in recommendations.items():
            html += f'<div class="scenario-section"><div class="scenario-title">{scenario}</div>'

            for i, officer in enumerate(crew, 1):
                medal = ["1st", "2nd", "3rd"][i - 1]
                html += f"""
                <div class="crew">
                    <div class="crew-rank">{medal} #{i}</div>
                    <div class="officer-name">{officer["name"]}</div>
                    <div class="officer-level">Level {officer["level"]} | Rank {officer["rank"]}</div>
                    <div class="stats">
                        <div class="stat">
                            <div class="stat-label">Attack</div>
                            <div class="stat-value">{officer["attack"]:,.0f}</div>
                        </div>
                        <div class="stat">
                            <div class="stat-label">Defense</div>
                            <div class="stat-value">{officer["defense"]:,.0f}</div>
                        </div>
                        <div class="stat">
                            <div class="stat-label">Health</div>
                            <div class="stat-value">{officer["health"]:,.0f}</div>
                        </div>
                        <div class="stat">
                            <div class="stat-label">OA%</div>
                            <div class="stat-value">{officer["oa_pct"]:.0f}%</div>
                        </div>
                    </div>
                    <div class="score">Score: {officer["score"]:.0f}</div>
                </div>
"""
            html += "</div>"

        html += """
    </div>
</body>
</html>
"""

        with open(output_path, "w", encoding="utf-8") as f:
            f.write(html)

        print(f"  HTML report saved to: {output_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def _print_crew_results(
    results: List[Tuple[float, Dict]],
    officers: List[Dict],
    ship_label: str,
    mode_label: str = "Score",
):
    """Print formatted crew recommendation results."""
    for rank, (score, bd) in enumerate(results, 1):
        medals = {1: ">>>", 2: " >>", 3: "  >", 4: "   ", 5: "   "}
        print(f"\n{medals.get(rank, '   ')} #{rank} — {mode_label}: {score:,.0f}")
        print(f"  {'=' * 80}")

        # Captain
        captain_name = bd["captain"]
        captain_obj = next((o for o in officers if o["name"] == captain_name), None)
        print(f"  CAPTAIN: {captain_name}")
        if captain_obj:
            print(
                f"    Level {captain_obj['level']} | Rank {captain_obj['rank']} | "
                f"ATK {captain_obj['attack']:,.0f} | DEF {captain_obj['defense']:,.0f} | "
                f"HP {captain_obj['health']:,.0f} | CM {captain_obj['cm_pct']:.0f}%"
            )
            if captain_obj.get("display"):
                print(f"    Abilities: {captain_obj['display'][:120]}")

        # Bridge officers
        for i, bname in enumerate(bd["bridge"], 1):
            bridge_obj = next((o for o in officers if o["name"] == bname), None)
            print(f"  BRIDGE {i}: {bname}")
            if bridge_obj:
                print(
                    f"    Level {bridge_obj['level']} | Rank {bridge_obj['rank']} | "
                    f"ATK {bridge_obj['attack']:,.0f} | DEF {bridge_obj['defense']:,.0f} | "
                    f"HP {bridge_obj['health']:,.0f} | OA {bridge_obj['oa_pct']:.0f}%"
                )
                if bridge_obj.get("display"):
                    print(f"    Abilities: {bridge_obj['display'][:120]}")

        # Synergy breakdown
        if bd["synergy_notes"]:
            print(f"  SYNERGY:")
            for note in bd["synergy_notes"]:
                print(f"    + {note}")

        # Penalties
        if bd["penalties"]:
            print(f"  WARNINGS:")
            for pen in bd["penalties"]:
                print(f"    - {pen}")

        # Score breakdown
        print(f"  SCORE BREAKDOWN:")
        for name, iscore in bd["individual_scores"].items():
            print(f"    {name:30s} individual: {iscore:>12,.0f}")
        if bd["state_chain_bonus"]:
            print(
                f"    {'State chain bonus':30s}          : {bd['state_chain_bonus']:>12,.0f}"
            )
        if bd["synergy_bonus"]:
            print(
                f"    {'Synergy coherence bonus':30s}          : {bd['synergy_bonus']:>12,.0f}"
            )
        if bd["crit_bonus"]:
            print(
                f"    {'Crit coverage bonus':30s}          : {bd['crit_bonus']:>12,.0f}"
            )
        if bd.get("ship_type_bonus"):
            print(
                f"    {f'{ship_label} specialization':30s}          : {bd['ship_type_bonus']:>12,.0f}"
            )
        if bd.get("dual_use_bonus"):
            print(
                f"    {'Dual-use crew bonus':30s}          : {bd['dual_use_bonus']:>12,.0f}"
            )
        if bd.get("amplifier_bonus"):
            print(
                f"    {'Ability amplifier captain':30s}          : {bd['amplifier_bonus']:>12,.0f}"
            )
        if bd["weakness_counter_bonus"]:
            print(
                f"    {'Battle-log weakness counter':30s}          : {bd['weakness_counter_bonus']:>12,.0f}"
            )
        if bd.get("scenario_bonus"):
            print(
                f"    {'Scenario specialization':30s}          : {bd['scenario_bonus']:>12,.0f}"
            )


def _serialize_crew_results(
    results: List[Tuple[float, Dict]], officers: List[Dict]
) -> List[Dict]:
    """Convert crew results to JSON-serializable dicts with full officer details."""
    serialized = []
    for rank, (score, bd) in enumerate(results, 1):
        captain_obj = next((o for o in officers if o["name"] == bd["captain"]), None)
        bridge_objs = [
            next((o for o in officers if o["name"] == bn), None) for bn in bd["bridge"]
        ]

        def _officer_info(obj):
            if not obj:
                return None
            return {
                "name": obj["name"],
                "level": obj["level"],
                "rank": obj["rank"],
                "attack": obj["attack"],
                "defense": obj["defense"],
                "health": obj["health"],
                "cm_pct": obj.get("cm_pct", 0),
                "oa_pct": obj.get("oa_pct", 0),
                "display": obj.get("display", ""),
            }

        crew_data = {
            "rank": rank,
            "score": round(score),
            "captain": _officer_info(captain_obj),
            "bridge": [_officer_info(b) for b in bridge_objs],
            "synergy_notes": bd.get("synergy_notes", []),
            "penalties": bd.get("penalties", []),
            "individual_scores": {
                k: round(v) for k, v in bd.get("individual_scores", {}).items()
            },
            "state_chain_bonus": round(bd.get("state_chain_bonus", 0)),
            "synergy_bonus": round(bd.get("synergy_bonus", 0)),
            "crit_bonus": round(bd.get("crit_bonus", 0)),
            "ship_type_bonus": round(bd.get("ship_type_bonus", 0)),
            "weakness_counter_bonus": round(bd.get("weakness_counter_bonus", 0)),
            "dual_use_bonus": round(bd.get("dual_use_bonus", 0)),
            "amplifier_bonus": round(bd.get("amplifier_bonus", 0)),
            "scenario_bonus": round(bd.get("scenario_bonus", 0)),
        }
        serialized.append(crew_data)
    return serialized


def _handle_json_mode(
    args,
    evaluator,
    optimizer,
    analyzer,
    weakness,
    ship_type,
    ship_label,
    ship_display,
    top_n,
):
    """Handle all --json sub-commands and return a JSON-serializable dict."""

    # Determine which scenarios to run based on --mode
    scenarios_to_run = _get_scenarios_from_mode(args.mode)

    if args.json == "crews":
        output = {
            "ship": ship_type,
            "ship_display": ship_display,
            "mode": args.mode,
            "top_n": top_n,
            "weakness": weakness,
            "scenarios": {},
        }
        for scenario in scenarios_to_run:
            label = PvPCrewEvaluator.SCENARIO_LABELS.get(scenario, scenario)
            crews = evaluator.find_best_scenario_crews(
                scenario, ship_type=ship_type, top_n=top_n
            )
            output["scenarios"][scenario] = {
                "label": label,
                "crews": _serialize_crew_results(crews, optimizer.officers),
            }
        # Backward compat: also set pvp_crews / hybrid_crews at top level
        if "pvp" in output["scenarios"]:
            output["pvp_crews"] = output["scenarios"]["pvp"]["crews"]
        if "hybrid" in output["scenarios"]:
            output["hybrid_crews"] = output["scenarios"]["hybrid"]["crews"]
        return output

    elif args.json == "bda":
        if not args.crew:
            return {
                "error": "Must provide --crew 'Captain|Bridge1|Bridge2' for BDA analysis"
            }
        parts = [p.strip() for p in args.crew.split("|")]
        if len(parts) != 3:
            return {"error": f"Expected 3 officers separated by |, got {len(parts)}"}
        captain_name, b1, b2 = parts
        mode = args.mode if args.mode not in ("both", "all") else "pvp"
        bda = evaluator.find_best_bda(captain_name, [b1, b2], mode=mode, top_n=top_n)
        return {
            "crew": parts,
            "mode": mode,
            "ship": ship_type,
            "bda_suggestions": bda,
        }

    elif args.json == "improve":
        if not args.crew:
            return {
                "error": "Must provide --crew 'Captain|Bridge1|Bridge2' for improvement analysis"
            }
        parts = [p.strip() for p in args.crew.split("|")]
        if len(parts) != 3:
            return {"error": f"Expected 3 officers separated by |, got {len(parts)}"}
        captain_name, b1, b2 = parts
        mode = args.mode if args.mode not in ("both", "all") else "pvp"
        analysis = evaluator.get_improvement_analysis(captain_name, [b1, b2], mode=mode)
        analysis["ship"] = ship_type
        analysis["mode"] = mode
        return analysis

    elif args.json == "search":
        results = evaluator.search_officers(
            query=args.query,
            state=args.state_filter,
            ship_filter="",
            top_n=top_n,
        )
        return {
            "query": args.query,
            "state_filter": args.state_filter,
            "ship": ship_type,
            "count": len(results),
            "officers": results,
        }

    elif args.json == "recommend":
        # Ship recommendation for each scenario
        output = {"scenarios": {}}
        for scenario in scenarios_to_run:
            rec = evaluator.recommend_ship(scenario)
            output["scenarios"][scenario] = rec
        return output

    elif args.json == "compare":
        # Compare all ships for each requested scenario
        output = {"scenarios": {}}
        for scenario in scenarios_to_run:
            comparison = evaluator.compare_ships(scenario, top_n=1)
            output["scenarios"][scenario] = comparison
        return output

    elif args.json == "full":
        output = {
            "ship": ship_type,
            "ship_display": ship_display,
            "mode": args.mode,
            "top_n": top_n,
            "weakness": weakness,
            "total_officers": len(optimizer.officers),
            "battles_analyzed": len(analyzer.battles),
            "scenarios": {},
        }
        for scenario in scenarios_to_run:
            label = PvPCrewEvaluator.SCENARIO_LABELS.get(scenario, scenario)
            crews = evaluator.find_best_scenario_crews(
                scenario, ship_type=ship_type, top_n=top_n
            )
            scenario_data = {
                "label": label,
                "crews": _serialize_crew_results(crews, optimizer.officers),
                "ship_recommendation": evaluator.recommend_ship(scenario),
            }

            # BDA + improvement for top crew
            if scenario_data["crews"]:
                top_crew = scenario_data["crews"][0]
                cn = top_crew["captain"]["name"]
                bn = [b["name"] for b in top_crew["bridge"]]
                bda = evaluator.find_best_bda(cn, bn, mode=scenario, top_n=3)
                top_crew["bda_suggestions"] = bda
                improve = evaluator.get_improvement_analysis(cn, bn, mode=scenario)
                top_crew["improvement_analysis"] = improve

            output["scenarios"][scenario] = scenario_data

        # Backward compat
        if "pvp" in output["scenarios"] and output["scenarios"]["pvp"]["crews"]:
            output["pvp_crews"] = output["scenarios"]["pvp"]["crews"]
        if "hybrid" in output["scenarios"] and output["scenarios"]["hybrid"]["crews"]:
            output["hybrid_crews"] = output["scenarios"]["hybrid"]["crews"]

        return output

    elif args.json == "loadout":
        if not args.docks:
            return {
                "error": "Must provide --docks with JSON dock configuration for loadout mode. "
                'Format: [{"scenario":"pvp"}, {"scenario":"mining_speed"}, ...]'
            }
        try:
            dock_configs = json.loads(args.docks)
        except json.JSONDecodeError as e:
            return {"error": f"Invalid JSON in --docks: {e}"}

        if not isinstance(dock_configs, list) or len(dock_configs) == 0:
            return {"error": "--docks must be a non-empty JSON array"}
        if len(dock_configs) > 7:
            return {"error": f"Maximum 7 docks supported, got {len(dock_configs)}"}

        # Validate scenarios
        valid_scenarios = set(PvPCrewEvaluator.ALL_DOCK_SCENARIOS)
        for i, dock in enumerate(dock_configs):
            scenario = dock.get("scenario", "")
            if scenario not in valid_scenarios:
                return {
                    "error": f"Dock {i + 1}: unknown scenario '{scenario}'. "
                    f"Valid: {', '.join(sorted(valid_scenarios))}"
                }

        result = evaluator.optimize_dock_loadout(dock_configs, top_n=1)

        # Serialize crew breakdowns for JSON output
        for dock in result["docks"]:
            crew = dock.get("crew")
            if crew and crew.get("breakdown"):
                bd = crew["breakdown"]
                crew["synergy_notes"] = bd.get("synergy_notes", [])
                crew["penalties"] = bd.get("penalties", [])
                crew["scenario_bonus"] = round(bd.get("scenario_bonus", 0))
                crew["individual_scores"] = {
                    k: round(v) for k, v in bd.get("individual_scores", {}).items()
                }
                del crew["breakdown"]

        result["ship"] = ship_type
        result["ship_display"] = ship_display
        return result

    return {"error": f"Unknown JSON mode: {args.json}"}


def _get_scenarios_from_mode(mode: str) -> List[str]:
    """Convert a --mode value into a list of scenarios to run."""
    if mode == "both":
        return ["pvp", "hybrid"]
    elif mode == "all":
        return PvPCrewEvaluator.ALL_SCENARIOS
    elif mode in PvPCrewEvaluator.ALL_SCENARIOS:
        return [mode]
    elif mode in PvPCrewEvaluator.MINING_SUB_SCENARIOS:
        return [mode]
    else:
        return ["pvp", "hybrid"]


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="STFC Crew Optimizer — Synergy-aware PvP & Hybrid crew evaluation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  %(prog)s                                  # Default: Explorer, both modes, top 5
  %(prog)s --ship battleship                # Negh'Var battleship
  %(prog)s --ship explorer --mode hybrid    # Explorer, hybrid only
  %(prog)s --ship interceptor --mode pvp --top 10
  %(prog)s --skip-battles --skip-legacy     # Minimal output
""",
    )
    parser.add_argument(
        "--ship",
        choices=["explorer", "battleship", "interceptor"],
        default="explorer",
        help="Ship type to optimize for (default: explorer)",
    )
    parser.add_argument(
        "--mode",
        choices=[
            "pvp",
            "hybrid",
            "both",
            "base_cracker",
            "pve_hostile",
            "mission_boss",
            "loot",
            "armada",
            "mining_speed",
            "mining_protected",
            "mining_crystal",
            "mining_gas",
            "mining_ore",
            "mining_general",
            "all",
        ],
        default="both",
        help="Optimization mode: pvp, hybrid, both, base_cracker, pve_hostile, "
        "mission_boss, loot, armada, mining_speed, mining_protected, "
        "mining_crystal, mining_gas, mining_ore, mining_general, or all (default: both)",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=5,
        help="Number of top crews to show (default: 5)",
    )
    parser.add_argument(
        "--skip-battles",
        action="store_true",
        help="Skip detailed battle log output",
    )
    parser.add_argument(
        "--skip-legacy",
        action="store_true",
        help="Skip legacy per-scenario recommendations",
    )
    parser.add_argument(
        "--json",
        choices=[
            "crews",
            "bda",
            "improve",
            "search",
            "full",
            "compare",
            "recommend",
            "loadout",
        ],
        default=None,
        help="Output structured JSON instead of text. Modes: "
        "crews=top crews, bda=BDA suggestions, improve=improvement analysis, "
        "search=officer search, full=all data, compare=compare ships, "
        "recommend=ship recommendation, loadout=7-dock loadout optimization",
    )
    parser.add_argument(
        "--crew",
        type=str,
        default=None,
        help="Crew to analyze (for --json bda/improve). Format: 'Captain|Bridge1|Bridge2'",
    )
    parser.add_argument(
        "--query",
        type=str,
        default="",
        help="Search query for --json search",
    )
    parser.add_argument(
        "--state-filter",
        type=str,
        default="",
        help="State filter for --json search (morale, breach, burning, assimilate, crit, etc.)",
    )
    parser.add_argument(
        "--docks",
        type=str,
        default=None,
        help="Dock loadout configuration as JSON string. "
        'Format: [{"scenario":"pvp","ship_override":null,"locked":false,"locked_crew":null}, ...]',
    )
    args = parser.parse_args()

    # In JSON mode, redirect all print() to stderr so only JSON goes to stdout
    _real_stdout = sys.stdout
    if args.json:
        sys.stdout = sys.stderr

    base_dir = os.path.dirname(os.path.abspath(__file__))
    roster_path = os.path.join(base_dir, "roster.csv")
    log_dir = os.path.join(base_dir, "logs")
    extra_logs = [os.path.join(base_dir, "battle_log.csv")]

    ship_type = args.ship
    ship_label = ship_type.capitalize()
    top_n = args.top

    # Ship name map for display
    SHIP_NAMES = {
        "explorer": "U.S.S. ENTERPRISE-D (Galaxy Class Explorer)",
        "battleship": "NEGH'VAR (T8 Battleship)",
        "interceptor": "Interceptor",
    }
    ship_display = SHIP_NAMES.get(ship_type, f"{ship_label} class")

    scenarios_to_run = _get_scenarios_from_mode(args.mode)
    total_steps = 2 + len(scenarios_to_run) + (0 if args.skip_legacy else 1)
    step = 0

    print("=" * 100)
    print(f"  STFC CREW OPTIMIZER — Multi-Scenario Analysis")
    print(f"  Ship: {ship_display}  |  Mode: {args.mode.upper()}  |  Top {top_n}")
    print("=" * 100)

    # ---- Load roster ----
    step += 1
    print(f"\n[{step}/{total_steps}] Loading officer roster ...")
    optimizer = STFCCrewOptimizer(roster_path)
    print(f"  Loaded {len(optimizer.officers)} officers")

    # ---- Analyze battle logs ----
    step += 1
    print(f"\n[{step}/{total_steps}] Analyzing battle logs ...")
    analyzer = BattleLogAnalyzer()
    analyzer.load_all_logs(log_dir=log_dir, extra_files=extra_logs)
    print(f"  Loaded {len(analyzer.battles)} battle(s)")

    if not args.skip_battles:
        for battle in analyzer.battles:
            analyzer.print_battle_summary(battle)

    # ---- Build weakness profile ----
    weakness = analyzer.get_weakness_profile()
    print(f"\n{'=' * 100}")
    print("  WEAKNESS PROFILE (from battle history)")
    print(f"{'=' * 100}")
    print(f"  Battles analyzed:         {weakness['total_battles']}")
    print(f"  Losses:                   {weakness['losses']}")
    print(f"  Crit Damage Gap:          {weakness['crit_damage_gap']:.1f}/10")
    print(f"  Crit Hit Disadvantage:    {weakness['crit_hit_disadvantage']:.1f}/10")
    print(f"  Shield Timing Loss:       {weakness['shield_timing_loss']:.1f}/10")
    print(f"  Stat Paradox:             {weakness['stat_paradox']:.1f}/10")
    print(f"  Damage Escalation:        {weakness['damage_escalation']:.1f}/10")

    evaluator = PvPCrewEvaluator(optimizer.officers, weakness, ship_type=ship_type)

    # ---- JSON output mode ----
    if args.json:
        output = _handle_json_mode(
            args,
            evaluator,
            optimizer,
            analyzer,
            weakness,
            ship_type,
            ship_label,
            ship_display,
            top_n,
        )
        # Write JSON to real stdout (not the stderr redirect)
        _real_stdout.write(json.dumps(output, indent=2, default=str) + "\n")
        return

    # ---- Scenario-based crew optimization ----
    for scenario in scenarios_to_run:
        step += 1
        label = PvPCrewEvaluator.SCENARIO_LABELS.get(scenario, scenario)
        rec = PvPCrewEvaluator.SHIP_RECOMMENDATIONS.get(scenario, {})
        rec_ship = rec.get("best", ship_type)
        rec_reason = rec.get("reason", "")

        print(f"\n{'=' * 100}")
        print(
            f"  [{step}/{total_steps}] OPTIMIZING {label.upper()} — {ship_label.upper()} CREWS"
        )
        if ship_type != rec_ship:
            print(
                f"  Note: Recommended ship for {label} is {rec_ship.capitalize()} "
                f"({rec_reason})"
            )
        print(f"{'=' * 100}")

        crews = evaluator.find_best_scenario_crews(
            scenario, ship_type=ship_type, top_n=top_n
        )

        print(f"\n{'=' * 100}")
        print(
            f"  TOP {top_n} {label.upper()} — {ship_label.upper()} CREW RECOMMENDATIONS"
        )
        print(f"{'=' * 100}")

        _print_crew_results(crews, optimizer.officers, ship_label, f"{label} Score")

    # ---- Legacy reports ----
    if not args.skip_legacy:
        step += 1
        print(f"\n{'=' * 100}")
        print(f"  [{step}/{total_steps}] Generating reports ...")
        print(f"{'=' * 100}")

        optimizer.generate_html_report(
            output_path=os.path.join(base_dir, "stfc_crew_recommendations.html")
        )

        print(f"\n{'=' * 100}")
        print("  LEGACY PER-SCENARIO RECOMMENDATIONS (individual scoring)")
        print(f"{'=' * 100}")

        recommendations = optimizer.generate_all_recommendations()
        for scenario, crew in recommendations.items():
            print(f"\n  --- {scenario} ---")
            if not crew:
                print("   No officers found")
                continue
            for i, officer in enumerate(crew, 1):
                print(
                    f"  {i}. {officer['name']} (Lvl {officer['level']}/{officer['rank']}) "
                    f"ATK:{officer['attack']:,.0f} DEF:{officer['defense']:,.0f} "
                    f"HP:{officer['health']:,.0f} Score:{officer['score']:,.0f}"
                )

    print(f"\n{'=' * 100}")
    print("  Done.")
    print(f"{'=' * 100}")


if __name__ == "__main__":
    main()
