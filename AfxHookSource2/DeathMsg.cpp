#include "stdafx.h"

#include <iostream>
#include "WrpConsole.h"
#include "../shared/MirvDeathMsgFilter.h"

#include <cstddef>
#include <Windows.h>
#include "../shared/binutils.h"
#include "../deps/release/Detours/src/detours.h"

#include "../deps/release/prop/AfxHookSource/SourceSdkShared.h"
#include "../deps/release/prop/cs2/sdk_src/public/igameevents.h"
#include "../deps/release/prop/cs2/sdk_src/public/tier1/utlmap.h"
#include "../deps/release/prop/cs2/sdk_src/public/tier1/utlstring.h"

#include "DeathMsg.h"
#include "Globals.h"
#include "ClientEntitySystem.h"
#include "SchemaSystem.h"
#include "MirvColors.h"
#include "ReplaceName.h"

#include "addresses.h"

#include <set>
#include <algorithm>
#include <list>
#include <vector>
#include <map>
#include <sstream>

#include <google/protobuf/message_lite.h>

// TODO: move panorama stuff out after addresses.cpp is done
// decompose/change myPanoramaWrapper too
// doing it messy way here for now because lazy

// credit https://github.com/danielkrupinski/Osiris

void* g_CStylePropertyOpacity_vtable = 0;

typedef void(__fastcall *g_CPanelStyleSetStyleProperty_t)(void* This, void* property, bool transition);
g_CPanelStyleSetStyleProperty_t g_CPanelStyleSetStyleProperty = nullptr;

typedef void(__fastcall *g_CUIEngine_RunScript_t)(u_char* thisptr, u_char* contextPanel, const char* scriptSource, const char* originFile, uint64_t line);
g_CUIEngine_RunScript_t g_CUIEngine_RunScript = nullptr;

struct CPanel2D {
	const char* getClassName() {
		// in case it breaks see nearby functions in vtable, it just returns DAT
		void * pClientClass = ((void * (__fastcall *)(void *)) (*(void***)this)[61]) (this);

		if(pClientClass) {
			return *(const char**)((unsigned char*)pClientClass + 0x8);
		}

		return nullptr;
	}
};

struct StylePropertySymbolMap {
    uint8_t findSymbol(const char* stylePropertyName) {
        if (!symbols) return 0xFF;

		for (int i = 0; i < symbols->numElements; ++i) {
            if (std::strcmp(symbols->memory[i].key.Get(), stylePropertyName) == 0)
                return symbols->memory[i].value;
        }

        return 0xFF;
    }

    SOURCESDK::CS2::CUtlMap<SOURCESDK::CS2::CUtlString, uint8_t>* symbols;
} g_PanoramaStylePropertySymbols;

CON_COMMAND(__mirv_panorama_dump_style_symbols, "") {
	auto symbols = g_PanoramaStylePropertySymbols.symbols;

	for (int i = 0; i < symbols->numElements; ++i) {
		auto node = symbols->memory[i];
		advancedfx::Message("%i: %s\n", node.value, node.key.Get());
	}
}

struct StylePropertyOpacity {
	void* vtable;
	uint8_t id;
	bool disallowTransition = false;
	u_char pad[0x6];
	float value;

	StylePropertyOpacity() {} 

	StylePropertyOpacity(void* vt, uint8_t i, float v) 
		: vtable(vt), id(i), value(v) {}

};

bool makeOpacityProperty(StylePropertyOpacity* out, float value) {
	auto id = g_PanoramaStylePropertySymbols.findSymbol("opacity");
	if (g_CStylePropertyOpacity_vtable == nullptr || id == 0xFF) return false;

	*out = StylePropertyOpacity { g_CStylePropertyOpacity_vtable, id, value};

	return true;
}

struct CUIPanel {
	bool setOpacity(float value) {
		auto style = (u_char*)(this + CS2::PanoramaUIPanel::panelStyle);

		StylePropertyOpacity styleProp;
		if (!makeOpacityProperty(&styleProp, value)) return false;

		g_CPanelStyleSetStyleProperty(style, &styleProp, true);

		return true;
	}
};

currentGameCamera g_CurrentGameCamera;

namespace CS2 {
	namespace PanoramaUIPanel {
		ptrdiff_t getAttributeString = 0;
		ptrdiff_t setAttributeString = 0;
	}

	namespace PanoramaPanelStyle {
		ptrdiff_t setPanelStyleProperty = 0;
	}

	namespace PanoramaUIEngine {
		ptrdiff_t makeSymbol = 0;
	}
};

struct PlayerInfo {
	char* name;
	uint64_t xuid;
	int specKey;
	int userId;
	u_char* playerController;
};

PlayerInfo getSpectatedPlayer() 
{
	PlayerInfo result = {0,0,0,-1,0};
	auto cameraOrigin = g_CurrentGameCamera.origin;
	auto cameraAngles = g_CurrentGameCamera.angles;

    int highestIndex = GetHighestEntityIndex();
	for(int i = 0; i < highestIndex + 1; i++)
	{
        if(auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,i))
		{
			if(!ent->IsPlayerPawn()) continue;

			float entityOrigin[3];
			ent->GetRenderEyeOrigin(entityOrigin);
			float entityAngles[3];
			ent->GetRenderEyeAngles(entityAngles);

			std::vector<double> deltaList = {
				std::abs(std::abs(entityOrigin[0]) - std::abs(cameraOrigin[0])),
				std::abs(std::abs(entityOrigin[1]) - std::abs(cameraOrigin[1])),
				std::abs(std::abs(entityOrigin[2]) - std::abs(cameraOrigin[2])),
				std::abs(std::abs(entityAngles[0]) - std::abs(cameraAngles[0])),
				std::abs(std::abs(entityAngles[1]) - std::abs(cameraAngles[1])),
				std::abs(std::abs(entityAngles[2]) - std::abs(cameraAngles[2]))
			};

			if (
				deltaList[0] > 0.2f || deltaList[1] > 0.2f || deltaList[2] > 0.2f ||
				deltaList[3] > 0.2f || deltaList[4] > 0.2f || deltaList[5] > 0.2f
			) continue;

			auto controllerIndex = ent->GetPlayerControllerHandle().GetEntryIndex();
			if (-1 == controllerIndex) continue;

			auto playerController = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,controllerIndex);

			auto xuid = *(uint64_t*)((u_char*)(playerController) + g_clientDllOffsets.CBasePlayerController.m_steamID);
			auto name = (char*)((u_char*)(playerController) + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName);

            if (0 == xuid) advancedfx::Warning("Error: could not find xuid for entity %i\n", controllerIndex);
            if (nullptr == name || 0 == strlen(name)) advancedfx::Warning("Error: could not find name for entity %i\n", controllerIndex);

			result.name = name;
			result.xuid = xuid;
			result.userId = controllerIndex - 1;
			result.playerController = (u_char*)(playerController);
			// speckey is not really needed here
			break;
		}
	}

	if (-1 == result.userId) 
		advancedfx::Warning(
			"Could not find spectated player.\n"
			"Make sure you're in pov mode and camera fully switched.\n"		
		);

	return result;
};

PlayerInfo getPlayerInfoFromControllerIndex(int entindex)
{
	PlayerInfo result = {0,0,0,0,0};

	// Left screen side keys: 1, 2, 3, 4, 5
	// Right screen side keys: 6, 7, 8, 9, 0
	int slotCT = 0;
	int slotT = 0;

	bool swapPlayerSide = false;
	// apparently in CS2 CT is always on the left side, so we don't have to check for swap
	// but in case they change it in future we can probably check for gamephase like below

    int highestIndex = GetHighestEntityIndex();

	// find gamerules, maybe we can save it since it's pointer
    // for(int i = 0; i < highestIndex + 1; i++)
	// {
    //     if(auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,i))
	// 	{
	// 		if (0 != strcmp("cs_gamerules", ent->GetClassName())) continue;

	// 		auto gameRules = *(u_char**)((u_char*)(ent) + CS2::C_CSGameRulesProxy::m_pGameRules);

	// 		auto gamePhase = *(int*)((gameRules) + CS2::C_CSGameRules::m_gamePhase);
	// 		auto overtimes = *(int*)((gameRules) + CS2::C_CSGameRules::m_nOvertimePlaying);

	// 		advancedfx::Message("gamePhase: %i\n", gamePhase);
	// 		advancedfx::Message("overtimes: %i\n", overtimes); // have to check if overtimes are affecting this or not

	// 		if(3 == gamePhase) swapPlayerSide = true;

	// 		// gamePhase:
	// 		// 	2 = "first"
	// 		// 	3 = "second"
	// 		// 	4 = "halftime"
	// 		// 	5 = "postgame

	// 		break;
	// 	}
	// }

    for(int i = 0; i < highestIndex + 1; i++)
	{
        if(auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,i))
		{
			if(!ent->IsPlayerController()) continue;

			auto teamNumber = *(int*)((u_char*)(ent) + g_clientDllOffsets.C_BaseEntity.m_iTeamNum);
			if (0 == teamNumber || 1 == teamNumber) continue;

			int slot = 0;
			if (3 == teamNumber) // CT
			{
				slot = 1 + slotCT;
				if (swapPlayerSide) slot += 5;
				++slotCT;
			} 
			else if (2 == teamNumber) // T
			{
				slot = 1 + slotT;
				if (!swapPlayerSide) slot += 5;
				++slotT;
			}
			slot = slot % 10;

			if(i != entindex) continue;

			auto xuid = *(uint64_t*)((u_char*)(ent) + g_clientDllOffsets.CBasePlayerController.m_steamID);
			auto name = (char*)((u_char*)(ent) + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName);

            if (0 == xuid) advancedfx::Warning("Error: could not find xuid for entity %i\n", i);
            if (nullptr == name || 0 == strlen(name)) advancedfx::Warning("Error: could not find name for entity %i\n", i);

			result.name = name;
			result.xuid = xuid;
			result.playerController = (u_char*)(ent);
			result.userId = i - 1;
			result.specKey = slot;

			if (i == entindex) break;
        }
    }

	return result;
}

void DeathMsgId::operator=(char const * consoleValue) {
	if (!consoleValue)
		return;

	if (StringBeginsWith(consoleValue, "k"))
	{
		this->Mode = Id_Key;
		this->Id.specKey = atoi(consoleValue +1);
	}
	else if (StringBeginsWith(consoleValue, "x"))
	{
		uintptr_t val;
		
		if (0 == _stricmp("xTrace", consoleValue))
		{
			auto player = getSpectatedPlayer();
			if (-1 != player.userId)
				val = player.xuid;
			else 
				val = 0;
		}
		else
			val = strtoull(consoleValue + 1, 0, 10);

		this->operator=(val);
	}
	else
	{
		int val;

		if (0 == _stricmp("trace", consoleValue))
		{
			auto player = getSpectatedPlayer();
			if (-1 != player.userId)
				val = player.userId;
			else 
				val = 0;
		}
		else
			val = atoi(consoleValue);

		this->operator=(val);
	}
};

bool DeathMsgId::EqualsUserId(int userId)
{
	if (Mode == Id_UserId) return userId == Id.userId;

	if (userId < 0)
		return false;

	switch(Mode)
	{
		case Id_Key:
			// in CS2 playercontroller entityindex is userId + 1
			return getPlayerInfoFromControllerIndex(userId + 1).specKey == Id.specKey;
			break;

		case Id_Xuid:
			return getPlayerInfoFromControllerIndex(userId + 1).xuid == Id.xuid;
			break;
	}

	return false;
};

int DeathMsgId::ResolveToUserId()
{
	switch(Mode)
	{
	case Id_Key:
		{
			int highestIndex = GetHighestEntityIndex();
			for(int i = 0; i < highestIndex + 1; i++)
			{
				if(auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,i))
				{
					if(!ent->IsPlayerController()) continue;
					auto player = getPlayerInfoFromControllerIndex(i);
					if (player.specKey == Id.specKey) return i - 1;
				}
			}
		}
		return 0;
	case Id_Xuid:
		{
			int highestIndex = GetHighestEntityIndex();
			for(int i = 0; i < highestIndex + 1; i++)
			{
				if(auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,i))
				{
					if(!ent->IsPlayerController()) continue;
					auto player = getPlayerInfoFromControllerIndex(i);
					if (player.xuid == Id.xuid) return i - 1;
				}
			}
		}			
		return 0;
	}

	return Id.userId;
};

struct myPanoramaWrapper {
// credit https://github.com/danielkrupinski/Osiris 
// for engine and panel methods
	bool hooked = false;

	short lifeTimeSymbol = -1;
	short spawnTimeSymbol = -1;
	u_char** pUIEngine = nullptr;
	u_char** pHudPanel = nullptr;

	struct ColorEntry {
		u_char* pointer;
		bool use;
		uint32_t value;
		uint32_t defaultValue;
		std::string userValue = "";

		bool convertColorFromStrToInt (const char* str, uint32_t* outColor) {
			if (nullptr == str || nullptr == outColor) return false;

			auto hexStr = afxUtils::rgbaToHex(str, " ");
			if (hexStr.length() != 8) return false;

			*outColor = afxUtils::hexStrToInt(hexStr);
			return true;
		}; 

		bool setColor(const char* arg) {
			if (nullptr == arg) return false;
			if (0 == _stricmp("default", arg))
			{
				use = false;
				value = defaultValue;
				return true;
			}

			for (auto it = afxBasicColors.begin(); it != afxBasicColors.end(); ++it)
			{
				if (0 == _stricmp(it->name, arg))
				{
					use = true;
					userValue = arg;
					value = afxUtils::rgbaToHex(it->value);

					return true;
				}
			}
			return false;
		}

		bool setColor(advancedfx::ICommandArgs* args) {
			auto argc = args->ArgC();

			if (argc == 5)
			{
				uint32_t color;
				std::string str = "";
				str.append(args->ArgV(1));
				str.append(" ");
				str.append(args->ArgV(2));
				str.append(" ");
				str.append(args->ArgV(3));
				str.append(" ");
				str.append(args->ArgV(4));

				if (convertColorFromStrToInt(str.c_str(), &color))
				{
					use = true;
					userValue = str.c_str();
					value = color;
					return true;
				}
			}

			return false;
		};
	};

	ColorEntry BorderColor = { nullptr, false, 0xFF0000E1, 0xFF0000E1 };
	ColorEntry BackgroundColor = { nullptr, false, 0xA0000000, 0xA0000000 };
	ColorEntry LocalBackgroundColor = { nullptr, false, 0xE7000000, 0xE7000000 };
	ColorEntry CTcolor = { nullptr, false, 0xFFE69C6F, 0xFFE69C6F };
	ColorEntry Tcolor = { nullptr, false, 0xFF54BEEA, 0xFF54BEEA };

	void initSymbols() {
		if (-1 != lifeTimeSymbol) return;

		if (nullptr == pUIEngine){
			advancedfx::Warning("pUIEngine is null\n");
			return;
		}

		typedef short(__fastcall *makeSymbol_t)(u_char* ptr, int type, const char* name);
		const auto makeSymbol = *(makeSymbol_t*)((*(u_char**)(*pUIEngine)) + CS2::PanoramaUIEngine::makeSymbol);

		lifeTimeSymbol = makeSymbol(*pUIEngine, 0, "Lifetime");
		spawnTimeSymbol = makeSymbol(*pUIEngine, 0, "SpawnTime");
	};

	bool runScript(u_char* contextPanel, const char* scriptSource) {
		if (nullptr == pUIEngine || nullptr == *pUIEngine) {
			advancedfx::Warning("pUIEngine is null\n");
			return false;
		}

		if (nullptr == contextPanel) {
			advancedfx::Warning("Panorama script context panel is null\n");
			return false;
		}

		if (nullptr == g_CUIEngine_RunScript) {
			advancedfx::Warning("Panorama RunScript address is not available yet.\n");
			return false;
		}

		// Non-zero line bypasses CS2's Panorama script cache for repeated generated snippets.
		g_CUIEngine_RunScript(*pUIEngine, contextPanel, scriptSource, "", 1);
		return true;
	}

	u_char* getHudPanel() {
		if (nullptr == pHudPanel) {
			advancedfx::Warning("pHudPanel is null\n");
			return nullptr;
		}

		return ((u_char***)pHudPanel)[0][1];
	}

	u_char* getDeathnotices(){
		if (nullptr == pHudPanel){
			advancedfx::Warning("pHudPanel is null\n");
			return nullptr;
		}

		const auto hudPanel = ((u_char***)pHudPanel)[0][1];
		if (!hudPanel) return nullptr;
		const auto hudDeathNotice = findChildInLayoutFile(hudPanel, "HudDeathNotice");
		if (!hudDeathNotice) return nullptr;
		const auto visibleNotices = findChildInLayoutFile(hudDeathNotice, "VisibleNotices");
		if (!visibleNotices) return nullptr;

		auto deathnotices = visibleNotices + CS2::PanoramaUIPanel::children;

		return deathnotices;
	};

	bool clearDeathnotices(){
		initSymbols();

		const auto pDeathnotices = getDeathnotices(); // dunno how to invalidate it, so will get it each time
		if (nullptr == pDeathnotices) return false;

		if (*(int*)pDeathnotices == 0) return false;

		bool result = false;

		for (int i = 0; i < *(int*)pDeathnotices; i++) {
			const auto panel = ((u_char***)pDeathnotices)[1][i];

			typedef const char* (__fastcall *getAttributeString_t)(u_char* ptr, short attributeName, const char* defaultValue);
			const auto getAttributeString = *(getAttributeString_t*)(*(u_char**)panel + CS2::PanoramaUIPanel::getAttributeString);

			typedef void* (__fastcall *setAttributeString_t)(u_char* ptr, short attributeName, const char* defaultValue);
			const auto setAttributeString = *(setAttributeString_t*)(*(u_char**)panel + CS2::PanoramaUIPanel::setAttributeString);

			const auto lifetimeString = getAttributeString(panel, lifeTimeSymbol, "");
			if (!lifetimeString || strlen(lifetimeString) == 0) continue;

			const auto spawnTimeString = getAttributeString(panel, spawnTimeSymbol, "");
			if (!spawnTimeString || strlen(spawnTimeString) == 0) continue;

			setAttributeString(panel, lifeTimeSymbol, "0.001");
			setAttributeString(panel, spawnTimeSymbol, std::to_string(g_CurrentGameCamera.time - 1).c_str());
			result = true;
		}

		return result;
	}

	void printChildren(const char* parentId) {
		auto parentPanel = getHudPanel();
		if (!parentPanel) return;

		if (strlen(parentId) > 0) {
			auto r = findChildInLayoutFile(parentPanel, parentId);
			if (r) {
				parentPanel = r;
			} else {
				auto r2 = findChildrenInLayoutFileByClassName(parentPanel, parentId);
				if (!r2.empty()) parentPanel = r2[0]; // could be multiple there
			}
		} 

		auto parentPanelId = *(char**)(parentPanel + CS2::PanoramaUIPanel::panelId);
		auto parentPanel2D = *(CPanel2D**)(parentPanel + 0x8);

		advancedfx::Message("ClientClass / PanelId:\n");
		if (0 != parentPanel2D)
			advancedfx::Message("%s / %s\n", parentPanel2D->getClassName(), parentPanelId);
		else 
			advancedfx::Message("%s / %s\n", "null", parentPanelId);

		const auto children = parentPanel + CS2::PanoramaUIPanel::children;
		if (!children) {
			advancedfx::Warning("No children found.\n");
			return;
		};

		for (int i = 0; i < *(int*)children; ++i) {
			const auto panel = ((u_char***)children)[1][i];
			const auto panelId = *(char**)(panel + CS2::PanoramaUIPanel::panelId);
			auto panel2D = *(CPanel2D**)(panel + 0x8);

			if (!panelId && !panel2D) continue;

			if (0 != panel2D)
				advancedfx::Message("\t%s / %s\n", panel2D->getClassName(), panelId);
			else 
				advancedfx::Message("\t%s / %s\n", "null", panelId);
		}

	}

	std::vector<u_char*> findChildrenInLayoutFileByClassName(u_char* parentPanel, const char* classNameToFind) {
		std::vector<u_char*> res;
		if (!parentPanel) return res;

		const auto children = parentPanel + CS2::PanoramaUIPanel::children;
		if (!children) return res;

		for (int i = 0; i < *(int*)children; ++i) {
			const auto panel = ((u_char***)children)[1][i];
			auto panel2D = *(CPanel2D**)(panel + 0x8);
			if (!panel2D) continue;

			auto panelClassName = panel2D->getClassName();

			if (strcmp(panelClassName, classNameToFind) == 0) {
				res.emplace_back(panel);
			};
		}

		for (int i = 0; i < *(int*)children; ++i) {
			const auto panel = ((u_char***)children)[1][i];
			const auto panelFlags = *(u_char*)(panel + CS2::PanoramaUIPanel::panelFlags);
			if ((panelFlags & CS2::PanoramaUIPanel::k_EPanelFlag_HasOwnLayoutFile) == 0) {
				auto found = findChildrenInLayoutFileByClassName(panel, classNameToFind);
				if (!found.empty()) {
					for (auto i : found) {
						res.emplace_back(i);
					}
				}
			}
		}

		return res;
	}

	u_char* findChildInLayoutFile(u_char* parentPanel, const char* idToFind){
		if (!parentPanel) return nullptr;

		const auto children = parentPanel + CS2::PanoramaUIPanel::children;
		if (!children) return nullptr;

		for (int i = 0; i < *(int*)children; ++i) {
			const auto panel = ((u_char***)children)[1][i];
			const auto panelId = *(char**)(panel + CS2::PanoramaUIPanel::panelId);
			if (!panelId) continue;
			if (strcmp(panelId, idToFind) == 0) {
				return panel;
			};
		}

		for (int i = 0; i < *(int*)children; ++i) {
			const auto panel = ((u_char***)children)[1][i];
			const auto panelFlags = *(u_char*)(panel + CS2::PanoramaUIPanel::panelFlags);
			if ((panelFlags & CS2::PanoramaUIPanel::k_EPanelFlag_HasOwnLayoutFile) == 0) {
				if (const auto found = findChildInLayoutFile(panel, idToFind)) {
					return found;
				}
			}
		}

		return nullptr;
	};

	void applyColors() {
		if (nullptr != BorderColor.pointer) {
			*(uint32_t*)(BorderColor.pointer + 0x38 + 4*0) = BorderColor.use ? BorderColor.value : BorderColor.defaultValue;
			*(uint32_t*)(BorderColor.pointer + 0x38 + 4*1) = BorderColor.use ? BorderColor.value : BorderColor.defaultValue;
			*(uint32_t*)(BorderColor.pointer + 0x38 + 4*2) = BorderColor.use ? BorderColor.value : BorderColor.defaultValue;
			*(uint32_t*)(BorderColor.pointer + 0x38 + 4*3) = BorderColor.use ? BorderColor.value : BorderColor.defaultValue;
		}
		if (nullptr != LocalBackgroundColor.pointer) {
			*(uint32_t*)(LocalBackgroundColor.pointer +0x20) = LocalBackgroundColor.use ? LocalBackgroundColor.value : LocalBackgroundColor.defaultValue;
		}	
		if (nullptr != BackgroundColor.pointer) {
			*(uint32_t*)(BackgroundColor.pointer +0x20) = BackgroundColor.use ? BackgroundColor.value : BackgroundColor.defaultValue;
		}	
		if (nullptr != CTcolor.pointer) {
			*(uint32_t*)(CTcolor.pointer + 0x20) = CTcolor.use ? CTcolor.value : CTcolor.defaultValue;
		}
		if (nullptr != Tcolor.pointer) {
			*(uint32_t*)(Tcolor.pointer + 0x20) = Tcolor.use ? Tcolor.value : Tcolor.defaultValue;
		}
	}

	/*
	void updateHudPanelStyles() {
		// currently not required.
		const auto hudPanel = ((u_char***)pHudPanel)[0][1];
		if (hudPanel) {
			// Function is called also in if after refrence to "CUIPanel::AddClassesInternal - apply old dirty styles":
			// and is also at vtable entry 71 of panorama panel class.
			void (__fastcall * applyStyleFn)(void *, signed short int) = (void (__fastcall *)(void *, signed short int))((*(void***)hudPanel)[71]);
			applyStyleFn(hudPanel, 0);
		}
	}
	*/

} g_myPanoramaWrapper;

CON_COMMAND(__mirv_panorama_print_children, "") {
	const auto arg0 = args->ArgV(0);
	int argc = args->ArgC();

	if (2 <= argc)
	{
		const char * arg1 = args->ArgV(1);
		g_myPanoramaWrapper.printChildren(arg1);
	} else {
		g_myPanoramaWrapper.printChildren("");
	}
}

typedef uint32_t* (__fastcall *g_Original_hashString_t)(uint32_t* pResult, const char* string);
g_Original_hashString_t g_Original_hashString = nullptr;


class MyDeathMsgGameEventWrapper : public SOURCESDK::CS2::IGameEvent, public MyDeathMsgGameEventWrapperBase
{
public:
	MyDeathMsgGameEventWrapper(SOURCESDK::CS2::IGameEvent * event)
	: m_Event(event) { }

	SOURCESDK::CS2::CKV3MemberName hashString(const char * string) {
		uint32_t hash;
		g_Original_hashString(&hash, string);
		return SOURCESDK::CS2::CKV3MemberName(hash, -1, string);
	}

	bool IsHashStringEqual(const char * a, const SOURCESDK::CS2::CKV3MemberName & b) {
		return IsHashEqual(hashString(a),b);
	}

private:
	bool IsHashEqual(const SOURCESDK::CS2::CKV3MemberName & a, const SOURCESDK::CS2::CKV3MemberName & b) {
		return a.GetHashCode() == b.GetHashCode();
	}

public:
	virtual ~MyDeathMsgGameEventWrapper() {};
	virtual const char *GetName() const {
		return m_Event->GetName();
	}
	virtual int GetID() const {
		return m_Event->GetID();
	}
	virtual bool IsReliable() const {
		return m_Event->IsReliable();
	}
	virtual bool IsLocal() const {
		return m_Event->IsLocal();
	}
	virtual bool IsEmpty( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->IsEmpty(keySymbol);
	}
	virtual bool GetBool( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol) {
		return m_Event->GetBool(keySymbol);
	}
	virtual int GetInt( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol) {

		if (assistedflash.use && IsHashStringEqual("assistedflash", keySymbol)) return assistedflash.value;
		if (headshot.use && IsHashStringEqual("headshot", keySymbol)) return headshot.value;
		if (penetrated.use && IsHashStringEqual("penetrated", keySymbol)) return penetrated.value;
		if (dominated.use && IsHashStringEqual("dominated", keySymbol)) return dominated.value;
		if (revenge.use && IsHashStringEqual("revenge", keySymbol)) return revenge.value;
		if (wipe.use && IsHashStringEqual("wipe", keySymbol)) return wipe.value;
		if (noscope.use && IsHashStringEqual("noscope", keySymbol)) return noscope.value;
		if (thrusmoke.use && IsHashStringEqual("thrusmoke", keySymbol)) return thrusmoke.value;
		if (attackerblind.use && IsHashStringEqual("attackerblind", keySymbol)) return attackerblind.value;
		if (attackerinair.use && IsHashStringEqual("attackerinair", keySymbol)) return attackerinair.value;
		
		return m_Event->GetInt(keySymbol);
	}
	virtual uint64_t GetUint64( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol) {
		return m_Event->GetUint64(keySymbol);
	}
	virtual float GetFloat( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol) {
		return m_Event->GetFloat(keySymbol);
	}
	virtual const char *GetString( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol) {
		if (weapon.use && IsHashStringEqual("weapon", keySymbol)) return weapon.value;
		return m_Event->GetString(keySymbol);
	}
	virtual void *GetPtr( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->GetPtr(keySymbol);
	}
	virtual SOURCESDK::CS2::CEntityHandle GetEHandle( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->GetEHandle(keySymbol);
	}
	virtual SOURCESDK::CS2::CEntityInstance *GetEntity( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol) {
		return m_Event->GetEntity(keySymbol);
	}
	virtual void* GetEntityIndex( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->GetEntityIndex(keySymbol);
	}
	virtual SOURCESDK::CS2::CPlayerSlot GetPlayerSlot( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {

		if (attacker.newId.use && IsHashStringEqual("attacker", keySymbol)) return SOURCESDK::CS2::CPlayerSlot(attacker.newId.value.ResolveToUserId());
		if (victim.newId.use && IsHashStringEqual("userid", keySymbol)) return SOURCESDK::CS2::CPlayerSlot(victim.newId.value.ResolveToUserId());
		if (assister.newId.use && IsHashStringEqual("assister", keySymbol)) return SOURCESDK::CS2::CPlayerSlot(assister.newId.value.ResolveToUserId());

		return m_Event->GetPlayerSlot(keySymbol);
	}
	virtual SOURCESDK::CS2::CEntityInstance *GetPlayerController( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->GetPlayerController(keySymbol);
	}
	virtual SOURCESDK::CS2::CEntityInstance *GetPlayerPawn( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->GetPlayerPawn(keySymbol);
	}
	virtual SOURCESDK::CS2::CEntityHandle GetPawnEHandle( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->GetPawnEHandle(keySymbol);
	}
	virtual void* GetPawnEntityIndex( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->GetPawnEntityIndex(keySymbol);
	}
	virtual void SetBool( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, bool value ) {
		m_Event->SetBool(keySymbol,value);
	}
	virtual void SetInt( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, int value ) {
		m_Event->SetInt(keySymbol,value);
	}
	virtual void SetUint64( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, uint64_t value ) {
		m_Event->SetUint64(keySymbol,value);
	}
	virtual void SetFloat( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, float value ) {
		m_Event->SetFloat(keySymbol,value);
	}
	virtual void SetString( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, const char *value ) {
		m_Event->SetString(keySymbol,value);
	}
	virtual void SetPtr( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, void *value ) {
		m_Event->SetPtr(keySymbol,value);
	}
	virtual void SetEntity(const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, SOURCESDK::CS2::CEntityInstance *value) {
		m_Event->SetEntity(keySymbol,value);
	}
	virtual void SetEntity( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, void* value ) {
		m_Event->SetEntity(keySymbol,value);
	}
	virtual void SetPlayer( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, SOURCESDK::CS2::CEntityInstance *pawn ) {
		m_Event->SetPlayer(keySymbol,pawn);
	}
	virtual void SetPlayer( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol, SOURCESDK::CS2::CPlayerSlot value ) {
		m_Event->SetPlayer(keySymbol,value);
	}
	virtual void SetPlayerRaw( const SOURCESDK::CS2::GameEventKeySymbol_t &controllerKeySymbol, const SOURCESDK::CS2::GameEventKeySymbol_t &pawnKeySymbol, SOURCESDK::CS2::CEntityInstance *pawn ) {
		m_Event->SetPlayerRaw(controllerKeySymbol,pawnKeySymbol,pawn);
	}
	virtual bool HasKey( const SOURCESDK::CS2::GameEventKeySymbol_t &keySymbol ) {
		return m_Event->HasKey(keySymbol);
	}
	virtual void CreateVMTable( void* &Table ) {
		m_Event->CreateVMTable(Table);
	}
	virtual struct SOURCESDK::CS2::KeyValues3* GetDataKeys() const {
		return m_Event->GetDataKeys();
	}

private:
	SOURCESDK::CS2::IGameEvent * m_Event;
};

struct CS2_MirvDeathMsgGlobals : MirvDeathMsgGlobals {
	bool hooked = false; 
	MyDeathMsgGameEventWrapper* activeWrapper = nullptr;
} g_MirvDeathMsgGlobals;

typedef uint64_t (__fastcall *g_Original_getLocalSteamId_t)(void* param_1);
g_Original_getLocalSteamId_t g_Original_getLocalSteamId = nullptr;

typedef bool(__fastcall* CSGOAvatarImage_PopulateFromPlayerSlot_t)(void* This, int playerSlot);
typedef void* (__fastcall* CSGOAvatarImage_MakeAvatarImageSource_t)(const char* steamId);
typedef void* (__fastcall* CSGO_GetPlayerFromSlot_t)(int playerSlot);
typedef uint64_t(__fastcall* CSGO_GetPlayerXuid_t)(void* player);
typedef bool(__fastcall* CSGOAvatarImage_SetImageSource_t)(void* This, void* imageSource);
typedef bool(__fastcall* CSGOAvatarImage_PopulateFromAvatarHandle_t)(void* This, unsigned short avatarHandle);

extern CSGOAvatarImage_PopulateFromPlayerSlot_t g_Original_CSGOAvatarImage_PopulateFromPlayerSlot;
extern CSGOAvatarImage_PopulateFromPlayerSlot_t g_Original_CSGOAvatarImage_PopulateFromPlayerSlotNative;
extern CSGOAvatarImage_PopulateFromAvatarHandle_t g_Original_CSGOAvatarImage_PopulateFromAvatarHandle;
extern CSGOAvatarImage_SetImageSource_t g_Original_CSGOAvatarImage_SetImageSource;
extern CSGOAvatarImage_MakeAvatarImageSource_t g_CSGOAvatarImage_MakeAvatarImageSource;
extern CSGO_GetPlayerFromSlot_t g_CSGO_GetPlayerFromSlot;
extern CSGO_GetPlayerXuid_t g_CSGO_GetPlayerXuid;

size_t relativeCallTarget(size_t instruction);
bool __fastcall New_CSGOAvatarImage_PopulateFromPlayerSlot(void* This, int playerSlot);
bool __fastcall New_CSGOAvatarImage_PopulateFromPlayerSlotNative(void* This, int playerSlot);
bool __fastcall New_CSGOAvatarImage_PopulateFromAvatarHandle(void* This, unsigned short avatarHandle);
bool __fastcall New_CSGOAvatarImage_SetImageSource(void* This, void* imageSource);

uint64_t __fastcall getLocalSteamId(void* param_1) {
	uint64_t result = 0;
	MyDeathMsgPlayerEntry entry;
	bool use = false;

	if (nullptr != g_MirvDeathMsgGlobals.activeWrapper) { 
		if (g_MirvDeathMsgGlobals.activeWrapper->attacker.isLocal.use) {
			entry = g_MirvDeathMsgGlobals.activeWrapper->attacker;			
			use = true;
		}
		else if (g_MirvDeathMsgGlobals.activeWrapper->victim.isLocal.use) {
			entry = g_MirvDeathMsgGlobals.activeWrapper->victim;
			use = true;
		}
		else if (g_MirvDeathMsgGlobals.activeWrapper->assister.isLocal.use) {
			entry = g_MirvDeathMsgGlobals.activeWrapper->assister;
			use = true;
		}
	}

	if (g_MirvDeathMsgGlobals.useHighlightId)
	{
		entry.newId.value = g_MirvDeathMsgGlobals.highlightId;
		entry.isLocal.value = true;
		use = true;
	}

	if (use && !entry.isLocal.value) return 0;

	if (!use) return g_Original_getLocalSteamId(param_1);

	switch (entry.newId.value.Mode) {
		case DeathMsgId::Id_Key:
		{
			int highestIndex = GetHighestEntityIndex();	
			for(int i = 0; i < highestIndex + 1; i++)
			{
				if(auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,i))
				{
					if(!ent->IsPlayerController()) continue;
					auto player = getPlayerInfoFromControllerIndex(i);	
					if (player.specKey == entry.newId.value.Id.specKey)
					{
						result = player.xuid != 0 ? player.xuid : result;
						break;
					}
					
				}
			}
		}

		break;
		case DeathMsgId::Id_Xuid:
			result = entry.newId.value.Id.xuid != 0 ? entry.newId.value.Id.xuid : result;
		break;
		case DeathMsgId::Id_UserId:
		{
			auto player = getPlayerInfoFromControllerIndex(entry.newId.value.Id.userId + 1);
			result = player.xuid != 0 ? player.xuid : result;
		}
		break;
	};

	return result;
};

// param 1 CCSGO_HudDeathNotice : panorama::CPanel2D : panorama::IUIPanelClient : CCSGOHudElement : CGameEventListener : IGameEventListener2
// param 2 IGameEvent
typedef void (__fastcall *g_Original_handlePlayerDeath_t)(u_char* param_1, SOURCESDK::CS2::IGameEvent* param_2);
g_Original_handlePlayerDeath_t g_Original_handlePlayerDeath = nullptr;

void __fastcall handleDeathnotice(u_char* hudDeathNotice, SOURCESDK::CS2::IGameEvent* gameEvent) {

	if (!AFXADDR_GET(cs2_deathmsg_lifetime_offset) || !AFXADDR_GET(cs2_deathmsg_lifetimemod_offset)) {
		advancedfx::Warning("AFXERROR: deathmsg offsets not installed.\n");
		return g_Original_handlePlayerDeath(hudDeathNotice, gameEvent);
	}

	auto lifetimeOffset = (uint8_t)AFXADDR_GET(cs2_deathmsg_lifetime_offset);
	auto lifetimeModOffset = (uint8_t)AFXADDR_GET(cs2_deathmsg_lifetimemod_offset);

	float orgDeathNoticeLifetime, orgDeathNoticeLocalPlayerLifetimeMod;

	MyDeathMsgGameEventWrapper myWrapper(gameEvent);

	auto pDeathNoticeLifetime = (float*)(hudDeathNotice + lifetimeOffset);
	auto pDeathNoticeLocalPlayerLifetimeMod = (float*)(hudDeathNotice + lifetimeModOffset);

	auto uidAttacker = (int)(int16_t)gameEvent->GetInt(myWrapper.hashString("attacker"));
	auto uidVictim = (int)(int16_t)gameEvent->GetInt(myWrapper.hashString("userid"));
	auto uidAssister = (int)(int16_t)gameEvent->GetInt(myWrapper.hashString("assister"));

	myWrapper.attacker.newId.value.Id.userId = uidAttacker;
	myWrapper.victim.newId.value.Id.userId = uidVictim;
	myWrapper.assister.newId.value.Id.userId = uidAssister;

	auto attackerController = gameEvent->GetPlayerController(myWrapper.hashString("attacker"));
	auto victimController = gameEvent->GetPlayerController(myWrapper.hashString("userid"));
	auto assisterController = gameEvent->GetPlayerController(myWrapper.hashString("assister"));

	if (g_MirvDeathMsgGlobals.Settings.Debug)
	{

		std::vector<std::vector<std::string>> rows = {
			{
				"weapon",
				"attackerName",
				"attackerUserId",
				"victimName",
				"victimUserId",
				"assisterName",
				"assisterUserId",
			},
			{
				gameEvent->GetString(myWrapper.hashString("weapon")),

				nullptr != attackerController ? (char*)((u_char*)attackerController + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName) : "null",
				std::to_string(uidAttacker),

				nullptr != victimController ? (char*)((u_char*)victimController + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName) : "null",
				std::to_string(uidVictim),

				nullptr != assisterController ? (char*)((u_char*)assisterController + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName) : "null",
				std::to_string(uidAssister),
			}
		};

		advancedfx::Message(
			"player_death\n"
			"%s", afxUtils::createTable(rows, " | ", "-").c_str()
		);

	}

	for(std::list<DeathMsgFilterEntry>::iterator it = g_MirvDeathMsgGlobals.Filter.begin(); it != g_MirvDeathMsgGlobals.Filter.end(); it++)
	{
		DeathMsgFilterEntry & e = *it;

		bool attackerBlocked;
		switch(e.attacker.mode)
		{
		case DMBM_ANY:
			attackerBlocked = true;
			break;
		case DMBM_EXCEPT:
			attackerBlocked = !e.attacker.id.EqualsUserId(uidAttacker);
			break;
		case DMBM_EQUAL:
		default:
			attackerBlocked = e.attacker.id.EqualsUserId(uidAttacker);
			break;
		}

		bool victimBlocked;
		switch(e.victim.mode)
		{
		case DMBM_ANY:
			victimBlocked = true;
			break;
		case DMBM_EXCEPT:
			victimBlocked = !e.victim.id.EqualsUserId(uidVictim);
			break;
		case DMBM_EQUAL:
		default:
			victimBlocked = e.victim.id.EqualsUserId(uidVictim);
			break;
		}

		bool assisterBlocked;
		switch(e.assister.mode)
		{
		case DMBM_ANY:
			assisterBlocked = true;
			break;
		case DMBM_EXCEPT:
			assisterBlocked = !e.assister.id.EqualsUserId(uidAssister);
			break;
		case DMBM_EQUAL:
		default:
			assisterBlocked = e.assister.id.EqualsUserId(uidAssister);
			break;
		}

		bool matched = attackerBlocked && victimBlocked && assisterBlocked;

		if(matched)
		{
			myWrapper.ApplyDeathMsgFilterEntry(e);

			uidAttacker = myWrapper.GetInt(myWrapper.hashString("attacker"));
			uidVictim = myWrapper.GetInt(myWrapper.hashString("userid"));
			uidAssister = myWrapper.GetInt(myWrapper.hashString("assister"));

			if (e.lastRule) break;
		}
	}

	if (myWrapper.block.use && myWrapper.block.value) {
		return;
	}

	if (g_MirvDeathMsgGlobals.useHighlightId)
	{
		myWrapper.attacker.isLocal.use = true;
		myWrapper.attacker.isLocal.value = g_MirvDeathMsgGlobals.highlightId.EqualsUserId(uidAttacker);

		myWrapper.victim.isLocal.use = true;
		myWrapper.victim.isLocal.value = g_MirvDeathMsgGlobals.highlightId.EqualsUserId(uidVictim);

		myWrapper.assister.isLocal.use = true;
		myWrapper.assister.isLocal.value = g_MirvDeathMsgGlobals.highlightId.EqualsUserId(uidAssister);
	}

	if (myWrapper.attacker.name.use && nullptr != attackerController) {
		((SOURCESDK::CS2::CUtlString *)((u_char*)attackerController + g_clientDllOffsets.CCSPlayerController.m_sSanitizedPlayerName))->Set(myWrapper.attacker.name.value);
	}

	if (myWrapper.victim.name.use && nullptr != victimController) {
		((SOURCESDK::CS2::CUtlString *)((u_char*)victimController + g_clientDllOffsets.CCSPlayerController.m_sSanitizedPlayerName))->Set(myWrapper.victim.name.value);
	}

	if (myWrapper.assister.name.use && nullptr != assisterController) {
		((SOURCESDK::CS2::CUtlString *)((u_char*)assisterController + g_clientDllOffsets.CCSPlayerController.m_sSanitizedPlayerName))->Set(myWrapper.assister.name.value);
	}

	if (g_MirvDeathMsgGlobals.Lifetime.use)
	{
		myWrapper.lifetime.use = true;
		myWrapper.lifetime.value = g_MirvDeathMsgGlobals.Lifetime.value;
	}

	if (g_MirvDeathMsgGlobals.LifetimeMod.use)
	{
		myWrapper.lifetimeMod.use = true;
		myWrapper.lifetimeMod.value = g_MirvDeathMsgGlobals.LifetimeMod.value;
	}

	if (myWrapper.lifetime.use)
	{
		orgDeathNoticeLifetime = *pDeathNoticeLifetime;
		*pDeathNoticeLifetime = myWrapper.lifetime.value;
	}

	if (myWrapper.lifetimeMod.use)
	{
		orgDeathNoticeLocalPlayerLifetimeMod = *pDeathNoticeLocalPlayerLifetimeMod;
		*pDeathNoticeLocalPlayerLifetimeMod = myWrapper.lifetimeMod.value;
	}

	g_MirvDeathMsgGlobals.activeWrapper = &myWrapper;

    g_Original_handlePlayerDeath(hudDeathNotice, &myWrapper);

	if (myWrapper.lifetimeMod.use) {
		*pDeathNoticeLocalPlayerLifetimeMod = orgDeathNoticeLocalPlayerLifetimeMod;
	}
	if (myWrapper.lifetime.use) {
		*pDeathNoticeLifetime = orgDeathNoticeLifetime;
	}

	g_MirvDeathMsgGlobals.activeWrapper = nullptr;
};


typedef int (__fastcall * Panorama_CLayoutFile_LoadFromFile_t)(void * This, const char * pFilePath, unsigned char _unk02);
typedef unsigned char (__fastcall * Panorama_CStyleProperty_Parse_t)(void * This, void* _unk01, const char * pValueStr);
typedef void (__fastcall * Panorama_CStyleProperty_Clone_t)(void * This, void * pTarget);

bool g_b_In_Panorama_CLayoutFile_LoadFromFile = false;
bool g_b_In_Panorama_CLayoutFile_LoadFromFile_HudReticle = false;

Panorama_CLayoutFile_LoadFromFile_t g_Org_Panorama_CLayoutFile_LoadFromFile = nullptr;
Panorama_CStyleProperty_Parse_t g_Org_Panorama_CStylePropertyForegroundColor_Parse = nullptr;
Panorama_CStyleProperty_Parse_t g_Org_Panorama_CStylePropertyBackgroundColor_Parse = nullptr;
Panorama_CStyleProperty_Parse_t g_Org_Panorama_CStylePropertyBorder_Parse = nullptr;

Panorama_CStyleProperty_Parse_t g_Org_Panorama_CStylePropertyWashColor_Parse = nullptr;
Panorama_CStyleProperty_Clone_t g_Org_Panorama_CStylePropertyWashColor_Clone = nullptr;

std::set<u_char*> g_pHudReticle_WashColor_T;
std::set<u_char*> g_pHudReticle_WashColor_CT;

void SetHudReticleWashColorT(uint32_t value) {
	for(auto it= g_pHudReticle_WashColor_T.begin(); it != g_pHudReticle_WashColor_T.end(); it++) {
		*(uint32_t*)(*it + 0x10) = value;
	}
}

void SetHudReticleWashColorCT(uint32_t value) {
	for(auto it= g_pHudReticle_WashColor_CT.begin(); it != g_pHudReticle_WashColor_CT.end(); it++) {
		*(uint32_t*)(*it + 0x10) = value;
	}
}

int __fastcall My_Panorama_CLayoutFile_LoadFromFile(void * This, const char * pFilePath, unsigned char _unk02) {
	if(0 == strcmp("panorama\\layout\\hud\\huddeathnotice.xml",pFilePath)) {		
		g_b_In_Panorama_CLayoutFile_LoadFromFile = true;
		int result = g_Org_Panorama_CLayoutFile_LoadFromFile(This,pFilePath,_unk02);
		g_b_In_Panorama_CLayoutFile_LoadFromFile = false;
		return result;
	}

	if(0 == strcmp("panorama\\layout\\hud\\hudreticle.xml",pFilePath)) {
		g_pHudReticle_WashColor_T.clear();
		g_pHudReticle_WashColor_CT.clear();
		g_b_In_Panorama_CLayoutFile_LoadFromFile_HudReticle = true;
		int result = g_Org_Panorama_CLayoutFile_LoadFromFile(This,pFilePath,_unk02);
		g_b_In_Panorama_CLayoutFile_LoadFromFile_HudReticle = false;
		return result;
	}

	return g_Org_Panorama_CLayoutFile_LoadFromFile(This,pFilePath,_unk02);
}

unsigned char __fastcall My_Panorama_CStylePropertyForegroundColor_Parse(void * This, void* _unk01, const char * pValueStr) {
	unsigned char result = g_Org_Panorama_CStylePropertyForegroundColor_Parse(This,_unk01,pValueStr);
	if(g_b_In_Panorama_CLayoutFile_LoadFromFile) {
		if(0 == strcmp(pValueStr,"#6f9ce6")) {
			g_myPanoramaWrapper.CTcolor.pointer = (u_char*)This;
		}
		else if(0 == strcmp(pValueStr,"#eabe54")) {
			g_myPanoramaWrapper.Tcolor.pointer = (u_char*)This;
		}		
	}
	return result;
}

unsigned char __fastcall My_Panorama_CStylePropertyBackgroundColor_Parse(void * This, void* _unk01, const char * pValueStr) {
	unsigned char result = g_Org_Panorama_CStylePropertyBackgroundColor_Parse(This,_unk01,pValueStr);
	if(g_b_In_Panorama_CLayoutFile_LoadFromFile) {
		if(0 == strcmp(pValueStr,"#000000a0")) {
			g_myPanoramaWrapper.BackgroundColor.pointer = (u_char*)This;
		}
		else if(0 == strcmp(pValueStr,"#000000e7")) {
			g_myPanoramaWrapper.LocalBackgroundColor.pointer = (u_char*)This;
		}		
	}
	return result;
}

unsigned char __fastcall My_Panorama_CStylePropertyBorder_Parse(void * This, void* _unk01, const char * pValueStr) {
	unsigned char result = g_Org_Panorama_CStylePropertyBorder_Parse(This,_unk01,pValueStr);
	if(g_b_In_Panorama_CLayoutFile_LoadFromFile) {
		if(0 == strcmp(pValueStr,"2px solid #e10000")) {
			g_myPanoramaWrapper.BorderColor.pointer = (u_char*)This;
		}
	}
	return result;
}


unsigned char __fastcall My_Panorama_CStylePropertyWashColor_Parse(void * This, void* _unk01, const char * pValueStr) {
	unsigned char result = g_Org_Panorama_CStylePropertyWashColor_Parse(This,_unk01,pValueStr);
	if(g_b_In_Panorama_CLayoutFile_LoadFromFile_HudReticle) {
		if(0 == strcmp(pValueStr,"rgb(150, 200, 250)")) {
			g_pHudReticle_WashColor_CT.emplace((u_char*)This);
		}
		else if(0 == strcmp(pValueStr,"#eabe54")) {
			g_pHudReticle_WashColor_T.emplace((u_char*)This);
		}		
	}
	return result;
}

void __fastcall My_Panorama_CStylePropertyWashColor_Clone(void * This, void * pTarget) {
	g_Org_Panorama_CStylePropertyWashColor_Clone(This, pTarget);
	if(g_b_In_Panorama_CLayoutFile_LoadFromFile_HudReticle) {
		auto itCT = g_pHudReticle_WashColor_CT.find((u_char*)This);
		if(itCT != g_pHudReticle_WashColor_CT.end()) {
			g_pHudReticle_WashColor_CT.emplace((u_char*)pTarget);
		}
		auto itT = g_pHudReticle_WashColor_T.find((u_char*)This);
		if(itT != g_pHudReticle_WashColor_T.end()) {
			g_pHudReticle_WashColor_T.emplace((u_char*)pTarget);
		}
	}
}

void getDeathMsgAddrs(HMODULE clientDll) {
	// can be found with strings like "attacker" and "userid", etc. it basically takes all info from player_death event
	if (auto addr = getAddress(clientDll, "48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 48 8D AC 24 10 E0 FF FF B8 F0 ?? ?? ?? E8 ?? ?? ?? ?? 48 2B")) {
		g_Original_handlePlayerDeath = (g_Original_handlePlayerDeath_t)(addr);
	} else ErrorBox(MkErrStr(__FILE__, __LINE__));

	// called in multiple places with strings like "userid", "attacker", etc. as second argument
	// e.g. in function above too
	if (auto addr = getAddress(clientDll, "48 8B 58 ?? 0F 29 B4 24 C0 20 00 00 E8 ?? ?? ?? ??")) {
		g_Original_hashString = (g_Original_hashString_t)(addr + 12 + 5 + *(int32_t*)(addr + 12 + 1));
	} else ErrorBox(MkErrStr(__FILE__, __LINE__));	

	// snippet from function handlePlayerDeath above	
	//   if (*(char *)(lVar17 + 0xb8) == '\0') {
	//     uVar18 = FUN_1808a1a00();
	//
	//     iVar10 = FUN_1808af610(uVar18); // the one we need called here, it returns local steamid, function has 2 xrefs
	//									   // later there is check if attackersteamid is equal to local one
	//
	//     if (((iVar10 != 0) && (plVar14 != (longlong *)0x0)) &&
	//        (piVar13 = (int *)FUN_18056a170(plVar14,&uStackX_20), *piVar13 == iVar10)) {
	//       bVar4 = true;
	//     }
	//   }
	size_t g_Original_getLocalSteamId_addr = getAddress(clientDll,"40 53 48 83 EC ?? 8B 51 ?? 48 8B D9 83 FA FF 0F 84 ?? ?? ?? ?? 4C 8B 0D ?? ?? ?? ??");
	if (0 == g_Original_getLocalSteamId_addr) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));	
	};

	g_Original_getLocalSteamId = (g_Original_getLocalSteamId_t)(g_Original_getLocalSteamId_addr);

	size_t populateFromPlayerSlot = getAddress(clientDll, "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B CA E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 0F 84 ?? ?? ?? ??");
	if (0 == populateFromPlayerSlot) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
	}
	g_Original_CSGOAvatarImage_PopulateFromPlayerSlot = (CSGOAvatarImage_PopulateFromPlayerSlot_t)populateFromPlayerSlot;
	g_CSGO_GetPlayerFromSlot = (CSGO_GetPlayerFromSlot_t)relativeCallTarget(populateFromPlayerSlot + 0x0f);

	size_t populateFromPlayerSlotNative = getAddress(clientDll, "48 89 5C 24 08 57 48 83 EC 20 48 8B F9 8B CA E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 0F 84 ?? ?? ?? ?? 8B 90 F8 03 00 00 C1 EA 08 F6 C2 01");
	if (0 == populateFromPlayerSlotNative) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
	}
	g_Original_CSGOAvatarImage_PopulateFromPlayerSlotNative = (CSGOAvatarImage_PopulateFromPlayerSlot_t)populateFromPlayerSlotNative;

	size_t populateFromAvatarHandle = getAddress(clientDll, "48 89 5C 24 08 57 48 83 EC 20 0F B7 FA B8 FF FF 00 00 48 8B D9 66 3B F8");
	if (0 == populateFromAvatarHandle) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
	}
	g_Original_CSGOAvatarImage_PopulateFromAvatarHandle = (CSGOAvatarImage_PopulateFromAvatarHandle_t)populateFromAvatarHandle;

	size_t populateFromSteamId = getAddress(clientDll, "48 89 5C 24 10 57 48 83 EC 20 48 8B 02 48 8B D9 48 85 C0 48 8D 0D ?? ?? ?? ?? 48 8B FA 48 0F 45 C8 E8 ?? ?? ?? ??");
	if (0 == populateFromSteamId) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
	}
	g_CSGOAvatarImage_MakeAvatarImageSource = (CSGOAvatarImage_MakeAvatarImageSource_t)relativeCallTarget(populateFromSteamId + 0x21);

	size_t getPlayerXuidStringFromPlayerSlot = getAddress(clientDll, "40 53 48 83 EC 20 48 63 DA 8B CB E8 ?? ?? ?? ?? 48 85 C0 74 ?? 8B 88 F8 03 00 00 C1 E9 08 F6 C1 01");
	if (0 == getPlayerXuidStringFromPlayerSlot) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
	}
	g_CSGO_GetPlayerXuid = (CSGO_GetPlayerXuid_t)relativeCallTarget(getPlayerXuidStringFromPlayerSlot + 0x2b);
};

bool getPanoramaAddrsFromClient(HMODULE clientDll) {
	// credit https://github.com/danielkrupinski/Osiris
	
/* In the middle of big function with MULTIPLE (3+) references to "Attempted to cast panel '%s' to type '%s'" and multiple to "file://{images}/%s.png":
        }
LAB_1809a7daf:
        if (DAT_181fc46d8 != (longlong *)0x0) {
          local_230 = FUN_1809bf960; <-- next 2 sigs in this one!!!
          local_238 = lVar12;
          (**(code **)(*DAT_181fc46d8 + 0x120))(DAT_181fc46d8,DAT_181c51f4c,plVar11,&local_238);
        }
      }
LAB_1809a7de1
*/
/*
                             LAB_1809bf9e6                                   XREF[1]:     1809bf9d9(j)  
       1809bf9e6 48 8b 4f 08     MOV        RCX,qword ptr [RDI + 0x8]
       1809bf9ea 4c 8d 05        LEA        R8,[DAT_1814ed000]
                 0f d6 b2 00
       1809bf9f1 0f b7 12        MOVZX      EDX=>DAT_181e808b8,word ptr [RDX]
       1809bf9f4 48 8b 01        MOV        RAX,qword ptr [RCX]
       1809bf9f7 ff 90 d0        CALL       qword ptr [RAX + 0x8d0]
                 08 00 00
       1809bf9fd 48 8b f0        MOV        RSI,RAX
       1809bfa00 48 85 c0        TEST       RAX,RAX
*/
	if (auto addr = getAddress(clientDll,"48 8b 4f 08 4c 8d 05 ?? ?? ?? ?? 0f b7 12 48 8b 01 ff 90 ?? ?? ?? ?? 48 8b f0 48 85 c0"); addr != 0) {
		CS2::PanoramaUIPanel::getAttributeString = *(int32_t*)((unsigned char*)addr + 19);
	} else {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
		return false;
	}

/*
                             LAB_1809bfa6a                                   XREF[1]:     1809bfa5d(j)  
       1809bfa6a 48 8b 4f 08     MOV        RCX,qword ptr [RDI + 0x8]
       1809bfa6e 4c 8d 05        LEA        R8,[DAT_1814ed000]
                 8b d5 b2 00
       1809bfa75 0f b7 13        MOVZX      EDX,word ptr [RBX]=>DAT_181e808b8
       1809bfa78 48 8b 01        MOV        RAX,qword ptr [RCX]
       1809bfa7b ff 90 00        CALL       qword ptr [RAX + 0x900]
                 09 00 00
       1809bfa81 b0 01           MOV        AL,0x1
       1809bfa83 e9 1a ff        JMP        LAB_1809bf9a2
                 ff ff
*/
	if (auto addr = getAddress(clientDll,"48 8b 4f 08 4c 8d 05 ?? ?? ?? ?? 0f b7 13 48 8b 01 ff 90 ?? ?? ?? ?? b0 01 e9 ?? ?? ?? ??"); addr != 0) {
		CS2::PanoramaUIPanel::setAttributeString = *(int32_t*)((unsigned char*)addr + 19);
	} else {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
		return false;
	}

	// "Can\'t call Panorama Symbol constructor outside panorama.dll until UIEngine is i nitialized! Symbol: %s"
	if (auto addr = getAddress(clientDll,"48 8B 01 4C 8B C3 BA ?? ?? ?? ?? FF 90 ?? ?? ?? ?? 48 8B 5C 24 ?? 66 89 07"); addr != 0) {
		CS2::PanoramaUIEngine::makeSymbol = *(int32_t*)((unsigned char*)addr + 13);
	} else {
		ErrorBox(MkErrStr(__FILE__, __LINE__));
		return false;
	}

	// function has "file://{resources}/layout/hud/hud.xml" string and also references CCSGO_Hud vftable
	// hudpanel is DAT that param_1 assigned to     
	size_t g_HudPanel_addr = getAddress(clientDll, "48 89 AE ?? ?? ?? ?? 89 AE ?? ?? ?? ?? C6 86 ?? ?? ?? ?? 01 48 89 86 ?? ?? ?? ?? 48 89 35 ?? ?? ?? ?? e8 ?? ?? ?? ??");
	if (g_HudPanel_addr == 0) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));	
		return false;
	} else {
		g_HudPanel_addr += 30;
	};

	// function has CreatePanelWithCurrentContext string
	// engine is DAT that param_1 assigned to
	size_t g_CUIEngine_addr = getAddress(clientDll, "48 89 78 ?? 48 89 0D ?? ?? ?? ??");
	if (g_CUIEngine_addr == 0) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));	
		return false;
	} else {
		g_CUIEngine_addr += 7;
	};

	uint32_t g_HudPanel_offset;
	std::memcpy(&g_HudPanel_offset, (void*)(g_HudPanel_addr), sizeof(g_HudPanel_offset));
	g_myPanoramaWrapper.pHudPanel = (u_char**)(g_HudPanel_addr + g_HudPanel_offset + 4);

	uint32_t g_CUIEngine_offset;
	std::memcpy(&g_CUIEngine_offset, (void*)(g_CUIEngine_addr), sizeof(g_CUIEngine_offset));
	g_myPanoramaWrapper.pUIEngine = (u_char**)(g_CUIEngine_addr + g_CUIEngine_offset + 4);

	return true;
};

bool getPanoramaAddrs(HMODULE panoramaDll) {

	// Refernces "CLayoutFile::LoadFromFile" string.
	g_Org_Panorama_CLayoutFile_LoadFromFile = (Panorama_CLayoutFile_LoadFromFile_t)getAddress(panoramaDll,"48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 60 48 8D 05 ?? ?? ?? ?? 48 C7 45 D0 F4 03 00 00 48");
	if(nullptr == g_Org_Panorama_CLayoutFile_LoadFromFile) {
		ErrorBox(MkErrStr(__FILE__, __LINE__));	
		return false;
	}

	{
		g_CUIEngine_RunScript = (g_CUIEngine_RunScript_t)getAddress(panoramaDll, "48 89 5C 24 ?? 4C 89 4C 24 ?? 48 89 54 24 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D");
		if (nullptr == g_CUIEngine_RunScript) {
			ErrorBox(MkErrStr(__FILE__, __LINE__));
			return false;
		}
	}

	{
		void **vtable = (void**)Afx::BinUtils::FindClassVtable(panoramaDll,".?AVCStylePropertyForegroundColor@panorama@@",0,0);
		if(nullptr == vtable) {
			ErrorBox(MkErrStr(__FILE__, __LINE__));	
			return false;
		}
		g_Org_Panorama_CStylePropertyForegroundColor_Parse = (Panorama_CStyleProperty_Parse_t)vtable[6];
	}

	{
		void **vtable = (void**)Afx::BinUtils::FindClassVtable(panoramaDll,".?AVCStylePropertyBackgroundColor@panorama@@",0,0);
		if(nullptr == vtable) {
			ErrorBox(MkErrStr(__FILE__, __LINE__));	
			return false;
		}
		g_Org_Panorama_CStylePropertyBackgroundColor_Parse = (Panorama_CStyleProperty_Parse_t)vtable[6];
	}

	{
		void **vtable = (void**)Afx::BinUtils::FindClassVtable(panoramaDll,".?AVCStylePropertyBorder@panorama@@",0,0);
		if(nullptr == vtable) {
			ErrorBox(MkErrStr(__FILE__, __LINE__));	
			return false;
		}
		g_Org_Panorama_CStylePropertyBorder_Parse = (Panorama_CStyleProperty_Parse_t)vtable[6];
	}

	{
		void **vtable = (void**)Afx::BinUtils::FindClassVtable(panoramaDll,".?AVCStylePropertyWashColor@panorama@@",0,0);
		if(nullptr == vtable) {
			ErrorBox(MkErrStr(__FILE__, __LINE__));	
			return false;
		}
		g_Org_Panorama_CStylePropertyWashColor_Clone = (Panorama_CStyleProperty_Clone_t)vtable[1];
		g_Org_Panorama_CStylePropertyWashColor_Parse = (Panorama_CStyleProperty_Parse_t)vtable[6];
	}		

	{
		g_CStylePropertyOpacity_vtable = (void**)Afx::BinUtils::FindClassVtable(panoramaDll,".?AVCStylePropertyOpacity@panorama@@",0,0);
		if(nullptr == g_CStylePropertyOpacity_vtable) {
			ErrorBox(MkErrStr(__FILE__, __LINE__));	
			return false;
		}
	}		

	{
		// after "Need to increase size of static g_StylePropertyRegistrations (MAX_PANORAMA_STYLE_SYMBOLS) before registering more styles, failed on %s"
		// lVar11 = FUN_180161290(); <----- we are looking for that it returns
		// ....
		// FUN_1800bce60(lVar11 + 8,&local_48,&local_68);
		auto addr = getAddress(panoramaDll, "7F ?? 48 8D 05 ?? ?? ?? ?? 48 83 C4 ?? C3");
		if (0 == addr)
			ErrorBox(MkErrStr(__FILE__, __LINE__));	
		else {
			auto out = addr + 7 + *(int32_t*)(addr + 5);
			g_PanoramaStylePropertySymbols.symbols = (SOURCESDK::CS2::CUtlMap<SOURCESDK::CS2::CUtlString, uint8_t>*)(out + 8 + 2);
		}
	}

	{
		// Can be found in constructor for any CStyleProperty
		// e.g. see 44th fn in vtable for CPanelStyle
		auto addr = getAddress(panoramaDll, "E8 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 45 ?? EB");
		if (addr) {
			g_CPanelStyleSetStyleProperty = (g_CPanelStyleSetStyleProperty_t)(addr + 5 + *(int32_t*)(addr + 1));
		} else 
			ErrorBox(MkErrStr(__FILE__, __LINE__));	
	}

	return true;
};

void HookPanorama(HMODULE panoramaDll)
{
	if (g_myPanoramaWrapper.hooked) return;

	if (!getPanoramaAddrs(panoramaDll)) return;

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourAttach(&(PVOID&)g_Org_Panorama_CLayoutFile_LoadFromFile, My_Panorama_CLayoutFile_LoadFromFile);
	DetourAttach(&(PVOID&)g_Org_Panorama_CStylePropertyForegroundColor_Parse, My_Panorama_CStylePropertyForegroundColor_Parse);
	DetourAttach(&(PVOID&)g_Org_Panorama_CStylePropertyBackgroundColor_Parse, My_Panorama_CStylePropertyBackgroundColor_Parse);
	DetourAttach(&(PVOID&)g_Org_Panorama_CStylePropertyBorder_Parse, My_Panorama_CStylePropertyBorder_Parse);
	DetourAttach(&(PVOID&)g_Org_Panorama_CStylePropertyWashColor_Clone, My_Panorama_CStylePropertyWashColor_Clone);
	DetourAttach(&(PVOID&)g_Org_Panorama_CStylePropertyWashColor_Parse, My_Panorama_CStylePropertyWashColor_Parse);

	if(NO_ERROR != DetourTransactionCommit()) {
		ErrorBox("Failed to detour panorama functions.");
		return;
	}

	g_myPanoramaWrapper.hooked = true;
};

void HookDeathMsg(HMODULE clientDll) {
	if (g_MirvDeathMsgGlobals.hooked) return;

    getDeathMsgAddrs(clientDll);
	if (!getPanoramaAddrsFromClient(clientDll)) return;

	DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourAttach(&(PVOID&)g_Original_handlePlayerDeath, handleDeathnotice);
    DetourAttach(&(PVOID&)g_Original_getLocalSteamId, getLocalSteamId);
	DetourAttach(&(PVOID&)g_Original_CSGOAvatarImage_PopulateFromPlayerSlot, New_CSGOAvatarImage_PopulateFromPlayerSlot);
	DetourAttach(&(PVOID&)g_Original_CSGOAvatarImage_PopulateFromPlayerSlotNative, New_CSGOAvatarImage_PopulateFromPlayerSlotNative);
	DetourAttach(&(PVOID&)g_Original_CSGOAvatarImage_PopulateFromAvatarHandle, New_CSGOAvatarImage_PopulateFromAvatarHandle);

	if(NO_ERROR != DetourTransactionCommit()) {
		ErrorBox("Failed to detour DeathMsg functions.");
		return;
	}

	g_MirvDeathMsgGlobals.hooked = true;
};

void deathMsgId_PrintHelp_Console(const char * cmd)
{
	advancedfx::Message(
		"%s accepts the following as <id...>:\n"
		"<iNumber> - UserID. Example: 9\n"
		"x<iNumber> - XUID. Example: x76561198106931330\n"
		"k<iNumber> - Spectator key number.\n"
		"trace - UserID from a screen trace (e.g. current POV).\n"
		"xTrace - XUID from a screen trace (e.g. current POV).\n"
		"We recommend getting the numbers from the output of \"mirv_deathmsg help players\".\n"
		, cmd
	);
};

void deathMsgPlayers_PrintHelp_Console()
{
    int highestIndex = GetHighestEntityIndex();

	std::vector<std::vector<std::string>> rows = {
		{"name", "userid", "xuid", "speckey"}, {}
	};

    for(int i = 0; i < highestIndex + 1; i++) {
        if(auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList,i)) {
			if(!ent->IsPlayerController()) continue;

			auto teamNumber = *(int*)((u_char*)(ent) + g_clientDllOffsets.C_BaseEntity.m_iTeamNum);
			if (0 == teamNumber || 1 == teamNumber) continue;

			// I know it's nested loop, but on this scale it doesn't matter
			auto playerInfo = getPlayerInfoFromControllerIndex(i);

			rows.push_back({
				playerInfo.name,
				std::to_string(playerInfo.userId), // apparently in CS2 userid is playercontroller entityindex - 1
				std::string("x").append(std::to_string(playerInfo.xuid)),
				std::string("k").append(std::to_string(playerInfo.specKey))
			});
        }
    }

	if (rows.size() == 2) return;

	advancedfx::Message(
		"%s", afxUtils::createTable(rows, " | ", "-").c_str()
	);
};

struct CS2_MirvDeathMsg : MirvDeathMsg {
	bool colors (IWrpCommandArgs * args) 
	{
		int argc = args->ArgC();
		const char * arg0 = args->ArgV(0);

		std::string colors = "";

		for (int i = 0; i < afxBasicColors.size(); i++)
		{
			auto color = afxBasicColors[i];
			colors.append(color.name);
			if (i < afxBasicColors.size() - 1) colors.append(", ");
		}

		const char* options = 
			"Where <option> is one of:\n"
			"default - use default game color\n"
			"<0-255> <0-255> <0-255> <0-255> - color in RGBA format e.g. 255 0 0 255\n"
			"<color> - one of the default colors e.g. red\n";

		if (3 > argc)
		{
			advancedfx::Message(
				"%s colors ct <option> - Control CT color.\n"
				"%s colors t <option> - Control T color.\n"
				"%s colors border <option> - Control border color of local player.\n"
				"%s colors background <option> - Control background color.\n"
				"%s colors backgroundLocal <option> - Control background color of local player.\n"
				"\n"
				"%s"
				"\n"
				"Available colors:\n"
				"%s\n"
				, arg0, arg0, arg0, arg0, arg0, options, colors.c_str()
			);
			return true;	
		}

		const char* arg2 = args->ArgV(2);
		
		if (0 == _stricmp("ct", arg2))
		{
			if (3 == argc)
			{
				advancedfx::Message(
					"%s colors %s <option> - Control CT color in death messages.\n"
					"Current value: %s\n"
					, arg0, arg2
					, g_myPanoramaWrapper.CTcolor.use ? g_myPanoramaWrapper.CTcolor.userValue.c_str() : "default"
				);
				return true;
			}

			if (4 == argc)
			{
				g_myPanoramaWrapper.CTcolor.setColor(args->ArgV(3));
				g_myPanoramaWrapper.applyColors();
				return true;
			}

			if (7 == argc)
			{
				advancedfx::CSubCommandArgs subArgs(args, 3);
				g_myPanoramaWrapper.CTcolor.setColor(&subArgs);
				g_myPanoramaWrapper.applyColors();
				return true;
			}
		}

		if (0 == _stricmp("t", arg2))
		{
			if (3 == argc)
			{
				advancedfx::Message(
					"%s colors %s <option> - Control T color in death messages.\n"
					"Current value: %s\n"
					, arg0, arg2
					, g_myPanoramaWrapper.Tcolor.use ? g_myPanoramaWrapper.Tcolor.userValue.c_str() : "default"
				);
				return true;
			}

			if (4 == argc)
			{
				g_myPanoramaWrapper.Tcolor.setColor(args->ArgV(3));
				g_myPanoramaWrapper.applyColors();
				return true;
			}

			if (7 == argc)
			{
				advancedfx::CSubCommandArgs subArgs(args, 3);
				g_myPanoramaWrapper.Tcolor.setColor(&subArgs);
				g_myPanoramaWrapper.applyColors();
				return true;
			}
		}

		if (0 == _stricmp("border", arg2))
		{
			if (3 == argc)
			{
				advancedfx::Message(
					"%s colors %s <option> - Control border color of local player in death messages.\n"
					"Current value: %s\n"
					, arg0, arg2
					, g_myPanoramaWrapper.BorderColor.use ? g_myPanoramaWrapper.BorderColor.userValue.c_str() : "default"
				);
				return true;
			}

			if (4 == argc)
			{
				g_myPanoramaWrapper.BorderColor.setColor(args->ArgV(3));
				g_myPanoramaWrapper.applyColors();
				return true;
			}

			if (7 == argc)
			{
				advancedfx::CSubCommandArgs subArgs(args, 3);
				g_myPanoramaWrapper.BorderColor.setColor(&subArgs);
				g_myPanoramaWrapper.applyColors();
				return true;
			}
		}

		if (0 == _stricmp("background", arg2))
		{
			if (3 == argc)
			{
				advancedfx::Message(
					"%s colors %s <option> - Control background color of death messages.\n"
					"Current value: %s\n"
					, arg0, arg2
					, g_myPanoramaWrapper.BackgroundColor.use ? g_myPanoramaWrapper.BackgroundColor.userValue.c_str() : "default"
				);

				return true;
			}

			if (4 == argc)
			{
				g_myPanoramaWrapper.BackgroundColor.setColor(args->ArgV(3));
				g_myPanoramaWrapper.applyColors();
				return true;
			}

			if (7 == argc)
			{
				advancedfx::CSubCommandArgs subArgs(args, 3);
				g_myPanoramaWrapper.BackgroundColor.setColor(&subArgs);
				g_myPanoramaWrapper.applyColors();
				return true;
			}
		}

		if (0 == _stricmp("backgroundLocal", arg2))
		{
			if (3 == argc)
			{
				advancedfx::Message(
					"%s colors %s <option> - Control background color of local player.\n"
					"Current value: %s\n"
					, arg0, arg2
					, g_myPanoramaWrapper.LocalBackgroundColor.use ? g_myPanoramaWrapper.LocalBackgroundColor.userValue.c_str() : "default"
				);

				return true;
			}

			if (4 == argc)
			{
				g_myPanoramaWrapper.LocalBackgroundColor.setColor(args->ArgV(3));
				g_myPanoramaWrapper.applyColors();
				return true;
			}

			if (7 == argc)
			{
				advancedfx::CSubCommandArgs subArgs(args, 3);
				g_myPanoramaWrapper.LocalBackgroundColor.setColor(&subArgs);
				g_myPanoramaWrapper.applyColors();
				return true;
			}
		}

		advancedfx::Message(
			"%s colors ct <option> - Control CT color.\n"
			"%s colors t <option> - Control T color.\n"
			"%s colors border <option> - Control border color of local player.\n"
			"%s colors background <option> - Control background color.\n"
			"%s colors backgroundLocal <option> - Control background color of local player.\n"
			"\n"
			"%s"
			"\n"
			"Available colors:\n"
			"%s\n"
			, arg0, arg0, arg0, arg0, arg0, options, colors.c_str()
		);
		return true;
	};
} g_MirvDeathMsg;

bool mirvDeathMsg_Console(advancedfx::ICommandArgs* args)
{
	const auto arg0 = args->ArgV(0);
	int argc = args->ArgC();

	if (2 <= argc)
	{
		const char * arg1 = args->ArgV(1);
		if (0 == _stricmp("clear", arg1))
		{
			auto result = g_myPanoramaWrapper.clearDeathnotices();
			return true;
		} else
		if (0 == _stricmp("filter", arg1))
		{
			return g_MirvDeathMsg.filter(args, g_MirvDeathMsgGlobals);
		} else
		if (0 == _stricmp("lifetime", arg1)) {
			return g_MirvDeathMsg.lifetime(args, g_MirvDeathMsgGlobals);
		} else
		if (0 == _stricmp("lifetimeMod", arg1)) {
			return g_MirvDeathMsg.lifetimeMod(args, g_MirvDeathMsgGlobals);
		} else
		if (0 == _stricmp("localPlayer", arg1)) {
			return g_MirvDeathMsg.localPlayer(args, g_MirvDeathMsgGlobals);
		} else
		if (0 == _stricmp("debug", arg1))
		{
			return g_MirvDeathMsg.debug(args, g_MirvDeathMsgGlobals);
		} else
		if (0 == _stricmp("colors", arg1)) {
			return g_MirvDeathMsg.colors(args);
		}
		if (0 == _stricmp("help", arg1))
		{
			if (3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				if (0 == _stricmp("id", arg2))
				{
					deathMsgId_PrintHelp_Console(arg0);
					return true;
				}

				if (0 == _stricmp("players", arg2))
				{
					deathMsgPlayers_PrintHelp_Console();
					return true;
				}

			}
			advancedfx::Message(
				"%s help id - Print help on <id...> usage.\n"
				"%s help players - Print available player ids.\n"
				, arg0, arg0, arg0
			);
			return true;
		}
	}

	advancedfx::Message(
		"%s clear - Clears all deathnotices.\n"
		"%s filter [...] - Filter death messages.\n"
		"%s lifetime [...] - Controls lifetime of death messages.\n"
		"%s lifetimeMod [...] - Controls lifetime modifier of death messages for the \"local\" player.\n"
		"%s localPlayer [...] - Controls what is considered \"local\" player (and thus highlighted in death notices).\n"
		"%s debug [...] - Enable / Disable debug spew upon death messages.\n"
		"%s colors [...] - Controls colors of death messages.\n"
		"%s help [...] - Print help.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
	return true;
};

CON_COMMAND(mirv_deathmsg, "controls death notification options")
{
	mirvDeathMsg_Console(args);
};

struct ScoreboardRankOverride {
	std::string ratingType;
	int score = 0;
	int wins = 10;
	bool use = false;
};

std::map<uint64_t, ScoreboardRankOverride> g_ScoreboardRankOverrides;

struct AvatarOverride {
	uint64_t replacementSteamId = 0;
	bool use = false;
};

std::map<uint64_t, AvatarOverride> g_AvatarOverrides;

CSGOAvatarImage_PopulateFromPlayerSlot_t g_Original_CSGOAvatarImage_PopulateFromPlayerSlot = nullptr;
CSGOAvatarImage_PopulateFromPlayerSlot_t g_Original_CSGOAvatarImage_PopulateFromPlayerSlotNative = nullptr;
CSGOAvatarImage_PopulateFromAvatarHandle_t g_Original_CSGOAvatarImage_PopulateFromAvatarHandle = nullptr;
CSGOAvatarImage_SetImageSource_t g_Original_CSGOAvatarImage_SetImageSource = nullptr;
CSGOAvatarImage_MakeAvatarImageSource_t g_CSGOAvatarImage_MakeAvatarImageSource = nullptr;
CSGO_GetPlayerFromSlot_t g_CSGO_GetPlayerFromSlot = nullptr;
CSGO_GetPlayerXuid_t g_CSGO_GetPlayerXuid = nullptr;
std::map<void*, uint64_t> g_AvatarPanelReplacementByPanel;
bool g_CSGOAvatarImage_SetImageSourceHookAttempted = false;

size_t relativeCallTarget(size_t instruction)
{
	return instruction + 5 + *(int32_t*)(instruction + 1);
}

uint64_t getAvatarReplacementForPlayerSlot(int playerSlot, bool& playerKnown)
{
	constexpr uint64_t steamId64IndividualBase = 76561197960265728ULL;
	playerKnown = false;
	if (!g_CSGO_GetPlayerFromSlot || !g_CSGO_GetPlayerXuid) return 0;

	void* player = g_CSGO_GetPlayerFromSlot(playerSlot);
	if (!player) return 0;

	const uint64_t xuid = g_CSGO_GetPlayerXuid(player);
	if (0 == xuid) return 0;
	playerKnown = true;

	auto it = g_AvatarOverrides.find(xuid);
	if (it == g_AvatarOverrides.end() && xuid < 0x100000000ULL) {
		it = g_AvatarOverrides.find(steamId64IndividualBase + xuid);
	}
	if (it == g_AvatarOverrides.end() || !it->second.use) return 0;

	return it->second.replacementSteamId;
}

bool makeAvatarImageSourceFromSteamId(uint64_t steamId, void*& imageSource)
{
	if (!g_CSGOAvatarImage_MakeAvatarImageSource) return false;

	char steamIdString[32];
	_snprintf_s(steamIdString, _TRUNCATE, "%llu", (unsigned long long)steamId);

	imageSource = g_CSGOAvatarImage_MakeAvatarImageSource(steamIdString);
	return nullptr != imageSource;
}

void ensureAvatarImageSourceHook(void* panel)
{
	if (g_CSGOAvatarImage_SetImageSourceHookAttempted || !panel) return;

	g_CSGOAvatarImage_SetImageSourceHookAttempted = true;

	auto vtable = *(void***)panel;
	g_Original_CSGOAvatarImage_SetImageSource = (CSGOAvatarImage_SetImageSource_t)vtable[0x2a0 / sizeof(void*)];
	if (!g_Original_CSGOAvatarImage_SetImageSource) return;

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)g_Original_CSGOAvatarImage_SetImageSource, New_CSGOAvatarImage_SetImageSource);
	if (NO_ERROR != DetourTransactionCommit()) {
		g_Original_CSGOAvatarImage_SetImageSource = (CSGOAvatarImage_SetImageSource_t)vtable[0x2a0 / sizeof(void*)];
	}
}

bool setAvatarImageFromSteamId(void* panel, uint64_t steamId)
{
	if (!panel || !g_CSGOAvatarImage_MakeAvatarImageSource) return false;

	void* imageSource = nullptr;
	if (!makeAvatarImageSourceFromSteamId(steamId, imageSource)) return false;

	auto vtable = *(void***)panel;
	ensureAvatarImageSourceHook(panel);
	auto setImageSource = (CSGOAvatarImage_SetImageSource_t)vtable[0x2a0 / sizeof(void*)];
	return setImageSource(panel, &imageSource);
}

bool __fastcall New_CSGOAvatarImage_SetImageSource(void* This, void* imageSource)
{
	const auto it = g_AvatarPanelReplacementByPanel.find(This);
	if (it != g_AvatarPanelReplacementByPanel.end()) {
		void* replacementSource = nullptr;
		if (makeAvatarImageSourceFromSteamId(it->second, replacementSource)) {
			return g_Original_CSGOAvatarImage_SetImageSource(This, &replacementSource);
		}
	}

	return g_Original_CSGOAvatarImage_SetImageSource(This, imageSource);
}

bool __fastcall New_CSGOAvatarImage_PopulateFromPlayerSlot(void* This, int playerSlot)
{
	bool playerKnown = false;
	const uint64_t replacementSteamId = getAvatarReplacementForPlayerSlot(playerSlot, playerKnown);
	if (replacementSteamId) {
		g_AvatarPanelReplacementByPanel[This] = replacementSteamId;
		setAvatarImageFromSteamId(This, replacementSteamId);
		return true;
	}

	if (playerKnown) g_AvatarPanelReplacementByPanel.erase(This);
	return g_Original_CSGOAvatarImage_PopulateFromPlayerSlot(This, playerSlot);
}

bool __fastcall New_CSGOAvatarImage_PopulateFromPlayerSlotNative(void* This, int playerSlot)
{
	bool playerKnown = false;
	const uint64_t replacementSteamId = getAvatarReplacementForPlayerSlot(playerSlot, playerKnown);
	if (replacementSteamId) {
		g_AvatarPanelReplacementByPanel[This] = replacementSteamId;
		setAvatarImageFromSteamId(This, replacementSteamId);
		return true;
	}

	if (playerKnown) g_AvatarPanelReplacementByPanel.erase(This);
	return g_Original_CSGOAvatarImage_PopulateFromPlayerSlotNative(This, playerSlot);
}

bool __fastcall New_CSGOAvatarImage_PopulateFromAvatarHandle(void* This, unsigned short avatarHandle)
{
	const auto it = g_AvatarPanelReplacementByPanel.find(This);
	if (it != g_AvatarPanelReplacementByPanel.end() && setAvatarImageFromSteamId(This, it->second)) {
		return true;
	}

	return g_Original_CSGOAvatarImage_PopulateFromAvatarHandle(This, avatarHandle);
}

bool parseXuidArg(const char* arg, uint64_t& outXuid) {
	if (nullptr == arg || arg[0] == '\0') return false;
	outXuid = StringIBeginsWith(arg, "x") ? strtoull(arg + 1, nullptr, 10) : strtoull(arg, nullptr, 10);
	return 0 != outXuid;
}

std::string jsEscapeSingleQuotedString(const std::string& value) {
	std::string result;
	result.reserve(value.size());

	for (char c : value) {
		switch (c) {
		case '\\': result.append("\\\\"); break;
		case '\'': result.append("\\'"); break;
		case '\n': result.append("\\n"); break;
		case '\r': result.append("\\r"); break;
		default: result.push_back(c); break;
		}
	}

	return result;
}

u_char* findScoreboardPanel() {
	auto hudPanel = g_myPanoramaWrapper.getHudPanel();
	if (!hudPanel) return nullptr;

	if (auto scoreboard = g_myPanoramaWrapper.findChildInLayoutFile(hudPanel, "Scoreboard")) {
		return scoreboard;
	}

	auto scoreboards = g_myPanoramaWrapper.findChildrenInLayoutFileByClassName(hudPanel, "CSGOScoreboard");
	if (!scoreboards.empty()) {
		return scoreboards[0];
	}

	return hudPanel;
}

bool applyScoreboardRankOverrides() {
	auto contextPanel = findScoreboardPanel();
	if (!contextPanel) return false;

	std::ostringstream script;
	script
		<< "(function(){"
		<< "var root=$('#Scoreboard')||$.GetContextPanel();"
		<< "if(!root||!root.IsValid||!root.IsValid())return;"
		<< "var data={";

	bool first = true;
	for (const auto& entry : g_ScoreboardRankOverrides) {
		if (!entry.second.use) continue;
		if (!first) script << ",";
		first = false;
		script
			<< "'" << entry.first << "':{"
			<< "ratingType:'" << jsEscapeSingleQuotedString(entry.second.ratingType) << "',"
			<< "score:" << entry.second.score << ","
			<< "wins:" << entry.second.wins
			<< "}";
	}

	script
		<< "};"
		<< "root.Data().mirvScoreboardRankOverrides=data;"
		<< "var apply=function(){"
		<< "var root=$('#Scoreboard')||$.GetContextPanel();"
		<< "if(!root||!root.IsValid||!root.IsValid()||typeof RatingEmblem==='undefined')return;"
		<< "var overrides=root.Data().mirvScoreboardRankOverrides||{};"
		<< "for(var xuid in overrides){"
		<< "var o=overrides[xuid];"
		<< "var row=root.FindChildTraverse?root.FindChildTraverse('player-'+xuid):null;"
		<< "if(!row||!row.IsValid())continue;"
		<< "var emblem=row.FindChildTraverse('jsRatingEmblem');"
		<< "if(!emblem||!emblem.IsValid())continue;"
		<< "emblem.visible=true;"
		<< "RatingEmblem.SetXuid({root_panel:emblem,full_details:false,rating_type:o.ratingType,leaderboard_details:{score:o.score,matchesWon:o.wins},local_player:false});"
		<< "}"
		<< "};"
		<< "root.Data().mirvScoreboardRankApply=apply;"
		<< "if(!root.Data().mirvScoreboardRankHooked){"
		<< "root.Data().mirvScoreboardRankHooked=true;"
		<< "$.RegisterEventHandler('Scoreboard_UpdateJob',root,function(){$.Schedule(0.0,function(){var r=$('#Scoreboard')||root;if(r&&r.IsValid&&r.IsValid()&&r.Data().mirvScoreboardRankApply)r.Data().mirvScoreboardRankApply();});});"
		<< "$.RegisterEventHandler('OnOpenScoreboard',root,function(){$.Schedule(0.05,function(){var r=$('#Scoreboard')||root;if(r&&r.IsValid&&r.IsValid()&&r.Data().mirvScoreboardRankApply)r.Data().mirvScoreboardRankApply();});});"
		<< "}"
		<< "apply();"
		<< "})();";

	return g_myPanoramaWrapper.runScript(contextPanel, script.str().c_str());
}

bool parseScoreboardRankMode(const char* arg, std::string& outRatingType) {
	if (0 == _stricmp("premier", arg) || 0 == _stricmp("premiere", arg)) {
		outRatingType = "Premier";
		return true;
	}
	if (0 == _stricmp("competitive", arg) || 0 == _stricmp("comp", arg)) {
		outRatingType = "Competitive";
		return true;
	}
	if (0 == _stricmp("wingman", arg)) {
		outRatingType = "Wingman";
		return true;
	}

	return false;
}

void mirvScoreboardRank_PrintHelp(const char* arg0) {
	advancedfx::Message(
		"%s byXuid add x<ullXuid> premier <rating> [wins <iWins>]\n"
		"%s byXuid add x<ullXuid> competitive|comp <rank 1-18> [wins <iWins>]\n"
		"%s byXuid add x<ullXuid> wingman <rank 1-18> [wins <iWins>]\n"
		"%s byXuid remove x<ullXuid>\n"
		"%s clear\n"
		"%s print\n"
		"%s apply - Re-apply current overrides to the open scoreboard.\n"
		"Notes:\n"
		"\tThis is a visual Panorama scoreboard override for demo playback / recording.\n"
		"\tCS2 may rebuild scoreboard rows; use %s apply or mirv_cmd scheduling if needed.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}

CON_COMMAND(mirv_scoreboard_rank, "Visually overrides scoreboard rank / Premier rating panels.")
{
	const auto arg0 = args->ArgV(0);
	const auto argc = args->ArgC();

	if (2 <= argc) {
		const auto arg1 = args->ArgV(1);

		if (0 == _stricmp("byXuid", arg1)) {
			if (3 <= argc) {
				const auto arg2 = args->ArgV(2);

				if (0 == _stricmp("add", arg2)) {
					if (6 <= argc) {
						uint64_t xuid = 0;
						if (!parseXuidArg(args->ArgV(3), xuid)) {
							advancedfx::Warning("Invalid XUID: %s\n", args->ArgV(3));
							return;
						}

						ScoreboardRankOverride entry;
						if (!parseScoreboardRankMode(args->ArgV(4), entry.ratingType)) {
							advancedfx::Warning("Invalid rank type: %s\n", args->ArgV(4));
							return;
						}

						entry.score = atoi(args->ArgV(5));
						entry.wins = 10;
						entry.use = true;

						for (int i = 6; i + 1 < argc; ++i) {
							if (0 == _stricmp("wins", args->ArgV(i))) {
								entry.wins = atoi(args->ArgV(i + 1));
								++i;
							}
						}

						if (entry.score < 0) {
							advancedfx::Warning("Rank / rating must be >= 0.\n");
							return;
						}

						if ((entry.ratingType == "Competitive" || entry.ratingType == "Wingman") && 18 < entry.score) {
							advancedfx::Warning("Competitive and Wingman rank image ids are expected to be 0..18.\n");
						}

						g_ScoreboardRankOverrides[xuid] = entry;
						applyScoreboardRankOverrides();
						return;
					}
				}
				else if (0 == _stricmp("remove", arg2)) {
					if (4 <= argc) {
						uint64_t xuid = 0;
						if (!parseXuidArg(args->ArgV(3), xuid)) {
							advancedfx::Warning("Invalid XUID: %s\n", args->ArgV(3));
							return;
						}
						g_ScoreboardRankOverrides.erase(xuid);
						applyScoreboardRankOverrides();
						return;
					}
				}
			}

			mirvScoreboardRank_PrintHelp(arg0);
			return;
		}

		if (0 == _stricmp("clear", arg1)) {
			g_ScoreboardRankOverrides.clear();
			applyScoreboardRankOverrides();
			return;
		}

		if (0 == _stricmp("print", arg1)) {
			for (const auto& entry : g_ScoreboardRankOverrides) {
				advancedfx::Message("x%llu %s %i wins %i\n", (unsigned long long)entry.first, entry.second.ratingType.c_str(), entry.second.score, entry.second.wins);
			}
			return;
		}

		if (0 == _stricmp("apply", arg1)) {
			applyScoreboardRankOverrides();
			return;
		}
	}

	mirvScoreboardRank_PrintHelp(arg0);
}

u_char* findAvatarContextPanel() {
	auto hudPanel = g_myPanoramaWrapper.getHudPanel();
	if (!hudPanel) return nullptr;
	return hudPanel;
}

bool applyAvatarOverrides() {
	auto contextPanel = findAvatarContextPanel();
	if (!contextPanel) return false;

	std::map<uint64_t, std::string> topHudPanelIds;
	if (g_pEntityList && *g_pEntityList && g_GetEntityFromIndex) {
		int ctIndex = 0;
		int tIndex = 0;
		const int highestIndex = GetHighestEntityIndex();
		for (int i = 0; i <= highestIndex; ++i) {
			auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i);
			if (!ent || !ent->IsPlayerController()) continue;

			const uint64_t xuid = *(uint64_t*)((u_char*)(ent) + g_clientDllOffsets.CBasePlayerController.m_steamID);
			const int teamNumber = *(int*)((u_char*)(ent) + g_clientDllOffsets.C_BaseEntity.m_iTeamNum);
			if (teamNumber == 3) {
				topHudPanelIds[xuid] = std::string("Avatar") + std::to_string(ctIndex++);
			}
			else if (teamNumber == 2) {
				topHudPanelIds[xuid] = std::string("Avatar") + std::to_string(10 + tIndex++);
			}
		}
	}

	std::ostringstream script;
	script
		<< "(function(){"
		<< "var root=$.GetContextPanel();"
		<< "if(!root||!root.IsValid||!root.IsValid())return;"
		<< "var data={";

	bool first = true;
	for (const auto& entry : g_AvatarOverrides) {
		if (!entry.second.use) continue;
		if (!first) script << ",";
		first = false;
		script
			<< "'" << entry.first << "':{steamid:'" << entry.second.replacementSteamId << "'";
		auto hudIt = topHudPanelIds.find(entry.first);
		if (hudIt != topHudPanelIds.end()) {
			script << ",hud:'" << jsEscapeSingleQuotedString(hudIt->second) << "'";
		}
		script << "}";
	}

	script
		<< "};"
		<< "root.Data().mirvAvatarOverrides=data;"
		<< "var applyAvatar=function(panel,replacement){"
		<< "if(!panel||!panel.IsValid||!panel.IsValid()||!replacement)return false;"
		<< "if(typeof panel.PopulateFromSteamID==='function'){panel.PopulateFromSteamID(replacement);panel.Data().mirvAvatarReplacementSteamId=replacement;return true;}"
		<< "return false;"
		<< "};"
		<< "var findAvatarImage=function(panel){"
		<< "if(!panel||!panel.IsValid||!panel.IsValid())return null;"
		<< "try{if(typeof panel.PopulateFromSteamID==='function')return panel;}catch(e){}"
		<< "var children=[];try{children=panel.Children?panel.Children():[];}catch(e){}"
		<< "for(var i=0;i<children.length;++i){var found=findAvatarImage(children[i]);if(found)return found;}"
		<< "return null;"
		<< "};"
		<< "var xuidFromPanel=function(panel){"
		<< "for(var p=panel,depth=0;p&&depth<8;p=p.GetParent?p.GetParent():null,++depth){"
		<< "try{var id=p.id||'';if(id.indexOf('player-')===0)return id.substring(7);}catch(e){}"
		<< "try{var x=p.GetAttributeString?p.GetAttributeString('xuid',''):'';if(x)return x;}catch(e){}"
		<< "try{x=p.GetAttributeString?p.GetAttributeString('steamid',''):'';if(x)return x;}catch(e){}"
		<< "try{x=p.GetAttributeString?p.GetAttributeString('steamid64',''):'';if(x)return x;}catch(e){}"
		<< "try{var s=p.GetAttributeInt?p.GetAttributeInt('player_slot',-1):-1;if(s>=0&&typeof GameStateAPI!=='undefined')return GameStateAPI.GetPlayerXuidStringFromPlayerSlot(s);}catch(e){}"
		<< "try{s=p.GetAttributeInt?p.GetAttributeInt('slot',-1):-1;if(s>=0&&typeof GameStateAPI!=='undefined')return GameStateAPI.GetPlayerXuidStringFromPlayerSlot(s);}catch(e){}"
		<< "}"
		<< "return '';"
		<< "};"
		<< "var walk=function(panel){"
		<< "if(!panel||!panel.IsValid||!panel.IsValid())return;"
		<< "try{if(typeof panel.PopulateFromSteamID==='function'){var x=xuidFromPanel(panel);if(x&&data[x])applyAvatar(panel,data[x].steamid);}}catch(e){}"
		<< "var children=[];try{children=panel.Children?panel.Children():[];}catch(e){}"
		<< "for(var i=0;i<children.length;++i)walk(children[i]);"
		<< "};"
		<< "var apply=function(){"
		<< "var root=$.GetContextPanel();"
		<< "if(!root||!root.IsValid||!root.IsValid())return;"
		<< "var overrides=root.Data().mirvAvatarOverrides||{};"
		<< "var scoreboard=$('#Scoreboard')||(root.FindChildTraverse?root.FindChildTraverse('Scoreboard'):null);"
		<< "for(var xuid in overrides){"
		<< "var o=overrides[xuid];"
		<< "var replacement=o&&o.steamid;"
		<< "var row=scoreboard&&scoreboard.FindChildTraverse?scoreboard.FindChildTraverse('player-'+xuid):null;"
		<< "if(row&&row.IsValid&&row.IsValid()){applyAvatar(findAvatarImage(row),replacement);}"
		<< "if(o&&o.hud){var hudPanel=root.FindChildTraverse?root.FindChildTraverse(o.hud):null;if(hudPanel&&hudPanel.IsValid&&hudPanel.IsValid()){applyAvatar(findAvatarImage(hudPanel),replacement);}}"
		<< "}"
		<< "try{if(scoreboard&&scoreboard.IsValid&&scoreboard.IsValid()&&!scoreboard.Data().mirvAvatarScoreboardHooked){scoreboard.Data().mirvAvatarScoreboardHooked=true;$.RegisterEventHandler('Scoreboard_UpdateJob',scoreboard,function(){$.Schedule(0.0,function(){var r=$.GetContextPanel();if(r&&r.IsValid&&r.IsValid()&&r.Data().mirvAvatarApply)r.Data().mirvAvatarApply();});});$.RegisterEventHandler('OnOpenScoreboard',scoreboard,function(){$.Schedule(0.05,function(){var r=$.GetContextPanel();if(r&&r.IsValid&&r.IsValid()&&r.Data().mirvAvatarApply)r.Data().mirvAvatarApply();});});}}catch(e){}"
		<< "walk(root);"
		<< "};"
		<< "root.Data().mirvAvatarApply=apply;"
		<< "if(!root.Data().mirvAvatarHooked){"
		<< "root.Data().mirvAvatarHooked=true;"
		<< "$.RegisterEventHandler('Scoreboard_UpdateJob',root,function(){$.Schedule(0.0,function(){var r=$.GetContextPanel();if(r&&r.IsValid&&r.IsValid()&&r.Data().mirvAvatarApply)r.Data().mirvAvatarApply();});});"
		<< "$.RegisterEventHandler('OnOpenScoreboard',root,function(){$.Schedule(0.05,function(){var r=$.GetContextPanel();if(r&&r.IsValid&&r.IsValid()&&r.Data().mirvAvatarApply)r.Data().mirvAvatarApply();});});"
		<< "var loop=function(){var r=$.GetContextPanel();if(r&&r.IsValid&&r.IsValid()&&r.Data().mirvAvatarApply)r.Data().mirvAvatarApply();$.Schedule(0.5,loop);};"
		<< "$.Schedule(0.5,loop);"
		<< "}"
		<< "apply();"
		<< "})();";

	return g_myPanoramaWrapper.runScript(contextPanel, script.str().c_str());
}

bool inspectAvatarPanels() {
	auto contextPanel = findAvatarContextPanel();
	if (!contextPanel) return false;

	std::ostringstream script;
	script
		<< "(function(){"
		<< "var root=$.GetContextPanel();"
		<< "if(!root||!root.IsValid||!root.IsValid()){ $.Msg('mirv_avatar inspect: no valid root'); return; }"
		<< "var count=0;"
		<< "var describe=function(panel){"
		<< "var path=[];"
		<< "for(var p=panel,depth=0;p&&depth<8;p=p.GetParent?p.GetParent():null,++depth){path.unshift((p.paneltype||'?')+'#'+(p.id||''));}"
		<< "var attrs=[];"
		<< "['xuid','steamid','steamid64','player_slot','slot','player-slot','data-player-slot','team','name'].forEach(function(k){"
		<< "try{var v=panel.GetAttributeString?panel.GetAttributeString(k,''):'';if(v!=='')attrs.push(k+'='+v);}catch(e){}"
		<< "try{var i=panel.GetAttributeInt?panel.GetAttributeInt(k,-999999):-999999;if(i!==-999999&&i!==-1)attrs.push(k+'='+i);}catch(e){}"
		<< "});"
		<< "$.Msg('mirv_avatar inspect: '+path.join(' > ')+' attrs=['+attrs.join(',')+']');"
		<< "};"
		<< "var walk=function(panel){"
		<< "if(!panel||!panel.IsValid||!panel.IsValid())return;"
		<< "try{if(panel.paneltype==='CSGOAvatarImage'||/(^|[^a-z])avatar([^a-z]|$)/i.test((panel.id||'')+' '+(panel.paneltype||''))){++count;describe(panel);}}catch(e){}"
		<< "var children=[];try{children=panel.Children?panel.Children():[];}catch(e){}"
		<< "for(var i=0;i<children.length;++i)walk(children[i]);"
		<< "};"
		<< "walk(root);"
		<< "$.Msg('mirv_avatar inspect: found '+count+' avatar-like panel(s)');"
		<< "})();";

	return g_myPanoramaWrapper.runScript(contextPanel, script.str().c_str());
}

void mirvAvatar_PrintHelp(const char* arg0) {
	advancedfx::Message(
		"%s byXuid add x<targetSteamID64> steamid x<avatarSteamID64>\n"
		"%s byXuid remove x<targetSteamID64>\n"
		"%s clear\n"
		"%s print\n"
		"%s apply\n"
		"%s inspect\n"
		"Notes:\n"
		"\tThis prototype visually repopulates Panorama CSGOAvatarImage panels from another SteamID.\n"
		"\tIt does not modify the demo or globally change the target player's identity.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}

CON_COMMAND(mirv_avatar, "Visually overrides CS2 player avatars during demo playback.")
{
	const auto arg0 = args->ArgV(0);
	const auto argc = args->ArgC();

	if (2 <= argc) {
		const auto arg1 = args->ArgV(1);

		if (0 == _stricmp("byXuid", arg1)) {
			if (3 <= argc) {
				const auto arg2 = args->ArgV(2);

				if (0 == _stricmp("add", arg2)) {
					if (6 <= argc) {
						uint64_t targetXuid = 0;
						if (!parseXuidArg(args->ArgV(3), targetXuid)) {
							advancedfx::Warning("mirv_avatar: invalid target XUID: %s\n", args->ArgV(3));
							return;
						}

						if (0 != _stricmp("steamid", args->ArgV(4)) && 0 != _stricmp("fromSteamId", args->ArgV(4))) {
							advancedfx::Warning("mirv_avatar: expected steamid x<avatarSteamID64>.\n");
							return;
						}

						uint64_t avatarXuid = 0;
						if (!parseXuidArg(args->ArgV(5), avatarXuid)) {
							advancedfx::Warning("mirv_avatar: invalid avatar SteamID: %s\n", args->ArgV(5));
							return;
						}

						AvatarOverride entry;
						entry.replacementSteamId = avatarXuid;
						entry.use = true;
						g_AvatarOverrides[targetXuid] = entry;
						applyAvatarOverrides();
						return;
					}
				}
				else if (0 == _stricmp("remove", arg2)) {
					if (4 <= argc) {
						uint64_t targetXuid = 0;
						if (!parseXuidArg(args->ArgV(3), targetXuid)) {
							advancedfx::Warning("mirv_avatar: invalid target XUID: %s\n", args->ArgV(3));
							return;
						}

						g_AvatarOverrides.erase(targetXuid);
						g_AvatarPanelReplacementByPanel.clear();
						applyAvatarOverrides();
						return;
					}
				}
			}

			mirvAvatar_PrintHelp(arg0);
			return;
		}

		if (0 == _stricmp("clear", arg1)) {
			g_AvatarOverrides.clear();
			g_AvatarPanelReplacementByPanel.clear();
			applyAvatarOverrides();
			return;
		}

		if (0 == _stricmp("print", arg1)) {
			for (const auto& entry : g_AvatarOverrides) {
				std::map<uint64_t, std::string> topHudPanelIds;
				if (g_pEntityList && *g_pEntityList && g_GetEntityFromIndex) {
					int ctIndex = 0;
					int tIndex = 0;
					const int highestIndex = GetHighestEntityIndex();
					for (int i = 0; i <= highestIndex; ++i) {
						auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i);
						if (!ent || !ent->IsPlayerController()) continue;

						const uint64_t xuid = *(uint64_t*)((u_char*)(ent) + g_clientDllOffsets.CBasePlayerController.m_steamID);
						const int teamNumber = *(int*)((u_char*)(ent) + g_clientDllOffsets.C_BaseEntity.m_iTeamNum);
						if (teamNumber == 3) {
							topHudPanelIds[xuid] = std::string("Avatar") + std::to_string(ctIndex++);
						}
						else if (teamNumber == 2) {
							topHudPanelIds[xuid] = std::string("Avatar") + std::to_string(10 + tIndex++);
						}
					}
				}
				advancedfx::Message(
					"x%llu steamid x%llu hud=%s\n",
					(unsigned long long)entry.first,
					(unsigned long long)entry.second.replacementSteamId,
					topHudPanelIds[entry.first].c_str());
			}
			return;
		}

		if (0 == _stricmp("apply", arg1)) {
			applyAvatarOverrides();
			return;
		}

		if (0 == _stricmp("inspect", arg1)) {
			inspectAvatarPanels();
			return;
		}
	}

	mirvAvatar_PrintHelp(arg0);
}

struct SyntheticChatMessage {
	std::string displayName;
	std::string team = "auto";
	std::string visibility = "all";
	std::string location;
	std::string message;
	int entityIndex = -1;
	int userId = -1;
	uint64_t xuid = 0;
	bool alive = true;
	bool teamSpecified = false;
	bool aliveSpecified = false;
};

namespace ChatNative {
	constexpr int kSayText2MessageId = 118;

	class CNetMessage {
	public:
		virtual ~CNetMessage() {}
		virtual void* AsProto() const = 0;
		virtual void* AsProto2() const = 0;
		virtual void* GetNetMessage() const = 0;
		virtual CNetMessage* CopyConstruct(const CNetMessage* other) const = 0;
	};

	class INetworkMessageInternal {
	public:
		virtual ~INetworkMessageInternal() = 0;
		virtual const char* GetUnscopedName() = 0;
		virtual void* GetNetMessageInfo() = 0;
		virtual void SetMessageId(unsigned short nMessageId) = 0;
		virtual void AddCategoryMask(int nMask, bool unk) = 0;
		virtual void SwitchMode(int nMode) = 0;
		virtual CNetMessage* AllocateMessage() = 0;
	};

	using CreateInterfaceFn = void* (*)(const char* pName, int* pReturnCode);

	CreateInterfaceFn getFactory(const char* moduleName) {
		auto module = GetModuleHandleA(moduleName);
		if (!module) return nullptr;
		return reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
	}

	void* createInterface(const char* moduleName, const char* interfaceName) {
		auto factory = getFactory(moduleName);
		if (!factory) return nullptr;
		return factory(interfaceName, nullptr);
	}

	void* getNetworkMessages() {
		static void* value = nullptr;
		static bool tried = false;
		if (!tried) {
			tried = true;
			value = createInterface("engine2.dll", "NetworkMessagesVersion001");
			if (!value) value = createInterface("networksystem.dll", "NetworkMessagesVersion001");
		}
		return value;
	}

	template <typename Fn>
	Fn vfunc(void* instance, size_t index) {
		return instance ? reinterpret_cast<Fn>((*reinterpret_cast<void***>(instance))[index]) : nullptr;
	}

	INetworkMessageInternal* findNetworkMessageById(void* networkMessages, int id) {
		using Fn = INetworkMessageInternal* (__fastcall*)(void*, int);
		// INetworkMessages::FindNetworkMessageById is slot 30 in the current public CS2 SDK.
		auto fn = vfunc<Fn>(networkMessages, 30);
		return fn ? fn(networkMessages, id) : nullptr;
	}

	INetworkMessageInternal* findNetworkMessagePartial(void* networkMessages, const char* name) {
		using Fn = INetworkMessageInternal* (__fastcall*)(void*, const char*);
		// INetworkMessages::FindNetworkMessagePartial is slot 14 in the current public CS2 SDK.
		auto fn = vfunc<Fn>(networkMessages, 14);
		return fn ? fn(networkMessages, name) : nullptr;
	}

	void deallocateNetworkMessage(void* networkMessages, INetworkMessageInternal* messageType, CNetMessage* message) {
		if (!networkMessages || !messageType || !message) return;
		using Fn = void(__fastcall*)(void*, INetworkMessageInternal*, CNetMessage*);
		// INetworkMessages::DeallocateNetMessageAbstract is slot 9.
		auto fn = vfunc<Fn>(networkMessages, 9);
		if (fn) fn(networkMessages, messageType, message);
	}

	std::string maybeTokenWithHash(const std::string& value) {
		if (!value.empty() && value[0] == '#') return value;
		return "#" + value;
	}

	std::string chooseMessageName(const SyntheticChatMessage& entry) {
		const bool teamChat = 0 == _stricmp(entry.visibility.c_str(), "team");
		const bool hasLocation = !entry.location.empty() && 0 != _stricmp(entry.location.c_str(), "none");

		if (teamChat) {
			if (0 == _stricmp(entry.team.c_str(), "CT")) {
				if (!entry.alive) return "Cstrike_Chat_CT_Dead";
				return hasLocation ? "Cstrike_Chat_CT_Loc" : "Cstrike_Chat_CT";
			}
			if (0 == _stricmp(entry.team.c_str(), "T")) {
				if (!entry.alive) return "Cstrike_Chat_T_Dead";
				return hasLocation ? "Cstrike_Chat_T_Loc" : "Cstrike_Chat_T";
			}
			return "Cstrike_Chat_Spec";
		}

		if (0 == _stricmp(entry.team.c_str(), "spec")) return "Cstrike_Chat_AllSpec";
		if (!entry.alive) return "Cstrike_Chat_AllDead";
		return "Cstrike_Chat_All";
	}

	void appendProtoVarint(std::vector<unsigned char>& out, uint64_t value) {
		while (value >= 0x80) {
			out.push_back(static_cast<unsigned char>((value & 0x7f) | 0x80));
			value >>= 7;
		}
		out.push_back(static_cast<unsigned char>(value));
	}

	void appendProtoStringField(std::vector<unsigned char>& out, int fieldNumber, const std::string& value) {
		appendProtoVarint(out, (static_cast<uint64_t>(fieldNumber) << 3) | 2);
		appendProtoVarint(out, value.size());
		out.insert(out.end(), value.begin(), value.end());
	}

	std::vector<unsigned char> makeSayText2Payload(const SyntheticChatMessage& entry, const std::string& messageName) {
		const auto location = (!entry.location.empty() && 0 != _stricmp(entry.location.c_str(), "none")) ? entry.location : "";
		std::vector<unsigned char> payload;
		payload.reserve(16 + messageName.size() + entry.displayName.size() + entry.message.size() + location.size());

		appendProtoVarint(payload, (1u << 3) | 0u);
		appendProtoVarint(payload, entry.entityIndex >= 0 ? static_cast<uint64_t>(entry.entityIndex) : 0u);
		appendProtoVarint(payload, (2u << 3) | 0u);
		appendProtoVarint(payload, 1);
		appendProtoStringField(payload, 3, maybeTokenWithHash(messageName));
		appendProtoStringField(payload, 4, entry.displayName);
		appendProtoStringField(payload, 5, entry.message);
		appendProtoStringField(payload, 6, location);
		appendProtoStringField(payload, 7, "");
		return payload;
	}

	bool fillSayText2Message(CNetMessage* message, const SyntheticChatMessage& entry, const std::string& messageName) {
		if (!message) return false;
		auto proto = static_cast<google::protobuf::MessageLite*>(message->AsProto());
		if (!proto) return false;

		const auto payload = makeSayText2Payload(entry, messageName);
		const bool ok = proto->ParseFromArray(payload.data(), static_cast<int>(payload.size()));
		if (ok) {
			advancedfx::Message("mirv_chat_insert: filled SayText2 protobuf object via MessageLite parse size=%zu.\n", payload.size());
		} else {
			advancedfx::Warning("mirv_chat_insert: MessageLite SayText2 protobuf parse failed size=%zu.\n", payload.size());
		}
		return ok;
	}

	using SayText2HandlerFn = void(__fastcall*)(void*, CNetMessage*);

	SayText2HandlerFn getSayText2Handler() {
		static SayText2HandlerFn value = nullptr;
		static bool tried = false;

		if (!tried) {
			tried = true;
			auto clientDll = GetModuleHandleA("client.dll");
			if (!clientDll) {
				advancedfx::Warning("mirv_chat_insert: client.dll not loaded; native SayText2 handler unavailable.\n");
				return nullptr;
			}

			const auto address = getAddress(
				clientDll,
				"48 89 4C 24 08 55 41 56 48 8D AC 24 ?? ?? ?? ?? "
				"48 81 EC ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 4C 8B F2 "
				"48 8B 01 FF 90 50 01 00 00 84 C0 74 ?? E8 ?? ?? ?? ?? "
				"80 78 72 00 0F 85 ?? ?? ?? ?? 41 8B 46 74 48 89 B4 24 ?? ?? ?? ??");
			value = reinterpret_cast<SayText2HandlerFn>(address);
			if (value) {
				advancedfx::Message("mirv_chat_insert: resolved native SayText2 handler at %p.\n", value);
			}
		}

		return value;
	}

	bool dispatchViaNativeSayText2Handler(CNetMessage* message) {
		auto handler = getSayText2Handler();
		if (!handler) {
			advancedfx::Warning("mirv_chat_insert: native SayText2 handler not found.\n");
			return false;
		}

		advancedfx::Message("mirv_chat_insert: calling native SayText2 handler.\n");
		handler(nullptr, message);
		advancedfx::Message("mirv_chat_insert: native SayText2 handler returned.\n");
		return true;
	}
}

u_char* findHudChatPanel() {
	auto hudPanel = g_myPanoramaWrapper.getHudPanel();
	if (!hudPanel) return nullptr;

	auto chats = g_myPanoramaWrapper.findChildrenInLayoutFileByClassName(hudPanel, "CSGOHudChat");
	if (!chats.empty()) {
		return chats[0];
	}

	return g_myPanoramaWrapper.findChildInLayoutFile(hudPanel, "ChatContainer");
}

bool resolveSyntheticChatPlayerByXuid(uint64_t xuid, PlayerInfo& outPlayer, int* outEntityIndex = nullptr) {
	const int highestIndex = GetHighestEntityIndex();
	for (int i = 0; i < highestIndex + 1; ++i) {
		if (auto ent = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i)) {
			if (!ent->IsPlayerController()) continue;
			auto player = getPlayerInfoFromControllerIndex(i);
			if (player.xuid == xuid) {
				outPlayer = player;
				if (outEntityIndex) *outEntityIndex = i;
				return true;
			}
		}
	}

	return false;
}

bool inferSyntheticChatState(SyntheticChatMessage& entry) {
	if (entry.entityIndex >= 0) {
		if (auto controller = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, entry.entityIndex)) {
			if (!entry.teamSpecified || 0 == _stricmp(entry.team.c_str(), "auto")) {
				switch (controller->GetTeam()) {
				case 2: entry.team = "T"; break;
				case 3: entry.team = "CT"; break;
				case 1: entry.team = "spec"; break;
				default: entry.team = "none"; break;
				}
			}

			if (!entry.aliveSpecified) {
				entry.alive = false;
				auto pawnHandle = controller->GetPlayerPawnHandle();
				const auto pawnIndex = pawnHandle.GetEntryIndex();
				if (pawnIndex >= 0) {
					if (auto pawn = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, pawnIndex)) {
						if (pawn->IsPlayerPawn()) {
							entry.alive = 0 < pawn->GetHealth();
						}
					}
				}
			}
		}
	}

	if (entry.team.empty() || 0 == _stricmp(entry.team.c_str(), "auto")) {
		entry.team = "none";
	}
	else if (0 == _stricmp(entry.team.c_str(), "ct")) {
		entry.team = "CT";
	}
	else if (0 == _stricmp(entry.team.c_str(), "t")) {
		entry.team = "T";
	}
	else if (0 == _stricmp(entry.team.c_str(), "spec") || 0 == _stricmp(entry.team.c_str(), "spectator")) {
		entry.team = "spec";
	}
	else if (0 == _stricmp(entry.team.c_str(), "none")) {
		entry.team = "none";
	}
	else {
		advancedfx::Warning("mirv_chat_insert: invalid team \"%s\". Use T, CT, spec, none, or auto.\n", entry.team.c_str());
		return false;
	}

	if (entry.visibility.empty()) {
		entry.visibility = "all";
	}
	else if (0 == _stricmp(entry.visibility.c_str(), "all")) {
		entry.visibility = "all";
	}
	else if (0 == _stricmp(entry.visibility.c_str(), "team")) {
		entry.visibility = "team";
	}
	else {
		advancedfx::Warning("mirv_chat_insert: invalid visibility \"%s\". Use all or team.\n", entry.visibility.c_str());
		return false;
	}

	if (0 == _stricmp(entry.visibility.c_str(), "team") && 0 == _stricmp(entry.team.c_str(), "none")) {
		advancedfx::Warning("mirv_chat_insert: team chat needs a player-resolved or explicit team.\n");
		return false;
	}

	return true;
}

bool applySyntheticChatMessage(const SyntheticChatMessage& entry) {
	auto networkMessages = ChatNative::getNetworkMessages();

	if (!networkMessages) {
		advancedfx::Warning("mirv_chat_insert: NetworkMessagesVersion001 interface not found.\n");
		return false;
	}

	auto messageType = ChatNative::findNetworkMessageById(networkMessages, ChatNative::kSayText2MessageId);
	if (!messageType) {
		messageType = ChatNative::findNetworkMessagePartial(networkMessages, "SayText2");
	}
	if (!messageType) {
		advancedfx::Warning("mirv_chat_insert: SayText2 network message type not found.\n");
		return false;
	}

	auto message = messageType->AllocateMessage();
	if (!message) {
		advancedfx::Warning("mirv_chat_insert: SayText2 AllocateMessage failed.\n");
		return false;
	}

	const auto messageName = ChatNative::chooseMessageName(entry);
	advancedfx::Message(
		"mirv_chat_insert: allocated SayText2 message=%p proto=%p entity=%i token=%s.\n",
		message,
		message->AsProto(),
		entry.entityIndex,
		messageName.c_str());

	if (!ChatNative::fillSayText2Message(message, entry, messageName)) {
		advancedfx::Warning("mirv_chat_insert: failed to fill SayText2 protobuf object.\n");
		ChatNative::deallocateNetworkMessage(networkMessages, messageType, message);
		return false;
	}

	advancedfx::Message("mirv_chat_insert: filled SayText2 protobuf object.\n");

	const bool handledSynchronously = ChatNative::dispatchViaNativeSayText2Handler(message);

	advancedfx::Message(
		"mirv_chat_insert: finished SayText2 native entity=%i token=%s name=\"%s\" text=\"%s\" posted=%i.\n",
		entry.entityIndex,
		messageName.c_str(),
		entry.displayName.c_str(),
		entry.message.c_str(),
		handledSynchronously ? 1 : 0);

	advancedfx::Message("mirv_chat_insert: deallocating SayText2 message.\n");
	ChatNative::deallocateNetworkMessage(networkMessages, messageType, message);
	advancedfx::Message("mirv_chat_insert: deallocated SayText2 message.\n");
	return handledSynchronously;
}

bool clearSyntheticChatMessages() {
	advancedfx::Warning("mirv_chat_insert clear: disabled with the rejected ChatHistoryText injection path.\n");
	return false;
}

bool inspectHudChatPanel() {
	auto contextPanel = findHudChatPanel();
	if (!contextPanel) {
		advancedfx::Warning("mirv_chat_insert inspect: CSGOHudChat / ChatContainer panel not found.\n");
		return false;
	}

	auto panelId = *(char**)(contextPanel + CS2::PanoramaUIPanel::panelId);
	auto panel2D = *(CPanel2D**)(contextPanel + 0x8);
	advancedfx::Message(
		"mirv_chat_insert inspect: context panel=%p id=%s class=%s\n",
		contextPanel,
		panelId ? panelId : "",
		panel2D ? panel2D->getClassName() : "null");

	std::ostringstream script;
	script
		<< "(function(){"
		<< "var root=$.GetContextPanel();"
		<< "$.Msg('mirv_chat_insert inspect context id='+(root?root.id:'<null>')+' type='+(root?root.paneltype:'<null>'));"
		<< "var chat=root&&root.FindChildTraverse?root.FindChildTraverse('ChatHistoryText'):null;"
		<< "$.Msg('mirv_chat_insert inspect ChatHistoryText='+(chat&&chat.IsValid&&chat.IsValid()?('id='+chat.id+' type='+chat.paneltype+' textLen='+(chat.text?chat.text.length:0)):'not found'));"
		<< "var names=[];"
		<< "var o=root;"
		<< "for(var depth=0;o&&depth<4;o=Object.getPrototypeOf(o),++depth){"
		<< "try{Object.getOwnPropertyNames(o).forEach(function(n){if(names.indexOf(n)<0)names.push(n);});}catch(e){}"
		<< "}"
		<< "names=names.filter(function(n){return /chat|say|submit|text|message|history/i.test(n);}).sort();"
		<< "$.Msg('mirv_chat_insert inspect candidate methods/properties: '+names.join(', '));"
		<< "})();";

	return g_myPanoramaWrapper.runScript(contextPanel, script.str().c_str());
}

bool inspectNativeChatPath() {
	auto networkMessages = ChatNative::getNetworkMessages();
	auto messageType = networkMessages ? ChatNative::findNetworkMessageById(networkMessages, ChatNative::kSayText2MessageId) : nullptr;
	if (!messageType && networkMessages) {
		messageType = ChatNative::findNetworkMessagePartial(networkMessages, "SayText2");
	}
	auto handler = ChatNative::getSayText2Handler();
	advancedfx::Message(
		"mirv_chat_insert inspect native: networkMessages=%p sayText2=%p handler=%p\n",
		networkMessages,
		messageType,
		handler);
	return nullptr != networkMessages && nullptr != messageType && nullptr != handler;
}

void mirvChatInsert_PrintHelp(const char* arg0) {
	advancedfx::Message(
		"%s byXuid x<ullXuid> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>\n"
		"%s byUserId <iUserId> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>\n"
		"%s name <displayName> [team=auto|T|CT|spec|none] [alive=0|1] [visibility=all|team] [location=<string>|none] message <text...>\n"
		"%s clear\n"
		"%s inspect\n"
		"Examples:\n"
		"\t%s byXuid x76561197962023477 team=CT alive=1 visibility=team location=A_Ramp message rotate now\n"
		"\tmirv_cmd addAtTick 12345 %s byXuid x76561197962023477 team=CT alive=1 visibility=team location=A_Ramp message rotate now\n"
		"Notes:\n"
		"\tThe command allocates native CUserMessageSayText2, parses a SayText2 protobuf payload into it, then calls CS2's native SayText2 HUD handler.\n"
		"\tFor byXuid / byUserId, team=auto and omitted alive infer state from the current player controller and pawn.\n"
		"\tUse mirv_cmd addAtTick / addAtTime to schedule insertion during demo playback.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}

bool parseSyntheticChatOptions(advancedfx::ICommandArgs* args, int firstOption, SyntheticChatMessage& entry) {
	const int argc = args->ArgC();
	int messageIndex = -1;

	for (int i = firstOption; i < argc; ++i) {
		const char* arg = args->ArgV(i);
		if (0 == _stricmp(arg, "message")) {
			messageIndex = i + 1;
			break;
		}
		else if (StringIBeginsWith(arg, "team=")) {
			entry.team = arg + strlen("team=");
			entry.teamSpecified = true;
		}
		else if (StringIBeginsWith(arg, "alive=")) {
			entry.alive = 0 != atoi(arg + strlen("alive="));
			entry.aliveSpecified = true;
		}
		else if (StringIBeginsWith(arg, "visibility=")) {
			entry.visibility = arg + strlen("visibility=");
		}
		else if (StringIBeginsWith(arg, "location=")) {
			entry.location = arg + strlen("location=");
			std::replace(entry.location.begin(), entry.location.end(), '_', ' ');
		}
		else {
			advancedfx::Warning("mirv_chat_insert: unknown option before message: %s\n", arg);
			return false;
		}
	}

	if (messageIndex < 0 || messageIndex >= argc) {
		advancedfx::Warning("mirv_chat_insert: missing message <text...>.\n");
		return false;
	}

	for (int i = messageIndex; i < argc; ++i) {
		if (!entry.message.empty()) entry.message.push_back(' ');
		entry.message.append(args->ArgV(i));
	}

	if (entry.message.empty()) {
		advancedfx::Warning("mirv_chat_insert: message cannot be empty.\n");
		return false;
	}

	return true;
}

const char* getSyntheticChatDisplayName(const PlayerInfo& player, int controllerIndex) {
	const auto replacement = GetReplaceNameOverride(controllerIndex, player.xuid);
	if (replacement) {
		return replacement;
	}

	return player.name;
}

CON_COMMAND(mirv_chat_insert, "Insert synthetic chat into CS2's real HUD chat panel.")
{
	const auto arg0 = args->ArgV(0);
	const auto argc = args->ArgC();

	if (2 <= argc) {
		const auto arg1 = args->ArgV(1);

		if (0 == _stricmp("clear", arg1)) {
			clearSyntheticChatMessages();
			return;
		}

		if (0 == _stricmp("inspect", arg1)) {
			inspectHudChatPanel();
			inspectNativeChatPath();
			return;
		}

		SyntheticChatMessage entry;
		int firstOption = 3;

		if (0 == _stricmp("byXuid", arg1)) {
			if (argc < 5) {
				mirvChatInsert_PrintHelp(arg0);
				return;
			}

			uint64_t xuid = 0;
			if (!parseXuidArg(args->ArgV(2), xuid)) {
				advancedfx::Warning("mirv_chat_insert: invalid XUID: %s\n", args->ArgV(2));
				return;
			}

			PlayerInfo player;
			int entityIndex = -1;
			if (resolveSyntheticChatPlayerByXuid(xuid, player, &entityIndex) && player.name) {
				entry.displayName = getSyntheticChatDisplayName(player, entityIndex);
				entry.entityIndex = entityIndex;
				entry.userId = player.userId;
				entry.xuid = player.xuid;
			}
			else {
				entry.displayName = args->ArgV(2);
				advancedfx::Warning("mirv_chat_insert: XUID not currently resolved; using %s as display name.\n", entry.displayName.c_str());
			}
		}
		else if (0 == _stricmp("byUserId", arg1)) {
			if (argc < 5) {
				mirvChatInsert_PrintHelp(arg0);
				return;
			}

			const int userId = atoi(args->ArgV(2));
			auto player = getPlayerInfoFromControllerIndex(userId + 1);
			if (player.name) {
				entry.displayName = getSyntheticChatDisplayName(player, userId + 1);
				entry.entityIndex = userId + 1;
				entry.userId = player.userId;
				entry.xuid = player.xuid;
			}
			else {
				entry.displayName = args->ArgV(2);
				advancedfx::Warning("mirv_chat_insert: userId not currently resolved; using %s as display name.\n", entry.displayName.c_str());
			}
		}
		else if (0 == _stricmp("name", arg1)) {
			if (argc < 5) {
				mirvChatInsert_PrintHelp(arg0);
				return;
			}
			entry.displayName = args->ArgV(2);
		}
		else {
			mirvChatInsert_PrintHelp(arg0);
			return;
		}

		if (!parseSyntheticChatOptions(args, firstOption, entry)) {
			return;
		}

		if (!inferSyntheticChatState(entry)) {
			return;
		}

		applySyntheticChatMessage(entry);
		return;
	}

	mirvChatInsert_PrintHelp(arg0);
}

enum panelMatchType {
	ID = 0,
	CLASS_NAME
};

void applyStyleProperty_Console(IWrpCommandArgs * args) {
	int argc = args->ArgC();
	const char * arg0 = args->ArgV(0);

	panelMatchType matchType = panelMatchType::ID;
	std::string panelId = "";
	// TODO: match by property type, when add new ones
	bool didMatchProperty = false;
	float opacity = 0;

	for (int i = 1; i < argc; ++i)
	{
		const char * argI = args->ArgV(i);
		if (StringIBeginsWith(argI, "panelId="))
		{
			panelId = argI + strlen("panelId=");
			matchType = panelMatchType::ID;
		}
		else if (StringIBeginsWith(argI, "panelClassName="))
		{
			panelId = argI + strlen("panelClassName=");
			matchType = panelMatchType::CLASS_NAME;
		}
		else if (StringIBeginsWith(argI, "opacity="))
		{
			opacity = float(atof(argI + strlen("opacity=")));
			didMatchProperty = true;
		}
	}

	if (panelId.empty()) {
		advancedfx::Warning("PanelId cannot be empty.\n");
		return;
	}

	if (!didMatchProperty) {
		advancedfx::Warning("Did not match any style property.\n");
		return;
	}

	auto parentPanel = ((u_char***)g_myPanoramaWrapper.pHudPanel)[0][1];
	if (!parentPanel) {
		advancedfx::Warning("Root panel is 0\n");
		return;
	}


	if (matchType == panelMatchType::ID) {
		u_char* targetPanel = g_myPanoramaWrapper.findChildInLayoutFile(parentPanel, panelId.c_str());

		if (0 == targetPanel) {
			advancedfx::Warning("Could not find panel %s\n", panelId.c_str());
			return;
		}

		auto res = ((CUIPanel*)targetPanel)->setOpacity(std::clamp(opacity, 0.0f, 1.0f));
		if (!res) {
			advancedfx::Warning("Could not set opacity property for %s\n", panelId.c_str());
		}
	} else if (matchType == panelMatchType::CLASS_NAME) {
		auto foundPanels = g_myPanoramaWrapper.findChildrenInLayoutFileByClassName(parentPanel, panelId.c_str());
		if (foundPanels.empty()) {
			advancedfx::Warning("Could not find panels with className %s\n", panelId.c_str());
		} else {
			for (auto panel : foundPanels) {
				((CUIPanel*)panel)->setOpacity(std::clamp(opacity, 0.0f, 1.0f));
			}
		}
	}
}

CON_COMMAND(mirv_panorama, "")
{
	const auto arg0 = args->ArgV(0);
	int argc = args->ArgC();

	if (2 <= argc)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("panelStyle", arg1)) {
			if (3 <= argc) {
				CSubWrpCommandArgs subArgs(args, 2);
				applyStyleProperty_Console(&subArgs);
			} else {
				advancedfx::Message(
					"%s %s panelStyle <option> <option>\n"
					"Where <option> at least 2 arguments are required: panelId or class and property to set.\n"
					"%s %s:\n"
					"\tpanelId=<str>\n"
					"\tpanelClassName=<str>\n"
					"\topacity=<fValue>\n"
					"Example:\n"
					"%s %s panelId=trueview_row opacity=0\n"
					"%s %s panelClassName=HudPerfStatsBasics opacity=0\n"
					"Warning: if matching by className the style would be applied to all instances.\n"
					, arg0, arg1
					, arg0, arg1
					, arg0, arg1
					, arg0, arg1
				);
			}
			return;
		}
	}

	advancedfx::Message(
		"%s panelStyle [...] - Set style for specific panorama panel.\n"
		, arg0
	);
}

