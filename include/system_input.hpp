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
//   Wayland: -lwayland-client  (and have wlroots unstable protocol headers generated/installed)
//   uinput: no extra libs; requires write access to /dev/uinput.
//
// Usage: #include "input.hpp"

#ifndef AUTOALG_INPUT_SIM_ALL_H_
#define AUTOALG_INPUT_SIM_ALL_H_

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <thread>

#ifdef __APPLE__
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
// Backend selection (Wayland wlroots or uinput can be enabled by macros)
#if !defined(INPUT_BACKEND_WAYLAND_WLR) && !defined(INPUT_BACKEND_UINPUT)
#define INPUT_BACKEND_X11 1
#endif
#ifdef INPUT_BACKEND_WAYLAND_WLR
#include <wayland-client.h>
// You must have these headers available (from wlroots protocols):
//   zwlr-virtual-pointer-unstable-v1-client-protocol.h
//   zwp-virtual-keyboard-unstable-v1-client-protocol.h
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

  // Construction / destruction.
  inline SystemInput() {
#ifdef __APPLE__
    CGDirectDisplayID did = CGMainDisplayID();
    display_x_ = CGDisplayPixelsWide(did);
    display_y_ = CGDisplayPixelsHigh(did);
    // Initialize from current system cursor.
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
    // Wayland (wlroots virtual devices)
    display_x_ = 1920;  // Wayland global coords are compositor-defined; keep a sane default
    display_y_ = 1080;
    wl_display_ = wl_display_connect(nullptr);
    if (wl_display_) {
      wl_registry_ = wl_display_get_registry(wl_display_);
      wl_registry_add_listener(wl_registry_, &RegistryListener(), this);
      wl_display_roundtrip(wl_display_);
      // Create virtual pointer/keyboard if managers are present.
      if (vp_mgr_) {
        vp_dev_ = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(vp_mgr_, nullptr);
      }
      if (vkbd_mgr_) {
        vkb_dev_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkbd_mgr_, nullptr);
      }
    }
    cur_x_ = 0;
    cur_y_ = 0;
#elif defined(INPUT_BACKEND_UINPUT)
    // uinput backend
    display_x_ = 1920;
    display_y_ = 1080;
    uinp_fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinp_fd_ >= 0) {
      ioctl(uinp_fd_, UI_SET_EVBIT, EV_KEY);
      ioctl(uinp_fd_, UI_SET_EVBIT, EV_REL);
      ioctl(uinp_fd_, UI_SET_EVBIT, EV_MSC);
      ioctl(uinp_fd_, UI_SET_EVBIT, EV_SYN);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_X);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_Y);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_WHEEL);
      ioctl(uinp_fd_, UI_SET_RELBIT, REL_HWHEEL);
      // Mouse buttons
      ioctl(uinp_fd_, UI_SET_KEYBIT, BTN_LEFT);
      ioctl(uinp_fd_, UI_SET_KEYBIT, BTN_RIGHT);
      ioctl(uinp_fd_, UI_SET_KEYBIT, BTN_MIDDLE);
      // Common keys (we’ll extend as needed)
      for (int code = 1; code < 256; ++code) ioctl(uinp_fd_, UI_SET_KEYBIT, code);
      struct uinput_setup usetup;
      memset(&usetup, 0, sizeof(usetup));
      usetup.id.bustype = BUS_USB;
      usetup.id.vendor = 0x1234;
      usetup.id.product = 0x5678;
      strcpy(usetup.name, "autoalg-uinput-virtual");
      ioctl(uinp_fd_, UI_DEV_SETUP, &usetup);
      ioctl(uinp_fd_, UI_DEV_CREATE);
    }
    cur_x_ = 0;
    cur_y_ = 0;
#else  // INPUT_BACKEND_X11
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
      } else {
        cur_x_ = 0;
        cur_y_ = 0;
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

  inline ~SystemInput() {
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
  inline std::size_t GetDisplayWidth() const { return display_x_; }
  inline std::size_t GetDisplayHeight() const { return display_y_; }
  inline int CursorX() const { return cur_x_; }
  inline int CursorY() const { return cur_y_; }

  inline void SyncCursorFromSystem() {
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
    // Wayland wlroots virtual devices do not read cursor; keep internal state.
#elif defined(INPUT_BACKEND_UINPUT)
    // uinput is headless; we track internal cursor only.
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
  inline void MouseMoveTo(int x, int y) {
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
      // wlroots virtual pointer uses relative or absolute? We’ll send absolute via set_cursor? Not available.
      // Use motion_absolute (since v1.2); if unavailable in headers, fallback to motion + frame.
#ifdef ZWLR_VIRTUAL_POINTER_V1_MOTION_ABSOLUTE_SINCE_VERSION
      zwlr_virtual_pointer_v1_motion_absolute(
          vp_dev_, 0, wl_fixed_from_double(static_cast<double>(x) / std::max<double>(1.0, static_cast<double>(display_x_))),
          wl_fixed_from_double(static_cast<double>(y) / std::max<double>(1.0, static_cast<double>(display_y_))));
      zwlr_virtual_pointer_v1_frame(vp_dev_);
#else
      // Fallback: relative motion from last pos.
      zwlr_virtual_pointer_v1_motion(vp_dev_, 0, wl_fixed_from_double(x - cur_x_), wl_fixed_from_double(y - cur_y_));
      zwlr_virtual_pointer_v1_frame(vp_dev_);
#endif
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    // uinput uses relative moves; send deltas.
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

  inline void MouseMoveRelative(int dx, int dy) { MouseMoveTo(cur_x_ + dx, cur_y_ + dy); }

  inline void MouseDown(int button) {
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

  inline void MouseUp(int button) {
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
#else
    if (dpy_) {
      XTestFakeButtonEvent(dpy_, XButtonFromGeneric_(button), False, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  inline void MouseClick(int button) {
    MouseDown(button);
    MouseUp(button);
  }

  inline void MouseDoubleClick(int button) {
    MouseClick(button);
    MouseClick(button);
  }

  inline void MouseTripleClick(int button) {
    MouseClick(button);
    MouseClick(button);
    MouseClick(button);
  }

  inline void MouseDownAt(int x, int y, int button) {
    MouseMoveTo(x, y);
    MouseDown(button);
  }

  inline void MouseUpAt(int x, int y, int button) {
    MouseMoveTo(x, y);
    MouseUp(button);
  }

  inline void MouseClickAt(int x, int y, int button) {
    MouseMoveTo(x, y);
    MouseClick(button);
  }

  // 用下面实现替换你原有的 MouseDragTo：

  inline void MouseDragTo(int x, int y, int button) {
    // 同步一下起点，避免内部坐标与系统真实位置有偏差
    SyncCursorFromSystem();

    // 起点终点裁剪
    x = std::max(0, std::min<int>(x, static_cast<int>(display_x_)));
    y = std::max(0, std::min<int>(y, static_cast<int>(display_y_)));

    const int sx = cur_x_;
    const int sy = cur_y_;

    // 先确保按下（按下事件本身很关键，很多程序以此开启拖拽状态机）
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

    // 关键：在按下状态下，逐步发出“拖拽中的移动”事件
    EmitDragPath_(sx, sy, x, y, button);

    // 最后抬起
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

  inline void MouseDragBy(int dx, int dy, int button) { MouseDragTo(cur_x_ + dx, cur_y_ + dy, button); }

  inline void MouseHold(int button, double seconds) {
    MouseDown(button);
    if (seconds > 0) std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    MouseUp(button);
  }

  inline void ScrollLines(int dx, int dy) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, dy, dx);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    if (dy != 0) {
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_WHEEL;
      in.mi.mouseData = static_cast<DWORD>(dy) * WHEEL_DELTA;
      SendInput(1, &in, sizeof(in));
    }
    if (dx != 0) {
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
      in.mi.mouseData = static_cast<DWORD>(dx) * WHEEL_DELTA;
      SendInput(1, &in, sizeof(in));
    }
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vp_dev_) {
      if (dx) {
        zwlr_virtual_pointer_v1_axis(vp_dev_, 0, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                                     wl_fixed_from_double(-dx));  // negative matches typical direction
      }
      if (dy) {
        zwlr_virtual_pointer_v1_axis(vp_dev_, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(-dy));
      }
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

  inline void ScrollPixels(int dx, int dy) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 2, dy, dx);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    // Win32 doesn't expose pixel-level directly; approximate using small wheel steps.
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
      for (int i = 0; i < n; ++i) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        in.mi.mouseData = (dx > 0 ? +step : -step);
        SendInput(1, &in, sizeof(in));
      }
    }
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    // Wayland axis are in "discrete steps" or continuous; we use continuous here.
    ScrollLines(dx, dy);
#elif defined(INPUT_BACKEND_UINPUT)
    // No true pixels; approximate.
    ScrollLines(dx, dy);
#else
    // X11 has no pixel scroll in XTest; approximate with lines.
    ScrollLines(dx, dy);
#endif
#endif
  }

  inline void MouseScrollX(int length) { ScrollLines(length, 0); }
  inline void MouseScrollY(int length) { ScrollLines(0, length); }

  // ---------- Keyboard ----------
  inline void KeyboardDown(int key) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), true);
    CGEventPost(kCGAnnotatedSessionEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = static_cast<WORD>(key);
    in.ki.dwFlags = 0;
    SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      zwp_virtual_keyboard_v1_key(vkb_dev_, 0 /*time*/, key, WL_KEYBOARD_KEY_STATE_PRESSED);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(key, 1);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeKeyEvent(dpy_, static_cast<KeyCode>(key), True, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  inline void KeyboardUp(int key) {
#ifdef __APPLE__
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), false);
    CGEventPost(kCGAnnotatedSessionEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = static_cast<WORD>(key);
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    if (vkb_dev_) {
      zwp_virtual_keyboard_v1_key(vkb_dev_, 0 /*time*/, key, WL_KEYBOARD_KEY_STATE_RELEASED);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    SendUinputKey_(key, 0);
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeKeyEvent(dpy_, static_cast<KeyCode>(key), False, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  inline void KeyboardClick(int key) {
    KeyboardDown(key);
    KeyboardUp(key);
  }

  inline void KeyboardDownWithMods(int key, uint64_t mods) {
#if defined(__APPLE__)
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), true);
    CGEventSetFlags(ev, BuildFlagsMac_(mods));
    CGEventPost(kCGAnnotatedSessionEventTap, ev);
    CFRelease(ev);
#elif defined(_WIN32)
    bool s = false, c = false, a = false, w = false;
    if (mods & kShift) {
      keybd_event(VK_SHIFT, 0, 0, 0);
      s = true;
    }
    if (mods & kControl) {
      keybd_event(VK_CONTROL, 0, 0, 0);
      c = true;
    }
    if (mods & kOption) {
      keybd_event(VK_MENU, 0, 0, 0);
      a = true;
    }
    if (mods & kCommand) {
      keybd_event(VK_LWIN, 0, 0, 0);
      w = true;
    }
    KeyboardDown(key);
    // Do not release here; caller manages via UpWithMods/ClickWithMods.
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    // Press modifiers (Linux key codes expected).
    if (vkb_dev_) {
      if (mods & kShift) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyShift_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      if (mods & kControl) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyCtrl_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      if (mods & kOption) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeyAlt_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      if (mods & kCommand) zwp_virtual_keyboard_v1_key(vkb_dev_, 0, LinuxKeySuper_(), WL_KEYBOARD_KEY_STATE_PRESSED);
      zwp_virtual_keyboard_v1_key(vkb_dev_, 0, key, WL_KEYBOARD_KEY_STATE_PRESSED);
      wl_display_flush(wl_display_);
    }
#elif defined(INPUT_BACKEND_UINPUT)
    if (mods & kShift) {
      SendUinputKey_(LinuxKeyShift_(), 1);
    }
    if (mods & kControl) {
      SendUinputKey_(LinuxKeyCtrl_(), 1);
    }
    if (mods & kOption) {
      SendUinputKey_(LinuxKeyAlt_(), 1);
    }
    if (mods & kCommand) {
      SendUinputKey_(LinuxKeySuper_(), 1);
    }
    SendUinputKey_(key, 1);
    SendUinputSync_();
#else
    if (dpy_) {
      PressModX11_(mods, true);
      XTestFakeKeyEvent(dpy_, static_cast<KeyCode>(key), True, CurrentTime);
      XFlush(dpy_);
    }
#endif
#endif
  }

  inline void KeyboardUpWithMods(int key, uint64_t mods) {
#if defined(__APPLE__)
    CGEventRef ev = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(key), false);
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
    if (mods & kCommand) {
      SendUinputKey_(LinuxKeySuper_(), 0);
    }
    if (mods & kOption) {
      SendUinputKey_(LinuxKeyAlt_(), 0);
    }
    if (mods & kControl) {
      SendUinputKey_(LinuxKeyCtrl_(), 0);
    }
    if (mods & kShift) {
      SendUinputKey_(LinuxKeyShift_(), 0);
    }
    SendUinputSync_();
#else
    if (dpy_) {
      XTestFakeKeyEvent(dpy_, static_cast<KeyCode>(key), False, CurrentTime);
      PressModX11_(mods, false);
      XFlush(dpy_);
    }
#endif
#endif
  }

  inline void KeyboardClickWithMods(int key, uint64_t mods) {
#if defined(_WIN32)
    bool s = false, c = false, a = false, w = false;
    if (mods & kShift) {
      keybd_event(VK_SHIFT, 0, 0, 0);
      s = true;
    }
    if (mods & kControl) {
      keybd_event(VK_CONTROL, 0, 0, 0);
      c = true;
    }
    if (mods & kOption) {
      keybd_event(VK_MENU, 0, 0, 0);
      a = true;
    }
    if (mods & kCommand) {
      keybd_event(VK_LWIN, 0, 0, 0);
      w = true;
    }
    KeyboardClick(key);
    if (w) keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
    if (a) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    if (c) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    if (s) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
#else
    KeyboardDownWithMods(key, mods);
    KeyboardUpWithMods(key, mods);
#endif
  }

  inline void KeyChord(std::initializer_list<uint64_t> modifiers, int key) {
    uint64_t m = 0;
    for (auto v : modifiers) m |= v;
    KeyboardClickWithMods(key, m);
  }

  inline void KeySequence(const std::string &sequence) {
    for (char c : sequence) {
      int code = CharToKeyCode(c);
      if (code >= 0) KeyboardClick(code);
    }
  }

  inline void TypeUTF8(const std::string &utf8_text) {
#ifdef __APPLE__
    if (utf8_text.empty()) return;
    CFStringRef cf = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(utf8_text.data()),
                                             utf8_text.size(), kCFStringEncodingUTF8, false);
    if (!cf) return;
    CFIndex len = CFStringGetLength(cf);
    UniChar *buf = static_cast<UniChar *>(malloc(sizeof(UniChar) * (len ? len : 1)));
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
    for (size_t i = 0; i < utf8_text.size();) {
      unsigned int cp = 0;
      unsigned char c = static_cast<unsigned char>(utf8_text[i]);
      if (c < 0x80) {
        cp = c;
        i += 1;
      } else if ((c >> 5) == 0x6) {
        cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8_text[i + 1]) & 0x3F);
        i += 2;
      } else if ((c >> 4) == 0xE) {
        cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(utf8_text[i + 1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(utf8_text[i + 2]) & 0x3F);
        i += 3;
      } else {
        cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(utf8_text[i + 1]) & 0x3F) << 12) |
             ((static_cast<unsigned char>(utf8_text[i + 2]) & 0x3F) << 6) |
             (static_cast<unsigned char>(utf8_text[i + 3]) & 0x3F);
        i += 4;
      }
      auto emit_utf16 = [&](wchar_t u) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wScan = u;
        in.ki.dwFlags = KEYEVENTF_UNICODE;
        SendInput(1, &in, sizeof(in));
        in.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(in));
      };
      if (cp <= 0xFFFF) {
        emit_utf16(static_cast<wchar_t>(cp));
      } else {
        cp -= 0x10000;
        wchar_t hi = static_cast<wchar_t>(0xD800 + (cp >> 10));
        wchar_t lo = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        emit_utf16(hi);
        emit_utf16(lo);
      }
    }
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    // Feed basic ASCII via virtual keyboard; for IME text, prefer real input method.
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
          sym = static_cast<KeySym>(c);
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

  inline int CharToKeyCode(char key_char) {
#ifdef __APPLE__
    // Layout-aware mapping (limited to ASCII for simplicity).
    // For robust text, use TypeUTF8.
    // Try direct printable ASCII -> keycode is not 1:1, so we fallback to Unicode injection usually.
    // Return -1 when unknown.
    // Here we do a minimal UCKeyTranslate scan for 0..127 once.
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
        const UCKeyboardLayout *layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
        UInt32 keys_down = 0;
        UniChar chars[4] = {0};
        UniCharCount real = 0;
        if (UCKeyTranslate(layout, static_cast<CGKeyCode>(i), kUCKeyActionDisplay, 0, LMGetKbdType(),
                           kUCKeyTranslateNoDeadKeysBit, &keys_down, 4, &real, chars) == noErr &&
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
    UniChar ch = static_cast<unsigned char>(key_char);
    CFStringRef key = CFStringCreateWithCharacters(kCFAllocatorDefault, &ch, 1);
    int32_t v = -1;
    if (key) {
      CFNumberRef num = nullptr;
      if (CFDictionaryGetValueIfPresent(dict, key, reinterpret_cast<const void **>(&num)) && num) {
        CFNumberGetValue(num, kCFNumberSInt32Type, &v);
      }
      CFRelease(key);
    }
    return v >= 0 ? v : -1;
#elif defined(_WIN32)
    SHORT vk = VkKeyScanA(key_char);
    if (vk == -1) return -1;
    return LOBYTE(vk);
#elif defined(__linux__)
#ifdef INPUT_BACKEND_WAYLAND_WLR
    return LinuxAsciiToKeyCode_(static_cast<unsigned char>(key_char));
#elif defined(INPUT_BACKEND_UINPUT)
    return LinuxAsciiToKeyCode_(static_cast<unsigned char>(key_char));
#else
    if (!dpy_) return -1;
    KeySym sym = NoSymbol;
    unsigned char c = static_cast<unsigned char>(key_char);
    if (c >= 32 && c < 127)
      sym = static_cast<KeySym>(c);
    else if (c == '\n' || c == '\r')
      sym = XK_Return;
    else if (c == '\t')
      sym = XK_Tab;
    if (sym == NoSymbol) return -1;
    KeyCode kc = XKeysymToKeycode(dpy_, sym);
    return kc ? static_cast<int>(kc) : -1;
#endif
#endif
  }

 private:
  // ---------- Shared state ----------
  int cur_x_{0};
  int cur_y_{0};
  std::size_t display_x_{0};
  std::size_t display_y_{0};

  // 在 class SystemInput 的 private: 区域里加上：

  inline void EmitDragPath_(int start_x, int start_y, int end_x, int end_y, int button) {
    // 线性插值若干步，逐步发出“拖拽中的移动事件”
    const int dx = end_x - start_x;
    const int dy = end_y - start_y;
    const int adx = std::abs(dx);
    const int ady = std::abs(dy);
    const int dist = std::max(adx, ady);

    // 每步像素（越小越平滑）
    const int kStepPx = 6;
    int steps = std::max(8, dist / kStepPx);
    steps = std::min(steps, 240);  // 上限，避免超多事件

    auto lerp = [](int a, int b, double t) -> int {
      return static_cast<int>(a + (b - a) * t + (t < 1.0 ? 0.5 : 0.0));  // 逐步取整
    };

    for (int i = 1; i <= steps; ++i) {
      const double t = static_cast<double>(i) / steps;
      const int ix = lerp(start_x, end_x, t);
      const int iy = lerp(start_y, end_y, t);

#ifdef __APPLE__
      // 发送“正在拖拽”的鼠标事件（而不是普通移动）
      CGEventRef drag = CGEventCreateMouseEvent(nullptr,
                                                (button == kRight    ? kCGEventRightMouseDragged
                                                 : button == kMiddle ? kCGEventOtherMouseDragged
                                                                     : kCGEventLeftMouseDragged),
                                                CGPointMake(ix, iy),
                                                (button == kRight    ? kCGMouseButtonRight
                                                 : button == kMiddle ? kCGMouseButtonCenter
                                                                     : kCGMouseButtonLeft));
      // 一些程序更吃 flags，这里无需额外 flags；如需可设置压力等高级字段：
      // CGEventSetDoubleValueField(drag, kCGMouseEventPressure, 1.0);
      CGEventPost(kCGHIDEventTap, drag);
      CFRelease(drag);

#elif defined(_WIN32)
      // 在按下期间发 MOVE 事件（ABS 模式），很多 UI 需要这些 MOVE 才会绘制选框
      INPUT in{};
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
      in.mi.dx = static_cast<LONG>((ix * 65535ll) / std::max<std::size_t>(1, display_x_ - 1));
      in.mi.dy = static_cast<LONG>((iy * 65535ll) / std::max<std::size_t>(1, display_y_ - 1));
      SendInput(1, &in, sizeof(in));

#elif defined(__linux__)
#if defined(INPUT_BACKEND_WAYLAND_WLR)
      if (vp_dev_) {
#ifdef ZWLR_VIRTUAL_POINTER_V1_MOTION_ABSOLUTE_SINCE_VERSION
        // 归一化到 [0,1]
        double nx = static_cast<double>(ix) / std::max<double>(1.0, static_cast<double>(display_x_));
        double ny = static_cast<double>(iy) / std::max<double>(1.0, static_cast<double>(display_y_));
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
      // uinput 只能相对移动，连续发相对步进
      SendUinputRel_(REL_X, ix - cur_x_);
      SendUinputRel_(REL_Y, iy - cur_y_);
      SendUinputSync_();
#else  // X11
      if (dpy_) {
        XTestFakeMotionEvent(dpy_, screen_, ix, iy, CurrentTime);
        XFlush(dpy_);
      }
#endif
#endif

      // 更新内部记录，供下一个步进（以及某些后端使用相对移动时依赖）
      cur_x_ = ix;
      cur_y_ = iy;

      // 小睡一会，给前台应用绘制机会；你可以按需调大或去掉
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  // ---------- macOS helpers ----------
#ifdef __APPLE__
  inline CGEventFlags BuildFlagsMac_(uint64_t mods) {
    CGEventFlags f = 0;
    if (mods & kShift) f |= kCGEventFlagMaskShift;
    if (mods & kControl) f |= kCGEventFlagMaskControl;
    if (mods & kOption) f |= kCGEventFlagMaskAlternate;
    if (mods & kCommand) f |= kCGEventFlagMaskCommand;
    return f;
  }

  inline CGMouseButton ToMouseButton_(int b) {
    return b == kRight ? kCGMouseButtonRight : (b == kMiddle ? kCGMouseButtonCenter : kCGMouseButtonLeft);
  }

  inline CGEventType DownEvent_(int b) {
    return b == kRight ? kCGEventRightMouseDown : b == kMiddle ? kCGEventOtherMouseDown : kCGEventLeftMouseDown;
  }

  inline CGEventType UpEvent_(int b) {
    return b == kRight ? kCGEventRightMouseUp : b == kMiddle ? kCGEventOtherMouseUp : kCGEventLeftMouseUp;
  }
#endif

  // ---------- Windows helpers ----------
#ifdef _WIN32
  inline WORD WinButtonDownFlag_(int b) {
    return b == kRight ? MOUSEEVENTF_RIGHTDOWN : (b == kMiddle ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN);
  }
  inline WORD WinButtonUpFlag_(int b) {
    return b == kRight ? MOUSEEVENTF_RIGHTUP : (b == kMiddle ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_LEFTUP);
  }
#endif

  // ---------- Linux helpers ----------
#ifdef __linux__
  // X11
#ifdef INPUT_BACKEND_X11
  Display *dpy_{nullptr};
  int screen_{0};
  inline int XButtonFromGeneric_(int b) { return b == kRight ? 3 : (b == kMiddle ? 2 : 1); }
  inline void PressModX11_(uint64_t mods, bool press) {
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

  // uinput
#ifdef INPUT_BACKEND_UINPUT
  int uinp_fd_{-1};
  inline void SendUinputSync_() {
    if (uinp_fd_ < 0) return;
    input_event ev{};
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinp_fd_, &ev, sizeof(ev));
  }
  inline void SendUinputRel_(unsigned short code, int value) {
    if (uinp_fd_ < 0) return;
    input_event ev{};
    ev.type = EV_REL;
    ev.code = code;
    ev.value = value;
    write(uinp_fd_, &ev, sizeof(ev));
  }
  inline void SendUinputKey_(int code, int press) {
    if (uinp_fd_ < 0) return;
    input_event ev{};
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = press ? 1 : 0;
    write(uinp_fd_, &ev, sizeof(ev));
  }
  inline int LinuxBtnCode_(int b) const { return b == kRight ? BTN_RIGHT : (b == kMiddle ? BTN_MIDDLE : BTN_LEFT); }
  inline int LinuxKeyShift_() const { return KEY_LEFTSHIFT; }
  inline int LinuxKeyCtrl_() const { return KEY_LEFTCTRL; }
  inline int LinuxKeyAlt_() const { return KEY_LEFTALT; }
  inline int LinuxKeySuper_() const { return KEY_LEFTMETA; }
  inline int LinuxAsciiToKeyCode_(unsigned char c) const {
    // Minimal ASCII mapping (extend as you need)
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
#endif  // INPUT_BACKEND_UINPUT

  // Wayland (wlroots virtual devices)
#ifdef INPUT_BACKEND_WAYLAND_WLR
  wl_display *wl_display_{nullptr};
  wl_registry *wl_registry_{nullptr};
  wl_seat *wl_seat_{nullptr};
  zwlr_virtual_pointer_manager_v1 *vp_mgr_{nullptr};
  zwp_virtual_keyboard_manager_v1 *vkbd_mgr_{nullptr};
  zwlr_virtual_pointer_v1 *vp_dev_{nullptr};
  zwp_virtual_keyboard_v1 *vkb_dev_{nullptr};

  static void RegistryGlobal_(void *data, wl_registry *reg, uint32_t name, const char *interface, uint32_t version) {
    auto *self = static_cast<SystemInput *>(data);
    if (strcmp(interface, wl_seat_interface.name) == 0) {
      self->wl_seat_ = static_cast<wl_seat *>(wl_registry_bind(reg, name, &wl_seat_interface, 1));
    } else if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
      self->vp_mgr_ = static_cast<zwlr_virtual_pointer_manager_v1 *>(
          wl_registry_bind(reg, name, &zwlr_virtual_pointer_manager_v1_interface, 1));
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
      self->vkbd_mgr_ = static_cast<zwp_virtual_keyboard_manager_v1 *>(
          wl_registry_bind(reg, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
    }
  }
  static void RegistryGlobalRemove_(void *, wl_registry *, uint32_t) {}

  inline const wl_registry_listener &RegistryListener() const {
    static const wl_registry_listener kListener = {RegistryGlobal_, RegistryGlobalRemove_};
    return kListener;
  }

  inline uint32_t LinuxBtnCode_(int b) const {
    // Wayland uses Linux input button codes.
    return b == kRight ? BTN_RIGHT : (b == kMiddle ? BTN_MIDDLE : BTN_LEFT);
  }
  inline int LinuxKeyShift_() const { return KEY_LEFTSHIFT; }
  inline int LinuxKeyCtrl_() const { return KEY_LEFTCTRL; }
  inline int LinuxKeyAlt_() const { return KEY_LEFTALT; }
  inline int LinuxKeySuper_() const { return KEY_LEFTMETA; }
  inline int LinuxAsciiToKeyCode_(unsigned char c) const {
    // Same minimal mapping as uinput.
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
#endif  // INPUT_BACKEND_WAYLAND_WLR
#endif  // __linux__
};
}  // namespace autoalg

#endif  // AUTOALG_INPUT_SIM_ALL_H_
