// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.
//
// Integration test: exercise SystemInput & SystemOutput end-to-end.
// After each action, capture a screenshot with cursor overlay to visually verify.
//
// Build (Linux X11 example):
//   g++ -std=c++17 main.cpp -o test_app -lX11 -lXtst
//
// Usage:
//   ./test_app [display_index] [output_prefix] [delay_ms_between_steps]
//   e.g. ./test_app 0 verify 500

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "system_input.hpp"
#include "system_output.hpp"

using namespace autoalg;

// ---------- Simple BMP writer (BGRA, 32-bit, top-down) ----------
#pragma pack(push, 1)
struct BMPFileHeader {
  uint16_t bfType{0x4D42};  // 'BM'
  uint32_t bfSize{};
  uint16_t bfReserved1{};
  uint16_t bfReserved2{};
  uint32_t bfOffBits{54};  // 14 + 40
};
struct BMPInfoHeader {
  uint32_t biSize{40};
  int32_t biWidth{};
  int32_t biHeight{};  // negative => top-down
  uint16_t biPlanes{1};
  uint16_t biBitCount{32};    // 32-bit
  uint32_t biCompression{0};  // BI_RGB
  uint32_t biSizeImage{};
  int32_t biXPelsPerMeter{2835};
  int32_t biYPelsPerMeter{2835};
  uint32_t biClrUsed{};
  uint32_t biClrImportant{};
};
#pragma pack(pop)

static bool SaveBMP_BGRA_TopDown(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
  if (w <= 0 || h <= 0) return false;
  const size_t need = static_cast<size_t>(w) * h * 4;
  if (rgba.size() < need) return false;

  // RGBA -> BGRA
  std::vector<uint8_t> bgra(rgba);
  for (size_t i = 0; i < need; i += 4) std::swap(bgra[i + 0], bgra[i + 2]);

  BMPFileHeader fh;
  BMPInfoHeader ih;
  ih.biWidth = w;
  ih.biHeight = -h;  // top-down
  ih.biSizeImage = static_cast<uint32_t>(need);
  fh.bfSize = fh.bfOffBits + ih.biSizeImage;

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  f.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
  f.write(reinterpret_cast<const char*>(bgra.data()), (std::streamsize)bgra.size());
  return f.good();
}

// Sanitize filename segment (keep [A-Za-z0-9_-.])
static std::string Sanitize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')
      out.push_back(c);
    else if (std::isspace((unsigned char)c))
      out.push_back('_');
    else
      out.push_back('_');
  }
  return out;
}

static bool CaptureStep(int display_index, const std::string& prefix, int step_no, const std::string& label,
                        const SystemInput& in) {
  ImageRGBA img;
  if (!SystemOutput::CaptureScreenWithCursor(display_index, img)) {
    std::fprintf(stderr, "[%02d] Capture failed: %s\n", step_no, label.c_str());
    return false;
  }
  std::ostringstream oss;
  oss << prefix << "_" << step_no << "_" << Sanitize(label) << ".bmp";
  if (!SaveBMP_BGRA_TopDown(oss.str(), img.width, img.height, img.pixels)) {
    std::fprintf(stderr, "[%02d] Save BMP failed: %s\n", step_no, oss.str().c_str());
    return false;
  }

  int cx_px = 0, cy_px = 0;
  // query pixel cursor (best-effort on Wayland/uinput)
  const_cast<SystemInput&>(in).GetCursorPixel(cx_px, cy_px);
  std::printf("[%02d] %-28s => captured %dx%d -> %s ; cursor(px)=(%d,%d)\n", step_no, label.c_str(), img.width, img.height,
              oss.str().c_str(), cx_px, cy_px);
  return true;
}

static void PauseMs(int ms) {
  if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main(int argc, char** argv) {
  int display_index = 0;
  std::string prefix = "ec_test";
  int delay_ms = 500;

  if (argc >= 2) {
    std::string a1 = argv[1];
    if (a1 == "-h" || a1 == "--help") {
      std::printf("Usage: %s [display_index] [output_prefix] [delay_ms]\n", argv[0]);
      return 0;
    }
    try {
      display_index = std::stoi(a1);
    } catch (...) {
      std::fprintf(stderr, "Bad display_index\n");
      return 1;
    }
  }
  if (argc >= 3) prefix = argv[2];
  if (argc >= 4) {
    try {
      delay_ms = std::stoi(argv[3]);
    } catch (...) {
      std::fprintf(stderr, "Bad delay_ms\n");
      return 1;
    }
  }

  std::printf("== Easy Control: end-to-end input/output test ==\n");
  std::printf("Display index: %d, prefix: %s, delay: %d ms\n", display_index, prefix.c_str(), delay_ms);

  // --- System info (from common.hpp helpers) ---
  std::cout << "pid=" << ProcessId() << ", hw=" << NumHWThreads() << "\n";
  std::cout << "exe=" << ExecutablePath().string() << "\n";
  std::cout << "home=" << HomeDir().string() << "\n";
  std::cout << "tmp=" << TempDir().string() << "\n";

  // --- Create input/output handles ---
  SystemInput in;
  in.SyncCursorFromSystem();

  // Ensure display count is readable (even if returns 0 on some backends, we still try index 0)
  const int disp_count = SystemOutput::GetDisplayCount();
  std::printf("SystemOutput::GetDisplayCount() => %d\n", disp_count);

  // Step counter
  int step = 0;

  // -------- Baseline capture --------
  CaptureStep(display_index, prefix, ++step, "baseline", in);
  PauseMs(delay_ms);

  // -------- Pixel size & pixel cursor --------
  {
    int w_px = 0, h_px = 0;
    in.GetPrimaryDisplayPixelSize(w_px, h_px);
    int cx = 0, cy = 0;
    in.GetCursorPixel(cx, cy);
    std::printf("Primary display(px)=%dx%d; cursor(px)=(%d,%d)\n", w_px, h_px, cx, cy);
  }
  CaptureStep(display_index, prefix, ++step, "after_query_sizes", in);
  PauseMs(delay_ms);

  // -------- Move to center using pixel-accurate API --------
  {
    int w_px = 0, h_px = 0;
    in.GetPrimaryDisplayPixelSize(w_px, h_px);
    in.MouseMoveToPixels(w_px / 2, h_px / 2);
  }
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "move_to_center_pixels", in);

  // -------- Relative move (logical API) --------
  in.MouseMoveRelative(+120, -80);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "relative_move_120_-80", in);

  // -------- Single click (left) --------
  in.MouseClick(SystemInput::kLeft);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "click_left", in);

  // -------- Double click (right) --------
  in.MouseDoubleClick(SystemInput::kRight);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "double_click_right", in);

  // -------- Drag by (left) --------
  in.MouseDragBy(160, 110, SystemInput::kLeft);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "drag_by_160_110_left", in);

  // -------- Scroll (lines) --------
  in.ScrollLines(0, -3);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "scroll_lines_down_3", in);

  // -------- Scroll (pixels or fine) --------
  in.ScrollPixels(+10, 0);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "scroll_pixels_right_10", in);

  // -------- Type some text --------
  in.TypeUTF8("Hello, AutoAlg! 你好～\n");
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "type_utf8", in);

  // -------- Select all via "Command+A" (mac) / "Super+A"(linux) / "Win+A"(win) semantics --------
  // 我们示例统一用 kCommand 修饰（mac=Cmd、win=Win、linux=Super）
  {
    int key_a = in.CharToKeyCode('a');
    if (key_a >= 0) {
      in.KeyboardClickWithMods(key_a, SystemInput::kCommand);
    }
  }
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "keychord_cmd_a", in);

  // -------- Copy via "Command+C" --------
  {
    int key_c = in.CharToKeyCode('c');
    if (key_c >= 0) {
      in.KeyboardClickWithMods(key_c, SystemInput::kCommand);
    }
  }
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "keychord_cmd_c", in);

  // -------- Move to a fixed logical position near top-left, then click middle --------
  in.MouseMoveTo(40, 40);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "move_to_40_40_logical", in);

  in.MouseClick(SystemInput::kMiddle);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "click_middle", in);

  // -------- Press and hold left for 0.3s, then release --------
  in.MouseHold(SystemInput::kLeft, 0.3);
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "mouse_hold_left_300ms", in);

  // -------- Drag to absolute pixel coordinate (quarter of screen) --------
  {
    int w_px = 0, h_px = 0;
    in.GetPrimaryDisplayPixelSize(w_px, h_px);
    // Convert to px, then move via pixels helper by building our own small steps:
    in.MouseMoveToPixels(w_px / 4, h_px / 4);
  }
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "move_to_quarter_pixels", in);

  // -------- Final snapshot with all queried coordinates printed again --------
  {
    int w_px = 0, h_px = 0;
    in.GetPrimaryDisplayPixelSize(w_px, h_px);
    int cx = 0, cy = 0;
    in.GetCursorPixel(cx, cy);
    std::printf("Final: display(px)=%dx%d ; cursor(px)=(%d,%d)\n", w_px, h_px, cx, cy);
  }
  PauseMs(delay_ms);
  CaptureStep(display_index, prefix, ++step, "final", in);

  std::printf("== Done. %d steps executed. Screenshots under prefix '%s_*.bmp'. ==\n", step, prefix.c_str());
  return 0;
}
