// Procedural unlock toasts for the Metal present. Sizes in PlaceToast are
// the 1280x720 spec scaled by the view height. The Apple mark is U+F8FF
// from the system font when that glyph exists, otherwise a small bitten
// circle. Nothing here is a console maker's logo artwork.

#import <AudioToolbox/AudioQueue.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "achievement_overlay.h"
#include "achievements.h"

namespace {

using gtaiv::achievements::Style;
using gtaiv::achievements::ToastFrame;

void PlayChime(const gtaiv::achievements::Pcm& pcm) {
  if (pcm.samples.empty()) return;
  AudioStreamBasicDescription fmt{};
  fmt.mSampleRate = pcm.sample_rate;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  fmt.mBitsPerChannel = 16;
  fmt.mChannelsPerFrame = 1;
  fmt.mFramesPerPacket = 1;
  fmt.mBytesPerFrame = 2;
  fmt.mBytesPerPacket = 2;
  AudioQueueRef queue = nullptr;
  if (AudioQueueNewOutput(&fmt, [](void*, AudioQueueRef q, AudioQueueBufferRef buf) {
        AudioQueueFreeBuffer(q, buf);
        AudioQueueStop(q, false);
        AudioQueueDispose(q, false);
      }, nullptr, nullptr, nullptr, 0, &queue))
    return;
  AudioQueueBufferRef buffer = nullptr;
  const UInt32 bytes = UInt32(pcm.samples.size() * sizeof(int16_t));
  if (AudioQueueAllocateBuffer(queue, bytes, &buffer)) {
    AudioQueueDispose(queue, true);
    return;
  }
  std::memcpy(buffer->mAudioData, pcm.samples.data(), bytes);
  buffer->mAudioDataByteSize = bytes;
  AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  AudioQueueSetParameter(queue, kAudioQueueParam_Volume, 0.85f);
  if (AudioQueueStart(queue, nullptr)) AudioQueueDispose(queue, true);
}

CTFontRef FontNamed(bool bold, CGFloat size) {
  const CFStringRef names[] = {bold ? CFSTR("SF Pro Display Bold") : CFSTR("SF Pro Display"),
                               bold ? CFSTR("SF Pro Text Bold") : CFSTR("SF Pro Text"),
                               bold ? CFSTR(".AppleSystemUIFontBold") : CFSTR(".AppleSystemUIFont"),
                               bold ? CFSTR("Helvetica-Bold") : CFSTR("Helvetica")};
  for (CFStringRef name : names) {
    CTFontRef font = CTFontCreateWithName(name, size, nullptr);
    if (!font) continue;
    CFStringRef got = CTFontCopyPostScriptName(font);
    const bool fallback = got && CFStringCompare(got, CFSTR("Helvetica"), 0) == 0 && name != CFSTR("Helvetica") &&
                          name != CFSTR("Helvetica-Bold");
    if (got) CFRelease(got);
    if (!fallback) return font;
    CFRelease(font);
  }
  return CTFontCreateUIFontForLanguage(bold ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem, size, nullptr);
}

void DrawText(CGContextRef ctx, const std::string& utf8, bool bold, CGFloat size, CGFloat x, CGFloat baseline,
              CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  if (utf8.empty() || !ctx) return;
  CTFontRef font = FontNamed(bold, size);
  if (!font) return;
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  const CGFloat comps[] = {r, g, b, a};
  CGColorRef color = CGColorCreate(space, comps);
  CFStringRef text = CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.data()),
                                             CFIndex(utf8.size()), kCFStringEncodingUTF8, false);
  const void* keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
  const void* vals[] = {font, color};
  CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks);
  if (text && attrs) {
    CFAttributedStringRef str = CFAttributedStringCreate(kCFAllocatorDefault, text, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(str);
    CGContextSetTextPosition(ctx, x, baseline);
    CTLineDraw(line, ctx);
    CFRelease(line);
    CFRelease(str);
  }
  if (attrs) CFRelease(attrs);
  if (text) CFRelease(text);
  CGColorRelease(color);
  CGColorSpaceRelease(space);
  CFRelease(font);
}

bool DrawAppleGlyph(CGContextRef ctx, CGFloat cx, CGFloat cy, CGFloat size, CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  CTFontRef font = FontNamed(false, size);
  if (!font) return false;
  UniChar ch = 0xF8FF;
  CGGlyph glyph = 0;
  const bool ok = CTFontGetGlyphsForCharacters(font, &ch, &glyph, 1) && glyph != 0;
  if (!ok) {
    CFRelease(font);
    return false;
  }
  CGSize adv{};
  CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &glyph, &adv, 1);
  CGRect box = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &glyph, nullptr, 1);
  CGPoint p{cx - adv.width * 0.5f - box.origin.x * 0.0f, cy - (box.origin.y + box.size.height * 0.5f)};
  p.x = cx - (box.origin.x + box.size.width * 0.5f);
  CGContextSetRGBFillColor(ctx, r, g, b, a);
  CTFontDrawGlyphs(font, &glyph, &p, 1, ctx);
  CFRelease(font);
  return true;
}

void DrawAppleFallback(CGContextRef ctx, CGFloat cx, CGFloat cy, CGFloat radius, CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  CGContextSaveGState(ctx);
  CGContextSetRGBFillColor(ctx, r, g, b, a);
  CGContextAddEllipseInRect(ctx, CGRectMake(cx - radius, cy - radius, radius * 2, radius * 2));
  CGContextAddEllipseInRect(ctx, CGRectMake(cx + radius * 0.15f, cy - radius * 0.15f, radius * 0.85f, radius * 0.85f));
  CGContextEOFillPath(ctx);
  CGContextAddEllipseInRect(ctx, CGRectMake(cx - radius * 0.15f, cy + radius * 0.72f, radius * 0.55f, radius * 0.38f));
  CGContextFillPath(ctx);
  CGContextRestoreGState(ctx);
}

void DrawApple(CGContextRef ctx, CGFloat cx, CGFloat cy, CGFloat size, CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  if (!DrawAppleGlyph(ctx, cx, cy, size, r, g, b, a)) DrawAppleFallback(ctx, cx, cy, size * 0.42f, r, g, b, a);
}

void FillVertical(CGContextRef ctx, CGRect rect, CGFloat radius, const CGFloat top[4], const CGFloat bot[4]) {
  CGContextSaveGState(ctx);
  CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, nullptr);
  CGContextAddPath(ctx, path);
  CGContextClip(ctx);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  const CGFloat comps[] = {top[0], top[1], top[2], top[3], bot[0], bot[1], bot[2], bot[3]};
  CGGradientRef grad = CGGradientCreateWithColorComponents(space, comps, nullptr, 2);
  CGContextDrawLinearGradient(ctx, grad, CGPointMake(CGRectGetMidX(rect), CGRectGetMaxY(rect)),
                              CGPointMake(CGRectGetMidX(rect), CGRectGetMinY(rect)), 0);
  CGGradientRelease(grad);
  CGColorSpaceRelease(space);
  CGPathRelease(path);
  CGContextRestoreGState(ctx);
}

void StrokeRound(CGContextRef ctx, CGRect rect, CGFloat radius, CGFloat width, CGFloat r, CGFloat g, CGFloat b, CGFloat a) {
  CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, nullptr);
  CGContextAddPath(ctx, path);
  CGContextSetStrokeColor(ctx, (CGFloat[]){r, g, b, a});
  CGContextSetLineWidth(ctx, width);
  CGContextStrokePath(ctx);
  CGPathRelease(path);
}

void DrawOrb(CGContextRef ctx, CGFloat cx, CGFloat cy, CGFloat radius, Style style, CGFloat fade) {
  const CGFloat pi = std::acos(-1.f);
  if (style == Style::XboxC) {
    CGContextSetRGBFillColor(ctx, 0x10 / 255.f, 0x7C / 255.f, 0x10 / 255.f, fade);
    CGContextFillEllipseInRect(ctx, CGRectMake(cx - radius, cy - radius, radius * 2, radius * 2));
    DrawApple(ctx, cx, cy, radius * 1.15f, 1, 1, 1, fade);
    return;
  }
  if (style == Style::XboxD) {
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 0.16f * fade);
    CGContextFillEllipseInRect(ctx, CGRectMake(cx - radius, cy - radius, radius * 2, radius * 2));
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, 0.45f * fade);
    CGContextSetLineWidth(ctx, std::max(1.0, double(radius * 0.06)));
    CGContextStrokeEllipseInRect(ctx, CGRectMake(cx - radius, cy - radius, radius * 2, radius * 2));
    DrawApple(ctx, cx, cy, radius * 1.05f, 1, 1, 1, fade);
    return;
  }
  if (style == Style::XboxE) {
    CGContextSetRGBStrokeColor(ctx, 0xD4 / 255.f, 0xA5 / 255.f, 0x37 / 255.f, fade);
    CGContextSetLineWidth(ctx, radius * 0.14f);
    CGContextStrokeEllipseInRect(ctx, CGRectMake(cx - radius * 0.82f, cy - radius * 0.82f, radius * 1.64f, radius * 1.64f));
    const CGFloat inner = radius * 0.62f;
    const CGFloat top[] = {0.45f, 0.36f, 0.22f, fade};
    const CGFloat bot[] = {0.18f, 0.14f, 0.10f, fade};
    FillVertical(ctx, CGRectMake(cx - inner, cy - inner, inner * 2, inner * 2), inner, top, bot);
    DrawApple(ctx, cx, cy, inner * 1.15f, 0xF3 / 255.f, 0xD2 / 255.f, 0x7A / 255.f, fade);
    return;
  }
  // A and B: a ring, then a sphere.
  const bool silver = style == Style::XboxB;
  CGContextSetRGBFillColor(ctx, 0, 0, 0, fade);
  CGContextFillEllipseInRect(ctx, CGRectMake(cx - radius, cy - radius, radius * 2, radius * 2));
  CGContextSaveGState(ctx);
  CGContextSetLineWidth(ctx, radius * 0.16f);
  CGContextSetLineCap(ctx, kCGLineCapRound);
  if (silver) {
    CGContextSetRGBStrokeColor(ctx, 0x6C / 255.f, 1.f, 0x14 / 255.f, fade);
    CGContextAddArc(ctx, cx, cy, radius * 0.78f, 0, pi * 2, 0);
    CGContextStrokePath(ctx);
  } else {
    CGContextSetRGBStrokeColor(ctx, 0x6C / 255.f, 1.f, 0x14 / 255.f, fade);
    const CGFloat span = 160.f * pi / 180.f;
    CGContextAddArc(ctx, cx, cy, radius * 0.78f, pi + span * 0.5f, pi - span * 0.5f, 1);
    CGContextStrokePath(ctx);
  }
  CGContextRestoreGState(ctx);
  const CGFloat inner = radius * 0.58f;
  const CGFloat top[] = {silver ? 0.95f : 0.78f, silver ? 0.95f : 0.80f, silver ? 0.97f : 0.82f, fade};
  const CGFloat bot[] = {silver ? 0.55f : 0.42f, silver ? 0.58f : 0.44f, silver ? 0.62f : 0.46f, fade};
  FillVertical(ctx, CGRectMake(cx - inner, cy - inner, inner * 2, inner * 2), inner, top, bot);
  const CGFloat ar = silver ? 0.15f : 0.95f, ag = silver ? 0.17f : 0.95f, ab = silver ? 0.18f : 0.96f;
  DrawApple(ctx, cx, cy, inner * 1.2f, ar, ag, ab, fade);
}

void DrawCup(CGContextRef ctx, CGFloat x, CGFloat y, CGFloat side, int points, CGFloat fade) {
  CGFloat r = 0xCD / 255.f, g = 0x7F / 255.f, b = 0x32 / 255.f;
  if (points >= 100) {
    r = 0xE5 / 255.f;
    g = 0xE4 / 255.f;
    b = 0xE2 / 255.f;
  } else if (points >= 40) {
    r = 1;
    g = 0xD7 / 255.f;
    b = 0;
  } else if (points >= 20) {
    r = g = b = 0xC0 / 255.f;
  }
  CGContextSetRGBFillColor(ctx, r, g, b, fade);
  const CGFloat cx = x + side * 0.5f, base = y + side * 0.18f;
  CGContextAddEllipseInRect(ctx, CGRectMake(x + side * 0.18f, y + side * 0.42f, side * 0.64f, side * 0.42f));
  CGContextFillPath(ctx);
  CGContextFillRect(ctx, CGRectMake(cx - side * 0.06f, base + side * 0.08f, side * 0.12f, side * 0.22f));
  CGContextFillRect(ctx, CGRectMake(cx - side * 0.22f, base, side * 0.44f, side * 0.08f));
  CGContextSetLineWidth(ctx, side * 0.06f);
  CGContextSetRGBStrokeColor(ctx, r, g, b, fade);
  CGContextMoveToPoint(ctx, x + side * 0.22f, y + side * 0.62f);
  CGContextAddQuadCurveToPoint(ctx, x + side * 0.02f, y + side * 0.55f, x + side * 0.16f, y + side * 0.40f);
  CGContextStrokePath(ctx);
  CGContextMoveToPoint(ctx, x + side * 0.78f, y + side * 0.62f);
  CGContextAddQuadCurveToPoint(ctx, x + side * 0.98f, y + side * 0.55f, x + side * 0.84f, y + side * 0.40f);
  CGContextStrokePath(ctx);
}

void DrawXbox(CGContextRef ctx, const ToastFrame& frame, int w, int h) {
  const CGFloat fade = frame.fade;
  const CGFloat u = h / 82.f;
  const CGFloat radius = frame.style == Style::XboxC ? 4.f * u : h * 0.5f;
  const CGFloat orb_r = h * 0.36f;
  const CGFloat body = std::max(orb_r * 2.f, orb_r * 2.f + frame.pill * (w - orb_r * 2.f - 8.f * u));
  CGRect pill = CGRectMake(2.f * u, 4.f * u, body, h - 10.f * u);
  if (frame.pill > 0.02f || frame.style == Style::XboxC) {
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 0.35f * fade);
    CGPathRef shadow = CGPathCreateWithRoundedRect(CGRectOffset(pill, 0, -3.f * u), radius, radius, nullptr);
    CGContextAddPath(ctx, shadow);
    CGContextFillPath(ctx);
    CGPathRelease(shadow);
  }
  if (frame.style == Style::XboxA || frame.style == Style::XboxE || frame.style == Style::XboxC) {
    const CGFloat fill[3] = {frame.style == Style::XboxE ? 0x3A / 255.f : frame.style == Style::XboxC ? 0x2B / 255.f : 0x3E / 255.f,
                             frame.style == Style::XboxE ? 0x32 / 255.f : frame.style == Style::XboxC ? 0x2B / 255.f : 0x40 / 255.f,
                             frame.style == Style::XboxE ? 0x26 / 255.f : frame.style == Style::XboxC ? 0x2B / 255.f : 0x3D / 255.f};
    CGContextSetRGBFillColor(ctx, fill[0], fill[1], fill[2], fade);
    CGPathRef path = CGPathCreateWithRoundedRect(pill, radius, radius, nullptr);
    CGContextAddPath(ctx, path);
    CGContextFillPath(ctx);
    CGPathRelease(path);
    if (frame.style != Style::XboxC)
      StrokeRound(ctx, pill, radius, std::max(1.0, double(u)), 0, 0, 0, 0.45f * fade);
  } else if (frame.style == Style::XboxB) {
    const CGFloat top[] = {0.93f, 0.93f, 0.95f, fade};
    const CGFloat bot[] = {0.62f, 0.64f, 0.68f, fade};
    FillVertical(ctx, pill, radius, top, bot);
    CGContextSaveGState(ctx);
    CGPathRef path = CGPathCreateWithRoundedRect(pill, radius, radius, nullptr);
    CGContextAddPath(ctx, path);
    CGContextClip(ctx);
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 0.45f * fade);
    CGContextFillEllipseInRect(ctx, CGRectMake(pill.origin.x, CGRectGetMidY(pill), pill.size.width, pill.size.height));
    CGContextRestoreGState(ctx);
    CGPathRelease(path);
  } else {
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 0.55f * fade);
    CGPathRef path = CGPathCreateWithRoundedRect(pill, radius, radius, nullptr);
    CGContextAddPath(ctx, path);
    CGContextFillPath(ctx);
    CGPathRelease(path);
    StrokeRound(ctx, pill, radius, std::max(1.0, double(u)), 1, 1, 1, 0.35f * fade);
  }
  if (frame.orb > 0.01f) {
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, pill.origin.x + orb_r + 4.f * u, CGRectGetMidY(pill));
    CGContextScaleCTM(ctx, frame.orb, frame.orb);
    DrawOrb(ctx, 0, 0, orb_r * 0.92f, frame.style, fade);
    CGContextRestoreGState(ctx);
  }
  if (frame.pill > 0.15f) {
    CGContextSaveGState(ctx);
    CGPathRef clip = CGPathCreateWithRoundedRect(pill, radius, radius, nullptr);
    CGContextAddPath(ctx, clip);
    CGContextClip(ctx);
    const bool dark_text = frame.style == Style::XboxB;
    const CGFloat tr = dark_text ? 0.16f : frame.style == Style::XboxE ? 1.f : 0xF3 / 255.f;
    const CGFloat tg = dark_text ? 0.17f : frame.style == Style::XboxE ? 0.96f : 0xF3 / 255.f;
    const CGFloat tb = dark_text ? 0.18f : frame.style == Style::XboxE ? 0.90f : 0xF1 / 255.f;
    const CGFloat text_x = pill.origin.x + orb_r * 2.f + 10.f * u;
    DrawText(ctx, frame.headline, false, (frame.style == Style::XboxC ? 11.f : 13.f) * u, text_x,
             CGRectGetMidY(pill) + 4.f * u, tr, tg, tb, fade);
    DrawText(ctx, frame.detail, true, 16.f * u, text_x, CGRectGetMidY(pill) - 16.f * u, tr, tg, tb, fade);
    CGContextRestoreGState(ctx);
    CGPathRelease(clip);
  }
}

void DrawPs3(CGContextRef ctx, const ToastFrame& frame, int w, int h) {
  const CGFloat fade = frame.fade;
  const CGFloat u = h / 72.f;
  CGRect box = CGRectMake(1, 1, w - 2, h - 2);
  CGContextSetRGBFillColor(ctx, 0x3F / 255.f, 0x40 / 255.f, 0x3F / 255.f, 0.88f * fade);
  CGPathRef path = CGPathCreateWithRoundedRect(box, 6.f * u, 6.f * u, nullptr);
  CGContextAddPath(ctx, path);
  CGContextFillPath(ctx);
  CGPathRelease(path);
  StrokeRound(ctx, box, 6.f * u, std::max(1.0, double(u)), 0.75f, 0.75f, 0.75f, 0.55f * fade);
  const CGFloat icon = 53.f * u;
  DrawCup(ctx, 8.f * u, (h - icon) * 0.5f, icon, frame.points, fade);
  DrawText(ctx, frame.headline, false, 14.f * u, icon + 16.f * u, h * 0.58f, 1, 1, 1, fade);
  DrawText(ctx, frame.detail, true, 15.f * u, icon + 16.f * u, h * 0.28f, 1, 1, 1, fade);
}

void DrawSteam(CGContextRef ctx, const ToastFrame& frame, int w, int h) {
  const CGFloat fade = frame.fade;
  const CGFloat u = h / 80.f;
  CGRect box = CGRectMake(1, 2.f * u, w - 2, h - 4.f * u);
  const CGFloat top[] = {0x1B / 255.f, 0x28 / 255.f, 0x38 / 255.f, fade};
  const CGFloat bot[] = {0x12 / 255.f, 0x1C / 255.f, 0x28 / 255.f, fade};
  FillVertical(ctx, box, 3.f * u, top, bot);
  StrokeRound(ctx, box, 3.f * u, std::max(1.0, double(u)), 0x2A / 255.f, 0x47 / 255.f, 0x5E / 255.f, fade);
  const CGFloat icon = 56.f * u;
  CGContextSetRGBFillColor(ctx, 0x0B / 255.f, 0x1B / 255.f, 0x2E / 255.f, fade);
  CGContextFillRect(ctx, CGRectMake(10.f * u, (h - icon) * 0.5f, icon, icon));
  DrawApple(ctx, 10.f * u + icon * 0.5f, h * 0.5f, icon * 0.62f, 0x66 / 255.f, 0xC0 / 255.f, 0xF4 / 255.f, fade);
  const CGFloat text_x = 10.f * u + icon + 10.f * u;
  DrawText(ctx, frame.headline, false, 12.f * u, text_x, h * 0.68f, 0x66 / 255.f, 0xC0 / 255.f, 0xF4 / 255.f, fade);
  DrawText(ctx, frame.detail, true, 15.f * u, text_x, h * 0.42f, 1, 1, 1, fade);
  if (!frame.line3.empty())
    DrawText(ctx, frame.line3, false, 11.f * u, text_x, h * 0.18f, 0xC6 / 255.f, 0xD4 / 255.f, 0xDF / 255.f, fade);
}

std::vector<uint8_t> Raster(const ToastFrame& frame, int w, int h) {
  std::vector<uint8_t> raw(size_t(std::max(w, 1) * std::max(h, 1) * 4), 0);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(raw.data(), size_t(w), size_t(h), 8, size_t(w) * 4, space,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(space);
  if (!ctx) return {};
  if (frame.style == Style::Ps3) DrawPs3(ctx, frame, w, h);
  else if (frame.style == Style::Steam) DrawSteam(ctx, frame, w, h);
  else DrawXbox(ctx, frame, w, h);
  CGContextRelease(ctx);
  // Row 0 of this buffer is already the top of the toast. Metal shows that
  // row at the top of the quad, so it is uploaded as it stands.
  std::vector<uint8_t> pixels(raw.size());
  gtaiv::achievements::CopyToastRows(raw.data(), pixels.data(), w, h);
  return pixels;
}

struct Cache {
  std::vector<uint8_t> pixels;
  int w = 0, h = 0, x = 0, y = 0;
  uint64_t serial = 1;
  std::string key;
};

Cache& Store() {
  static Cache cache;
  return cache;
}

std::string KeyOf(const ToastFrame& frame, int w, int h) {
  return std::to_string(int(frame.style)) + "|" + frame.headline + "|" + frame.detail + "|" + frame.line3 + "|" +
         std::to_string(frame.points) + "|" + std::to_string(int(frame.orb * 20)) + "|" +
         std::to_string(int(frame.pill * 20)) + "|" + std::to_string(int(frame.fade * 20)) + "|" + std::to_string(w) +
         "x" + std::to_string(h);
}

bool Publish(const ToastFrame& frame, int view_w, int view_h, GtaivToastBitmap* out) {
  const auto box = gtaiv::achievements::PlaceToast(frame.style, frame.slide, view_w, view_h);
  if (box.w <= 0 || box.h <= 0) return false;
  Cache& cache = Store();
  const std::string key = KeyOf(frame, box.w, box.h);
  if (key != cache.key) {
    cache.pixels = Raster(frame, box.w, box.h);
    cache.w = box.w;
    cache.h = box.h;
    cache.key = key;
    cache.serial++;
  }
  cache.x = box.x;
  cache.y = box.y;
  if (cache.pixels.empty()) return false;
  out->rgba = cache.pixels.data();
  out->width = cache.w;
  out->height = cache.h;
  out->x = cache.x;
  out->y = cache.y;
  out->serial = cache.serial;
  return true;
}

}  // namespace

extern "C" bool gtaiv_achievement_overlay(int view_px_w, int view_px_h, float scale, bool scene, GtaivToastBitmap* out) {
  (void)scale;
  if (!out || view_px_w < 8 || view_px_h < 8) return false;
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  const ToastFrame frame = gtaiv::achievements::PresentToastAt(scene, now);
  if (frame.chime) PlayChime(gtaiv::achievements::MakeUnlockChime());
  if (!frame.visible) return false;
  return Publish(frame, view_px_w, view_px_h, out);
}

extern "C" bool gtaiv_achievement_preview(const char* style_name, GtaivToastBitmap* out) {
  if (!out) return false;
  ToastFrame frame;
  frame.visible = true;
  frame.style = gtaiv::achievements::ParseStyle(style_name);
  frame.anchor = gtaiv::achievements::StyleAnchor(frame.style);
  frame.orb = frame.pill = frame.slide = frame.fade = 1;
  frame.points = 5;
  const auto copy = gtaiv::achievements::CopyFor(frame.style, 5, "Off The Boat", "You have completed the first mission.", false);
  frame.headline = copy.line1;
  frame.detail = copy.line2;
  frame.line3 = copy.line3;
  return Publish(frame, 1280, 720, out);
}
