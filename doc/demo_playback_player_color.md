# Demo Playback Player Color

Status: researched, not implemented.

This means the player color used for avatar frame, scoreboard row indicator, radar/minimap/team indicators, and similar UI surfaces.

Current CS2 tracking shows:

- Scoreboard avatar rows call `GameStateAPI.GetPlayerColor(xuid)` and write the result into the `player-color` image `washColor`.
- Generic/lobby avatar panels call `PartyListAPI.GetPartyMemberSetting(xuid, 'game/teamcolor')`, convert it through `TeamColor.GetTeamColor`, and write that into `JsAvatarTeamColor`.
- Client schema exposes `CCSPlayerController::m_iCompTeammateColor`.

Likely command shape:

```text
mirv_player_color byXuid add x<steamid64> yellow|purple|green|blue|orange|<0-4>
mirv_player_color byXuid remove x<steamid64>
mirv_player_color clear
mirv_player_color print
mirv_player_color apply
```

Likely implementation:

1. Add `CCSPlayerController::m_iCompTeammateColor` to schema offsets.
2. Store overrides by XUID.
3. Enumerate player controllers, match `GetSteamId()`, and write the color index.
4. Reapply during demo playback because demo/client updates may restore original values.

Color index mapping:

```text
0 -> cl_teammate_color_1
1 -> cl_teammate_color_2
2 -> cl_teammate_color_3
3 -> cl_teammate_color_4
4 -> cl_teammate_color_5
```

In normal CS2 settings these correspond to yellow, purple, green, blue, and orange, but the actual RGB values come from client cvars.
