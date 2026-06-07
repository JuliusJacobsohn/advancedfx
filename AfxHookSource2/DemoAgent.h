#pragma once

#include <Windows.h>

void HookDemoAgent(HMODULE clientDll);
void DemoAgent_OnClientFrameStageNotify(int curStage, bool isAfter);
