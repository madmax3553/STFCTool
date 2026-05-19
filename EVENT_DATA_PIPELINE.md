# Event Data Pipeline

Goal: make event recommendations depend on real event requirements and payouts, not on generic "event active" signals.

## What The Live Sync Provides

The `platform_event` sync payload provides:

- `config_id`
- category/source/placement type
- schedule
- current score/rank/claimable state
- scoring metadata IDs/icons
- milestone thresholds
- reward type IDs such as `chest_1597370152`

It does not currently provide localized event names or chest contents.

## What Is Now Preserved

`data/player_data/player_data.json` now persists full event metadata and reward segments instead of only `reward_count`.

That means the app can show:

- scoring metadata
- milestone thresholds
- reward IDs
- claimability
- grouped similar event rows

## Cache And Fallback Signals

The app now checks the event-adjacent `/consumable` cache with a 24-hour TTL during game-data refresh. Startup stays cache-only, while `r` refreshes stale static/helper data.

Event rows use:

- `*` when the display name is derived from category/source because no localized event title was available.
- `!` when reward contents are unresolved.
- `C#` when grouped rows include claimable rewards.

Cache staleness is shown in the summary/detail as `fresh-cache`, `stale-cache`, or `missing` so it does not mark every row as individually actionable.

Labels resolve in this order:

1. Real `group_name` from live sync, when present.
2. Local overrides in `data/game_data/event_labels.json`.
3. Derived fallback label from scope, term/duration, category, scoring icon, and priority code.

Override file shape:

```json
{
  "labels": {
    "config_id:7dcfe8f336682e4c7be3ea6525948b241d2ceb8e": "Real Event Name",
    "category:28|priority:100005|scope:alliance": "Alliance Event Family Name"
  }
}
```

## Remaining Missing Link

To answer "does this event pay 6* Ore?" the planner needs a chest-definition table:

```json
{
  "chest_1597370152": [
    {"resource_id": 123, "name": "6* Ore", "amount": 1000000}
  ]
}
```

The public Spocks Club API currently exposes resources and consumables, but the synced `chest_*` reward IDs from platform events do not resolve through `/consumable/{id}` and are not present in `/consumable`.

Checked on 2026-05-16:

- `/event` -> 404
- `/platform_event` -> 404
- `/translations/en/events` -> 404
- `/chest` -> 404
- `/consumable/1597370152` -> 404 for a live event `chest_*` reward ID

`stfc.pro` was also checked on 2026-05-16. It exposes useful statistics endpoints, but they do not currently solve event title or payout resolution:

- `https://stfc.pro/api/events` returns broad event groups and windows such as `alliance_tournaments`, `incursions`, `sarris_invasions`, and `flashpoint`.
- `https://stfc.pro/api/schedules` reports source freshness. Its `eventdata_fetch.py` last completed on `2026-04-26T16:50:45Z`, so event data is not currently fresh.
- `https://stfc.pro/api/tournaments` works with the same anonymous session flow used by the site and an `X-STFC-Token` header from `/api/request-token`, but it returns alliance tournament ranking rows, not platform event definitions.
- The client bundle references only broad/status endpoints: `/api/events`, `/api/schedules`, `/api/server-status`, `/api/tournaments`, and account/settings endpoints.

Useful planner role: `stfc.pro` can provide macro context like "alliance tournament window is/was active" and external ranking context, but it should not be treated as a resolver for live sync `config_id`, event serial labels, milestone rewards, or `chest_*` contents.

## Planner Rule

Once chest contents are resolvable, the strategy planner should:

1. Build current needs from `ActionPlan.save_for` and missing resources.
2. Expand event reward chests into resource payouts.
3. Match event payouts against current needs.
4. Match event scoring requirements against current planned activity.
5. Promote the event only when both are true:
   - reward payout advances a current bottleneck
   - scoring requirement overlaps planned work or cheap passive work

Example:

- Current need: `6* Ore`
- Event pays: `6* Ore`
- Event scoring: mining / auto-grind / Reliant-compatible activity
- Recommendation: join/complete event before unrelated active loops.

If payout or scoring cannot be resolved, the planner must not promote the event as high confidence.
