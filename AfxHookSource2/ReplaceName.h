#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>

void HookReplaceName(HMODULE clientDll);

const char* GetReplaceNameOverride(int controllerIndex, uint64_t steamId);
