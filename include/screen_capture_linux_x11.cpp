// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#if defined(__linux__) && !defined(AUTOALG_USE_WAYLAND_PORTAL)
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>

#include <algorithm>
#include <string>
#include <vector>

#include "screen_capture.hpp"

namespace {
struct Monitor {
  int x, y, w, h;
};

inline int mask_shift(unsigned long mask) {
  int s = 0;
  while (mask && !(mask & 1)) {
    mask >>= 1;
    ++s;
  }
  return s;
}

inline uint8_t extract_chan(unsigned long pixel, unsigned long mask) {
  if (!mask) return 0;
  int s = mask_shift(mask);
  unsigned long v = (pixel & mask) >> s;
  int bits = 0;
  unsigned long m = mask >> s;
  while (m) {
    ++bits;
    m >>= 1;
  }
  if (bits >= 8) return uint8_t(v & 0xFF);
  if (bits == 0) return 0;
  return uint8_t((v * 255U) / ((1U << bits) - 1U));
}

inline void alpha_blend(uint8_t *dst, const uint8_t src[4]) {
  const float a = src[3] / 255.0f;
  dst[0] = uint8_t(src[0] * a + dst[0] * (1 - a));
  dst[1] = uint8_t(src[1] * a + dst[1] * (1 - a));
  dst[2] = uint8_t(src[2] * a + dst[2] * (1 - a));
  dst[3] = 255;
}

std::vector<Monitor> get_monitors(Display *dpy, Window root) {
  std::vector<Monitor> out;
  XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
  if (!res) return out;
  for (int i = 0; i < res->ncrtc; ++i) {
    XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, res->crtcs[i]);
    if (ci && ci->noutput > 0 && ci->mode != None && ci->width > 0 && ci->height > 0) {
      out.push_back({(int)ci->x, (int)ci->y, (int)ci->width, (int)ci->height});
    }
    if (ci) XRRFreeCrtcInfo(ci);
  }
  XRRFreeScreenResources(res);
  if (out.empty()) {
    Screen *s = DefaultScreenOfDisplay(dpy);
    out.push_back({0, 0, s->width, s->height});
  }
  return out;
}
}  // namespace

namespace autoalg {
bool ScreenCapture::CaptureScreenWithCursor(int displayIndex, ImageRGBA &out) {
  Display *dpy = XOpenDisplay(nullptr);
  if (!dpy) return false;
  int scrIdx = DefaultScreen(dpy);
  Window root = RootWindow(dpy, scrIdx);

  auto mons = get_monitors(dpy, root);
  if (displayIndex < 0 || displayIndex >= (int)mons.size()) {
    XCloseDisplay(dpy);
    return false;
  }
  auto m = mons[(size_t)displayIndex];

  XImage *img = XGetImage(dpy, root, m.x, m.y, (unsigned)m.w, (unsigned)m.h, AllPlanes, ZPixmap);
  if (!img) {
    XCloseDisplay(dpy);
    return false;
  }

  out.width = m.w;
  out.height = m.h;
  out.pixels.assign((size_t)m.w * m.h * 4, 255);

  for (int y = 0; y < m.h; ++y) {
    for (int x = 0; x < m.w; ++x) {
      unsigned long px = XGetPixel(img, x, y);
      size_t di = ((size_t)y * m.w + x) * 4;
      out.pixels[di + 0] = extract_chan(px, img->red_mask);
      out.pixels[di + 1] = extract_chan(px, img->green_mask);
      out.pixels[di + 2] = extract_chan(px, img->blue_mask);
      out.pixels[di + 3] = 255;
    }
  }

  XFixesCursorImage *cur = XFixesGetCursorImage(dpy);
  if (cur) {
    int cx = cur->x - cur->xhot - m.x;
    int cy = cur->y - cur->yhot - m.y;
    for (int j = 0; j < (int)cur->height; ++j) {
      int py = cy + j;
      if (py < 0 || py >= m.h) continue;
      for (int i = 0; i < (int)cur->width; ++i) {
        int px = cx + i;
        if (px < 0 || px >= m.w) continue;
        uint32_t argb = cur->pixels[j * cur->width + i];
        uint8_t a = (argb >> 24) & 0xFF;
        if (!a) continue;
        uint8_t src[4] = {(uint8_t)((argb >> 16) & 0xFF), (uint8_t)((argb >> 8) & 0xFF), (uint8_t)((argb >> 0) & 0xFF), a};
        size_t di = ((size_t)py * m.w + px) * 4;
        alpha_blend(&out.pixels[di], src);
      }
    }
    XFree(cur);
  }

  XDestroyImage(img);
  XCloseDisplay(dpy);
  return true;
}

int ScreenCapture::GetDisplayCount() {
  Display *dpy = XOpenDisplay(nullptr);
  if (!dpy) return 0;
  Window root = RootWindow(dpy, DefaultScreen(dpy));
  auto mons = get_monitors(dpy, root);
  XCloseDisplay(dpy);
  return (int)mons.size();
}

std::string ScreenCapture::GetDisplayInfo(int index) { return "Linux X11 Monitor " + std::to_string(index); }
}  // namespace autoalg
#endif
