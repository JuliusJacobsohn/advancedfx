# Local HLAE Build Notes

This repo already has the upstream build instructions in `how_to_build.txt`.
These notes capture the local Windows setup used successfully on this machine.

## Tooling installed

- Visual Studio 2022 Build Tools with C++ and .NET desktop build workloads.
- Kitware CMake at `C:\Program Files\CMake\bin\cmake.exe`.
- Rust via rustup with targets:
  - `x86_64-pc-windows-msvc`
  - `i686-pc-windows-msvc`
- WiX .NET tool `wix` version `5.0.2`.
- GNU gettext, available as `msgfmt`.
- Python 3 with `polib`.

## Important local details

The MSYS CMake from `c:\devkitPro\msys2\usr\bin\cmake.exe` appears earlier on
`PATH` in some shells and does not provide Visual Studio generators. Use the
Kitware CMake path explicitly or make sure `C:\Program Files\CMake\bin` comes
first on `PATH`.

Visual Studio Build Tools is detected by `vswhere` only when `-products *` is
used. The local `CMakeLists.txt` includes that fix.

`installer/build_installer.bat` was made idempotent for WiX setup by trying
`dotnet tool update --global wix --version 5.0.2` before falling back to install.

## Build commands

From the repo root:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:USERPROFILE\.cargo\bin;$env:USERPROFILE\.dotnet\tools;$env:PATH"

& "C:\Program Files\CMake\bin\cmake.exe" -E make_directory build\Release
Set-Location build\Release

& "C:\Program Files\CMake\bin\cmake.exe" -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" -T "v143" -A "Win32" ../..
& "C:\Program Files\CMake\bin\cmake.exe" --build . --config Release -v -- -r
& "C:\Program Files\CMake\bin\cmake.exe" --install . --config Release -v
```

## Fast AfxHookSource2 iteration

After the initial configure/build has populated `build\Release`, rebuild only
the x64 hook DLL while working on CS2 / `AfxHookSource2` changes:

```powershell
$env:PATH = "C:\Program Files\CMake\bin;$env:USERPROFILE\.cargo\bin;$env:USERPROFILE\.dotnet\tools;$env:PATH"

Set-Location build\Release\advancedfx-x64
& "C:\Program Files\CMake\bin\cmake.exe" --build . --config Release --target AfxHookSource2 -- /m:1 -r
```

In this local multibuild layout, the x64 `AfxHookSource2` target writes the
rebuilt hook directly to `build\Release\dist\bin\x64\AfxHookSource2.dll` and
its PDB to `build\Release\dist\pdb\x64\AfxHookSource2.pdb`.

Use the full top-level build/install only when installer, package, or shared
dependency output changed. If MSVC reports `C1041` for a shared PDB during a
parallel build, rebuild this target with `/m:1` as shown above.

## Outputs

After a successful build/install:

- Runnable local HLAE folder: `build\Release\dist\bin`
- Zip package: `build\Release\dist\hlae.zip`
- Installer: `build\Release\dist\HLAE_Setup.exe`

## Observed warnings

The build completed successfully with warnings:

- Node engine warnings because local Node is `v20.16.0`, while the snippets
  package asks for Node `>=24.14.0`.
- Existing C# warning in `hlae\AfxRgbaLutVoronoiGenerator.cs`.
- Build Tools layout warning about a missing ATL/MFC lib path.
- CMake/MSBuild incremental dependency warning for an AfxHookSource NASM rule.
