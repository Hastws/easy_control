# (c) 2025 AutoAlg (autoalg.com).
# Author: Chunzhi Qu.
# SPDX-License-Identifier: MIT.

cmake_minimum_required(VERSION 3.10)

# ===== auto version from Git (before project) =====
set(AUTOALG_DEFAULT_SEMVER "0.0.0")
set(AUTOALG_SEMVER "${AUTOALG_DEFAULT_SEMVER}")  # 仅 x.y.z，传给 project()
set(AUTOALG_PKG_SUFFIX "")                       # 如 +5.gabc1234 或 .dirty 组合
set(AUTOALG_VERSION_FULL "")                     # 展示/打包用：x.y.z+...

# 额外暴露给头文件的变量
set(EASY_GIT_DESCRIBE "")
set(EASY_GIT_SHA "")
set(EASY_GIT_DIRTY 0)

function(_easy_random_suffix out)
    string(TIMESTAMP _d "%Y%m%d-%H%M%S" UTC)
    string(RANDOM LENGTH 6 ALPHABET 0123456789abcdef _r)
    set(${out} "${_d}-${_r}" PARENT_SCOPE)
endfunction()

find_program(GIT_EXE git)
if (GIT_EXE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
            COMMAND ${GIT_EXE} -C ${CMAKE_CURRENT_SOURCE_DIR} describe --tags --dirty --abbrev=7
            OUTPUT_VARIABLE _desc
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _rc
    )
    if (_rc EQUAL 0 AND NOT _desc STREQUAL "")
        set(EASY_GIT_DESCRIBE "${_desc}")
        string(REGEX REPLACE "^v" "" _desc "${_desc}") # 允许 tag 带 v 前缀
        # 形如 1.2.3-5-gabc1234[-dirty] 或 1.2.3[-dirty]
        if (_desc MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9]+)-g([0-9a-f]+))?(-dirty)?$")
            set(AUTOALG_SEMVER "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
            if (CMAKE_MATCH_5) # 距离上次 tag 的提交数 + 哈希
                set(AUTOALG_PKG_SUFFIX "+${CMAKE_MATCH_5}.g${CMAKE_MATCH_6}")
                set(EASY_GIT_SHA "${CMAKE_MATCH_6}")
            endif ()
            if (CMAKE_MATCH_7) # 工作区脏
                if (AUTOALG_PKG_SUFFIX)
                    set(AUTOALG_PKG_SUFFIX "${AUTOALG_PKG_SUFFIX}.dirty")
                else ()
                    set(AUTOALG_PKG_SUFFIX "+dirty")
                endif ()
                set(EASY_GIT_DIRTY 1)
            endif ()
        endif ()
    endif ()
endif ()

# 没有符合规则的 tag → 强制显示 dirty
if (AUTOALG_SEMVER STREQUAL AUTOALG_DEFAULT_SEMVER)
    if (GIT_EXE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        execute_process(
                COMMAND ${GIT_EXE} -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short=7 HEAD
                OUTPUT_VARIABLE _sha
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _rc2
        )
        if (DEFINED _sha AND _rc2 EQUAL 0)
            set(EASY_GIT_SHA "${_sha}")
        endif ()
    endif ()
    if (EASY_GIT_SHA STREQUAL "")
        _easy_random_suffix(_rnd)
        set(EASY_GIT_SHA "${_rnd}")
    endif ()
    set(AUTOALG_PKG_SUFFIX "+dirty-${EASY_GIT_SHA}") # ★ 按你的要求，无 tag 时显示 dirty
    set(EASY_GIT_DIRTY 1)
endif ()

project(easy_control VERSION ${AUTOALG_SEMVER} LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 完整版本串（展示/打包可用）
set(AUTOALG_VERSION_FULL "${PROJECT_VERSION}${AUTOALG_PKG_SUFFIX}")
message(STATUS "[easy_control] version: ${AUTOALG_VERSION_FULL}  (describe='${EASY_GIT_DESCRIBE}', sha='${EASY_GIT_SHA}', dirty=${EASY_GIT_DIRTY})")

# =========================
# Options
# =========================
option(INPUT_BACKEND_WAYLAND_WLR "Use Wayland (wlroots virtual pointer/keyboard) backend on Linux" OFF)
option(INPUT_BACKEND_UINPUT "Use uinput backend on Linux" OFF)
option(INPUT_STRICT_WARNINGS "Enable strict warnings for system_input" ON)
option(AUTOALG_USE_WAYLAND_PORTAL "Use xdg-desktop-portal on Linux/Wayland for screen capture" OFF)
option(EASY_CONTROL_BUILD_DEMOS "Build demos (not installed/exported)" OFF)

# =========================
# 生成版本头文件（供 #include <easy_control/version.h>）
# =========================
set(EASY_VER_OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/generated/easy_control")
file(MAKE_DIRECTORY "${EASY_VER_OUTDIR}")
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/easy_control_version.h.in"
        "${EASY_VER_OUTDIR}/version.h"
        @ONLY
)

# =========================
# system_input: header-only
# =========================
add_library(system_input INTERFACE)
target_include_directories(system_input INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>     # 让构建时能找到 easy_control/version.h
        $<INSTALL_INTERFACE:include>
)
target_compile_features(system_input INTERFACE cxx_std_17)
add_library(easy_control::system_input ALIAS system_input)

if (MSVC)
    target_compile_definitions(system_input INTERFACE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif ()

if (INPUT_STRICT_WARNINGS)
    if (MSVC)
        target_compile_options(system_input INTERFACE /W4)
    else ()
        target_compile_options(system_input INTERFACE -Wall -Wextra -Wpedantic)
    endif ()
endif ()

if (APPLE)
    find_library(APP_SERVICES_FRAMEWORK ApplicationServices)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    if (NOT APP_SERVICES_FRAMEWORK OR NOT FOUNDATION_FRAMEWORK)
        message(FATAL_ERROR "macOS frameworks not found: ApplicationServices / Foundation")
    endif ()
    target_link_libraries(system_input INTERFACE
            "${APP_SERVICES_FRAMEWORK}"
            "${FOUNDATION_FRAMEWORK}"
    )
elseif (WIN32)
    # target_link_libraries(system_input INTERFACE user32) # optional
elseif (UNIX AND NOT APPLE)
    include(CheckIncludeFile)
    include(FindPkgConfig)
    if (INPUT_BACKEND_WAYLAND_WLR)
        pkg_check_modules(WAYLAND REQUIRED wayland-client)
        target_link_libraries(system_input INTERFACE ${WAYLAND_LINK_LIBRARIES})
        target_include_directories(system_input INTERFACE ${WAYLAND_INCLUDE_DIRS})
        target_compile_definitions(system_input INTERFACE INPUT_BACKEND_WAYLAND_WLR=1)
    elseif (INPUT_BACKEND_UINPUT)
        check_include_file("linux/uinput.h" HAVE_UINPUT_H)
        if (NOT HAVE_UINPUT_H)
            message(FATAL_ERROR "linux/uinput.h not found. Install kernel headers or uinput dev package.")
        endif ()
        target_compile_definitions(system_input INTERFACE INPUT_BACKEND_UINPUT=1)
    else ()
        if (PKG_CONFIG_FOUND)
            pkg_check_modules(X11 REQUIRED x11)
            pkg_check_modules(XTST REQUIRED xtst)
            target_include_directories(system_input INTERFACE ${X11_INCLUDE_DIRS} ${XTST_INCLUDE_DIRS})
            target_link_libraries(system_input INTERFACE ${X11_LIBRARIES} ${XTST_LIBRARIES})
        else ()
            find_package(X11 REQUIRED)
            if (NOT X11_XTest_LIB)
                find_library(XTST_LIB Xtst)
                if (NOT XTST_LIB)
                    message(FATAL_ERROR "X11 found but Xtst not found. Install libxtst-dev.")
                endif ()
                target_link_libraries(system_input INTERFACE ${X11_LIBRARIES} ${XTST_LIB})
            else ()
                target_link_libraries(system_input INTERFACE ${X11_LIBRARIES} ${X11_XTest_LIB})
            endif ()
            target_include_directories(system_input INTERFACE ${X11_INCLUDE_DIR})
        endif ()
        target_compile_definitions(system_input INTERFACE INPUT_BACKEND_X11=1)
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
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
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
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
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
    target_link_libraries(system_input_test PRIVATE system_input)
    if (APPLE)
        target_link_libraries(system_input_test PRIVATE
                ${APP_SERVICES_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
        )
    endif ()

    add_executable(joint_test demo/joint_test.cpp)
    target_link_libraries(joint_test PRIVATE system_input system_output)
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

# 只安装头文件；不把同目录下的 .cpp/.mm 等源文件装进系统
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
        PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.hh" PATTERN "*.hxx"
        PATTERN "*.inc" PATTERN "*.inl" PATTERN "*.ipp"
        PATTERN "*.c" EXCLUDE
        PATTERN "*.cc" EXCLUDE
        PATTERN "*.cxx" EXCLUDE
        PATTERN "*.cpp" EXCLUDE
        PATTERN "*.m" EXCLUDE
        PATTERN "*.mm" EXCLUDE
        PATTERN "*.S" EXCLUDE
        PATTERN "*.asm" EXCLUDE
)

# 安装生成的版本头文件到 include/easy_control
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/easy_control/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/easy_control)

# 统一导出：只导出库（不含 demos）
install(TARGETS system_input system_output
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

set(CPACK_PACKAGE_NAME "easy-control")
set(CPACK_PACKAGE_VENDOR "AutoAlg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "easy_control: system_input + system_output cross-platform library")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/hastws/easy_control")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# 让包名包含提交/dirty 后缀（若存在）
if (AUTOALG_PKG_SUFFIX)
    set(CPACK_PACKAGE_VERSION_SUFFIX "${AUTOALG_PKG_SUFFIX}")
endif ()

# include(CPack) 之后添加
set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_NAME}-${AUTOALG_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")


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

include(CPack)