// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - The Fourth Wall - Eye Contact. See LICENSE.

#include "PCH.h"

#include "Eyes.h"
#include "UI.h"

namespace
{
	void InitLogging()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= "FourthWall.log"sv;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %T.%e] [%l] %v"s);
	}

	class KeySink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			for (auto* e = *a_event; e; e = e->next) {
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
					continue;
				}
				auto* b = e->AsButtonEvent();
				if (b && b->IsDown() && Eyes::Hotkey() > 0 && !Eyes::IsKeyCapture() && static_cast<std::int32_t>(b->GetIDCode()) == Eyes::Hotkey()) {
					SKSE::GetTaskInterface()->AddTask([]() { Eyes::Toggle(); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	KeySink g_keySink;

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Eyes::LoadConfig();
			Eyes::Init();
			UI::Register();
			if (auto* in = RE::BSInputDeviceManager::GetSingleton()) {
				in->AddEventSink(&g_keySink);
			}
		}
	}
}

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v{};
	v.PluginVersion({ 1, 1, 0, 0 });
	v.PluginName("FourthWall");
	v.AuthorName("Bolescothegoat");
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	// no CompatibleVersions list: Address Library plugins load on every AE runtime; the hook is found through Address Library ids
	return v;
}();

// Skyrim SE 1.5.97 uses the old SKSE entry point instead of SKSEPlugin_Version
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = "FourthWall";
	a_info->version = REL::Version(1, 1, 0, 0).pack();
	return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLogging();
	SKSE::Init(a_skse);
	logger::info("The Fourth Wall 1.1.0 loaded (runtime {})", REL::Module::get().version().string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
