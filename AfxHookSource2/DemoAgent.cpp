#include "stdafx.h"

#include "DemoAgent.h"

#include "ClientEntitySystem.h"
#include "Globals.h"
#include "SchemaSystem.h"
#include "WrpConsole.h"

#include "../deps/release/prop/AfxHookSource/SourceSdkShared.h"
#include "../deps/release/prop/cs2/sdk_src/public/cdll_int.h"
#include "../shared/StringTools.h"

#include <string>

extern SOURCESDK::CS2::ISource2EngineToClient* g_pEngineToClient;

namespace {

constexpr const char* kVypaModel = "agents/models/tm_jungle_raider/tm_jungle_raider_variante.vmdl";

using CBaseModelEntity_SetModel_t = bool(__fastcall*)(void* entity, const char* modelName);

CBaseModelEntity_SetModel_t g_SetModel = nullptr;
std::string g_AllModelOverride;
bool g_ApplyEveryFrame = false;

bool isPlayingDemo()
{
	if (!g_pEngineToClient) return false;
	if (auto demoFile = g_pEngineToClient->GetDemoFile()) {
		return demoFile->IsPlayingDemo();
	}
	return false;
}

const char* resolveModelAlias(const char* arg)
{
	if (!arg || !arg[0]) return nullptr;
	if (0 == _stricmp(arg, "vypa")) return kVypaModel;
	return arg;
}

bool looksLikeSteamId(uint64_t xuid)
{
	return 70000000000000000ULL < xuid && xuid < 80000000000000000ULL;
}

bool isDemoPlayerPawn(CEntityInstance* entity)
{
	if (!entity || !entity->IsPlayerPawn()) return false;

	const char* className = entity->GetClientClassName();
	return className && 0 == _stricmp(className, "C_CSPlayerPawn");
}

CEntityInstance* getControllerForPawn(CEntityInstance* pawn)
{
	if (!pawn || !pawn->IsPlayerPawn() || !g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) return nullptr;

	const auto controllerHandle = pawn->GetPlayerControllerHandle();
	if (!controllerHandle.IsValid()) return nullptr;

	const int entry = controllerHandle.GetEntryIndex();
	if (entry < 0) return nullptr;

	auto controller = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, entry);
	if (!controller || !controller->IsPlayerController()) return nullptr;

	return controller;
}

const char* getControllerName(CEntityInstance* controller)
{
	if (!controller) return "";
	return (const char*)((unsigned char*)controller + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName);
}

uint64_t getControllerXuid(CEntityInstance* controller)
{
	if (!controller) return 0;
	return *(uint64_t*)((unsigned char*)controller + g_clientDllOffsets.CBasePlayerController.m_steamID);
}

unsigned char* getModelState(CEntityInstance* entity)
{
	if (!entity) return nullptr;

	auto bodyComponent = *(unsigned char**)((unsigned char*)entity + g_clientDllOffsets.C_BaseEntity.m_CBodyComponent);
	if (!bodyComponent) return nullptr;

	auto skeletonInstance = bodyComponent + g_clientDllOffsets.CBodyComponentSkeletonInstance.m_skeletonInstance;
	return skeletonInstance + g_clientDllOffsets.CSkeletonInstance.m_modelState;
}

uintptr_t getModelHandle(CEntityInstance* entity)
{
	auto modelState = getModelState(entity);
	if (!modelState) return 0;
	return *(uintptr_t*)(modelState + g_clientDllOffsets.CModelState.m_hModel);
}

uint32_t getModelNameSymbol(CEntityInstance* entity)
{
	auto modelState = getModelState(entity);
	if (!modelState) return 0;
	return *(uint32_t*)(modelState + g_clientDllOffsets.CModelState.m_ModelName);
}

bool resolveSetModel(HMODULE clientDll, bool print)
{
	if (g_SetModel) return true;
	if (!clientDll) return false;

	const char* patterns[] = {
		// CounterStrikeSharp's current server-side CBaseModelEntity_SetModel pattern.
		// Kept as a client-side experiment; inspect output tells us whether it matches.
		"40 53 48 83 EC ?? 48 8B D9 4C 8B C2 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B 01 FF 50 ?? 48 8B 54 24 ?? 48 8B CB E8 ?? ?? ?? ?? 48 83 C4 ?? 5B C3",
		// Slightly looser candidate for the same shape if the final epilogue changes.
		"40 53 48 83 EC ?? 48 8B D9 4C 8B C2 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B 01 FF 50 ?? 48 8B 54 24 ?? 48 8B CB E8 ?? ?? ?? ??"
	};

	for (const char* pattern : patterns) {
		const auto addr = getAddress(clientDll, pattern);
		if (addr) {
			g_SetModel = (CBaseModelEntity_SetModel_t)addr;
			if (print) {
				advancedfx::Message("mirv_demo_agent: resolved client SetModel candidate at 0x%p.\n", (void*)addr);
			}
			return true;
		}
	}

	if (print) {
		advancedfx::Warning("mirv_demo_agent: client SetModel signature not resolved.\n");
	}

	return false;
}

int applyAllPlayers(const char* modelName, bool print)
{
	if (!modelName || !modelName[0]) return 0;
	if (!isPlayingDemo()) {
		if (print) advancedfx::Warning("mirv_demo_agent: not applying because demo playback is not active.\n");
		return 0;
	}
	if (!g_SetModel) {
		if (print) advancedfx::Warning("mirv_demo_agent: SetModel is not resolved.\n");
		return 0;
	}
	if (!g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) {
		if (print) advancedfx::Warning("mirv_demo_agent: client entity system is not available.\n");
		return 0;
	}

	int applied = 0;
	const int highest = GetHighestEntityIndex();
	for (int i = 1; i <= highest; ++i) {
		auto entity = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i);
		if (!isDemoPlayerPawn(entity)) continue;

		g_SetModel(entity, modelName);
		++applied;

		if (print) {
			auto controller = getControllerForPawn(entity);
			const uint64_t xuid = getControllerXuid(controller);
			advancedfx::Message(
				"mirv_demo_agent: applied entry=%i controllerXuid=%s%llu name=%s model=%s\n",
				i,
				looksLikeSteamId(xuid) ? "x" : "",
				(unsigned long long)xuid,
				getControllerName(controller),
				modelName
			);
		}
	}

	return applied;
}

void inspectPlayers()
{
	advancedfx::Message(
		"mirv_demo_agent inspect: playingDemo=%i setModel=%p allOverride=%s applyEveryFrame=%i\n",
		isPlayingDemo() ? 1 : 0,
		(void*)g_SetModel,
		g_AllModelOverride.empty() ? "<none>" : g_AllModelOverride.c_str(),
		g_ApplyEveryFrame ? 1 : 0
	);

	if (!g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) {
		advancedfx::Warning("mirv_demo_agent: client entity system is not available.\n");
		return;
	}

	const int highest = GetHighestEntityIndex();
	advancedfx::Message("entry / class / modelHandle / modelNameSymbol / controllerXuid / name\n");
	for (int i = 1; i <= highest; ++i) {
		auto entity = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i);
		if (!isDemoPlayerPawn(entity)) continue;

		auto controller = getControllerForPawn(entity);
		const uint64_t xuid = getControllerXuid(controller);
		advancedfx::Message(
			"%i / %s / 0x%p / 0x%08x / %s%llu / %s\n",
			i,
			entity->GetClientClassName() ? entity->GetClientClassName() : "",
			(void*)getModelHandle(entity),
			getModelNameSymbol(entity),
			looksLikeSteamId(xuid) ? "x" : "",
			(unsigned long long)xuid,
			getControllerName(controller)
		);
	}
}

void printHelp(const char* arg0)
{
	advancedfx::Message(
		"%s all set vypa|<modelPath> - Configure the model used by apply.\n"
		"%s all clear - Clear the all-player model override.\n"
		"%s apply - Apply the current override once during demo playback.\n"
		"%s inspect - Print model diagnostics for current player pawns.\n"
		"Built-in aliases:\n"
		"\tvypa -> %s\n"
		"Notes:\n"
		"\tPrototype for local demo playback / recording only. Requires -insecure and active demo playback.\n",
		arg0,
		arg0,
		arg0,
		arg0,
		kVypaModel
	);
}

} // namespace

void HookDemoAgent(HMODULE clientDll)
{
	resolveSetModel(clientDll, true);
}

void DemoAgent_OnFrameRenderPass()
{
	// Re-applying SetModel every frame is intentionally avoided while this is experimental.
}

CON_COMMAND(mirv_demo_agent, "Prototype CS2 demo playback player model / agent overrides.")
{
	const char* arg0 = args->ArgV(0);
	const int argc = args->ArgC();

	if (2 <= argc) {
		const char* arg1 = args->ArgV(1);

		if (0 == _stricmp("all", arg1)) {
			if (4 <= argc && 0 == _stricmp("set", args->ArgV(2))) {
				const char* model = resolveModelAlias(args->ArgV(3));
				if (!model || !model[0]) {
					advancedfx::Warning("mirv_demo_agent: invalid model.\n");
					return;
				}

				g_AllModelOverride = model;
				g_ApplyEveryFrame = false;
				advancedfx::Message("mirv_demo_agent: configured all-player model override: %s\n", g_AllModelOverride.c_str());
				return;
			}

			if (3 <= argc && 0 == _stricmp("clear", args->ArgV(2))) {
				g_AllModelOverride.clear();
				g_ApplyEveryFrame = false;
				return;
			}

			printHelp(arg0);
			return;
		}

		if (0 == _stricmp("apply", arg1)) {
			if (g_AllModelOverride.empty()) {
				advancedfx::Warning("mirv_demo_agent: no all-player model override configured.\n");
				return;
			}

			const int applied = applyAllPlayers(g_AllModelOverride.c_str(), true);
			advancedfx::Message("mirv_demo_agent apply: applied %i player model override(s).\n", applied);
			return;
		}

		if (0 == _stricmp("inspect", arg1)) {
			inspectPlayers();
			return;
		}
	}

	printHelp(arg0);
}
