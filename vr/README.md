# The Fourth Wall VR (beta)

An experimental Skyrim VR (1.4.15, SKSEVR) build of The Fourth Wall. It is a **separate DLL** (`FourthWallVR.dll`), built from the same code as the SE/AE plugin with `FW_VR_BUILD` defined. Do not install it next to the normal plugin.

## Status

Beta 0.2.0. It has been run by volunteer testers (OStim, no SexLab):

- the hook target was correct, the face-data safety check passed, there were no crashes or errors in the log;
- during OStim scenes the characters' eyes met the headset position correctly, also at a distance;
- not yet verified: SexLab in VR, a VR third-person camera (the logs only ever showed the VR camera state, 9), long sessions, save/load cycles, other VR mod combinations.

I have no VR hardware, so I cannot test it myself. Reports are welcome (see `USER_README.txt`, which is the text shipped with the release).

## What is different from the SE/AE plugin

- **Engine address by offset.** VR's Address Library does not carry the id this plugin uses on SE/AE, so the face-update function is located by a fixed offset in SkyrimVR.exe (runtime 1.4.15 only). The plugin refuses to continue if the runtime version differs, if the address is not inside the executable's code section, or if `Data/SKSE/Plugins/version-1-4-15-0.csv` (VR Address Library for SKSEVR, Nexus mod 58101) is missing, because the library ends the game with an error when it cannot read that file.
- **Safety levels** (`iVRLevel` in `FourthWall.ini`): `0` log only, nothing hooked; `1` hook installed but read-only, logs headset vs camera positions; `2` full behaviour, enabled only after the face data has looked plausible on 60 consecutive calls. All memory reads of engine structures are wrapped in structured exception handling.
- **Camera.** The look-at target is the player camera root, which a tester confirmed meets the eyes correctly. The engine's own headset nodes (`PlayerCharacter::GetVRNodeData()->HmdNode` and `UprightHmdNode`) are read crash-protected and logged next to it; `bVRUseHmdNode = 1` aims at `HmdNode` instead. (An earlier 0.1.0 searched the scene graph for a node named `HmdNode` and found nothing, because these nodes are not under the player's 3D.)
- **Player idle look is off in VR by default.** The engine reports no first-person state in VR (camera state 9), so that rule's first-person check never applies, and your own face is not seen anyway. `bPlayerIdle = 1` turns it back on.
- **Quiet log.** The headset / camera probe thread runs at level 1 and when `bVRProbe = 1`; at level 2 it is off by default.
- **No in-game menu.** The SKSE Menu Framework page is not part of the VR build; configure through the ini.
- **Self-test.** `tools/vr_selftest.cpp` compiles the exact crash-protected probes from `src/VrSafe.h` into a small program and feeds them good data, garbage and invalid pointers (20 checks).

## Building

Same prerequisites as the main plugin (`../BUILDING.md`: Visual Studio, CMake, Ninja, vcpkg, CommonLibSSE-NG 15.5.0 at tag `v15.5.0` via `COMMONLIB_SSE_FOLDER`), plus the library's `extern/openvr` folder populated from <https://github.com/ValveSoftware/openvr> (`headers/` and `lib/win64/openvr_api.lib`).

```
cd vr
cmake --preset release
cmake --build --preset release
```

The DLL is written to `vr/dist/SKSE/Plugins/FourthWallVR.dll`. The self-test is the extra target `vr_selftest`.

## Licence

GNU GPL v3 or later, like the rest of this repository. Copyright (C) 2026 Bolescothegoat. Built with AI assistance (Claude, by Anthropic) from the author's designs and testers' logs.
