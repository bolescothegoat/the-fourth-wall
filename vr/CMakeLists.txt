# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Bolescothegoat
cmake_minimum_required(VERSION 3.21)

project(FourthWallVR VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# VR-ONLY build of CommonLibSSE-NG 15.5.0 (alandtse fork, tag v15.5.0, GPL-3.0-or-later). A separate DLL on purpose: the normal SE/AE plugin
# is untouched and nobody on SE/AE ever needs OpenVR. The VR build links openvr_api, which Skyrim VR ships next to SkyrimVR.exe.
# NOTE: the library's extern/openvr submodule must be populated (Valve's openvr repo: headers/ and lib/win64/openvr_api.lib).
set(COMMONLIB_DIR "$ENV{COMMONLIB_SSE_FOLDER}" CACHE PATH "CommonLibSSE-NG source (an alandtse/CommonLibSSE-NG checkout, tag v15.5.0)")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)

# keep the build machine's folder names out of the DLL (log macros embed __FILE__)
if(MSVC)
	foreach(_dir IN ITEMS "${COMMONLIB_DIR}" "${CMAKE_SOURCE_DIR}")
		file(TO_NATIVE_PATH "${_dir}" _native)
		string(APPEND CMAKE_CXX_FLAGS " /d1trimfile:\"${_native}\"")
	endforeach()
endif()

add_subdirectory("${COMMONLIB_DIR}" "${CMAKE_BINARY_DIR}/commonlib" EXCLUDE_FROM_ALL)

add_library(FourthWallVR SHARED
	src/main.cpp
	src/Eyes.cpp
)
target_include_directories(FourthWallVR PRIVATE src)
target_precompile_headers(FourthWallVR PRIVATE src/PCH.h)
target_link_libraries(FourthWallVR PRIVATE CommonLibSSE::CommonLibSSE)
target_compile_definitions(FourthWallVR PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX FW_VR_BUILD=1)
target_compile_options(FourthWallVR PRIVATE /permissive- /Zc:preprocessor /utf-8 /W4 /wd4100)

find_package(spdlog CONFIG REQUIRED)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" REQUIRED)
find_library(DETOURS_LIBRARY detours REQUIRED)
target_include_directories(FourthWallVR PRIVATE ${DETOURS_INCLUDE_DIRS})
target_link_libraries(FourthWallVR PRIVATE ${DETOURS_LIBRARY})
target_link_libraries(FourthWallVR PRIVATE spdlog::spdlog)

set_target_properties(FourthWallVR PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
)

# Self-test of the crash-protected probes (not shipped). Build target vr_selftest, run build/selftest/vr_selftest.exe
add_executable(vr_selftest tools/vr_selftest.cpp)
target_include_directories(vr_selftest PRIVATE src)
target_link_libraries(vr_selftest PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog)
target_compile_definitions(vr_selftest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX FW_VR_BUILD=1)
target_compile_options(vr_selftest PRIVATE /permissive- /Zc:preprocessor /utf-8 /W4 /wd4100)
set_target_properties(vr_selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/selftest" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/selftest")
