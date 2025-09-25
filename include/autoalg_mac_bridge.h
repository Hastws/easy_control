// (c) 2025 AutoAlg
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int width;
  int height;
  uint8_t* pixels; // RGBA, malloc'ed
} AutoAlg_MacImage;

int AutoAlg_MacCaptureScreenWithCursor(int displayIndex, AutoAlg_MacImage* outImage);
void AutoAlg_MacFreeImage(AutoAlg_MacImage* img);

#ifdef __cplusplus
}
#endif