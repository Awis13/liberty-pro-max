// Aspect ratio fixes for screens other than 16:9 (ultrawide 21:9 above
// all), ported from FusionFix's widescreenfix (ThirteenAG, GPL-3.0) for
// GTAIV.exe 1.0.8.0:
//   - "auto" aspect ratio is the back buffer's, not the first display
//     mode's (the game stretched everything to that mode's 4:3);
//   - the caller of the aspect function asks for the computed ratio;
//   - the cellphone and the frontend scale their width by 16:9 / aspect;
//   - loading-screen art keeps 16:9, centred between black pillars;
//   - the pause menu's map: the screen's aspect, as many tile columns as
//     fit, round area blips and its crosshair's thickness
//     (GTAIV_PAUSE_MAP_ASPECT, on by default);
//   - cutscenes framed 16:9 between black pillars, as they were composed
//     (GTAIV_CUTSCENE_PILLARS, on by default);
//   - the in-game computer's web pages not stretched, its 2D in a centred
//     16:9 frame and the mouse hitting the links (GTAIV_WEB_PAGES, on).
// Not ported: FusionFix's offline_21by9 menu texture (its own asset).

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <cmath>

#include "option.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

void f_007F2630_lifted(Ctx& c);
void f_0089DF30_lifted(Ctx& c);
void f_00B4D880_lifted(Ctx& c);
void f_004BEDF0_lifted(Ctx& c);

namespace {

// rage::grcDevice::ms_nActiveWidth / ms_nActiveHeight.
constexpr uint32_t kActiveWidth = 0x010A7518, kActiveHeight = 0x010A751C;
constexpr float kDefaultAspect = 16.0f / 9.0f;

float ScreenAspect() {
  const int32_t w = int32_t(Rd32(kActiveWidth)), h = int32_t(Rd32(kActiveHeight));
  return w > 0 && h > 0 ? float(w) / float(h) : kDefaultAspect;
}

void MulLane0(Xmm& x, float k) {
  float v;
  std::memcpy(&v, x.data(), 4);
  v *= k;
  std::memcpy(x.data(), &v, 4);
}

float Lane0(const Xmm& x) {
  float v;
  std::memcpy(&v, x.data(), 4);
  return v;
}

void WrF32(uint32_t va, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  Wr32(va, bits);
}

float RdF32(uint32_t va) {
  const uint32_t bits = Rd32(va);
  float v;
  std::memcpy(&v, &bits, 4);
  return v;
}

bool PauseMapAspect() {
  static const bool on = OptionOn("GTAIV_PAUSE_MAP_ASPECT", true);
  return on;
}

// CSprite2d: the full draw (cdecl: x0, y0, x1, y1, z, u0, v0, u1, v1, and
// two more; 0x2C bytes of arguments) and DrawRect (cdecl: const Vector4*,
// const Color32*).
constexpr uint32_t kSpriteDraw = 0x0085D780, kSpriteDrawRect = 0x0085DCB0;

// Black bars left and right of the centred 16:9 area.
void DrawPillars(Ctx& c) {
  const float w = float(int32_t(Rd32(kActiveWidth))), h = float(int32_t(Rd32(kActiveHeight)));
  const float half = (w - h * kDefaultAspect) / 2.0f;
  if (half <= 0.5f) return;
  c.esp -= 32;  // two Vector4s' room and the colour, below the caller's frame
  const uint32_t rect = c.esp, col = c.esp + 16;
  Wr32(col, 0xFF000000);  // opaque black
  const float bars[2][4] = {{0.0f, 0.0f, half, h}, {w - half, 0.0f, w, h}};
  for (const auto& b : bars) {
    std::memcpy(GPtr(rect), b, 16);
    CallGuest(c, kSpriteDrawRect, {rect, col});
  }
  c.esp += 32;
}

// A call of CSprite2d's draw for loading-screen art (FusionFix
// DrawLoadingScreen): on a screen wider than 3:2 a full-height picture is
// narrowed to 16:9 and centred, then pillars cover the sides.
void LoadingScreenSprite(Ctx& c) {
  uint32_t a[11];
  for (int i = 0; i < 11; i++) a[i] = Rd32(c.esp + 4 * uint32_t(i));
  float r[4];
  std::memcpy(r, a, 16);
  const float w = float(int32_t(Rd32(kActiveWidth))), aspect = ScreenAspect();
  const bool wide = aspect > 1.5f, full_height = (r[3] - r[1]) / 720.0f >= 1.0f;
  if (wide && full_height) {
    const float scale = kDefaultAspect / aspect, offset = (w - w * scale) / 2.0f;
    r[0] = r[0] * scale + offset;
    r[2] = r[2] * scale + offset;
    std::memcpy(a, r, 16);
  }
  CallGuest(c, kSpriteDraw, {a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]});
  if (aspect > kDefaultAspect + 0.01f) DrawPillars(c);
}

}  // namespace

// The three loading-screen draws (the `call 85D780` at each).
void hook_004A1729(Ctx& c) { LoadingScreenSprite(c); }
void hook_004A179E(Ctx& c) { LoadingScreenSprite(c); }
void hook_004A1840(Ctx& c) { LoadingScreenSprite(c); }

// [89BA65, 89BAC2): `jne` taken always: the ratio comes from the active
// width and height (FusionFix: "Fix auto aspect ratio").
void hook_0089BA65(Ctx& c) { (void)c; }

// 4A158D: `push 1` -> `push 0` (the aspect function's argument).
void hook_004A158D(Ctx& c) {
  c.esp -= 4;
  Wr32(c.esp, 0);
}

// 492428: mulss xmm1, [0.75]: the cellphone's width.
void hook_00492428(Ctx& c) { MulLane0(c.xmm[1], 0.75f * kDefaultAspect / ScreenAspect()); }

// D44C99: mulss xmm0, [1152.0]: the frontend's width.
void hook_00D44C99(Ctx& c) { MulLane0(c.xmm[0], 1152.0f * kDefaultAspect / ScreenAspect()); }

// The pause menu's map (FusionFix's frontend map fixes, by their idea).

// The radar viewport's camera: movss [esi+2BC], xmm0 with a fixed 16:9 (or
// 4:3) aspect in the frontend, where the map covers the screen; 1.0 is
// the round radar's own.
void hook_004B1248(Ctx& c) {
  if (PauseMapAspect() && Lane0(c.xmm[0]) != 1.0f) {
    const float a = ScreenAspect();
    std::memcpy(c.xmm[0].data(), &a, 4);
  }
  std::memcpy(GPtr(c.esi + 0x2BC), c.xmm[0].data(), 4);
}

// The map's tile columns drawn: the centre one (esi) and eax either side,
// edi..ebp; 3 in the frontend, as wide as 16:9. Wider screens get more,
// within the map's columns ([00F35F64]).
void hook_008739AC(Ctx& c) {
  int32_t lo = int32_t(c.esi) - int32_t(c.eax), hi = int32_t(c.esi) + int32_t(c.eax);
  if (PauseMapAspect() && c.eax == 3) {
    const int32_t range = int32_t(std::ceil(3.0f * ScreenAspect() / kDefaultAspect - 0.01f));
    if (range > 3) {
      lo = std::max(int32_t(c.esi) - range, 0);
      hi = std::min(int32_t(c.esi) + range, int32_t(Rd32(0x00F35F64)) - 1);
    }
  }
  c.edi = uint32_t(lo);
  c.ebp = uint32_t(hi);
}

// An area blip's vertical radius: the horizontal one times the fixed
// aspect (16:9 if al, else 4:3), then movss [esp+1C], xmm0.
void hook_00870B0E(Ctx& c) {
  if (PauseMapAspect()) MulLane0(c.xmm[0], ScreenAspect() / ((c.eax & 0xFF) ? kDefaultAspect : 4.0f / 3.0f));
  std::memcpy(GPtr(c.esp + 0x1C), c.xmm[0].data(), 4);
}

// The map crosshair: its line thickness (one pixel of the screen's width
// and of its height), from the frontend's widget 166 instead
// (FE_MAP_CURSOR_THICKNESS), the width narrowed by 4:3 / aspect; and the
// fade at its ends ([esp+20]) likewise.
namespace {
float g_cross_y;
float CrossNarrow() { return ScreenAspect() * 0.75f; }
}  // namespace
void hook_00486DDC(Ctx& c) {  // movss [esp+30], xmm0
  float x = Lane0(c.xmm[0]);
  if (PauseMapAspect()) {
    const uint32_t v = c.esp - 16;  // a Vector2 below the frame
    c.esp -= 16;
    CallGuest(c, 0x007BD670, {v, 166});  // CFrontEnd::GetWidgetValue
    c.esp += 16;
    x = RdF32(v) / CrossNarrow();
    g_cross_y = RdF32(v + 4);
  }
  WrF32(c.esp + 0x30, x);
}
void hook_00486E0D(Ctx& c) {  // movss [esp+3C], xmm0
  WrF32(c.esp + 0x3C, PauseMapAspect() ? g_cross_y : Lane0(c.xmm[0]));
}
void hook_00486E9E(Ctx& c) {  // movss xmm3, [k]
  if (PauseMapAspect()) WrF32(c.esp + 0x20, RdF32(c.esp + 0x20) / CrossNarrow());
  std::memset(c.xmm[3].data(), 0, 16);
  std::memcpy(c.xmm[3].data(), GPtr(0x00E7C6CC), 4);
}

// The cutscene borders (CCamera's widescreen borders, 007F2630): black
// bars top and bottom, queued for the render thread (a command of 1C
// bytes from 85E390, made a rectangle by 861780 (thiscall: rect, colour)
// and queued by 475920), or drawn at once on the render thread (DrawRect,
// when its thread-local "queue" pointer is 0). The screen's size: the
// window's or the full screen's (407600). On a screen wider than 16:9 two
// more, left and right of the 16:9 frame, the same way (FusionFix's
// cutscene pillarbox, by its idea).
void f_007F2630(Ctx& c) {
  static const bool on = OptionOn("GTAIV_CUTSCENE_PILLARS", true);
  f_007F2630_lifted(c);
  if (!on) return;
  const uint32_t ebx = c.ebx, esi = c.esi, edi = c.edi, ebp = c.ebp;
  c.ecx = 0x011F8280;
  if (CallGuest(c, 0x0089BB60, {}) & 0xFF) return;  // a menu is up: no borders
  const bool windowed = (CallGuest(c, 0x00407600, {}) & 0xFF) != 0;
  const float w = float(int32_t(Rd32(windowed ? 0x010A7510 : 0x010A7518)));
  const float h = float(int32_t(Rd32(windowed ? 0x010A7514 : 0x010A751C)));
  const float half = (w - h * kDefaultAspect) / 2.0f;
  if (w <= 0 || h <= 0 || half <= 0.5f) return;
  const uint32_t tls = Rd32(c.fs_base + 0x2C);
  const bool queued = Rd32(Rd32(tls + 4 * Rd32(0x01744224)) + 0x80) != 0;
  c.esp -= 32;  // a rectangle and a colour, below the frame
  const uint32_t rect = c.esp, col = c.esp + 16;
  Wr32(col, 0xFF000000);
  const float bars[2][4] = {{0.0f, 0.0f, half, h}, {w - half, 0.0f, w, h}};
  for (const auto& b : bars) {
    std::memcpy(GPtr(rect), b, 16);
    if (!queued) {
      CallGuest(c, kSpriteDrawRect, {rect, col});
      continue;
    }
    const uint32_t cmd = uint32_t(CallGuest(c, 0x0085E390, {0x1C, 0}));
    if (!cmd) continue;
    c.ecx = cmd;
    CallGuest(c, 0x00861780, {rect, 0xFF000000});
    c.ecx = cmd;
    CallGuest(c, 0x00475920, {});
  }
  c.esp += 32;
  c.ebx = ebx;
  c.esi = esi;
  c.edi = edi;
  c.ebp = ebp;
}

// The in-game computer (FusionFix's widescreen web pages, by its idea).
// Its web pages are 4:3 in a fixed 16:9 orthographic window, its desktop
// and browser 2D are stretched to the screen, and its scripts' mouse is
// the screen's. On a wider screen: the window follows the screen's aspect,
// the stretched 2D goes into a centred 16:9 frame while a page is shown,
// and the mouse and link picking go through the same frame.
namespace {

bool WebPages() {
  static const bool on = OptionOn("GTAIV_WEB_PAGES", true);
  return on;
}

// A web page is shown: one of the viewports (the list at 011F8280: data,
// 16-bit count) is a CViewportHtml (its vtable 00EC211C).
bool WebPageShown() {
  const uint32_t data = Rd32(0x011F8280);
  const uint32_t n = Rd32(0x011F8284) & 0xFFFF;
  if (!data) return false;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t vp = Rd32(data + 4 * i);
    if (vp && Rd32(vp) == 0x00EC211C) return true;
  }
  return false;
}

// How much narrower the 16:9 frame is than the screen (1: no page shown,
// or a screen no wider than 16:9).
float WebFrameScale() {
  if (!WebPages() || !WebPageShown()) return 1.0f;
  const float a = ScreenAspect();
  return a > kDefaultAspect ? kDefaultAspect / a : 1.0f;
}

float Mid(float x, float scale) { return 0.5f + (x - 0.5f) * scale; }

}  // namespace

// The page's orthographic window (left, right, bottom, top, near, far on
// the stack; call 4149A0): [-1/6, 7/6] across for 16:9 (left 0 for 4:3).
void hook_004CCC57(Ctx& c) {
  if (WebPages() && RdF32(c.esp) < 0.0f) {
    const float half = ScreenAspect() * 0.75f * 0.5f;
    WrF32(c.esp, 0.5f - half);
    WrF32(c.esp + 4, 0.5f + half);
  }
  CallGuest(c, 0x004149A0, {Rd32(c.esp), Rd32(c.esp + 4), Rd32(c.esp + 8), Rd32(c.esp + 12),
                            Rd32(c.esp + 16), Rd32(c.esp + 20)});
  c.esp += 0x18;  // its ret 18h took the arguments
}

// A script widescreen format applied to 2D (cdecl: format, float* pos,
// float* size, float* x range; whether it did): format 0, stretch, is left
// alone by the game; with a web page up, into the 16:9 frame instead.
void f_0089DF30(Ctx& c) {
  const float scale = Rd32(c.esp + 4) == 0 ? WebFrameScale() : 1.0f;
  if (Rd32(c.esp + 4) != 0 || scale == 1.0f) return f_0089DF30_lifted(c);
  const uint32_t pos = Rd32(c.esp + 8), size = Rd32(c.esp + 12), range = Rd32(c.esp + 16);
  if (pos) WrF32(pos, Mid(RdF32(pos), scale));
  if (size) WrF32(size, RdF32(size) * scale);
  if (range) {
    WrF32(range, Mid(RdF32(range), scale));
    WrF32(range + 4, Mid(RdF32(range + 4), scale));
  }
  c.eax = 1;
  c.esp += 4;  // ret
}

// GET_MOUSE_POSITION (cdecl: float* x, float* y): x in the 16:9 frame.
void f_00B4D880(Ctx& c) {
  const uint32_t x = Rd32(c.esp + 4);
  const float scale = WebFrameScale();
  f_00B4D880_lifted(c);
  if (scale != 1.0f && x) WrF32(x, std::clamp(0.5f + (RdF32(x) - 0.5f) / scale, 0.0f, 1.0f));
}

// GET_WEB_PAGE_LINK_AT_POSN (cdecl: viewport, x, y): the frame's x back on
// the screen, where the page is projected.
void f_004BEDF0(Ctx& c) {
  const float scale = WebFrameScale();
  if (scale != 1.0f) WrF32(c.esp + 8, Mid(RdF32(c.esp + 8), scale));
  f_004BEDF0_lifted(c);
}

}  // namespace recomp::gen
