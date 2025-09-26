// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#pragma once
#ifndef AUTOALG_SCREEN_CAPTURE_HPP
#define AUTOALG_SCREEN_CAPTURE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace autoalg {

struct ImageRGBA {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // RGBA8, size = w*h*4
};

class SystemOutput {
 public:
  // Capture the entire display with cursor blended.
  // displayIndex in [0, GetDisplayCount()).
  static bool CaptureScreenWithCursor(int displayIndex, ImageRGBA& outImage);

  // Number of displays.
  static int GetDisplayCount();

  // Human-readable display info.
  static std::string GetDisplayInfo(int displayIndex);
};

}  // namespace autoalg

// macOS bridge (.mm library you build separately)
#if defined(__APPLE__)
extern "C" {
struct MacImage {
  int width;
  int height;
  uint8_t* pixels;  // RGBA, malloc'ed
};
int MacCaptureScreenWithCursor(int displayIndex, MacImage* outImage);
void MacFreeImage(MacImage* img);
}
#endif

#endif  // AUTOALG_SCREEN_CAPTURE_HPP