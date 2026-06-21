#include "stdafx.h"

#include "DemoSkin.h"

#include "ClientEntitySystem.h"
#include "Globals.h"
#include "SchemaSystem.h"
#include "WrpConsole.h"

#include "../deps/release/prop/AfxHookSource/SourceSdkShared.h"
#include "../deps/release/prop/cs2/sdk_src/public/cdll_int.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern SOURCESDK::CS2::ISource2EngineToClient* g_pEngineToClient;

namespace {

enum class SkinTarget {
	ActiveWeapon,
	AllWeapons,
	Gloves
};

struct SkinRule {
	SkinTarget target = SkinTarget::ActiveWeapon;
	int team = 0;
	int paintKit = 0;
	float wear = 0.0f;
	int seed = 0;
	bool hasStatTrak = false;
	int statTrak = -1;
	int weaponDefIndex = 0;
	int itemDefIndex = 0;
	bool hasMeshGroup = false;
	uint64_t meshGroup = 0;
};

struct PlayerPawnInfo {
	int entry = 0;
	CEntityInstance* pawn = nullptr;
	CEntityInstance* controller = nullptr;
	uint64_t xuid = 0;
};

struct ApplyStats {
	int configured = 0;
	int present = 0;
	int applicable = 0;
	int candidates = 0;
	int patched = 0;
	int skipped = 0;
};

struct WeaponAssignment {
	uint64_t xuid = 0;
	int team = 0;
	int weaponDefIndex = 0;
};

std::unordered_map<uint64_t, std::vector<SkinRule>> g_SkinRules;
std::unordered_map<uint32_t, WeaponAssignment> g_WeaponAssignments;
std::unordered_set<uint64_t> g_RefreshRequestedItemIds;
bool g_WarnedMissingOffsets = false;

bool isPlayingDemo()
{
	if (!g_pEngineToClient) return false;
	if (auto demoFile = g_pEngineToClient->GetDemoFile()) {
		return demoFile->IsPlayingDemo();
	}
	return false;
}

bool hasCoreWeaponOffsets()
{
	return
		0 != g_clientDllOffsets.C_BasePlayerPawn.m_pWeaponServices &&
		0 != g_clientDllOffsets.CPlayer_WeaponServices.m_hActiveWeapon &&
		0 != g_clientDllOffsets.C_EconEntity.m_AttributeManager &&
		0 != g_clientDllOffsets.C_EconEntity.m_bAttributesInitialized &&
		0 != g_clientDllOffsets.C_EconEntity.m_OriginalOwnerXuidLow &&
		0 != g_clientDllOffsets.C_EconEntity.m_OriginalOwnerXuidHigh &&
		0 != g_clientDllOffsets.C_EconEntity.m_nFallbackPaintKit &&
		0 != g_clientDllOffsets.C_EconEntity.m_nFallbackSeed &&
		0 != g_clientDllOffsets.C_EconEntity.m_flFallbackWear &&
		0 != g_clientDllOffsets.C_EconEntity.m_nFallbackStatTrak &&
		0 != g_clientDllOffsets.C_AttributeContainer.m_Item &&
		0 != g_clientDllOffsets.C_EconItemView.m_iItemDefinitionIndex &&
		0 != g_clientDllOffsets.C_EconItemView.m_iEntityQuality &&
		0 != g_clientDllOffsets.C_EconItemView.m_iItemID &&
		0 != g_clientDllOffsets.C_EconItemView.m_iItemIDHigh &&
		0 != g_clientDllOffsets.C_EconItemView.m_iItemIDLow &&
		0 != g_clientDllOffsets.C_EconItemView.m_iAccountID &&
		0 != g_clientDllOffsets.C_EconItemView.m_bInitialized;
}

bool hasGloveOffsets()
{
	return hasCoreWeaponOffsets() && 0 != g_clientDllOffsets.CCSPlayerPawn.m_EconGloves;
}

bool ensureSkinOffsets(bool print)
{
	if (hasCoreWeaponOffsets()) return true;
	if (print || !g_WarnedMissingOffsets) {
		g_WarnedMissingOffsets = true;
		advancedfx::Warning("mirv_demo_skin: required skin schema offsets are not available in this CS2 build.\n");
	}
	return false;
}

int getCurrentDemoTick()
{
	if (!g_pEngineToClient) return -1;
	if (auto demoFile = g_pEngineToClient->GetDemoFile()) {
		if (demoFile->IsPlayingDemo()) return demoFile->GetDemoTick();
	}
	return -1;
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

bool parseInt(const char* arg, int& value)
{
	if (!arg || !arg[0]) return false;
	char* end = nullptr;
	const long parsed = strtol(arg, &end, 10);
	if (!end || *end) return false;
	value = (int)parsed;
	return true;
}

bool parseUInt64(const char* arg, uint64_t& value)
{
	if (!arg || !arg[0]) return false;
	char* end = nullptr;
	const auto parsed = _strtoui64(arg, &end, 0);
	if (!end || *end) return false;
	value = parsed;
	return true;
}

bool parseFloat(const char* arg, float& value)
{
	if (!arg || !arg[0]) return false;
	char* end = nullptr;
	const float parsed = strtof(arg, &end);
	if (!end || *end) return false;
	value = parsed;
	return true;
}

bool parseTeam(const char* arg, int& team)
{
	if (!arg || !arg[0]) return false;
	if (0 == _stricmp(arg, "any") || 0 == _stricmp(arg, "all")) {
		team = 0;
		return true;
	}
	if (0 == _stricmp(arg, "t") || 0 == _stricmp(arg, "terrorist") || 0 == _stricmp(arg, "terrorists")) {
		team = 2;
		return true;
	}
	if (0 == _stricmp(arg, "ct") || 0 == _stricmp(arg, "counterterrorist") || 0 == _stricmp(arg, "counterterrorists")) {
		team = 3;
		return true;
	}
	return false;
}

const char* getValueArg(const char* arg, const char* key)
{
	if (!arg || !key) return nullptr;
	const size_t keyLen = strlen(key);
	if (0 != _strnicmp(arg, key, keyLen)) return nullptr;
	if ('=' != arg[keyLen]) return nullptr;
	return arg + keyLen + 1;
}

bool isDemoPlayerPawn(CEntityInstance* entity)
{
	if (!entity || !entity->IsPlayerPawn()) return false;

	const char* className = entity->GetClientClassName();
	return className && 0 == _stricmp(className, "C_CSPlayerPawn");
}

CEntityInstance* getEntityFromHandle(SOURCESDK::CS2::CBaseHandle handle)
{
	if (!handle.IsValid() || !g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) return nullptr;
	const int entry = handle.GetEntryIndex();
	if (entry < 0) return nullptr;
	return (CEntityInstance*)g_GetEntityFromIndex(*g_pEntityList, entry);
}

CEntityInstance* getControllerForPawn(CEntityInstance* pawn)
{
	if (!pawn || !pawn->IsPlayerPawn()) return nullptr;
	return getEntityFromHandle(pawn->GetPlayerControllerHandle());
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

uint32_t getAccountId(uint64_t xuid)
{
	return (uint32_t)(xuid & 0xffffffffULL);
}

int getPawnTeam(CEntityInstance* pawn)
{
	if (!pawn) return 0;
	return *(uint8_t*)((unsigned char*)pawn + g_clientDllOffsets.C_BaseEntity.m_iTeamNum);
}

bool ruleMatchesPlayer(const SkinRule& rule, const PlayerPawnInfo& player)
{
	return 0 == rule.team || getPawnTeam(player.pawn) == rule.team;
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

unsigned char* getWeaponServices(CEntityInstance* pawn)
{
	if (!pawn || !pawn->IsPlayerPawn()) return nullptr;
	return *(unsigned char**)((unsigned char*)pawn + g_clientDllOffsets.C_BasePlayerPawn.m_pWeaponServices);
}

std::vector<CEntityInstance*> getAllWeapons(CEntityInstance* pawn)
{
	std::vector<CEntityInstance*> result;
	std::unordered_set<int> seenEntries;

	if (auto active = getEntityFromHandle(pawn->GetActiveWeaponHandle())) {
		result.push_back(active);
		seenEntries.insert(active->GetHandle().GetEntryIndex());
	}

	auto weaponServices = getWeaponServices(pawn);
	if (!weaponServices) return result;
	if (0 == g_clientDllOffsets.CPlayer_WeaponServices.m_hMyWeapons) return result;

	auto vector = weaponServices + g_clientDllOffsets.CPlayer_WeaponServices.m_hMyWeapons;
	const int count = *(int*)vector;
	if (count < 0 || 128 < count) return result;

	auto memory = *(uint32_t**)(vector + 8);
	if (!memory) return result;

	for (int i = 0; i < count; ++i) {
		SOURCESDK::CS2::CBaseHandle handle(memory[i]);
		auto weapon = getEntityFromHandle(handle);
		if (!weapon) continue;

		const int entry = handle.GetEntryIndex();
		if (seenEntries.find(entry) != seenEntries.end()) continue;

		result.push_back(weapon);
		seenEntries.insert(entry);
	}

	return result;
}

bool isKnife(CEntityInstance* weapon)
{
	if (!weapon) return false;
	const char* className = weapon->GetClientClassName();
	if (!className) return false;
	for (const char* cursor = className; *cursor; ++cursor) {
		if (0 == _strnicmp(cursor, "knife", 5)) return true;
	}
	return false;
}

unsigned char* getModelState(CEntityInstance* entity)
{
	if (!entity) return nullptr;

	auto bodyComponent = *(unsigned char**)((unsigned char*)entity + g_clientDllOffsets.C_BaseEntity.m_CBodyComponent);
	if (!bodyComponent) return nullptr;

	auto skeletonInstance = bodyComponent + g_clientDllOffsets.CBodyComponentSkeletonInstance.m_skeletonInstance;
	return skeletonInstance + g_clientDllOffsets.CSkeletonInstance.m_modelState;
}

void applyMeshGroup(CEntityInstance* entity, const SkinRule& rule)
{
	if (!rule.hasMeshGroup) return;
	if (0 == g_clientDllOffsets.CModelState.m_MeshGroupMask) return;

	auto modelState = getModelState(entity);
	if (!modelState) return;

	*(uint64_t*)(modelState + g_clientDllOffsets.CModelState.m_MeshGroupMask) = rule.meshGroup;
}

unsigned char* getItemViewFromEconEntity(CEntityInstance* entity)
{
	if (!entity) return nullptr;
	auto attributeManager = (unsigned char*)entity + g_clientDllOffsets.C_EconEntity.m_AttributeManager;
	return attributeManager + g_clientDllOffsets.C_AttributeContainer.m_Item;
}

int getItemDefinitionIndex(unsigned char* itemView)
{
	if (!itemView) return 0;
	return *(uint16_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iItemDefinitionIndex);
}

uint32_t getWeaponKey(CEntityInstance* weapon)
{
	if (!weapon) return 0;
	auto handle = weapon->GetHandle();
	return handle.IsValid() ? (uint32_t)handle.ToInt() : 0;
}

bool isActiveWeapon(const PlayerPawnInfo& player, CEntityInstance* weapon)
{
	if (!player.pawn || !weapon) return false;
	const auto active = player.pawn->GetActiveWeaponHandle();
	return active.IsValid() && active == weapon->GetHandle();
}

SkinRule* findWeaponRule(uint64_t xuid, int team, int weaponDefIndex, bool isActive)
{
	auto rulesIt = g_SkinRules.find(xuid);
	if (rulesIt == g_SkinRules.end()) return nullptr;

	SkinRule* best = nullptr;
	int bestScore = -1;

	for (auto& rule : rulesIt->second) {
		if (rule.target == SkinTarget::Gloves) continue;
		if (rule.target == SkinTarget::ActiveWeapon && !isActive) continue;
		if (0 != rule.team && rule.team != team) continue;
		if (0 < rule.weaponDefIndex && rule.weaponDefIndex != weaponDefIndex) continue;

		const int score =
			(0 != rule.team ? 100 : 0) +
			(0 < rule.weaponDefIndex ? 10 : 0) +
			(rule.target == SkinTarget::AllWeapons ? 1 : 0);

		if (bestScore < score) {
			best = &rule;
			bestScore = score;
		}
	}

	return best;
}

uint64_t makePreviewItemId(int paintKit, int itemDef)
{
	return 0xF000000000000000ULL | (((uint64_t)(uint32_t)paintKit) << 16) | (uint16_t)itemDef;
}

void requestCustomEconRefresh(uint64_t fakeItemId)
{
	if (!g_pEngineToClient || 0 == fakeItemId) return;
	if (!g_RefreshRequestedItemIds.insert(fakeItemId).second) return;

	g_pEngineToClient->ExecuteClientCmd(0, "clientside_reload_custom_econ", true);
}

bool patchItemView(unsigned char* itemView, const SkinRule& rule, uint64_t ownerXuid, int currentItemDef, bool allowItemDefWrite, uint64_t* outFakeItemId)
{
	if (!itemView) return false;

	int effectiveItemDef = currentItemDef;
	if (allowItemDefWrite && 0 < rule.itemDefIndex) {
		effectiveItemDef = rule.itemDefIndex;
		*(uint16_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iItemDefinitionIndex) = (uint16_t)effectiveItemDef;
	}
	if (effectiveItemDef <= 0) effectiveItemDef = currentItemDef;

	const uint64_t fakeItemId = makePreviewItemId(rule.paintKit, effectiveItemDef);
	if (outFakeItemId) *outFakeItemId = fakeItemId;
	*(uint64_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iItemID) = fakeItemId;
	*(uint32_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iItemIDHigh) = 0xffffffffu;
	*(uint32_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iItemIDLow) = (uint32_t)fakeItemId;
	*(uint32_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iAccountID) = getAccountId(ownerXuid);
	*(bool*)(itemView + g_clientDllOffsets.C_EconItemView.m_bInitialized) = true;

	if (rule.hasStatTrak && 0 <= rule.statTrak) {
		*(int32_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iEntityQuality) = 9;
	} else if (rule.hasStatTrak) {
		*(int32_t*)(itemView + g_clientDllOffsets.C_EconItemView.m_iEntityQuality) = 0;
	}

	return true;
}

bool patchWeapon(CEntityInstance* weapon, SkinRule& rule, uint64_t ownerXuid, bool print)
{
	if (!weapon) return false;
	if (isKnife(weapon) && 0 == rule.itemDefIndex) return false;

	auto itemView = getItemViewFromEconEntity(weapon);
	const int currentDef = getItemDefinitionIndex(itemView);
	if (currentDef <= 0) return false;

	if (0 < rule.weaponDefIndex && currentDef != rule.weaponDefIndex) {
		return false;
	}

	if (rule.target == SkinTarget::ActiveWeapon && 0 == rule.weaponDefIndex && !isKnife(weapon)) {
		rule.weaponDefIndex = currentDef;
	}

	*(bool*)((unsigned char*)weapon + g_clientDllOffsets.C_EconEntity.m_bAttributesInitialized) = true;
	*(uint32_t*)((unsigned char*)weapon + g_clientDllOffsets.C_EconEntity.m_OriginalOwnerXuidLow) = (uint32_t)(ownerXuid & 0xffffffffULL);
	*(uint32_t*)((unsigned char*)weapon + g_clientDllOffsets.C_EconEntity.m_OriginalOwnerXuidHigh) = (uint32_t)(ownerXuid >> 32);
	*(int32_t*)((unsigned char*)weapon + g_clientDllOffsets.C_EconEntity.m_nFallbackPaintKit) = rule.paintKit;
	*(int32_t*)((unsigned char*)weapon + g_clientDllOffsets.C_EconEntity.m_nFallbackSeed) = rule.seed;
	*(float*)((unsigned char*)weapon + g_clientDllOffsets.C_EconEntity.m_flFallbackWear) = rule.wear;
	if (rule.hasStatTrak) {
		*(int32_t*)((unsigned char*)weapon + g_clientDllOffsets.C_EconEntity.m_nFallbackStatTrak) = rule.statTrak;
	}

	uint64_t fakeItemId = 0;
	patchItemView(itemView, rule, ownerXuid, currentDef, 0 < rule.itemDefIndex, &fakeItemId);
	applyMeshGroup(weapon, rule);
	requestCustomEconRefresh(fakeItemId);

	if (print) {
		advancedfx::Message(
			"mirv_demo_skin: patched weapon entry=%i class=%s owner=x%llu def=%i paintKit=%i seed=%i wear=%.6f statTrak=%s%i meshGroup=%s0x%llx\n",
			weapon->GetHandle().GetEntryIndex(),
			weapon->GetClientClassName() ? weapon->GetClientClassName() : "",
			(unsigned long long)ownerXuid,
			currentDef,
			rule.paintKit,
			rule.seed,
			rule.wear,
			rule.hasStatTrak ? "" : "<unset>/",
			rule.hasStatTrak ? rule.statTrak : -1,
			rule.hasMeshGroup ? "" : "<unset>/",
			(unsigned long long)rule.meshGroup
		);
	}

	return true;
}

bool patchGloves(CEntityInstance* pawn, const SkinRule& rule, uint64_t ownerXuid, bool print)
{
	if (!pawn || 0 >= rule.itemDefIndex) return false;
	if (!hasGloveOffsets()) return false;

	auto itemView = (unsigned char*)pawn + g_clientDllOffsets.CCSPlayerPawn.m_EconGloves;
	const int oldDef = getItemDefinitionIndex(itemView);
	uint64_t fakeItemId = 0;
	if (!patchItemView(itemView, rule, ownerXuid, oldDef, true, &fakeItemId)) return false;
	requestCustomEconRefresh(fakeItemId);

	if (print) {
		advancedfx::Message(
			"mirv_demo_skin: patched gloves pawnEntry=%i owner=x%llu oldDef=%i itemDef=%i paintKit=%i seed=%i wear=%.6f\n",
			pawn->GetHandle().GetEntryIndex(),
			(unsigned long long)ownerXuid,
			oldDef,
			rule.itemDefIndex,
			rule.paintKit,
			rule.seed,
			rule.wear
		);
	}

	return true;
}

bool applyWeaponToPlayer(const PlayerPawnInfo& player, CEntityInstance* weapon, bool print, ApplyStats* stats)
{
	if (!weapon) return false;
	if (stats) ++stats->candidates;

	auto itemView = getItemViewFromEconEntity(weapon);
	const int currentDef = getItemDefinitionIndex(itemView);
	if (currentDef <= 0) {
		if (stats) ++stats->skipped;
		return false;
	}

	const uint32_t weaponKey = getWeaponKey(weapon);
	if (0 == weaponKey) {
		if (stats) ++stats->skipped;
		return false;
	}

	const int holderTeam = getPawnTeam(player.pawn);
	const bool active = isActiveWeapon(player, weapon);
	SkinRule* rule = nullptr;
	uint64_t ruleOwnerXuid = player.xuid;

	auto assignmentIt = g_WeaponAssignments.find(weaponKey);
	if (assignmentIt != g_WeaponAssignments.end()) {
		const auto assignment = assignmentIt->second;
		rule = findWeaponRule(assignment.xuid, assignment.team, currentDef, active);
		if (rule) {
			ruleOwnerXuid = assignment.xuid;
		} else {
			g_WeaponAssignments.erase(assignmentIt);
		}
	}

	if (!rule) {
		rule = findWeaponRule(player.xuid, holderTeam, currentDef, active);
		if (rule) {
			ruleOwnerXuid = player.xuid;
			g_WeaponAssignments[weaponKey] = { player.xuid, rule->team, currentDef };
		}
	}

	if (!rule) {
		if (stats) ++stats->skipped;
		return false;
	}

	const bool patched = patchWeapon(weapon, *rule, ruleOwnerXuid, print);
	if (stats) patched ? ++stats->patched : ++stats->skipped;
	return patched;
}

bool applyRuleToPlayer(const PlayerPawnInfo& player, SkinRule& rule, bool print, ApplyStats* stats)
{
	if (!ruleMatchesPlayer(rule, player)) return false;

	if (rule.target == SkinTarget::Gloves) {
		if (stats) ++stats->candidates;
		const bool patched = patchGloves(player.pawn, rule, player.xuid, print);
		if (stats) patched ? ++stats->patched : ++stats->skipped;
		return patched;
	}

	return false;
}

ApplyStats applyConfiguredRules(bool print)
{
	ApplyStats stats;
	for (const auto& entry : g_SkinRules) {
		stats.configured += (int)entry.second.size();
	}

	if (!isPlayingDemo() || !g_pEntityList || !*g_pEntityList || !g_GetEntityFromIndex) {
		g_WeaponAssignments.clear();
		return stats;
	}
	if (!ensureSkinOffsets(false)) return stats;

	const auto players = collectPlayerPawns();
	for (const auto& player : players) {
		auto it = g_SkinRules.find(player.xuid);
		if (it == g_SkinRules.end()) continue;

		++stats.present;
		for (auto& rule : it->second) {
			if (ruleMatchesPlayer(rule, player)) ++stats.applicable;
			if (rule.target == SkinTarget::Gloves) {
				applyRuleToPlayer(player, rule, print, &stats);
			}
		}

		for (auto weapon : getAllWeapons(player.pawn)) {
			applyWeaponToPlayer(player, weapon, print, &stats);
		}
	}

	return stats;
}

template<typename TArgs>
bool parseSkinRule(int startIndex, int argc, TArgs* args, SkinTarget target, SkinRule& rule)
{
	rule.target = target;

	for (int i = startIndex; i < argc; ++i) {
		const char* arg = args->ArgV(i);
		const char* value = nullptr;

		if ((value = getValueArg(arg, "paintKit"))) {
			if (!parseInt(value, rule.paintKit)) return false;
		} else if ((value = getValueArg(arg, "wear"))) {
			if (!parseFloat(value, rule.wear)) return false;
		} else if ((value = getValueArg(arg, "seed")) || (value = getValueArg(arg, "pattern"))) {
			if (!parseInt(value, rule.seed)) return false;
		} else if ((value = getValueArg(arg, "statTrak")) || (value = getValueArg(arg, "stattrak")) || (value = getValueArg(arg, "killCount")) || (value = getValueArg(arg, "kills"))) {
			rule.hasStatTrak = true;
			if (0 == _stricmp(value, "off") || 0 == _stricmp(value, "no") || 0 == _stricmp(value, "false")) {
				rule.statTrak = -1;
			} else if (!parseInt(value, rule.statTrak)) {
				return false;
			}
		} else if ((value = getValueArg(arg, "defIndex")) || (value = getValueArg(arg, "weaponDef"))) {
			if (target == SkinTarget::Gloves) {
				if (!parseInt(value, rule.itemDefIndex)) return false;
			} else if (!parseInt(value, rule.weaponDefIndex)) {
				return false;
			}
		} else if ((value = getValueArg(arg, "itemDef")) || (value = getValueArg(arg, "itemDefIndex"))) {
			if (!parseInt(value, rule.itemDefIndex)) return false;
		} else if ((value = getValueArg(arg, "meshGroup"))) {
			rule.hasMeshGroup = true;
			if (!parseUInt64(value, rule.meshGroup)) return false;
		} else if ((value = getValueArg(arg, "team")) || (value = getValueArg(arg, "side"))) {
			if (!parseTeam(value, rule.team)) return false;
		} else {
			return false;
		}
	}

	if (rule.paintKit <= 0) return false;
	if (rule.wear < 0.0f) rule.wear = 0.0f;
	if (1.0f < rule.wear) rule.wear = 1.0f;
	return true;
}

const char* targetName(SkinTarget target)
{
	switch (target) {
	case SkinTarget::ActiveWeapon: return "active";
	case SkinTarget::AllWeapons: return "all";
	case SkinTarget::Gloves: return "gloves";
	default: return "?";
	}
}

const char* teamName(int team)
{
	switch (team) {
	case 2: return "T";
	case 3: return "CT";
	default: return "any";
	}
}

void setRule(uint64_t xuid, const SkinRule& rule)
{
	auto& rules = g_SkinRules[xuid];
	rules.erase(
		std::remove_if(
			rules.begin(),
			rules.end(),
			[&rule](const SkinRule& existing) {
				if (existing.target != rule.target) return false;
				if (existing.team != rule.team) return false;
				if (rule.target == SkinTarget::Gloves) return true;
				if (0 == rule.weaponDefIndex) return existing.itemDefIndex == rule.itemDefIndex;
				return existing.weaponDefIndex == rule.weaponDefIndex && existing.itemDefIndex == rule.itemDefIndex;
			}
		),
		rules.end()
	);
	rules.push_back(rule);
	g_WeaponAssignments.clear();
	g_RefreshRequestedItemIds.clear();

	advancedfx::Message(
		"mirv_demo_skin: configured xuid=x%llu target=%s team=%s paintKit=%i seed=%i wear=%.6f statTrak=%s%i weaponDef=%i itemDef=%i meshGroup=%s0x%llx\n",
		(unsigned long long)xuid,
		targetName(rule.target),
		teamName(rule.team),
		rule.paintKit,
		rule.seed,
		rule.wear,
		rule.hasStatTrak ? "" : "<unset>/",
		rule.hasStatTrak ? rule.statTrak : -1,
		rule.weaponDefIndex,
		rule.itemDefIndex,
		rule.hasMeshGroup ? "" : "<unset>/",
		(unsigned long long)rule.meshGroup
	);

	if (isPlayingDemo()) {
		applyConfiguredRules(true);
	}
}

void printStatus()
{
	const ApplyStats stats = applyConfiguredRules(false);
	advancedfx::Message(
		"mirv_demo_skin status: playingDemo=%i demoTick=%i configured=%i present=%i applicable=%i assigned=%llu candidates=%i patched=%i skipped=%i refreshed=%llu coreOffsets=%i gloveOffsets=%i\n",
		isPlayingDemo() ? 1 : 0,
		getCurrentDemoTick(),
		stats.configured,
		stats.present,
		stats.applicable,
		(unsigned long long)g_WeaponAssignments.size(),
		stats.candidates,
		stats.patched,
		stats.skipped,
		(unsigned long long)g_RefreshRequestedItemIds.size(),
		hasCoreWeaponOffsets() ? 1 : 0,
		hasGloveOffsets() ? 1 : 0
	);
}

void printRules()
{
	advancedfx::Message("xuid / target / team / paintKit / seed / wear / statTrak / weaponDef / itemDef / meshGroup\n");
	for (const auto& entry : g_SkinRules) {
		for (const auto& rule : entry.second) {
			advancedfx::Message(
				"x%llu / %s / %s / %i / %i / %.6f / %s%i / %i / %i / %s0x%llx\n",
				(unsigned long long)entry.first,
				targetName(rule.target),
				teamName(rule.team),
				rule.paintKit,
				rule.seed,
				rule.wear,
				rule.hasStatTrak ? "" : "<unset>/",
				rule.hasStatTrak ? rule.statTrak : -1,
				rule.weaponDefIndex,
				rule.itemDefIndex,
				rule.hasMeshGroup ? "" : "<unset>/",
				(unsigned long long)rule.meshGroup
			);
		}
	}
}

void inspect()
{
	int configuredRules = 0;
	for (const auto& entry : g_SkinRules) {
		configuredRules += (int)entry.second.size();
	}

	advancedfx::Message(
		"mirv_demo_skin inspect: playingDemo=%i demoTick=%i configured=%llu assigned=%llu coreOffsets=%i gloveOffsets=%i myWeapons=%i meshGroup=%i\n",
		isPlayingDemo() ? 1 : 0,
		getCurrentDemoTick(),
		(unsigned long long)configuredRules,
		(unsigned long long)g_WeaponAssignments.size(),
		hasCoreWeaponOffsets() ? 1 : 0,
		hasGloveOffsets() ? 1 : 0,
		0 != g_clientDllOffsets.CPlayer_WeaponServices.m_hMyWeapons ? 1 : 0,
		0 != g_clientDllOffsets.CModelState.m_MeshGroupMask ? 1 : 0
	);

	const auto players = collectPlayerPawns();
	advancedfx::Message("slot / pawnEntry / team / xuid / name / activeWeaponEntry / activeClass / activeDef / configuredRuleCount\n");
	for (size_t i = 0; i < players.size(); ++i) {
		const auto& player = players[i];
		auto active = getEntityFromHandle(player.pawn->GetActiveWeaponHandle());
		int activeDef = 0;
		if (active) activeDef = getItemDefinitionIndex(getItemViewFromEconEntity(active));
		const auto ruleIt = g_SkinRules.find(player.xuid);
		advancedfx::Message(
			"%llu / %i / %s / x%llu / %s / %i / %s / %i / %llu\n",
			(unsigned long long)(i + 1),
			player.entry,
			teamName(getPawnTeam(player.pawn)),
			(unsigned long long)player.xuid,
			getControllerName(player.controller),
			active ? active->GetHandle().GetEntryIndex() : -1,
			active && active->GetClientClassName() ? active->GetClientClassName() : "",
			activeDef,
			ruleIt == g_SkinRules.end() ? 0ULL : (unsigned long long)ruleIt->second.size()
		);
	}
}

void printHelp(const char* arg0)
{
	advancedfx::Message(
		"%s xuid <steamid64> weapon paintKit=<id> wear=<0..1> seed=<id> defIndex=<weaponDef> [statTrak=<id|off>|killCount=<id>|kills=<id>] [meshGroup=<mask>] [team=T|CT|any] - Configure persistent normal weapon skin override.\n"
		"%s xuid <steamid64> weapon active|all ... - Backward-compatible targeting syntax; normal weapon rules default to the persistent all-weapons scan.\n"
		"%s xuid <steamid64> gloves paintKit=<id> wear=<0..1> seed=<id> defIndex=<gloveItemDef> [team=T|CT|any] - Configure glove item view override.\n"
		"%s xuid <steamid64> clear - Clear one player's skin override.\n"
		"%s byXuid add x<steamid64> active|all paintKit=<id> wear=<0..1> seed=<id> [statTrak=<id|off>|killCount=<id>|kills=<id>] [defIndex=<weaponDef>] [meshGroup=<mask>] [team=T|CT|any] - Backward-compatible weapon syntax.\n"
		"%s byXuid remove x<steamid64> - Backward-compatible clear syntax.\n"
		"%s clear - Clear all skin overrides.\n"
		"%s apply - Apply configured skin overrides once during demo playback.\n"
		"%s refresh - Run clientside_reload_custom_econ once.\n"
		"%s status - Print concise skin override status.\n"
		"%s print - Print configured rules.\n"
		"%s inspect - Print player and active weapon diagnostics.\n",
		arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0, arg0
	);
}

bool parseTarget(const char* arg, SkinTarget& target)
{
	if (!arg) return false;
	if (0 == _stricmp(arg, "active")) {
		target = SkinTarget::ActiveWeapon;
		return true;
	}
	if (0 == _stricmp(arg, "all")) {
		target = SkinTarget::AllWeapons;
		return true;
	}
	if (0 == _stricmp(arg, "gloves")) {
		target = SkinTarget::Gloves;
		return true;
	}
	return false;
}

} // namespace

void HookDemoSkin(HMODULE clientDll)
{
	(void)clientDll;
}

void DemoSkin_OnClientFrameStageNotify(int curStage, bool isAfter)
{
	(void)curStage;
	if (!isAfter || g_SkinRules.empty()) return;

	applyConfiguredRules(false);
}

CON_COMMAND(mirv_demo_skin, "Prototype CS2 demo playback weapon / glove skin overrides.")
{
	const char* arg0 = args->ArgV(0);
	const int argc = args->ArgC();

	if (2 <= argc) {
		const char* arg1 = args->ArgV(1);

		if (0 == _stricmp(arg1, "status")) {
			printStatus();
			return;
		}
		if (0 == _stricmp(arg1, "print")) {
			printRules();
			return;
		}
		if (0 == _stricmp(arg1, "inspect")) {
			inspect();
			return;
		}
		if (0 == _stricmp(arg1, "apply")) {
			const ApplyStats stats = applyConfiguredRules(true);
			advancedfx::Message("mirv_demo_skin apply: candidates=%i patched=%i skipped=%i.\n", stats.candidates, stats.patched, stats.skipped);
			return;
		}
		if (0 == _stricmp(arg1, "refresh")) {
			if (g_pEngineToClient) {
				g_pEngineToClient->ExecuteClientCmd(0, "clientside_reload_custom_econ", true);
				advancedfx::Message("mirv_demo_skin: requested clientside_reload_custom_econ.\n");
			} else {
				advancedfx::Warning("mirv_demo_skin: engine client interface unavailable.\n");
			}
			return;
		}
		if (0 == _stricmp(arg1, "clear")) {
			g_SkinRules.clear();
			g_WeaponAssignments.clear();
			g_RefreshRequestedItemIds.clear();
			advancedfx::Message("mirv_demo_skin: cleared all configured skin overrides.\n");
			return;
		}

		if (0 == _stricmp(arg1, "xuid")) {
			uint64_t xuid = 0;
			if (argc < 4 || !parseXuid(args->ArgV(2), xuid)) {
				advancedfx::Warning("mirv_demo_skin: expected xuid <steamid64> weapon|gloves|clear ...\n");
				return;
			}

			if (0 == _stricmp(args->ArgV(3), "clear")) {
				const auto erased = g_SkinRules.erase(xuid);
				g_WeaponAssignments.clear();
				g_RefreshRequestedItemIds.clear();
				advancedfx::Message("mirv_demo_skin: %s override for xuid=x%llu.\n", erased ? "cleared" : "had no", (unsigned long long)xuid);
				return;
			}

			if (0 == _stricmp(args->ArgV(3), "weapon")) {
				SkinTarget target = SkinTarget::AllWeapons;
				int firstRuleArg = 4;
				if (argc >= 5 && parseTarget(args->ArgV(4), target)) {
					if (target == SkinTarget::Gloves) {
						advancedfx::Warning("mirv_demo_skin: expected xuid <steamid64> weapon [active|all] ...\n");
						return;
					}
					firstRuleArg = 5;
				}
				if (argc <= firstRuleArg) {
					advancedfx::Warning("mirv_demo_skin: expected xuid <steamid64> weapon [active|all] paintKit=<id> ...\n");
					return;
				}
				SkinRule rule;
				if (!parseSkinRule(firstRuleArg, argc, args, target, rule)) {
					advancedfx::Warning("mirv_demo_skin: invalid weapon skin arguments.\n");
					return;
				}
				setRule(xuid, rule);
				return;
			}

			if (0 == _stricmp(args->ArgV(3), "gloves")) {
				SkinRule rule;
				if (!parseSkinRule(4, argc, args, SkinTarget::Gloves, rule) || rule.itemDefIndex <= 0) {
					advancedfx::Warning("mirv_demo_skin: expected xuid <steamid64> gloves paintKit=<id> wear=<0..1> seed=<id> defIndex=<gloveItemDef>.\n");
					return;
				}
				setRule(xuid, rule);
				return;
			}

			printHelp(arg0);
			return;
		}

		if (0 == _stricmp(arg1, "byXuid")) {
			if (argc < 4) {
				printHelp(arg0);
				return;
			}

			if (0 == _stricmp(args->ArgV(2), "remove")) {
				uint64_t xuid = 0;
				if (!parseXuid(args->ArgV(3), xuid)) {
					advancedfx::Warning("mirv_demo_skin: expected byXuid remove x<steamid64>.\n");
					return;
				}
				const auto erased = g_SkinRules.erase(xuid);
				g_WeaponAssignments.clear();
				g_RefreshRequestedItemIds.clear();
				advancedfx::Message("mirv_demo_skin: %s override for xuid=x%llu.\n", erased ? "cleared" : "had no", (unsigned long long)xuid);
				return;
			}

			if (0 == _stricmp(args->ArgV(2), "add")) {
				uint64_t xuid = 0;
				SkinTarget target = SkinTarget::ActiveWeapon;
				if (argc < 5 || !parseXuid(args->ArgV(3), xuid) || !parseTarget(args->ArgV(4), target) || target == SkinTarget::Gloves) {
					advancedfx::Warning("mirv_demo_skin: expected byXuid add x<steamid64> active|all ...\n");
					return;
				}
				SkinRule rule;
				if (!parseSkinRule(5, argc, args, target, rule)) {
					advancedfx::Warning("mirv_demo_skin: invalid byXuid add arguments.\n");
					return;
				}
				setRule(xuid, rule);
				return;
			}
		}
	}

	printHelp(arg0);
}
