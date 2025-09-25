// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#ifdef __APPLE__

#include <string>

#include "screen_capture.hpp"

extern "C" {
int MacCaptureScreenWithCursor(int displayIndex, MacImage *outImage);

void MacFreeImage(MacImage *img);
}

namespace autoalg {
bool SystemOutput::CaptureScreenWithCursor(int displayIndex, ImageRGBA &outImage) {
  MacImage m{};
  if (!MacCaptureScreenWithCursor(displayIndex, &m)) return false;
  outImage.width = m.width;
  outImage.height = m.height;
  outImage.pixels.assign(m.pixels, m.pixels + (size_t)m.width * m.height * 4);
  MacFreeImage(&m);
  return true;
}

int SystemOutput::GetDisplayCount() {
  // For simplicity return 1; actual multi-display supported in bridge by index.
  return 1;
}

std::string SystemOutput::GetDisplayInfo(int index) { return "macOS Display " + std::to_string(index); }
}  // namespace autoalg
#endif
