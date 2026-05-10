#include "stdafx.h"

#include "ClientEntitySystem.h"
#include "Globals.h"
#include "SchemaSystem.h"
#include "WrpConsole.h"

#include "../deps/release/prop/AfxHookSource/SourceSdkShared.h"
#include "../deps/release/prop/AfxHookSource/SourceInterfaces.h"
#include "../deps/release/prop/cs2/sdk_src/public/cdll_int.h"
#include "../shared/StringTools.h"

#include <map>

extern SOURCESDK::CS2::ISource2EngineToClient* g_pEngineToClient;

namespace {

std::map<uint64_t, int> g_PlayerColorOverrides;

bool isPlayingDemo()
{
	if (!g_pEngineToClient) return false;
	if (auto demoFile = g_pEngineToClient->GetDemoFile()) {
		return demoFile->IsPlayingDemo();
	}
	return false;
}

bool parseXuid(const char* arg, uint64_t& outXuid)
{
	if (nullptr == arg || '\0' == arg[0]) return false;
	outXuid = StringIBeginsWith(arg, "x") ? strtoull(arg + 1, nullptr, 10) : strtoull(arg, nullptr, 10);
	return 0 != outXuid;
}

const char* colorNameFromIndex(int color)
{
	switch (color) {
	case 0: return "blue";
	case 1: return "green";
	case 2: return "yellow";
	case 3: return "orange";
	case 4: return "purple";
	default: return "unknown";
	}
}

bool parseColorIndex(const char* arg, int& outColor)
{
	if (nullptr == arg || '\0' == arg[0]) return false;

	if (0 == _stricmp(arg, "blue") || 0 == _stricmp(arg, "cyan")) {
		outColor = 0;
		return true;
	}
	if (0 == _stricmp(arg, "green")) {
		outColor = 1;
		return true;
	}
	if (0 == _stricmp(arg, "yellow")) {
		outColor = 2;
		return true;
	}
	if (0 == _stricmp(arg, "orange")) {
		outColor = 3;
		return true;
	}
	if (0 == _stricmp(arg, "purple") || 0 == _stricmp(arg, "pink")) {
		outColor = 4;
		return true;
	}

	char* end = nullptr;
	long parsed = strtol(arg, &end, 10);
	if (end && '\0' == *end && 0 <= parsed && parsed <= 4) {
		outColor = static_cast<int>(parsed);
		return true;
	}

	return false;
}

bool looksLikeSteamId(uint64_t xuid)
{
	return 70000000000000000ULL < xuid && xuid < 80000000000000000ULL;
}

uint64_t getControllerXuid(CEntityInstance* controller)
{
	if (!controller) return 0;
	return *(uint64_t*)((unsigned char*)controller + g_clientDllOffsets.CBasePlayerController.m_steamID);
}

const char* getControllerName(CEntityInstance* controller)
{
	if (!controller) return "";
	return (const char*)((unsigned char*)controller + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName);
}

bool setControllerColor(CEntityInstance* controller, int color)
{
	if (!controller) return false;
	*(int*)((unsigned char*)controller + g_clientDllOffsets.CCSPlayerController.m_iCompTeammateColor) = color;
	return true;
}

int getControllerColor(CEntityInstance* controller)
{
	if (!controller) return -1;
	return *(int*)((unsigned char*)controller + g_clientDllOffsets.CCSPlayerController.m_iCompTeammateColor);
}

int applyPlayerColorOverrides(bool print)
{
	if (!g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) return 0;

	int applied = 0;
	for (int i = 1; i <= 64; ++i) {
		auto controller = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i);
		if (!controller) continue;

		const uint64_t xuid = getControllerXuid(controller);
		if (!looksLikeSteamId(xuid)) continue;

		auto it = g_PlayerColorOverrides.find(xuid);
		if (it == g_PlayerColorOverrides.end()) continue;

		setControllerColor(controller, it->second);
		++applied;

		if (print) {
			advancedfx::Message(
				"applied entry=%i x%llu color=%i (%s) name=%s\n",
				i,
				(unsigned long long)xuid,
				it->second,
				colorNameFromIndex(it->second),
				getControllerName(controller)
			);
		}
	}

	return applied;
}

void printPlayerColorHelp(const char* arg0)
{
	advancedfx::Message(
		"%s byXuid add x<ullXuid> blue|green|yellow|orange|purple|<0-4>\n"
		"%s byXuid remove x<ullXuid>\n"
		"%s clear\n"
		"%s print\n"
		"%s apply - Re-apply current overrides during demo playback.\n"
		"%s inspect - Print current player-controller color values.\n"
		"Color indexes map to CS2's cl_teammate_color_1..5 cvars:\n"
		"\t0 blue/cyan, 1 green, 2 yellow, 3 orange, 4 purple/pink\n"
		"Notes:\n"
		"\tThis writes CCSPlayerController::m_iCompTeammateColor for local demo playback / recording.\n",
		arg0,
		arg0,
		arg0,
		arg0,
		arg0,
		arg0
	);
}

void inspectPlayerColors()
{
	advancedfx::Message("mirv_player_color inspect: playingDemo=%i overrides=%llu\n", isPlayingDemo() ? 1 : 0, (unsigned long long)g_PlayerColorOverrides.size());

	if (!g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) {
		advancedfx::Warning("Client entity system is not available.\n");
		return;
	}

	advancedfx::Message("entry / xuid / color / name\n");
	for (int i = 1; i <= 64; ++i) {
		auto controller = (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, i);
		if (!controller) continue;

		const uint64_t xuid = getControllerXuid(controller);
		if (!looksLikeSteamId(xuid)) continue;

		const int color = getControllerColor(controller);
		advancedfx::Message(
			"%i / x%llu / %i (%s) / %s%s\n",
			i,
			(unsigned long long)xuid,
			color,
			colorNameFromIndex(color),
			getControllerName(controller),
			g_PlayerColorOverrides.find(xuid) != g_PlayerColorOverrides.end() ? " / override" : ""
		);
	}
}

} // namespace

CON_COMMAND(mirv_player_color, "Visually overrides CS2 player teammate colors during demo playback.")
{
	const char* arg0 = args->ArgV(0);
	const int argc = args->ArgC();

	if (2 <= argc) {
		const char* arg1 = args->ArgV(1);

		if (0 == _stricmp("byXuid", arg1)) {
			if (3 <= argc) {
				const char* arg2 = args->ArgV(2);

				if (0 == _stricmp("add", arg2) && 5 <= argc) {
					uint64_t xuid = 0;
					if (!parseXuid(args->ArgV(3), xuid)) {
						advancedfx::Warning("Invalid XUID: %s\n", args->ArgV(3));
						return;
					}

					int color = 0;
					if (!parseColorIndex(args->ArgV(4), color)) {
						advancedfx::Warning("Invalid color: %s\n", args->ArgV(4));
						printPlayerColorHelp(arg0);
						return;
					}

					g_PlayerColorOverrides[xuid] = color;
					if (isPlayingDemo()) {
						applyPlayerColorOverrides(false);
					}
					return;
				}

				if (0 == _stricmp("remove", arg2) && 4 <= argc) {
					uint64_t xuid = 0;
					if (!parseXuid(args->ArgV(3), xuid)) {
						advancedfx::Warning("Invalid XUID: %s\n", args->ArgV(3));
						return;
					}

					g_PlayerColorOverrides.erase(xuid);
					return;
				}
			}

			printPlayerColorHelp(arg0);
			return;
		}

		if (0 == _stricmp("clear", arg1)) {
			g_PlayerColorOverrides.clear();
			return;
		}

		if (0 == _stricmp("print", arg1)) {
			for (const auto& entry : g_PlayerColorOverrides) {
				advancedfx::Message("x%llu %i (%s)\n", (unsigned long long)entry.first, entry.second, colorNameFromIndex(entry.second));
			}
			return;
		}

		if (0 == _stricmp("apply", arg1)) {
			if (!isPlayingDemo()) {
				advancedfx::Warning("Not applying player colors because demo playback is not active.\n");
				return;
			}

			const int applied = applyPlayerColorOverrides(true);
			advancedfx::Message("mirv_player_color apply: applied %i override(s).\n", applied);
			return;
		}

		if (0 == _stricmp("inspect", arg1)) {
			inspectPlayerColors();
			return;
		}
	}

	printPlayerColorHelp(arg0);
}
