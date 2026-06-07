#pragma once

#include <windows.h>

void HookDemoSkin(HMODULE clientDll);
void DemoSkin_OnClientFrameStageNotify(int curStage, bool isAfter);
