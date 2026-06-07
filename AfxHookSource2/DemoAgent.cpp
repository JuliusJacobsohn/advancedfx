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
#include <unordered_map>
#include <vector>

extern SOURCESDK::CS2::ISource2EngineToClient* g_pEngineToClient;

namespace {

constexpr const char* kVypaModel = "agents/models/tm_jungle_raider/tm_jungle_raider_variante.vmdl";

using CBaseModelEntity_SetModel_t = bool(__fastcall*)(void* entity, const char* modelName);

CBaseModelEntity_SetModel_t g_SetModel = nullptr;
std::unordered_map<uint64_t, std::string> g_PlayerModelOverrides;

struct PlayerPawnInfo {
	int entry = 0;
	CEntityInstance* pawn = nullptr;
	CEntityInstance* controller = nullptr;
	uint64_t xuid = 0;
};

struct AppliedModelState {
	CEntityInstance* pawn = nullptr;
	std::string modelName;
	uintptr_t modelHandle = 0;
	uint32_t modelNameSymbol = 0;
};

std::unordered_map<uint64_t, AppliedModelState> g_AppliedModelStates;

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

bool parseXuid(const char* arg, uint64_t& outXuid)
{
	if (!arg || !arg[0]) return false;
	if ('x' == arg[0] || 'X' == arg[0]) ++arg;
	if (!arg[0]) return false;

	char* end = nullptr;
	const auto value = _strtoui64(arg, &end, 10);
	if (!end || *end) return false;

	outXuid = value;
	return 0 != outXuid;
}

bool parseSlot(const char* arg, int& outSlot)
{
	if (!arg || !arg[0]) return false;

	char* end = nullptr;
	const long value = strtol(arg, &end, 10);
	if (!end || *end || value < 1 || 10 < value) return false;

	outSlot = (int)value;
	return true;
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

std::vector<PlayerPawnInfo> collectPlayerPawns()
{
	std::vector<PlayerPawnInfo> result;

	if (!g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) {
		return result;
	}

	const int highest = GetHighestEntityIndex();
	for (int i = 1; i <= highest; ++i) {
		auto entity = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i);
		if (!isDemoPlayerPawn(entity)) continue;

		auto controller = getControllerForPawn(entity);
		const uint64_t xuid = getControllerXuid(controller);
		if (!looksLikeSteamId(xuid)) continue;

		PlayerPawnInfo info;
		info.entry = i;
		info.pawn = entity;
		info.controller = controller;
		info.xuid = xuid;
		result.push_back(info);
	}

	return result;
}

bool ensureCanApply(bool print)
{
	if (!isPlayingDemo()) {
		if (print) advancedfx::Warning("mirv_demo_agent: not applying because demo playback is not active.\n");
		return false;
	}
	if (!g_SetModel) {
		if (print) advancedfx::Warning("mirv_demo_agent: SetModel is not resolved.\n");
		return false;
	}
	if (!g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) {
		if (print) advancedfx::Warning("mirv_demo_agent: client entity system is not available.\n");
		return false;
	}
	return true;
}

bool applyPlayerModel(const PlayerPawnInfo& info, const char* modelName, bool print)
{
	if (!modelName || !modelName[0] || !info.pawn || !g_SetModel) return false;

	g_SetModel(info.pawn, modelName);

	AppliedModelState state;
	state.pawn = info.pawn;
	state.modelName = modelName;
	state.modelHandle = getModelHandle(info.pawn);
	state.modelNameSymbol = getModelNameSymbol(info.pawn);
	g_AppliedModelStates[info.xuid] = state;

	if (print) {
		advancedfx::Message(
			"mirv_demo_agent: applied slotEntry=%i xuid=%s%llu name=%s model=%s\n",
			info.entry,
			looksLikeSteamId(info.xuid) ? "x" : "",
			(unsigned long long)info.xuid,
			getControllerName(info.controller),
			modelName
		);
	}

	return true;
}

bool needsApply(const PlayerPawnInfo& info, const char* modelName)
{
	if (!modelName || !modelName[0] || !info.pawn) return false;

	const auto stateIt = g_AppliedModelStates.find(info.xuid);
	if (g_AppliedModelStates.end() == stateIt) return true;

	const auto& state = stateIt->second;
	if (state.pawn != info.pawn) return true;
	if (state.modelName != modelName) return true;
	if (state.modelHandle != getModelHandle(info.pawn)) return true;
	if (state.modelNameSymbol != getModelNameSymbol(info.pawn)) return true;

	return false;
}

int applyConfiguredOverrides(bool print, bool onlyIfNeeded)
{
	if (!ensureCanApply(print)) return 0;

	int applied = 0;
	const auto players = collectPlayerPawns();
	for (const auto& player : players) {
		auto it = g_PlayerModelOverrides.find(player.xuid);
		if (g_PlayerModelOverrides.end() == it) continue;
		if (onlyIfNeeded && !needsApply(player, it->second.c_str())) continue;

		if (applyPlayerModel(player, it->second.c_str(), print)) {
			++applied;
		}
	}

	return applied;
}

bool applySingleXuid(uint64_t xuid, bool print)
{
	if (!ensureCanApply(print)) return false;

	const auto modelIt = g_PlayerModelOverrides.find(xuid);
	if (g_PlayerModelOverrides.end() == modelIt) {
		if (print) advancedfx::Warning("mirv_demo_agent: no model configured for xuid=%llu.\n", (unsigned long long)xuid);
		return false;
	}

	const auto players = collectPlayerPawns();
	for (const auto& player : players) {
		if (player.xuid != xuid) continue;
		return applyPlayerModel(player, modelIt->second.c_str(), print);
	}

	if (print) advancedfx::Warning("mirv_demo_agent: no current player pawn found for xuid=%llu.\n", (unsigned long long)xuid);
	return false;
}

bool setXuidOverride(uint64_t xuid, const char* modelName, bool applyNow)
{
	if (!modelName || !modelName[0]) {
		advancedfx::Warning("mirv_demo_agent: invalid model.\n");
		return false;
	}

	g_PlayerModelOverrides[xuid] = modelName;
	advancedfx::Message(
		"mirv_demo_agent: configured xuid=%s%llu model=%s\n",
		looksLikeSteamId(xuid) ? "x" : "",
		(unsigned long long)xuid,
		modelName
	);

	if (applyNow && isPlayingDemo()) {
		applySingleXuid(xuid, true);
	}

	return true;
}

bool setSlotOverride(int slot, const char* modelName)
{
	const auto players = collectPlayerPawns();
	if (slot < 1 || (int)players.size() < slot) {
		advancedfx::Warning("mirv_demo_agent: slot %i is not available; run mirv_demo_agent inspect for current slots.\n", slot);
		return false;
	}

	const auto& player = players[slot - 1];
	advancedfx::Message(
		"mirv_demo_agent: slot %i resolves to xuid=%s%llu name=%s.\n",
		slot,
		looksLikeSteamId(player.xuid) ? "x" : "",
		(unsigned long long)player.xuid,
		getControllerName(player.controller)
	);
	return setXuidOverride(player.xuid, modelName, true);
}

void inspectPlayers()
{
	advancedfx::Message(
		"mirv_demo_agent inspect: playingDemo=%i setModel=%p configured=%llu\n",
		isPlayingDemo() ? 1 : 0,
		(void*)g_SetModel,
		(unsigned long long)g_PlayerModelOverrides.size()
	);

	if (!g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) {
		advancedfx::Warning("mirv_demo_agent: client entity system is not available.\n");
		return;
	}

	const auto players = collectPlayerPawns();
	advancedfx::Message("slot / entry / class / modelHandle / modelNameSymbol / xuid / name / configuredModel\n");
	for (size_t i = 0; i < players.size(); ++i) {
		const auto& player = players[i];
		const auto modelIt = g_PlayerModelOverrides.find(player.xuid);
		advancedfx::Message(
			"%llu / %i / %s / 0x%p / 0x%08x / %s%llu / %s / %s\n",
			(unsigned long long)(i + 1),
			player.entry,
			player.pawn->GetClientClassName() ? player.pawn->GetClientClassName() : "",
			(void*)getModelHandle(player.pawn),
			getModelNameSymbol(player.pawn),
			looksLikeSteamId(player.xuid) ? "x" : "",
			(unsigned long long)player.xuid,
			getControllerName(player.controller),
			g_PlayerModelOverrides.end() == modelIt ? "<none>" : modelIt->second.c_str()
		);
	}
}

void printHelp(const char* arg0)
{
	advancedfx::Message(
		"%s inspect - Print current player pawn slots, XUIDs, model state, and configured overrides.\n"
		"%s xuid <steamid64> set vypa|<modelPath> - Configure and apply one player's model.\n"
		"%s xuid <steamid64> clear - Clear one player's configured model.\n"
		"%s slot <1-10> set vypa|<modelPath> - Configure and apply the current slot's model by resolved XUID.\n"
		"%s apply - Apply all configured XUID model overrides once during demo playback.\n"
		"%s clear - Clear all configured model overrides.\n"
		"Built-in aliases:\n"
		"\tvypa -> %s\n"
		"Notes:\n"
		"\tPrototype for local demo playback / recording only. Requires -insecure and active demo playback.\n"
		"\tSlot numbers are temporary helpers from current pawn enumeration; XUID overrides are the stable targeting key.\n",
		arg0,
		arg0,
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
	if (g_PlayerModelOverrides.empty()) return;
	applyConfiguredOverrides(false, true);
}

CON_COMMAND(mirv_demo_agent, "Prototype CS2 demo playback player model / agent overrides.")
{
	const char* arg0 = args->ArgV(0);
	const int argc = args->ArgC();

	if (2 <= argc) {
		const char* arg1 = args->ArgV(1);

		if (0 == _stricmp("inspect", arg1)) {
			inspectPlayers();
			return;
		}

		if (0 == _stricmp("apply", arg1)) {
			const int applied = applyConfiguredOverrides(true, false);
			advancedfx::Message("mirv_demo_agent apply: applied %i configured player model override(s).\n", applied);
			return;
		}

		if (0 == _stricmp("clear", arg1)) {
			g_PlayerModelOverrides.clear();
			g_AppliedModelStates.clear();
			advancedfx::Message("mirv_demo_agent: cleared all configured model overrides.\n");
			return;
		}

		if (0 == _stricmp("xuid", arg1)) {
			uint64_t xuid = 0;
			if (argc < 4 || !parseXuid(args->ArgV(2), xuid)) {
				advancedfx::Warning("mirv_demo_agent: expected xuid <steamid64> set|clear ...\n");
				return;
			}

			if (0 == _stricmp("set", args->ArgV(3))) {
				if (argc < 5) {
					advancedfx::Warning("mirv_demo_agent: expected xuid <steamid64> set vypa|<modelPath>.\n");
					return;
				}

				const char* model = resolveModelAlias(args->ArgV(4));
				setXuidOverride(xuid, model, true);
				return;
			}

			if (0 == _stricmp("clear", args->ArgV(3))) {
				const auto erased = g_PlayerModelOverrides.erase(xuid);
				g_AppliedModelStates.erase(xuid);
				advancedfx::Message(
					"mirv_demo_agent: %s override for xuid=%s%llu.\n",
					erased ? "cleared" : "had no",
					looksLikeSteamId(xuid) ? "x" : "",
					(unsigned long long)xuid
				);
				return;
			}

			printHelp(arg0);
			return;
		}

		if (0 == _stricmp("slot", arg1)) {
			int slot = 0;
			if (argc < 5 || !parseSlot(args->ArgV(2), slot) || 0 != _stricmp("set", args->ArgV(3))) {
				advancedfx::Warning("mirv_demo_agent: expected slot <1-10> set vypa|<modelPath>.\n");
				return;
			}

			const char* model = resolveModelAlias(args->ArgV(4));
			setSlotOverride(slot, model);
			return;
		}
	}

	printHelp(arg0);
}
