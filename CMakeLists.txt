# (c) 2025 AutoAlg (autoalg.com).
# Author: Chunzhi Qu.
# SPDX-License-Identifier: MIT.

cmake_minimum_required(VERSION 3.10)
project(autoalg_all VERSION 0.0.3 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================
# Options
# =========================
option(INPUT_BACKEND_WAYLAND_WLR "Use Wayland (wlroots virtual pointer/keyboard) backend on Linux" OFF)
option(INPUT_BACKEND_UINPUT "Use uinput backend on Linux" OFF)
option(INPUT_STRICT_WARNINGS "Enable strict warnings for input_sim" ON)
option(AUTOALG_USE_WAYLAND_PORTAL "Use xdg-desktop-portal on Linux/Wayland for screen capture" OFF)
option(EASY_CONTROL_BUILD_DEMOS "Build demos (not installed/exported)" ON)

# =========================
# input_sim: header-only
# =========================
add_library(input_sim INTERFACE)
target_include_directories(input_sim INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(input_sim INTERFACE cxx_std_17)
# 统一命名空间别名
add_library(easy_control::input_sim ALIAS input_sim)

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
    # target_link_libraries(input_sim INTERFACE user32) # optional
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

# =========================
# screen capture libraries
# =========================

# 1) mac 桥接库（仅 macOS）
if (APPLE)
    add_library(mac_bridge STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include/mac_bridge.mm
    )
    set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/include/mac_bridge.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    target_include_directories(mac_bridge PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(APP_SERVICES_FRAMEWORK ApplicationServices REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

    target_link_libraries(mac_bridge PUBLIC
            ${SCREENCAPTUREKIT_FRAMEWORK}
            ${COREGRAPHICS_FRAMEWORK}
            ${APP_SERVICES_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
    )
    add_library(easy_control::mac_bridge ALIAS mac_bridge)
endif ()

# 2) 主捕获库
add_library(system_output STATIC)
target_include_directories(system_output PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
add_library(easy_control::system_output ALIAS system_output)

# Windows 源
if (WIN32)
    target_sources(system_output PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_windows.cpp
    )
endif ()

# Linux 源
if (UNIX AND NOT APPLE)
    target_sources(system_output PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_linux_x11.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_linux_wayland_portal.cpp
    )

    if (AUTOALG_USE_WAYLAND_PORTAL)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLIB REQUIRED glib-2.0 gobject-2.0 gio-2.0)
        target_compile_definitions(system_output PRIVATE AUTOALG_USE_WAYLAND_PORTAL=1)
        target_include_directories(system_output PRIVATE ${GLIB_INCLUDE_DIRS})
        target_link_libraries(system_output PRIVATE ${GLIB_LIBRARIES})

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
        target_include_directories(system_output PRIVATE ${stb_SOURCE_DIR})
    else ()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(X11 REQUIRED x11 xfixes xrandr)
        target_include_directories(system_output PRIVATE ${X11_INCLUDE_DIRS})
        target_link_libraries(system_output PRIVATE ${X11_LIBRARIES})
    endif ()
endif ()

# macOS 源 + 依赖桥接库
if (APPLE)
    target_sources(system_output PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/system_output_mac.cpp
    )
    target_link_libraries(system_output PUBLIC mac_bridge)
endif ()

# ===== demos（不安装/不导出） =====
if (EASY_CONTROL_BUILD_DEMOS)
    add_executable(system_output_test demo/system_output_test.cpp)
    target_link_libraries(system_output_test PRIVATE system_output)

    add_executable(system_input_test demo/system_input_test.cpp)
    target_link_libraries(system_input_test PRIVATE input_sim)
    if (APPLE)
        target_link_libraries(system_input_test PRIVATE
                ${APP_SERVICES_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
        )
    endif ()

    add_executable(joint_test demo/joint_test.cpp)
    target_link_libraries(joint_test PRIVATE input_sim system_output)
    if (APPLE)
        find_library(FW_APP_SERVICES ApplicationServices)
        find_library(FW_FOUNDATION Foundation)
        find_library(FW_CARBON Carbon)
        target_link_libraries(joint_test PRIVATE
                ${APP_SERVICES_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
                ${FW_APP_SERVICES}
                ${FW_FOUNDATION}
                ${FW_CARBON}
        )
    endif ()
endif ()

# =========================
# Install & Package (ONLY libs; export as easy_control)
# =========================
include(GNUInstallDirs)

# 头文件
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# 统一导出：只导出库（不含 demos）
install(TARGETS input_sim system_output
        EXPORT easy_controlTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if (APPLE)
    install(TARGETS mac_bridge
            EXPORT easy_controlTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif ()

# 安装导出文件（easy_control）
install(EXPORT easy_controlTargets
        FILE easy_controlTargets.cmake
        NAMESPACE easy_control::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/easy_control
)

# 生成并安装 easy_control 的 Config & Version
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/easy_controlConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/easy_controlConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/easy_controlConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/easy_control
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/easy_controlConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/easy_controlConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/easy_control
)

# =========================
# Messages
# =========================
message(STATUS "===========================================")
message(STATUS " easy_control configured:")
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
message(STATUS "-------------------------------------------")
message(STATUS " screen_capture configured:")
if (APPLE)
    message(STATUS "   macOS: CoreGraphics + ApplicationServices + Foundation + ScreenCaptureKit")
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

# =========================
# CPack（跨平台打包：ZIP/DEB/NSIS/Txz）
# =========================
include(CPack)

set(CPACK_PACKAGE_NAME "easy-control")
set(CPACK_PACKAGE_VENDOR "AutoAlg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "easy_control: input_sim + system_output cross-platform library")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/hastws/easy_control")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# 生成多种包：Windows/ZIP/NSIS，macOS/ZIP/TXZ，Linux/DEB
set(CPACK_GENERATOR "ZIP;TXZ;DEB;NSIS")

# Debian/Ubuntu
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "AutoAlg <hastws@hotmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# 让 .deb 安装到 /usr（仅对打包有效，不影响本地 install）
if (UNIX AND NOT APPLE)
    set(CPACK_SET_DESTDIR ON)
    set(CMAKE_INSTALL_PREFIX "/usr")
endif ()
