// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - The Fourth Wall - Eye Contact. See LICENSE.

#pragma once
// Crash-protected probes used by the VR diagnostic build. No C++ objects with destructors inside the __try functions (MSVC requires that).
// Kept in their own header so tools/vr_selftest.cpp can compile and exercise exactly this code.
#include <cstdint>
#include <cstring>
#include <Windows.h>

namespace FwVr
{
struct VrCodeProbe
{
	bool          ok{ false };
	char          section[9]{};
	std::uint32_t protect{ 0 };
	std::uint8_t  bytes[16]{};
};

// No C++ objects with destructors in this function: __try needs that.
inline bool VrProbeCode(std::uintptr_t a_base, std::uintptr_t a_addr, VrCodeProbe& a_out) noexcept
{
	__try {
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(a_base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return false;
		}
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(a_base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE) {
			return false;
		}
		const auto* sec = IMAGE_FIRST_SECTION(nt);
		for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
			const auto lo = a_base + sec[i].VirtualAddress;
			const auto hi = lo + sec[i].Misc.VirtualSize;
			if (a_addr >= lo && a_addr < hi) {
				std::memcpy(a_out.section, sec[i].Name, 8);
				a_out.ok = (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
			}
		}
		MEMORY_BASIC_INFORMATION mbi{};
		if (::VirtualQuery(reinterpret_cast<const void*>(a_addr), &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) {
			a_out.ok = false;
			return false;
		}
		a_out.protect = mbi.Protect;
		std::memcpy(a_out.bytes, reinterpret_cast<const void*>(a_addr), sizeof(a_out.bytes));
		return a_out.ok;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}


// 0 = fine, otherwise a reason code
inline int VrProbeStruct(RE::BSFaceGenAnimationData* a_self) noexcept
{
	__try {
		if (!a_self) {
			return 1;
		}
		auto& fin = a_self->modifier3;
		auto& ex = a_self->expression3;
		if (!fin.values || fin.count < 8 || fin.count > 512) {
			return 2;
		}
		if (!ex.values || ex.count < 8 || ex.count > 512) {
			return 3;
		}
		for (std::uint32_t i = 0; i < fin.count && i < 64; ++i) {
			const float v = fin.values[i];
			if (!(v > -4.0f && v < 4.0f)) {
				return 4;
			}
		}
		for (std::uint32_t i = 0; i < ex.count && i < 64; ++i) {
			const float v = ex.values[i];
			if (!(v > -4.0f && v < 4.0f)) {
				return 5;
			}
		}
		return 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return 9;
	}
}

// The engine's own headset nodes (PlayerCharacter VR node data): HmdNode is the live-tracked headset pose, UprightHmdNode the yaw-smoothed one.
// Raw pointers and plain floats only inside the __try function; a bad read is reported, never thrown.
struct VrHmdRead
{
	bool  dataOk{ false };   // GetVRNodeData() answered and the read did not fault
	bool  hasHmd{ false };   // HmdNode non-null
	bool  hasUpright{ false };
	float hmd[3]{ 0, 0, 0 };      // world translate
	float upright[3]{ 0, 0, 0 };
};

inline VrHmdRead VrReadHmdNodes(RE::PlayerCharacter* a_p) noexcept
{
	VrHmdRead r;
	__try {
		auto* d = a_p ? a_p->GetVRNodeData() : nullptr;
		if (!d) {
			return r;
		}
		r.dataOk = true;
		if (auto* h = d->HmdNode.get()) {
			r.hasHmd = true;
			r.hmd[0] = h->world.translate.x;
			r.hmd[1] = h->world.translate.y;
			r.hmd[2] = h->world.translate.z;
		}
		if (auto* u = d->UprightHmdNode.get()) {
			r.hasUpright = true;
			r.upright[0] = u->world.translate.x;
			r.upright[1] = u->world.translate.y;
			r.upright[2] = u->world.translate.z;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		r.dataOk = false;
	}
	return r;
}

}
