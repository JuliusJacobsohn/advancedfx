# Demo Playback Ranks

Status: scoreboard-only implementation exists.

Current command:

```text
mirv_scoreboard_rank byXuid add x<steamid64> premier <rating> [wins <iWins>]
mirv_scoreboard_rank byXuid add x<steamid64> competitive <rank 1-18> [wins <iWins>]
mirv_scoreboard_rank byXuid add x<steamid64> wingman <rank 1-18> [wins <iWins>]
mirv_scoreboard_rank byXuid remove x<steamid64>
mirv_scoreboard_rank clear
mirv_scoreboard_rank print
mirv_scoreboard_rank apply
```

Implementation approach:

- Visual scoreboard override, not a demo-message or GC-data override.
- Uses a `CUIEngine::RunScript` bridge.
- Finds scoreboard row `player-<xuid>`.
- Finds its `jsRatingEmblem`.
- Calls Valve's own `RatingEmblem.SetXuid(...)` with the replacement rating type, score/rank, and wins.
- Registers a `Scoreboard_UpdateJob` reapply handler because scoreboard rows refresh while open.

Difficulty after implementation: low-medium for scoreboard-only overrides; still high for a general client-data rank override.

Notes:

- SteamID64 targeting is reliable for finding rows.
- Real ranks are not retrieved automatically. Commands should accept explicit fake/desired visual values.
- Other rank surfaces, such as end-of-match or profile cards, may need separate Panorama hooks.
