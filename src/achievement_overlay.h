// The toast bitmap the Metal present draws on top of the frame.
// Implemented in achievement_overlay.mm and compiled into the Metal
// renderer, which calls it directly (a lookup across dylibs was silent
// when it failed, and the loading screen then never logged a toast).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GtaivToastBitmap {
  const uint8_t* rgba;  // premultiplied RGBA, tightly packed, top row first
  int width;
  int height;
  int x;  // top-left, view pixels
  int y;
  uint64_t serial;  // changes when the pixels change, not when it only slides
};

// False when there is nothing to draw. `rgba` stays valid until the next call.
// `scene` is true once the renderer has presented a first scene.
bool gtaiv_achievement_overlay(int view_px_w, int view_px_h, float scale, bool scene, struct GtaivToastBitmap* out);

// A fully shown sample of `style` (Off The Boat) for the launcher. The
// pointer stays valid until the next preview or overlay call.
__attribute__((visibility("default"))) bool gtaiv_achievement_preview(const char* style, struct GtaivToastBitmap* out);

#ifdef __cplusplus
}
#endif
