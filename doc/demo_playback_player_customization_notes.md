# Demo Playback Player Customization Notes

This note summarizes how player name replacement currently works during demo playback and what would be involved in adding demo-only overrides for profile pictures, ranks, and player/weapon skins.

## Scope / goal

The target use case is fragmovie production from demos. The desired behavior is purely visual: anonymize players or exaggerate matches by changing how names, ranks, avatars, skins, and chat render during local demo playback and recording.

There is no need to change the demo file, server truth, Steam inventory ownership, matchmaking state, or live gameplay state. If the rendered output looks correct in the captured movie, the implementation is good enough. This favors local HUD/Panorama hooks, client-side visual field overrides, and scheduled visual inserts over deeper network, GC, or inventory mutation.

## Current name replacement

The current CS2 name replacement is a runtime client hook. It does not edit the `.dem` file and it does not rewrite demo packets. HLAE is injected into the CS2 process and changes what the client returns when UI/HUD code asks for player names.

Relevant files:

- `AfxHookSource2/ReplaceName.cpp`
- `AfxHookSource2/ClientEntitySystem.cpp`
- `AfxHookSource2/DeathMsg.cpp`
- `AfxHookSource2/SchemaSystem.cpp`

`mirv_replace_name` is implemented in `AfxHookSource2/ReplaceName.cpp:145`. It stores replacement strings in four maps:

- controller entry index -> plain name
- XUID / SteamID64 -> plain name
- controller entry index -> decorated name
- XUID / SteamID64 -> decorated name

The command's `byUserId` mode adds `1` to the supplied userid before using it as the map key. This matches the convention used elsewhere in the CS2 code: the player controller entity index is treated as `userid + 1`.

The actual replacement happens in two detours:

- `New_CCSPlayerController_GetPlayerName` at `AfxHookSource2/ReplaceName.cpp:29`
- `New_GetDecoratedPlayerName` at `AfxHookSource2/ReplaceName.cpp:61`

`HookReplaceName` at `AfxHookSource2/ReplaceName.cpp:90` installs those detours. One target is found by a signature for `GetDecoratedPlayerName`; the plain player name function is taken from the `CCSPlayerController` vtable at index `226`. The hook is installed when `client.dll` is loaded in `AfxHookSource2/main.cpp:2155`.

XUID matching uses `CEntityInstance::GetSteamId` from `AfxHookSource2/ClientEntitySystem.cpp:182`, which reads `CBasePlayerController::m_steamID`. The offset is resolved through the Source 2 schema system in `AfxHookSource2/SchemaSystem.cpp:101`.

## Player discovery helpers

`mirv_listentities` in `AfxHookSource2/ClientEntitySystem.cpp:440` enumerates the client entity list and can filter player-related entities. It is useful for handles and entity indices, but it does not print the richer player identifiers.

For player identity, `mirv_deathmsg help players` is more relevant. It calls `deathMsgPlayers_PrintHelp_Console` at `AfxHookSource2/DeathMsg.cpp:1494`, which enumerates player controllers and prints:

- current name
- userid
- XUID
- spectator key

The data comes from `getPlayerInfoFromControllerIndex` at `AfxHookSource2/DeathMsg.cpp:199`. That function reads controller team, name, and XUID from schema-resolved fields and derives spectator key order from team slots.

## Death notice name handling

`mirv_deathmsg` is separate from general name replacement. It wraps the `player_death` game event in `handleDeathnotice` at `AfxHookSource2/DeathMsg.cpp:924`, applies filters, and passes the wrapped event to the original death notice handler. The hook is installed by `HookDeathMsg` at `AfxHookSource2/DeathMsg.cpp:1460` from `AfxHookSource2/main.cpp:2153`.

For death notices, name replacement can also patch `CCSPlayerController::m_sSanitizedPlayerName` just before the original handler builds the notice:

- attacker: `AfxHookSource2/DeathMsg.cpp:1067`
- victim: `AfxHookSource2/DeathMsg.cpp:1071`
- assister: `AfxHookSource2/DeathMsg.cpp:1075`

That is narrower than `mirv_replace_name`: it targets death-notice construction, while `mirv_replace_name` targets generic name getters.

The older CS:GO path used a similar idea but different hook points. See `AfxHookSource/csgo_CHudDeathNotice.cpp:824` for `CPlayerResource::GetPlayerName`, `AfxHookSource/csgo_CHudDeathNotice.cpp:890` for the decorated/wide-string name path, and `AfxHookSource/csgo_CHudDeathNotice.cpp:1388` for the old console command.

## What "demo playback only" means here

The existing features are already playback-side runtime overrides. They affect what the local client shows while the demo is being viewed. They do not change server truth, matchmaking state, or the demo file.

For new features, a clean implementation should explicitly guard behavior behind demo playback. The repo already exposes demo state in `AfxHookSource2/AfxHookSource2Rs.cpp` (`afx_hook_source2_is_playing_demo`) and also has existing client demo-time plumbing. A new command should avoid applying these overrides in live/offline server sessions unless intentionally allowed.

## Profile pictures

Difficulty: high unless the first version is restricted to local image overrides on specific Panorama panels.

The repo does not currently have a Steam Friends / avatar replacement implementation. The only visible Steam API hook in the CS2 path is `New_SteamInternal_FindOrCreateUserInterface` in `AfxHookSource2/main.cpp:1791`, and that is only used to disable Steam Remote Storage when requested.

Likely implementation routes:

1. Panorama/UI override
   - Find which Panorama panels or image attributes display scoreboard/player-card/avatar images.
   - Reuse the existing Panorama machinery in `AfxHookSource2/DeathMsg.cpp`, such as layout load hooks and panel attribute access.
   - Command shape could be explicit: `mirv_replace_avatar byXuid x... file://...` or a packaged resource path.
   - This avoids Steam API image ownership but is UI-surface specific. Scoreboard, kill cards, end-of-match, and other panels may need separate handling.

2. Steam avatar API override
   - Hook `ISteamFriends` avatar-related calls and possibly `ISteamUtils::GetImageRGBA`.
   - Map SteamID64 to replacement image handles or replacement RGBA data.
   - Handle async avatar availability and image caching.
   - This would be broader but much more invasive. The repo does not currently contain the needed Steam Friends wrappers.

Automatic retrieval "through Steam ID" is not enough for replacement by itself. SteamID64 is available from `m_steamID`, but displaying a custom profile picture requires either feeding Panorama a different image path or making Steam avatar image requests resolve differently.

Changing the player SteamID itself might be enough for avatars if the CS2 UI resolves avatar images dynamically from `CBasePlayerController::m_steamID` or a getter that reads that field. That is worth testing because the repo already reads `m_steamID` and the game clearly has Steam avatar plumbing elsewhere.

However, changing the global controller SteamID is a broad hammer:

- It may affect `mirv_replace_name byXuid`, `mirv_deathmsg` XUID matching, local-player highlighting, player listings, and any rank/loadout/avatar lookups using the same ID.
- It may not refresh already-cached avatar images unless the relevant UI panel is recreated or the avatar cache is invalidated.
- It may have side effects if other client systems treat the SteamID as identity rather than display data.

For the fragmovie-only goal, a better first milestone is probably an avatar-specific visual override: either hook the avatar lookup result for a player, or set the avatar image path/texture on the relevant Panorama panels. A SteamID-swap command can still be a useful experiment, but it should be scoped as visual-only and reversible.

## Premier / competitive / wingman ranks

Difficulty: medium-high if the rank is already present in client memory/UI; high if it requires GC or matchmaking data.

There is no current rank replacement code in the repo. Searches found no existing CS2 offsets for competitive rank, wingman rank, Premier rating, or rank image fields. The schema resolver can be extended, but the exact data source still has to be found.

Possible implementation routes:

1. UI-layer rank override
   - Find Panorama panels/images/text that render rank or Premier rating.
   - Override label text or rank image attributes when those panels are created or updated.
   - This is probably the most practical for demo playback and movie-making because it only needs the final visual result.

2. Client data/function override
   - Find the client function or data structure that scoreboard/end-of-match UI queries for ranks.
   - Detour the getter or patch the schema field before UI reads it.
   - More general than UI patching, but requires reverse engineering the current CS2 client.

3. Network/demo message rewrite
   - Intercept or rewrite the message that carries ranking data, if it is serialized into the demo at all.
   - The repo does not currently have a CS2 demo/protobuf rewriting layer for this.

Automatic lookup by Steam ID is probably not reliable. Public Steam profile data does not generally expose current CS2 competitive, wingman, or Premier rank in a simple public API. A practical command should accept explicit values, e.g. "show this XUID as Premier 18750" or "show this XUID as Wingman rank N".

### Scoreboard-only rank override implementation

Branch `codex/demo-rank-scoreboard-override` starts with the easiest useful version: a visual scoreboard override, not a client-data or demo-message override.

The current CS2 scoreboard Panorama files in SteamTracking's `GameTracking-CS2` show that scoreboard player rows are created as `player-<xuid>` in `panorama/scripts/scoreboard.js`, and rank rendering is delegated to `RatingEmblem.SetXuid(...)` on a `jsRatingEmblem` frame. Premier, Competitive, and Wingman display are all handled by `panorama/scripts/rating_emblem.js`.

Implemented command shape:

```text
mirv_scoreboard_rank byXuid add x<steamid64> premier <rating> [wins <iWins>]
mirv_scoreboard_rank byXuid add x<steamid64> competitive <rank 1-18> [wins <iWins>]
mirv_scoreboard_rank byXuid add x<steamid64> wingman <rank 1-18> [wins <iWins>]
mirv_scoreboard_rank byXuid remove x<steamid64>
mirv_scoreboard_rank clear
mirv_scoreboard_rank print
mirv_scoreboard_rank apply
```

Internally this adds a small `CUIEngine::RunScript` bridge and executes a generated Panorama script in the scoreboard context. The script finds `player-<xuid>`, finds its `jsRatingEmblem`, then calls Valve's own `RatingEmblem.SetXuid` with the replacement rating type, score/rank, and wins. It also registers a small `Scoreboard_UpdateJob` re-apply handler because CS2 refreshes scoreboard rows while the board is open. This is intentionally visual-only and scoreboard-scoped.

The command applies immediately when an override is added. If CS2 rebuilds the scoreboard rows later, `mirv_scoreboard_rank apply` re-applies all stored overrides. For scripted recording, this can also be scheduled with `mirv_cmd` around the tick/time where the scoreboard is opened.

External reference used for current scoreboard structure:

- <https://github.com/SteamTracking/GameTracking-CS2>

## Used player skins / weapon skins

Difficulty: medium-high for basic weapon paint-kit overrides after offsets are known; high for a polished, broad skin system.

The repo already has the start of entity access needed for this:

- active weapon handle: `CEntityInstance::GetActiveWeaponHandle` at `AfxHookSource2/ClientEntitySystem.cpp:170`
- schema offset resolver: `AfxHookSource2/SchemaSystem.cpp:10` and `AfxHookSource2/SchemaSystem.cpp:84`
- active weapon service offset: `AfxHookSource2/SchemaSystem.cpp:108`

But it does not currently resolve or write econ item fields such as paint kit, seed, wear, item definition index, owner XUID, gloves, stickers, or agent model data.

Open-source server-side CS2 skin plugins are useful references for the shape of the problem, even though their server-plugin architecture does not directly fit HLAE injection. For example:

- <https://github.com/Nereziel/cs2-WeaponPaints>
- <https://github.com/Dyshay/CS2Skin>
- <https://github.com/ianlucas/cs2-inventory-simulator-plugin>
- <https://github.com/singhhdev/CS2-Internal-SkinChanger-Inventory-Changer>
- <https://github.com/a2x/cs2-dumper>

Those projects commonly modify `CEconItemView` / weapon econ state: item definition index, entity quality, account ID, item ID high/low, fallback paint kit, fallback seed, fallback wear, custom name, networked dynamic attributes, stickers, and glove/agent state. They also explicitly refresh or recreate weapons in some paths. That supports the earlier assessment: the hard part is not just locating the fields, but making CS2 rebuild the visual model/material state at the right time.

More concrete fields and systems to investigate for a demo-playback HLAE implementation:

- `C_EconEntity::m_nFallbackPaintKit`
- `C_EconEntity::m_nFallbackSeed`
- `C_EconEntity::m_flFallbackWear`
- `C_EconEntity::m_nFallbackStatTrak`
- `C_EconItemView::m_iItemDefinitionIndex`
- `C_EconItemView::m_iItemID`, `m_iItemIDHigh`, `m_iItemIDLow`
- `C_EconItemView::m_iAccountID`
- `C_EconItemView::m_iEntityQuality`
- `C_EconItemView::m_AttributeList`
- `C_EconItemView::m_NetworkedDynamicAttributes`
- `C_AttributeContainer::m_Item`
- `CPlayer_WeaponServices::m_hMyWeapons`
- `CPlayer_WeaponServices::m_hActiveWeapon`
- `C_CSPlayerPawn::m_EconGloves`

The HLAE repo already resolves `m_hActiveWeapon`, but not `m_hMyWeapons` or the econ fields above.

Server plugins often use named econ attributes through a native equivalent of `CAttributeList::SetOrAddAttributeValueByName`. Important attribute names seen in open implementations:

- paint kit: `set item texture prefab`
- seed: `set item texture seed`
- wear: `set item texture wear`
- StatTrak: `kill eater`, `kill eater score type`
- stickers: `sticker slot N id`, `sticker slot N schema`, `sticker slot N wear`, `sticker slot N offset x`, `sticker slot N offset y`, `sticker slot N scale`, `sticker slot N rotation`
- keychains: `keychain slot N id`, `keychain slot N seed`, `keychain slot N sticker`, `keychain slot N offset x`, `keychain slot N offset y`, `keychain slot N offset z`

Refresh patterns seen in those projects:

- apply after `GiveNamedItem`, item pickup, spawn, and inventory/loadout updates
- update item IDs so the client treats modified econ views as fresh items
- clear and repopulate dynamic attributes
- force weapon/world/view model refresh by regiving or killing/recreating weapons on servers
- toggle bodygroups / mesh group masks for legacy versus current models
- use small wear changes as a material/sticker refresh workaround

For HLAE, the useful subset is visual-only local modification during demo playback: apply overrides when demo entities appear or change, keep viewmodel/worldmodel in sync, avoid ownership/inventory spoofing, and avoid live-session network mutation. The risky or unrelated pieces are external memory write loops, VAC/GSLT bypass logic, inventory/SO cache item creation, `EquipItemInLoadout` ownership manipulation, StatTrak syncing, and anything intended for live matchmaking.

Likely implementation shape:

- Extend `ClientDllOffsets_t` with relevant econ/weapon/player appearance schema fields.
- Enumerate weapon entities owned by a target player, or hook an update/render point where the weapon entity is known.
- Add a wrapper for the native named-attribute setter if direct fallback fields are insufficient.
- Apply overrides repeatedly or at the right lifecycle point, because demo network updates may overwrite client fields.
- Force or trigger whatever visual refresh CS2 needs after changing econ item data. In many Source/CS2 skin-change approaches, setting fields alone is not enough once materials/models have already been cached.

For "used skins" as in weapon paint kits used by players, this is feasible but requires ongoing offset/schema maintenance. For player agent models, gloves, stickers, StatTrak, and nametags, the scope grows quickly because different visual systems may need different refresh paths.

## Suggested command architecture

Do not put this directly into `mirv_replace_name`. Name replacement is narrow and currently works by hooking name getters. A better approach is a new module, for example `DemoPlayerOverrides`, with a command namespace such as:

```text
mirv_demo_player profileImage byXuid x<steamid64> <path>|default
mirv_demo_player rank byXuid x<steamid64> premier <rating>|default
mirv_demo_player rank byXuid x<steamid64> competitive <rank>|default
mirv_demo_player rank byXuid x<steamid64> wingman <rank>|default
mirv_demo_player skin byXuid x<steamid64> weapon <weapon-or-all> paintKit=<id> wear=<float> seed=<id>
```

Implementation pieces to reuse:

- ID parsing conventions from `DeathMsgId` in `shared/MirvDeathMsgFilter.h`
- XUID lookup from `CEntityInstance::GetSteamId`
- player listing from `deathMsgPlayers_PrintHelp_Console`
- schema offset resolution from `SchemaSystem`
- Panorama panel/style machinery from `DeathMsg.cpp`

## Synthetic chat messages

Desired feature: add a demo-playback-only command that inserts chat messages at a specific demo tick or timestamp, with fields such as player name, location, team, alive/dead state, team/all-chat visibility, and message text.

Difficulty: medium if implemented as a purely visual Panorama chat-row overlay; high if implemented as a real synthetic CS2 chat user message.

Current repo state:

- There is no existing CS2 chat insertion/replacement implementation.
- `resources/AfxHookSource2_changelog.xml:353` explicitly notes that improved `mirv_replace_name` does not replace chat messages and suggests `tv_nochat true`.
- `AfxHookSource2/GameEvents.cpp` hooks `CGameEventManager::FireEvent` and `FireEventClientSide`, but chat is not currently exposed there.
- `AfxHookSource2/main.cpp:1415` exposes `mirv_cmd`, and the shared command system already schedules arbitrary commands by tick or time.

The scheduling piece is already mostly solved. `mirv_cmd addAtTick <tick> ...` and `mirv_cmd addAtTime <time> ...` are handled by `shared/CommandSystem.cpp`; execution happens from `g_CommandSystem.OnExecuteCommands()` during `FRAME_RENDER_PASS` in `AfxHookSource2/main.cpp`. A future chat command could simply be scheduled through `mirv_cmd`, for example:

```text
mirv_cmd addAtTick 12345 mirv_chat_insert byXuid x765... team=CT alive=1 visibility=team location="A Ramp" message="rotate"
```

Likely data model:

```text
mirv_chat_insert
  byUserId <id> | byXuid x<steamid64> | name <displayName>
  team=T|CT|spec|none
  alive=0|1
  visibility=all|team
  location=<string>|none
  message=<string>
```

For chat formatting, Source/CS chat uses localized message tokens such as `Cstrike_Chat_All`, `Cstrike_Chat_AllDead`, `Cstrike_Chat_CT_Loc`, and `Cstrike_Chat_T_Loc`. External demo/parser references describe `SayText2` as raw chat network data with `MsgName`, `Params`, `IsChat`, and `IsChatAll`; for `Cstrike_Chat_All`, params contain player and message. See:

- <https://pkg.go.dev/github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/events#SayText2>
- <https://pkg.go.dev/github.com/astephensen/csgodemogo/cstrikeproto#CCSUsrMsg_SayText2>
- <https://github.com/SteamDatabase/Protobufs/blob/master/csgo/cstrike15_usermessages.proto>

Implementation routes:

1. **Panorama-only visual insertion**
   - Find the HUD chat panel and append rows directly, probably using the Panorama panel helpers already started in `AfxHookSource2/DeathMsg.cpp`.
   - Advantages: easiest to make demo-only, can accept arbitrary explicit name/location/team/alive/message fields, does not need real user-message construction.
   - Disadvantages: must reproduce CS2 chat styling, fading, scroll behavior, team color/name formatting, and any chat filters manually. It may only affect one UI surface.

2. **Synthetic user-message injection**
   - Reverse engineer the CS2 user-message dispatch path, construct something equivalent to `CCSUsrMsg_SayText2`, and feed it to the same HUD handler used by demo playback.
   - Advantages: closer to real chat behavior, likely gets native formatting and lifetime behavior.
   - Disadvantages: the repo currently has game-event hooks but not user-message creation/dispatch hooks. CS2 user-message IDs in current protobufs use Source 2 values such as `CS_UM_SayText2 = 306`; older CS:GO examples often use legacy ID `6`, so the implementation must resolve by name or current enum, not hard-code old IDs.

3. **Demo-file rewrite**
   - Insert user-message packets into the demo stream itself.
   - Advantages: most faithful if done correctly.
   - Disadvantages: largest scope. This repo does not currently have a CS2 `.dem` protobuf rewrite pipeline. It would also be harder to iterate interactively from console commands.

The requested fields map naturally to chat tokens:

- `visibility=all`, `alive=1` -> likely `Cstrike_Chat_All`
- `visibility=all`, `alive=0` -> likely `Cstrike_Chat_AllDead`
- `visibility=team`, `team=CT`, `alive=1`, `location` set -> likely `Cstrike_Chat_CT_Loc`
- `visibility=team`, `team=T`, `alive=1`, `location` set -> likely `Cstrike_Chat_T_Loc`
- `visibility=team`, dead -> likely `Cstrike_Chat_CT_Dead` or `Cstrike_Chat_T_Dead`
- spectator -> likely `Cstrike_Chat_Spec` / `Cstrike_Chat_AllSpec`

For a first implementation, the pragmatic route is a visual Panorama insertion command plus `mirv_cmd` scheduling. A more native second milestone would be to hook user-message dispatch and synthesize `SayText2`.

## Effort summary

Name replacement is easy because the game has stable-ish name getter functions and the repo already hooks them.

Profile pictures are medium if handled as per-panel visual avatar image overrides and high if handled as a general Steam-avatar pipeline. Swapping a player's SteamID may be a useful experiment, but it is broader than needed and may require cache/panel refresh work.

Ranks are medium if treated as visual UI overrides with explicit user-provided values. They are high effort if the goal is to discover or retrieve real ranks automatically from SteamID64.

Weapon skins are medium-high for basic paint-kit overrides and high for a complete system. The repo has entity and schema infrastructure, but not the econ-item field offsets or refresh hooks needed to make skin changes reliable during demo playback.

Synthetic chat messages are medium for a Panorama-only visual insert scheduled through `mirv_cmd`, and high for a native `SayText2` user-message injection or demo-file rewrite.
