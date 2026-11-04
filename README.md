# The Fourth Wall - Eye Contact

An SKSE plugin for Skyrim SE / AE that makes player and NPC eyes look into the camera. Eyes only: the head keeps whatever pose the game or animation gives it.

- Nexus page: https://www.nexusmods.com/skyrimspecialedition/mods/197923
- License: GNU GPL v3 or later (see `LICENSE`)
- Build instructions: `BUILDING.md`
- Development timeline: `DEVELOPMENT_HISTORY.md`

## How it works

The plugin hooks the engine's face-animation update (Address Library ID 26598, chained with MFG Fix) and writes the four eye-direction values into the final face buffer after everything else has run. It works with OStim, SexLab and Flower Girls scenes, and for the player's idle behaviour outside scenes.

## Built with AI assistance

The C++ was written by Claude (Anthropic) through Claude Code, from my designs, my reading of the open-source MFG Fix code, and my in-game testing and logs. The mod contains no AI-made art, textures, meshes or sounds.

## Credits

- MFG Fix and MFG Fix NG (andrelo; antpillager, crajjj, naitro2010)
- OStim / OStimNG (VersuchDrei and contributors; GPL-3.0-or-later)
- SKSE Menu Framework (Thiago099 / QTR-Modding)
- CommonLibSSE-NG (alandtse fork)
- Microsoft Detours
- Look At Me SKSE, whose limits in OStim scenes were the reason I started this

Full third-party licences: `THIRD_PARTY_NOTICES.txt`.

## Issues

Bug reports are welcome. Please attach `Documents/My Games/Skyrim Special Edition/SKSE/FourthWall.log`.
