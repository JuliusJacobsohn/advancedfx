# Demo Playback Weapon Skins

Status: implementation restarted on `codex/demo-skin-pipeline`. The current branch has the first in-tree `mirv_demo_skin` implementation for persistent demo-time econ/item mutation. Normal weapon paint overrides on an existing weapon definition are the current focus and have worked in local tests for Glock Fade and M4A4 Howl. Knife model replacement, glove type replacement, and physical StatTrak counter attachment still need deeper client/model work.

Goal: local/demo-only visual override for fragmovie rendering, starting with by-XUID weapon paint kits such as AWP Dragon Lore or USP-S Printstream.

Current command:

```text
mirv_demo_skin byXuid add x<steamid64> active|all paintKit=<id> wear=<float> seed=<id> [statTrak=<id>] [meshGroup=<1|2>] [defIndex=<id>]
mirv_demo_skin byXuid remove x<steamid64>
mirv_demo_skin xuid <steamid64> weapon paintKit=<id> wear=<float> seed=<id> defIndex=<weaponDef> [statTrak=<id|off>|killCount=<id>|kills=<id>] [meshGroup=<mask>] [team=T|CT|any]
mirv_demo_skin xuid <steamid64> gloves paintKit=<id> wear=<float> seed=<id> defIndex=<gloveItemDef> [team=T|CT|any]
mirv_demo_skin xuid <steamid64> clear
mirv_demo_skin clear
mirv_demo_skin print
mirv_demo_skin apply
mirv_demo_skin status
mirv_demo_skin inspect
```

Examples:

```text
mirv_demo_skin byXuid add x76561198000000000 active paintKit=344 wear=0.01 seed=0
mirv_demo_skin byXuid add x76561198000000000 all paintKit=1142 wear=0.01 seed=0 statTrak=1337 meshGroup=1
mirv_demo_skin xuid x76561198000000000 weapon active paintKit=1142 wear=0.01 pattern=777 statTrak=1337 defIndex=61 meshGroup=2
mirv_demo_skin xuid x76561198000000000 weapon paintKit=38 wear=0.0001 seed=0 killCount=1234 defIndex=4 team=T meshGroup=2
mirv_demo_skin xuid x76561198000000000 gloves paintKit=10006 wear=0.08 seed=321 defIndex=5032
```

Implemented behavior:

- Configuration can be issued before or during demo playback. Application only happens while demo playback is active and matching player pawns/weapons exist.
- Multiple rules can be configured for one XUID, for example one weapon rule plus one glove rule.
- Resolves controller XUID to pawn to active weapon.
- Normal weapon overrides default to the persistent all-weapons scan. A rule such as `weapon paintKit=38 defIndex=4 team=T` stores one XUID-side-weapon-definition rule and applies it to matching weapon entities whenever they appear.
- The old `active|all` target argument is still accepted for compatibility, but it is not the intended normal weapon command shape anymore.
- Weapon entities keep their first matching rule assignment. If Donald's T-side Glock rule skins a Glock and Benjamin later picks that entity up, the entity continues to use Donald's Glock rule instead of being reassigned to Benjamin's Glock rule. A new Glock entity spawned for Benjamin can still receive Benjamin's own Glock rule.
- `active` is legacy behavior for quick testing. It only considers the active weapon.
- The persistent scan checks `CPlayer_WeaponServices::m_hMyWeapons` plus active weapon fallback, but skips knife entities for normal weapon paint overrides unless an explicit experimental `itemDef=` replacement is supplied. Applying a normal weapon paint kit to a knife produced bad render state and a crash when switching weapons.
- Reapplies during `FRAME_RENDER_PASS`.
- Writes fallback paint kit, seed, wear, StatTrak, owner XUID, item ID high/low, account ID, quality, initialized state, and material/visual dirty flags.
- Uses CS2's faux item-id preview shape (`0xF000000000000000 | paintKit << 16 | itemDefinitionIndex`) instead of a monotonically increasing fake id. This matches public CS2 preview-panel code and may be required by composite material generation.
- `statTrak=<count>`, `killCount=<count>`, and `kills=<count>` are equivalent. `statTrak=off` writes the fallback counter as disabled and clears the item quality back to normal.
- `team=T` and `team=CT` rules are exact-side rules. Omitting `team=` stores a backward-compatible `team=any` rule.
- `gloves` currently writes the pawn `CCSPlayerPawn::m_EconGloves` item view, including item definition, fake item id, owner account, and initialized state. Paint/wear/seed still need the attribute/material path to become visually reliable.
- Current in-tree implementation does not yet call Panorama `InventoryAPI.PrecacheCustomMaterials`, the vendor `RegenerateWeaponSkins` primitive, or a fake inventory/SO-cache path. Those remain the next likely material-refresh steps.

Earlier prototype / research behavior not yet ported into the current branch:

- Sets `C_EconItemView::m_iItemIDHigh` to `-1` when applying skins, matching the vendor project's fallback-material path.
- Writes direct `CEconItemAttribute` entries into `m_AttributeList` only for the material refresh path. This intentionally matches the inspected external skin changer more closely than earlier tests that also wrote `m_NetworkedDynamicAttributes`.
- Attempts `clientside_reload_custom_econ` after explicit `apply` / `add`.
- Clears `C_EconItemView::m_bDisallowSOC`.
- Resolves and calls the client `CGameSceneNode::SetMeshGroupMask` function when possible, with a direct `CModelState::m_MeshGroupMask` fallback.
- The direct mesh-group fallback now also marks the vendor-observed dirty mesh-group field (`modelState + 0xD8 -> +0x10`) before writing `CModelState::m_MeshGroupMask`.
- Applies the mesh-group mask to the weapon scene node and the matching `C_CS2HudModelWeapon` owned by that weapon. A broader pass over all HUD model entities also touched arms and caused the first-person hands to disappear.
- `inspect` prints runtime `client.dll` diagnostics for known public skin-changer patterns, material/composite/StatTrak strings, and RIP-relative xrefs. This is for finding the next refresh function without guessing offsets.
- `inspect` also checks the inventory/SO-cache function signatures used by the one cloned client-side skin changer that has real implementation code.
- `inspect` checks the external UnknownCheats skin-changer `RegenerateWeaponSkins` signature too.
- `byXuid debug` scans for client entities whose class/debug names contain `stattrak` or `kill`, so the physical counter path can be diagnosed separately from weapon paint/material state.

Known local test results from earlier skin investigation:

- 2026-06-08 in-tree smoke test on `replays/match730_003824372541888135516_1872110164_388.dem`: one `weapon all` rule and one `gloves` rule were configured before `+playdemo` for `x76561198012831233`. Runtime status at demo tick `621`: `configured=2 present=1 candidates=4 patched=3 skipped=1 coreOffsets=1 gloveOffsets=1`. The skipped candidate is expected from the knife guard. This proves command parsing, XUID matching, weapon list walking, glove offset resolution, and frame-stage reapply are functioning; it does not yet prove visible material regeneration or physical StatTrak attachment.

- XUID targeting works.
- Weapon targeting works.
- `all` patches knife and USP-S in `m_hMyWeapons`.
- UI/name path consumes the fake data, e.g. `StatTrak USP-S Printstream`.
- The tested USP-S Printstream visible skin is correct with `meshGroup=2` after explicit apply and a little demo time advancement.
- The physical StatTrak counter model is not visible yet even when the name and fallback StatTrak value are correct.
- Latest paused-state test had correct name and correct skin, the R8/Revolver no longer inherited the USP-S override, but no physical StatTrak counter appeared on the model.
- Visible first-person weapon material changes correctly for the tested USP-S Printstream path after explicit apply and a little demo time advancement.
- Setting the override before weapon spawn / round start still only updates UI/econ state. Timing is probably not the main blocker.
- `clientside_reload_custom_econ` fires successfully from HLAE's side but still only refreshes UI/name state.
- CS2's built-in `cl_paintkit_override 1142` did not visibly change the weapon in the tested demo context.
- Current client build exposes `C_CSPlayerPawn::m_hHudModelArms`, not the older pawn `m_pViewModelServices` route.
- `m_hHudModelArms` points to a valid `C_CS2HudModelArms` entity with a high handle entry outside normal `GetHighestEntityIndex()` enumeration.
- A later experiment linking `C_EconEntity::m_hViewmodelAttachment` to the owned `C_CS2HudModelWeapon` and marking render/anim/model flags dirty caused an extra default weapon to render in first person. It looked like a duplicated invisible player/world weapon, moved with the player, and did not solve material refresh. That path is disabled by default and should be treated as a dead end unless deliberately reproducing the artifact.
- The first `RegenerateWeaponSkins` test proved visible material refresh is possible, but applying the USP-S Printstream override to `all` also changed the knife and made the arms disappear / crash on weapon switch. The current implementation now avoids both known bad writes: no knife patching, no all-HUD-model mesh mask pass.
- After removing those unsafe writes, hands are visible again and switching to the knife no longer crashes. The remaining blocker is visual correctness: `meshGroup=1` produces the custom material but it is misaligned/wrong-looking; `meshGroup=2` reverts to the default material for the tested USP-S.
- The stricter vendor lifecycle fixed the tested USP-S Printstream material: `meshGroup=2`, fake item id, fallback paint kept, and empty temporary attributes produce the correct visible skin after the demo advances.
- After switching away and back, the demo can still stomp UI/name fallback fields while the generated material remains correct. The steady-state path now reapplies fallback/UI fields and writes `m_NetworkedDynamicAttributes` without recreating the temporary material vector in `m_AttributeList`.

Fields currently used by the in-tree implementation:

- `CPlayer_WeaponServices::m_hActiveWeapon`
- `CPlayer_WeaponServices::m_hMyWeapons`
- `CCSPlayerPawn::m_EconGloves`
- `C_EconEntity::m_AttributeManager`
- `C_EconEntity::m_OriginalOwnerXuidLow`
- `C_EconEntity::m_OriginalOwnerXuidHigh`
- `C_EconEntity::m_nFallbackPaintKit`
- `C_EconEntity::m_nFallbackSeed`
- `C_EconEntity::m_flFallbackWear`
- `C_EconEntity::m_nFallbackStatTrak`
- `C_EconEntity::m_bAttributesInitialized`
- `C_AttributeContainer::m_Item`
- `C_EconItemView::m_iItemDefinitionIndex`
- `C_EconItemView::m_iEntityQuality`
- `C_EconItemView::m_iItemID`
- `C_EconItemView::m_iItemIDHigh`
- `C_EconItemView::m_iItemIDLow`
- `C_EconItemView::m_iAccountID`
- `C_EconItemView::m_bInitialized`
- `CSkeletonInstance::m_modelState`
- `CModelState::m_MeshGroupMask`

Attribute definition indexes identified for the future temporary material refresh vector:

```text
6  -> set item texture prefab
7  -> set item texture seed
8  -> set item texture wear
```

StatTrak is currently written through fallback/item fields only. Attribute definitions `80` and `81` remain candidates for a later physical StatTrak counter path, separate from material generation.

Current interpretation:

- We are past the wrong-XUID/wrong-weapon/wrong-field stage.
- The missing piece is material/model refresh, not basic econ mutation.
- The current first-person route appears to be the `C_CS2HudModel*` path, especially `C_CSPlayerPawn::m_hHudModelArms`.
- High-index client-only HUD model entities are identifiable, but direct mesh-group writes and previous viewmodel attachment experiments did not rebuild painted materials.
- The next implementation needs a real composite/material rebuild function, likely near `PrecacheCustomMaterials`, `C_CS2HudModelArms::UpdateAndSetupView`, or related custom material generation code.
- If the Panorama precache call still only updates UI text, the next serious implementation path is not more field guessing. It is to port the actual client-side skin-changer architecture: create a local fake `CEconItem`, add it to the local inventory/SO cache, trigger `SOCreated` / loadout update behavior, then copy the resulting item ID onto the demo weapon.

Useful public references:

- <https://github.com/Nereziel/cs2-WeaponPaints>
- <https://github.com/SteamDatabase/GameTracking-CS2>
- <https://s2v.app/SchemaExplorer/cs2/client/C_CSPlayerPawn>
- <https://s2v.app/SchemaExplorer/cs2/client/C_CS2HudModelWeapon>

Research notes:

- `Nereziel/cs2-WeaponPaints` writes the same important econ attributes and uses a server-side `CAttributeList_SetOrAddAttributeValueByName` signature.
- That signature is not present in local `client.dll`, matching `attrSetter=0`.
- Server plugins commonly avoid in-place client material invalidation by killing/recreating or regiving weapons, then applying skin data to new entities. That route is not directly available during passive demo playback.
- `singhhdev/CS2-Internal-SkinChanger-Inventory-Changer` adds three client-side clues: set `m_bDisallowSOC=false`, update scene-node mesh group masks, and use `CGameSceneNode::SetMeshGroupMask` rather than only writing econ fields.
- That internal project still relies on older/local-player viewmodel services for the actual viewmodel, while the tested current CS2 build appears to use the newer `C_CS2HudModel*` path.
- More importantly, that project does not skin weapons from fallback fields alone. It creates a `CEconItem`, adds it to the local GC/SO inventory cache, equips it in loadout, then copies the generated loadout item's ID to the live weapon. This likely explains why direct weapon mutation updates UI text but not the rendered material.
- Its public signatures for mesh-group/model/stattrak/name-tag functions are currently not found in the tested `client.dll`, so the code is a useful architectural reference, not a directly portable patch.
- Local clone inspected: `C:\Users\Julius\AppData\Local\Temp\cs2-skin-research-singhhdev`.
- User-provided vendor repo inspected: `C:\Users\Julius\source\repos\advancedfx\VendorTestRepos\source_unknowncheats`.
- The vendor repo is an external memory writer, not an injected inventory/SO changer. Its useful path is `src/main.cpp` plus `src/SDK/weapon/C_EconEntity.h`: allocate/write a temporary `CEconItemAttribute` vector, set fallback paint, set item-id high to `-1`, patch/call `RegenerateWeaponSkins`, then clear fallback paint and remove the temporary vector.
- The current implementation now follows the vendor project more closely for material-generation attributes: three attributes only (`6`, `7`, `8`) and only on `m_AttributeList`. Earlier attempts wrote StatTrak attributes (`80`, `81`) and `m_NetworkedDynamicAttributes`, which may have polluted the generated composite material state.
- The next implementation pass follows the vendor lifecycle more closely too: after a successful explicit `RegenerateWeaponSkins` call, clear only command-owned temporary attributes while leaving the fake item id and fallback paint kit in place. The frame loop then avoids recreating the temporary attributes for that weapon until the demo/game overwrites the fake item id.
- The feature is not ready to merge as behavior yet. The useful retained output is the research and diagnostics: working XUID/weapon targeting, fake item-id shape, vendor regeneration path, mesh-group requirement, and the clear separation between material refresh and StatTrak counter/module attachment.

Open paths to try:

1. Locate the client path that attaches or updates the physical StatTrak counter. Paint/material generation is now working, so this should be treated as a separate model/module problem.
2. If the numeric dynamic attribute write does not enable the counter, add inspect strings/xrefs around `C_StattrakModule`, `stattrak`, `kill eater`, and any generated counter model/material names.
3. Run `mirv_demo_skin inspect` in-game and use the runtime xrefs to locate/call the client function near `PrecacheCustomMaterials`, `C_CS2HudModelArms::UpdateAndSetupView`, or the StatTrak module setup path.
4. Identify how the real first-person HUD model obtains or caches its material and child module entities after econ data is finalized.
5. If current inventory/SO-cache signatures still resolve, prototype a demo-only fake local `CEconItem` path and copy that item ID onto the target demo weapon.
6. Hook the material/module lookup path rather than making another render entity visible.
7. As a fallback for fragmovie output, intercept render/material replacement for the weapon model directly.

Out of scope for now:

- Live inventory ownership changes.
- GC/SO cache item fabrication.
- Anti-cheat bypasses.
- Matchmaking/live-server behavior.
- Stickers, gloves, and agents before basic visible weapon paint works.
