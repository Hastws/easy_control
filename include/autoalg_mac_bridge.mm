// (c) 2025 AutoAlg
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ApplicationServices/ApplicationServices.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>   // ScreenCaptureKit
#import "autoalg_mac_bridge.h"

#include <vector>
#include <cstdlib>
#include <cmath>

// 使用老而稳的获取分享内容 API（CLT SDK 可用）
static SCDisplay* sc_display_for_index(NSInteger index) {
  __block SCShareableContent* content = nil;
  __block NSError* err = nil;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);

  // 统一走旧 API：+getShareableContentWithCompletionHandler:
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

// 将 CGImage 画进 RGBA(8, premultiplied last) 缓冲
static int cgimage_to_rgba(CGImageRef src, AutoAlg_MacImage* outImage) {
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

int AutoAlg_MacCaptureScreenWithCursor(int displayIndex, AutoAlg_MacImage* outImage) {
  if (!outImage) return 0;

  SCDisplay* disp = sc_display_for_index(displayIndex);
  if (!disp) return 0;

  // 整屏过滤器
  SCContentFilter* filter = nil;
  if ([SCContentFilter instancesRespondToSelector:@selector(initWithDisplay:excludingWindows:)]) {
    filter = [[SCContentFilter alloc] initWithDisplay:disp excludingWindows:@[]];
  } else if ([SCContentFilter instancesRespondToSelector:@selector(initWithDisplay:includingApplications:exceptingWindows:)]) {
    filter = [[SCContentFilter alloc] initWithDisplay:disp includingApplications:@[] exceptingWindows:@[]];
  } else {
    return 0;
  }

  // 输出配置：分辨率 = contentRect * pointPixelScale
  CGRect contentRect = filter.contentRect;
  const CGFloat scale = filter.pointPixelScale;

  SCStreamConfiguration* cfg = [SCStreamConfiguration new];
  cfg.showsCursor = YES;  // 把鼠标光标画进去（ScreenCaptureKit 支持） :contentReference[oaicite:1]{index=1}
  if ([cfg respondsToSelector:@selector(setCaptureResolution:)]) {
    cfg.captureResolution = SCCaptureResolutionBest;
  }
  cfg.width  = (int)llround(CGRectGetWidth(contentRect)  * scale);
  cfg.height = (int)llround(CGRectGetHeight(contentRect) * scale);

  // 使用 SCScreenshotManager 抓单帧
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

void AutoAlg_MacFreeImage(AutoAlg_MacImage* img) {
  if (img && img->pixels) { std::free(img->pixels); img->pixels = NULL; }
}
