// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bolescothegoat - The Fourth Wall - Eye Contact. See LICENSE.

#pragma once

// Interop declaration for OStim Standalone's thread API (OStim.dll export RequestPluginAPI_Thread, OStim 7.5.x).
// The virtual order and struct layouts must match OStim's exactly, including members this plugin never calls.

namespace OStimApi
{
	enum class InterfaceVersion : std::uint8_t
	{
		V1
	};

	enum class ApiResult : std::uint8_t
	{
		OK,
		Invalid,
		Failed
	};

	enum class ThreadEvent : std::uint8_t
	{
		ThreadStarted,
		ThreadEnded,
		NodeChanged,
		ControlInput
	};

	enum class Controls : std::uint8_t
	{
		Up,
		Down,
		Left,
		Right,
		Toggle,
		Yes,
		No,
		Menu,
		KeyHide,
		AlignMenu,
		SearchMenu
	};

	struct KeyData
	{
		std::int32_t keys[17];
	};

	using ThreadEventCallback = void (*)(ThreadEvent a_event, std::uint32_t a_threadID, void* a_userData);
	using ControlEventCallback = void (*)(Controls a_control, std::uint32_t a_threadID, void* a_userData);

	struct ActorData
	{
		std::uint32_t formID;
		float         excitement;  // 0-100
		bool          isFemale;
		bool          hasSchlong;
		std::int32_t  timesClimaxed;
	};

	struct NavigationData
	{
		const char* sceneId;
		const char* destinationId;
		const char* icon;
		const char* description;
		const char* border;
		bool        isTransition;
	};

	struct ActorAlignmentData
	{
		float offsetX;
		float offsetY;
		float offsetZ;
		float scale;
		float rotation;
		float sosBend;
	};

	struct SceneSearchResult
	{
		const char*   sceneId;
		const char*   name;
		std::uint32_t actorCount;
	};

	struct OptionsMenuItem
	{
		const char* id;
		const char* title;
		const char* icon;
		const char* border;
		const char* description;
	};

	class IThreadInterface
	{
	public:
		virtual std::uint32_t GetPlayerThreadID() noexcept = 0;
		virtual bool          IsThreadValid(std::uint32_t threadID) noexcept = 0;
		virtual const char*   GetCurrentSceneID(std::uint32_t threadID) noexcept = 0;
		virtual std::uint32_t GetActorCount(std::uint32_t threadID) noexcept = 0;
		virtual std::uint32_t GetActors(std::uint32_t threadID, ActorData* buffer, std::uint32_t bufferSize) noexcept = 0;
		virtual std::uint32_t GetNavigationCount(std::uint32_t threadID) noexcept = 0;
		virtual std::uint32_t GetNavigationOptions(std::uint32_t threadID, NavigationData* buffer, std::uint32_t bufferSize) noexcept = 0;
		virtual ApiResult     NavigateToScene(std::uint32_t threadID, const char* sceneID) noexcept = 0;
		virtual bool          IsTransition(std::uint32_t threadID) noexcept = 0;
		virtual bool          IsInSequence(std::uint32_t threadID) noexcept = 0;
		virtual bool          IsAutoMode(std::uint32_t threadID) noexcept = 0;
		virtual bool          IsPlayerControlDisabled(std::uint32_t threadID) noexcept = 0;
		virtual void          RegisterEventCallback(ThreadEventCallback callback, void* userData) noexcept = 0;
		virtual void          UnregisterEventCallback(ThreadEventCallback callback) noexcept = 0;
		virtual void          RegisterControlCallback(ControlEventCallback callback, void* userData) noexcept = 0;
		virtual void          UnregisterControlCallback(ControlEventCallback callback) noexcept = 0;
		virtual void          SetExternalUIEnabled(bool enabled) noexcept = 0;
		virtual void          GetKeyData(KeyData* outData) noexcept = 0;
		virtual const char*   GetCurrentNodeName(std::uint32_t threadID) noexcept = 0;
		virtual std::int32_t  GetCurrentSpeed(std::uint32_t threadID) noexcept = 0;
		virtual std::int32_t  GetMaxSpeed(std::uint32_t threadID) noexcept = 0;
		virtual ApiResult     SetSpeed(std::uint32_t threadID, std::int32_t speed) noexcept = 0;
		virtual bool          GetActorAlignment(std::uint32_t threadID, std::uint32_t actorIndex, ActorAlignmentData* outData) noexcept = 0;
		virtual ApiResult     SetActorAlignment(std::uint32_t threadID, std::uint32_t actorIndex, const ActorAlignmentData* data) noexcept = 0;
		virtual std::uint32_t SearchScenes(const char* query, SceneSearchResult* buffer, std::uint32_t bufferSize) noexcept = 0;
		virtual bool          GetSceneInfo(const char* sceneID, SceneSearchResult* outInfo) noexcept = 0;
		virtual ApiResult     NavigateToSearchResult(std::uint32_t threadID, const char* sceneID) noexcept = 0;
		virtual void          RebuildOptionsTree() noexcept = 0;
		virtual std::uint32_t GetOptionsItemCount() noexcept = 0;
		virtual std::uint32_t GetOptionsItems(OptionsMenuItem* buffer, std::uint32_t bufferSize) noexcept = 0;
		virtual bool          SelectOptionsItem(std::int32_t index) noexcept = 0;
		virtual bool          IsOptionsAtRoot() noexcept = 0;
		virtual bool          IsActorInAnyThread(std::uint32_t actorFormID) noexcept = 0;
	};

	using RequestPluginApiThread = IThreadInterface* (*)(InterfaceVersion a_version, const char* a_pluginName, REL::Version a_pluginVersion);
}
