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
| Player color | Prototype verified | `mirv_player_color` writes `CCSPlayerController::m_iCompTeammateColor`; verified for HUD/chat color in the Inferno test demo. |
| Avatars | Notes only | Intended as a visual override topic; implementation details are tracked separately. |
| Weapon skins | Prototype/investigation | Partial skin path found, but Stattrak/counter and robustness issues remain. |

Shared implementation constraints:

- Prefer demo-playback guards for every visual override.
- Prefer UI/Panorama or client-side render overrides over network, GC, or inventory mutation.
- Commands should accept explicit values. SteamID64/XUID is useful for targeting, not for retrieving private/current matchmaking data.
- A feature is successful when captured video output looks correct.

Current follow-up work:

- Re-test player color overrides on more demos, maps, teams, and spectator states.
- Decide whether `mirv_player_color` should be part of an official upstream PR or kept with the broader demo-customization branch.
- Keep avatar and weapon skin customization as separate follow-up topics.

Shared repo building blocks:

- XUID/player lookup: `CEntityInstance::GetSteamId`, `mirv_deathmsg help players`, and controller/pawn enumeration.
- Runtime schema offsets: `AfxHookSource2/SchemaSystem.cpp`.
- Frame-stage reapply point: `ClientFrameStageNotify` around `FRAME_RENDER_PASS`.
- Scheduled commands: `mirv_cmd addAtTick` / `mirv_cmd addAtTime`.
- Panorama bridge patterns: current scoreboard-rank override and existing death-notice UI hooks.
- Decompiled local CS2 Panorama assets are available for research under the ignored repo folder `research/cs2-chat-vrf/`; the extraction tool is under `research/tools/vrf-cli/`. These files are not committed, but keeping them below the repo avoids scattering project research under random temp folders.
