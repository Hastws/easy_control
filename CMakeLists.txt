# (c) 2025 AutoAlg (autoalg.com).
# Author: Chunzhi Qu.
# SPDX-License-Identifier: MIT.

cmake_minimum_required(VERSION 3.10)
project(autoalg_all LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================
# Options
# =========================
option(INPUT_BUILD_DEMO "Build a tiny input_sim demo" ON)
option(INPUT_BACKEND_WAYLAND_WLR "Use Wayland (wlroots virtual pointer/keyboard) backend on Linux" OFF)
option(INPUT_BACKEND_UINPUT "Use uinput backend on Linux" OFF)
option(INPUT_STRICT_WARNINGS "Enable strict warnings for input_sim" ON)
option(AUTOALG_USE_WAYLAND_PORTAL "Use xdg-desktop-portal on Linux/Wayland for screen capture" OFF)

# =========================
# input_sim: header-only
# =========================
add_library(input_sim INTERFACE)
target_include_directories(input_sim INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(input_sim INTERFACE cxx_std_17)

if (MSVC)
    target_compile_definitions(input_sim INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif ()

if (INPUT_STRICT_WARNINGS)
    if (MSVC)
        target_compile_options(input_sim INTERFACE /W4)
    else ()
        target_compile_options(input_sim INTERFACE -Wall -Wextra -Wpedantic)
    endif ()
endif ()

if (APPLE)
    # macOS: 常见输入模拟/系统服务来自 ApplicationServices（覆盖 CoreGraphics 等）
    find_library(APP_SERVICES_FRAMEWORK ApplicationServices)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    if (NOT APP_SERVICES_FRAMEWORK OR NOT FOUNDATION_FRAMEWORK)
        message(FATAL_ERROR "macOS frameworks not found: ApplicationServices / Foundation")
    endif ()
    target_link_libraries(input_sim INTERFACE
            "${APP_SERVICES_FRAMEWORK}"
            "${FOUNDATION_FRAMEWORK}"
    )
elseif (WIN32)
    # 需要时可打开
    # target_link_libraries(input_sim INTERFACE user32)
elseif (UNIX AND NOT APPLE)
    include(CheckIncludeFile)
    include(FindPkgConfig)
    if (INPUT_BACKEND_WAYLAND_WLR)
        pkg_check_modules(WAYLAND REQUIRED wayland-client)
        target_link_libraries(input_sim INTERFACE ${WAYLAND_LINK_LIBRARIES})
        target_include_directories(input_sim INTERFACE ${WAYLAND_INCLUDE_DIRS})
        target_compile_definitions(input_sim INTERFACE INPUT_BACKEND_WAYLAND_WLR=1)
    elseif (INPUT_BACKEND_UINPUT)
        check_include_file("linux/uinput.h" HAVE_UINPUT_H)
        if (NOT HAVE_UINPUT_H)
            message(FATAL_ERROR "linux/uinput.h not found. Install kernel headers or uinput dev package.")
        endif ()
        target_compile_definitions(input_sim INTERFACE INPUT_BACKEND_UINPUT=1)
    else ()
        if (PKG_CONFIG_FOUND)
            pkg_check_modules(X11 REQUIRED x11)
            pkg_check_modules(XTST REQUIRED xtst)
            target_include_directories(input_sim INTERFACE ${X11_INCLUDE_DIRS} ${XTST_INCLUDE_DIRS})
            target_link_libraries(input_sim INTERFACE ${X11_LIBRARIES} ${XTST_LIBRARIES})
        else ()
            find_package(X11 REQUIRED)
            if (NOT X11_XTest_LIB)
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

if (INPUT_BUILD_DEMO)
    add_executable(input_demo demo/main.cpp)  # 请提供 demo/main.cpp
    target_link_libraries(input_demo PRIVATE input_sim)
    if (APPLE)
        target_link_libraries(input_demo PRIVATE
                ${APP_SERVICES_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
        )
    endif ()
endif ()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS input_sim EXPORT input_simTargets)
install(EXPORT input_simTargets
        FILE input_simTargets.cmake
        NAMESPACE autoalg::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/autoalg_input_sim
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/autoalg_input_simConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/autoalg_input_simConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/autoalg_input_sim
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/autoalg_input_simConfig.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/autoalg_input_sim
)

# =========================
# screen capture libraries
# =========================

# 1) mac 桥接库：只在 APPLE 下存在（.mm 用 ARC）
if (APPLE)
    add_library(mac_bridge STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include/mac_bridge.mm
    )
    set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/include/mac_bridge.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    target_include_directories(mac_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # 找到框架
    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(APP_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

    # 关键：ScreenCaptureKit 用 PUBLIC，让依赖它的目标自动继承链接项
    target_link_libraries(mac_bridge PUBLIC
            ${SCREENCAPTUREKIT_FRAMEWORK}
            ${COREGRAPHICS_FRAMEWORK}
            ${APP_SERVICES_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
    )
endif ()

# 2) 主捕获库：按平台选择性添加源文件
add_library(autoalg_capture STATIC)
target_include_directories(autoalg_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Windows 源
if (WIN32)
    target_sources(autoalg_capture PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_windows.cpp
    )
endif ()

# Linux 源
if (UNIX AND NOT APPLE)
    target_sources(autoalg_capture PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_linux_x11.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_linux_wayland_portal.cpp
    )

    if (AUTOALG_USE_WAYLAND_PORTAL)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0 gio-2.0)
        target_compile_definitions(autoalg_capture PRIVATE AUTOALG_USE_WAYLAND_PORTAL=1)
        target_include_directories(autoalg_capture PRIVATE ${GLIB_INCLUDE_DIRS})
        target_link_libraries(autoalg_capture PRIVATE ${GLIB_LIBRARIES})

        include(FetchContent)
        FetchContent_Declare(
                stb
                GIT_REPOSITORY https://github.com/nothings/stb.git
                GIT_TAG master
                CONFIGURE_COMMAND ""
                BUILD_COMMAND ""
                INSTALL_COMMAND ""
                SUBBUILD_DIR ""
        )
        FetchContent_MakeAvailable(stb)
        target_include_directories(autoalg_capture PRIVATE ${stb_SOURCE_DIR})
    else ()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(X11 REQUIRED x11 xfixes xrandr)
        target_include_directories(autoalg_capture PRIVATE ${X11_INCLUDE_DIRS})
        target_link_libraries(autoalg_capture PRIVATE ${X11_LIBRARIES})
    endif ()
endif ()

# macOS 源 + 依赖桥接库
if (APPLE)
    target_sources(autoalg_capture PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_mac.cpp
    )
    target_link_libraries(autoalg_capture PUBLIC mac_bridge)
endif ()

# demo
add_executable(capture_demo demo/demo.cpp)
target_link_libraries(capture_demo PRIVATE autoalg_capture)

# =========================
# Messages
# =========================
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
message(STATUS "-------------------------------------------")
message(STATUS " screen_capture configured:")
if (APPLE)
    message(STATUS "   macOS: CoreGraphics + ApplicationServices + Foundation")
elseif (UNIX AND NOT APPLE)
    if (AUTOALG_USE_WAYLAND_PORTAL)
        message(STATUS "   Linux: Wayland portal (gio/glib + stb_image)")
    else ()
        message(STATUS "   Linux: X11 + XRandR + XFixes")
    endif ()
elseif (WIN32)
    message(STATUS "   Windows: win32 capture backend")
endif ()
message(STATUS "===========================================")
