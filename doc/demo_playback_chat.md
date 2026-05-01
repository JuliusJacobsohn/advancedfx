# Demo Playback Synthetic Chat

Status: working native prototype. Visual Panorama overlays and direct `ChatHistoryText` mutation were rejected because they do not enter CS2's real closed-chat feed. The current implementation constructs a native `CUserMessageSayText2` message through Source 2's `NetworkMessagesVersion001`, serializes a normal SayText2 protobuf payload, parses it into the CS2-allocated protobuf object with a CS2-compatible protobuf-lite runtime, and calls CS2's native `CHudChatDelegate` SayText2 HUD handler. This renders into the real closed chat feed with CS2's own row styling and lifetime.

Desired feature: insert chat messages at a specific demo tick or timestamp with player name, location, team, alive/dead state, team/all visibility, and message text.

For the local test demo, use the known player names and XUIDs in [demo_playback_test_demo_roster.md](demo_playback_test_demo_roster.md).

## Command reference

The command inserts exactly one synthetic row into CS2's normal closed chat feed at the moment the command runs.

```text
mirv_chat_insert byXuid x<steamid64> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>
mirv_chat_insert byUserId <id> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>
mirv_chat_insert name <displayName> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>
mirv_chat_insert clear
mirv_chat_insert inspect
```

Targets:

- `byXuid x<steamid64>` targets the current demo player with that SteamID64/XUID. This is the preferred target for stable scripts.
- `byUserId <id>` targets the current demo player by demo user id. This is convenient for local test demos, but less portable across demos.
- `name <displayName>` uses a literal display name and does not resolve a player entity. Use explicit `team=` / `alive=` values with this form when team/dead/spec formatting matters.

Options:

- `message <text...>` is required and consumes every remaining argument. Put it last. Quotes are usually not needed because the parser joins all remaining tokens with spaces.
- `visibility=all` renders an all-chat row. This is the default.
- `visibility=team` renders a team-chat row. The row is still subject to CS2's normal viewer/team visibility filtering.
- `team=auto` infers `T`, `CT`, `spec`, or `none` from the current player controller for `byXuid` / `byUserId`. This is the default when the player can be resolved.
- `team=T`, `team=CT`, `team=spec`, or `team=none` overrides team inference.
- `alive=1` or `alive=0` overrides alive/dead inference. If omitted for `byXuid` / `byUserId`, the command checks the current player pawn health.
- `location=<string>` sets the team-chat location token parameter. Underscores are converted to spaces, so `location=A_Ramp` renders as `A Ramp`. Use `location=none` or omit it to avoid a location.

Utility commands:

- `mirv_chat_insert inspect` checks whether the HUD chat panel, `NetworkMessagesVersion001`, `SayText2`, and the native SayText2 handler can be found. It does not insert a message.
- `mirv_chat_insert clear` is intentionally disabled. The rejected fake `ChatHistoryText` path had something to clear; native CS2 chat rows are owned by the game HUD.

Direct examples:

```text
mirv_chat_insert byXuid x76561197962023477 visibility=all message rotating now
mirv_chat_insert byXuid x76561197962023477 visibility=team location=A_Ramp message rotate now
mirv_chat_insert byUserId 9 team=CT alive=1 visibility=team location=A_Ramp message rotate now
mirv_chat_insert name caster team=spec alive=1 visibility=all message bought two smokes
mirv_chat_insert inspect
```

## Scheduling examples

Scheduling is handled by the existing `mirv_cmd` system. `mirv_chat_insert` should be the command payload that `mirv_cmd` executes.

Message at demo tick:

```text
mirv_cmd addAtTick 12345 mirv_chat_insert byXuid x76561197962023477 team=CT alive=1 visibility=team location=A_Ramp message rotate now
```

Message at demo time:

```text
mirv_cmd addAtTime 42.5 mirv_chat_insert name caster team=spec visibility=all message bought two smokes
```

Pause shortly after a message for visual inspection:

```text
mirv_cmd addAtTick 300 mirv_chat_insert byXuid x76561197962023477 visibility=all message handler_all_probe
mirv_cmd addAtTick 330 demo_pause
```

Quit shortly after a log-only probe:

```text
mirv_cmd addAtTick 300 mirv_chat_insert byXuid x76561197962023477 visibility=all message handler_all_probe
mirv_cmd addAtTick 960 quit
```

Script a short conversation:

```text
mirv_cmd addAtTick 300 mirv_chat_insert byXuid x76561198723801816 visibility=all message gl hf
mirv_cmd addAtTick 304 mirv_chat_insert byXuid x76561198723801816 visibility=team location=T_Start message walk mid together
mirv_cmd addAtTick 308 mirv_chat_insert byXuid x76561198772930198 visibility=team location=T_Start message flashing ramp
mirv_cmd addAtTick 312 mirv_chat_insert byXuid x76561198721306201 visibility=all message banana is quiet
mirv_cmd addAtTick 316 mirv_chat_insert byXuid x76561197962023477 visibility=all message rotating now
mirv_cmd addAtTick 960 quit
```

The `message` token deliberately consumes all remaining command arguments. This makes scheduled commands more reliable, because `mirv_cmd addAtTick` / `mirv_cmd addAtTime` store and replay the remaining command text as one command string.

`mirv_cmd addAtTick` is relative to demo playback, not CS2 process launch. In the local test demo, tick `300` is early in the first round and maps to a `CGameRules` log tick around `3400`. Chat rows animate in, so screenshot automation should wait roughly 1.5-2.0 seconds after the last inserted row before capturing.

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
- HLAE's existing protobuf v31.1 dependency must not be used directly against CS2-allocated protobuf objects. A previous attempt cast the engine-allocated `CNetMessagePB<CUserMessageSayText2>` to HLAE's v31.1 protobuf ABI and crashed.
- Feeding normal protobuf wire bytes to CS2's `SayText2` `INetworkMessageInternal` parser failed without crashing (`bitsRead=72`), and feeding the legacy Source user-message stream also failed without crashing (`bitsRead=88`). The current path instead calls `google::protobuf::MessageLite::ParseFromArray` on the CS2-allocated protobuf subobject with an isolated protobuf 3.21.8 lite runtime.
- A local generated protobuf layout check under `research/proto-gen/` matches CS2's observed allocation size: `CNetMessagePB<CUserMessageSayText2>` is `0x78`, with a `0x30` `CNetMessage` base and a `0x48` `CUserMessageSayText2` protobuf subobject. This is retained as a diagnostic clue only; the current implementation no longer writes those fields by hard-coded offsets.

Implementation direction:

- Construct and dispatch something equivalent to `CUserMessageSayText2`.
- The current prototype serializes fields `entityindex`, `chat`, `messagename`, `param1`, `param2`, `param3`, and `param4` as protobuf wire data, then parses that payload into the real protobuf object returned by `CNetMessage::AsProto()`.
- The result must be rendered by the real CS2 chat component, using the same position, row template, lifetime, font, colors, and formatting as demo chat.
- A Panorama implementation is acceptable only if it calls a native method on `CSGOHudChat`. Creating separate HUD labels/panels, or writing `ChatHistoryText` directly, is the rejected overlay/popup approach.
- For replacement workflows, hide recorded chat with CS2/HLAE settings such as `tv_nochat true` where applicable, then schedule synthetic native chat rows.

Current implementation behavior:

- `mirv_chat_insert` resolves the player name and controller entity index for `byXuid` / `byUserId`.
- For `byXuid` / `byUserId`, omitted `team` and `alive` values are inferred from the current player controller / pawn when possible. Use explicit `team=` / `alive=` to override the inferred state for staged conversations.
- It selects `Cstrike_Chat_All`, `Cstrike_Chat_AllDead`, `Cstrike_Chat_AllSpec`, `Cstrike_Chat_CT`, `Cstrike_Chat_T`, `Cstrike_Chat_CT_Loc`, `Cstrike_Chat_T_Loc`, `Cstrike_Chat_CT_Dead`, `Cstrike_Chat_T_Dead`, or `Cstrike_Chat_Spec` from `visibility`, `team`, `alive`, and `location`.
- It allocates native message id `118` / partial name `SayText2`, builds a normal protobuf SayText2 payload, and parses that payload into the CS2-allocated `CUserMessageSayText2` object through `MessageLite::ParseFromArray`.
- The rejected parser-based probes were different from the retained implementation: raw protobuf wire data and the legacy byte/string `SayText2` stream both failed when fed to CS2's `INetworkMessageInternal` parser.
- It calls CS2's native `CHudChatDelegate` SayText2 handler directly. This is the first path that rendered a visible synthetic row in the real closed chat feed.
- Old event-system dispatch experiments are intentionally not exposed as command options: broadcast posted but was invisible, while local/filter paths crashed during testing.
- The client-side `player_chat` event probe is disabled after an insertion crash report; it is not the current path.
- `mirv_chat_insert inspect` prints the located chat panel, exposed Panorama properties, native interface pointers, the current `CGameEventManager` pointer, and the resolved `SayText2` message type. It deliberately does not allocate or dispatch `CUserMessageSayText2`.
- Recent inspect result found `CSGOHudChat` and `ChatHistoryText`, but only generic methods were exposed from Panorama: `RunScriptInPanelContext`, `SetCompositionLayerTextureName`, `SetTopOfInputContext`, and `UpdateFocusInContext`.

Safer implementation evaluation:

- The current path is better than the earlier working prototype because it avoids the two most dangerous assumptions from that prototype: hard-coded protobuf field offsets and borrowed `std::string` storage crossing the CS2 / HLAE module boundary.
- The retained path still allocates the real CS2 `SayText2` message through `INetworkMessageInternal::AllocateMessage()` and still lets CS2 deallocate it. The only mutation step is a normal protobuf parse into the CS2-owned object.
- CounterStrikeSharp's CS2 user-message wrapper shows the useful allocation pattern used by an active open source CS2 project: allocate through `INetworkMessageInternal::AllocateMessage()`, cast the returned object to `CNetMessagePB<google::protobuf::Message>`, then mutate fields through protobuf APIs.
- The matching public Source 2 SDK layout confirms the same allocation rule: do not construct `CNetMessagePB` directly; allocate through the engine network-message interface, then treat the result as a `CNetMessagePB<ProtoClass>`.
- Full protobuf reflection was tested with an isolated upstream protobuf 3.21.8 runtime. It rendered chat, but crashed during CS2 cleanup immediately after `mirv_chat_insert: deallocating SayText2 message.` Reflection likely installs or touches ownership metadata in a way that does not match CS2's exact protobuf runtime.
- `MessageLite::ParseFromArray` with the same isolated protobuf 3.21.8 lite runtime has been more stable in testing: it rendered multiple all/team chat rows and CS2 deallocated the messages without crashing.
- This is production-ready enough for the current feature branch, but not maintenance-free. The remaining version-sensitive pieces are the `CHudChatDelegate` SayText2 handler byte pattern and the assumption that CS2's `CUserMessageSayText2` wire schema remains compatible with protobuf 3.21-style generated layout.
- The handler lookup remains a separate risk. The current implementation still finds and calls the native `CHudChatDelegate` SayText2 handler by byte pattern. The most robust final route would either locate that handler through engine registration / user-message dispatch metadata or find a stable `CCSGO_HudChat` append entry point.

Native binary research findings:

- `client.dll` contains `CS_UM_SayText2`, `CUserMessageSayText2_t`, and `CUserMessageSayText2` strings, but direct string xrefs found registration/type metadata rather than the runtime handler.
- `client.dll` contains `CCSGO_HudChat` xrefs around RVAs `0xc674e2`, `0xcefe96`, `0xd0511a`, `0xd4147f`, `0xdb9afa`, `0xdff917`, and `0xe07949` in the tested build.
- `CHudChatDelegate` has a SayText2 handler at client RVA `0x10c4150` in the tested build. The implementation resolves it by byte pattern instead of hard-coding the RVA.
- The handler reads the same fields we populate on the full `CNetMessagePB<CUserMessageSayText2>` object: `message_name` at object offset `0x48`, `param1` at `0x50`, `param2` at `0x58`, `param3` at `0x60`, `param4` at `0x68`, `chat` at `0x70`, and `entityindex` at `0x74`.
- The native handler calls chat formatting helpers at RVAs `0x10c0fe0` / `0x10c10f0`, which find `CCSGO_HudVoiceStatus` and then append through the real `CCSGO_HudChat` path.
- The first native user-message prototype used public SDK virtual-table assumptions for `INetworkMessages` and `IGameEventSystem`. That was not safe in-process for this CS2 build: probing the path in `inspect` and sending through it both crashed the game. The retained fix is to avoid protobuf ABI casts and use the current Windows gamedata slot numbers for `IGameEventSystem`.
- The `player_chat` event backend was not a safe path; insertion crashed CS2. The likely robust path remains the actual client handler for `CUserMessageSayText2` / `CCSGO_HudChat`.

Handler byte-pattern repair guide:

- First run `mirv_chat_insert inspect`. If `networkMessages` and `sayText2` resolve but `handler=0000000000000000`, the protobuf allocation path is still intact and only the native HUD handler signature likely broke.
- The current signature is in `ChatNative::getSayText2Handler()` in `AfxHookSource2/DeathMsg.cpp`. It scans `client.dll` for the SayText2 handler function prologue and nearby field reads:

```text
48 89 4C 24 08 55 41 56 48 8D AC 24 ?? ?? ?? ??
48 81 EC ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 4C 8B F2
48 8B 01 FF 90 50 01 00 00 84 C0 74 ?? E8 ?? ?? ?? ??
80 78 72 00 0F 85 ?? ?? ?? ?? 41 8B 46 74 48 89 B4 24 ?? ?? ?? ??
```

- The string search trail that led to this handler was: `CUserMessageSayText2`, `CUserMessageSayText2_t`, `CS_UM_SayText2`, `CCSGO_HudChat`, and `Cstrike_Chat_*`. The `CUserMessageSayText2` strings mostly lead to registration/type metadata; the useful runtime path is around the `CCSGO_HudChat` / `CHudChatDelegate` code that formats and appends rows.
- In a disassembler, validate any replacement candidate before changing the pattern. The correct function takes the chat delegate/context in `rcx` and the native `CNetMessage*` in `rdx`; early in the function it preserves `rdx` in `r14`, calls `CNetMessage::AsProto()` / equivalent, reads the SayText2 string fields, checks the chat boolean, reads entity index, and then calls the helper that ultimately appends to `CCSGO_HudChat`.
- Useful validation anchors from the tested build: the handler start was RVA `0x10c4150`, the nearby formatting helpers were around RVAs `0x10c0fe0` and `0x10c10f0`, and the object reads matched `CNetMessagePB<CUserMessageSayText2>` offsets `0x48` through `0x74`.
- Do not patch by hard-coding those RVAs. They are only orientation markers for the tested client build. Use them to recognize the same code shape after a Valve update, then update the byte pattern with wildcards around calls, RIP-relative globals, stack frame sizes, and conditional jump displacements.
- After updating the signature, test with an early scheduled all/team conversation and a delayed `quit`, then verify the console has `filled SayText2 protobuf object via MessageLite parse`, `native SayText2 handler returned`, and `deallocated SayText2 message` for every inserted row.

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
mirv_cmd addAtTick 960 quit
```

Result: all five lines rendered through CS2's native closed chat feed, then CS2 closed through the scheduled `quit` command instead of crashing. The team messages used the T team-chat token with location (`T Start`) and the all-chat messages used the normal `[ALL]` token. Capture timing matters: when several rows are inserted within a short tick window, wait roughly 1.5-2.0 seconds after the final insertion before taking a still screenshot so the native chat row animation has completed. The verification capture is `research/chat-lite-parse-conversation.png`.

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
