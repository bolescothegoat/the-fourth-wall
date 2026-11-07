# Development history

Dates before 2026-11-03 come from my own file timestamps, edit audit log and project notes on my PC. They are not third-party timestamps. Nexus dates (UTC) come from the Nexus Mods API.

| Date | Event |
|---|---|
| 2026-10-09 | Project created as "OEyeContact". First source files and build script (`OStimApi.h`, `PCH.h`, `_build.cmd`, `CMakePresets.json`). Motivation: Look At Me SKSE did not work on OStim scene actors and turned the whole head. |
| 2026-10-09 | v0.5.0 confirmed in-game in OStim, SexLab and Flower Girls scenes. |
| 2026-10-09 | Decided to release free, as a general eye-contact mod that also works for the player. Renamed "The Fourth Wall". Release candidate 1.0.0 and a GPL-3.0 source zip built; clean build of the source zip verified. |
| 2026-10-10 | Fixed mouth closing in oral scenes (yield to PPA facial presets). |
| 2026-10-26 | Yields to FaceLab's EyesCross for cross-eyed expressions. |
| 2026-11-03 | v1 published on Nexus with a GPL v3 source zip uploaded as a separate download. |
| 2026-11-04 | v1.1 and source 1.1 published. This repository's first commit is the 1.1.0 source exactly as uploaded to Nexus. |
| 2026-11-04 to 2026-11-05 | Per-eye gaze calibration against camera angle and distance, driven through FaceLab (gaze layer, API v6). |
| 2026-11-06 | Rest period after a FaceLab crossed-eyes face so both eyes return together. A Nexus user reported eyes far left/right while idle; investigation started. A PPA 0.0.39 late-pass experiment broke PPA's bone bending and sounds in testing, so it was removed again before release. |
| 2026-11-06 | "Pause during conversations" added at the request of a Nexus user, for conversation camera mods. 1.1.1 prepared. |
| 2026-11-07 | Frame-rate work after a Nexus user's report: measured with the plugin's own perf line and FaceLab's timers. Gaze updates that have not changed are no longer sent to FaceLab; the Gaze engine setting gets four choices (FaceLab for you and scene actors / for everyone / MFG for everyone / FaceLab for you only) and background characters nearby default to MFG. 1.1.2. |

From the first commit of this repository onward, git history is the record.
