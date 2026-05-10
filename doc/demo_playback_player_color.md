# Demo Playback Player Color

Status: implemented native controller-field prototype.

This means the player color used for avatar frame, scoreboard row indicator, radar/minimap/team indicators, and similar UI surfaces.

Current CS2 tracking shows:

- Scoreboard avatar rows call `GameStateAPI.GetPlayerColor(xuid)` and write the result into the `player-color` image `washColor`.
- Generic/lobby avatar panels call `PartyListAPI.GetPartyMemberSetting(xuid, 'game/teamcolor')`, convert it through `TeamColor.GetTeamColor`, and write that into `JsAvatarTeamColor`.
- Client schema exposes `CCSPlayerController::m_iCompTeammateColor`.

Current command:

```text
mirv_player_color byXuid add x<steamid64> blue|green|yellow|orange|purple|<0-4>
mirv_player_color byXuid remove x<steamid64>
mirv_player_color clear
mirv_player_color print
mirv_player_color apply
mirv_player_color inspect
```

Implementation:

- Adds `CCSPlayerController::m_iCompTeammateColor` to schema offsets.
- Stores overrides by XUID.
- Enumerates player controllers, matches `GetSteamId()`, and writes the color index.
- Applies immediately when adding an override during active demo playback, and can be reapplied manually with `mirv_player_color apply`.
- `inspect` prints current controller XUIDs, color field values, names, and whether an override is configured.

Visible color index mapping verified with the Inferno test demo:

```text
0 -> blue / cyan
1 -> green
2 -> yellow
3 -> orange
4 -> purple / pink
```

The command names map to the verified visible colors. Raw numeric values still write the raw controller field value.

Verification:

- Built `AfxHookSource2` with the local x64 target. The DLL was emitted directly to `build\Release\dist\bin\x64\AfxHookSource2.dll`.
- Launched the documented Inferno demo through HLAE with Wladimir Putin renamed to `MS Word`, `mirv_player_color byXuid add x76561197960680616 purple`, and a team chat message `hi team` at the demo start.
- Screenshot one second after the chat insert showed the chat color dot as purple/pink and the player renamed to `MS Word`.
- A follow-up test with the frame-stage reapply disabled still showed the requested purple/pink chat dot, so the implementation does not use a per-frame color write.
- The test harness used scheduled `quit`; do not schedule `demo_pause` for this test because it can leave playback appearing stuck if the user presses play near that scheduled tick.
