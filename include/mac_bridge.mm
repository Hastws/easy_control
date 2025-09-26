// (c) 2025 AutoAlg (autoalg.com).
// Author: Chunzhi Qu.
// SPDX-License-Identifier: MIT.

#import "mac_bridge.h"

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ApplicationServices/ApplicationServices.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <vector>
#include <cstdlib>
#include <cmath>

static SCDisplay* sc_display_for_index(NSInteger index) {
  __block SCShareableContent* content = nil;
  __block NSError* err = nil;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);

  [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent * _Nullable c,
                                                                 NSError * _Nullable e) {
    content = c; err = e; dispatch_semaphore_signal(sem);
  }];

  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  if (err || content.displays.count == 0) return nil;

  if (index < 0 || index >= (NSInteger)content.displays.count) {
    return content.displays.firstObject;
  }
  return content.displays[(NSUInteger)index];
}

static int cgimage_to_rgba(CGImageRef src, MacImage* outImage) {
  if (!src || !outImage) return 0;
  const size_t w = CGImageGetWidth(src);
  const size_t h = CGImageGetHeight(src);
  const size_t stride = w * 4;

  uint8_t* buf = (uint8_t*)std::malloc(w * h * 4);
  if (!buf) return 0;

  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(buf, w, h, 8, stride, cs, kCGImageAlphaPremultipliedLast);
  if (!ctx) { CGColorSpaceRelease(cs); std::free(buf); return 0; }

  CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), src);

  CGContextRelease(ctx);
  CGColorSpaceRelease(cs);

  outImage->width  = (int)w;
  outImage->height = (int)h;
  outImage->pixels = buf;
  return 1;
}

int MacCaptureScreenWithCursor(int displayIndex, MacImage* outImage) {
  if (!outImage) return 0;

  SCDisplay* disp = sc_display_for_index(displayIndex);
  if (!disp) return 0;

  SCContentFilter* filter = nil;
  if ([SCContentFilter instancesRespondToSelector:@selector(initWithDisplay:excludingWindows:)]) {
    filter = [[SCContentFilter alloc] initWithDisplay:disp excludingWindows:@[]];
  } else if ([SCContentFilter instancesRespondToSelector:@selector(initWithDisplay:includingApplications:exceptingWindows:)]) {
    filter = [[SCContentFilter alloc] initWithDisplay:disp includingApplications:@[] exceptingWindows:@[]];
  } else {
    return 0;
  }

  CGRect contentRect = filter.contentRect;
  const CGFloat scale = filter.pointPixelScale;

  SCStreamConfiguration* cfg = [SCStreamConfiguration new];
  cfg.showsCursor = YES;
  if ([cfg respondsToSelector:@selector(setCaptureResolution:)]) {
    cfg.captureResolution = SCCaptureResolutionBest;
  }
  cfg.width  = (int)llround(CGRectGetWidth(contentRect)  * scale);
  cfg.height = (int)llround(CGRectGetHeight(contentRect) * scale);

  __block CGImageRef grabbed = nil;
  __block NSError* err = nil;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);

  if ([SCScreenshotManager respondsToSelector:@selector(captureImageWithFilter:configuration:completionHandler:)]) {
    [SCScreenshotManager captureImageWithFilter:filter
                                   configuration:cfg
                               completionHandler:^(CGImageRef _Nullable image, NSError * _Nullable e) {
      if (image) CFRetain(image);
      grabbed = image; err = e;
      dispatch_semaphore_signal(sem);
    }];
  } else {
    return 0;
  }

  dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
  if (err || !grabbed) { if (grabbed) CFRelease(grabbed); return 0; }

  int ok = cgimage_to_rgba(grabbed, outImage);
  CFRelease(grabbed);
  return ok;
}

void MacFreeImage(MacImage* img) {
  if (img && img->pixels) { std::free(img->pixels); img->pixels = NULL; }
}
