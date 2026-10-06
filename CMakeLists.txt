cmake_minimum_required(VERSION 3.28)

# On Windows, link the vcpkg dependencies statically (fewer DLLs to ship) while keeping the dynamic CRT that Qt uses.
if(CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
endif()

project(Snapcord
    VERSION 0.1.0
    DESCRIPTION "Native, lightweight, voice-focused Discord client"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Network WebSockets LinguistTools)
qt_standard_project_setup()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-nayuki-qr-code-generator CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)

# Discord's DAVE end-to-end encryption library. It is built from source because it has no vcpkg port;
# its own dependencies (OpenSSL, mlspp, nlohmann-json) come from vcpkg.
include(FetchContent)
FetchContent_Declare(libdave
    URL https://github.com/discord/libdave/archive/8de72b1f8a2ac3c5a5270755bb8091a62e3c6169.tar.gz
    SOURCE_SUBDIR cpp
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(libdave)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/voice)
add_subdirectory(src/platform)
add_subdirectory(src/app)

option(SNAPCORD_BUILD_TESTS "Build the unit tests" ON)
if(SNAPCORD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
