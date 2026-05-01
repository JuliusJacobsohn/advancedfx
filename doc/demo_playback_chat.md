# Demo Playback Synthetic Chat

Status: working native prototype. Visual Panorama overlays and direct `ChatHistoryText` mutation were rejected because they do not enter CS2's real closed-chat feed. The current implementation constructs a native `CUserMessageSayText2` message through Source 2's `NetworkMessagesVersion001`, fills the CS2-allocated protobuf object directly, and calls CS2's native `CHudChatDelegate` SayText2 HUD handler. This renders into the real closed chat feed with CS2's own row styling and lifetime.

Desired feature: insert chat messages at a specific demo tick or timestamp with player name, location, team, alive/dead state, team/all visibility, and message text.

For the local test demo, use the known player names and XUIDs in [demo_playback_test_demo_roster.md](demo_playback_test_demo_roster.md).

Target command shape:

```text
mirv_chat_insert byXuid x<steamid64> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>
mirv_chat_insert byUserId <id> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>
mirv_chat_insert name <displayName> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>
mirv_chat_insert clear
mirv_chat_insert inspect
```

Scheduling is handled by the existing `mirv_cmd` system:

```text
mirv_cmd addAtTick 12345 mirv_chat_insert byXuid x76561197962023477 team=CT alive=1 visibility=team location=A_Ramp message rotate now
mirv_cmd addAtTime 42.5 mirv_chat_insert name caster team=spec visibility=all message bought two smokes
```

The `message` token deliberately consumes all remaining command arguments. This makes scheduled commands more reliable, because `mirv_cmd addAtTick` stores and replays the remaining command text as one command string.

Rejected implementations:

- A separate Panorama overlay using `$.CreatePanel` under the HUD was tested and removed.
- It rendered text in a different place and style from native CS2 chat, and it did not flow through the real chat panel or chat row template.
- Appending formatted text into `CSGOHudChat` / `ChatHistoryText` was tested and disabled. It can make the full chat popup appear, but it does not append a real row into the normal closed-chat feed. Real demo chat messages continue to render separately.
- These are not acceptable for the feature. The implementation must inject into, or call into, CS2's real chat rendering path.

Repo clues:

- `mirv_replace_name` does not replace existing chat lines; `resources/AfxHookSource2_changelog.xml` explicitly recommends `tv_nochat true` for old chat.
- `mirv_cmd addAtTick` / `mirv_cmd addAtTime` solve scheduling, so the chat command only needs to render one synthetic row at command execution time.
- The repo has game-event hooks, but not a CS2 user-message injection path yet.
- The existing Panorama bridge used by scoreboard rank overrides is the most useful starting point for visual-only demo customization.

Research findings:

- GameTracking-CS2 confirms compiled chat assets exist: `panorama/layout/chat.vxml_c`, `panorama/layout/hud/hudchat.vxml_c`, `panorama/scripts/chat.vts_c`, `panorama/styles/chat.vcss_c`, and `panorama/styles/hud/hudchat.vcss_c`.
- `cstrike15_usermessages.proto` confirms native message ids: `CS_UM_SayText = 305`, `CS_UM_SayText2 = 306`, `CS_UM_TextMsg = 307`, and `CS_UM_RadioText = 322`.
- The available GameTracking files are compiled assets / package indexes, not decompiled Panorama source.
- Local decompiled CS2 Panorama assets are kept in the ignored repo folder `research/cs2-chat-vrf/`. The in-game HUD chat asset is `panorama/layout/hud/hudchat.xml`; it defines a native `CSGOHudChat` panel with `ChatHistoryText`. The lobby / party chat asset `panorama/scripts/chat.js` is not the in-game HUD chat path.
- Current SteamDatabase `usermessages.proto` maps `UM_SayText2` to id `118`. This is the base Source 2 user-message id used by the current native prototype; the older `cstrike15_usermessages.proto` ids such as `CS_UM_SayText2 = 306` are not the id used for `NetworkMessagesVersion001::FindNetworkMessageById`.
- CounterStrikeSharp's user-message API provided the useful allocation pattern: `INetworkMessages::FindNetworkMessageById` / `FindNetworkMessagePartial`, `INetworkMessageInternal::AllocateMessage`, and protobuf field mutation.
- SwiftlyS2's CS2 gamedata was useful for event-system research, but event dispatch was not the successful local-HUD path. The retained implementation calls the native HUD SayText2 handler directly.
- HLAE's linked protobuf version must not be used directly against CS2-allocated protobuf objects. A previous attempt cast the engine-allocated `CNetMessagePB<CUserMessageSayText2>` to HLAE's protobuf ABI and crashed.
- Feeding normal protobuf wire bytes to CS2's `SayText2` parser failed without crashing (`bitsRead=72`), and feeding the legacy Source user-message stream also failed without crashing (`bitsRead=88`). The current path avoids both parser formats and fills the CS2-allocated protobuf object directly.
- A local generated protobuf layout check under `research/proto-gen/` matches CS2's observed allocation size: `CNetMessagePB<CUserMessageSayText2>` is `0x78`, with a `0x30` `CNetMessage` base and a `0x48` `CUserMessageSayText2` protobuf subobject. The fields used by the direct writer are `has_bits` at `0x10`, `messagename` at `0x18`, `param1` at `0x20`, `param2` at `0x28`, `param3` at `0x30`, `param4` at `0x38`, `chat` at `0x40`, and `entityindex` at `0x44` relative to `CNetMessage::AsProto()`.

Implementation direction:

- Construct and dispatch something equivalent to `CUserMessageSayText2`.
- The current prototype fills fields `entityindex`, `chat`, `messagename`, `param1`, `param2`, `param3`, and `param4` directly in the real protobuf object returned by `CNetMessage::AsProto()`.
- The result must be rendered by the real CS2 chat component, using the same position, row template, lifetime, font, colors, and formatting as demo chat.
- A Panorama implementation is acceptable only if it calls a native method on `CSGOHudChat`. Creating separate HUD labels/panels, or writing `ChatHistoryText` directly, is the rejected overlay/popup approach.
- For replacement workflows, hide recorded chat with CS2/HLAE settings such as `tv_nochat true` where applicable, then schedule synthetic native chat rows.

Current implementation behavior:

- `mirv_chat_insert` resolves the player name and controller entity index for `byXuid` / `byUserId`.
- For `byXuid` / `byUserId`, omitted `team` and `alive` values are inferred from the current player controller / pawn when possible. Use explicit `team=` / `alive=` to override the inferred state for staged conversations.
- It selects `Cstrike_Chat_All`, `Cstrike_Chat_AllDead`, `Cstrike_Chat_AllSpec`, `Cstrike_Chat_CT`, `Cstrike_Chat_T`, `Cstrike_Chat_CT_Loc`, `Cstrike_Chat_T_Loc`, `Cstrike_Chat_CT_Dead`, `Cstrike_Chat_T_Dead`, or `Cstrike_Chat_Spec` from `visibility`, `team`, `alive`, and `location`.
- It allocates native message id `118` / partial name `SayText2` and fills the CS2-allocated `CUserMessageSayText2` object directly. The direct layout comes from locally generated protobuf 3.21.8 code and matches the observed `0x78` allocation.
- The parser-based paths remain documented as failed probes: raw protobuf wire data and the legacy byte/string `SayText2` stream both reached the parser but were rejected.
- It calls CS2's native `CHudChatDelegate` SayText2 handler directly. This is the first path that rendered a visible synthetic row in the real closed chat feed.
- Old event-system dispatch experiments are intentionally not exposed as command options: broadcast posted but was invisible, while local/filter paths crashed during testing.
- The client-side `player_chat` event probe is disabled after an insertion crash report; it is not the current path.
- `mirv_chat_insert inspect` prints the located chat panel, exposed Panorama properties, native interface pointers, the current `CGameEventManager` pointer, and the resolved `SayText2` message type. It deliberately does not allocate or dispatch `CUserMessageSayText2`.
- Recent inspect result found `CSGOHudChat` and `ChatHistoryText`, but only generic methods were exposed from Panorama: `RunScriptInPanelContext`, `SetCompositionLayerTextureName`, `SetTopOfInputContext`, and `UpdateFocusInContext`.

Native binary research findings:

- `client.dll` contains `CS_UM_SayText2`, `CUserMessageSayText2_t`, and `CUserMessageSayText2` strings, but direct string xrefs found registration/type metadata rather than the runtime handler.
- `client.dll` contains `CCSGO_HudChat` xrefs around RVAs `0xc674e2`, `0xcefe96`, `0xd0511a`, `0xd4147f`, `0xdb9afa`, `0xdff917`, and `0xe07949` in the tested build.
- `CHudChatDelegate` has a SayText2 handler at client RVA `0x10c4150` in the tested build. The implementation resolves it by byte pattern instead of hard-coding the RVA.
- The handler reads the same fields we fill on the full `CNetMessagePB<CUserMessageSayText2>` object: `message_name` at object offset `0x48`, `param1` at `0x50`, `param2` at `0x58`, `param3` at `0x60`, `param4` at `0x68`, `chat` at `0x70`, and `entityindex` at `0x74`.
- The native handler calls chat formatting helpers at RVAs `0x10c0fe0` / `0x10c10f0`, which find `CCSGO_HudVoiceStatus` and then append through the real `CCSGO_HudChat` path.
- The first native user-message prototype used public SDK virtual-table assumptions for `INetworkMessages` and `IGameEventSystem`. That was not safe in-process for this CS2 build: probing the path in `inspect` and sending through it both crashed the game. The retained fix is to avoid protobuf ABI casts and use the current Windows gamedata slot numbers for `IGameEventSystem`.
- The `player_chat` event backend was not a safe path; insertion crashed CS2. The likely robust path remains the actual client handler for `CUserMessageSayText2` / `CCSGO_HudChat`.

Latest verified test:

```text
mirv_cmd addAtTick 300 mirv_chat_insert byXuid x76561197962023477 team=CT alive=1 visibility=all message handler_all_probe
mirv_cmd addAtTick 310 demo_pause
```

Result: CS2 rendered `[ALL] Benjamin Netanjahu: handler_all_probe` in the real closed chat feed. Team-only chat follows CS2's normal visibility filtering; when the camera was on a Terrorist player, a CT team message was not shown.

Latest multi-message test:

```text
mirv_cmd addAtTick 300 mirv_chat_insert byXuid x76561198723801816 visibility=all message gl hf
mirv_cmd addAtTick 304 mirv_chat_insert byXuid x76561198723801816 visibility=team location=T_Start message walk mid together
mirv_cmd addAtTick 308 mirv_chat_insert byXuid x76561198772930198 visibility=team location=T_Start message flashing ramp
mirv_cmd addAtTick 312 mirv_chat_insert byXuid x76561198721306201 visibility=all message banana is quiet
mirv_cmd addAtTick 316 mirv_chat_insert byXuid x76561197962023477 visibility=all message rotating now
mirv_cmd addAtTick 330 demo_pause
```

Result: all five lines rendered through CS2's native closed chat feed. The team messages used the T team-chat token with location (`T Start`) and the all-chat messages used the normal `[ALL]` token. Capture timing matters: when several rows are inserted within a short tick window, wait roughly 1.5-2.0 seconds after the final insertion before taking a still screenshot so the native chat row animation has completed.

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
