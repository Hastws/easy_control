// demo.cpp
// (c) 2025 AutoAlg (autoalg.com)
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "system_output.hpp"

static bool SaveRAW_RGBA(const std::string& path, const std::vector<uint8_t>& rgba) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
  return f.good();
}

#pragma pack(push, 1)
struct BMPFileHeader {
  uint16_t bfType{0x4D42};  // 'BM'
  uint32_t bfSize{};
  uint16_t bfReserved1{};
  uint16_t bfReserved2{};
  uint32_t bfOffBits{54};  // 14 (file) + 40 (info)
};
struct BMPInfoHeader {
  uint32_t biSize{40};
  int32_t biWidth{};
  int32_t biHeight{};  // 负值 = top-down（上到下）
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

// 将 RGBA 保存为 32-bit BMP（BGRA 顺序，top-down，不需逐行倒置）
static bool SaveBMP_BGRA_TopDown(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
  if (w <= 0 || h <= 0) return false;
  const size_t need = static_cast<size_t>(w) * h * 4;
  if (rgba.size() < need) return false;

  // 拷贝一份并把 RGBA -> BGRA
  std::vector<uint8_t> bgra(rgba);
  for (size_t i = 0; i < need; i += 4) std::swap(bgra[i + 0], bgra[i + 2]);

  BMPFileHeader fh;
  BMPInfoHeader ih;
  ih.biWidth = w;
  ih.biHeight = -h;  // top-down 布局
  ih.biSizeImage = static_cast<uint32_t>(need);
  fh.bfSize = fh.bfOffBits + ih.biSizeImage;

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
  f.write(reinterpret_cast<const char*>(&ih), sizeof(ih));
  f.write(reinterpret_cast<const char*>(bgra.data()), static_cast<std::streamsize>(bgra.size()));
  return f.good();
}

static void PrintUsage(const char* argv0) {
  std::printf(
      "Usage:\n"
      "  %s [display_index] [output_prefix]\n\n"
      "Args:\n"
      "  display_index  : Optional, default 0. Index in [0, GetDisplayCount()).\n"
      "  output_prefix  : Optional, default 'capture'. Files like capture_0.bmp.\n\n"
      "Notes:\n"
      "  Writes 32-bit BMP (top-down). If BMP fails, writes RGBA raw as fallback.\n",
      argv0);
}

int main(int argc, char** argv) {
  int display_index = 0;
  std::string prefix = "capture";

  if (argc >= 2) {
    std::string a1 = argv[1];
    if (a1 == "-h" || a1 == "--help") {
      PrintUsage(argv[0]);
      return 0;
    }
    try {
      display_index = std::stoi(a1);
    } catch (...) {
      std::fprintf(stderr, "Invalid display_index: %s\n", a1.c_str());
      PrintUsage(argv[0]);
      return 1;
    }
  }
  if (argc >= 3) {
    prefix = argv[2];
  }

  const int count = autoalg::SystemOutput::GetDisplayCount();
  std::printf("Display count reported: %d\n", count);
  if (count > 0 && (display_index < 0 || display_index >= count)) {
    std::fprintf(stderr, "display_index %d out of range [0, %d)\n", display_index, count);
    return 1;
  }

  // 如果平台返回 0（例如某些 Wayland/bridge 情况），仍尝试 index 0。
  const int target_index = (count > 0) ? display_index : 0;

  autoalg::ImageRGBA img;
  if (!autoalg::SystemOutput::CaptureScreenWithCursor(target_index, img)) {
    std::fprintf(stderr, "Capture failed (index=%d)\n", target_index);
    return 2;
  }

  std::printf("Captured %dx%d, %zu bytes RGBA\n", img.width, img.height, img.pixels.size());

  // 生成输出文件名
  std::ostringstream oss_bmp, oss_raw;
  oss_bmp << prefix << "_" << target_index << ".bmp";
  oss_raw << prefix << "_" << target_index << ".raw";

  // 先尝试 BMP（最方便查看）
  if (SaveBMP_BGRA_TopDown(oss_bmp.str(), img.width, img.height, img.pixels)) {
    std::printf("Wrote %s (32-bit BGRA, top-down)\n", oss_bmp.str().c_str());
    return 0;
  }

  // 兜底 RAW
  if (SaveRAW_RGBA(oss_raw.str(), img.pixels)) {
    std::printf("BMP failed; wrote %s (RGBA8 dump, stride=width*4)\n", oss_raw.str().c_str());
    return 0;
  }

  std::fprintf(stderr, "Failed to write image to disk.\n");
  return 3;
}
