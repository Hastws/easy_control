// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#ifndef EASY_CONTROL_INCLUDE_SYSTEM_OUTPUT_HPP
#define EASY_CONTROL_INCLUDE_SYSTEM_OUTPUT_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "macro.h"

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
  static bool CaptureScreenWithCursor(int display_index, ImageRGBA& out_image);

  // Number of displays.
  static int GetDisplayCount();

  // Human-readable display info.
  static std::string GetDisplayInfo(int display_index);
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

int MacCaptureScreenWithCursor(int display_index, MacImage* out_image);

void MacFreeImage(MacImage* img);
}
#endif

#endif  // EASY_CONTROL_INCLUDE_SYSTEM_OUTPUT_HPP