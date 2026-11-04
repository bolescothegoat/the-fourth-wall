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
		float        nearbyRadius{ 12.0f };     // metres from the camera

		// ---- eye movement ----
		float strength{ 1.0f };       // 0..1 multiplier on the look values
		float speed{ 300.0f };        // deg/s the eyes may move
		float giveUpAngle{ 180.0f };  // camera further than this from where she faces: eyes drift back to centre
		float verticalBoost{ 3.0f };  // multiplies the up/down deflection (many heads barely show vertical looks)

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

		// ---- internal (not in the menu or the ini) ----
		float overshoot{ 1.0f };  // values above 1.0 are invalid in the face data, so no overshoot
		bool  writeLayer1{ true };
		bool  writeTarget{ true };
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
	// which scene frameworks were found at startup: bit 0 OStim API, bit 1 SexLab, bit 2 Flower Girls
	std::uint32_t Frameworks();
	void SetKeyCapture(bool a_on);  // the menu is waiting for a new hotkey: the hotkey itself must not fire
	bool IsKeyCapture();
}
