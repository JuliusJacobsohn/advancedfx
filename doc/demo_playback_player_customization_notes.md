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
| Agent models | Prototype/investigation | `mirv_demo_agent` can inspect demo player pawn model state and apply model overrides by SteamID64/XUID. It hooks normal model assignment and also repairs configured pawns after client frame-stage updates if demo skip/rewind restores original model state. |

Agent model prototype notes:

- Agent values can be direct `agents/models/...vmdl` paths, numeric item definition IDs, or internal `customplayer_*` names. Full list: [cs2_agent_models.md](cs2_agent_models.md).
- Stable command sequence for a specific player: `mirv_demo_agent inspect`, `mirv_demo_agent xuid <steamid64> set customplayer_tm_jungle_raider_variante`, `mirv_demo_agent inspect`.
- Manual test helper: `mirv_demo_agent slot <1-10> set customplayer_tm_jungle_raider_variante`. Slots are resolved from current pawn enumeration and immediately stored as XUID-specific overrides; they are not stable identifiers.
- Use `mirv_demo_agent apply` to re-apply all currently configured XUID overrides once.
- Configured XUID overrides are applied through a detour on the client `CBaseModelEntity_SetModel` candidate. When CS2 assigns a model to a `C_CSPlayerPawn`, the hook resolves pawn -> controller -> XUID and substitutes the configured model before the original assignment runs. `mirv_demo_agent apply` remains only as an immediate helper for already-spawned pawns.
- 2026-06-07 skip/rewind finding: demo seeking can restore player model state without hitting the current `SetModel` detour. The prototype now records the target `CModelState` handle/name symbol after applying an override and checks configured player pawns after client frame-stage notifications. If a configured pawn no longer matches its remembered target model state, the override is applied again.
- This is a repair at the client frame-stage boundary, not yet the ideal low-level packet/entity-field replacement. It should survive skip/rewind better than the pure `SetModel` detour, and it avoids unconditional per-frame `SetModel` spam by only reapplying on detected mismatch.
- Seek safety detail: an early repair immediately after demo skip produced `CModelState::SetupModel` assertions while the pawn identity still had `EF_IN_STAGING_LIST`. The repair path now reads `CEntityIdentity::m_flags` and skips automatic repair while spawn/staging/delete/construction bits are present. `mirv_demo_agent inspect` prints identity flags for this reason.
- 2026-06-07 verification on `replays/match730_003816038387630997543_1135542072_274.dem`: inspect listed 10 `C_CSPlayerPawn` entries; single apply changed all 10 to one shared model handle/name symbol and exited cleanly through scheduled `quit`.
- 2026-06-07 seek verification on the same demo: ten different configured agent models survived multiple skips. Console showed repairs after forward seeks to demo ticks `9807` and `14903`, and after a backward seek to demo tick `29`. After adding the entity staging guard, the previous `CModelState::SetupModel` assertion did not reappear in the filtered log.
- 2026-06-07 newer-demo verification on `replays/match730_003824372541888135516_1872110164_388.dem`: internal `customplayer_*` names resolved to model paths and applied to all 10 player slots. Final status at demo tick `31`: `configured=10 remembered=10 present=10 unsafe=0 mismatched=0`. A separate scheduled seek run on the same demo repaired all 10 overrides after a jump to demo tick `10000`; the schedule was too compressed for useful visual inspection, so the follow-up launch intentionally left the demo running.
- Avoid applying to `C_CSObserverPawn` entries. Automatic repair is currently limited to configured `C_CSPlayerPawn` entries during active demo playback.
- Current limitation: overrides are stored in process memory only; they are not saved to disk or embedded into demo files.

Lower-level agent replacement research:

- CS2 demos replay normal network data. Relevant protobuf messages include `CSVCMsg_CreateStringTable`, `CSVCMsg_UpdateStringTable`, and `CSVCMsg_PacketEntities`; model names / indices are expected to enter the client through network string tables plus packet-entity state rather than through a bespoke demo-only agent format.
- A resource-load hook alone is probably too late and too broad. It can redirect a requested `.vmdl`, but it does not naturally know which player XUID caused the request.
- Better low-level targets are either the decoded model string/index before it is assigned to the player pawn, or the client model-assignment path (`CBaseModelEntity_SetModel` / model-state update) with current entity context available.
- Current agent path intercepts model assignment for `C_CSPlayerPawn`, maps pawn -> controller -> XUID, and substitutes the configured model before the original assignment finishes. Demo seeking appears to have a model-state restore path that can bypass this function, so a frame-stage mismatch repair currently backs it up.
- The local CounterStrikeSharp schema dump marks `CModelState::m_hModel` as `NetworkEnable` with `NetworkChangeCallback` `skeletonModelChanged`. That matches the observed skip/rewind behavior: a demo packet/full-update can restore `CModelState` directly and invoke the model-change path without going through the `CBaseModelEntity_SetModel` function currently detoured.
- This tree currently has reusable client hooks for entity add/remove and `ClientFrameStageNotify`, but not for packet-entity decode, network field proxying, or the `skeletonModelChanged` callback itself. A true low-level replacement would require adding a new hook layer in the networked-property application path, then carrying current entity context into the replacement decision.
- The branch-local repair loop is therefore intentionally hybrid rather than fully low-level: `SetModel` catches normal model assignment, and the post-frame mismatch check catches direct network/model-state restores. It is less clean than decode-time replacement, but it is scoped, uses schema offsets already present in the repo, and avoids guessing a volatile callback signature.
- For weapon skins, the equivalent target is lower than agent model assignment: packet-entity/econ fields for weapon entities, keyed by owner XUID plus weapon identity. That requires mapping weapon entity -> owner pawn/controller -> XUID and replacing paint/econ fields when weapon state is decoded or applied.

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
