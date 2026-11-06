// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - The Fourth Wall - Eye Contact. See LICENSE.

#include "PCH.h"

#include <atomic>
#include <filesystem>
#include <string>

#include "Eyes.h"
#include "SKSEMenuFramework.h"
#include "UI.h"

namespace G = ImGuiMCP;

namespace UI
{
	namespace
	{
		bool                                g_dirty = false;
		int                                 g_saveState = 0;  // 0 nothing to say, 1 saved, 2 failed
		bool                                g_confirmReset = false;
		bool                                g_waitKey = false;
		bool                                g_held[256]{};
		SKSEMenuFramework::Model::Event*    g_event = nullptr;

		const G::ImVec4 kGood{ 0.45f, 0.90f, 0.45f, 1.0f };
		const G::ImVec4 kWarn{ 1.00f, 0.75f, 0.30f, 1.0f };
		const G::ImVec4 kBad{ 1.00f, 0.40f, 0.40f, 1.0f };

		void Dirty()
		{
			g_dirty = true;
			g_saveState = 0;
		}

		void Tip(const char* a_text)
		{
			if (a_text && *a_text && G::IsItemHovered()) {
				G::SetTooltip("%s", a_text);
			}
		}

		void Check(const char* a_label, bool& a_v, const char* a_tip)
		{
			if (G::Checkbox(a_label, &a_v)) {
				Dirty();
			}
			Tip(a_tip);
		}

		void Slider(const char* a_label, float& a_v, float a_lo, float a_hi, const char* a_fmt, const char* a_tip)
		{
			G::PushItemWidth(320.0f);
			if (G::SliderFloat(a_label, &a_v, a_lo, a_hi, a_fmt)) {
				Dirty();
			}
			G::PopItemWidth();
			Tip(a_tip);
		}

		void Choice(const char* a_label, int& a_v, const char* const* a_items, int a_count, const char* a_tip)
		{
			G::PushItemWidth(320.0f);
			if (G::Combo(a_label, &a_v, a_items, a_count)) {
				Dirty();
			}
			G::PopItemWidth();
			Tip(a_tip);
		}

		void Note(const char* a_text)
		{
			G::TextWrapped("%s", a_text);
		}

		// ---- keys --------------------------------------------------------------------------------------------------------
		std::string KeyName(int a_dik)
		{
			if (a_dik <= 0) {
				return "None";
			}
			const bool ext = a_dik >= 0x80;
			const int  scan = a_dik & 0x7F;
			wchar_t    wide[64]{};
			const LONG lparam = (static_cast<LONG>(scan) << 16) | (ext ? (1L << 24) : 0L);
			if (::GetKeyNameTextW(lparam, wide, 64) > 0) {
				char utf8[128]{};
				if (::WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0) {
					return utf8;
				}
			}
			char buf[32];
			std::snprintf(buf, sizeof(buf), "Key 0x%02X", a_dik);
			return buf;
		}

		void BeginKeyWait()
		{
			for (int vk = 0; vk < 256; ++vk) {
				g_held[vk] = (::GetAsyncKeyState(vk) & 0x8000) != 0;  // keys already down (e.g. the mouse click) are ignored until released
			}
			g_waitKey = true;
			Eyes::SetKeyCapture(true);
		}

		void EndKeyWait()
		{
			g_waitKey = false;
			Eyes::SetKeyCapture(false);
		}

		// Looks for a freshly pressed key. Returns its DirectInput scancode, -1 for Esc (cancel), 0 for nothing yet.
		int PollKey()
		{
			for (int vk = 8; vk < 255; ++vk) {
				if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
					continue;  // the left / right variants are reported separately
				}
				const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
				if (g_held[vk]) {
					g_held[vk] = down;
					continue;
				}
				if (!down) {
					continue;
				}
				if (vk == VK_ESCAPE) {
					return -1;
				}
				const UINT sc = ::MapVirtualKeyW(static_cast<UINT>(vk), 4 /*MAPVK_VK_TO_VSC_EX*/);
				if ((sc & 0xFF) == 0) {
					continue;
				}
				const bool ext = (sc & 0xFF00) == 0xE000 || (sc & 0xFF00) == 0xE100;
				return static_cast<int>(sc & 0x7F) | (ext ? 0x80 : 0);
			}
			return 0;
		}

		// ---- saving ------------------------------------------------------------------------------------------------------
		void Save()
		{
			g_saveState = Eyes::SaveConfig() ? 1 : 2;
			if (g_saveState == 1) {
				g_dirty = false;
			}
		}

		void FixPairs()
		{
			auto& c = Eyes::Cfg();
			c.awayEveryMax = std::max(c.awayEveryMax, c.awayEveryMin);
			c.awayDurMax = std::max(c.awayDurMax, c.awayDurMin);
			c.relEveryMax = std::max(c.relEveryMax, c.relEveryMin);
			c.relDurMax = std::max(c.relDurMax, c.relDurMin);
			c.fluEveryMax = std::max(c.fluEveryMax, c.fluEveryMin);
		}

		void SaveBar()
		{
			FixPairs();
			G::Spacing();
			G::Separator();
			G::Spacing();
			if (G::Button("Save settings")) {
				Save();
			}
			Tip("Writes every setting to Data/SKSE/Plugins/FourthWall.ini. Changes also save automatically when you close this menu.");
			G::SameLine();
			if (!g_confirmReset) {
				if (G::Button("Reset everything to defaults")) {
					g_confirmReset = true;
				}
				Tip("Puts every setting back to how the mod ships. Asks you to confirm, and is only saved when you press Save.");
			} else {
				if (G::Button("Click again to confirm reset")) {
					Eyes::ResetDefaults();
					g_confirmReset = false;
					Dirty();
				}
				G::SameLine();
				if (G::Button("Cancel")) {
					g_confirmReset = false;
				}
			}
			if (g_saveState == 1) {
				G::TextColored(kGood, "Settings saved.");
			} else if (g_saveState == 2) {
				G::TextColored(kBad, "Could not write the settings file (see FourthWall.log).");
			} else if (g_dirty) {
				G::TextColored(kWarn, "Unsaved changes (they are used right away, and saved when you close this menu).");
			} else {
				G::TextDisabled("All changes are saved.");
			}
		}

		// ---- pages -------------------------------------------------------------------------------------------------------
		void __stdcall PageGeneral()
		{
			auto& c = Eyes::Cfg();

			G::SeparatorText("Eye contact");
			Note("Characters look into the camera with their eyes only (the head is not touched). It works with the free camera. Your own character does it whenever you stand still, and everyone in a scene does it during the scene.");
			G::Spacing();
			if (Eyes::IsOn()) {
				G::TextColored(kGood, "Eye contact is ON");
			} else {
				G::TextColored(kWarn, "Eye contact is OFF");
			}
			G::SameLine();
			if (G::Button(Eyes::IsOn() ? "Turn off" : "Turn on")) {
				Eyes::SetOn(!Eyes::IsOn());
			}
			Check("On when the game starts", c.startOn, "On: eye contact is already active when you load in. Off: press the hotkey to start it.");

			G::SeparatorText("Hotkey");
			G::Text("Toggle eye contact:  %s", KeyName(c.hotkey).c_str());
			G::SameLine();
			if (g_waitKey) {
				G::Button("Press a key...  (Esc cancels)");
				const int k = PollKey();
				if (k == -1) {
					EndKeyWait();
				} else if (k > 0) {
					c.hotkey = k;
					EndKeyWait();
					Dirty();
				}
			} else {
				if (G::Button("Change key")) {
					BeginKeyWait();
				}
				Tip("Click, then press the key you want. Esc cancels.");
				G::SameLine();
				if (G::Button("Clear")) {
					c.hotkey = 0;
					Dirty();
				}
				Tip("Removes the hotkey. Eye contact can still be switched here.");
				G::SameLine();
				if (G::Button("Default (Insert)")) {
					c.hotkey = 0xD2;
					Dirty();
				}
			}

			G::SeparatorText("Your character");
			Note("Standing around, your character's eyes usually stare into space. With this on, when you stand still in third person or the free camera, your character looks at the camera instead. It stops as soon as you move, draw a weapon, ride, fight or open a menu.");
			Check("Look at the camera when idle", c.playerIdle, "Your character looks into the camera while you stand still (third person or free camera). Best with the camera in front of your character.");
			G::BeginDisabled(!c.playerIdle);
			Slider("Wait before starting", c.idleDelay, 0.0f, 5.0f, "%.1f s", "How long you have to stand still before your character looks at the camera.");
			Slider("Give-up angle while idle", c.idleGiveUp, 40.0f, 180.0f, "%.0f deg", "If the camera is further than this from where your character is facing (for example behind you, the normal third-person view), the eyes stay natural instead of straining sideways. Raise it to force eye contact from more angles.");
			G::EndDisabled();
			Check("Include your character in scenes", c.includePlayer, "Your character also looks at the camera during OStim, SexLab and Flower Girls scenes.");

			G::SeparatorText("Scene frameworks");
			Note("Eye contact is applied to actors in scenes played by these frameworks. A framework that is not installed cannot be ticked.");
			const std::uint32_t fw = Eyes::Frameworks();
			G::BeginDisabled(!(fw & 1u));
			Check("OStim Standalone", c.useOStim, "Take scene actors from OStim's thread API.");
			G::EndDisabled();
			G::SameLine();
			G::TextDisabled(fw & 1u ? "(found)" : "(not installed)");
			G::BeginDisabled(!(fw & 2u));
			Check("SexLab", c.useSexLab, "Actors SexLab is animating (and every add-on built on SexLab).");
			G::EndDisabled();
			G::SameLine();
			G::TextDisabled(fw & 2u ? "(found)" : "(not installed)");
			G::BeginDisabled(!(fw & 4u));
			Check("Flower Girls", c.useFlowerGirls, "Actors Flower Girls SE is animating (it has its own scene engine, separate from SexLab).");
			G::EndDisabled();
			G::SameLine();
			G::TextDisabled(fw & 4u ? "(found)" : "(not installed)");

			G::SeparatorText("Scene actors");
			static const char* kWho[] = { "All scene actors", "Female only", "Male only" };
			Choice("Actors", c.who, kWho, 3, "Which of the other people in a scene look at the camera (your own character is controlled separately above). Also used for the nearby characters below.");

			G::SeparatorText("Conversations");
			Note("Conversation camera mods (Cinematic Conversation Camera, Alternate Conversation Camera and similar) stage you and the character you are talking to for each other. With this on, nobody looks into the camera while a conversation is open (and for a second after), so the eye contact in the conversation is left to the game or to that mod. Everyone returns to the camera afterwards.");
			Check("Pause during conversations", c.pauseInConversation, "On: no eye contact with the camera while the dialogue menu is open, for scene actors, nearby characters and your own character. Off: the old behaviour (scene actors keep looking at the camera, even in a conversation).");

			G::SeparatorText("Other characters nearby");
			Note("Off by default. Turn it on and ordinary characters near the camera (followers, townsfolk, anyone) look at it too, even outside scenes. It uses the Actors filter above, skips anyone in combat, with a weapon drawn, or talking to you, and leaves their eyes natural when the camera is behind them.");
			Check("Nearby characters look at the camera too", c.nearby, "On: ordinary characters near the camera also look at it, not only scene actors and your own character.");
			G::BeginDisabled(!c.nearby);
Slider("Nearby radius", c.nearbyRadius, 2.0f, 40.0f, "%.0f m", "How far from the camera, in metres, characters are included.");
			float maxNear = static_cast<float>(c.maxNearby);
			Slider("Maximum nearby characters", maxNear, 0.0f, 20.0f, "%.0f", "At most this many nearby characters look at the camera at once, the nearest first. Every character the plugin drives has its face updated every frame, so a cap keeps busy places cheap. 0 = no limit.");
			c.maxNearby = static_cast<int>(maxNear + 0.5f);
			Check("Only characters on screen", c.nearbyOnScreen, "Skip characters the camera cannot see (behind you, round a corner of the view). You cannot see their eyes anyway, so this saves work.");
			G::EndDisabled();

			G::SeparatorText("Eye movement");
			Slider("Eye range", c.strength, 0.0f, 1.0f, "%.2f", "How far the eyes turn toward the camera. 1.0 = the full, calibrated amount, lower = more subtle for every direction.");
			static const char* kGaze[] = { "Auto (FaceLab when possible)", "FaceLab only", "MFG only" };
			Choice("Gaze engine", c.gazeMode, kGaze, 3, "What moves the eyes. FaceLab moves each eye separately, further, with a natural inward turn for a close camera; it needs FaceLab and a High Poly Head. MFG is the original method that works on any head. Auto uses FaceLab where it can and MFG everywhere else.");
			const int fls = Eyes::FaceLabStatus();
			G::TextDisabled(fls == 2 ? "FaceLab: found, gaze layer available" : (fls == 1 ? "FaceLab: found, but too old for the gaze layer (update FaceLab)" : "FaceLab: not installed (MFG is used)"));
Slider("Eye speed", c.speed, 30.0f, 900.0f, "%.0f deg/s", "How fast the eyes follow the camera. Lower = lazier, slower glances.");
			Slider("Give-up angle", c.giveUpAngle, 40.0f, 180.0f, "%.0f deg",
				"If the camera is further than this to the side of or behind them, the eyes drift back to centre instead of straining. 180 = never give up.");
			Slider("Up / down boost", c.verticalBoost, 1.0f, 3.0f, "%.1f",
				"Makes the eyes reach their full up/down turn at a smaller camera angle. Many heads show vertical looks weakly, so this defaults to 3.");

			SaveBar();
		}

		void __stdcall PageBehaviour()
		{
			auto& c = Eyes::Cfg();

			G::SeparatorText("Glance away now and then");
			Check("Occasionally look away", c.lookAway,
				"Every so often the eyes slide off the camera for a moment (mostly sideways, a little downward) and then come back. It makes the character feel less fixed.");
			G::BeginDisabled(!c.lookAway);
			Slider("Shortest time between glances", c.awayEveryMin, 1.0f, 60.0f, "%.1f s", "Each gap is random between the shortest and longest.");
			Slider("Longest time between glances", c.awayEveryMax, 1.0f, 120.0f, "%.1f s", "");
			Slider("Shortest glance", c.awayDurMin, 0.2f, 10.0f, "%.1f s", "How long each look-away lasts before the eyes return to the camera.");
			Slider("Longest glance", c.awayDurMax, 0.2f, 15.0f, "%.1f s", "");
			Slider("How far they look away", c.awayAngle, 5.0f, 60.0f, "%.0f deg", "How far the eyes move away from the camera direction. Capped by how far the eyes can turn.");
			G::EndDisabled();

			G::SeparatorText("Hand the eyes back now and then");
			Check("Periodically release control", c.release,
				"Every so often the plugin lets go of the eyes completely so the character does whatever the scene normally has them doing (looking at their partner, straight ahead...), then returns to the camera. The change is smoothed, not a snap.");
			G::BeginDisabled(!c.release);
			Slider("Shortest time on the camera", c.relEveryMin, 1.0f, 120.0f, "%.1f s", "Seconds of eye contact between releases. Each gap is random between the shortest and longest.");
			Slider("Longest time on the camera", c.relEveryMax, 1.0f, 180.0f, "%.1f s", "");
			Slider("Shortest release", c.relDurMin, 0.5f, 30.0f, "%.1f s", "How long the eyes are handed back before returning to the camera.");
			Slider("Longest release", c.relDurMax, 0.5f, 60.0f, "%.1f s", "");
			Slider("Time to let go of the camera", c.fadeOut, 0.1f, 4.0f, "%.1f s", "How long it takes to hand the eyes back to the scene at the start of a release.");
			Slider("Time to return to the camera", c.fadeIn, 0.1f, 4.0f, "%.1f s", "How long it takes to bring the eyes back onto the camera at the end of a release. Higher = a softer, slower return.");
			Slider("Rest after a FaceLab face (crossed eyes)", c.faceSettle, 0.0f, 3.0f, "%.1f s", "When a FaceLab face with crossed eyes (an ahegao) ends, the eyes first rest on their own for this long, both together, and only then return to the camera. 0 = hand back at once (one eye may move more than the other).");
			G::EndDisabled();

			SaveBar();
		}

		void __stdcall PageFlutter()
		{
			auto& c = Eyes::Cfg();

			G::SeparatorText("Wide eyes and lash flutter");
			Note("Now and then, while they are looking at the camera, their eyes open wide and they bat their eyelids twice in a slow, deliberate way, then relax. It never starts while their eyes are shut, and waits for a calm moment (not during a glance or a release).");
			G::Spacing();
			Check("Occasional wide-eyed flutter", c.flutter, "Turn the whole effect on or off.");
			G::BeginDisabled(!c.flutter);
			Slider("Shortest time between flutters", c.fluEveryMin, 3.0f, 300.0f, "%.0f s", "Each gap is random between the shortest and longest.");
			Slider("Longest time between flutters", c.fluEveryMax, 3.0f, 600.0f, "%.0f s", "");
			static const char* kStyle[] = { "Negative lids", "Happy (mood)", "Fear (mood)" };
			Choice("Widening style", c.fluStyle, kStyle, 3,
				"How the eyes open wide. Negative lids pulls the eyelids past their rest position (the best looking on most heads). Happy and Fear use a face expression instead.");
			Slider("How wide the eyes open", c.fluStrength, 0.1f, 1.0f, "%.2f", "1.0 = as wide as the head allows. How wide it gets depends on each head's morphs.");
			Slider("Brow lift", c.fluBrow, 0.0f, 1.0f, "%.2f", "A small extra lift of the eyebrows during the flutter. 0 = brows do not move.");
			Slider("Time for each blink", c.fluBlinkTime, 0.1f, 1.0f, "%.2f s", "How long each of the two deliberate blinks takes. Higher = slower.");
			Slider("Pause between the two blinks", c.fluGap, 0.0f, 1.0f, "%.2f s", "Time between the first blink finishing and the second starting.");
			G::EndDisabled();

			SaveBar();
		}

		void __stdcall PageCalibration()
		{
			auto& c = Eyes::Cfg();

			G::SeparatorText("If the eyes look wrong");
			Note("The defaults suit the standard rig. Only change these if the eyes look to the wrong side of the camera or consistently above or below it.");
			G::Spacing();
			Check("Swap left / right", c.invertX, "Turn this on if the eyes look to the wrong side of the camera.");
			Check("Swap up / down", c.invertY, "Turn this on if the eyes look above the camera when it is below them, or the reverse.");
			Slider("Left / right offset", c.yawOffset, -30.0f, 30.0f, "%.0f deg", "Nudges where the eyes aim horizontally.");
			Slider("Up / down offset", c.pitchOffset, -30.0f, 30.0f, "%.0f deg", "Nudges where the eyes aim vertically.");
			Slider("Full left/right turn at", c.maxYaw, 10.0f, 60.0f, "%.0f deg", "Camera angle at which the eyes reach their full sideways turn. Matches MFG Fix's fTrackEyeXY (28).");
			Slider("Full up/down turn at", c.maxPitch, 10.0f, 70.0f, "%.0f deg", "Camera angle at which the eyes reach their full vertical turn. Matches MFG Fix's fTrackEyeZ (40).");

Slider("Inward turn (close camera)", c.crossScale, 0.0f, 2.5f, "%.2f", "FaceLab gaze: how strongly the eyes turn in toward a close camera. 1.0 = the calibrated amount, 0 = none. Only matters when the camera is near her face.");
			G::SeparatorText("Male characters (FaceLab gaze)");
			Slider("Sideways amount", c.maleYawScale, 0.3f, 1.5f, "%.2f", "Male eyes are smaller, so the same turn goes further. Lower this if a male character's eyes overshoot when looking left or right.");
			Slider("Up / down amount", c.maleVertScale, 0.3f, 1.5f, "%.2f", "The same for looking up and down on male characters.");
			G::SeparatorText("Testing");
			Check("Hold the engine's eyes neutral (testing)", c.holdNeutral,
				"For judging other face mods: keeps your character's own eye look values at zero so the eyes stop wandering. Everything else in this mod pauses while it is on. Not saved; it is off every time the game starts.");

			G::SeparatorText("Troubleshooting");
			Check("Write a diagnostic log", c.diag,
				"Writes what the plugin sees and does to FourthWall.log (Documents/My Games/Skyrim Special Edition/SKSE). Leave it off unless you are tracking down a problem; it makes a large file.");

			SaveBar();
		}

		void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::kCloseMenu) {
				if (g_waitKey) {
					EndKeyWait();
				}
				g_confirmReset = false;
				if (g_dirty) {
					Save();
				}
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework is not installed: no in-game menu. Settings come from FourthWall.ini and the defaults.");
			return;
		}
		SKSEMenuFramework::SetSection("The Fourth Wall");
		SKSEMenuFramework::AddSectionItem("Eye contact", PageGeneral);
		SKSEMenuFramework::AddSectionItem("Glance and release", PageBehaviour);
		SKSEMenuFramework::AddSectionItem("Wide-eyed flutter", PageFlutter);
		SKSEMenuFramework::AddSectionItem("Calibration", PageCalibration);
		g_event = SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
		logger::info("SKSE Menu Framework pages registered (section: The Fourth Wall)");
	}
}
