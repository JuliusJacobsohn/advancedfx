#include "SchemaSystem.h"
#include "Globals.h"
#include <winsock.h>

ClientDllOffsets_t g_clientDllOffsets;

// module name -> class name -> field name -> offset
std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::ptrdiff_t>>> g_SchemaSystemOffsets;

void getOffsetsFromSchemaSystem(SDK::CSchemaSystem* pSchemaSystem)
{
	void** pScopeArray = (void**)(pSchemaSystem->m_pScopeArray);

	for (uint64_t i = 0; pSchemaSystem->m_nScopeSize > i; ++i)
	{
		SDK::CSchemaSystemTypeScope* pSchemaScope = (SDK::CSchemaSystemTypeScope*)(pScopeArray[i]);

		// we don't need other modules for now
		if (!pSchemaScope || !pSchemaScope->m_pDeclaredClasses || 0 != strcmp(pSchemaScope->m_szName, "client.dll"))
		{
			continue;
		}

		std::vector<SDK::CSchemaDeclaredClassEntry> declaredClassEntries(pSchemaScope->m_nNumDeclaredClasses);
		memcpy(declaredClassEntries.data(), pSchemaScope->m_pDeclaredClasses, (pSchemaScope->m_nNumDeclaredClasses) * sizeof(SDK::CSchemaDeclaredClassEntry));

		for (uint16_t j = 0; j < pSchemaScope->m_nNumDeclaredClasses; ++j)
		{
			SDK::CSchemaDeclaredClass* pDeclaredClass = declaredClassEntries[j].m_pDeclaredClass;
			if (!pDeclaredClass) continue;

			SDK::CSchemaClass* pClass = pDeclaredClass->m_Class;
			if (!pClass) continue;

			const char* className = pClass->m_szName;

			uintptr_t pClassFields = (uintptr_t)(pClass->m_pFields);
			if (pClassFields)
			{
				for (uint16_t k = 0; pClass->m_nNumFields > k; ++k)
				{
					SDK::CSchemaField* pField = (SDK::CSchemaField*)(pClassFields + sizeof(SDK::CSchemaField) * k);

					if (!pField) continue;
					if (!pField->m_pType) continue; 
					if (!pField->m_szName) continue;

					auto fieldName = pField->m_szName;
		
					size_t fieldNameSize = strlen(fieldName);
					bool isNameValid = (fieldNameSize > 0);

					for (size_t n = 0; n < fieldNameSize; ++n) {
						if (!isascii(fieldName[n])) {
							isNameValid = false;
							break;
						}
					}

					if (!isNameValid) continue;

					g_SchemaSystemOffsets[pSchemaScope->m_szName][className][fieldName] = pField->m_nOffset;
				}
			}
		
		}
	}
}

bool getOffset(ptrdiff_t* offset, std::string moduleName, std::string className, std::string fieldName)
{
	if(g_SchemaSystemOffsets.find(moduleName) == g_SchemaSystemOffsets.end()) return false;
	auto& module = g_SchemaSystemOffsets.at(moduleName);

	if(module.find(className) == module.end()) return false;
	auto& classFields = module.at(className);

	if(classFields.find(fieldName) == classFields.end()) return false;
	*offset = classFields.at(fieldName);

	return true;
}

void getOptionalOffset(ptrdiff_t* offset, std::string moduleName, std::string className, std::string fieldName)
{
	getOffset(offset, moduleName, className, fieldName);
}

void initSchemaSystemOffsets()
{
	bool bOk = true;

	bOk = bOk && getOffset(&g_clientDllOffsets.C_CSGameRulesProxy.m_pGameRules, "client.dll", "C_CSGameRulesProxy", "m_pGameRules");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_CSGameRules.m_gamePhase, "client.dll", "C_CSGameRules", "m_gamePhase");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_CSGameRules.m_nOvertimePlaying, "client.dll", "C_CSGameRules", "m_nOvertimePlaying");
	bOk = bOk && getOffset(&g_clientDllOffsets.CEntityInstance.m_pEntity, "client.dll", "CEntityInstance", "m_pEntity");
	bOk = bOk && getOffset(&g_clientDllOffsets.CEntityIdentity.m_flags, "client.dll", "CEntityIdentity", "m_flags");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseEntity.m_CBodyComponent, "client.dll", "C_BaseEntity", "m_CBodyComponent");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseEntity.m_pGameSceneNode, "client.dll", "C_BaseEntity", "m_pGameSceneNode");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseEntity.m_iHealth, "client.dll", "C_BaseEntity", "m_iHealth");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseEntity.m_hOwnerEntity, "client.dll", "C_BaseEntity", "m_hOwnerEntity");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseEntity.m_iTeamNum, "client.dll", "C_BaseEntity", "m_iTeamNum");
	bOk = bOk && getOffset(&g_clientDllOffsets.CBodyComponentSkeletonInstance.m_skeletonInstance, "client.dll", "CBodyComponentSkeletonInstance", "m_skeletonInstance");
	bOk = bOk && getOffset(&g_clientDllOffsets.CSkeletonInstance.m_modelState, "client.dll", "CSkeletonInstance", "m_modelState");
	bOk = bOk && getOffset(&g_clientDllOffsets.CModelState.m_hModel, "client.dll", "CModelState", "m_hModel");
	bOk = bOk && getOffset(&g_clientDllOffsets.CModelState.m_ModelName, "client.dll", "CModelState", "m_ModelName");
	getOptionalOffset(&g_clientDllOffsets.CModelState.m_MeshGroupMask, "client.dll", "CModelState", "m_MeshGroupMask");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseModelEntity.m_Glow, "client.dll", "C_BaseModelEntity", "m_Glow");
	bOk = bOk && getOffset(&g_clientDllOffsets.CGameSceneNode.m_pOwner, "client.dll", "CGameSceneNode", "m_pOwner");
	bOk = bOk && getOffset(&g_clientDllOffsets.CGameSceneNode.m_pParent, "client.dll", "CGameSceneNode", "m_pParent");
	bOk = bOk && getOffset(&g_clientDllOffsets.CGameSceneNode.m_vecAbsOrigin, "client.dll", "CGameSceneNode", "m_vecAbsOrigin");
	bOk = bOk && getOffset(&g_clientDllOffsets.CBasePlayerController.m_iszPlayerName, "client.dll", "CBasePlayerController", "m_iszPlayerName");
	bOk = bOk && getOffset(&g_clientDllOffsets.CBasePlayerController.m_steamID, "client.dll", "CBasePlayerController", "m_steamID");
	bOk = bOk && getOffset(&g_clientDllOffsets.CBasePlayerController.m_hPawn, "client.dll", "CBasePlayerController", "m_hPawn");
	bOk = bOk && getOffset(&g_clientDllOffsets.CCSPlayerController.m_sSanitizedPlayerName, "client.dll", "CCSPlayerController", "m_sSanitizedPlayerName");
	bOk = bOk && getOffset(&g_clientDllOffsets.CCSPlayerController.m_iCompTeammateColor, "client.dll", "CCSPlayerController", "m_iCompTeammateColor");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BasePlayerPawn.m_hController, "client.dll", "C_BasePlayerPawn", "m_hController");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BasePlayerPawn.m_pWeaponServices, "client.dll", "C_BasePlayerPawn", "m_pWeaponServices");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BasePlayerPawn.m_pObserverServices, "client.dll", "C_BasePlayerPawn", "m_pObserverServices");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BasePlayerPawn.m_pCameraServices, "client.dll", "C_BasePlayerPawn", "m_pCameraServices");
	bOk = bOk && getOffset(&g_clientDllOffsets.CPlayer_WeaponServices.m_hActiveWeapon, "client.dll", "CPlayer_WeaponServices", "m_hActiveWeapon");
	getOptionalOffset(&g_clientDllOffsets.CPlayer_WeaponServices.m_hMyWeapons, "client.dll", "CPlayer_WeaponServices", "m_hMyWeapons");
	if (!getOffset(&g_clientDllOffsets.CCSPlayerPawn.m_EconGloves, "client.dll", "C_CSPlayerPawn", "m_EconGloves")) getOptionalOffset(&g_clientDllOffsets.CCSPlayerPawn.m_EconGloves, "client.dll", "CCSPlayerPawn", "m_EconGloves");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_AttributeManager, "client.dll", "C_EconEntity", "m_AttributeManager");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_bAttributesInitialized, "client.dll", "C_EconEntity", "m_bAttributesInitialized");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_OriginalOwnerXuidLow, "client.dll", "C_EconEntity", "m_OriginalOwnerXuidLow");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_OriginalOwnerXuidHigh, "client.dll", "C_EconEntity", "m_OriginalOwnerXuidHigh");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_nFallbackPaintKit, "client.dll", "C_EconEntity", "m_nFallbackPaintKit");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_nFallbackSeed, "client.dll", "C_EconEntity", "m_nFallbackSeed");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_flFallbackWear, "client.dll", "C_EconEntity", "m_flFallbackWear");
	getOptionalOffset(&g_clientDllOffsets.C_EconEntity.m_nFallbackStatTrak, "client.dll", "C_EconEntity", "m_nFallbackStatTrak");
	getOptionalOffset(&g_clientDllOffsets.C_AttributeContainer.m_Item, "client.dll", "C_AttributeContainer", "m_Item");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_iItemDefinitionIndex, "client.dll", "C_EconItemView", "m_iItemDefinitionIndex")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_iItemDefinitionIndex, "client.dll", "CEconItemView", "m_iItemDefinitionIndex");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_iEntityQuality, "client.dll", "C_EconItemView", "m_iEntityQuality")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_iEntityQuality, "client.dll", "CEconItemView", "m_iEntityQuality");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_iItemID, "client.dll", "C_EconItemView", "m_iItemID")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_iItemID, "client.dll", "CEconItemView", "m_iItemID");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_iItemIDHigh, "client.dll", "C_EconItemView", "m_iItemIDHigh")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_iItemIDHigh, "client.dll", "CEconItemView", "m_iItemIDHigh");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_iItemIDLow, "client.dll", "C_EconItemView", "m_iItemIDLow")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_iItemIDLow, "client.dll", "CEconItemView", "m_iItemIDLow");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_iAccountID, "client.dll", "C_EconItemView", "m_iAccountID")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_iAccountID, "client.dll", "CEconItemView", "m_iAccountID");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_bInitialized, "client.dll", "C_EconItemView", "m_bInitialized")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_bInitialized, "client.dll", "CEconItemView", "m_bInitialized");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_bDisallowSOC, "client.dll", "C_EconItemView", "m_bDisallowSOC")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_bDisallowSOC, "client.dll", "CEconItemView", "m_bDisallowSOC");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_AttributeList, "client.dll", "C_EconItemView", "m_AttributeList")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_AttributeList, "client.dll", "CEconItemView", "m_AttributeList");
	if (!getOffset(&g_clientDllOffsets.C_EconItemView.m_NetworkedDynamicAttributes, "client.dll", "C_EconItemView", "m_NetworkedDynamicAttributes")) getOptionalOffset(&g_clientDllOffsets.C_EconItemView.m_NetworkedDynamicAttributes, "client.dll", "CEconItemView", "m_NetworkedDynamicAttributes");
	getOptionalOffset(&g_clientDllOffsets.CAttributeList.m_Attributes, "client.dll", "CAttributeList", "m_Attributes");
	bOk = bOk && getOffset(&g_clientDllOffsets.CPlayer_CameraServices.m_hViewEntity, "client.dll", "CPlayer_CameraServices", "m_hViewEntity");
	bOk = bOk && getOffset(&g_clientDllOffsets.CPlayer_ObserverServices.m_iObserverMode, "client.dll", "CPlayer_ObserverServices", "m_iObserverMode");
	bOk = bOk && getOffset(&g_clientDllOffsets.CPlayer_ObserverServices.m_hObserverTarget, "client.dll", "CPlayer_ObserverServices", "m_hObserverTarget");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseCSGrenadeProjectile.m_bCanCreateGrenadeTrail, "client.dll", "C_BaseCSGrenadeProjectile", "m_bCanCreateGrenadeTrail");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseCSGrenadeProjectile.m_nSnapshotTrajectoryEffectIndex, "client.dll", "C_BaseCSGrenadeProjectile", "m_nSnapshotTrajectoryEffectIndex");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_BaseCSGrenadeProjectile.m_flTrajectoryTrailEffectCreationTime, "client.dll", "C_BaseCSGrenadeProjectile", "m_flTrajectoryTrailEffectCreationTime");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_SmokeGrenadeProjectile.m_vSmokeColor, "client.dll", "C_SmokeGrenadeProjectile", "m_vSmokeColor");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_EnvSky.m_hSkyMaterial, "client.dll", "C_EnvSky", "m_hSkyMaterial");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_EnvSky.m_vTintColor, "client.dll", "C_EnvSky", "m_vTintColor");
	bOk = bOk && getOffset(&g_clientDllOffsets.C_EnvSky.m_flBrightnessScale, "client.dll", "C_EnvSky", "m_flBrightnessScale");

	if (!bOk) ErrorBox(MkErrStr(__FILE__, __LINE__));	
}

void HookSchemaSystem(HMODULE schemaSystemDll)
{

   // 18000d8a7 48  89  05       MOV        qword ptr [DAT_180076730 ],RAX
   //           82  8e  06  00
   // 18000d8ae 4c  8d  0d       LEA        R9,[s_schema_list_bindings_<substring>_1800548   = "schema_list_bindings <substri
   //           0b  70  04  00
   // 18000d8b5 33  c0           XOR        EAX ,EAX
   // 18000d8b7 48  c7  05       MOV        qword ptr [DAT_18007675c ],0xc80000
   //           9a  8e  06 
   //           00  00  00 
	size_t instructionAddr = getAddress(schemaSystemDll, "48 89 05 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 33 C0 48 C7 05");
	if (0 == instructionAddr) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));	
		return;
	}

	uintptr_t _SchemaSystemInterface = instructionAddr + *(int32_t*)(instructionAddr + 3) + 7;
	SDK::CSchemaSystem* schemaSystem = (SDK::CSchemaSystem*)(_SchemaSystemInterface);

	if (!schemaSystem)
	{
		ErrorBox(MkErrStr(__FILE__, __LINE__));
		return;
	}

	getOffsetsFromSchemaSystem(schemaSystem);

	initSchemaSystemOffsets();

	g_SchemaSystemOffsets.clear();
}
