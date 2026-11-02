// The Mac app's launcher: the first window of "Liberty Pro Max.app". It
// picks the game folder and the port's options, then starts the game as a
// new instance of the app with them (--no-launcher, through Launch
// Services): the game gets a process of its own from the start, so options
// read when it loads (the Metal HUD) apply, SDL sets up the application as
// it expects, and macOS turns Game Mode on for it.
//
// Shown when the executable runs from the app bundle; with "Skip this
// window" set, only while Option is held at launch.

#import <AppKit/AppKit.h>

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

NSString* const kFolderKey = @"GameFolder";
NSString* const kWindowKey = @"Windowed";
NSString* const kVsyncKey = @"VerticalSync";  // 0 the game's setting, 1 on, 2 off
NSString* const kMetalFXKey = @"MetalFX";      // earlier versions: on/off
NSString* const kAntiAliasingKey = @"AntiAliasing";  // 0 off, 1 SMAA, 2 MetalFX, 3 TAA, 4 SMAA + MetalFX spatial
NSString* const kMetalFXScaleKey = @"MetalFXScale";  // 0 native, 1 quality, 2 balanced, 3 performance
// TAA's sharpening: 0 off, 1 low, 2 medium, 3 high.
NSString* const kTaaSharpeningKey = @"TaaSharpening";
NSString* const kHudKey = @"PerformanceOverlay";
NSString* const kSkipKey = @"SkipLauncher";
NSString* const kSkipIntroKey = @"SkipIntro";
NSString* const kContinueKey = @"ContinueLastSave";
// The 2018-cut radio songs, from the payload tools/radio_import.sh imports.
NSString* const kRadioKey = @"OriginalRadio";
// Advanced: 0 the game's own, 1 high.
NSString* const kShadowsKey = @"ShadowResolution";
NSString* const kReflectionsKey = @"ReflectionResolution";
NSString* const kFastLoadingKey = @"FastLoading";
NSString* const kFovKey = @"ExtraFieldOfView";  // 0-4: +0, +5, +10, +15, +20 degrees
NSString* const kStunPunchKey = @"StunPunchAnywhere";
NSString* const kExecutionsKey = @"ExecutionsAnywhere";

// Advanced, the community's fixes (community_fixes.cpp, widescreen.cpp):
// each a checkbox, on unless the player turns it off.
struct Fix {
  NSString* key;
  const char* env;
  NSString* title;
  bool on = true;  // by default
};
const Fix kFixes[] = {
    {@"FixLadders", "GTAIV_LADDER_FIX", @"Ladders: no stalls before climbing at high frame rates"},
    {@"FixDrawDistance", "GTAIV_DRAW_DISTANCE_FIX", @"Draw distance settings apply from the start (costs fps)", false},
    {@"FixRadio", "GTAIV_RADIO_FIX", @"Radio keeps playing after cutscenes"},
    {@"InstantMenus", "GTAIV_INSTANT_MENUS", @"Pause menu opens and closes without delay"},
    {@"VehicleBudget", "GTAIV_VEHICLE_BUDGET", @"Twice the vehicle budget (no streets full of taxis)"},
    {@"BiggerPools", "GTAIV_BIGGER_POOLS", @"Bigger task and pointer pools"},
    {@"DrawableLimit", "GTAIV_DRAWABLE_LIMIT", @"No crash in \"In the Crosshairs\" at long draw distance"},
    {@"SaveInVehicle", "GTAIV_SAVE_IN_VEHICLE", @"Saving from the phone works in vehicles"},
    {@"PauseMapAspect", "GTAIV_PAUSE_MAP_ASPECT", @"Pause menu map fits wide screens"},
    {@"CutscenePillars", "GTAIV_CUTSCENE_PILLARS", @"Cutscenes framed 16:9 with side bars on wide screens"},
    {@"WebPages", "GTAIV_WEB_PAGES", @"In-game internet and police computer fit wide screens"},
    {@"MouseRecoil", "GTAIV_MOUSE_RECOIL", @"Weapon recoil with the mouse as with a gamepad"},
    {@"CoverCamera", "GTAIV_COVER_CAMERA", @"No forced camera centring at the edge of cover"},
    {@"LodLights", "GTAIV_LOD_LIGHTS", @"Distant city lights as on consoles (from 7 pm, street level)"},
    {@"CursorSize", "GTAIV_CURSOR_SIZE", @"Mouse cursor scaled to the screen"},
    {@"HideCursorOnPad", "GTAIV_HIDE_CURSOR_ON_PAD", @"Menu cursor hidden while playing with a gamepad"},
    {@"ReticleHealth", "GTAIV_RETICLE_HEALTH", @"Target health on the reticle with the mouse"},
    {@"RadarZoomDelay", "GTAIV_RADAR_ZOOM_DELAY", @"Radar stays zoomed out for 3 seconds"},
    {@"TrainLanes", "GTAIV_TRAIN_LANES", @"Cars do not swerve under trains"},
    {@"DateFix", "GTAIV_DATE_FIX", @"Date does not go back after dying or arrest"},
    {@"NoMemRestrict", "GTAIV_NO_MEM_RESTRICT", @"Streaming not held back by the memory budget"},
    {@"TimerFloor", "GTAIV_TIMER_FLOOR", @"No speed-up above 300 fps"},
    {@"HeliCeiling", "GTAIV_HELI_CEILING", @"Helicopters fly up to 770 m (as in TBoGT)", false},
    {@"PhysicsStep", "GTAIV_PHYSICS_STEP", @"Vehicle and ragdoll physics steady at high frame rates"},
    {@"SlideObject", "GTAIV_SLIDE_OBJECT", @"Scripted doors and gates move at the right speed"},
    {@"ShakeFix", "GTAIV_SHAKE_FIX", @"Camera shake as strong at any frame rate"},
    {@"CameraShake", "GTAIV_CAMERA_SHAKE", @"Camera shake (hand-held, drunk, vehicles)"},
    {@"CutsceneCamera", "GTAIV_CUTSCENE_CAMERA", @"Cutscene cameras without zoom fits and flicker at cuts"},
    {@"CutsceneClock", "GTAIV_CUTSCENE_CLOCK", @"Cutscene animations smooth and in sync with their audio"},
    {@"HeliRotorDamage", "GTAIV_HELI_ROTOR_DAMAGE", @"Helicopter rotors break as easily at any frame rate"},
    {@"LoadingAnimation", "GTAIV_LOADING_ANIMATION", @"Loading screen pictures move at console speed"},
    {@"WaterFlip", "GTAIV_WATER_FLIP", @"No water flicker (buffers swapped in step with rendering)"},
    {@"Minigame30", "GTAIV_MINIGAME_30FPS", @"30 fps in pool, air hockey, arm wrestling, bowling, drinking"},
    {@"RadarEveryFrame", "GTAIV_RADAR_EVERY_FRAME", @"Radar moves smoothly at high frame rates"},
    {@"HoodCamera", "GTAIV_HOOD_CAMERA", @"Bonnet camera sway smooth at high frame rates"},
    {@"CarCameraReturn", "GTAIV_CAR_CAMERA_RETURN", @"Car camera swings back faster with keyboard and mouse"},
    {@"FusionFixData", "GTAIV_FF_DATA", @"Use FusionFix's mission and model fixes if installed (update folder)"},
    {@"RunInBackground", "GTAIV_RUN_IN_BACKGROUND", @"Keep running when the window is not in front", false},
    {@"MenuRightClick", "GTAIV_MENU_RIGHT_CLICK", @"Right click goes back in menus"},
    {@"ActionCamera", "GTAIV_ACTION_CAMERA", @"Cinematic camera when vaulting walls and jacking cars"},
    {@"StuntJumpCamera", "GTAIV_STUNT_JUMP_CAMERA", @"Cinematic camera and slow motion on stunt jumps"},
    {@"ConsoleReflections", "GTAIV_CONSOLE_REFLECTIONS", @"Reflections and car dirt as on consoles"},
    {@"MirrorOffset", "GTAIV_MIRROR_OFFSET", @"Mirrors show decals near them"},
    {@"WaterTiling", "GTAIV_WATER_TILING", @"Water surface tiling as on consoles"},
    {@"MouseCameraDelay", "GTAIV_MOUSE_CAMERA_DELAY", @"On foot, the camera waits 2 s after the mouse before recentring"},
    {@"CoronaLimit", "GTAIV_CORONA_LIMIT", @"No flickering lights where there are many (corona limit 768 -> 49152)"},
    {@"CopHats", "GTAIV_COP_HATS", @"Cops take off their hats in vehicles (as on consoles)"},
    {@"CounterDeathAnim", "GTAIV_COUNTER_DEATH_ANIM", @"No double death animation after a melee counter"},
    {@"ReversingLights", "GTAIV_REVERSING_LIGHTS", @"Reversing lights at the back of the car"},
    {@"BikeFeet", "GTAIV_BIKE_FEET", @"Bikes: feet down when stopped on the phone and when reversing"},
    {@"SniperMovement", "GTAIV_SNIPER_MOVEMENT", @"Walk while zoomed in with a sniper rifle"},
    {@"RainLighting", "GTAIV_RAIN_LIGHTING", @"Rain lit as on consoles (visible at night)"},
    {@"HeliSearchlights", "GTAIV_HELI_SEARCHLIGHTS", @"Police helicopter searchlights do not flicker"},
    {@"NightShadowResolution", "GTAIV_NIGHT_SHADOW_RESOLUTION", @"Night shadows setting sets their sharpness"},
    {@"CenteredCarCamera", "GTAIV_CENTERED_CAR_CAMERA", @"Car camera centred behind the car", false},
    {@"CenteredFootCamera", "GTAIV_CENTERED_FOOT_CAMERA", @"On-foot camera centred behind Niko", false},
    {@"BrakeLightsStopped", "GTAIV_BRAKE_LIGHTS_STOPPED", @"Brake lights stay on while your car stands", false},
    {@"Bloom", "GTAIV_BLOOM", @"Bloom (glow around bright lights)"},
    {@"DepthOfField", "GTAIV_DEPTH_OF_FIELD", @"Depth of field blur"},
    {@"PadCameraDelay", "GTAIV_PAD_CAMERA_DELAY", @"On foot with a gamepad: camera waits 2 s before recentring", false},
    {@"CarCameraDelay", "GTAIV_CAR_CAMERA_DELAY", @"In vehicles: camera waits 2 s before recentring", false},
    {@"AimZoomMemory", "GTAIV_AIM_ZOOM_MEMORY", @"Aiming zoom remembered between aims (as on Xbox)", false},
    {@"HeliPadReticle", "GTAIV_HELI_PAD_RETICLE", @"First-person helicopter reticle with a gamepad", true},
    {@"AutoLadders", "GTAIV_AUTO_LADDERS", @"Climb ladders by walking into them", false},
    {@"WheelStuntCamera", "GTAIV_WHEEL_STUNT_CAMERA", @"Stunt jumps: mouse wheel sets the slow motion", true},
    {@"VehicleNightShadows", "GTAIV_VEHICLE_NIGHT_SHADOWS", @"Vehicles cast shadows from street lights (needs FusionFix shaders)"},
    {@"EmissiveNoDepthWrite", "GTAIV_EMISSIVE_NO_DEPTH_WRITE", @"Glowing lights and signs without depth artifacts"},
    {@"DynamicShadowsEverywhere", "GTAIV_DYNAMIC_SHADOWS_EVERYWHERE", @"Every light casts moving shadows (very slow)", false},
    {@"CarShadowAnyDistance", "GTAIV_CAR_SHADOW_ANY_DISTANCE", @"Shadow under cars at any distance"},
    {@"NoStaticCarShadows", "GTAIV_NO_STATIC_CAR_SHADOWS", @"No shadow patch under cars", false},
    {@"NoCutsceneStaticCarShadows", "GTAIV_NO_CUTSCENE_STATIC_CAR_SHADOWS", @"No shadow patch under cars in cutscenes", false},
    {@"HighDetailReflections", "GTAIV_HIGH_DETAIL_REFLECTIONS", @"High detail reflections (heavy for the GPU)", false},
    {@"ShakeBlurFix", "GTAIV_SHAKE_BLUR_FIX", @"No motion blur flicker while the camera shakes (drunk)"},
    {@"ExtraStreamingMemory", "GTAIV_EXTRA_STREAMING_MEMORY", @"Extra streaming memory while there is room", false},
    {@"MenuExtraInfo", "GTAIV_MENU_EXTRA_INFO", @"Pause menu: version, IMG files and streaming memory", false},
    {@"GlassShardColours", "GTAIV_GLASS_SHARD_COLOURS", @"Red and amber glass from broken tail lights and indicators"},
    {@"BulletTracers", "GTAIV_BULLET_TRACERS", @"Tracer on every shot", false},
    {@"TBoGTTracerFx", "GTAIV_TBOGT_TRACER_FX", @"TBoGT: tracers drawn in the right place"},
    {@"PauseInfoSpacing", "GTAIV_PAUSE_INFO_SPACING", @"Pause menu time and money spaced as on consoles"},
    {@"CompareStringFix", "GTAIV_COMPARE_STRING_FIX", @"No script crash comparing missing strings (TLAD calls)"},
    {@"LongMenuLists", "GTAIV_LONG_MENU_LISTS", @"Menu lists show 32 rows before scrolling"},
    {@"DeathMusic", "GTAIV_DEATH_MUSIC", @"Death and arrest music (cut on PC)", false},
    {@"EditorFreeCamera", "GTAIV_EDITOR_FREE_CAMERA", @"Video editor: free camera without bounds"},
    {@"ErrorsToLog", "GTAIV_ERRORS_TO_LOG", @"Game errors only to the log, no error box (may crash later)", false},
    {@"NoPhysicsSleep", "GTAIV_NO_PHYSICS_SLEEP", @"Cars roll to a stop instead of freezing (costs CPU)", false},
    {@"TurnIndicators", "GTAIV_TURN_INDICATORS", @"Turn indicators when steering, hazard lights after a crash", false},
    {@"TurnIndicatorsManual", "GTAIV_TURN_INDICATORS_MANUAL", @"Turn indicators by hand: [ and ] or bumpers, both for hazards", false},
    {@"KeepWheelsTurned", "GTAIV_KEEP_WHEELS_TURNED", @"Parked vehicles keep their wheels turned", false},
    {@"FFToneMapping", "GTAIV_FF_TONE_MAPPING", @"FusionFix shaders: tone mapping (highlights not clipped)", false},
    {@"FFNoBloomColorShift", "GTAIV_FF_NO_BLOOM_COLOR_SHIFT", @"FusionFix shaders: bloom keeps its colour with tone mapping"},
    {@"FFAutoExposure", "GTAIV_FF_AUTO_EXPOSURE", @"FusionFix shaders: exposure adapts as on consoles"},
    {@"FFDefinition", "GTAIV_FF_DEFINITION", @"FusionFix shaders: definition (sharper image)"},
    {@"FFUnclampLighting", "GTAIV_FF_UNCLAMP_LIGHTING", @"FusionFix shaders: unclamped sunlight", false},
    {@"FFSmoothShorelines", "GTAIV_FF_SMOOTH_SHORELINES", @"FusionFix shaders: smooth shorelines"},
    {@"FFSmoothLightVolumes", "GTAIV_FF_SMOOTH_LIGHT_VOLUMES", @"FusionFix shaders: smooth light volumes"},
    {@"FFCascadeShadows", "GTAIV_FF_CASCADE_SHADOWS", @"FusionFix shaders: cascaded shadow fixes (atlas, ranges)"},
    {@"DrivebyFovFix", "GTAIV_DRIVEBY_FOV_FIX", @"Drive-by shots land in the crosshair when high up"},
    {@"AlwaysRun", "GTAIV_ALWAYS_RUN", @"Always run (tap sprint: run or jog, hold: sprint)", false},
    {@"ExtendedSniper", "GTAIV_EXTENDED_SNIPER", @"Sniper rifles aim over the shoulder; Jump toggles the scope", false},
    {@"TreeShadows", "GTAIV_TREE_SHADOWS", @"Trees and bushes cast real shadows (needs FusionFix shaders)"},
    {@"NoImpostorHeightCutoff", "GTAIV_NO_IMPOSTOR_HEIGHT_CUTOFF", @"Distant traffic on every road at any height"},
    {@"FFHighResShadows", "GTAIV_FF_HIGH_RES_SHADOWS", @"FusionFix shaders: high resolution cascaded shadows (heavy)", false},
    {@"FFVolumetricFog", "GTAIV_FF_VOLUMETRIC_FOG", @"FusionFix shaders: volumetric fog", false},
    {@"OutOfCommissionFix", "GTAIV_OUT_OF_COMMISSION_FIX", @"Final mission: helicopter climb not harder at high frame rates"},
    {@"PhoneCheats", "GTAIV_PHONE_CHEATS", @"Phone cheats: beta gloves (458-555-0100), beta hair (288-555-0100)"},
    {@"AltDialogue", "GTAIV_ALT_DIALOGUE", @"Missions play the alternative car banter", false},
    {@"AimProjectileFix", "GTAIV_AIM_PROJECTILE_FIX", @"No crash aiming thrown weapons with a ped that has no pad"},
};

// Advanced, FusionFix's shader settings: a popup each, the item's index
// as the variable's value (ff_shaders.cpp reads 0-n). They take effect
// with FusionFix's shaders (GTAIV_FF_SHADERS).
struct ShaderChoice {
  NSString* key;
  const char* env;
  NSString* title;
  NSArray<NSString*>* items;
  NSInteger def;
};
const ShaderChoice kShaderChoices[] = {
    {@"FFShadowFilter", "GTAIV_FF_SHADOW_FILTER", @"Shadow filter:", @[ @"Sharp", @"Soft", @"Contact hardening" ], 0},
    {@"FFTreeLighting", "GTAIV_FF_TREE_LIGHTING", @"Tree lighting:", @[ @"PC", @"PC+", @"Console" ], 1},
    {@"FFTreeAlpha", "GTAIV_FF_TREE_ALPHA", @"Tree leaves:", @[ @"PC", @"PC+", @"Console" ], 0},
    {@"FFMotionBlur", "GTAIV_MOTION_BLUR", @"Motion blur:", @[ @"Off", @"1/8", @"1/4", @"1/2", @"Full" ], 2},
};

bool HasGame(NSString* dir) {
  return dir.length &&
         [NSFileManager.defaultManager fileExistsAtPath:[dir stringByAppendingPathComponent:@"GTAIV.exe"]];
}

// Whether the restored radio payload has been imported (radio.cpp reads it
// from the port's data directory: $XDG_DATA_HOME/gtaiv, else
// ~/Library/Application Support/gtaiv - the runtime's DataDir).
bool HasRadioPayload() {
  NSString* root;
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x)
    root = @(x);
  else
    root = [NSFileManager.defaultManager.homeDirectoryForCurrentUser.path
                stringByAppendingPathComponent:@"Library/Application Support"];
  return [NSFileManager.defaultManager
      fileExistsAtPath:[root stringByAppendingPathComponent:@"gtaiv/drive_c/radio-restored/pc/audio/config/sounds.dat15"]];
}

// The arguments this process was given: a game folder to start with and
// the game's own (after "--"), passed on.
struct Args {
  NSString* game_dir = nil;
  std::vector<std::string> game_args;
};

Args ParseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      a.game_dir = @(argv[++i]);
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) a.game_args.push_back(argv[i]);
    }
  }
  return a;
}

// Runs the game: this executable again, with the options in its
// environment. Returns only if that failed.
void Launch(NSString* folder, NSUserDefaults* d, const Args& args) {
  setenv("GTAIV_FULLSCREEN", [d boolForKey:kWindowKey] ? "window" : "desktop", 1);
  switch ([d integerForKey:kVsyncKey]) {
    case 1: setenv("GTAIV_VSYNC", "1", 1); break;
    case 2: setenv("GTAIV_VSYNC", "0", 1); break;
    default: unsetenv("GTAIV_VSYNC"); break;
  }
  // The Metal renderer (its anti-aliasing, upscalers and fixes): without
  // GTAIV_GFX the runtime takes its generic SDL GPU path, much slower.
  setenv("GTAIV_GFX", "metal", 0);
  switch ([d integerForKey:kAntiAliasingKey]) {
    case 1: setenv("RECOMP_UPSCALER", "smaa", 1); break;
    case 2:
    case 4: {
      // MetalFX upscales: the scene drawn at this share of the screen.
      static const char* const kScales[] = {"1.0", "0.67", "0.58", "0.5"};
      setenv("RECOMP_UPSCALER", [d integerForKey:kAntiAliasingKey] == 4 ? "metalfx-spatial" : "metalfx", 1);
      setenv("RECOMP_RENDER_SCALE", kScales[std::clamp<NSInteger>([d integerForKey:kMetalFXScaleKey], 0, 3)], 1);
      break;
    }
    case 3: setenv("RECOMP_UPSCALER", "taa", 1); break;
    default: unsetenv("RECOMP_UPSCALER"); break;
  }
  static const char* const kSharpness[] = {"0", "0.3", "0.5", "0.8"};
  setenv("RECOMP_TAA_SHARPNESS", kSharpness[std::clamp<NSInteger>([d integerForKey:kTaaSharpeningKey], 0, 3)], 1);
  setenv("MTL_HUD_ENABLED", [d boolForKey:kHudKey] ? "1" : "0", 1);
  setenv("GTAIV_SKIP_INTRO", [d boolForKey:kSkipIntroKey] ? "1" : "0", 1);
  // Render targets made larger (RECOMP_RT_BOOST): the shadow maps (the
  // cascades' atlas 8192x2048, 2048 and 1024 maps), the reflections (512
  // and 256).
  std::string boost;
  if ([d integerForKey:kShadowsKey] == 1) boost += "8192x2048:2,2048x2048:2,1024x1024:2,";
  if ([d integerForKey:kReflectionsKey] == 1) boost += "512x512:2,256x256:2,";
  setenv("GTAIV_FAST_LOADING", [d boolForKey:kFastLoadingKey] ? "1" : "0", 1);
  const NSInteger fov = std::clamp<NSInteger>([d integerForKey:kFovKey], 0, 4);
  if (fov) setenv("GTAIV_FOV", std::to_string(fov * 5).c_str(), 1);
  else unsetenv("GTAIV_FOV");
  if (boost.empty()) unsetenv("RECOMP_RT_BOOST");
  else setenv("RECOMP_RT_BOOST", boost.substr(0, boost.size() - 1).c_str(), 1);
  setenv("GTAIV_CONTINUE", [d boolForKey:kContinueKey] ? "1" : "0", 1);
  setenv("GTAIV_RADIO", [d boolForKey:kRadioKey] ? "1" : "0", 1);
  setenv("GTAIV_STUN_PUNCH", [d boolForKey:kStunPunchKey] ? "1" : "0", 1);
  setenv("GTAIV_EXECUTIONS", [d boolForKey:kExecutionsKey] ? "1" : "0", 1);
  for (const Fix& f : kFixes) setenv(f.env, [d boolForKey:f.key] ? "1" : "0", 1);
  for (const ShaderChoice& sc : kShaderChoices) {
    const NSInteger v = std::clamp<NSInteger>([d integerForKey:sc.key], 0, NSInteger(sc.items.count) - 1);
    setenv(sc.env, std::to_string(v).c_str(), 1);
  }

  // The game's log: ~/Library/Logs/gtaiv.log unless given (main.cpp).
  setenv("GTAIV_LOG", [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/gtaiv.log"].fileSystemRepresentation, 0);

  // A new instance of the app started by Launch Services, as a double click
  // would: macOS then treats the game as the game it is (Game Mode). Run
  // again in this process (execv), it did not.
  NSMutableArray<NSString*>* arguments =
      [NSMutableArray arrayWithObjects:@"--no-launcher", @"--game-dir", folder, nil];
  if (!args.game_args.empty()) {
    [arguments addObject:@"--"];
    for (const std::string& a : args.game_args) [arguments addObject:@(a.c_str())];
  }
  NSWorkspaceOpenConfiguration* config = [NSWorkspaceOpenConfiguration configuration];
  config.createsNewApplicationInstance = YES;
  config.arguments = arguments;
  config.environment = NSProcessInfo.processInfo.environment;  // with the options set above
  config.activates = YES;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  __block NSError* failed = nil;
  [NSWorkspace.sharedWorkspace openApplicationAtURL:NSBundle.mainBundle.bundleURL
                                      configuration:config
                                  completionHandler:^(NSRunningApplication*, NSError* error) {
                                    failed = error;
                                    dispatch_semaphore_signal(done);
                                  }];
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  if (failed) std::fprintf(stderr, "gtaiv: launching the game: %s\n", failed.localizedDescription.UTF8String);
  std::fflush(nullptr);
  std::exit(failed ? 1 : 0);
}

}  // namespace

@interface GtaivLauncher : NSObject <NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTextField* folderField;
@property(strong) NSTextField* problem;
@property(strong) NSPopUpButton* display;
@property(strong) NSPopUpButton* vsync;
@property(strong) NSPopUpButton* antialiasing;
@property(strong) NSPopUpButton* metalfxScale;
@property(strong) NSPopUpButton* taaSharpening;
@property(strong) NSButton* hud;
@property(strong) NSButton* skipIntro;
@property(strong) NSButton* continueSave;
@property(strong) NSButton* originalRadio;
@property(strong) NSTextField* radioNote;
@property(strong) NSTabView* tabs;
@property(strong) NSPopUpButton* shadows;
@property(strong) NSPopUpButton* reflections;
@property(strong) NSPopUpButton* fov;
@property(strong) NSButton* fastLoading;
@property(strong) NSButton* stunPunch;
@property(strong) NSButton* executions;
@property(strong) NSMutableArray<NSButton*>* fixes;
@property(strong) NSMutableArray<NSPopUpButton*>* shaderChoices;
@property(strong) NSButton* skip;
@property(strong) NSButton* play;
@property(copy) NSString* folder;
@end

// A document view laid out from the top (the Advanced tab scrolls).
@interface GtaivFlippedView : NSView
@end
@implementation GtaivFlippedView
- (BOOL)isFlipped {
  return YES;
}
@end

@implementation GtaivLauncher

- (NSTextField*)label:(NSString*)text {
  NSTextField* t = [NSTextField labelWithString:text];
  t.alignment = NSTextAlignmentRight;
  return t;
}

- (NSPopUpButton*)popup:(NSArray<NSString*>*)items selected:(NSInteger)i {
  NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [p addItemsWithTitles:items];
  [p selectItemAtIndex:i];
  return p;
}

- (void)build:(NSUserDefaults*)d {
  self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 620, 560)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
  self.window.title = @"Liberty Pro Max";
  self.window.delegate = self;

  NSTextField* title = [NSTextField labelWithString:@"Liberty Pro Max"];
  title.font = [NSFont systemFontOfSize:22 weight:NSFontWeightBold];
  NSTextField* subtitle = [NSTextField labelWithString:@"Grand Theft Auto IV, running natively on your Mac"];
  subtitle.textColor = NSColor.secondaryLabelColor;

  self.folderField = [NSTextField labelWithString:@""];
  self.folderField.lineBreakMode = NSLineBreakByTruncatingMiddle;
  [self.folderField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                             forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSButton* choose = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(choose:)];
  NSStackView* folderRow = [NSStackView stackViewWithViews:@[ self.folderField, choose ]];

  self.display = [self popup:@[ @"Full screen", @"Window" ] selected:[d boolForKey:kWindowKey] ? 1 : 0];
  self.vsync = [self popup:@[ @"As set in the game", @"On", @"Off" ]
                  selected:std::clamp<NSInteger>([d integerForKey:kVsyncKey], 0, 2)];
  self.antialiasing = [self popup:@[
    @"Off", @"SMAA (light)", @"MetalFX (temporal)", @"TAA (temporal, light)", @"SMAA + MetalFX spatial (fast)"
  ]
                         selected:std::clamp<NSInteger>([d integerForKey:kAntiAliasingKey], 0, 4)];
  self.metalfxScale = [self popup:@[ @"Native (100%)", @"Quality (67%)", @"Balanced (58%)", @"Performance (50%)" ]
                         selected:std::clamp<NSInteger>([d integerForKey:kMetalFXScaleKey], 0, 3)];
  self.taaSharpening = [self popup:@[ @"Off", @"Low", @"Medium", @"High" ]
                          selected:std::clamp<NSInteger>([d integerForKey:kTaaSharpeningKey], 0, 3)];
  NSTextField* aaNote = [NSTextField
      labelWithString:@"SMAA smooths edges cheaply. TAA also steadies wires, fences and leaves at little cost; "
                      @"MetalFX does it best and, below native resolution, also raises the frame rate. "
                      @"SMAA + MetalFX spatial upscales without history: the fastest below native resolution."];
  aaNote.textColor = NSColor.secondaryLabelColor;
  aaNote.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  self.hud = [NSButton checkboxWithTitle:@"Show the performance overlay (frame rate, frame times)"
                                  target:nil
                                  action:nil];
  self.hud.state = [d boolForKey:kHudKey] ? NSControlStateValueOn : NSControlStateValueOff;

  self.skipIntro = [NSButton checkboxWithTitle:@"Skip the logo and legal screens" target:nil action:nil];
  self.skipIntro.state = [d boolForKey:kSkipIntroKey] ? NSControlStateValueOn : NSControlStateValueOff;
  self.continueSave = [NSButton checkboxWithTitle:@"Continue from the last save (no main menu)"
                                          target:nil
                                          action:nil];
  self.continueSave.state = [d boolForKey:kContinueKey] ? NSControlStateValueOn : NSControlStateValueOff;

  // The 2008 radio the 2018 update cut: served from the imported payload
  // while the game runs (radio.cpp); a no-op until it is imported.
  self.originalRadio = [NSButton checkboxWithTitle:@"Original 2008 radio songs (Vladivostok FM and the rest)"
                                           target:nil
                                           action:nil];
  self.originalRadio.state = [d boolForKey:kRadioKey] ? NSControlStateValueOn : NSControlStateValueOff;
  self.radioNote = [NSTextField
      labelWithString:HasRadioPayload()
          ? @"The songs the 2018 update cut, played from files outside your install."
          : @"Not ready yet: import the payload with tools/radio_import.sh first."];
  self.radioNote.textColor = HasRadioPayload() ? NSColor.secondaryLabelColor : NSColor.systemRedColor;
  self.radioNote.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];

  NSGridView* grid = [NSGridView gridViewWithViews:@[
    @[ [self label:@"Game folder:"], folderRow ],
    @[ [self label:@"Display:"], self.display ],
    @[ [self label:@"Vertical sync:"], self.vsync ],
    @[ [self label:@"Anti-aliasing:"], self.antialiasing ],
    @[ [self label:@"MetalFX resolution:"], self.metalfxScale ],
    @[ NSGridCell.emptyContentView, aaNote ],
    @[ [self label:@"TAA sharpening:"], self.taaSharpening ],
    @[ NSGridCell.emptyContentView, self.hud ],
    @[ [self label:@"Start:"], self.skipIntro ],
    @[ NSGridCell.emptyContentView, self.continueSave ],
    @[ [self label:@"Radio:"], self.originalRadio ],
    @[ NSGridCell.emptyContentView, self.radioNote ],
  ]];
  grid.rowSpacing = 10;
  grid.columnSpacing = 10;
  [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  grid.rowAlignment = NSGridRowAlignmentFirstBaseline;

  // Advanced: the port's improvements, each on its own.
  self.shadows = [self popup:@[ @"As in the game", @"High (twice the resolution)" ]
                    selected:std::clamp<NSInteger>([d integerForKey:kShadowsKey], 0, 1)];
  NSTextField* shadowNote = [NSTextField labelWithString:@"Sharper shadow edges; about 3 ms of GPU time a frame."];
  self.reflections = [self popup:@[ @"As in the game", @"High (twice the resolution)" ]
                        selected:std::clamp<NSInteger>([d integerForKey:kReflectionsKey], 0, 1)];
  NSTextField* reflNote = [NSTextField labelWithString:@"Sharper reflections on water and cars."];
  self.fov = [self popup:@[ @"As in the game", @"+5°", @"+10°", @"+15°", @"+20°" ]
                selected:std::clamp<NSInteger>([d integerForKey:kFovKey], 0, 4)];
  NSTextField* fovNote = [NSTextField labelWithString:@"A wider view for wide and ultrawide screens."];
  self.fastLoading = [NSButton checkboxWithTitle:@"Fast loading" target:nil action:nil];
  self.fastLoading.state = [d boolForKey:kFastLoadingKey] ? NSControlStateValueOn : NSControlStateValueOff;
  NSTextField* loadNote = [NSTextField labelWithString:@"No idle waits while loading screens are up."];
  self.stunPunch = [NSButton checkboxWithTitle:@"Stun punch on anyone" target:nil action:nil];
  self.stunPunch.state = [d boolForKey:kStunPunchKey] ? NSControlStateValueOn : NSControlStateValueOff;
  self.executions = [NSButton checkboxWithTitle:@"Executions on anyone" target:nil action:nil];
  self.executions.state = [d boolForKey:kExecutionsKey] ? NSControlStateValueOn : NSControlStateValueOff;
  NSTextField* meleeNote = [NSTextField
      labelWithString:@"Moves the game keeps for a few missions: sneak up unarmed (or with a pistol), lock on, attack."];
  for (NSTextField* n : @[ shadowNote, reflNote, fovNote, loadNote, meleeNote ]) {
    n.textColor = NSColor.secondaryLabelColor;
    n.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  }
  NSMutableArray<NSArray<NSView*>*>* rows = [NSMutableArray arrayWithArray:@[
    @[ [self label:@"Shadows:"], self.shadows ],
    @[ NSGridCell.emptyContentView, shadowNote ],
    @[ [self label:@"Reflections:"], self.reflections ],
    @[ NSGridCell.emptyContentView, reflNote ],
    @[ [self label:@"Field of view:"], self.fov ],
    @[ NSGridCell.emptyContentView, fovNote ],
    @[ [self label:@"Loading:"], self.fastLoading ],
    @[ NSGridCell.emptyContentView, loadNote ],
    @[ [self label:@"Combat:"], self.stunPunch ],
    @[ NSGridCell.emptyContentView, self.executions ],
    @[ NSGridCell.emptyContentView, meleeNote ],
  ]];
  self.shaderChoices = [NSMutableArray new];
  for (const ShaderChoice& sc : kShaderChoices) {
    NSPopUpButton* p = [self popup:sc.items
                          selected:std::clamp<NSInteger>([d integerForKey:sc.key], 0, NSInteger(sc.items.count) - 1)];
    [self.shaderChoices addObject:p];
    [rows addObject:@[ [self label:sc.title], p ]];
  }
  NSTextField* shaderNote = [NSTextField labelWithString:@"With FusionFix's shaders (in the update folder)."];
  shaderNote.textColor = NSColor.secondaryLabelColor;
  shaderNote.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  [rows addObject:@[ NSGridCell.emptyContentView, shaderNote ]];
  self.fixes = [NSMutableArray new];
  for (const Fix& f : kFixes) {
    NSButton* b = [NSButton checkboxWithTitle:f.title target:nil action:nil];
    b.state = [d boolForKey:f.key] ? NSControlStateValueOn : NSControlStateValueOff;
    [self.fixes addObject:b];
    [rows addObject:@[ self.fixes.count == 1 ? [self label:@"Fixes:"] : NSGridCell.emptyContentView, b ]];
  }
  NSGridView* adv = [NSGridView gridViewWithViews:rows];
  adv.rowSpacing = 10;
  adv.columnSpacing = 10;
  [adv columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  adv.rowAlignment = NSGridRowAlignmentFirstBaseline;

  // The Advanced grid scrolls: it outgrows the tab.
  NSScrollView* advScroll = [NSScrollView new];
  advScroll.hasVerticalScroller = YES;
  advScroll.drawsBackground = NO;
  advScroll.borderType = NSNoBorder;
  GtaivFlippedView* advDoc = [GtaivFlippedView new];
  adv.translatesAutoresizingMaskIntoConstraints = NO;
  [advDoc addSubview:adv];
  advDoc.translatesAutoresizingMaskIntoConstraints = NO;
  advScroll.documentView = advDoc;
  [NSLayoutConstraint activateConstraints:@[
    [adv.leadingAnchor constraintEqualToAnchor:advDoc.leadingAnchor],
    [adv.topAnchor constraintEqualToAnchor:advDoc.topAnchor],
    [adv.bottomAnchor constraintEqualToAnchor:advDoc.bottomAnchor constant:-14],
    [advDoc.widthAnchor constraintEqualToAnchor:advScroll.contentView.widthAnchor],
  ]];

  self.tabs = [NSTabView new];
  for (NSArray* tab in @[ @[ @"General", grid ], @[ @"Advanced", advScroll ] ]) {
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:tab[0]];
    item.label = tab[0];
    NSView* page = [NSView new];
    NSView* g = tab[1];
    g.translatesAutoresizingMaskIntoConstraints = NO;
    [page addSubview:g];
    [NSLayoutConstraint activateConstraints:@[
      [g.leadingAnchor constraintEqualToAnchor:page.leadingAnchor constant:14],
      [g.topAnchor constraintEqualToAnchor:page.topAnchor constant:14],
    ]];
    if (g == advScroll)  // fills the page, scrolls inside
      [NSLayoutConstraint activateConstraints:@[
        [g.trailingAnchor constraintEqualToAnchor:page.trailingAnchor constant:-4],
        [g.bottomAnchor constraintEqualToAnchor:page.bottomAnchor constant:-4],
      ]];
    else
      [g.trailingAnchor constraintLessThanOrEqualToAnchor:page.trailingAnchor constant:-14].active = YES;
    item.view = page;
    [self.tabs addTabViewItem:item];
  }

  self.problem = [NSTextField labelWithString:@""];
  self.problem.textColor = NSColor.systemRedColor;

  self.skip = [NSButton checkboxWithTitle:@"Skip this window next time (hold Option at launch to see it again)"
                                   target:nil
                                   action:nil];
  self.skip.state = [d boolForKey:kSkipKey] ? NSControlStateValueOn : NSControlStateValueOff;

  NSButton* quit = [NSButton buttonWithTitle:@"Quit" target:self action:@selector(quit:)];
  quit.keyEquivalent = @"\e";
  self.play = [NSButton buttonWithTitle:@"Play" target:self action:@selector(play:)];
  self.play.keyEquivalent = @"\r";
  NSView* spacer = [NSView new];
  [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView* buttons = [NSStackView stackViewWithViews:@[ spacer, quit, self.play ]];

  NSStackView* all = [NSStackView stackViewWithViews:@[ title, subtitle, self.tabs, self.problem, self.skip, buttons ]];
  all.orientation = NSUserInterfaceLayoutOrientationVertical;
  all.alignment = NSLayoutAttributeLeading;
  all.spacing = 14;
  [all setCustomSpacing:4 afterView:title];
  [all setCustomSpacing:20 afterView:subtitle];
  all.edgeInsets = NSEdgeInsetsMake(24, 28, 22, 28);
  all.translatesAutoresizingMaskIntoConstraints = NO;
  NSView* content = self.window.contentView;
  [content addSubview:all];
  [NSLayoutConstraint activateConstraints:@[
    [all.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
    [all.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    [all.topAnchor constraintEqualToAnchor:content.topAnchor],
    [all.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
    [buttons.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-56],
    [self.tabs.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-56],
    [self.tabs.heightAnchor constraintEqualToConstant:360],
    [self.folderField.widthAnchor constraintGreaterThanOrEqualToConstant:300],
  ]];
  [self showFolder];
  [self.window center];
}

- (void)showFolder {
  self.folderField.stringValue = self.folder.length ? self.folder.stringByAbbreviatingWithTildeInPath : @"Not chosen";
  const bool ok = HasGame(self.folder);
  self.problem.stringValue = ok ? @"" : @"Choose the folder of your Grand Theft Auto IV (PC) install: the one with GTAIV.exe.";
  self.problem.hidden = ok;
  self.play.enabled = ok;
}

- (void)choose:(id)sender {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseDirectories = YES;
  panel.canChooseFiles = NO;
  panel.message = @"Choose the folder with GTAIV.exe";
  if (self.folder.length) panel.directoryURL = [NSURL fileURLWithPath:self.folder];
  if ([panel runModal] == NSModalResponseOK) {
    self.folder = panel.URL.path;
    [self showFolder];
  }
}

- (void)play:(id)sender {
  NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
  [d setObject:self.folder forKey:kFolderKey];
  [d setBool:self.display.indexOfSelectedItem == 1 forKey:kWindowKey];
  [d setInteger:self.vsync.indexOfSelectedItem forKey:kVsyncKey];
  [d setInteger:self.antialiasing.indexOfSelectedItem forKey:kAntiAliasingKey];
  [d setInteger:self.metalfxScale.indexOfSelectedItem forKey:kMetalFXScaleKey];
  [d setInteger:self.taaSharpening.indexOfSelectedItem forKey:kTaaSharpeningKey];
  [d setBool:self.hud.state == NSControlStateValueOn forKey:kHudKey];
  [d setBool:self.skipIntro.state == NSControlStateValueOn forKey:kSkipIntroKey];
  [d setBool:self.continueSave.state == NSControlStateValueOn forKey:kContinueKey];
  [d setBool:self.originalRadio.state == NSControlStateValueOn forKey:kRadioKey];
  [d setInteger:self.shadows.indexOfSelectedItem forKey:kShadowsKey];
  [d setInteger:self.reflections.indexOfSelectedItem forKey:kReflectionsKey];
  [d setInteger:self.fov.indexOfSelectedItem forKey:kFovKey];
  [d setBool:self.fastLoading.state == NSControlStateValueOn forKey:kFastLoadingKey];
  [d setBool:self.stunPunch.state == NSControlStateValueOn forKey:kStunPunchKey];
  [d setBool:self.executions.state == NSControlStateValueOn forKey:kExecutionsKey];
  for (NSUInteger i = 0; i < self.fixes.count; i++)
    [d setBool:self.fixes[i].state == NSControlStateValueOn forKey:kFixes[i].key];
  for (NSUInteger i = 0; i < self.shaderChoices.count; i++)
    [d setInteger:self.shaderChoices[i].indexOfSelectedItem forKey:kShaderChoices[i].key];
  [d setBool:self.skip.state == NSControlStateValueOn forKey:kSkipKey];
  [d synchronize];
  [NSApp stopModalWithCode:NSModalResponseOK];
}

- (void)quit:(id)sender {
  [NSApp stopModalWithCode:NSModalResponseCancel];
}

- (void)windowWillClose:(NSNotification*)n {
  [NSApp stopModalWithCode:NSModalResponseCancel];
}

@end

// main.cpp: before anything else. Returns if there is no launcher to show
// (not run from the app bundle, or --no-launcher); otherwise starts the
// game in a new process image or exits.
void RunLauncherIfWanted(int argc, char** argv) {
  for (int i = 1; i < argc; i++)
    if (!std::strcmp(argv[i], "--no-launcher")) return;
  @autoreleasepool {
    if (![NSBundle.mainBundle.bundlePath.pathExtension isEqualToString:@"app"]) return;
    const Args args = ParseArgs(argc, argv);
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    NSMutableDictionary* defaults =
        [NSMutableDictionary dictionaryWithDictionary:@{kHudKey : @YES, kSkipIntroKey : @YES, kFastLoadingKey : @YES, kRadioKey : @YES, kStunPunchKey : @YES, kExecutionsKey : @YES, kMetalFXScaleKey : @1, kTaaSharpeningKey : @2, kAntiAliasingKey : @3}];
    for (const Fix& f : kFixes) defaults[f.key] = @(f.on);
    for (const ShaderChoice& sc : kShaderChoices) defaults[sc.key] = @(sc.def);
    [d registerDefaults:defaults];
    if ([d objectForKey:kMetalFXKey] && ![d objectForKey:kAntiAliasingKey])  // the older on/off setting
      [d setInteger:[d boolForKey:kMetalFXKey] ? 2 : 0 forKey:kAntiAliasingKey];
    // The folder: given, else the last one, else the repository's run/game
    // next to the build ("out/build/<preset>/Liberty Pro Max.app").
    NSString* folder = args.game_dir ?: [d stringForKey:kFolderKey];
    if (!HasGame(folder)) {
      NSString* repo = [[[NSBundle.mainBundle.bundlePath stringByDeletingLastPathComponent]
          stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
      NSString* guess = [repo.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"run/game"];
      if (HasGame(guess)) folder = guess;
    }

    const bool option = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
    if ([d boolForKey:kSkipKey] && HasGame(folder) && !option) {
      Launch(folder, d, args);
      std::exit(1);
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSMenu* bar = [NSMenu new];
    NSMenuItem* appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu* appMenu = [NSMenu new];
    [appMenu addItemWithTitle:@"Quit Liberty Pro Max" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    NSApp.mainMenu = bar;

    GtaivLauncher* l = [GtaivLauncher new];
    l.folder = folder;
    [l build:d];
    // Dev aids: GTAIV_LAUNCHER_SNAPSHOT=file.png draws the window there
    // (unshown) and exits; GTAIV_LAUNCHER_AUTOPLAY=1 presses Play.
    if (const char* snap = std::getenv("GTAIV_LAUNCHER_SNAPSHOT")) {
      NSView* v = l.window.contentView;
      [l.window layoutIfNeeded];
      v.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
      NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
      NSGraphicsContext* ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
      [NSGraphicsContext saveGraphicsState];
      NSGraphicsContext.currentContext = ctx;
      [NSColor.windowBackgroundColor setFill];
      NSRectFill(v.bounds);
      [NSGraphicsContext restoreGraphicsState];
      [v displayRectIgnoringOpacity:v.bounds inContext:ctx];
      [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(snap) atomically:YES];
      if (const char* snap2 = std::getenv("GTAIV_LAUNCHER_SNAPSHOT_ADVANCED")) {
        [l.tabs selectTabViewItemAtIndex:1];
        [l.window layoutIfNeeded];
        NSBitmapImageRep* rep2 = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        NSGraphicsContext* ctx2 = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep2];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = ctx2;
        [NSColor.windowBackgroundColor setFill];
        NSRectFill(v.bounds);
        [NSGraphicsContext restoreGraphicsState];
        [v displayRectIgnoringOpacity:v.bounds inContext:ctx2];
        [[rep2 representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(snap2) atomically:YES];
      }
      std::exit(0);
    }
    if (const char* a = std::getenv("GTAIV_LAUNCHER_AUTOPLAY"); a && *a == '1') {
      Launch(l.folder, d, args);
      std::exit(1);
    }
    [l.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    const NSModalResponse r = [NSApp runModalForWindow:l.window];
    if (r != NSModalResponseOK) std::exit(0);
    [l.window orderOut:nil];
    Launch(l.folder, d, args);
    NSAlert* a = [NSAlert new];
    a.messageText = @"The game could not be started.";
    a.informativeText = @(std::strerror(errno));
    [a runModal];
    std::exit(1);
  }
}
