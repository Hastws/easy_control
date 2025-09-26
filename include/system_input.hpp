// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.
//
// Single-header, cross-platform input synthesizer.
// Backends:
//   - macOS: Quartz/Carbon
//   - Windows: Win32 SendInput
//   - Linux:
//       * Default: X11 + XTest
//       * Optional: Wayland (wlroots virtual pointer/keyboard) => define INPUT_BACKEND_WAYLAND_WLR
//       * Optional: uinput (/dev/uinput) => define INPUT_BACKEND_UINPUT
//
// Build notes (Linux):
//   X11:    -lX11 -lXtst
//   Wayland: -lwayland-client
//   uinput: no extra libs; requires write access to /dev/uinput.
//
// Usage: #include "system_input.hpp"

#ifndef EASY_CONTROL_INCLUDE_SYSTEM_INPUT_HPP
#define EASY_CONTROL_INCLUDE_SYSTEM_INPUT_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#if !defined(INPUT_BACKEND_WAYLAND_WLR) && !defined(INPUT_BACKEND_UINPUT)
#define INPUT_BACKEND_X11 1
#endif
#ifdef INPUT_BACKEND_WAYLAND_WLR
#include <wayland-client.h>
#include <zwlr-virtual-pointer-unstable-v1-client-protocol.h>
#include <zwp-virtual-keyboard-unstable-v1-client-protocol.h>
#endif
#ifdef INPUT_BACKEND_X11
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#endif
#ifdef INPUT_BACKEND_UINPUT
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#else
#error "Unsupported platform."
#endif

#include "common.hpp"

namespace autoalg {

class SystemInput {
 public:
  enum MouseButton : int { kLeft = 0, kRight = 1, kMiddle = 2 };

  enum Mod : uint64_t {
    kNone = 0,
    kShift = 1ull << 0,
    kControl = 1ull << 1,
    kOption = 1ull << 2,  // Alt
    kCommand = 1ull << 3  // Cmd / Super / Win
  };

  EC_INLINE SystemInput() {
#ifdef __APPLE__
    const CGDirectDisplayID did = CGMainDisplayID();
    display_x_ = CGDisplayPixelsWide(did);
    display_y_ = CGDisplayPixelsHigh(did);
    CGEventRef ev = CGEventCreate(nullptr);
    CGPoint p = CGEventGetLocation(ev);
    CFRelease(ev);
    cur_x_ = static_cast<int>(p.x);
    cur_y_ = static_cast<int>(p.y);
#elif defined(_WIN32)
    display_x_ = static_cast<std::size_t>(GetSystemMetrics(SM_CXSCREEN));
    display_y_ = static_cast<std::size_t>(GetSystemMetrics(SM_CYSCREEN));
    POINT pt;
    GetCursorPos(&pt);
    cur_x_ = pt.x;
    cur_y_ = pt.y;
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    display_x_ = 1920;
    display_y_ = 1080;
    wl_display_ = wl_display_connect(nullptr);
    if (wl_display_) {
      wl_registry_ = wl_display_get_registry(wl_display_);
      wl_registry_add_listener(wl_registry_, &RegistryListener(), this);
      wl_display_roundtrip(wl_display_);
      if (vp_mgr_) vp_dev_ = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(vp_mgr_, nullptr);
      if (vkbd_mgr_) vkb_dev_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkbd_mgr_, nullptr);
    }
    cur_x_ = 0;
    cur_y_ = 0;
#elif defined(INPUT_BACKEND_UINPUT)
    display_x_ = 1920;
    display_y_ = 1080;
    uinp_fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinp_fd_ >= 0) {
      ioctl(uinp_fd_, UI_DEV_DESTROY);  // 防止残留
      ioctl(uinp_fd_, UI_SET_EVBIT, EV_KEY);
      ioctl(uinp_fd_, UI_SET_EVBIT, EV_REL);
      ioctl(uinp_fd_, UI_SET_EVBIT, EV_SYN);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_X);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_Y);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_WHEEL);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_HWHEEL);
      ioctl(uinp_fd_, UI_SET_KEYBIT, BTN_LEFT);
      ioctl(uinp_fd_, UI_SET_KEYBIT, BTN_RIGHT);
      ioctl(uinp_fd_, UI_SET_KEYBIT, BTN_MIDDLE);
      for (int code = 1; code < 256; ++code) ioctl(uinp_fd_, UI_SET_KEYBIT, code);
      struct uinput_setup usetup{};
      usetup.id.bustype = BUS_USB;
      usetup.id.vendor = 0x1234;
      usetup.id.product = 0x5678;
      strcpy(usetup.name, "autoalg-uinput-virtual");
      ioctl(uinp_fd_, UI_DEV_SETUP, &usetup);
      ioctl(uinp_fd_, UI_DEV_CREATE);
    }
    cur_x_ = 0;
    cur_y_ = 0;
#else
    dpy_ = XOpenDisplay(nullptr);
    if (dpy_) {
      screen_ = DefaultScreen(dpy_);
      display_x_ = static_cast<std::size_t>(DisplayWidth(dpy_, screen_));
      display_y_ = static_cast<std::size_t>(DisplayHeight(dpy_, screen_));
      Window root = RootWindow(dpy_, screen_);
      Window r, w;
      int rx, ry, wx, wy;
      unsigned int mask;
      if (XQueryPointer(dpy_, root, &r, &w, &rx, &ry, &wx, &wy, &mask)) {
        cur_x_ = rx;
        cur_y_ = ry;
      }
    } else {
      display_x_ = 1920;
      display_y_ = 1080;
      cur_x_ = 0;
      cur_y_ = 0;
    }
#endif
#endif
  }

  EC_INLINE ~SystemInput() {
#if defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      zwp_virtual_keyboard_v1_destroy(vkb_dev_);
      vkb_dev_ = nullptr;
    }
    if (vp_dev_) {
      zwlr_virtual_pointer_v1_destroy(vp_dev_);
      vp_dev_ = nullptr;
    }
    if (wl_seat_) wl_seat_destroy(wl_seat_);
    if (vkbd_mgr_) zwp_virtual_keyboard_manager_v1_destroy(vkbd_mgr_);
    if (vp_mgr_) zwlr_virtual_pointer_manager_v1_destroy(vp_mgr_);
    if (wl_registry_) wl_registry_destroy(wl_registry_);
    if (wl_display_) {
      wl_display_flush(wl_display_);
      wl_display_disconnect(wl_display_);
      wl_display_ = nullptr;
    }
#endif
#ifdef INPUT_BACKEND_UINPUT
    if (uinp_fd_ >= 0) {
      ioctl(uinp_fd_, UI_DEV_DESTROY);
      close(uinp_fd_);
      uinp_fd_ = -1;
    }
#endif
#ifdef INPUT_BACKEND_X11
    if (dpy_) {
      XCloseDisplay(dpy_);
      dpy_ = nullptr;
    }
#endif
#endif
  }

  // ---------- Info / sync ----------
  EC_INLINE std::size_t GetDisplayWidth() const { return display_x_; }  // 原接口（可能是逻辑或像素，取决于平台）
  EC_INLINE std::size_t GetDisplayHeight() const { return display_y_; }
  EC_INLINE int CursorX() const { return cur_x_; }
  EC_INLINE int CursorY() const { return cur_y_; }

  EC_INLINE void SyncCursorFromSystem() {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreate(nullptr);
    CGPoint p = CGEventGetLocation(ev);
    CFRelease(ev);
    cur_x_ = static_cast<int>(p.x);
    cur_y_ = static_cast<int>(p.y);
#elif defined(_WIN32)
    POINT pt;
    if (GetCursorPos(&pt)) {
      cur_x_ = pt.x;
      cur_y_ = pt.y;
    }
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    // 无法直接读取合成器光标，保留内部状态
#elif defined(INPUT_BACKEND_UINPUT)
    // 仅内部追踪
#else
    if (dpy_) {
      Window root = RootWindow(dpy_, screen_);
      Window r, w;
      int rx, ry, wx, wy;
      unsigned int mask;
      if (XQueryPointer(dpy_, root, &r, &w, &rx, &ry, &wx, &wy, &mask)) {
        cur_x_ = rx;
        cur_y_ = ry;
      }
    }
#endif
#endif
  }

  // ---------- Mouse basic ----------
  EC_INLINE void MouseMoveTo(int x, int y) {
    x = std::max(0, std::min<int>(x, static_cast<int>(display_x_)));
    y = std::max(0, std::min<int>(y, static_cast<int>(display_y_)));
#ifdef __APPLE__
    CGEventRef e = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, CGPointMake(x, y), kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
#elif defined(_WIN32)
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    in.mi.dx = static_cast<LONG>((x * 65535LL) / std::max<std::size_t>(1, display_x_ - 1));
    in.mi.dy = static_cast<LONG>((y * 65535LL) / std::max<std::size_t>(1, display_y_ - 1));
    SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vp_dev_) {
#ifdef ZWLR_VIRTUAL_POINTER_V1_MOTION_ABSOLUTE_SINCE_VERSION
      zwlr_virtual_pointer_v1_motion_absolute(
          vp_dev_, 0, wl_fixed_from_double(static_cast<double>(x) / std::max<double>(1.0, (double)display_x_)),
          wl_fixed_from_double(static_cast<double>(y) / std::max<double>(1.0, (double)display_y_)));
      zwlr_virtual_pointer_v1_frame(vp_dev_);
      wl_display_flush(wl_display_);
#else
      zwlr_virtual_pointer_v1_motion(vp_dev_, 0, wl_fixed_from_double(x - cur_x_), wl_fixed_from_double(y - cur_y_));
      zwlr_virtual_pointer_v1_frame(vp_dev_);
      wl_display_flush(wl_display_);
#endif
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputRel_(REL_X, x - cur_x_);
    SendUinputRel_(REL_Y, y - cur_y_);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeMotionEvent(dpy_, screen_, x, y, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
    cur_x_ = x;
    cur_y_ = y;
  }

  EC_INLINE void MouseMoveRelative(int dx, int dy) { MouseMoveTo(cur_x_ + dx, cur_y_ + dy); }

  EC_INLINE void MouseDown(int button) {
#ifdef __APPLE__
    CGEventRef e = CGEventCreateMouseEvent(nullptr, DownEvent_(button), CGPointMake(cur_x_, cur_y_), ToMouseButton_(button));
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
#elif defined(_WIN32)
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = WinButtonDownFlag_(button);
    SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vp_dev_) {
      zwlr_virtual_pointer_v1_button(vp_dev_, 0, LinuxBtnCode_(button), WL_POINTER_BUTTON_STATE_PRESSED);
      zwlr_virtual_pointer_v1_frame(vp_dev_);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(LinuxBtnCode_(button), 1);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeButtonEvent(dpy_, XButtonFromGeneric_(button), True, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE void MouseUp(int button) {
#ifdef __APPLE__
    CGEventRef e = CGEventCreateMouseEvent(nullptr, UpEvent_(button), CGPointMake(cur_x_, cur_y_), ToMouseButton_(button));
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
#elif defined(_WIN32)
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = WinButtonUpFlag_(button);
    SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vp_dev_) {
      zwlr_virtual_pointer_v1_button(vp_dev_, 0, LinuxBtnCode_(button), WL_POINTER_BUTTON_STATE_RELEASED);
      zwlr_virtual_pointer_v1_frame(vp_dev_);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(LinuxBtnCode_(button), 0);
    SendUinputSync_();
    else if (dpy_) {
      XTestFakeButtonEvent(dpy_, XButtonFromGeneric_(button), False, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE void MouseClick(int button) {
    MouseDown(button);
    MouseUp(button);
  }
  EC_INLINE void MouseDoubleClick(int button) {
    MouseClick(button);
    MouseClick(button);
  }
  EC_INLINE void MouseTripleClick(int button) {
    MouseClick(button);
    MouseClick(button);
    MouseClick(button);
  }

  EC_INLINE void MouseDownAt(int x, int y, int button) {
    MouseMoveTo(x, y);
    MouseDown(button);
  }
  EC_INLINE void MouseUpAt(int x, int y, int button) {
    MouseMoveTo(x, y);
    MouseUp(button);
  }
  EC_INLINE void MouseClickAt(int x, int y, int button) {
    MouseMoveTo(x, y);
    MouseClick(button);
  }

  EC_INLINE void MouseDragTo(int x, int y, int button) {
    SyncCursorFromSystem();
    x = std::max(0, std::min<int>(x, static_cast<int>(display_x_)));
    y = std::max(0, std::min<int>(y, static_cast<int>(display_y_)));
    const int sx = cur_x_, sy = cur_y_;
#ifdef __APPLE__
    {
      CGEventRef down = CGEventCreateMouseEvent(nullptr,
                                                (button == kRight    ? kCGEventRightMouseDown
                                                 : button == kMiddle ? kCGEventOtherMouseDown
                                                                     : kCGEventLeftMouseDown),
                                                CGPointMake(sx, sy),
                                                (button == kRight    ? kCGMouseButtonRight
                                                 : button == kMiddle ? kCGMouseButtonCenter
                                                                     : kCGMouseButtonLeft));
      CGEventPost(kCGHIDEventTap, down);
      CFRelease(down);
    }
#elif defined(_WIN32)
    {
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags =
          (button == kRight ? MOUSEEVENTF_RIGHTDOWN : (button == kMiddle ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN));
      SendInput(1, &in, sizeof(in));
    }
#elif defined(__linux__)
#if defined(INPUT_BACKEND_WAYLAND_WLR)
    if (vp_dev_) {
      zwlr_virtual_pointer_v1_button(vp_dev_, 0, LinuxBtnCode_(button), WL_POINTER_BUTTON_STATE_PRESSED);
      zwlr_virtual_pointer_v1_frame(vp_dev_);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(LinuxBtnCode_(button), 1);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeButtonEvent(dpy_, XButtonFromGeneric_(button), True, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
    EmitDragPath_(sx, sy, x, y, button);
#ifdef __APPLE__
    {
      CGEventRef up = CGEventCreateMouseEvent(nullptr,
                                              (button == kRight    ? kCGEventRightMouseUp
                                               : button == kMiddle ? kCGEventOtherMouseUp
                                                                   : kCGEventLeftMouseUp),
                                              CGPointMake(x, y),
                                              (button == kRight    ? kCGMouseButtonRight
                                               : button == kMiddle ? kCGMouseButtonCenter
                                                                   : kCGMouseButtonLeft));
      CGEventPost(kCGHIDEventTap, up);
      CFRelease(up);
    }
#elif defined(_WIN32)
    {
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags =
          (button == kRight ? MOUSEEVENTF_RIGHTUP : (button == kMiddle ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_LEFTUP));
      SendInput(1, &in, sizeof(in));
    }
#elif defined(__linux__)
#if defined(INPUT_BACKEND_WAYLAND_WLR)
    if (vp_dev_) {
      zwlr_virtual_pointer_v1_button(vp_dev_, 0, LinuxBtnCode_(button), WL_POINTER_BUTTON_STATE_RELEASED);
      zwlr_virtual_pointer_v1_frame(vp_dev_);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(LinuxBtnCode_(button), 0);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeButtonEvent(dpy_, XButtonFromGeneric_(button), False, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }
  EC_INLINE void MouseDragBy(int dx, int dy, int button) { MouseDragTo(cur_x_ + dx, cur_y_ + dy, button); }

  EC_INLINE void MouseHold(int button, double seconds) {
    MouseDown(button);
    if (seconds > 0) std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    MouseUp(button);
  }

  EC_INLINE void ScrollLines(int dx, int dy) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, dy, dx);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    if (dy != 0) {
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_WHEEL;
      in.mi.mouseData = (DWORD)dy * WHEEL_DELTA;
      SendInput(1, &in, sizeof(in));
    }
    if (dx != 0) {
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
      in.mi.mouseData = (DWORD)dx * WHEEL_DELTA;
      SendInput(1, &in, sizeof(in));
    }
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vp_dev_) {
      if (dx) zwlr_virtual_pointer_v1_axis(vp_dev_, 0, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(-dx));
      if (dy) zwlr_virtual_pointer_v1_axis(vp_dev_, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(-dy));
      zwlr_virtual_pointer_v1_frame(vp_dev_);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    if (dy) SendUinputRel_(REL_WHEEL, dy);
    if (dx) SendUinputRel_(REL_HWHEEL, dx);
    SendUinputSync_();
#else
    if (dpy_) {
      auto emit = [&](int btn, int cnt) {
        int n = std::abs(cnt);
        for (int i = 0; i < n; ++i) {
          XTestFakeButtonEvent(dpy_, btn, True, CurrentTime);
          XTestFakeButtonEvent(dpy_, btn, False, CurrentTime);
        }
      };
      if (dy > 0)
        emit(4, dy);
      else if (dy < 0)
        emit(5, -dy);
      if (dx > 0)
        emit(6, dx);
      else if (dx < 0)
        emit(7, -dx);
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE void ScrollPixels(int dx, int dy) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 2, dy, dx);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    const int step = 1;
    if (dy) {
      int n = std::abs(dy);
      for (int i = 0; i < n; ++i) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = (dy > 0 ? +step : -step);
        SendInput(1, &in, sizeof(in));
      }
    }
    if (dx) {
      int n = std::abs(dx);
      for (int i = 0; i + n; ++i) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        in.mi.mouseData = (dx > 0 ? +step : -step);
        SendInput(1, &in, sizeof(in));
      }
    }
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    ScrollLines(dx, dy);
#elif defined(INPUT_BACKEND_UINPUT)
    ScrollLines(dx, dy);
#else
    ScrollLines(dx, dy);
#endif
#endif
  }

  EC_INLINE void MouseScrollX(int length) { ScrollLines(length, 0); }
  EC_INLINE void MouseScrollY(int length) { ScrollLines(0, length); }

  // ---------- Keyboard ----------
  EC_INLINE void KeyboardDown(int key) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), true);
    CGEventPost(kCGAnnotatedSessionEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)key;
    in.ki.dwFlags = 0;
    SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      zwp_virtual_keyboard_v1_key(vkb_dev_, 0, key, WL_KEYBOARD_KEY_STATE_PRESSED);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(key, 1);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeKeyEvent(dpy_, (KeyCode)key, True, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE void KeyboardUp(int key) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), false);
    CGEventPost(kCGAnnotatedSessionEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)key;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      zwp_virtual_keyboard_v1_key(vkb_dev_, 0, key, WL_KEYBOARD_KEY_STATE_RELEASED);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(key, 0);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeKeyEvent(dpy_, (KeyCode)key, False, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE void KeyboardClick(int key) {
    KeyboardDown(key);
    KeyboardUp(key);
  }

  EC_INLINE void KeyboardDownWithMods(int key, uint64_t mods) {
#if defined(__APPLE__)
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)key, true);
    CGEventSetFlags(ev, BuildFlagsMac_(mods));
    CGEventPost(kCGAnnotatedSessionEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    if (mods & kShift) keybd_event(VK_SHIFT, 0, 0, 0);
    if (mods & kControl) keybd_event(VK_CONTROL, 0, 0, 0);
    if (mods & kOption) keybd_event(VK_MENU, 0, 0, 0);
    if (mods & kCommand) keybd_event(VK_LWIN, 0, 0, 0);
    KeyboardDown(key);
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      if (mods & kShift) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyShift_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      if (mods & kControl) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyCtrl_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      if (mods & kOption) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyAlt_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      if (mods & kCommand) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeySuper_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      zwp_virtual_keyboard_v1_key(vkb_dev_, 0, key, WL_KEYBOARD_KEY_STATE_PRESSED);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    if (mods & kShift) SendUinputKey_(LinuxKeyShift_(), 1);
    if (mods & kControl) SendUinputKey_(LinuxKeyCtrl_(), 1);
    if (mods & kOption) SendUinputKey_(LinuxKeyAlt_(), 1);
    if (mods & kCommand) SendUinputKey_(LinuxKeySuper_(), 1);
    SendUinputKey_(key, 1);
    SendUinputSync_();
#else
    if (dpy_) {
      PressModX11_(mods, true);
      XTestFakeKeyEvent(dpy_, (KeyCode)key, True, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE void KeyboardUpWithMods(int key, uint64_t mods) {
#if defined(__APPLE__)
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, (CGKeyCode)key, false);
    CGEventSetFlags(ev, BuildFlagsMac_(mods));
    CGEventPost(kCGAnnotatedSessionEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    KeyboardUp(key);
    if (mods & kCommand) keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
    if (mods & kOption) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    if (mods & kControl) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    if (mods & kShift) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      zwp_virtual_keyboard_v1_key(vkb_dev_, 0, key, WL_KEYBOARD_KEY_STATE_RELEASED);
      if (mods & kCommand) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeySuper_(), WL_KEYBOARD_KEY_STATE_RELEASED);
      if (mods & kOption) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyAlt_(), WL_KEYBOARD_KEY_STATE_RELEASED);
      if (mods & kControl) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyCtrl_(), WL_KEYBOARD_KEY_STATE_RELEASED);
      if (mods & kShift) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyShift_(), WL_KEYBOARD_KEY_STATE_RELEASED);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(key, 0);
    if (mods & kCommand) SendUinputKey_(LinuxKeySuper_(), 0);
    if (mods & kOption) SendUinputKey_(LinuxKeyAlt_(), 0);
    if (mods & kControl) SendUinputKey_(LinuxKeyCtrl_(), 0);
    if (mods & kShift) SendUinputKey_(LinuxKeyShift_(), 0);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeKeyEvent(dpy_, (KeyCode)key, False, CurrentTime);
      PressModX11_(mods, false);
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE void KeyboardClickWithMods(int key, uint64_t mods) {
#if defined(_WIN32)
    if (mods & kShift) keybd_event(VK_SHIFT, 0, 0, 0);
    if (mods & kControl) keybd_event(VK_CONTROL, 0, 0, 0);
    if (mods & kOption) keybd_event(VK_MENU, 0, 0, 0);
    if (mods & kCommand) keybd_event(VK_LWIN, 0, 0, 0);
    KeyboardClick(key);
    if (mods & kCommand) keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
    if (mods & kOption) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    if (mods & kControl) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    if (mods & kShift) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
#else
    KeyboardDownWithMods(key, mods);
    KeyboardUpWithMods(key, mods);
#endif
  }

  EC_INLINE void KeyChord(std::initializer_list<uint64_t> modifiers, int key) {
    uint64_t m = 0;
    for (auto v : modifiers) m |= v;
    KeyboardClickWithMods(key, m);
  }

  EC_INLINE void KeySequence(const std::string& sequence) {
    for (char c : sequence) {
      int code = CharToKeyCode(c);
      if (code >= 0) KeyboardClick(code);
    }
  }

  EC_INLINE void TypeUTF8(const std::string& utf8_text) {
#ifdef __APPLE__
    if (utf8_text.empty()) return;
    CFStringRef cf = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8_text.data()),
                                             utf8_text.size(), kCFStringEncodingUTF8, false);
    if (!cf) return;
    CFIndex len = CFStringGetLength(cf);
    UniChar* buf = (UniChar*)malloc(sizeof(UniChar) * (len ? len : 1));
    if (!buf) {
      CFRelease(cf);
      return;
    }
    CFStringGetCharacters(cf, CFRangeMake(0, len), buf);
    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, 0, true);
    CGEventKeyboardSetUnicodeString(down, len, buf);
    CGEventPost(kCGAnnotatedSessionEventTap, down);
    CFRelease(down);
    CGEventRef up = CGEventCreateKeyboardEvent(nullptr, 0, false);
    CGEventKeyboardSetUnicodeString(up, len, buf);
    CGEventPost(kCGAnnotatedSessionEventTap, up);
    CFRelease(up);
    free(buf);
    CFRelease(cf);
#elif defined(_WIN32)
    if (utf8_text.empty()) return;
    auto emit_utf16 = [&](wchar_t u) {
      INPUT in{};
      in.type = INPUT_KEYBOARD;
      in.ki.wScan = u;
      in.ki.dwFlags = KEYEVENTF_UNICODE;
      SendInput(1, &in, sizeof(in));
      in.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      SendInput(1, &in, sizeof(in));
    };
    for (size_t i = 0; i < utf8_text.size();) {
      unsigned int cp = 0;
      unsigned char c = (unsigned char)utf8_text[i];
      if (c < 0x80) {
        cp = c;
        i += 1;
      } else if ((c >> 5) == 0x6) {
        cp = ((c & 0x1F) << 6) | (utf8_text[i + 1] & 0x3F);
        i += 2;
      } else if ((c >> 4) == 0xE) {
        cp = ((c & 0x0F) << 12) | ((utf8_text[i + 1] & 0x3F) << 6) | (utf8_text[i + 2] & 0x3F);
        i += 3;
      } else {
        cp = ((c & 0x07) << 18) | ((utf8_text[i + 1] & 0x3F) << 12) | ((utf8_text[i + 2] & 0x3F) << 6) |
             (utf8_text[i + 3] & 0x3F);
        i += 4;
      }
      if (cp <= 0xFFFF)
        emit_utf16((wchar_t)cp);
      else {
        cp -= 0x10000;
        wchar_t hi = (wchar_t)(0xD800 + (cp >> 10)), lo = (wchar_t)(0xDC00 + (cp & 0x3FF));
        emit_utf16(hi);
        emit_utf16(lo);
      }
    }
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      for (unsigned char c : utf8_text) {
        int key = LinuxAsciiToKeyCode_(c);
        if (key < 0) continue;
        zwp_virtual_keyboard_v1_key(vkb_dev_, 0, key, WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(vkb_dev_, 0, key, WL_KEYBOARD_KEY_STATE_RELEASED);
      }
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    for (unsigned char c : utf8_text) {
      int key = LinuxAsciiToKeyCode_(c);
      if (key < 0) continue;
      SendUinputKey_(key, 1);
      SendUinputKey_(key, 0);
      SendUinputSync_();
    }
#else
    if (dpy_) {
      for (unsigned char c : utf8_text) {
        KeySym sym = NoSymbol;
        if (c >= 32 && c < 127)
          sym = (KeySym)c;
        else if (c == '\n' || c == '\r')
          sym = XK_Return;
        else if (c == '\t')
          sym = XK_Tab;
        else
          continue;
        KeyCode kc = XKeysymToKeycode(dpy_, sym);
        if (kc) {
          XTestFakeKeyEvent(dpy_, kc, True, CurrentTime);
          XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        }
      }
      XFlush(dpy_);
    }
#endif
#endif
  }

  EC_INLINE int CharToKeyCode(char key_char) {
#ifdef __APPLE__
    static CFMutableDictionaryRef dict = nullptr;
    if (!dict) {
      dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 128, &kCFCopyStringDictionaryKeyCallBacks, nullptr);
      for (std::size_t i = 0; i < 128; ++i) {
        TISInputSourceRef src = TISCopyCurrentKeyboardInputSource();
        if (!src) continue;
        CFDataRef data = (CFDataRef)TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData);
        if (!data) {
          CFRelease(src);
          continue;
        }
        const UCKeyboardLayout* layout = (const UCKeyboardLayout*)CFDataGetBytePtr(data);
        UInt32 keys_down = 0;
        UniChar chars[4] = {0};
        UniCharCount real = 0;
        if (UCKeyTranslate(layout, (CGKeyCode)i, kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit,
                           &keys_down, 4, &real, chars) == noErr &&
            real > 0) {
          CFStringRef s = CFStringCreateWithCharacters(kCFAllocatorDefault, chars, 1);
          if (s) {
            CFNumberRef num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &i);
            CFDictionaryAddValue(dict, s, num);
            CFRelease(num);
            CFRelease(s);
          }
        }
        CFRelease(src);
      }
    }
    UniChar ch = (unsigned char)key_char;
    CFStringRef key = CFStringCreateWithCharacters(kCFAllocatorDefault, &ch, 1);
    int32_t v = -1;
    if (key) {
      CFNumberRef num = nullptr;
      if (CFDictionaryGetValueIfPresent(dict, key, (const void**)&num) && num) CFNumberGetValue(num, kCFNumberSInt32Type, &v);
      CFRelease(key);
    }
    return v >= 0 ? v : -1;
#elif defined(_WIN32)
    SHORT vk = VkKeyScanA(key_char);
    if (vk == -1) return -1;
    return LOBYTE(vk);
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    return LinuxAsciiToKeyCode_((unsigned char)key_char);
#elif defined(INPUT_BACKEND_UINPUT)
    return LinuxAsciiToKeyCode_((unsigned char)key_char);
#else
    if (!dpy_) return -1;
    KeySym sym = NoSymbol;
    unsigned char c = (unsigned char)key_char;
    if (c >= 32 && c < 127)
      sym = (KeySym)c;
    else if (c == '\n' || c == '\r')
      sym = XK_Return;
    else if (c == '\t')
      sym = XK_Tab;
    if (sym == NoSymbol) return -1;
    KeyCode kc = XKeysymToKeycode(dpy_, sym);
    return kc ? (int)kc : -1;
#endif
#endif
  }

  // ===== 新增：像素映射能力（对齐分辨率 & 鼠标位置） =====

  // 校准：基于“光标所在显示器”计算逻辑↔像素的 scale 与偏移
  // EC_INLINE void CalibratePixelMapping();

  // 获取像素口径的光标坐标（与屏幕像素分辨率一致）
  // EC_INLINE void GetCursorPixel(int& x_px, int& y_px);

  // 获取主屏像素分辨率
  // EC_INLINE void GetPrimaryDisplayPixelSize(int& w_px, int& h_px);

  // 按像素移动鼠标到“光标所在显示器”的 (px,py) 位置（对齐显示器像素坐标系）
  EC_INLINE void MouseMoveToPixels(int px, int py) {
    // 懒校准
    CalibratePixelMapping();

#if defined(__APPLE__)
    // 目标像素 -> 该显示器的“点”
    int lx = (int)std::lround(px / std::max(1e-9, dpi_scale_x_)) + mon_origin_logical_x_;
    int ly = (int)std::lround(py / std::max(1e-9, dpi_scale_y_)) + mon_origin_logical_y_;
    MouseMoveTo(lx, ly);

#elif defined(_WIN32)
    // 我们把 MouseMoveTo 视为“全局虚拟桌面逻辑坐标”
    // 需要把“显示器内像素” -> “全局像素” -> “逻辑”（除以 scale）
    int base_px = (int)std::lround(mon_origin_logical_x_ * dpi_scale_x_);
    int base_py = (int)std::lround(mon_origin_logical_y_ * dpi_scale_y_);
    int global_px = base_px + px;
    int global_py = base_py + py;
    int lx = (int)std::lround(global_px / std::max(1e-9, dpi_scale_x_));
    int ly = (int)std::lround(global_py / std::max(1e-9, dpi_scale_y_));
    MouseMoveTo(lx, ly);

#elif defined(__linux__)
#ifdef INPUT_BACKEND_X11
    // X11 默认逻辑=像素；可直接移动
    MouseMoveTo(px, py);
#else
    // Wayland/uinput 无法严格像素定位，这里近似归一化/相对移动
    MouseMoveTo(px, py);
#endif
#endif
  }

 private:
  // ---------- Shared state ----------
  int cur_x_{0};
  int cur_y_{0};
  std::size_t display_x_{0};
  std::size_t display_y_{0};

  // ===== 新增：像素映射缓存（当前光标所在显示器） =====
  double dpi_scale_x_{1.0};
  double dpi_scale_y_{1.0};
  int mon_origin_logical_x_{0};  // 显示器左上角在虚拟桌面的逻辑坐标
  int mon_origin_logical_y_{0};
  int mon_width_px_{0};  // 显示器像素宽高（可用于裁剪）
  int mon_height_px_{0};

  EC_INLINE void EmitDragPath_(int start_x, int start_y, int end_x, int end_y, int button) {
    const int dx = end_x - start_x, dy = end_y - start_y;
    const int dist = std::max(std::abs(dx), std::abs(dy));
    const int kStepPx = 6;
    int steps = std::max(8, dist / kStepPx);
    steps = std::min(steps, 240);
    auto lerp = [](int a, int b, double t) -> int { return (int)(a + (b - a) * t + (t < 1.0 ? 0.5 : 0.0)); };
    for (int i = 1; i <= steps; ++i) {
      const double t = (double)i / steps;
      const int ix = lerp(start_x, end_x, t);
      const int iy = lerp(start_y, end_y, t);
#ifdef __APPLE__
      CGEventRef drag = CGEventCreateMouseEvent(nullptr,
                                                (button == kRight    ? kCGEventRightMouseDragged
                                                 : button == kMiddle ? kCGEventOtherMouseDragged
                                                                     : kCGEventLeftMouseDragged),
                                                CGPointMake(ix, iy),
                                                (button == kRight    ? kCGMouseButtonRight
                                                 : button == kMiddle ? kCGMouseButtonCenter
                                                                     : kCGMouseButtonLeft));
      CGEventPost(kCGHIDEventTap, drag);
      CFRelease(drag);
#elif defined(_WIN32)
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
      in.mi.dx = (LONG)((ix * 65535ll) / std::max<std::size_t>(1, display_x_ - 1));
      in.mi.dy = (LONG)((iy * 65535ll) / std::max<std::size_t>(1, display_y_ - 1));
      SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#if defined(INPUT_BACKEND_WAYLAND_WLR)
      if (vp_dev_) {
#ifdef ZWLR_VIRTUAL_POINTER_V1_MOTION_ABSOLUTE_SINCE_VERSION
        double nx = (double)ix / std::max<double>(1.0, (double)display_x_);
        double ny = (double)iy / std::max<double>(1.0, (double)display_y_);
        zwlr_virtual_pointer_v1_motion_absolute(vp_dev_, 0, wl_fixed_from_double(nx), wl_fixed_from_double(ny));
        zwlr_virtual_pointer_v1_frame(vp_dev_);
        wl_display_flush(wl_display_);
#else
        zwlr_virtual_pointer_v1_motion(vp_dev_, 0, wl_fixed_from_double(ix - cur_x_), wl_fixed_from_double(iy - cur_y_));
        zwlr_virtual_pointer_v1_frame(vp_dev_);
        wl_display_flush(wl_display_);
#endif
      }
#elif defined(INPUT_BACKEND_UINPUT)
      SendUinputRel_(REL_X, ix - cur_x_);
      SendUinputRel_(REL_Y, iy - cur_y_);
      SendUinputSync_();
#else
      if (dpy_) {
        XTestFakeMotionEvent(dpy_, screen_, ix, iy, CurrentTime);
        XFlush(dpy_);
      }
#endif
#endif
      cur_x_ = ix;
      cur_y_ = iy;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

#ifdef __APPLE__
  EC_INLINE CGEventFlags BuildFlagsMac_(uint64_t mods) {
    CGEventFlags f = 0;
    if (mods & kShift) f |= kCGEventFlagMaskShift;
    if (mods & kControl) f |= kCGEventFlagMaskControl;
    if (mods & kOption) f |= kCGEventFlagMaskAlternate;
    if (mods & kCommand) f |= kCGEventFlagMaskCommand;
    return f;
  }
  EC_INLINE CGMouseButton ToMouseButton_(int b) {
    return b == kRight ? kCGMouseButtonRight : (b == kMiddle ? kCGMouseButtonCenter : kCGMouseButtonLeft);
  }
  EC_INLINE CGEventType DownEvent_(int b) {
    return b == kRight ? kCGEventRightMouseDown : b == kMiddle ? kCGEventOtherMouseDown : kCGEventLeftMouseDown;
  }
  EC_INLINE CGEventType UpEvent_(int b) {
    return b == kRight ? kCGEventRightMouseUp : b == kMiddle ? kCGEventOtherMouseUp : kCGEventLeftMouseUp;
  }
#endif

#ifdef _WIN32
  EC_INLINE WORD WinButtonDownFlag_(int b) {
    return b == kRight ? MOUSEEVENTF_RIGHTDOWN : (b == kMiddle ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN);
  }
  EC_INLINE WORD WinButtonUpFlag_(int b) {
    return b == kRight ? MOUSEEVENTF_RIGHTUP : (b == kMiddle ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_LEFTUP);
  }
#endif

#ifdef __linux__
#ifdef INPUT_BACKEND_X11
  Display* dpy_{nullptr};
  int screen_{0};
  EC_INLINE int XButtonFromGeneric_(int b) { return b == kRight ? 3 : (b == kMiddle ? 2 : 1); }
  EC_INLINE void PressModX11_(uint64_t mods, bool press) {
    auto act = [&](KeySym sym) {
      KeyCode kc = XKeysymToKeycode(dpy_, sym);
      if (kc) XTestFakeKeyEvent(dpy_, kc, press ? True : False, CurrentTime);
    };
    if (mods & kShift) act(XK_Shift_L);
    if (mods & kControl) act(XK_Control_L);
    if (mods & kOption) act(XK_Alt_L);
    if (mods & kCommand) act(XK_Super_L);
  }
#endif

#ifdef INPUT_BACKEND_UINPUT
  int uinp_fd_{-1};
  EC_INLINE void SendUinputSync_() {
    if (uinp_fd_ < 0) return;
    input_event ev{};
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinp_fd_, &ev, sizeof(ev));
  }
  EC_INLINE void SendUinputRel_(unsigned short code, int value) {
    if (uinp_fd_ < 0) return;
    input_event ev{};
    ev.type = EV_REL;
    ev.code = code;
    ev.value = value;
    write(uinp_fd_, &ev, sizeof(ev));
  }
  EC_INLINE void SendUinputKey_(int code, int press) {
    if (uinp_fd_ < 0) return;
    input_event ev{};
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = press ? 1 : 0;
    write(uinp_fd_, &ev, sizeof(ev));
  }
  EC_INLINE int LinuxBtnCode_(int b) const { return b == kRight ? BTN_RIGHT : (b == kMiddle ? BTN_MIDDLE : BTN_LEFT); }
  EC_INLINE int LinuxKeyShift_() const { return KEY_LEFTSHIFT; }
  EC_INLINE int LinuxKeyCtrl_() const { return KEY_LEFTCTRL; }
  EC_INLINE int LinuxKeyAlt_() const { return KEY_LEFTALT; }
  EC_INLINE int LinuxKeySuper_() const { return KEY_LEFTMETA; }
  EC_INLINE int LinuxAsciiToKeyCode_(unsigned char c) const {
    if (c >= 'a' && c <= 'z') return KEY_A + (c - 'a');
    if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
    if (c >= '0' && c <= '9') return KEY_0 + (c - '0');
    switch (c) {
      case ' ':
        return KEY_SPACE;
      case '\n':
      case '\r':
        return KEY_ENTER;
      case '\t':
        return KEY_TAB;
      case '-':
        return KEY_MINUS;
      case '=':
        return KEY_EQUAL;
      case '[':
        return KEY_LEFTBRACE;
      case ']':
        return KEY_RIGHTBRACE;
      case '\\':
        return KEY_BACKSLASH;
      case ';':
        return KEY_SEMICOLON;
      case '\'':
        return KEY_APOSTROPHE;
      case ',':
        return KEY_COMMA;
      case '.':
        return KEY_DOT;
      case '/':
        return KEY_SLASH;
      case '`':
        return KEY_GRAVE;
      default:
        return -1;
    }
  }
#endif

#ifdef INPUT_BACKEND_WAYLAND_WLR
  wl_display* wl_display_{nullptr};
  wl_registry* wl_registry_{nullptr};
  wl_seat* wl_seat_{nullptr};
  zwlr_virtual_pointer_manager_v1* vp_mgr_{nullptr};
  zwp_virtual_keyboard_manager_v1* vkbd_mgr_{nullptr};
  zwlr_virtual_pointer_v1* vp_dev_{nullptr};
  zwp_virtual_keyboard_v1* vkb_dev_{nullptr};

  static void RegistryGlobal_(void* data, wl_registry* reg, uint32_t name, const char* interface, uint32_t version) {
    auto* self = static_cast<SystemInput*>(data);
    if (strcmp(interface, wl_seat_interface.name) == 0)
      self->wl_seat_ = (wl_seat*)wl_registry_bind(reg, name, &wl_seat_interface, 1);
    else if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0)
      self->vp_mgr_ =
          (zwlr_virtual_pointer_manager_v1*)wl_registry_bind(reg, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
    else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0)
      self->vkbd_mgr_ =
          (zwp_virtual_keyboard_manager_v1*)wl_registry_bind(reg, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
  }
  static void RegistryGlobalRemove_(void*, wl_registry*, uint32_t) {}
  EC_INLINE const wl_registry_listener& RegistryListener() const {
    static const wl_registry_listener k = {RegistryGlobal_, RegistryGlobalRemove_};
    return k;
  }

  EC_INLINE uint32_t LinuxBtnCode_(int b) const { return b == kRight ? BTN_RIGHT : (b == kMiddle ? BTN_MIDDLE : BTN_LEFT); }
  EC_INLINE int LinuxKeyShift_() const { return KEY_LEFTSHIFT; }
  EC_INLINE int LinuxKeyCtrl_() const { return KEY_LEFTCTRL; }
  EC_INLINE int LinuxKeyAlt_() const { return KEY_LEFTALT; }
  EC_INLINE int LinuxKeySuper_() const { return KEY_LEFTMETA; }
  EC_INLINE int LinuxAsciiToKeyCode_(unsigned char c) const {
    if (c >= 'a' && c <= 'z') return KEY_A + (c - 'a');
    if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
    if (c >= '0' && c <= '9') return KEY_0 + (c - '0');
    switch (c) {
      case ' ':
        return KEY_SPACE;
      case '\n':
      case '\r':
        return KEY_ENTER;
      case '\t':
        return KEY_TAB;
      case '-':
        return KEY_MINUS;
      case '=':
        return KEY_EQUAL;
      case '[':
        return KEY_LEFTBRACE;
      case ']':
        return KEY_RIGHTBRACE;
      case '\\':
        return KEY_BACKSLASH;
      case ';':
        return KEY_SEMICOLON;
      case '\'':
        return KEY_APOSTROPHE;
      case ',':
        return KEY_COMMA;
      case '.':
        return KEY_DOT;
      case '/':
        return KEY_SLASH;
      case '`':
        return KEY_GRAVE;
      default:
        return -1;
    }
  }
#endif
#endif  // __linux__

  // ===== 新增方法实现 =====

 public:
  EC_INLINE void CalibratePixelMapping() {
#if defined(__APPLE__)
    CGEventRef ev = CGEventCreate(nullptr);
    CGPoint p = CGEventGetLocation(ev);
    CFRelease(ev);
    uint32_t cnt = 0;
    CGGetActiveDisplayList(0, nullptr, &cnt);
    std::vector<CGDirectDisplayID> ids(cnt);
    if (cnt) CGGetActiveDisplayList(cnt, ids.data(), &cnt);
    CGDirectDisplayID did = CGMainDisplayID();
    for (auto d : ids) {
      if (CGRectContainsPoint(CGDisplayBounds(d), p)) {
        did = d;
        break;
      }
    }
    CGRect r_pt = CGDisplayBounds(did);
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(did);
    double w_pt = mode ? CGDisplayModeGetWidth(mode) : CGDisplayPixelsWide(did);
    double h_pt = mode ? CGDisplayModeGetHeight(mode) : CGDisplayPixelsHigh(did);
    int w_px = mode ? (int)CGDisplayModeGetPixelWidth(mode) : (int)CGDisplayPixelsWide(did);
    int h_px = mode ? (int)CGDisplayModeGetPixelHeight(mode) : (int)CGDisplayPixelsHigh(did);
    if (mode) CGDisplayModeRelease(mode);
    mon_origin_logical_x_ = (int)std::lround(r_pt.origin.x);
    mon_origin_logical_y_ = (int)std::lround(r_pt.origin.y);
    mon_width_px_ = w_px;
    mon_height_px_ = h_px;
    dpi_scale_x_ = (w_pt > 0) ? (double)w_px / w_pt : 1.0;
    dpi_scale_y_ = (h_pt > 0) ? (double)h_px / h_pt : 1.0;

#elif defined(_WIN32)
    POINT p{};
    GetCursorPos(&p);
    HMONITOR hMon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoA(hMon, &mi);
    RECT r = mi.rcMonitor;
    UINT dpiX = 96, dpiY = 96;
    typedef HRESULT(WINAPI * GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
    static auto getDpiForMonitor = (GetDpiForMonitorFn)GetProcAddress(LoadLibraryA("Shcore.dll"), "GetDpiForMonitor");
    if (getDpiForMonitor)
      getDpiForMonitor(hMon, 0, &dpiX, &dpiY);
    else {
      HDC hdc = GetDC(nullptr);
      dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
      dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
      ReleaseDC(nullptr, hdc);
    }
    mon_origin_logical_x_ = (int)std::lround(r.left * 96.0 / dpiX);
    mon_origin_logical_y_ = (int)std::lround(r.top * 96.0 / dpiY);
    mon_width_px_ = (int)(r.right - r.left);
    mon_height_px_ = (int)(r.bottom - r.top);
    dpi_scale_x_ = (double)dpiX / 96.0;
    dpi_scale_y_ = (double)dpiY / 96.0;

#elif defined(__linux__)
#ifdef INPUT_BACKEND_X11
    if (dpy_) {
      mon_origin_logical_x_ = 0;
      mon_origin_logical_y_ = 0;
      mon_width_px_ = (int)DisplayWidth(dpy_, screen_);
      mon_height_px_ = (int)DisplayHeight(dpy_, screen_);
      dpi_scale_x_ = dpi_scale_y_ = 1.0;
    } else {
      mon_origin_logical_x_ = mon_origin_logical_y_ = 0;
      mon_width_px_ = (int)display_x_;
      mon_height_px_ = (int)display_y_;
      dpi_scale_x_ = dpi_scale_y_ = 1.0;
    }
#else
    mon_origin_logical_x_ = mon_origin_logical_y_ = 0;
    mon_width_px_ = (int)display_x_;
    mon_height_px_ = (int)display_y_;
    dpi_scale_x_ = dpi_scale_y_ = 1.0;
#endif
#endif
  }

  EC_INLINE void GetCursorPixel(int& x_px, int& y_px) {
    CalibratePixelMapping();
#if defined(__APPLE__)
    CGEventRef ev = CGEventCreate(nullptr);
    CGPoint p = CGEventGetLocation(ev);
    CFRelease(ev);
    int local_lx = (int)std::lround(p.x) - mon_origin_logical_x_;
    int local_ly = (int)std::lround(p.y) - mon_origin_logical_y_;
    x_px = (int)std::lround(local_lx * dpi_scale_x_);
    y_px = (int)std::lround(local_ly * dpi_scale_y_);

#elif defined(_WIN32)
    POINT p{};
    GetCursorPos(&p);
    int base_px = (int)std::lround(mon_origin_logical_x_ * dpi_scale_x_);
    int base_py = (int)std::lround(mon_origin_logical_y_ * dpi_scale_y_);
    x_px = p.x - base_px;
    y_px = p.y - base_py;

#elif defined(__linux__)
#ifdef INPUT_BACKEND_X11
    if (dpy_) {
      Window root = RootWindow(dpy_, screen_);
      Window rr, cw;
      int rx, ry, wx, wy;
      unsigned int mask;
      if (XQueryPointer(dpy_, root, &rr, &cw, &rx, &ry, &wx, &wy, &mask)) {
        x_px = rx;
        y_px = ry;
        return;
      }
    }
    x_px = cur_x_;
    y_px = cur_y_;
#else
    x_px = cur_x_;
    y_px = cur_y_;
#endif
#endif
  }

  EC_INLINE void GetPrimaryDisplayPixelSize(int& w_px, int& h_px) {
#if defined(__APPLE__)
    CGDirectDisplayID did = CGMainDisplayID();
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(did);
    if (mode) {
      w_px = (int)CGDisplayModeGetPixelWidth(mode);
      h_px = (int)CGDisplayModeGetPixelHeight(mode);
      CGDisplayModeRelease(mode);
    } else {
      w_px = (int)CGDisplayPixelsWide(did);
      h_px = (int)CGDisplayPixelsHigh(did);
    }
#elif defined(_WIN32)
    w_px = GetSystemMetrics(SM_CXSCREEN);
    h_px = GetSystemMetrics(SM_CYSCREEN);
#elif defined(__linux__)
#ifdef INPUT_BACKEND_X11
    if (dpy_) {
      w_px = DisplayWidth(dpy_, screen_);
      h_px = DisplayHeight(dpy_, screen_);
    } else {
      w_px = (int)display_x_;
      h_px = (int)display_y_;
    }
#else
    w_px = (int)display_x_;
    h_px = (int)display_y_;
#endif
#endif
  }
};

}  // namespace autoalg

#endif  // EASY_CONTROL_INCLUDE_SYSTEM_INPUT_HPP
