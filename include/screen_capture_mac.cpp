#ifdef __APPLE__

#include "screen_capture.hpp"

#include <string>
extern "C" {
  int AutoAlg_MacCaptureScreenWithCursor(int displayIndex, AutoAlg_MacImage* outImage);
  void AutoAlg_MacFreeImage(AutoAlg_MacImage* img);
}

namespace autoalg {

bool ScreenCapture::CaptureScreenWithCursor(int displayIndex, ImageRGBA& outImage) {
  AutoAlg_MacImage m{};
  if (!AutoAlg_MacCaptureScreenWithCursor(displayIndex, &m)) return false;
  outImage.width = m.width;
  outImage.height = m.height;
  outImage.pixels.assign(m.pixels, m.pixels + (size_t)m.width * m.height * 4);
  AutoAlg_MacFreeImage(&m);
  return true;
}

int ScreenCapture::GetDisplayCount() {
  // For simplicity return 1; actual multi-display supported in bridge by index.
  return 1;
}

std::string ScreenCapture::GetDisplayInfo(int index) {
  return "macOS Display " + std::to_string(index);
}

} // namespace autoalg
#endif