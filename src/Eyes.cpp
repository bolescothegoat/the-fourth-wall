// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - The Fourth Wall - Eye Contact. See LICENSE.

#include "PCH.h"

#include "Eyes.h"
#include "FaceLabAPI.h"
#include "OStimApi.h"

#include <Windows.h>

#include <detours/detours.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <random>
#include <thread>

#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Eyes
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// BSFaceGenAnimationData::modifierKeyFrame slots (CK / MFG Fix order: MfgConsoleFuncExt expression[16 + i])
		constexpr std::uint32_t kLookDown = 8;
		constexpr std::uint32_t kLookLeft = 9;
		constexpr std::uint32_t kLookRight = 10;
		constexpr std::uint32_t kLookUp = 11;
		constexpr std::uint32_t kSlots[4] = { kLookDown, kLookLeft, kLookRight, kLookUp };

		// other modifier slots used by the flutter (same table: 0 BlinkL, 1 BlinkR, 6 BrowUpL, 7 BrowUpR, 12 SquintL, 13 SquintR)
		constexpr std::uint32_t kBlinkL = 0, kBlinkR = 1, kBrowUpL = 6, kBrowUpR = 7, kSquintL = 12, kSquintR = 13;

		constexpr float kRad2Deg = 57.2957795f;

		Settings g_cfg;

		struct ActorEntry
		{
			RE::FormID formID{ 0 };
			bool       female{ false };
			bool       force{ false };  // the idle-player source: skips the actor filter
			std::uint32_t thread{ 0xFFFFFFFFu };  // OStim thread this actor came from (diag only)
		};

		struct State
		{
			float             yaw{ 0 };    // normalised -1..1 (smoothed)
			float             pitch{ 0 };  // normalised -1..1 (smoothed)
			float             w[4]{};      // last values we wrote (down, left, right, up)
			bool              wrote{ false };
			std::uint32_t     writes{ 0 };
			std::uint32_t     foreign{ 0 };  // ticks on which the stored value was no longer what we wrote
			std::uint32_t     uiForeign{ 0 };  // same, measured at the late (UI) pass of the same frame
			std::uint32_t     uiPasses{ 0 };
			float             l1[4]{};      // what the dialogue layer held when we looked
			float             fin[4]{};     // what the final (drawn) buffer held when we looked
			bool              dumped{ false };
			float             fluNext{ -1.0f };   // seconds until the next flutter (<0 = not scheduled)
			float             fluLeft{ 0.0f };    // seconds the current flutter still occupies
			float             relNext{ -1.0f };   // seconds until the next release (<0 = not scheduled)
			float             relLeft{ 0.0f };    // seconds left in the current release
			float             awayNext{ -1.0f };  // seconds until the next glance (<0 = not scheduled yet)
			float             awayLeft{ 0.0f };   // seconds left in the current glance
			float             awayYaw{ 0.0f };    // current glance offset in degrees
			float             awayPitch{ 0.0f };
			bool              idleSource{ false };  // this actor is driven by the "player idle" rule, not by a scene
			float             leaveT{ -1.0f };      // >= 0: no longer a target, fading back to the engine's eyes
			int               slot{ -1 };  // entry in the hook table
			RE::BSFaceGenAnimationData* fgPtr{ nullptr };
			Clock::time_point lastLog{};
		};

		OStimApi::IThreadInterface*                       g_api{ nullptr };
		std::map<std::uint32_t, std::vector<ActorEntry>>  g_threads;  // main thread only
		std::vector<ActorEntry>                           g_extra;    // SexLab / Flower Girls actors found by the faction scan (main thread only)
		RE::TESFaction*                                   g_slFaction{ nullptr };  // SexLabAnimatingFaction  (SexLab.esm 0x00E50F)
		RE::TESFaction*                                   g_fgFaction{ nullptr };  // dxAnimatingFaction  (FlowerGirls SE.esm 0x5BEF2C)
		std::map<RE::FormID, State>                       g_state;     // main thread only
		bool                                              g_on{ false };
		std::jthread                                      g_ticker;
		std::atomic<bool>                                 g_running{ false };
		Clock::time_point                                 g_lastTick{};
		Clock::time_point                                 g_lastPoll{};
		int                                               g_diagLines{ 0 };
		std::map<std::uint32_t, std::string>              g_sceneOf;  // OStim thread -> current scene id (diag; main thread only)
		std::string                                       g_curScene;  // scene of the actor being driven right now (diag)
		std::map<RE::FormID, Clock::time_point>           g_diagThrottle;

		// at most one diag line per actor and reason every 2 s
		bool DiagThrottle(RE::FormID a_id, int a_reason)
		{
			const auto key = a_id ^ (static_cast<RE::FormID>(a_reason) << 28);
			const auto now = Clock::now();
			auto       it = g_diagThrottle.find(key);
			if (it != g_diagThrottle.end() && std::chrono::duration<float>(now - it->second).count() < 2.0f) {
				return false;
			}
			g_diagThrottle[key] = now;
			return true;
		}

		bool Wanted(const ActorEntry& a_e, const RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (a_e.force) {
				return true;
			}
			if (a_actor->IsPlayerRef()) {
				return g_cfg.includePlayer;  // the player is not subject to the male / female filter
			}
			if (g_cfg.who == 1) {
				return a_e.female;
			}
			if (g_cfg.who == 2) {
				return !a_e.female;
			}
			return true;
		}

		// ---- final-buffer hook -------------------------------------------------------------------------------------------------
		// MFG Fix NG replaces BSFaceGenAnimationData::KeyframesUpdate (Address Library ID 26598) and rebuilds the final modifier
		// buffer (0x100) from several layers every frame. Anything written to a layer can be eased, merged or clamped away. So we
		// chain a Detours hook on the same function and, after it returns, write the four look slots straight into the final buffer.
		// SE 1.5.97 id 25983 was matched to AE id 26598 by comparing the two Address Library files (same function order and sizes
		// around SetExpressionOverride/Reset); it has not been run on a real 1.5.97 game yet, so InstallHook checks it and logs.
		const REL::RelocationID kKeyframesUpdateId(25983, 26598);

		struct HookSlot
		{
			std::atomic<RE::BSFaceGenAnimationData*> fg{ nullptr };
			std::atomic<float>                       w[4]{};
			std::atomic<float>                       blend{ 1.0f };     // target: 1 = our eyes, 0 = whatever the engine / OStim computed
			std::atomic<float>                       blendCur{ 1.0f };  // current, moved by the hook so releases and returns are smooth
			std::atomic<std::uint64_t>               applied{ 0 };      // frames on which the hook really wrote this slot (diag)
			// diag: at the start of the NEXT update (before the engine resets the buffer) is the final buffer still what we wrote?
			std::atomic<std::uint64_t>               preCalls{ 0 };
			std::atomic<std::uint64_t>               preForeign{ 0 };
			std::atomic<float>                       preSeen[4]{};
			std::atomic<float>                       preWas[4]{};
			float                                    lastW[4]{};     // hook thread only: what we put into the final buffer last time
			bool                                     haveLast{ false };
			// flutter: the main thread bumps fluReq to ask for one; the hook plays it with its own per-frame clock
			std::atomic<int>                         fluReq{ 0 };
			std::atomic<int>                         fluBlinks{ 3 };
			std::atomic<float>                       fluAmt{ 0.6f };
			std::atomic<float>                       fluBrow{ 0.25f };
			std::atomic<int>                         fluStyle{ 0 };
			std::atomic<float>                       fluBlinkT{ 0.30f };
			std::atomic<float>                       fluGap{ 0.03f };
			int                                      fluSeen{ 0 };   // hook thread only
			float                                    fluT{ -1.0f };  // hook thread only; <0 = idle
		};
		std::atomic<float> g_fadeOut{ 0.5f };  // seconds to hand the eyes back to the engine
		std::atomic<float> g_fadeIn{ 1.2f };   // seconds to take them back (eased)
		constexpr int kHookSlots = 16;
		HookSlot      g_hookSlots[kHookSlots];
		std::atomic<std::uint64_t> g_hookCalls{ 0 };
		std::atomic<std::uint64_t> g_hookWrites{ 0 };
		bool (*g_origKeyframes)(RE::BSFaceGenAnimationData*, float, bool){ nullptr };
		bool g_hooked{ false };

		bool KeyframesUpdateDetour(RE::BSFaceGenAnimationData* a_self, float a_dt, bool a_blink)
		{
			for (auto& s : g_hookSlots) {
				if (s.fg.load(std::memory_order_acquire) == a_self && s.haveLast) {
					RE::BSSpinLockGuard lock(a_self->lock);
					auto&               fin = a_self->modifier3;
					if (fin.values && fin.count > kLookUp) {
						s.preCalls.fetch_add(1, std::memory_order_relaxed);
						bool differs = false;
						for (int i = 0; i < 4; ++i) {
							if (std::fabs(fin.values[kSlots[i]] - s.lastW[i]) > 0.04f) {
								differs = true;
							}
						}
						if (differs) {
							s.preForeign.fetch_add(1, std::memory_order_relaxed);
							for (int i = 0; i < 4; ++i) {
								s.preSeen[i].store(fin.values[kSlots[i]], std::memory_order_relaxed);
								s.preWas[i].store(s.lastW[i], std::memory_order_relaxed);
							}
						}
					}
					break;
				}
			}
			const bool r = g_origKeyframes(a_self, a_dt, a_blink);
			g_hookCalls.fetch_add(1, std::memory_order_relaxed);
			for (auto& s : g_hookSlots) {
				if (s.fg.load(std::memory_order_acquire) == a_self) {
					RE::BSSpinLockGuard lock(a_self->lock);
					auto&               fin = a_self->modifier3;
					const float tgt = s.blend.load(std::memory_order_relaxed);
					float       cur = s.blendCur.load(std::memory_order_relaxed);
					const float dt = (a_dt > 0.0f && a_dt < 0.5f ? a_dt : 0.016f);
					if (cur < tgt) {
						cur = std::min(tgt, cur + dt / std::max(g_fadeIn.load(std::memory_order_relaxed), 0.05f));
					} else if (cur > tgt) {
						cur = std::max(tgt, cur - dt / std::max(g_fadeOut.load(std::memory_order_relaxed), 0.05f));
					}
					s.blendCur.store(cur, std::memory_order_relaxed);
					if (cur > 0.001f && fin.values && fin.count > kLookUp) {
						for (int i = 0; i < 4; ++i) {
							float       ours = s.w[i].load(std::memory_order_relaxed);
							ours = ours < 0.0f ? 0.0f : (ours > 1.0f ? 1.0f : ours);  // values above 1.0 are invalid: the eyes drop to centre
							const float nat = fin.values[kSlots[i]];                   // what the engine / OStim computed for this frame
							const float eased = cur * cur * (3.0f - 2.0f * cur);  // smoothstep: gentle start and end
							float       v = nat + (ours - nat) * eased;
							fin.values[kSlots[i]] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
							s.lastW[i] = fin.values[kSlots[i]];
						}
						s.haveLast = true;
						fin.isUpdated = false;
						g_hookWrites.fetch_add(1, std::memory_order_relaxed);
						s.applied.fetch_add(1, std::memory_order_relaxed);
					}

					// ---- flutter: eyes widen (upper lids lift via a mood expression), she looks at us, two deliberate blinks, relax ----
					const int req = s.fluReq.load(std::memory_order_relaxed);
					if (req != s.fluSeen) {
						s.fluSeen = req;
						const float closed = (fin.values && fin.count > kBlinkR) ? std::max(fin.values[kBlinkL], fin.values[kBlinkR]) : 1.0f;
						s.fluT = (closed < 0.4f) ? 0.0f : -1.0f;  // never start while her eyes are shut (e.g. another mod's orgasm face)
					}
					if (s.fluT >= 0.0f && fin.values && fin.count > kSquintR) {
						constexpr float kIn = 0.25f, kHold = 0.35f, kTail = 0.25f, kOut = 0.5f;
						const float     D = s.fluBlinkT.load(std::memory_order_relaxed);
						const float     G = s.fluGap.load(std::memory_order_relaxed);
						const float     amt = s.fluAmt.load(std::memory_order_relaxed);
						const float     browAmt = s.fluBrow.load(std::memory_order_relaxed);
						const int       style = s.fluStyle.load(std::memory_order_relaxed);
						const float     t = s.fluT;
						const float     b1 = kIn + kHold;     // first blink starts
						const float     b2 = b1 + D + G;      // second blink starts
						const float     fEnd = b2 + D + kTail;  // widening starts to relax
						auto            ease = [](float x) { x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); return x * x * (3.0f - 2.0f * x); };
						float           w = 1.0f;
						if (t < kIn) {
							w = ease(t / kIn);
						} else if (t >= fEnd) {
							w = 1.0f - ease((t - fEnd) / kOut);
						}
						// one deliberate blink: lids close over 40% of D, open over the other 60%
						auto blink = [&](float a_start) {
							const float x = (t - a_start) / D;
							if (x < 0.0f || x > 1.0f) {
								return 0.0f;
							}
							return x < 0.4f ? ease(x / 0.4f) : 1.0f - ease((x - 0.4f) / 0.6f);
						};
						const float bl = std::max(blink(b1), blink(b2));
						auto        put = [&](std::uint32_t a_slot, float a_v) {
                            fin.values[a_slot] = a_v < 0.0f ? 0.0f : (a_v > 1.0f ? 1.0f : a_v);
						};
						// widening: a mood / dialogue expression raises the upper lids; the brows only lift a little
						auto& ex = a_self->expression3;  // MFG Fix naming: expression3 = final expression values
						if (style != 0 && ex.values && ex.count > 12 && ex.type == RE::BSFaceGenKeyframe::Type::Expression) {
							const std::uint32_t idx = style == 1 ? 10u : 9u;  // 10 MoodHappy, 9 MoodFear
							const float         e = std::max(ex.values[idx], w * amt);
							ex.values[idx] = e > 1.0f ? 1.0f : e;
							ex.isUpdated = false;
						}
						const float brow = w * browAmt;
						put(kBrowUpL, std::max(fin.values[kBrowUpL], brow));
						put(kBrowUpR, std::max(fin.values[kBrowUpR], brow));
						put(kSquintL, fin.values[kSquintL] * (1.0f - 0.9f * w));
						put(kSquintR, fin.values[kSquintR] * (1.0f - 0.9f * w));
						if (style == 0) {
							// negative blink weight tries to open the lids past their rest position
							const float open = -0.4f * w * amt;
							fin.values[kBlinkL] = std::max(fin.values[kBlinkL], bl) + (bl > 0.0f ? 0.0f : open);
							fin.values[kBlinkR] = std::max(fin.values[kBlinkR], bl) + (bl > 0.0f ? 0.0f : open);
						} else {
							put(kBlinkL, std::max(fin.values[kBlinkL], bl));
							put(kBlinkR, std::max(fin.values[kBlinkR], bl));
						}
						fin.isUpdated = false;
						s.fluT += (a_dt > 0.0f && a_dt < 0.5f ? a_dt : 0.016f);
						if (s.fluT >= fEnd + kOut) {
							s.fluT = -1.0f;
						}
					}
					break;
				}
			}
			return r;
		}

		// diag: dump the two engine functions another mod (PPA) patches a call inside, to find out where it re-writes the face after us
		void DumpCallSites()
		{
			for (std::uint64_t id : { 26999ull, 26418ull }) {
				const auto addr = REL::ID(id).address();
				const auto* b = reinterpret_cast<const std::uint8_t*>(addr);
				std::string hex;
				for (int i = 0; i < 0x80; ++i) {
					hex += fmt::format("{:02X} ", b[i]);
				}
				logger::info("callsite fn id {} @ rva {:X}: {}", id, addr - REL::Module::get().base(), hex);
				const auto* c = b + 0x13;
				if (c[0] == 0xE8) {
					std::int32_t rel;
					std::memcpy(&rel, c + 1, 4);
					const auto  tgt = reinterpret_cast<std::uintptr_t>(c) + 5 + rel;
					HMODULE     mod = nullptr;
					char        name[260]{};
					if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(tgt), &mod) && mod) {
						::GetModuleFileNameA(mod, name, sizeof(name));
					}
					const auto* t = reinterpret_cast<const std::uint8_t*>(tgt);
					std::string th;
					for (int i = 0; i < 0x20; ++i) {
						th += fmt::format("{:02X} ", t[i]);
					}
					logger::info("  call at +0x13 -> {:X} (module '{}', skyrim rva {:X}) first bytes: {}", tgt, name[0] ? name : "(none: trampoline/heap)", tgt - REL::Module::get().base(), th);
				} else {
					logger::info("  byte at +0x13 is {:02X} (not a direct call)", c[0]);
				}
			}
		}

		// ---- late pass --------------------------------------------------------------------------------------------------------
		// PPA (Procedural Penis Animations, AccuratePenetration.dll) patches a call inside BSFaceGenNiNode's morph step (Address Library
		// ID 26999, +0x13). Its replacement edits the face buffers and then tail-jumps to the original callee, which turns the buffers
		// into morphs. That runs AFTER KeyframesUpdate, so PPA's face preset (blowjobs) wiped our eye values. We detour the original
		// callee (the address PPA saved) and re-apply the four look slots right before the morphs are built, after PPA's edit.
		constexpr std::uintptr_t kPpaSavedCalleeRva = 0x22AA08;  // PPA 0.0.37 .data: original callee pointer
		constexpr std::uintptr_t kPpaHookFnRva = 0x11E1D0;       // PPA 0.0.37: its replacement function
		constexpr std::size_t    kFaceNodeAnimDataOffset = 0x150;  // BSFaceGenNiNode::animationData (SE/AE)
		std::uintptr_t (*g_origMorphStep)(void*, bool){ nullptr };
		bool                       g_lateHooked{ false };
		int                        g_lateTries{ 0 };
		std::atomic<std::uint64_t> g_lateCalls{ 0 };
		std::atomic<std::uint64_t> g_lateFixed{ 0 };  // times PPA (or anyone) had changed our look values by then

		std::uintptr_t MorphStepDetour(void* a_self, bool a_flag)
		{
			g_lateCalls.fetch_add(1, std::memory_order_relaxed);
			if (a_self) {
				auto* fg = *reinterpret_cast<RE::BSFaceGenAnimationData**>(reinterpret_cast<std::uint8_t*>(a_self) + kFaceNodeAnimDataOffset);
				if (fg) {
					for (auto& s : g_hookSlots) {
						if (s.fg.load(std::memory_order_acquire) != fg) {
							continue;
						}
						RE::BSSpinLockGuard lock(fg->lock);
						auto&               fin = fg->modifier3;
						const float         cur = s.blendCur.load(std::memory_order_relaxed);
						if (cur > 0.001f && fin.values && fin.count > kLookUp) {
							const float eased = cur * cur * (3.0f - 2.0f * cur);
							bool        changed = false;
							for (int i = 0; i < 4; ++i) {
								float ours = s.w[i].load(std::memory_order_relaxed);
								ours = ours < 0.0f ? 0.0f : (ours > 1.0f ? 1.0f : ours);
								const float nat = fin.values[kSlots[i]];
								float       v = nat + (ours - nat) * eased;
								v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
								if (std::fabs(v - nat) > 0.03f) {
									changed = true;
								}
								fin.values[kSlots[i]] = v;
							}
							fin.isUpdated = false;
							if (changed) {
								g_lateFixed.fetch_add(1, std::memory_order_relaxed);
							}
						}
						break;
					}
				}
			}
			return g_origMorphStep(a_self, a_flag);
		}

		// retried from Tick until PPA has installed its hook (or we decide it is not there)
		void TryInstallLateHook()
		{
			if (g_lateHooked || g_lateTries > 40) {
				return;
			}
			++g_lateTries;
			HMODULE ppa = ::GetModuleHandleA("AccuratePenetration.dll");
			if (!ppa) {
				if (g_lateTries == 1) {
					logger::info("late pass: PPA (AccuratePenetration.dll) not loaded - not needed");
				}
				g_lateTries = 1000;  // no PPA: stop trying
				return;
			}
			const auto  base = reinterpret_cast<std::uintptr_t>(ppa);
			const auto* fn = reinterpret_cast<const std::uint8_t*>(base + kPpaHookFnRva);
			static const std::uint8_t kPrologue[10] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18 };
			if (std::memcmp(fn, kPrologue, sizeof(kPrologue)) != 0) {
				logger::warn("late pass: PPA is a different build than 0.0.37 (its hook function was not found at the expected place) - not hooking");
				g_lateTries = 1000;
				return;
			}
			const auto saved = *reinterpret_cast<const std::uintptr_t*>(base + kPpaSavedCalleeRva);
			const auto skyBase = REL::Module::get().base();
			if (g_lateTries == 1 || g_lateTries == 4 || g_lateTries == 20 || g_lateTries == 40) {
				logger::info("late pass try {}: PPA base {:X}, saved callee value {:X} (Skyrim base {:X})", g_lateTries, base, saved, skyBase);
			}
			if (saved == 0) {
				return;  // PPA has not saved its original callee yet: try again later
			}
			const auto* b = reinterpret_cast<const std::uint8_t*>(saved);
			logger::info("late pass: PPA forwards to {:X} (module of that address: {}); first bytes {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", saved,
				saved >= skyBase && saved < skyBase + 0x6000000 ? "Skyrim" : "another plugin's trampoline", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
			// PPA ends its replacement with `jmp [saved]`. Whatever it saved may be the engine function or another plugin's hook, so rather
			// than patching code we put OUR function into that pointer: PPA edits the face, jumps to us, we re-apply the eyes, then we
			// jump on to what it had saved.
			auto* slot = reinterpret_cast<std::uintptr_t*>(base + kPpaSavedCalleeRva);
			DWORD oldProt = 0;
			if (!::VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProt)) {
				logger::error("late pass: VirtualProtect failed ({})", ::GetLastError());
				g_lateTries = 1000;
				return;
			}
			g_origMorphStep = reinterpret_cast<std::uintptr_t (*)(void*, bool)>(saved);
			*slot = reinterpret_cast<std::uintptr_t>(&MorphStepDetour);
			::VirtualProtect(slot, sizeof(*slot), oldProt, &oldProt);
			g_lateHooked = true;
			logger::info("late pass: hooked - our eye values are re-applied after PPA's face edit and before the morphs are built");
		}

		bool InstallHook()
		{
			const auto addr = kKeyframesUpdateId.address();
			const auto* b = reinterpret_cast<const std::uint8_t*>(addr);
			// (DumpCallSites used AE-only ids and is a one-off diagnostic: not run, so other game versions never look those ids up)
			logger::info("KeyframesUpdate at {:X} (module base {:X}); first bytes {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", addr,
				REL::Module::get().base(), b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
			g_origKeyframes = reinterpret_cast<bool (*)(RE::BSFaceGenAnimationData*, float, bool)>(addr);
			DetourTransactionBegin();
			DetourUpdateThread(::GetCurrentThread());
			LONG err = DetourAttach(reinterpret_cast<PVOID*>(&g_origKeyframes), reinterpret_cast<PVOID>(&KeyframesUpdateDetour));
			if (err != NO_ERROR) {
				DetourTransactionAbort();
				logger::error("DetourAttach failed ({}); falling back to layer writes", err);
				g_origKeyframes = nullptr;
				return false;
			}
			err = DetourTransactionCommit();
			if (err != NO_ERROR) {
				logger::error("DetourTransactionCommit failed ({}); falling back to layer writes", err);
				g_origKeyframes = nullptr;
				return false;
			}
			logger::info("final-buffer hook installed (chained after any existing hook on KeyframesUpdate)");
			return true;
		}

		void PublishToHook(State& a_st, RE::BSFaceGenAnimationData* a_fg, const float (&a_w)[4], float a_blend)
		{
			if (a_st.slot >= 0 && g_hookSlots[a_st.slot].fg.load() != a_fg) {
				g_hookSlots[a_st.slot].fg.store(nullptr);
				a_st.slot = -1;
			}
			if (a_st.slot < 0) {
				for (int k = 0; k < kHookSlots; ++k) {
					RE::BSFaceGenAnimationData* expected = nullptr;
					if (g_hookSlots[k].fg.compare_exchange_strong(expected, a_fg)) {
						a_st.slot = k;
						break;
					}
				}
			}
			if (a_st.slot >= 0) {
				for (int i = 0; i < 4; ++i) {
					g_hookSlots[a_st.slot].w[i].store(a_w[i], std::memory_order_relaxed);
				}
				g_hookSlots[a_st.slot].blend.store(a_blend, std::memory_order_relaxed);
			}
			a_st.fgPtr = a_fg;
		}

		std::vector<ActorEntry> ReadActors(std::uint32_t a_id)
		{
			std::vector<ActorEntry>        out;
			std::vector<OStimApi::ActorData> buf(16);
			const auto                     n = g_api->GetActors(a_id, buf.data(), static_cast<std::uint32_t>(buf.size()));
			for (std::uint32_t i = 0; i < n && i < buf.size(); ++i) {
				out.push_back({ buf[i].formID, buf[i].isFemale, false, a_id });
			}
			return out;
		}

		// Puts the four look slots back to 0 if they still hold what we wrote.
		void Release(RE::FormID a_id)
		{
			auto it = g_state.find(a_id);
			if (it == g_state.end()) {
				return;
			}
			if (it->second.slot >= 0) {
				g_hookSlots[it->second.slot].fg.store(nullptr);
			}
			if (it->second.wrote && !g_hooked) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
				auto* fg = actor ? actor->GetFaceGenAnimationData() : nullptr;
				if (fg) {
					RE::BSSpinLockGuard lock(fg->lock);
					auto&               mk = fg->modifierKeyFrame;
					if (mk.values && mk.count > kLookUp) {
						for (int i = 0; i < 4; ++i) {
							if (std::fabs(mk.values[kSlots[i]] - it->second.w[i]) <= 0.05f) {
								mk.SetValue(kSlots[i], 0.0f);
							}
						}
					}
				}
			}
			g_state.erase(it);
		}

		void ReleaseAll()
		{
			std::vector<RE::FormID> ids;
			for (auto& [id, st] : g_state) {
				ids.push_back(id);
			}
			for (auto id : ids) {
				Release(id);
			}
		}

		float Clamp(float v, float lo, float hi)
		{
			return v < lo ? lo : (v > hi ? hi : v);
		}



		// Which local axes of "NPC Head [Head]" point forward / up. Found at run time from an upright actor (the player), because the
		// skeleton convention is not documented anywhere I could verify. Logged once.
		struct Calib
		{
			bool  ok{ false };
			int   fwdIdx{ 0 }, upIdx{ 0 };
			float fwdSign{ 1 }, upSign{ 1 };
		};
		Calib g_cal;

		RE::NiAVObject* HeadNode(RE::Actor* a_actor)
		{
			auto* root = a_actor ? a_actor->Get3D() : nullptr;
			return root ? root->GetObjectByName("NPC Head [Head]") : nullptr;
		}

		RE::NiPoint3 Col(const RE::NiMatrix3& a_m, int a_k)
		{
			return { a_m.entry[0][a_k], a_m.entry[1][a_k], a_m.entry[2][a_k] };
		}

		void TryCalibrate(RE::Actor* a_actor)
		{
			if (g_cal.ok) {
				return;
			}
			auto* head = HeadNode(a_actor);
			if (!head) {
				return;
			}
			const auto& R = head->world.rotate;
			RE::NiPoint3 c[3] = { Col(R, 0), Col(R, 1), Col(R, 2) };
			int          ui = 0;
			for (int k = 1; k < 3; ++k) {
				if (std::fabs(c[k].z) > std::fabs(c[ui].z)) {
					ui = k;
				}
			}
			if (std::fabs(c[ui].z) < 0.8f) {
				return;  // head not upright (lying scene): try again later
			}
			const float ang = a_actor->GetAngleZ();
			const float hx = std::sin(ang), hy = std::cos(ang);
			int         fi = -1;
			float       best = 0.0f;
			for (int k = 0; k < 3; ++k) {
				if (k == ui) {
					continue;
				}
				const float d = c[k].x * hx + c[k].y * hy;
				if (std::fabs(d) > std::fabs(best)) {
					best = d;
					fi = k;
				}
			}
			if (fi < 0 || std::fabs(best) < 0.5f) {
				return;  // head turned too far from the body to tell
			}
			g_cal.ok = true;
			g_cal.upIdx = ui;
			g_cal.upSign = c[ui].z > 0 ? 1.0f : -1.0f;
			g_cal.fwdIdx = fi;
			g_cal.fwdSign = best > 0 ? 1.0f : -1.0f;
			logger::info("head axes calibrated from {}: local axis {} is UP ({:+.0f}), local axis {} is FORWARD ({:+.0f}) "
						 "[axes in world: x({:+.2f} {:+.2f} {:+.2f}) y({:+.2f} {:+.2f} {:+.2f}) z({:+.2f} {:+.2f} {:+.2f}); body heading ({:+.2f} {:+.2f})]",
				a_actor->GetName(), ui, g_cal.upSign, fi, g_cal.fwdSign, c[0].x, c[0].y, c[0].z, c[1].x, c[1].y, c[1].z, c[2].x, c[2].y,
				c[2].z, hx, hy);
		}

		float RandF(float a_lo, float a_hi)
		{
			static std::mt19937 rng{ std::random_device{}() };
			if (a_lo > a_hi) {
				std::swap(a_lo, a_hi);
			}
			if (a_hi - a_lo < 0.0001f) {
				return a_lo;
			}
			return std::uniform_real_distribution<float>(a_lo, a_hi)(rng);
		}

		// Release: every so often stop overriding her eyes altogether so she does whatever OStim / the engine has her doing (looking
		// at her partner, straight ahead, ...), then take them back. The hook blends in and out, so there is no snap.
		bool UpdateRelease(RE::Actor* a_actor, State& a_st, float a_dt)
		{
			if (!g_cfg.release) {
				a_st.relLeft = 0.0f;
				a_st.relNext = -1.0f;
				return false;
			}
			if (a_st.relNext < 0.0f && a_st.relLeft <= 0.0f) {
				a_st.relNext = RandF(g_cfg.relEveryMin, g_cfg.relEveryMax);
			}
			if (a_st.relLeft > 0.0f) {
				a_st.relLeft -= a_dt;
				if (a_st.relLeft <= 0.0f) {
					a_st.relNext = RandF(g_cfg.relEveryMin, g_cfg.relEveryMax);
					if (g_cfg.diag) {
						logger::info("release over on {}: back to the camera", a_actor->GetName());
					}
					return false;
				}
				return true;
			}
			a_st.relNext -= a_dt;
			if (a_st.relNext <= 0.0f && a_st.fluLeft <= 0.0f) {
				a_st.relLeft = RandF(g_cfg.relDurMin, g_cfg.relDurMax);
				if (g_cfg.diag) {
					logger::info("release on {}: eyes handed back for {:.1f}s", a_actor->GetName(), a_st.relLeft);
				}
				return true;
			}
			return false;
		}

		// Flutter scheduler. Only asks; the hook plays it. Waits for a calm moment: on the camera, not mid-glance, not released.
		void UpdateFlutter(RE::Actor* a_actor, State& a_st, float a_dt, bool a_released)
		{
			if (!g_cfg.flutter || !g_hooked || a_st.slot < 0) {
				a_st.fluNext = -1.0f;
				a_st.fluLeft = 0.0f;
				return;
			}
			if (a_st.fluLeft > 0.0f) {
				a_st.fluLeft -= a_dt;
				return;
			}
			if (a_st.fluNext < 0.0f) {
				a_st.fluNext = RandF(g_cfg.fluEveryMin, g_cfg.fluEveryMax);
			}
			a_st.fluNext -= a_dt;
			if (a_st.fluNext > 0.0f) {
				return;
			}
			if (a_released || a_st.awayLeft > 0.0f || a_st.relLeft > 0.0f) {
				a_st.fluNext = 1.5f;  // not a good moment: try again shortly
				return;
			}
			auto& slot = g_hookSlots[a_st.slot];
			slot.fluAmt.store(g_cfg.fluStrength);
			slot.fluBrow.store(g_cfg.fluBrow);
			slot.fluStyle.store(g_cfg.fluStyle);
			slot.fluBlinkT.store(g_cfg.fluBlinkTime);
			slot.fluGap.store(g_cfg.fluGap);
			slot.fluReq.fetch_add(1);
			a_st.fluLeft = 0.25f + 0.35f + 2.0f * g_cfg.fluBlinkTime + g_cfg.fluGap + 0.25f + 0.5f + 0.3f;
			a_st.fluNext = -1.0f;
			if (g_cfg.diag) {
				logger::info("flutter on {}: style {}, wide {:.2f}, brow {:.2f}, blinks {:.2f}s gap {:.2f}s", a_actor->GetName(), g_cfg.fluStyle,
					g_cfg.fluStrength, g_cfg.fluBrow, g_cfg.fluBlinkTime, g_cfg.fluGap);
			}
		}

		// Look-away: every so often the eyes slide off the camera by awayAngle degrees (mostly sideways, a little downward, like a
		// shy glance), hold for a moment, and come back. The camera target itself is untouched; only an offset is added.
		void UpdateAway(RE::Actor* a_actor, State& a_st, float a_dt)
		{
			if (!g_cfg.lookAway) {
				a_st.awayLeft = 0.0f;
				a_st.awayYaw = a_st.awayPitch = 0.0f;
				a_st.awayNext = -1.0f;
				return;
			}
			if (a_st.awayNext < 0.0f && a_st.awayLeft <= 0.0f) {
				a_st.awayNext = RandF(g_cfg.awayEveryMin, g_cfg.awayEveryMax);
			}
			if (a_st.awayLeft > 0.0f) {
				a_st.awayLeft -= a_dt;
				if (a_st.awayLeft <= 0.0f) {
					a_st.awayYaw = a_st.awayPitch = 0.0f;
					a_st.awayNext = RandF(g_cfg.awayEveryMin, g_cfg.awayEveryMax);
				}
			} else {
				a_st.awayNext -= a_dt;
				if (a_st.awayNext <= 0.0f && a_st.fluLeft <= 0.0f) {
					const float side = RandF(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
					a_st.awayYaw = side * g_cfg.awayAngle * RandF(0.6f, 1.0f);
					a_st.awayPitch = -g_cfg.awayAngle * RandF(0.1f, 0.5f);
					a_st.awayLeft = RandF(g_cfg.awayDurMin, g_cfg.awayDurMax);
					if (g_cfg.diag) {
						logger::info("look-away on {}: yaw {:+.0f} pitch {:+.0f} deg for {:.1f}s", a_actor->GetName(), a_st.awayYaw, a_st.awayPitch,
							a_st.awayLeft);
					}
				}
			}
		}

		void Drive(RE::Actor* a_actor, State& a_st, const RE::NiPoint3& a_cam, float a_dt)
		{
			auto* fg = a_actor->GetFaceGenAnimationData();
			if (!fg) {
				if (g_cfg.diag && DiagThrottle(a_actor->GetFormID(), 1)) {
					logger::info("[{}] {:08X} {}: NO FACE DATA (GetFaceGenAnimationData is null) - cannot drive", g_curScene, a_actor->GetFormID(), a_actor->GetName());
				}
				return;
			}
			TryCalibrate(a_actor);
			RE::NiPoint3 origin{}, f{}, r{}, u{};
			bool         headFrame = false;
			if (g_cal.ok) {
				if (auto* head = HeadNode(a_actor)) {
					const auto& R = head->world.rotate;
					const auto  cf = Col(R, g_cal.fwdIdx);
					const auto  cu = Col(R, g_cal.upIdx);
					f = { cf.x * g_cal.fwdSign, cf.y * g_cal.fwdSign, cf.z * g_cal.fwdSign };
					u = { cu.x * g_cal.upSign, cu.y * g_cal.upSign, cu.z * g_cal.upSign };
					r = { f.y * u.z - f.z * u.y, f.z * u.x - f.x * u.z, f.x * u.y - f.y * u.x };  // f x u = actor's right
					// the head bone sits at the base of the skull; the eyes are about 7 units forward and 10 up
					origin = { head->world.translate.x + f.x * 7.0f + u.x * 10.0f, head->world.translate.y + f.y * 7.0f + u.y * 10.0f,
						head->world.translate.z + f.z * 7.0f + u.z * 10.0f };
					headFrame = true;
				}
			}
			if (!headFrame) {
				// fallback: the engine's eye vector (follows the body heading, not the animated head)
				RE::NiPoint3 dir{};
				a_actor->GetEyeVector(origin, dir, false);
				const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
				if (dl < 0.5f) {
					return;
				}
				f = { dir.x / dl, dir.y / dl, dir.z / dl };
				r = { f.y, -f.x, 0.0f };  // f x (0,0,1)
				const float rl = std::sqrt(r.x * r.x + r.y * r.y);
				if (rl < 0.2f) {
					return;
				}
				r = { r.x / rl, r.y / rl, 0.0f };
				u = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
			}

			RE::NiPoint3 v{ a_cam.x - origin.x, a_cam.y - origin.y, a_cam.z - origin.z };
			const float  vl = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
			if (vl < 1.0f) {
				return;
			}
			v = { v.x / vl, v.y / vl, v.z / vl };
			const float vx = v.x * r.x + v.y * r.y + v.z * r.z;
			const float vy = v.x * f.x + v.y * f.y + v.z * f.z;
			const float vz = v.x * u.x + v.y * u.y + v.z * u.z;

			float yawDeg = std::atan2(vx, vy) * kRad2Deg + g_cfg.yawOffset;  // + = camera to her right
			float pitchDeg = std::atan2(vz, std::sqrt(vx * vx + vy * vy)) * kRad2Deg + g_cfg.pitchOffset;  // + = above
			if (g_cfg.invertX) {
				yawDeg = -yawDeg;
			}
			if (g_cfg.invertY) {
				pitchDeg = -pitchDeg;
			}
			const float giveUp = a_st.idleSource ? g_cfg.idleGiveUp : g_cfg.giveUpAngle;
			const float away = Clamp((giveUp - std::fabs(yawDeg)) / 10.0f, 0.0f, 1.0f);
			UpdateAway(a_actor, a_st, a_dt);
			const bool releasedByTimer = UpdateRelease(a_actor, a_st, a_dt);
			// FaceLab (optional): while a face has the eyes crossed (an ahegao) it owns the gaze, so we let go of the eyes, easing with the face's own fade.
			// Only the cross-eyed slider counts; every other face leaves the eyes to us.
			float faceCtl = 0.0f;
			if (const auto* fl = FaceLabAPI::Get(); fl && fl->version >= 4) {
				faceCtl = std::clamp(fl->GetEyeControl(a_actor->GetFormID()), 0.0f, 1.0f);
			}
			const bool released = releasedByTimer || faceCtl > 0.02f;
			const float ty = Clamp((yawDeg + a_st.awayYaw) / g_cfg.maxYaw, -1.0f, 1.0f) * away;
			const float tp = Clamp((pitchDeg + a_st.awayPitch) / g_cfg.maxPitch * g_cfg.verticalBoost, -1.0f, 1.0f) * away;

			const float stepY = g_cfg.speed * a_dt / g_cfg.maxYaw;
			const float stepP = g_cfg.speed * a_dt / g_cfg.maxPitch * g_cfg.verticalBoost;
			a_st.yaw += Clamp(ty - a_st.yaw, -stepY, stepY);
			a_st.pitch += Clamp(tp - a_st.pitch, -stepP, stepP);

			const float want[4] = {
				std::max(-a_st.pitch, 0.0f) * g_cfg.strength,  // down
				std::max(-a_st.yaw, 0.0f) * g_cfg.strength,    // left
				std::max(a_st.yaw, 0.0f) * g_cfg.strength,     // right
				std::max(a_st.pitch, 0.0f) * g_cfg.strength,   // up
			};

			if (g_hooked) {
				PublishToHook(a_st, fg, want, releasedByTimer ? 0.0f : 1.0f - faceCtl);
			}
			UpdateFlutter(a_actor, a_st, a_dt, released);
			bool          foreignNow = false;
			float         seen[4]{};
			float         tkv[4]{};
			std::uint32_t count = 0;
			bool          tkMatch = false;
			bool          tkPresent = false;
			std::uint32_t mkType = 0, tkType = 0;
			{
				RE::BSSpinLockGuard lock(fg->lock);
				auto&               mk = fg->modifierKeyFrame;
				count = mk.count;
				if (!mk.values || mk.count <= kLookUp) {
					if (g_cfg.diag && DiagThrottle(a_actor->GetFormID(), 2)) {
						logger::info("[{}] {:08X} {}: modifier layer unusable (values={} count={})", g_curScene, a_actor->GetFormID(), a_actor->GetName(), mk.values != nullptr, mk.count);
					}
					return;
				}
				auto* tk = fg->transitionTargetKeyFrame;
				tkPresent = tk != nullptr;
				mkType = static_cast<std::uint32_t>(mk.type);
				if (tk) {
					tkType = static_cast<std::uint32_t>(tk->type);
					tkMatch = tk != &mk && tk->values && tk->count == mk.count && tk->type == mk.type;
				}
				auto& m1 = fg->modifier1;  // MFG Fix naming: modifier1 ("dialogue values"), merged into the final buffer first
				auto& m3 = fg->modifier3;  // modifier3: final values, rebuilt every KeyframesUpdate and the one that is drawn
				const bool ok1 = m1.values && m1.count > kLookUp && m1.type == mk.type;
				const bool ok3 = m3.values && m3.count > kLookUp && m3.type == mk.type;
				if (!a_st.dumped) {
					a_st.dumped = true;
					const RE::BSFaceGenKeyframeMultiple* kf[12] = { &fg->expressionKeyFrame, &fg->expressionKeyFrame2, &fg->modifierKeyFrame, &fg->phenomeKeyFrame,
						&fg->customKeyFrame, &fg->expression3, &fg->modifier1, &fg->modifier3, &fg->phoneme1, &fg->phoneme3, &fg->custom1, &fg->custom3 };
					const char* nm[12] = { "0x20", "0x40", "0x60", "0x80", "0xA0", "0xC0", "0xE0", "0x100", "0x120", "0x140", "0x160", "0x180" };
					for (int k = 0; k < 12; ++k) {
						logger::info("  face layer @{}: type={} count={}", nm[k], static_cast<int>(kf[k]->type), kf[k]->count);
					}
					logger::info("  layer checks: dialogue(0xE0) usable={} final(0x100) usable={}", ok1, ok3);
				}
				for (int i = 0; i < 4; ++i) {
					seen[i] = mk.values[kSlots[i]];
					a_st.l1[i] = ok1 ? m1.values[kSlots[i]] : -1.0f;
					a_st.fin[i] = ok3 ? m3.values[kSlots[i]] : -1.0f;
					if (tkMatch) {
						tkv[i] = tk->values[kSlots[i]];
					}
					if (a_st.wrote && std::fabs(seen[i] - a_st.w[i]) > 0.03f) {
						foreignNow = true;
					}
					if (!g_hooked && !released) {
						mk.SetValue(kSlots[i], want[i]);
					}
					if (!g_hooked && ok1 && g_cfg.writeLayer1) {
						m1.SetValue(kSlots[i], want[i]);
					}
					if (!g_hooked && tkMatch && g_cfg.writeTarget) {
						tk->values[kSlots[i]] = want[i];
					}
					a_st.w[i] = want[i];
				}
			}
			a_st.wrote = true;
			++a_st.writes;
			if (foreignNow) {
				++a_st.foreign;
				static int s_detail = 0;
				if (g_cfg.diag && s_detail < 40) {
					++s_detail;
					logger::info("  overwritten: found D{:.2f} L{:.2f} R{:.2f} U{:.2f} (we had written D{:.2f} L{:.2f} R{:.2f} U{:.2f}) | transition target present={} match={} "
								 "tk D{:.2f} L{:.2f} R{:.2f} U{:.2f} | types mk={} tk={} | dialogue-layer D{:.2f} L{:.2f} R{:.2f} U{:.2f} | FINAL D{:.2f} L{:.2f} R{:.2f} U{:.2f}",
						seen[0], seen[1], seen[2], seen[3], a_st.w[0], a_st.w[1], a_st.w[2], a_st.w[3], tkPresent, tkMatch, tkv[0], tkv[1], tkv[2],
						tkv[3], mkType, tkType, a_st.l1[0], a_st.l1[1], a_st.l1[2], a_st.l1[3], a_st.fin[0], a_st.fin[1], a_st.fin[2], a_st.fin[3]);
				}
			}

			if (g_cfg.diag && g_diagLines < 800) {
				const auto now = Clock::now();
				if (std::chrono::duration<float>(now - a_st.lastLog).count() >= g_cfg.diagInterval) {
					a_st.lastLog = now;
					++g_diagLines;
					logger::info("[{}] slot={} applied={} fg={:X} away={:.2f} giveUp={:.0f} released={} | {:08X} yaw {:+.1f} pitch {:+.1f} deg | norm y {:+.2f} p {:+.2f} | wrote D{:.2f} L{:.2f} R{:.2f} U{:.2f} | "
								 "found before write D{:.2f} L{:.2f} R{:.2f} U{:.2f} | modifiers={} writes={} overwritten-by-other={} late-pass overwritten={}/{} | "
								 "hook={} calls={} writes={} | foreign-before-next-update={}/{} last D{:.2f}->{:.2f} L{:.2f}->{:.2f} R{:.2f}->{:.2f} U{:.2f}->{:.2f} | exprOverride={} u21A={} | late-pass calls={} fixed={} | fwd ({:+.2f} {:+.2f} {:+.2f}) frame={} | dialogue-layer D{:.2f} L{:.2f} R{:.2f} U{:.2f} | FINAL D{:.2f} L{:.2f} R{:.2f} U{:.2f}",
						g_curScene, a_st.slot, a_st.slot >= 0 ? g_hookSlots[a_st.slot].applied.load() : 0ull, reinterpret_cast<std::uintptr_t>(fg), away, giveUp, released,
						a_actor->GetFormID(), yawDeg, pitchDeg, a_st.yaw, a_st.pitch, want[0], want[1], want[2], want[3],
						seen[0], seen[1], seen[2], seen[3], count, a_st.writes, a_st.foreign, a_st.uiForeign, a_st.uiPasses, g_hooked ? "ON" : "off", g_hookCalls.load(), g_hookWrites.load(),
						a_st.slot >= 0 ? g_hookSlots[a_st.slot].preForeign.load() : 0ull, a_st.slot >= 0 ? g_hookSlots[a_st.slot].preCalls.load() : 0ull,
						a_st.slot >= 0 ? g_hookSlots[a_st.slot].preWas[0].load() : 0.0f, a_st.slot >= 0 ? g_hookSlots[a_st.slot].preSeen[0].load() : 0.0f,
						a_st.slot >= 0 ? g_hookSlots[a_st.slot].preWas[1].load() : 0.0f, a_st.slot >= 0 ? g_hookSlots[a_st.slot].preSeen[1].load() : 0.0f,
						a_st.slot >= 0 ? g_hookSlots[a_st.slot].preWas[2].load() : 0.0f, a_st.slot >= 0 ? g_hookSlots[a_st.slot].preSeen[2].load() : 0.0f,
						a_st.slot >= 0 ? g_hookSlots[a_st.slot].preWas[3].load() : 0.0f, a_st.slot >= 0 ? g_hookSlots[a_st.slot].preSeen[3].load() : 0.0f,
						fg->exprOverride, fg->unk21A, g_lateCalls.load(), g_lateFixed.load(), f.x, f.y, f.z, headFrame ? "head" : "body", a_st.l1[0], a_st.l1[1], a_st.l1[2], a_st.l1[3], a_st.fin[0], a_st.fin[1], a_st.fin[2], a_st.fin[3]);
				}
			}
		}

		// Second pass of the same frame, queued as a UI task: that queue runs after the world (and face) update, so a value the
		// engine / MFG Fix transition rewrote this frame is put back before it is drawn.
		void Reapply()
		{
			if (!g_running || !g_on || g_hooked) {
				return;
			}
			for (auto& [id, st] : g_state) {
				if (!st.wrote) {
					continue;
				}
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
				auto* fg = actor ? actor->GetFaceGenAnimationData() : nullptr;
				if (!fg) {
					continue;
				}
				RE::BSSpinLockGuard lock(fg->lock);
				auto&               mk = fg->modifierKeyFrame;
				if (!mk.values || mk.count <= kLookUp) {
					continue;
				}
				bool changed = false;
				for (int i = 0; i < 4; ++i) {
					if (std::fabs(mk.values[kSlots[i]] - st.w[i]) > 0.03f) {
						changed = true;
					}
					mk.SetValue(kSlots[i], st.w[i]);
					auto& m1 = fg->modifier1;
					if (g_cfg.writeLayer1 && m1.values && m1.count > kLookUp && m1.type == mk.type) {
						m1.SetValue(kSlots[i], st.w[i]);
					}
				}
				++st.uiPasses;
				if (changed) {
					++st.uiForeign;
				}
			}
		}

		Clock::time_point g_lastFactionScan{};

		// SexLab and Flower Girls both keep a faction on every actor they are animating. Looking for that faction needs no Papyrus
		// events and works for every add-on built on those frameworks.
		void ScanFactions()
		{
			g_extra.clear();
			const bool useSl = g_cfg.useSexLab && g_slFaction;
			const bool useFg = g_cfg.useFlowerGirls && g_fgFaction;
			if (!useSl && !useFg) {
				return;
			}
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}
			for (auto& h : lists->highActorHandles) {
				auto  ptr = h.get();
				auto* actor = ptr.get();
				if (!actor || actor->IsDead() || !actor->Is3DLoaded()) {
					continue;
				}
				if ((useSl && actor->IsInFaction(g_slFaction)) || (useFg && actor->IsInFaction(g_fgFaction))) {
					auto* base = actor->GetActorBase();
					g_extra.push_back({ actor->GetFormID(), base && base->GetSex() == RE::SEXES::kFemale });
				}
			}
		}

		RE::NiPoint3 g_idlePos{};
		bool         g_idleHavePos{ false };
		float        g_idleAccum{ 0.0f };
		float        g_idleStill{ 0.0f };  // seconds the player has been standing still

		// "Idle" = standing still in third person or the free camera, nothing going on: no menu or dialogue, no combat, weapons away,
		// not riding, swimming or flying. Movement is sampled every 0.2 s so slow walking and sneaking count as moving.
		bool PlayerIdleNow(float a_dt)
		{
			auto* p = RE::PlayerCharacter::GetSingleton();
			auto* pc = RE::PlayerCamera::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			if (!p || !pc || !p->Is3DLoaded() || p->IsDead()) {
				g_idleStill = 0.0f;
				return false;
			}
			g_idleAccum += a_dt;
			if (g_idleAccum >= 0.2f) {
				const auto  pos = p->GetPosition();
				const float dx = pos.x - g_idlePos.x, dy = pos.y - g_idlePos.y, dz = pos.z - g_idlePos.z;
				if (!g_idleHavePos || dx * dx + dy * dy + dz * dz > 16.0f) {  // moved more than 4 units in 0.2 s
					g_idleStill = 0.0f;
				} else {
					g_idleStill += g_idleAccum;
				}
				g_idlePos = pos;
				g_idleHavePos = true;
				g_idleAccum = 0.0f;
			}
			auto* state = p->AsActorState();
			const bool busy = pc->IsInFirstPerson() || (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) ||
			                  p->IsInCombat() || p->IsOnMount() || p->IsInRagdollState() ||
			                  (state && (state->IsWeaponDrawn() || state->IsSwimming() || state->IsFlying()));
			if (busy) {
				g_idleStill = 0.0f;
				return false;
			}
			return g_idleStill >= g_cfg.idleDelay;
		}

		void Tick()
		{
			if (!g_running) {
				return;
			}
			const auto now = Clock::now();
			float      dt = std::chrono::duration<float>(now - g_lastTick).count();
			g_lastTick = now;
			dt = Clamp(dt, 0.001f, 0.1f);

			// refresh the actor lists ~4x/s
			if (g_api && std::chrono::duration<float>(now - g_lastPoll).count() >= 0.25f) {
				g_lastPoll = now;
				for (auto& [id, list] : g_threads) {
					list = ReadActors(id);
					const char* sid = g_api->GetCurrentSceneID(id);
					std::string cur = sid ? sid : "";
					auto        sit = g_sceneOf.find(id);
					if (sit == g_sceneOf.end() || sit->second != cur) {
						logger::info("scene on thread {}: '{}'{}", id, cur, sit == g_sceneOf.end() ? "" : (" (was '" + sit->second + "')"));
						g_sceneOf[id] = cur;
					}
				}
			}
			if (std::chrono::duration<float>(now - g_lastFactionScan).count() >= 0.25f) {
				g_lastFactionScan = now;
				ScanFactions();
				if (g_hooked && !g_lateHooked) {
					TryInstallLateHook();
				}
			}

			if (!g_on) {
				if (!g_state.empty()) {
					ReleaseAll();
				}
				return;
			}

			auto* pc = RE::PlayerCamera::GetSingleton();
			if (!pc || !pc->cameraRoot) {
				return;
			}
			const RE::NiPoint3 cam = pc->cameraRoot->world.translate;

			TryCalibrate(RE::PlayerCharacter::GetSingleton());
			std::map<RE::FormID, bool> seen;
			std::vector<ActorEntry> sources;
			if (g_cfg.useOStim) {
				for (auto& [id, list] : g_threads) {
					sources.insert(sources.end(), list.begin(), list.end());
				}
			}
			sources.insert(sources.end(), g_extra.begin(), g_extra.end());
			if (g_cfg.playerIdle && PlayerIdleNow(dt)) {
				auto* pl = RE::PlayerCharacter::GetSingleton();
				auto* base = pl ? pl->GetActorBase() : nullptr;
				if (pl) {
					sources.push_back({ pl->GetFormID(), base && base->GetSex() == RE::SEXES::kFemale, true });
				}
			}
			{
				for (auto& e : sources) {
					auto* actor = RE::TESForm::LookupByID<RE::Actor>(e.formID);
					if (!Wanted(e, actor) || !actor->Is3DLoaded() || seen.contains(e.formID)) {
						if (g_cfg.diag && actor && !seen.contains(e.formID) && DiagThrottle(e.formID, 3)) {
							logger::info("[{}] {:08X} {} skipped: wanted={} 3D loaded={}", e.thread != 0xFFFFFFFFu ? g_sceneOf[e.thread] : std::string("-"), e.formID, actor->GetName(), Wanted(e, actor), actor->Is3DLoaded());
						}
						continue;
					}
					g_curScene = e.thread != 0xFFFFFFFFu ? g_sceneOf[e.thread] : std::string("faction scan");
					seen[e.formID] = true;
					auto it = g_state.find(e.formID);
					if (it == g_state.end()) {
						logger::info("eye contact started on {:08X} ({}, {})", e.formID, actor->GetName(), e.female ? "female" : "male");
						it = g_state.emplace(e.formID, State{}).first;
					}
					it->second.idleSource = e.force;
					Drive(actor, it->second, cam, dt);
				}
			}
			auto* uiMgr = RE::UI::GetSingleton();
			const bool talking = uiMgr && uiMgr->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
			if (g_cfg.nearby && !talking) {
				if (auto* pl = RE::ProcessLists::GetSingleton()) {
					const float maxD = g_cfg.nearbyRadius * 70.0f;
					for (auto& h : pl->highActorHandles) {
						auto ptr = h.get();
						auto* actor = ptr.get();
						if (!actor || actor->IsDead() || !actor->Is3DLoaded() || seen.contains(actor->GetFormID())) {
							continue;
						}
						// the player has her own rule; fighters and anyone with a weapon out keep their natural eyes
						auto* astate = actor->AsActorState();
						if (actor->IsPlayerRef() || actor->IsInCombat() || (astate && astate->IsWeaponDrawn())) {
							continue;
						}
						const auto  p = actor->GetPosition();
						const float dx = p.x - cam.x, dy = p.y - cam.y, dz = p.z - cam.z;
						if (dx * dx + dy * dy + dz * dz > maxD * maxD) {
							continue;
						}
						auto*      base = actor->GetActorBase();
						const bool female = base && base->GetSex() == RE::SEXES::kFemale;
						const ActorEntry e{ actor->GetFormID(), female };
						if (!Wanted(e, actor)) {
							continue;
						}
						seen[e.formID] = true;
						auto it = g_state.find(e.formID);
						if (it == g_state.end()) {
							logger::info("eye contact started on {:08X} ({}, nearby)", e.formID, actor->GetName());
							it = g_state.emplace(e.formID, State{}).first;
						}
						it->second.idleSource = true;  // not in a scene: with the camera behind them their eyes stay natural
						Drive(actor, it->second, cam, dt);
					}
				}
			}
			// actors that left the scene
			std::vector<RE::FormID> gone;
			for (auto& [id, st] : g_state) {
				if (seen.contains(id)) {
					st.leaveT = -1.0f;
					continue;
				}
				if (st.leaveT < 0.0f) {  // just stopped being a target: let the hook fade her eyes back to the engine's before letting go
					if (g_hooked && st.slot >= 0) {
						g_hookSlots[st.slot].blend.store(0.0f);
						st.leaveT = g_cfg.fadeOut + 0.15f;
					} else {
						st.leaveT = 0.0f;
					}
				} else {
					st.leaveT -= dt;
				}
				if (st.leaveT <= 0.0f) {
					gone.push_back(id);
				}
			}
			for (auto id : gone) {
				logger::info("eye contact released {:08X}", id);
				Release(id);
			}
		}

		void OnThreadEvent(OStimApi::ThreadEvent a_event, std::uint32_t a_id, void*)
		{
			// callbacks arrive on OStim's thread; the handling runs on the main thread
			SKSE::GetTaskInterface()->AddTask([a_event, a_id]() {
				switch (a_event) {
				case OStimApi::ThreadEvent::ThreadStarted:
					if (g_api) {
						g_threads[a_id] = ReadActors(a_id);
						logger::info("thread {} started with {} actor(s), scene '{}'", a_id, g_threads[a_id].size(), g_api->GetCurrentSceneID(a_id) ? g_api->GetCurrentSceneID(a_id) : "");
					}
					break;
				case OStimApi::ThreadEvent::ThreadEnded:
					logger::info("thread {} ended", a_id);
					g_threads.erase(a_id);
					g_sceneOf.erase(a_id);
					break;
				default:
					break;
				}
			});
		}
	}

	Settings& Cfg()
	{
		return g_cfg;
	}

	std::int32_t Hotkey()
	{
		return g_cfg.hotkey;
	}

	std::uint32_t Frameworks()
	{
		return (g_api ? 1u : 0u) | (g_slFaction ? 2u : 0u) | (g_fgFaction ? 4u : 0u);
	}

	namespace
	{
		constexpr const char* kUserIni = "Data/SKSE/Plugins/FourthWall.ini";
		constexpr const char* kLegacyIni = "Data/SKSE/Plugins/OEyeContact.ini";  // the name before 0.6.1: settings are carried over once
		std::atomic<bool>     g_keyCapture{ false };

		// plain "key = value" lines; a missing key keeps the compiled default
		void ReadIni(const char* a_path, std::unordered_map<std::string, double>& a_out)
		{
			std::ifstream f(a_path);
			std::string   line;
			while (std::getline(f, line)) {
				const auto eq = line.find('=');
				if (eq == std::string::npos || line.empty() || line[0] == ';' || line[0] == '[') {
					continue;
				}
				auto trim = [](std::string t) {
					const auto b = t.find_first_not_of(" \t\r\xEF\xBB\xBF");
					const auto e = t.find_last_not_of(" \t\r");
					return b == std::string::npos ? std::string{} : t.substr(b, e - b + 1);
				};
				const auto key = trim(line.substr(0, eq));
				const auto val = trim(line.substr(eq + 1));
				try {
					a_out[key] = std::stod(val);
				} catch (...) {
				}
			}
		}

		// keeps values inside what the code can handle, whatever the ini or the menu says
		void Sanitize(Settings& s)
		{
			s.who = std::clamp(s.who, 0, 2);
			s.strength = Clamp(s.strength, 0.0f, 1.0f);
			s.speed = std::max(s.speed, 1.0f);
			s.verticalBoost = Clamp(s.verticalBoost, 1.0f, 3.0f);
			s.maxYaw = std::max(s.maxYaw, 1.0f);
			s.maxPitch = std::max(s.maxPitch, 1.0f);
			s.nearbyRadius = std::max(s.nearbyRadius, 1.0f);
			s.fadeOut = Clamp(s.fadeOut, 0.1f, 4.0f);
			s.fadeIn = Clamp(s.fadeIn, 0.1f, 4.0f);
			s.awayAngle = Clamp(s.awayAngle, 0.0f, 60.0f);
			s.idleDelay = Clamp(s.idleDelay, 0.0f, 10.0f);
			s.idleGiveUp = Clamp(s.idleGiveUp, 40.0f, 180.0f);
			s.fluStrength = Clamp(s.fluStrength, 0.1f, 1.0f);
			s.fluBrow = Clamp(s.fluBrow, 0.0f, 1.0f);
			s.fluStyle = std::clamp(s.fluStyle, 0, 2);
			s.fluBlinkTime = Clamp(s.fluBlinkTime, 0.1f, 1.0f);
			s.fluGap = Clamp(s.fluGap, 0.0f, 1.0f);
			s.overshoot = 1.0f;
			g_fadeOut.store(s.fadeOut);
			g_fadeIn.store(s.fadeIn);
		}
	}

	void LoadConfig()
	{
		std::unordered_map<std::string, double> v;
		ReadIni(kUserIni, v);
		bool migrated = false;
		if (v.empty()) {
			ReadIni(kLegacyIni, v);
			migrated = !v.empty();
		}
		const bool found = !v.empty();

		Settings s;
		auto     num = [&](const char* k, double d) { auto it = v.find(k); return it == v.end() ? d : it->second; };
		auto     flt = [&](const char* k, float d) { return static_cast<float>(num(k, d)); };
		auto     flg = [&](const char* k, bool d) { return num(k, d ? 1.0 : 0.0) != 0.0; };
		s.startOn = flg("bStartOn", s.startOn);
		s.hotkey = static_cast<std::int32_t>(num("iHotkey", s.hotkey));
		s.who = static_cast<int>(num("iWho", s.who));
		s.includePlayer = flg("bIncludePlayer", s.includePlayer);
		s.playerIdle = flg("bPlayerIdle", s.playerIdle);
		s.idleDelay = flt("fIdleDelay", s.idleDelay);
		s.idleGiveUp = flt("fIdleGiveUp", s.idleGiveUp);
		s.useOStim = flg("bUseOStim", s.useOStim);
		s.useSexLab = flg("bUseSexLab", s.useSexLab);
		s.useFlowerGirls = flg("bUseFlowerGirls", s.useFlowerGirls);
		s.nearby = flg("bNearby", s.nearby);
		s.nearbyRadius = flt("fNearbyRadius", s.nearbyRadius);
		s.strength = flt("fStrength", s.strength);
		s.speed = flt("fSpeed", s.speed);
		s.giveUpAngle = flt("fGiveUpAngle", s.giveUpAngle);
		s.verticalBoost = flt("fVerticalBoost", s.verticalBoost);
		s.lookAway = flg("bLookAway", s.lookAway);
		s.awayEveryMin = flt("fAwayEveryMin", s.awayEveryMin);
		s.awayEveryMax = flt("fAwayEveryMax", s.awayEveryMax);
		s.awayDurMin = flt("fAwayDurMin", s.awayDurMin);
		s.awayDurMax = flt("fAwayDurMax", s.awayDurMax);
		s.awayAngle = flt("fAwayAngle", s.awayAngle);
		s.release = flg("bRelease", s.release);
		s.relEveryMin = flt("fRelEveryMin", s.relEveryMin);
		s.relEveryMax = flt("fRelEveryMax", s.relEveryMax);
		s.relDurMin = flt("fRelDurMin", s.relDurMin);
		s.relDurMax = flt("fRelDurMax", s.relDurMax);
		s.fadeOut = flt("fFadeOut", s.fadeOut);
		s.fadeIn = flt("fFadeIn", s.fadeIn);
		s.flutter = flg("bFlutter", s.flutter);
		s.fluEveryMin = flt("fFluEveryMin", s.fluEveryMin);
		s.fluEveryMax = flt("fFluEveryMax", s.fluEveryMax);
		s.fluStrength = flt("fFluStrength", s.fluStrength);
		s.fluBrow = flt("fFluBrow", s.fluBrow);
		s.fluStyle = static_cast<int>(num("iFluStyle", s.fluStyle));
		s.fluBlinkTime = flt("fFluBlinkTime", s.fluBlinkTime);
		s.fluGap = flt("fFluGap", s.fluGap);
		s.maxYaw = flt("fMaxYaw", s.maxYaw);
		s.maxPitch = flt("fMaxPitch", s.maxPitch);
		s.invertX = flg("bInvertX", s.invertX);
		s.invertY = flg("bInvertY", s.invertY);
		s.yawOffset = flt("fYawOffset", s.yawOffset);
		s.pitchOffset = flt("fPitchOffset", s.pitchOffset);
		s.diag = flg("bDiag", s.diag);
		Sanitize(s);
		g_cfg = s;
		if (migrated) {
			logger::info("settings carried over from the old file {}", kLegacyIni);
			SaveConfig();
		}
		logger::info("settings {} ({}): hotkey={} who={} glance={} release={} flutter={} style={} diag={}", found ? "loaded from" : "not found, defaults used",
			kUserIni, s.hotkey, s.who, s.lookAway, s.release, s.flutter, s.fluStyle, s.diag);
	}

	bool SaveConfig()
	{
		const Settings& s = g_cfg;
		std::ofstream   f(kUserIni, std::ios::trunc);
		if (!f) {
			logger::warn("could not write {}", kUserIni);
			return false;
		}
		auto b = [](bool v) { return v ? 1 : 0; };
		f << "; FourthWall settings. Written by the in-game menu (SKSE Menu Framework > The Fourth Wall > Save settings).\n"
			 "; Safe to edit by hand while the game is closed; delete this file to return to the defaults.\n"
			 "[General]\n";
		f << "bStartOn = " << b(s.startOn) << "\niHotkey = " << s.hotkey << "\niWho = " << s.who << "\nbIncludePlayer = " << b(s.includePlayer)
		  << "\nbPlayerIdle = " << b(s.playerIdle) << "\nfIdleDelay = " << s.idleDelay << "\nfIdleGiveUp = " << s.idleGiveUp
		  << "\nbUseOStim = " << b(s.useOStim) << "\nbUseSexLab = " << b(s.useSexLab) << "\nbUseFlowerGirls = " << b(s.useFlowerGirls)
		  << "\nbNearby = " << b(s.nearby) << "\nfNearbyRadius = " << s.nearbyRadius << "\n";
		f << "fStrength = " << s.strength << "\nfSpeed = " << s.speed << "\nfGiveUpAngle = " << s.giveUpAngle << "\nfVerticalBoost = " << s.verticalBoost << "\n";
		f << "bLookAway = " << b(s.lookAway) << "\nfAwayEveryMin = " << s.awayEveryMin << "\nfAwayEveryMax = " << s.awayEveryMax
		  << "\nfAwayDurMin = " << s.awayDurMin << "\nfAwayDurMax = " << s.awayDurMax << "\nfAwayAngle = " << s.awayAngle << "\n";
		f << "bRelease = " << b(s.release) << "\nfRelEveryMin = " << s.relEveryMin << "\nfRelEveryMax = " << s.relEveryMax
		  << "\nfRelDurMin = " << s.relDurMin << "\nfRelDurMax = " << s.relDurMax << "\nfFadeOut = " << s.fadeOut << "\nfFadeIn = " << s.fadeIn << "\n";
		f << "bFlutter = " << b(s.flutter) << "\nfFluEveryMin = " << s.fluEveryMin << "\nfFluEveryMax = " << s.fluEveryMax
		  << "\nfFluStrength = " << s.fluStrength << "\nfFluBrow = " << s.fluBrow << "\niFluStyle = " << s.fluStyle
		  << "\nfFluBlinkTime = " << s.fluBlinkTime << "\nfFluGap = " << s.fluGap << "\n";
		f << "fMaxYaw = " << s.maxYaw << "\nfMaxPitch = " << s.maxPitch << "\nbInvertX = " << b(s.invertX) << "\nbInvertY = " << b(s.invertY)
		  << "\nfYawOffset = " << s.yawOffset << "\nfPitchOffset = " << s.pitchOffset << "\nbDiag = " << b(s.diag) << "\n";
		f.flush();
		const bool ok = f.good();
		logger::info("settings {} to {}", ok ? "saved" : "NOT saved", kUserIni);
		return ok;
	}

	void ResetDefaults()
	{
		Settings s;
		Sanitize(s);
		g_cfg = s;
		logger::info("settings reset to the defaults (not saved until Save is pressed)");
	}

	bool IsOn()
	{
		return g_on;
	}

	void SetOn(bool a_on)
	{
		if (g_on != a_on) {
			Toggle();
		}
	}

	void SetKeyCapture(bool a_on)
	{
		g_keyCapture.store(a_on);
	}

	bool IsKeyCapture()
	{
		return g_keyCapture.load();
	}

	void Toggle()
	{
		g_on = !g_on;
		g_diagLines = 0;
		logger::info("eye contact {}", g_on ? "ON" : "OFF");
		RE::SendHUDMessage::ShowHUDMessage(g_on ? "Eye contact: ON" : "Eye contact: OFF");
	}

	bool Init()
	{
		g_on = g_cfg.startOn;
		g_hooked = InstallHook();
		const auto module = ::GetModuleHandleW(L"OStim.dll");
		if (module) {
			const auto request = reinterpret_cast<OStimApi::RequestPluginApiThread>(::GetProcAddress(module, "RequestPluginAPI_Thread"));
			if (request) {
				g_api = request(OStimApi::InterfaceVersion::V1, "FourthWall", REL::Version(0, 1, 0, 0));
			}
		}
		if (g_api) {
			g_api->RegisterEventCallback(OnThreadEvent, nullptr);
			logger::info("OStim: connected to the thread API");
		} else {
			logger::info("OStim: not found (SexLab / Flower Girls / nearby NPCs can still be used)");
		}
		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			g_slFaction = dh->LookupForm<RE::TESFaction>(0x00E50F, "SexLab.esm");
			g_fgFaction = dh->LookupForm<RE::TESFaction>(0x5BEF2C, "FlowerGirls SE.esm");
		}
		logger::info("SexLab: {}   Flower Girls: {}", g_slFaction ? "found" : "not installed", g_fgFaction ? "found" : "not installed");

		g_running = true;
		g_lastTick = Clock::now();
		g_ticker = std::jthread([](std::stop_token a_stop) {
			while (!a_stop.stop_requested()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				SKSE::GetTaskInterface()->AddTask([]() { Tick(); });
				SKSE::GetTaskInterface()->AddUITask([]() { Reapply(); });
			}
		});
		return true;
	}

	void Shutdown()
	{
		g_running = false;
		if (g_ticker.joinable()) {
			g_ticker.request_stop();
		}
		if (g_api) {
			g_api->UnregisterEventCallback(OnThreadEvent);
		}
	}
}
