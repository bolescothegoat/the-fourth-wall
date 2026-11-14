THE FOURTH WALL VR - BETA 0.2.0
================================
Makes characters look into your eyes: scene characters in OStim / SexLab, and optionally NPCs
near you. Eyes only; heads and bodies keep their normal animation. Skyrim VR only.

STATUS: BETA. Tested by a small number of volunteers in OStim (no crashes; eyes meet the
headset position, also at a distance). NOT yet verified: SexLab in VR, VR's own third-person
camera mod setups, very long sessions. Reports of any kind are welcome.

REQUIREMENTS
 - Skyrim VR 1.4.15 and SKSEVR 2.0.12
 - Address Library for SKSE Plugins AND "VR Address Library for SKSEVR" (Nexus mod 58101);
   the file Data/SKSE/Plugins/version-1-4-15-0.csv must exist. Without it this plugin does
   nothing (it checks first, so it cannot crash the game over it).
 - Latest Visual C++ Redistributable (x64)
 - OStim and/or SexLab for scenes (OStim is the tested one)

INSTALL
 Install as a mod (MO2 / Vortex) or copy the SKSE folder into Data/. Do NOT install together
 with the normal "The Fourth Wall" (SE/AE) plugin. Settings: Data/SKSE/Plugins/FourthWall.ini
 (no in-game menu in the VR build).

IF SOMETHING GOES WRONG
 Set iVRLevel = 0 (off) or 1 (read only) in FourthWall.ini and tell me what you saw.
 Please send: FourthWallVR.log (My Games > Skyrim VR > SKSE), any crash-*.log from there, and
 which of OStim / SexLab you use. For a deeper look, set bVRProbe = 1 and bDiag = 1 first.

Source (GPL-3.0-or-later): https://github.com/bolescothegoat/the-fourth-wall  (folder vr/)
