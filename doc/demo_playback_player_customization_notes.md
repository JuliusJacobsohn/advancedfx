# Demo Playback Player Customization

Target use case: fragmovie production from demos. All work in this area is intended to be purely visual during local demo playback and recording. The goal is to anonymize players or exaggerate matches by changing how names, ranks, avatars, player colors, skins, and chat render. There is no need to change the demo file, server truth, Steam inventory ownership, matchmaking state, or live gameplay state.

Current topic notes:

- [Names](demo_playback_names.md)
- [Ranks](demo_playback_ranks.md)
- [Player color](demo_playback_player_color.md)
- [Avatars](demo_playback_avatars.md)
- [Weapon skins](demo_playback_weapon_skins.md)
- [Synthetic chat](demo_playback_chat.md)

Current implementation status:

| Topic | Status | Notes |
| --- | --- | --- |
| Names | Done | Runtime `mirv_replace_name` hooks work for HUD/UI name lookups; death notices are handled through `mirv_deathmsg`. |
| Ranks | Done | Scoreboard visual rank overrides work through `mirv_scoreboard_rank`; other rank surfaces would be separate follow-up work. |
| Synthetic chat | Done | `mirv_chat_insert` inserts native all/team chat rows through CS2's real closed-chat feed and can be scheduled with `mirv_cmd`. |
| Player color | Done | `mirv_player_color` writes `CCSPlayerController::m_iCompTeammateColor`; verified for HUD/chat color in the Inferno test demo and good enough for local fragmovie/demo-render use. |
| Avatars | Done | `mirv_avatar` replaces demo player avatars with avatars resolved from another SteamID64; verified for top bar, scoreboard, and bottom spectator bar without overlay panels. |
| Weapon skins | Prototype/investigation | Partial skin path found, but Stattrak/counter and robustness issues remain. |
| Agent models | Prototype/investigation | `mirv_demo_agent` can inspect demo player pawn model state and apply a Vypa model override once to all real `C_CSPlayerPawn` entities. Repeat-per-frame application is disabled after crash reports. |

Agent model prototype notes:

- Built-in alias: `vypa -> agents/models/tm_jungle_raider/tm_jungle_raider_variante.vmdl`, from item def `4777` / `customplayer_tm_jungle_raider_variante`.
- Stable command sequence for the test demo: `mirv_demo_agent inspect`, `mirv_demo_agent all set vypa`, `mirv_demo_agent apply`, `mirv_demo_agent inspect`.
- 2026-06-07 verification on `replays/match730_003816038387630997543_1135542072_274.dem`: inspect listed 10 `C_CSPlayerPawn` entries; single apply changed all 10 to one shared model handle/name symbol and exited cleanly through scheduled `quit`.
- Avoid applying to `C_CSObserverPawn` entries and avoid automatic frame-by-frame `SetModel` calls while this remains experimental.

Shared implementation constraints:

- Prefer demo-playback guards for every visual override.
- Prefer UI/Panorama or client-side render overrides over network, GC, or inventory mutation.
- Commands should accept explicit values. SteamID64/XUID is useful for targeting, not for retrieving private/current matchmaking data.
- A feature is successful when captured video output looks correct.

Current follow-up work:

- Re-test player color overrides on more demos, maps, teams, and spectator states.
- Decide whether `mirv_player_color` should be part of an official upstream PR or kept with the broader demo-customization branch.
- Re-test avatar overrides after CS2 updates, especially scoreboard and bottom spectator bar paths.
- Keep weapon skin customization as a separate follow-up topic.

Shared repo building blocks:

- XUID/player lookup: `CEntityInstance::GetSteamId`, `mirv_deathmsg help players`, and controller/pawn enumeration.
- Runtime schema offsets: `AfxHookSource2/SchemaSystem.cpp`.
- Frame-stage reapply point: `ClientFrameStageNotify` around `FRAME_RENDER_PASS`.
- Scheduled commands: `mirv_cmd addAtTick` / `mirv_cmd addAtTime`.
- Panorama bridge patterns: current scoreboard-rank override and existing death-notice UI hooks.
- Decompiled local CS2 Panorama assets are available for research under the ignored repo folder `research/cs2-chat-vrf/`; the extraction tool is under `research/tools/vrf-cli/`. These files are not committed, but keeping them below the repo avoids scattering project research under random temp folders.
