# The Fourth Wall - Eye Contact - source code (version 1.1.3)

Free software under the GNU GPL v3 or later (see `LICENSE`). Copyright (C) 2026 Bolescothegoat.
Third-party licences: `THIRD_PARTY_NOTICES.txt`.

An SKSE plugin (C++23) for Skyrim SE 1.5.97 and AE 1.6.x / 1.7.x (one DLL; the engine addresses come from Address Library). It makes characters look into the camera with their eyes only, by hooking the engine's
face-animation update and writing the four eye-direction values (and, during the flutter, the blink / brow / lid values) into the final
face buffer after everything else has run.

Built with AI assistance (Claude, by Anthropic) from the author's designs and in-game testing.

## Layout

| File | What it is |
|---|---|
| `src/main.cpp` | SKSE plugin entry, logging, hotkey input sink |
| `src/Eyes.h`, `src/Eyes.cpp` | Settings, camera / head-bone maths, the face-update hook (Microsoft Detours), glance / release / flutter, scene detection (OStim API, SexLab and Flower Girls factions), player idle rule |
| `src/UI.h`, `src/UI.cpp` | SKSE Menu Framework pages |
| `src/OStimApi.h` | Layout of OStim's public plugin thread API |
| `src/FaceLabAPI.h` | Interop header for the optional FaceLab integration (found at run time; nothing happens when FaceLab is absent) |
| `src/PCH.h` | Precompiled header |

## Building

Prerequisites: Visual Studio 2022+ (MSVC, C++23), CMake 3.21+, Ninja, [vcpkg](https://github.com/microsoft/vcpkg), and:

1. **CommonLibSSE-NG 15.5.0** - a checkout of <https://github.com/alandtse/CommonLibSSE-NG> at tag `v15.5.0` (commit `8eadded`). Not the
   older CharmedBaryon repository: it cannot read the AE 1.7 Address Library files. Set the environment variable `COMMONLIB_SSE_FOLDER`
   to the checkout folder.
2. **SKSEMenuFramework.h** - not included here. Download `resources/SKSEMenuFramework.h` from
   <https://github.com/QTR-Modding/SKSE-Menu-Framework-3> and put it in `src/`.
3. vcpkg packages (`spdlog`, `fmt`, `detours`, `directxtk`, ...) are installed automatically from `vcpkg.json`; set `VCPKG_ROOT` to your vcpkg folder.

```
cmake --preset release
cmake --build --preset release
```

The DLL is written to `dist/SKSE/Plugins/FourthWall.dll`.

## Notes for hackers

* The hook target is `BSFaceGenAnimationData::KeyframesUpdate`: Address Library ID 26598 on AE (1.6.x and 1.7.x) and 25983 on SE 1.5.97
  (`REL::RelocationID`). The SE id was matched to the AE one by comparing the two Address Library files. The DLL exports both
  `SKSEPlugin_Version` (AE) and `SKSEPlugin_Query` (SE) and has no CompatibleVersions list. MFG Fix replaces the same function with
  its own Detours hook; a second Detours hook chains on top of it, so this plugin runs after MFG Fix and writes last.
* Face-data layout (offsets, layers, eye-direction slots 8-11, blink 0/1, brow-up 6/7, squint 12/13) follows the layout documented by
  the MFG Fix source: <https://github.com/andrelo1/mfgfix>.
* Values above 1.0 are invalid in the final face buffer, so everything written there is clamped to 0..1.
* Settings are plain `key = value` lines in `Data/SKSE/Plugins/FourthWall.ini`, written by the menu.
* Procedural Penis Animations (AccuratePenetration.dll, v0.0.37) re-sets the face after the engine's morph step in oral scenes. `Eyes.cpp` finds PPA's saved callee pointer (guarded by a prologue check and the module name) and swaps it for a function that re-applies the eye slots before calling the original target. If PPA is absent or a different version, this is skipped.
