# Demo Playback Weapon Skins

Status: partially implemented. Econ/item/UI-name mutation works; USP-S Printstream visible material replacement works after demo time advances. Physical StatTrak counter attachment is still unresolved.

Goal: local/demo-only visual override for fragmovie rendering, starting with by-XUID weapon paint kits such as AWP Dragon Lore or USP-S Printstream.

Current command:

```text
mirv_demo_skin byXuid add x<steamid64> active|all paintKit=<id> wear=<float> seed=<id> [statTrak=<id>] [meshGroup=<1|2>] [defIndex=<id>]
mirv_demo_skin byXuid remove x<steamid64>
mirv_demo_skin byXuid debug x<steamid64>
mirv_demo_skin clear
mirv_demo_skin print
mirv_demo_skin apply
mirv_demo_skin inspect
```

Examples:

```text
mirv_demo_skin byXuid add x76561198000000000 active paintKit=344 wear=0.01 seed=0
mirv_demo_skin byXuid add x76561198000000000 all paintKit=1142 wear=0.01 seed=0 statTrak=1337 meshGroup=1
```

Implemented behavior:

- Applies only while demo playback is active.
- Resolves controller XUID to pawn to active weapon.
- `active` patches the active weapon.
- `active` auto-locks to the currently held weapon definition when possible, so a USP-S override does not later bleed onto an R8 after the player buys/switches weapons. `defIndex=<id>` can set this manually.
- `all` patches `CPlayer_WeaponServices::m_hMyWeapons` plus active weapon fallback, but skips knife entities for normal weapon paint overrides. Applying a USP/AWP paint kit to a knife produced bad render state and a crash when switching weapons.
- Reapplies during `FRAME_RENDER_PASS`.
- Also attempts an early patch from the client entity add hook when a matching weapon entity is created. This is meant for commands that are configured before the relevant weapon is spawned, for example before the next round or after seeking before the player receives weapons.
- Writes fallback paint kit, seed, wear, StatTrak, owner XUID, item ID high/low, account ID, quality, initialized state, and material/visual dirty flags.
- Uses CS2's faux item-id preview shape (`0xF000000000000000 | paintKit << 16 | itemDefinitionIndex`) instead of a monotonically increasing fake id. This matches public CS2 preview-panel code and may be required by composite material generation.
- Calls Panorama `InventoryAPI.PrecacheCustomMaterials('<fauxItemId>')` after creating the preview-style fake item ID. This is a low-risk experiment to see whether CS2 will generate the missing composite material for the same fake item ID the UI/name path already accepts.
- Uses a vendor skin-changer refresh primitive when available: finds `RegenerateWeaponSkins` with signature `48 83 EC ?? E8 ?? ?? ?? ?? 48 85 C0 0F 84 ?? ?? ?? ?? 48 8B 10`, patches its hardcoded attribute-vector offset at `+0x52`, and calls it after explicit `apply` / `add`.
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

Known local test results:

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

Fields currently used:

- `CPlayer_WeaponServices::m_hActiveWeapon`
- `CPlayer_WeaponServices::m_hMyWeapons`
- `C_EconEntity::m_AttributeManager`
- `C_EconEntity::m_OriginalOwnerXuidLow`
- `C_EconEntity::m_OriginalOwnerXuidHigh`
- `C_EconEntity::m_nFallbackPaintKit`
- `C_EconEntity::m_nFallbackSeed`
- `C_EconEntity::m_flFallbackWear`
- `C_EconEntity::m_nFallbackStatTrak`
- `C_EconEntity::m_bAttributesInitialized`
- `C_EconEntity::m_hViewmodelAttachment`
- `C_EconEntity::m_bAttachmentDirty`
- `C_CSWeaponBase::m_bClearWeaponIdentifyingUGC`
- `C_CSWeaponBase::m_bVisualsDataSet`
- `C_CSWeaponBase::m_nCustomEconReloadEventId`
- `C_CSWeaponBase::m_bUIWeapon`
- `C_AttributeContainer::m_Item`
- `C_EconItemView::m_iItemDefinitionIndex`
- `C_EconItemView::m_iEntityQuality`
- `C_EconItemView::m_iItemID`
- `C_EconItemView::m_iItemIDHigh`
- `C_EconItemView::m_iItemIDLow`
- `C_EconItemView::m_iAccountID`
- `C_EconItemView::m_bDisallowSOC`
- `C_EconItemView::m_bInitialized`
- `C_EconItemView::m_bRestoreCustomMaterialAfterPrecache`
- `C_EconItemView::m_AttributeList`
- `C_EconItemView::m_NetworkedDynamicAttributes`
- `CAttributeList::m_Attributes`
- `CEconItemAttribute` fields
- `C_CSPlayerPawn::m_hHudModelArms`
- `C_BaseEntity::m_pGameSceneNode`
- `CSkeletonInstance::m_modelState`
- `CModelState::m_MeshGroupMask`

Attribute definition indexes currently written for the temporary material refresh vector:

```text
6  -> set item texture prefab
7  -> set item texture seed
8  -> set item texture wear
```

StatTrak is still written through fallback/item fields for the UI path, but no longer as temporary material-regeneration attributes. The steady-state networked dynamic attributes write definition `80` as the numeric kill count and definition `81` as `0`; this is an experiment for the client-side StatTrak path and is separate from material generation.

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
