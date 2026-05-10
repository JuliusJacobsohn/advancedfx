# Demo Playback Avatars

Status: implemented SteamID-source avatar override prototype.

The feature visually replaces a demo player's avatar with the avatar resolved for another SteamID64. It does not accept arbitrary image URLs or local image paths, does not edit the demo, and does not globally change the target player's identity.

Current command:

```text
mirv_avatar byXuid add x<targetSteamID64> steamid x<avatarSteamID64>
mirv_avatar byXuid remove x<targetSteamID64>
mirv_avatar clear
mirv_avatar print
mirv_avatar apply
mirv_avatar inspect
```

Example:

```text
mirv_avatar byXuid add x76561198723801816 steamid x76561197960680616
```

This keeps player `x76561198723801816` as the same demo player, but asks CS2 avatar UI to render the image source for `x76561197960680616`.

What works:

- Top team counter avatars.
- Scoreboard row avatars.
- Bottom spectator player avatar.
- Replacement avatars can come from Steam accounts that did not participate in the demo.
- Replacements can be scheduled with `mirv_cmd addAtTick`.

What this intentionally does not do:

- No overlay panels.
- No hidden native panels with a replacement panel drawn above them.
- No arbitrary image URL or local image path input.
- No demo packet mutation.
- No global SteamID swap on player controllers.
- No Steam Friends API avatar cache replacement.

## Findings

Fresh CS2 Panorama assets were extracted under the ignored research folder and inspected during implementation.

Scoreboard:

- `panorama/scripts/scoreboard.js` updates the `avatar` stat by calling `PopulateFromPlayerSlot(GameStateAPI.GetPlayerSlot(oPlayer.m_xuid))`.
- `panorama/layout/scoreboard.xml` builds repeated `player-<xuid>` rows with a child `CSGOAvatarImage id="image"`.
- Duplicate `image` IDs in repeated rows mean direct `FindChildTraverse("image")` can find the wrong panel. The implementation searches recursively from the row instead.

Top team counter:

- `panorama/layout/hud/hudteamcounter.xml` uses top-level panels `Avatar0..Avatar4` for CT and `Avatar10..Avatar14` for T.
- The actual image is a `CSGOAvatarImage` child named `AvatarImage`.

Bottom spectator bar:

- `panorama/layout/hud/hudhealthammocenter.xml` contains `Panel id="HudSpecplayer__Avatar"`.
- Inside it are `Image id="PlayerAvatarDefault"` and `CSGOAvatarImage id="PlayerAvatar"`.
- The visible bottom avatar is not owned by a Panorama JS script. It is updated by native client code.
- The native bottom path finds `PlayerAvatar` and calls a dedicated native helper around the current tested `client.dll` address `0x180d0c490`.
- That helper can set the avatar through both a direct image-source path and an avatar-handle path.

The bottom-bar flicker was caused by original native source writes racing against replacement source writes on the same real `CSGOAvatarImage` panel. The fix is source-level: track the real panel pointer and swap later source writes for tracked panels back to the configured replacement SteamID.

## Implementation

The implementation lives in `AfxHookSource2/DeathMsg.cpp` for now because the existing CS2 UI/death-message hook plumbing is there. This should be split into a dedicated avatar module if the feature is prepared for a cleaner upstream PR.

State:

- `g_AvatarOverrides` maps target demo XUID to replacement SteamID64.
- `g_AvatarPanelReplacementByPanel` maps live `CSGOAvatarImage` panel pointers to the replacement SteamID64 that should be forced for that panel.

Native hooks:

- `CSGOAvatarImage::PopulateFromPlayerSlot` is hooked for Panorama-exposed avatar population.
- A native player-slot avatar helper is hooked for the bottom spectator bar path.
- The native avatar-handle path is hooked because CS2 can update avatars from avatar handles instead of directly from SteamID strings.
- The `CSGOAvatarImage` image-source setter is detoured lazily from the live panel vtable slot used by the avatar class.

Native replacement flow:

1. Resolve the player slot to the real player object.
2. Resolve the player XUID.
3. Check whether `mirv_avatar` has an override for that XUID.
4. If it does, tag the real avatar panel pointer with the replacement SteamID64 before setting the source.
5. Create the replacement avatar image source using CS2's own SteamID image-source helper.
6. Call the real image-source setter with the replacement source.
7. If later native code tries to write the original source to the same tracked panel, the source setter hook swaps that write back to the replacement.

The tag-before-set ordering is important. Setting the source before tagging the panel allowed re-entrant/native follow-up writes to see the panel as untracked, which caused the bottom spectator avatar to flicker between original and replacement.

Panorama support:

- `applyAvatarOverrides()` stores override data on the HUD root panel.
- It applies replacements to scoreboard rows and top team-counter avatar panels by calling the real `PopulateFromSteamID` method on existing `CSGOAvatarImage` panels.
- It registers scoreboard open/update handlers for Panorama-created/rebuilt scoreboard panels.
- This is not an overlay. It repopulates existing native `CSGOAvatarImage` panels.

Cleanup:

- `mirv_avatar remove` removes the configured override and clears tracked panel pointers.
- `mirv_avatar clear` removes all configured overrides and clears tracked panel pointers.

## Update Failure Points

This implementation depends on current CS2 native code shapes. After a game update, check these areas first:

- Byte signatures in `getDeathMsgAddrs` for:
  - `CSGOAvatarImage::PopulateFromPlayerSlot`.
  - The native bottom-bar player-slot avatar helper.
  - The avatar-handle population path.
  - The SteamID-to-avatar-image-source helper.
  - The player-slot-to-player and player-to-XUID helpers.
- The `CSGOAvatarImage` vtable slot used for image-source setting. It was `0x2a0 / sizeof(void*)` in the tested build.
- `hudhealthammocenter.xml` layout IDs: `HudSpecplayer__Avatar`, `PlayerAvatarDefault`, and `PlayerAvatar`.
- `scoreboard.js` and `scoreboard.xml` row/avatar structure.
- `hudteamcounter.xml` top-avatar panel IDs.
- Whether CS2 changes the player-slot or avatar-handle flow to bypass the hooked source setter.

Expected update symptoms:

- Game shows an HLAE error box on launch: one of the signatures was not found.
- Top bar works but scoreboard does not: scoreboard Panorama row structure or event timing changed.
- Scoreboard works but bottom bar flickers or shows the original avatar: native bottom helper, avatar-handle path, or setter vtable slot changed.
- Bottom bar shows the original avatar only: panel tagging is not reached, or the source setter hook is not installed.
- `mirv_avatar print` works but no UI changes: Panorama context or native hooks are not being applied.

## Verification

Local test used the Ancient demo documented in `demo_playback_test_demo_roster.md`.

The test config alternated every player between the original Wladimir Putin avatar SteamID and another Steam account avatar:

```text
mirv_cmd addAtTick 240 mirv_avatar byXuid add x76561198723801816 steamid x76561197960680616
mirv_cmd addAtTick 241 mirv_avatar byXuid add x76561198772930198 steamid x76561198012831233
...
```

Verified results:

- Top bar avatars changed.
- Scoreboard avatars changed across both teams.
- Bottom spectator bar changed without overlay code.
- A 50-frame bottom-bar capture after the final source-hook fix showed `hayzinvor` consistently using the replacement Putin avatar in frames `00`, `25`, and `49`.

Test harness notes:

- Use scheduled `quit` or an external auto-cleanup wrapper for repeated HLAE/CS2 tests.
- Do not schedule `demo_pause` for this workflow; it previously made demo playback appear stuck after pressing play.
- For screenshot checks, wait until the demo has advanced past the scheduled `mirv_avatar` commands and Steam has had time to resolve avatars.

## PR Readiness

Good enough for local fragmovie/demo-render use, but still prototype-shaped for an official upstream PR.

Before upstreaming, consider:

- Move avatar code out of `DeathMsg.cpp`.
- Resolve the image-source setter deterministically instead of installing that detour lazily from a live panel vtable.
- Harden raw panel pointer tracking against panel destruction/recreation and pointer reuse.
- Retest after CS2 updates and across more demos, teams, scoreboard states, spectator states, and `remove`/`clear` flows.
