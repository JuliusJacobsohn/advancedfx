# Demo Playback Avatars

Status: researched, not implemented.

Difficulty: high unless restricted to local image overrides on specific Panorama panels.

Current repo state:

- No Steam Friends / avatar replacement implementation exists.
- The visible Steam API hook in the CS2 path is `New_SteamInternal_FindOrCreateUserInterface`, currently used for Steam Remote Storage behavior.
- Player SteamID64 is available through `CBasePlayerController::m_steamID`.

Possible routes:

1. Panorama/UI override
   - Find scoreboard/player-card/avatar panels.
   - Override image paths or textures for those panels.
   - Most practical for fragmovie-only use.
   - UI-surface specific.

2. Steam avatar API override
   - Hook `ISteamFriends` avatar calls and possibly `ISteamUtils::GetImageRGBA`.
   - Map SteamID64 to replacement image handles or RGBA data.
   - Broader but much more invasive.

3. SteamID swap experiment
   - Maybe changing `m_steamID` makes the game resolve another player's avatar.
   - This is broad and may affect name replacement, rank/loadout/avatar lookup, player listings, local-player highlighting, and XUID matching.
   - Cached avatar images may not refresh without panel/cache invalidation.

Recommendation:

Use a dedicated avatar visual override rather than changing identity globally. A SteamID-swap command can be tested later, but it should be clearly experimental and reversible.
