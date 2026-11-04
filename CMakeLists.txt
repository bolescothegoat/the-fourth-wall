# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Bolescothegoat
cmake_minimum_required(VERSION 3.21)

project(FourthWall VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibSSE-NG 15.5.0 (alandtse fork, tag v15.5.0): set COMMONLIB_SSE_FOLDER to its checkout
set(COMMONLIB_DIR "$ENV{COMMONLIB_SSE_FOLDER}" CACHE PATH "CommonLibSSE-NG source (an alandtse/CommonLibSSE-NG checkout, tag v15.5.0)")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory("${COMMONLIB_DIR}" "${CMAKE_BINARY_DIR}/commonlib" EXCLUDE_FROM_ALL)

add_library(FourthWall SHARED
	src/main.cpp
	src/Eyes.cpp
	src/UI.cpp
)
target_include_directories(FourthWall PRIVATE src)
target_precompile_headers(FourthWall PRIVATE src/PCH.h)
target_link_libraries(FourthWall PRIVATE CommonLibSSE::CommonLibSSE)
target_compile_definitions(FourthWall PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(FourthWall PRIVATE /permissive- /Zc:preprocessor /utf-8 /W4 /wd4100)

find_package(spdlog CONFIG REQUIRED)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" REQUIRED)
find_library(DETOURS_LIBRARY detours REQUIRED)
target_include_directories(FourthWall PRIVATE ${DETOURS_INCLUDE_DIRS})
target_link_libraries(FourthWall PRIVATE ${DETOURS_LIBRARY})
target_link_libraries(FourthWall PRIVATE spdlog::spdlog)

set_target_properties(FourthWall PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
)
