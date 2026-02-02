// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#ifdef __APPLE__

#include <string>

#include "system_output.hpp"

namespace autoalg {
bool SystemOutput::CaptureScreenWithCursor(int display_index, ImageRGBA &out_image) {
  MacImage m{};
  if (!MacCaptureScreenWithCursor(display_index, &m)) return false;
  out_image.width = m.width;
  out_image.height = m.height;
  out_image.pixels.assign(m.pixels, m.pixels + static_cast<size_t>(m.width) * m.height * 4);
  MacFreeImage(&m);
  return true;
}

int SystemOutput::GetDisplayCount() {
  uint32_t count = 0;
  if (CGGetActiveDisplayList(0, nullptr, &count) == kCGErrorSuccess && count > 0) {
    return static_cast<int>(count);
  }
  return 1;  // Fallback to 1 if query fails
}

std::string SystemOutput::GetDisplayInfo(const int display_index) { return "macOS Display " + std::to_string(display_index); }
}  // namespace autoalg
#endif
