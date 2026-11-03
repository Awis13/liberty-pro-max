// Touch controls on the iPad: an overlay on the game's view that drives a
// virtual gamepad, which the game reads like any other (xinput.cpp). One
// layout on foot, one in a vehicle, one in the menus (bench.cpp says which).
// The left half of the screen is a stick that appears under the finger, the
// right half moves the camera, the rest are buttons. A real controller hides
// it all and takes the virtual one's place; GTAIV_TOUCH=0 turns it off.

#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace {

enum Context { kOnFoot, kInVehicle, kMenu };
std::atomic<int> g_context{kMenu};

// What a button presses: an SDL gamepad button, or a trigger.
constexpr int kLeftTrigger = -1, kRightTrigger = -2;

// x and y on the layouts' 2732 x 2048 screen, y up from the bottom edge; r on
// its 2x scale.
struct Def {
  const char* label;
  float x, y, r;
  int press;
  bool main;
};
constexpr int A = SDL_GAMEPAD_BUTTON_SOUTH, B = SDL_GAMEPAD_BUTTON_EAST, X = SDL_GAMEPAD_BUTTON_WEST,
              Y = SDL_GAMEPAD_BUTTON_NORTH, LS = SDL_GAMEPAD_BUTTON_LEFT_STICK,
              RS = SDL_GAMEPAD_BUTTON_RIGHT_STICK, LB = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
              RB = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, UP = SDL_GAMEPAD_BUTTON_DPAD_UP,
              RIGHT = SDL_GAMEPAD_BUTTON_DPAD_RIGHT, START = SDL_GAMEPAD_BUTTON_START,
              BACK = SDL_GAMEPAD_BUTTON_BACK;
const std::vector<Def> kLayouts[3] = {
    {{"Fire", 2430, 520, 135, kRightTrigger, true},
     {"Aim", 2150, 330, 115, kLeftTrigger, true},
     {"Run", 2470, 240, 115, A, false},
     {"Jump", 2130, 640, 95, X, false},
     {"Enter", 2360, 800, 95, Y, false},
     {"Cover", 2600, 790, 85, RB, false},
     {"Reload", 1890, 470, 85, B, false},
     {"Crouch", 1880, 240, 75, LS, false},
     {"Weapon", 1640, 150, 70, RIGHT, false},
     {"Phone", 2600, 1720, 70, UP, false},
     {"Menu", 2600, 1920, 60, START, false},
     {"Map", 130, 1920, 60, START, false}},
    {{"Gas", 2430, 400, 150, kRightTrigger, true},
     {"Brake", 2110, 280, 130, kLeftTrigger, true},
     {"Hand\nbrake", 2110, 610, 105, RB, false},
     {"Exit", 2470, 760, 95, Y, false},
     {"Horn", 1830, 400, 80, LS, false},
     {"Radio", 1620, 160, 75, RIGHT, false},
     {"Look\nback", 1830, 160, 75, RS, false},
     {"Camera", 2600, 1500, 70, BACK, false},
     {"Phone", 2600, 1720, 70, UP, false},
     {"Menu", 2600, 1920, 60, START, false},
     {"Map", 130, 1920, 60, START, false}},
    {{"Accept", 2430, 400, 135, A, true},
     {"Back", 2110, 280, 115, B, false},
     {"Prev", 1830, 160, 75, LB, false},
     {"Next", 2110, 560, 75, RB, false},
     {"Menu", 2600, 1920, 60, START, false}},
};

constexpr CGFloat kStickRadius = 125;
constexpr CGFloat kLookFullSpeed = 14;  // points a tick for the right stick at full

Sint16 Axis(CGFloat v) { return Sint16(std::clamp(v, -1.0, 1.0) * 32767); }

SDL_Joystick* g_pad = nullptr;

void AttachPad() {
  if (g_pad) return;
  SDL_VirtualJoystickDesc d;
  SDL_INIT_INTERFACE(&d);
  d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  d.naxes = SDL_GAMEPAD_AXIS_COUNT;
  d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  d.name = "Touch Controls";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
  g_pad = id ? SDL_OpenJoystick(id) : nullptr;
  if (!g_pad) SDL_Log("touch controls: %s", SDL_GetError());
}

void DetachPad() {
  if (!g_pad) return;
  const SDL_JoystickID id = SDL_GetJoystickID(g_pad);
  SDL_CloseJoystick(g_pad);
  SDL_DetachVirtualJoystick(id);
  g_pad = nullptr;
}

bool RealPad() {
  int n = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&n);
  bool real = false;
  for (int i = 0; ids && i < n; i++) real = real || !SDL_IsJoystickVirtual(ids[i]);
  SDL_free(ids);
  return real;
}

}  // namespace

@interface GtaivTouchView : UIView
@end

@implementation GtaivTouchView {
  struct Track {
    int button = -1;  // index in the layout; else the stick (left) or the camera (right)
    bool left = false;
    CGPoint start{}, at{}, last{};
  };
  std::unordered_map<void*, Track> _tracks;
  NSMutableArray<UIView*>* _buttons;
  int _layout;
  UIView* _ring;
  UIView* _knob;
  CGSize _laidOutFor;
  CGFloat _lookX, _lookY;
}

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.backgroundColor = UIColor.clearColor;
    self.multipleTouchEnabled = YES;
    self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _buttons = [NSMutableArray array];
    _layout = -1;
    _ring = [self circle:NO];
    _knob = [self circle:NO];
    _ring.hidden = _knob.hidden = YES;
    [self addSubview:_ring];
    [self addSubview:_knob];
  }
  return self;
}

- (UIView*)circle:(BOOL)main {
  UIView* v = [UIView new];
  v.userInteractionEnabled = NO;
  v.backgroundColor = (main ? UIColor.systemOrangeColor : UIColor.blackColor);
  v.layer.borderColor = UIColor.whiteColor.CGColor;
  v.layer.borderWidth = 2;
  v.alpha = 0.4;
  return v;
}

- (CGPoint)place:(const Def&)d {
  const CGSize s = self.bounds.size;
  return CGPointMake(d.x / 2732 * s.width, (1 - d.y / 2048) * s.height);
}

- (CGFloat)radius:(const Def&)d {
  const CGSize s = self.bounds.size;
  return d.r / 2 * std::min(s.width / 1366, s.height / 1024);
}

- (void)releaseAll {
  if (!g_pad) return;
  for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) SDL_SetJoystickVirtualButton(g_pad, i, false);
  for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++) SDL_SetJoystickVirtualAxis(g_pad, i, 0);
}

- (void)showLayout:(int)layout {
  [self releaseAll];
  _tracks.clear();
  _ring.hidden = _knob.hidden = YES;
  for (UIView* v in _buttons) [v removeFromSuperview];
  [_buttons removeAllObjects];
  _layout = layout;
  _laidOutFor = self.bounds.size;
  for (const Def& d : kLayouts[layout]) {
    UIView* v = [self circle:d.main];
    const CGFloat r = [self radius:d];
    const CGPoint c = [self place:d];
    v.frame = CGRectMake(c.x - r, c.y - r, 2 * r, 2 * r);
    v.layer.cornerRadius = r;
    UILabel* l = [[UILabel alloc] initWithFrame:v.bounds];
    l.text = @(d.label);
    l.numberOfLines = 2;
    l.textAlignment = NSTextAlignmentCenter;
    l.textColor = UIColor.whiteColor;
    l.font = [UIFont systemFontOfSize:r > 50 ? 22 : 16 weight:UIFontWeightSemibold];
    l.adjustsFontSizeToFitWidth = YES;
    [v addSubview:l];
    [self insertSubview:v belowSubview:_ring];
    [_buttons addObject:v];
  }
}

- (void)set:(int)press down:(bool)down {
  if (!g_pad) return;
  if (press == kLeftTrigger) SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, down ? 32767 : 0);
  else if (press == kRightTrigger) SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, down ? 32767 : 0);
  else SDL_SetJoystickVirtualButton(g_pad, press, down);
}

- (void)press:(int)button down:(bool)down {
  const std::vector<Def>& layout = kLayouts[_layout];
  // (Two buttons can press the same thing: held while either is.)
  bool held = down;
  for (const auto& [t, track] : _tracks)
    if (track.button >= 0 && layout[track.button].press == layout[button].press) held = true;
  [self set:layout[button].press down:held];
  _buttons[button].alpha = down ? 0.75 : 0.4;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  if (_layout < 0) return;
  const std::vector<Def>& layout = kLayouts[_layout];
  for (UITouch* touch in touches) {
    const CGPoint p = [touch locationInView:self];
    Track t;
    t.start = t.at = t.last = p;
    for (size_t i = 0; i < layout.size(); i++) {
      const CGPoint c = [self place:layout[i]];
      if (std::hypot(p.x - c.x, p.y - c.y) <= [self radius:layout[i]] * 1.1) t.button = int(i);
    }
    t.left = t.button < 0 && p.x < self.bounds.size.width / 2;
    _tracks[(__bridge void*)touch] = t;
    if (t.button >= 0) {
      [self press:t.button down:true];
    } else if (t.left) {
      _ring.frame = CGRectMake(p.x - kStickRadius, p.y - kStickRadius, 2 * kStickRadius, 2 * kStickRadius);
      _ring.layer.cornerRadius = kStickRadius;
      _knob.bounds = CGRectMake(0, 0, 90, 90);
      _knob.layer.cornerRadius = 45;
      _knob.center = p;
      _ring.hidden = _knob.hidden = NO;
    }
  }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  for (UITouch* touch in touches) {
    auto it = _tracks.find((__bridge void*)touch);
    if (it == _tracks.end()) continue;
    Track& t = it->second;
    t.at = [touch locationInView:self];
    if (t.left && g_pad) {
      CGFloat dx = t.at.x - t.start.x, dy = t.at.y - t.start.y;
      const CGFloat len = std::hypot(dx, dy);
      if (len > kStickRadius) {
        dx *= kStickRadius / len;
        dy *= kStickRadius / len;
      }
      _knob.center = CGPointMake(t.start.x + dx, t.start.y + dy);
      SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX, Axis(dx / kStickRadius));
      SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY, Axis(dy / kStickRadius));
    }
  }
}

- (void)end:(NSSet<UITouch*>*)touches {
  for (UITouch* touch in touches) {
    auto it = _tracks.find((__bridge void*)touch);
    if (it == _tracks.end()) continue;
    const Track t = it->second;
    _tracks.erase(it);
    if (t.button >= 0) {
      [self press:t.button down:false];
    } else if (t.left) {
      _ring.hidden = _knob.hidden = YES;
      if (g_pad) {
        SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX, 0);
        SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY, 0);
      }
    }
  }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self end:touches]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event { [self end:touches]; }

// Each frame: the right layout for the moment, and the camera from how far the
// finger went since the last one, eased so that a stopped finger stops it.
- (void)tick {
  const int want = g_context.load();
  if (want != _layout || !CGSizeEqualToSize(_laidOutFor, self.bounds.size)) [self showLayout:want];
  CGFloat dx = 0, dy = 0;
  for (auto& [key, t] : _tracks) {
    if (t.button >= 0 || t.left) continue;
    dx += t.at.x - t.last.x;
    dy += t.at.y - t.last.y;
    t.last = t.at;
  }
  _lookX += (dx / kLookFullSpeed - _lookX) * 0.5;
  _lookY += (dy / kLookFullSpeed - _lookY) * 0.5;
  if (g_pad) {
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTX, Axis(_lookX));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTY, Axis(_lookY));
  }
}

@end

@interface GtaivTouchDriver : NSObject
@property(strong) GtaivTouchView* view;
- (void)frame:(CADisplayLink*)link;
@end

@implementation GtaivTouchDriver

- (void)frame:(CADisplayLink*)link {
  if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) return;
  if (!(SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD)) return;  // (the game's first look at pads)
  const bool real = RealPad();
  if (real) DetachPad();
  else AttachPad();
  if (!self.view) {
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
      if ([scene isKindOfClass:UIWindowScene.class])
        for (UIWindow* w in ((UIWindowScene*)scene).windows)
          if (w.isKeyWindow && w.rootViewController.view) window = w;
    if (!window) return;
    UIView* top = window.rootViewController.view;
    self.view = [[GtaivTouchView alloc] initWithFrame:top.bounds];
    [top addSubview:self.view];
  }
  self.view.hidden = real;
  if (!real) [self.view tick];
}

@end

// main.cpp: once the launcher is gone.
void GtaivTouchStart() {
  if (const char* e = std::getenv("GTAIV_TOUCH"); e && *e == '0') return;
  static GtaivTouchDriver* driver = [GtaivTouchDriver new];
  CADisplayLink* link = [CADisplayLink displayLinkWithTarget:driver selector:@selector(frame:)];
  link.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
  [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSDefaultRunLoopMode];
}

// bench.cpp, from the game's main loop.
void GtaivTouchContext(bool in_game, bool in_vehicle, bool menu) {
  g_context.store(!in_game || menu ? kMenu : in_vehicle ? kInVehicle : kOnFoot);
}
