# Demo Playback Names

Status: working.

Name replacement is a runtime client hook. It does not edit `.dem` files and does not rewrite demo packets. HLAE is injected into CS2 and changes what the local client returns when HUD/UI code asks for player names.

Relevant files:

- `AfxHookSource2/ReplaceName.cpp`
- `AfxHookSource2/ClientEntitySystem.cpp`
- `AfxHookSource2/DeathMsg.cpp`
- `AfxHookSource2/SchemaSystem.cpp`

`mirv_replace_name` stores replacement strings by controller entity index or by XUID / SteamID64. `byUserId` adds `1` to the supplied userid before using it as a controller index, matching the CS2 convention used elsewhere in HLAE.

Main hooks:

- `New_CCSPlayerController_GetPlayerName`
- `New_GetDecoratedPlayerName`

Death notices are handled separately by `mirv_deathmsg`. That path wraps `player_death` events and can patch sanitized controller names while the death notice is constructed.

Useful helper:

```text
mirv_deathmsg help players
```

This prints name, userid, XUID, and spectator key.
