// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - The Fourth Wall - Eye Contact. See LICENSE.

#pragma once

namespace Eyes
{
	// Every user-facing setting, with the out-of-the-box defaults. Saved to Data/SKSE/Plugins/FourthWall.ini by the menu.
	struct Settings
	{
		// ---- general ----
		bool         startOn{ true };           // eye contact is already on when the game starts
		std::int32_t hotkey{ 0xD2 };            // DirectInput scancode, 0xD2 = Insert; 0 = no hotkey
		int          who{ 1 };                  // 0 all, 1 female only, 2 male only
		bool         includePlayer{ true };    // your own character looks at the camera in scenes too
		bool         playerIdle{ true };       // ...and whenever you stand still in third person / the free camera
		float        idleDelay{ 1.0f };        // seconds of standing still before the idle gaze starts
		float        idleGiveUp{ 110.0f };     // while idle: camera further than this from where you face (e.g. behind you) = no eye contact
		bool         useOStim{ true };          // scene frameworks to take actors from
		bool         useSexLab{ true };
		bool         useFlowerGirls{ true };
		bool         nearby{ false };           // also drive ordinary NPCs near the camera (outside OStim scenes)
int          maxNearby{ 5 };            // at most this many nearby characters are driven at once, the nearest first (0 = no limit)
		bool         nearbyOnScreen{ true };    // only nearby characters the camera can see
		float        nearbyRadius{ 12.0f };     // metres from the camera
		int          nearbyWho{ 3 };            // who the nearby characters are: 0 everyone, 1 female only, 2 male only, 3 the same as 'who' (the default: as before)
		bool         pauseInConversation{ true };  // nobody looks at the camera while a conversation is open (and for a moment after): conversation cameras stage the two people for each other

		// ---- eye movement ----
		float strength{ 1.0f };       // 0..1 multiplier on the look values
		float speed{ 300.0f };        // deg/s the eyes may move
		float giveUpAngle{ 180.0f };  // camera further than this from where she faces: eyes drift back to centre
		float verticalBoost{ 3.0f };  // multiplies the up/down deflection (many heads barely show vertical looks)
int          gazeMode{ 0 };
		float        maleYawScale{ 0.85f };   // FaceLab gaze, male characters: sideways amount (smaller eyes turn further per weight)
float        maleVertScale{ 1.0f };
		float        crossScale{ 1.0f };      // FaceLab gaze: how strongly the eyes turn inward for a close camera (1.0 = the calibrated table)   // ... up / down amount
		int          gazeModeDummy_{ 0 };         // 0 FaceLab for you and scene actors / MFG for the rest (default), 1 FaceLab for everyone, 2 MFG for everyone, 3 FaceLab for you only / MFG for the rest

		// ---- glance ----
		bool  lookAway{ true };
		float awayEveryMin{ 5.0f };
		float awayEveryMax{ 12.0f };
		float awayDurMin{ 0.8f };
		float awayDurMax{ 2.0f };
		float awayAngle{ 22.0f };

		// ---- release ----
		bool  release{ true };
		float relEveryMin{ 10.0f };
		float relEveryMax{ 15.0f };
		float relDurMin{ 3.0f };
		float relDurMax{ 5.0f };
		float fadeOut{ 0.5f };  // seconds to let go of the eyes
		float fadeIn{ 1.2f };   // seconds to take them back
		float faceSettle{ 0.8f };  // seconds the eyes rest on their own after a FaceLab face with crossed eyes ends, before the gaze comes back (both eyes return together)

		// ---- flutter ----
		bool  flutter{ true };
		float fluEveryMin{ 10.0f };
		float fluEveryMax{ 30.0f };
		float fluStrength{ 0.5f };    // how wide the eyes open
		float fluBrow{ 0.25f };       // small extra brow lift
		int   fluStyle{ 0 };          // 0 negative lids, 1 happy (mood), 2 fear (mood)
		float fluBlinkTime{ 0.25f };  // seconds for each of the two deliberate blinks
		float fluGap{ 0.04f };        // seconds between the two blinks

		// ---- calibration ----
		float maxYaw{ 28.0f };    // degrees that count as full left/right (MFG Fix: fTrackEyeXY)
		float maxPitch{ 40.0f };  // degrees that count as full up/down (MFG Fix: fTrackEyeZ)
		bool  invertX{ false };
		bool  invertY{ false };
		float yawOffset{ 0.0f };
		float pitchOffset{ 0.0f };

		// ---- diagnostics ----
		bool  diag{ false };
		float diagInterval{ 1.5f };

#ifdef FW_VR_BUILD
		// ---- VR diagnostic build only (iVRLevel in FourthWall.ini) ----
		bool  vrProbe{ false };   // keep the headset / camera probe log running at level 2 (bVRProbe); levels 0 and 1 always run it
		bool  vrUseHmd{ false };  // aim at the engine's HmdNode instead of the camera root (bVRUseHmdNode); the camera root was confirmed correct by a tester
		int   vrLevel{ 0 };  // 0 probe only (log, no hooks) | 1 observe (the hook only reads; headset / camera probe) | 2 full (only after the struct probe passed)
#endif

		// ---- internal (not in the menu or the ini) ----
		float overshoot{ 1.0f };  // values above 1.0 are invalid in the face data, so no overshoot
		bool  writeLayer1{ true };
		bool  writeTarget{ true };
		bool  holdNeutral{ false };  // testing aid (not saved): hold the engine's four look values at zero on the player, nothing else runs
	};

	Settings& Cfg();

	void LoadConfig();              // compiled defaults, then Data/SKSE/Plugins/FourthWall.ini on top
	bool SaveConfig();              // writes every setting to the ini
	void ResetDefaults();           // back to the compiled defaults (does not save)
	bool Init();                    // connects to OStim's thread API, installs the face hook, starts the driver
	void Shutdown();
	void Toggle();                  // hotkey: eye contact on/off, with a HUD message
	bool IsOn();
	void SetOn(bool a_on);
	std::int32_t Hotkey();
	// FaceLab for the gaze: 0 not installed, 1 installed but older than the gaze layer (API v6), 2 ready
	int FaceLabStatus();
	// which scene frameworks were found at startup: bit 0 OStim API, bit 1 SexLab, bit 2 Flower Girls
	std::uint32_t Frameworks();
	void SetKeyCapture(bool a_on);  // the menu is waiting for a new hotkey: the hotkey itself must not fire
	bool IsKeyCapture();
}
