#!/usr/bin/env python3
"""
STFC Crew Optimizer
Analyzes officer roster and generates optimized crews for different scenarios
"""

import csv
import json
from typing import List, Dict
from datetime import datetime


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

    def generate_html_report(
        self, output_path: str = "/home/groot/Downloads/stfc_crew_recommendations.html"
    ):
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
                medal = ["🥇", "🥈", "🥉"][i - 1]
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

        print(f"HTML report saved to: {output_path}")


def main():
    optimizer = STFCCrewOptimizer(
        "/home/groot/Downloads/Copy of 1.8.M84 STFC Officers Tool - Roster.csv"
    )
    print(f"Loaded {len(optimizer.officers)} officers\n")

    recommendations = optimizer.generate_all_recommendations()

    for scenario, crew in recommendations.items():
        print(f"\n{'=' * 90}")
        print(f"🎯 {scenario}")
        print(f"{'=' * 90}")

        if not crew:
            print("   No officers found for this scenario")
            continue

        for i, officer in enumerate(crew, 1):
            medals = ["🥇", "🥈", "🥉"]
            print(
                f"\n{medals[i - 1]} {i}. {officer['name']} (Lvl {officer['level']}/{officer['rank']})"
            )
            print(
                f"   ATK: {officer['attack']:>10,.0f} | DEF: {officer['defense']:>10,.0f} | HP: {officer['health']:>10,.0f}"
            )
            print(f"   CM: {officer['cm_pct']:>6.0f}% | OA: {officer['oa_pct']:>8.0f}%")
            if officer["display"]:
                preview = officer["display"][:70]
                print(f"   Abilities: {preview}...")
            print(f"   Score: {officer['score']:.0f}")

    # Generate HTML report
    print(f"\n{'=' * 90}")
    print("Generating HTML report...")
    optimizer.generate_html_report()


if __name__ == "__main__":
    main()
