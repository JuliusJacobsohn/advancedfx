# Demo Playback Synthetic Chat

Status: researched, not implemented. This is the next planned feature after the rank-switcher branch.

Desired feature: insert chat messages at a specific demo tick or timestamp with player name, location, team, alive/dead state, team/all visibility, and message text.

Scheduling is already mostly solved by existing HLAE commands:

```text
mirv_cmd addAtTick 12345 mirv_chat_insert byXuid x765... team=CT alive=1 visibility=team location="A Ramp" message="rotate"
```

Likely command shape:

```text
mirv_chat_insert byUserId <id>|byXuid x<steamid64>|name <displayName> team=T|CT|spec|none alive=0|1 visibility=all|team location=<string>|none message=<string>
```

Current repo clues:

- `mirv_replace_name` does not replace existing chat lines; `resources/AfxHookSource2_changelog.xml` explicitly recommends `tv_nochat true` for old chat.
- `mirv_cmd addAtTick` / `mirv_cmd addAtTime` already solve scheduling, so a first implementation can focus on rendering one synthetic row at command execution time.
- The repo has game-event hooks, but not a CS2 user-message injection path yet.
- The existing Panorama bridge used by scoreboard rank overrides is the most useful starting point for a visual-only first version.

Implementation routes:

1. Panorama-only visual insertion
   - Find the HUD chat panel and append rows directly.
   - Easiest for demo-only visual output.
   - Must reproduce CS2 chat styling, fading, scroll behavior, team color/name formatting, and filters.
   - Recommended first milestone because it matches the fragmovie-only goal and avoids real user-message construction.

2. Synthetic user-message injection
   - Construct and dispatch something equivalent to `CCSUsrMsg_SayText2`.
   - More native formatting/lifetime.
   - Requires user-message creation/dispatch hooks that the repo does not currently have.

3. Demo-file rewrite
   - Insert user-message packets into the demo stream.
   - Most faithful but largest scope. The repo does not currently have a CS2 `.dem` protobuf rewrite pipeline.

Useful chat token mapping to investigate:

- `visibility=all`, `alive=1` -> likely `Cstrike_Chat_All`
- `visibility=all`, `alive=0` -> likely `Cstrike_Chat_AllDead`
- `visibility=team`, `team=CT`, `alive=1`, `location` set -> likely `Cstrike_Chat_CT_Loc`
- `visibility=team`, `team=T`, `alive=1`, `location` set -> likely `Cstrike_Chat_T_Loc`
- dead team chat -> likely `Cstrike_Chat_CT_Dead` / `Cstrike_Chat_T_Dead`

External references:

- <https://pkg.go.dev/github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/events#SayText2>
- <https://pkg.go.dev/github.com/astephensen/csgodemogo/cstrikeproto#CCSUsrMsg_SayText2>
- <https://github.com/SteamDatabase/Protobufs/blob/master/csgo/cstrike15_usermessages.proto>

Suggested first branch tasks:

1. Inspect current CS2 Panorama chat files in `GameTracking-CS2`.
2. Locate the live chat panel path and row template/classes.
3. Add a small `mirv_chat_insert` command that runs a Panorama script to append one row.
4. Use `mirv_cmd addAtTick` for scheduling instead of building a new scheduler.
5. Keep a `tv_nochat true` workflow in mind if synthetic chat is meant to fully replace recorded chat.
