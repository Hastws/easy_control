// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#ifndef EASY_CONTROL_INCLUDE_MAC_BRIDGE_H
#define EASY_CONTROL_INCLUDE_MAC_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int width;
  int height;
  uint8_t *pixels;  // RGBA, malloc'ed
} MacImage;

int MacCaptureScreenWithCursor(int displayIndex, MacImage *outImage);

void MacFreeImage(MacImage *img);

#ifdef __cplusplus
}
#endif

#endif
