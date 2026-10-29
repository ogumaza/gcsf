# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
# Shared version for gcsfrip, gcsfplay, the foobar2000 component, the XMPlay plugin, Windows file properties and macOS
# bundles (xsflib_add_version below). For a release, update it here and add notes in docs/release-notes/.
project(gcsf VERSION 1.8.1 LANGUAGES C CXX)

# xsflib, a library for players and rippers of the PSF family, is the submodule externals/xsflib. Its
# XsflibProject.cmake sets what a project built on it starts with: C++20, a release build by default, the static C
# runtime, the options that keep the output the same on every platform, and the warnings for GCSF's targets
# (GCSF_WARNINGS). It refuses the compilers whose output would differ from other builds. bzip2's header includes
# windows.h on Windows. NOMINMAX keeps windows.h from defining min and max as macros.
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/externals/xsflib/CMakeLists.txt")
    message(FATAL_ERROR "externals/xsflib is missing. Run: git submodule update --init --recursive")
endif()
include(externals/xsflib/cmake/XsflibProject.cmake)
xsflib_project(CHECK_COMPILERS NAME GCSF PREFIX GCSF NOMINMAX)

# ntdlib, a library for the hardware and formats of Nintendo's consoles, is the submodule externals/ntdlib. It builds on
# xsflib. xsflib provides the compression libraries that the image formats require: zlib (GCZ and the PSF container),
# Zstandard (RVZ), liblzma (LZMA and LZMA2 in WIA and RVZ) and bzip2 (WIA and RVZ). It provides libFLAC for the
# command-line tools too.
set(XSFLIB_PSF ON)
set(XSFLIB_CLI ON)
set(XSFLIB_DISC_LIBRARIES ON)
set(NTDLIB_DISC ON)
xsflib_add_libraries(xsflib ntdlib)

# The include directory for all of src, and xsflib's common helpers. The other GCSF libraries link it.
add_library(gcsf_common INTERFACE)
target_include_directories(gcsf_common INTERFACE src)
target_link_libraries(gcsf_common INTERFACE xsflib::common)

# The GameCube for game mode: Gekko, memory, the DSP and the hardware around them. ntdlib provides the CPU, the DSP and
# the devices that the GameCube and the Wii share. The disc interface and the machine are GCSF's.
add_library(gcsf_gc STATIC
    src/gc/di.cpp
    src/gc/di.h
    src/gc/memory.h
    src/gc/system.cpp
    src/gc/system.h
)
target_link_libraries(gcsf_gc PUBLIC gcsf_common ntdlib::common ntdlib::dsp ntdlib::flipper ntdlib::ppc)
target_compile_options(gcsf_gc PRIVATE ${GCSF_WARNINGS})

# The GCSF file format: the PSF container, chunks, descriptors and tags.
add_library(gcsf_format STATIC
    src/gcsf/format.cpp
    src/gcsf/format.h
)
target_link_libraries(gcsf_format PUBLIC gcsf_common xsflib::psf)
target_compile_options(gcsf_format PRIVATE ${GCSF_WARNINGS})

# The player: a set's audio, in game mode on the emulated GameCube.
add_library(gcsf_player STATIC
    src/gcsf/player.cpp
    src/gcsf/player.h
)
find_package(Threads REQUIRED)
target_link_libraries(gcsf_player PUBLIC gcsf_format gcsf_gc xsflib::player Threads::Threads)
target_compile_options(gcsf_player PRIVATE ${GCSF_WARNINGS})

# The ripper: the game profiles, the sets and the length analysis.
add_library(gcsf_rip STATIC
    src/rip/driver_animal_crossing_blob.h
    src/rip/driver_collectors_edition_blob.h
    src/rip/driver_colosseum_bonus_blob.h
    src/rip/driver_crystal_chronicles_blob.h
    src/rip/driver_double_dash_blob.h
    src/rip/driver_four_swords_blob.h
    src/rip/driver_jungle_beat_blob.h
    src/rip/driver_luigis_mansion_blob.h
    src/rip/driver_mario_golf_blob.h
    src/rip/driver_mario_party_blob.h
    src/rip/driver_musyx_blob.h
    src/rip/driver_musyx_upload_blob.h
    src/rip/driver_pacman_vs_blob.h
    src/rip/driver_pikmin2_blob.h
    src/rip/driver_pikmin_blob.h
    src/rip/driver_super_mario_sunshine_blob.h
    src/rip/driver_twilight_princess_blob.h
    src/rip/driver_warioware_blob.h
    src/rip/driver_wind_waker_blob.h
    src/rip/analysis.h
    src/rip/fsys.cpp
    src/rip/fsys.h
    src/rip/hal.cpp
    src/rip/jaudio.cpp
    src/rip/msm.cpp
    src/rip/musyx.cpp
    src/rip/musyx_catalogue.cpp
    src/rip/musyx_catalogue.h
    src/rip/n64.cpp
    src/rip/profiles.cpp
    src/rip/red.cpp
    src/rip/rip.cpp
    src/rip/rip.h
    src/rip/sound_libraries.h
)
target_link_libraries(gcsf_rip PUBLIC gcsf_player ntdlib::common ntdlib::disc_containers ntdlib::jaudio xsflib::rip)
target_compile_options(gcsf_rip PRIVATE ${GCSF_WARNINGS})

add_executable(gcsfplay src/cli/gcsfplay.cpp)
target_link_libraries(gcsfplay PRIVATE gcsf_player xsflib::cli)
target_compile_options(gcsfplay PRIVATE ${GCSF_WARNINGS})
xsflib_add_version(gcsfplay VERSION ${gcsf_VERSION} DEFINE GCSF_VERSION DESCRIPTION "GCSF renderer" PRODUCT GCSF
                   COPYRIGHT "Copyright (c) 2026 the GCSF authors. MIT licence.")

add_executable(gcsfrip src/cli/gcsfrip.cpp)
# gcsfrip analyses lengths on several threads (--jobs). glibc before 2.34 keeps threads in a separate library.
target_link_libraries(gcsfrip PRIVATE gcsf_rip xsflib::cli xsflib::rip Threads::Threads)
target_compile_options(gcsfrip PRIVATE ${GCSF_WARNINGS})
xsflib_add_version(gcsfrip VERSION ${gcsf_VERSION} DEFINE GCSF_VERSION DESCRIPTION "GCSF ripper" PRODUCT GCSF
                   COPYRIGHT "Copyright (c) 2026 the GCSF authors. MIT licence.")

# Windows: UTF-8 as the tools' code page. macOS: a droplet app for each tool (gcsfrip.app, gcsfplay.app), so that files
# can be dropped on it in the Finder.
foreach (tool gcsfrip gcsfplay)
    xsflib_add_utf8_manifest(${tool})
    xsflib_add_droplet(${tool} VERSION ${gcsf_VERSION})
endforeach()

# foobar2000 component (Windows with clang-cl, or macOS): cmake -DFOOBAR2000_SDK_DIR=<unpacked SDK> ...
set(FOOBAR2000_SDK_DIR "" CACHE PATH "foobar2000 SDK (builds the foo_input_gcsf component when set)")
if (FOOBAR2000_SDK_DIR)
    if (NOT MSVC AND NOT APPLE)
        message(FATAL_ERROR "the foobar2000 component requires clang-cl on Windows, or macOS")
    endif()
    add_subdirectory(plugins/foobar2000)
endif()

# XMPlay plugin (32-bit Windows with clang-cl): cmake -A Win32 -T ClangCL -DXMPLAY_SDK_DIR=<unpacked SDK> ...
set(XMPLAY_SDK_DIR "" CACHE PATH "XMPlay plugin SDK (builds the xmp-gcsf plugin when set)")
if (XMPLAY_SDK_DIR)
    add_subdirectory(plugins/xmplay)
endif()

# The tests. -DGCSF_TESTS=OFF leaves them out of a build that only makes the programs and plugins, such as CI's
# macOS job.
option(GCSF_TESTS "Build GCSF's tests and register them with CTest." ON)
if (GCSF_TESTS)
    enable_testing()

    # The unit tests: gcsf_<name>_test from tests/<name>_test.cpp, registered as gcsf_<name>.
    xsflib_add_project_test(gcsf format gcsf_format)
    xsflib_add_project_test(gcsf fsys gcsf_rip)
    xsflib_add_project_test(gcsf mini gcsf_rip)
    xsflib_add_project_test(gcsf musyx gcsf_rip)
    xsflib_add_project_test(gcsf sound_files gcsf_rip ntdlib::testing)
    xsflib_add_project_test(gcsf unplayed gcsf_rip ntdlib::testing)

    # The tools report the version they were built as.
    string(REPLACE "." "\\." version_pattern ${gcsf_VERSION})
    add_test(NAME version_gcsfrip COMMAND gcsfrip --version)
    set_tests_properties(version_gcsfrip PROPERTIES PASS_REGULAR_EXPRESSION "^gcsfrip ${version_pattern}[\r\n]*$")

    # Load local_tests.cmake from each immediate subdirectory to register tests that use locally supplied files.
    xsflib_include_local_tests()
endif()
