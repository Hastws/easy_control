# (c) 2025 AutoAlg (autoalg.com).
# Author: Chunzhi Qu.
# SPDX-License-Identifier: MIT.

cmake_minimum_required(VERSION 3.10)
project(autoalg_input_sim LANGUAGES C CXX)

# ===== Options =====
option(INPUT_BUILD_DEMO "Build a tiny demo" ON)
option(INPUT_BACKEND_WAYLAND_WLR "Use Wayland (wlroots virtual pointer/keyboard) backend on Linux" OFF)
option(INPUT_BACKEND_UINPUT "Use uinput backend on Linux" OFF)
option(INPUT_STRICT_WARNINGS "Enable strict warnings" ON)

# ===== Library: header-only (INTERFACE) =====
add_library(input_sim INTERFACE)
target_include_directories(input_sim INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

# You can set C++ standard here if you want consumers to inherit it.
target_compile_features(input_sim INTERFACE cxx_std_17)

# ===== Common compile definitions (per platform) =====
if (MSVC)
    target_compile_definitions(input_sim INTERFACE
            UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
else ()
    # Nothing mandatory here, but you can add feature switches if needed.
endif ()

# ===== Warnings (consumers inherit; keep gentle for header-only) =====
if (INPUT_STRICT_WARNINGS)
    if (MSVC)
        target_compile_options(input_sim INTERFACE /W4)
    else ()
        target_compile_options(input_sim INTERFACE -Wall -Wextra -Wpedantic)
    endif ()
endif ()

# ===== Platform-specific link + compile defs =====
if (APPLE)
    # Quartz/Carbon frameworks for CGEvent/UCKeyTranslate, etc.
    find_library(APP_SERVICES_FRAMEWORK ApplicationServices)
    find_library(CARBON_FRAMEWORK Carbon)
    if (APP_SERVICES_FRAMEWORK AND CARBON_FRAMEWORK)
        target_link_libraries(input_sim INTERFACE
                "${APP_SERVICES_FRAMEWORK}" "${CARBON_FRAMEWORK}")
    else ()
        message(FATAL_ERROR "macOS frameworks not found: ApplicationServices / Carbon")
    endif ()

elseif (WIN32)
    # Win32 SendInput; usually no extra libs needed. Just ensure Unicode and lean headers.
    # Optionally: target_link_libraries(input_sim INTERFACE user32)

elseif (UNIX AND NOT APPLE) # Linux
    include(CheckIncludeFile)
    include(FindPkgConfig)

    # Backend selection rules:
    # - If Wayland ON -> use Wayland (wlroots virtual input)
    # - Else if uinput ON -> use uinput
    # - Else -> try X11+Xtst (default)
    if (INPUT_BACKEND_WAYLAND_WLR)
        message(STATUS "[input_sim] Linux backend: Wayland (wlroots)")
        pkg_check_modules(WAYLAND REQUIRED wayland-client)
        target_link_libraries(input_sim INTERFACE ${WAYLAND_LINK_LIBRARIES})
        target_include_directories(input_sim INTERFACE ${WAYLAND_INCLUDE_DIRS})
        # 由用户保证：已可 #include <zwlr-virtual-pointer-unstable-v1-client-protocol.h>
        # 和 <zwp-virtual-keyboard-unstable-v1-client-protocol.h>
        target_compile_definitions(input_sim INTERFACE INPUT_BACKEND_WAYLAND_WLR=1)

    elseif (INPUT_BACKEND_UINPUT)
        message(STATUS "[input_sim] Linux backend: uinput (/dev/uinput)")
        # 仅需头文件和运行时权限；通常不需要额外库
        check_include_file("linux/uinput.h" HAVE_UINPUT_H)
        if (NOT HAVE_UINPUT_H)
            message(FATAL_ERROR "linux/uinput.h not found. Install kernel headers or uinput dev package.")
        endif ()
        target_compile_definitions(input_sim INTERFACE INPUT_BACKEND_UINPUT=1)

    else ()
        message(STATUS "[input_sim] Linux backend: X11 + XTest (default)")
        # Try to find X11 and Xtst via PkgConfig; fallback to FindX11 if needed.
        if (PKG_CONFIG_FOUND)
            pkg_check_modules(X11 REQUIRED x11)
            pkg_check_modules(XTST REQUIRED xtst)
            target_include_directories(input_sim INTERFACE ${X11_INCLUDE_DIRS} ${XTST_INCLUDE_DIRS})
            target_link_libraries(input_sim INTERFACE ${X11_LIBRARIES} ${XTST_LIBRARIES})
        else ()
            find_package(X11 REQUIRED)
            if (NOT X11_XTest_LIB)
                # On some distros FindX11 may not find Xtst; try manual.
                find_library(XTST_LIB Xtst)
                if (NOT XTST_LIB)
                    message(FATAL_ERROR "X11 found but Xtst not found. Install libxtst-dev.")
                endif ()
                target_link_libraries(input_sim INTERFACE ${X11_LIBRARIES} ${XTST_LIB})
            else ()
                target_link_libraries(input_sim INTERFACE ${X11_LIBRARIES} ${X11_XTest_LIB})
            endif ()
            target_include_directories(input_sim INTERFACE ${X11_INCLUDE_DIR})
        endif ()
        target_compile_definitions(input_sim INTERFACE INPUT_BACKEND_X11=1)
    endif ()
endif ()

# ===== Demo (optional) =====
if (INPUT_BUILD_DEMO)
    add_executable(input_demo
            demo/main.cpp)
    target_link_libraries(input_demo PRIVATE input_sim)
    # Some compilers on macOS require explicit frameworks on final link too:
    if (APPLE)
        target_link_libraries(input_demo PRIVATE
                ${APP_SERVICES_FRAMEWORK} ${CARBON_FRAMEWORK})
    endif ()
endif ()

# ===== Install rules (header-only) =====
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS input_sim EXPORT input_simTargets)

install(EXPORT input_simTargets
        FILE input_simTargets.cmake
        NAMESPACE autoalg::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/autoalg_input_sim)

# A minimal config file for find_package(autoalg_input_sim)
include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/autoalg_input_simConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/autoalg_input_simConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/autoalg_input_sim
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/autoalg_input_simConfig.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/autoalg_input_sim)

message(STATUS "===========================================")
message(STATUS " input_sim configured:")
message(STATUS "   Platform: ${CMAKE_SYSTEM_NAME}")
if (UNIX AND NOT APPLE)
    if (INPUT_BACKEND_WAYLAND_WLR)
        message(STATUS "   Backend : Wayland (wlroots)")
    elseif (INPUT_BACKEND_UINPUT)
        message(STATUS "   Backend : uinput")
    else ()
        message(STATUS "   Backend : X11 + XTest")
    endif ()
endif ()
message(STATUS "   Demo    : ${INPUT_BUILD_DEMO}")
message(STATUS "===========================================")
