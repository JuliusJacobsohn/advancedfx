# HLAE CS2 Demo Launch Notes

These notes document the local test setup for launching CS2 through HLAE, loading a demo, scheduling commands during playback, and reading the console dump.

## Required local paths

HLAE:

```text
C:\Users\Julius\source\repos\advancedfx\build\Release\dist\bin\HLAE.exe
```

CS2:

```text
C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\cs2.exe
```

HLAE CS2 hook DLL:

```text
C:\Users\Julius\source\repos\advancedfx\build\Release\dist\bin\x64\AfxHookSource2.dll
```

Example demo:

```text
C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\replays\match730_003816038387630997543_1135542072_274.dem
```

The player roster for this demo is documented in [demo_playback_test_demo_roster.md](demo_playback_test_demo_roster.md).

CS2 console dump produced by `-condebug`:

```text
C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\console.log
```

## Required launch flags

Use these CS2 flags:

```text
-steam -insecure -console -condebug -novid
```

Important details:

- `-insecure` is required. `AfxHookSource2` refuses to work without it.
- `-condebug` writes console output to `game\csgo\console.log`.
- `-console` opens the console and makes command debugging easier.
- `-novid` is optional, but shortens startup.

The HLAE CS2 launcher code normally builds `-steam -insecure` itself. For command-line automation, use HLAE's custom loader mode and inject `AfxHookSource2.dll` explicitly.

## Launch command

Run from PowerShell:

```powershell
$hlae = "C:\Users\Julius\source\repos\advancedfx\build\Release\dist\bin\HLAE.exe"
$cs2 = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\cs2.exe"
$hook = "C:\Users\Julius\source\repos\advancedfx\build\Release\dist\bin\x64\AfxHookSource2.dll"

$cmd = "-steam -insecure -console -condebug -novid +playdemo replays/match730_003816038387630997543_1135542072_274.dem"

$argString = '-customLoader -autoStart -noGui ' +
  '-programPath "' + $cs2 + '" ' +
  '-cmdLine "' + $cmd + '" ' +
  '-hookDllPath "' + $hook + '" ' +
  '-addEnv "SteamPath=C:\Program Files (x86)\Steam" ' +
  '-addEnv "SteamClientLaunch=1" ' +
  '-addEnv "SteamGameId=730" ' +
  '-addEnv "SteamAppId=730" ' +
  '-addEnv "SteamOverlayGameId=730"'

Start-Process -FilePath $hlae -ArgumentList $argString -WorkingDirectory (Split-Path $hlae)
```

Do not pass the HLAE arguments as a simple PowerShell array unless you verify quoting. A path with spaces can be split incorrectly and cause:

```text
AfxError #2002: Loader could not create requested process.
GetLastWin32Error = 2: The system cannot find the file specified
```

The demo path in `+playdemo` is relative to:

```text
C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo
```

So this absolute file:

```text
C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\replays\match730_003816038387630997543_1135542072_274.dem
```

is loaded with:

```text
+playdemo replays/match730_003816038387630997543_1135542072_274.dem
```

## Scheduling commands during demo playback

Use HLAE's `mirv_cmd` command system. It can schedule commands by demo time or demo tick.

Schedule by demo time:

```text
mirv_cmd addAtTime 1 __mirv_info
```

Schedule by demo tick:

```text
mirv_cmd addAtTick 12345 __mirv_info
```

Print scheduled commands:

```text
mirv_cmd print
```

Example launch command that schedules an HLAE command one second into demo playback, prints the schedule, and loads the demo:

```powershell
$cmd = "-steam -insecure -console -condebug -novid +mirv_cmd addAtTime 1 __mirv_info +mirv_cmd print +playdemo replays/match730_003816038387630997543_1135542072_274.dem"
```

Keep scheduled `+mirv_cmd ...` launch commands before `+playdemo`. In local CS2 1.41.6.0 testing, putting scheduled commands after `+playdemo` printed them in the `[CommandLine]` log but did not reliably execute the schedule.

For feature tests, replace `__mirv_info` with the command under test, for example:

```text
mirv_cmd addAtTime 42.5 mirv_chat_insert name caster team=spec visibility=all message bought two smokes
```

Fast early-tick tests:

```text
mirv_cmd addAtTick 300 mirv_chat_insert byXuid x76561197962023477 team=CT alive=1 visibility=all message handler_all_probe
mirv_cmd addAtTick 310 demo_pause
```

`mirv_cmd addAtTick` is relative to demo playback, not CS2 process launch. In the local test demo, tick `300` maps to a `CGameRules` log tick around `3400`. Use `demo_pause` shortly after the command when a screenshot is needed, because chat rows fade quickly. For log-only probes, schedule `quit` after the final command to keep runs short.

## Reading the console dump

Read the latest console output:

```powershell
Get-Content -LiteralPath "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\console.log" -Tail 260
```

Filter for the important launch and demo markers:

```powershell
Select-String -LiteralPath "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\console.log" `
  -Pattern "CommandLine|AfxHookSource2|playdemo|Playing Demo|Host activate|Demo is version|insecure"
```

Useful success markers:

```text
[CommandLine] -steam -insecure -console -condebug ...
| AfxHookSource2 (...)
[Demo] Requesting playback of 'replays/...dem'
[Demo] playing demo from 'replays/...dem'
[HostStateManager] Host activate: Playing Demo (...)
[Prediction] Demo is version ...
```

If a scheduled HLAE command such as `__mirv_info` runs after demo activation, the log should show another `AfxHookSource2` banner after the `Host activate: Playing Demo` line.

## Notes

- Avoid PowerShell TCP/netcon scripts for this workflow. Windows Defender can flag that pattern. Launch-time `+commands`, cfg files, and `mirv_cmd` scheduling are enough for repeatable demo tests.
- Keep `-insecure` in every automated launch command.
- `console.log` is appended/rewritten by CS2 depending on launch behavior. Check timestamps when comparing multiple test runs.
