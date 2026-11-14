// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - The Fourth Wall - Eye Contact. See LICENSE.

// Self-test of the VR diagnostic build's crash-protected probes (src/VrSafe.h): the SAME code the plugin runs, fed good data, broken data and invalid
// memory. A probe that crashes instead of answering would take a tester's game down, so this is the promise that matters most.
#include "PCH.h"

#include "VrSafe.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace FwVr;

static int g_fail = 0;
static int g_pass = 0;
static int g_dataSymbol = 0;  // lives in .data/.bss: a perfectly readable address that is NOT executable code

#define CHECK(cond, msg)                                         \
	do {                                                         \
		const bool ok_ = (cond);                                 \
		std::printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg);     \
		ok_ ? ++g_pass : ++g_fail;                               \
	} while (0)

static std::uintptr_t Unmapped()
{
	// a page that was mapped and is released again: touching it is a guaranteed access violation
	void* p = ::VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	const auto a = reinterpret_cast<std::uintptr_t>(p);
	::VirtualFree(p, 0, MEM_RELEASE);
	return a;
}

int main()
{
	std::printf("== struct probe (VrProbeStruct) ==\n");
	alignas(16) static std::uint8_t mem[sizeof(RE::BSFaceGenAnimationData)] = {};
	auto*                           fg = reinterpret_cast<RE::BSFaceGenAnimationData*>(mem);
	static float                    fin[54]{};
	static float                    ex[32]{};
	auto                            reset = [&]() {
        std::memset(mem, 0, sizeof(mem));
        for (auto& v : fin) { v = 0.25f; }
        for (auto& v : ex) { v = 0.10f; }
        fg->modifier3.values = fin;
        fg->modifier3.count = 54;
        fg->expression3.values = ex;
        fg->expression3.count = 32;
	};

	reset();
	CHECK(VrProbeStruct(fg) == 0, "valid object (54 final values, 32 expression values, all in range) -> 0 (fine)");
	CHECK(VrProbeStruct(nullptr) == 1, "null pointer -> 1");

	reset(); fg->modifier3.values = nullptr;
	CHECK(VrProbeStruct(fg) == 2, "final buffer pointer null -> 2");
	reset(); fg->modifier3.count = 4;
	CHECK(VrProbeStruct(fg) == 2, "final buffer count 4 (too small) -> 2");
	reset(); fg->modifier3.count = 100000;
	CHECK(VrProbeStruct(fg) == 2, "final buffer count 100000 (absurd) -> 2");
	reset(); fg->expression3.count = 3;
	CHECK(VrProbeStruct(fg) == 3, "expression count 3 (too small) -> 3");

	reset(); fin[5] = std::numeric_limits<float>::quiet_NaN();
	CHECK(VrProbeStruct(fg) == 4, "NaN inside the final buffer -> 4");
	reset(); fin[7] = 100.0f;
	CHECK(VrProbeStruct(fg) == 4, "value 100.0 inside the final buffer -> 4");
	reset(); fin[2] = std::numeric_limits<float>::infinity();
	CHECK(VrProbeStruct(fg) == 4, "infinity inside the final buffer -> 4");
	reset(); ex[9] = -50.0f;
	CHECK(VrProbeStruct(fg) == 5, "value -50 inside the expression values -> 5");

	reset(); fg->modifier3.values = reinterpret_cast<float*>(static_cast<std::uintptr_t>(0x10));
	CHECK(VrProbeStruct(fg) == 9, "final buffer pointer is wild (0x10): the access violation is CAUGHT -> 9");
	reset(); fg->expression3.values = reinterpret_cast<float*>(Unmapped());
	CHECK(VrProbeStruct(fg) == 9, "expression pointer into released memory: access violation CAUGHT -> 9");
	CHECK(VrProbeStruct(reinterpret_cast<RE::BSFaceGenAnimationData*>(Unmapped())) == 9, "the whole object is unmapped memory: access violation CAUGHT -> 9");

	std::memset(mem, 0xFF, sizeof(mem));
	const int garbage = VrProbeStruct(fg);
	CHECK(garbage != 0, "0xFF garbage instead of an object -> not 'fine' (and no crash)");
	std::memset(mem, 0xCD, sizeof(mem));
	CHECK(VrProbeStruct(fg) != 0, "0xCD garbage instead of an object -> not 'fine' (and no crash)");

	std::printf("\n== code probe (VrProbeCode): is an address executable code inside the image? ==\n");
	HMODULE kb = ::GetModuleHandleW(L"KernelBase.dll");
	const auto base = reinterpret_cast<std::uintptr_t>(kb);
	const auto fn = reinterpret_cast<std::uintptr_t>(::GetProcAddress(kb, "GetTickCount64"));
	{
		VrCodeProbe pr;
		const bool  ok = VrProbeCode(base, fn, pr);
		std::printf("     section '%s', protect %X, first byte %02X\n", pr.section, pr.protect, pr.bytes[0]);
		CHECK(ok && std::strncmp(pr.section, ".text", 5) == 0, "a real function inside KernelBase.dll -> executable '.text' section");
	}
	{
		VrCodeProbe pr;
		const auto  exeBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
		const bool  ok = VrProbeCode(exeBase, reinterpret_cast<std::uintptr_t>(&g_dataSymbol), pr);
		CHECK(!ok, "a readable address in a DATA section (not code) -> rejected");
	}
	{
		VrCodeProbe pr;
		CHECK(!VrProbeCode(base, base + 0x7FFFFFFF, pr), "an address far outside every section -> rejected");
	}
	{
		VrCodeProbe pr;
		CHECK(!VrProbeCode(reinterpret_cast<std::uintptr_t>(mem), fn, pr), "a 'module base' that is not a PE image -> rejected");
	}
	{
		VrCodeProbe pr;
		CHECK(!VrProbeCode(Unmapped(), Unmapped(), pr), "a 'module base' in released memory: access violation CAUGHT -> rejected");
	}

	std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
