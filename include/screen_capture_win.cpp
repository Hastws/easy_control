#ifdef _WIN32
#include "autoalg/screen_capture.hpp"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>

namespace {

struct MonInfo {
  HMONITOR hmon{};
  RECT rect{};
};

BOOL CALLBACK EnumMonProc(HMONITOR hMon, HDC, LPRECT r, LPARAM p) {
  auto* vec = reinterpret_cast<std::vector<MonInfo>*>(p);
  MonInfo mi; mi.hmon = hMon; mi.rect = *r;
  vec->push_back(mi);
  return TRUE;
}

bool CaptureRectToBitmapWithCursor(const RECT& rc, HBITMAP& outHbmp, int& w, int& h) {
  HDC hscr = GetDC(nullptr);
  if (!hscr) return false;
  HDC hdc = CreateCompatibleDC(hscr);
  if (!hdc) { ReleaseDC(nullptr, hscr); return false; }

  w = rc.right - rc.left;
  h = rc.bottom - rc.top;

  outHbmp = CreateCompatibleBitmap(hscr, w, h);
  if (!outHbmp) { DeleteDC(hdc); ReleaseDC(nullptr, hscr); return false; }

  HGDIOBJ old = SelectObject(hdc, outHbmp);
  if (!BitBlt(hdc, 0, 0, w, h, hscr, rc.left, rc.top, SRCCOPY | CAPTUREBLT)) {
    SelectObject(hdc, old); DeleteObject(outHbmp); outHbmp = nullptr;
    DeleteDC(hdc); ReleaseDC(nullptr, hscr); return false;
  }

  // Draw cursor
  CURSORINFO ci{ sizeof(CURSORINFO) };
  if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
    POINT p = ci.ptScreenPos;
    int cx = p.x - rc.left;
    int cy = p.y - rc.top;
    DrawIconEx(hdc, cx, cy, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
  }

  SelectObject(hdc, old);
  DeleteDC(hdc);
  ReleaseDC(nullptr, hscr);
  return true;
}

} // namespace

namespace autoalg {

bool ScreenCapture::CaptureScreenWithCursor(int displayIndex, ImageRGBA& out) {
  std::vector<MonInfo> mons;
  EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(&mons));
  if (mons.empty()) return false;
  if (displayIndex < 0 || displayIndex >= (int)mons.size()) return false;

  const RECT rc = mons[(size_t)displayIndex].rect;

  HBITMAP hbmp = nullptr; int w=0, h=0;
  if (!CaptureRectToBitmapWithCursor(rc, hbmp, w, h)) return false;

  HDC hscr = GetDC(nullptr);
  BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h; // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  out.width = w; out.height = h;
  out.pixels.resize((size_t)w*h*4);

  if (!GetDIBits(hscr, hbmp, 0, h, out.pixels.data(), &bi, DIB_RGB_COLORS)) {
    DeleteObject(hbmp); ReleaseDC(nullptr, hscr); out = {}; return false;
  }
  // BGRA->RGBA
  for (size_t i=0,n=out.pixels.size(); i<n; i+=4) std::swap(out.pixels[i], out.pixels[i+2]);

  DeleteObject(hbmp);
  ReleaseDC(nullptr, hscr);
  return true;
}

int ScreenCapture::GetDisplayCount() {
  std::vector<MonInfo> mons;
  EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(&mons));
  return (int)mons.size();
}

std::string ScreenCapture::GetDisplayInfo(int index) {
  return "Windows Monitor " + std::to_string(index);
}

} // namespace autoalg
#endif