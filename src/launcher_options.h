// The port's options, shared by the Mac launcher (launcher_mac.mm) and the
// iPad's (launcher_ios.mm): their keys in the user defaults, the community
// fixes and FusionFix shader choices, the defaults, and the environment
// variables the game reads them from (ApplyOptions). Included by one
// source file per target.
#pragma once

#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace {

NSString* const kFolderKey = @"GameFolder";
NSString* const kWindowKey = @"Windowed";
NSString* const kTouchKey = @"TouchControls";
NSString* const kOverNotchKey = @"FullScreenOverNotch";
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
// The player's mods (mods.cpp).
NSString* const kModsKey = @"Mods";
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
  bool hidden = false;  // needs the extended shaders (GTAIV_FF_SHADERS, not finished): not shown
};
const Fix kFixes[] = {
    {@"FixLadders", "GTAIV_LADDER_FIX", @"Ladders: no stalls before climbing at high frame rates"},
    {@"FixDrawDistance", "GTAIV_DRAW_DISTANCE_FIX", @"Draw distance settings apply from the start (costs fps)", false},
    {@"FixRadio", "GTAIV_RADIO_FIX", @"Radio keeps playing after cutscenes"},
    {@"InstantMenus", "GTAIV_INSTANT_MENUS", @"Pause menu opens and closes without delay"},
    {@"VehicleBudget", "GTAIV_VEHICLE_BUDGET", @"More kinds of cars in traffic (no streets full of taxis)"},
    {@"BiggerPools", "GTAIV_BIGGER_POOLS", @"Twice the game's limits for people's actions and object lists"},
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
    {@"PlayerFullDamage", "GTAIV_PLAYER_FULL_DAMAGE", @"Full damage: the player takes bullet damage ×3, explosion, fire and thrown object damage ×1.5", false},
    {@"SaveHealthCap", "GTAIV_SAVE_HEALTH_CAP", @"Health from a save at most a full bar (trainer saves)"},
    {@"SaveSync", "GTAIV_SAVE_SYNC", @"Saves synced with your iPad and Apple TV at start and quit (needs Xcode)"},
    {@"RadarZoomDelay", "GTAIV_RADAR_ZOOM_DELAY", @"Radar stays zoomed out for 3 seconds"},
    {@"TrainLanes", "GTAIV_TRAIN_LANES", @"Cars do not swerve under trains"},
    {@"DateFix", "GTAIV_DATE_FIX", @"Date does not go back after dying or arrest"},
    {@"NoMemRestrict", "GTAIV_NO_MEM_RESTRICT", @"City not held back from loading by the game's memory limit"},
    {@"TimerFloor", "GTAIV_TIMER_FLOOR", @"No speed-up above 300 fps"},
    {@"HeliCeiling", "GTAIV_HELI_CEILING", @"Helicopters fly up to 770 m (as in TBoGT)", false},
    {@"PhysicsStep", "GTAIV_PHYSICS_STEP", @"Vehicle and ragdoll physics steady at high frame rates"},
    {@"SlideObject", "GTAIV_SLIDE_OBJECT", @"Scripted doors and gates move at the right speed"},
    {@"ShakeFix", "GTAIV_SHAKE_FIX", @"Camera shake as strong at any frame rate"},
    {@"CameraShake", "GTAIV_CAMERA_SHAKE", @"Keep camera shake (hand-held, drunk, vehicles)"},
    {@"CutsceneCamera", "GTAIV_CUTSCENE_CAMERA", @"Cutscene cameras steady, without sudden zooms or flicker at cuts"},
    {@"CutsceneClock", "GTAIV_CUTSCENE_CLOCK", @"Cutscene animations smooth and in sync with their audio"},
    {@"HeliRotorDamage", "GTAIV_HELI_ROTOR_DAMAGE", @"Helicopter rotors break as easily at any frame rate"},
    {@"LoadingAnimation", "GTAIV_LOADING_ANIMATION", @"Loading screen pictures move at console speed"},
    {@"WaterFlip", "GTAIV_WATER_FLIP", @"No water flicker"},
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
    {@"MirrorOffset", "GTAIV_MIRROR_OFFSET", @"Mirrors show graffiti and other marks near them"},
    {@"WaterTiling", "GTAIV_WATER_TILING", @"Water surface tiling as on consoles"},
    {@"MouseCameraDelay", "GTAIV_MOUSE_CAMERA_DELAY", @"On foot, the camera waits 2 s after the mouse before recentring"},
    {@"CoronaLimit", "GTAIV_CORONA_LIMIT", @"No flickering lights where there are many"},
    {@"CopHats", "GTAIV_COP_HATS", @"Cops take off their hats in vehicles (as on consoles)"},
    {@"CounterDeathAnim", "GTAIV_COUNTER_DEATH_ANIM", @"No double death animation after a melee counter"},
    {@"ReversingLights", "GTAIV_REVERSING_LIGHTS", @"Reversing lights at the back of the car"},
    {@"BikeFeet", "GTAIV_BIKE_FEET", @"Bikes: feet down when stopped on the phone and when reversing"},
    {@"SniperMovement", "GTAIV_SNIPER_MOVEMENT", @"Walk while zoomed in with a sniper rifle"},
    {@"RainLighting", "GTAIV_RAIN_LIGHTING", @"Rain lit as on consoles (visible at night)"},
    {@"HeliSearchlights", "GTAIV_HELI_SEARCHLIGHTS", @"Police helicopter searchlights do not flicker"},
    {@"NightShadowResolution", "GTAIV_NIGHT_SHADOW_RESOLUTION", @"The Night Shadows setting changes how sharp night shadows are"},
    {@"CenteredCarCamera", "GTAIV_CENTERED_CAR_CAMERA", @"Car camera centred behind the car", false},
    {@"CenteredFootCamera", "GTAIV_CENTERED_FOOT_CAMERA", @"On-foot camera centred behind the player", false},
    {@"BrakeLightsStopped", "GTAIV_BRAKE_LIGHTS_STOPPED", @"Brake lights stay on while your car stands", false},
    {@"Bloom", "GTAIV_BLOOM", @"Keep bloom (glow around bright lights)"},
    {@"DepthOfField", "GTAIV_DEPTH_OF_FIELD", @"Keep depth of field blur"},
    {@"PadCameraDelay", "GTAIV_PAD_CAMERA_DELAY", @"On foot with a gamepad: camera waits 2 s before recentring", false},
    {@"CarCameraDelay", "GTAIV_CAR_CAMERA_DELAY", @"In vehicles: camera waits 2 s before recentring", false},
    {@"AimZoomMemory", "GTAIV_AIM_ZOOM_MEMORY", @"Aiming zoom remembered between aims (as on Xbox)", false},
    {@"HeliPadReticle", "GTAIV_HELI_PAD_RETICLE", @"First-person helicopter reticle with a gamepad", true},
    {@"AutoLadders", "GTAIV_AUTO_LADDERS", @"Climb ladders by walking into them", false},
    {@"WheelStuntCamera", "GTAIV_WHEEL_STUNT_CAMERA", @"Stunt jumps: mouse wheel sets the slow motion", true},
    {@"VehicleNightShadows", "GTAIV_VEHICLE_NIGHT_SHADOWS", @"Vehicles cast shadows from street lights (needs FusionFix shaders)", true, true},
    {@"EmissiveNoDepthWrite", "GTAIV_EMISSIVE_NO_DEPTH_WRITE", @"The glow of lamps and signs does not cut into what is in front of it"},
    {@"SignGlowFix", "GTAIV_SIGN_GLOW_FIX", @"Neon signs glow from every angle"},
    {@"DynamicShadowsEverywhere", "GTAIV_DYNAMIC_SHADOWS_EVERYWHERE", @"Every light casts moving shadows (very slow)", false},
    {@"CarShadowAnyDistance", "GTAIV_CAR_SHADOW_ANY_DISTANCE", @"Shadow under cars at any distance"},
    {@"NoStaticCarShadows", "GTAIV_NO_STATIC_CAR_SHADOWS", @"No shadow patch under cars", false},
    {@"NoCutsceneStaticCarShadows", "GTAIV_NO_CUTSCENE_STATIC_CAR_SHADOWS", @"No shadow patch under cars in cutscenes", false},
    {@"HighDetailReflections", "GTAIV_HIGH_DETAIL_REFLECTIONS", @"High detail reflections (heavy for the GPU)", false},
    {@"ShakeBlurFix", "GTAIV_SHAKE_BLUR_FIX", @"No motion blur flicker while the camera shakes (drunk)"},
    {@"ExtraStreamingMemory", "GTAIV_EXTRA_STREAMING_MEMORY", @"More memory for loading the city", false},
    {@"MenuExtraInfo", "GTAIV_MENU_EXTRA_INFO", @"Pause menu shows the game's version, the archives open and memory use", false},
    {@"GlassShardColours", "GTAIV_GLASS_SHARD_COLOURS", @"Red and amber glass from broken tail lights and indicators"},
    {@"BulletTracers", "GTAIV_BULLET_TRACERS", @"Tracer on every shot", false},
    {@"Episodes", "GTAIV_EPISODES", @"The episodes, when their folders are in the game's"},
    {@"TBoGTTracerFx", "GTAIV_TBOGT_TRACER_FX", @"TBoGT: tracers drawn in the right place"},
    {@"PauseInfoSpacing", "GTAIV_PAUSE_INFO_SPACING", @"Pause menu time and money spaced as on consoles"},
    {@"CompareStringFix", "GTAIV_COMPARE_STRING_FIX", @"No crash on phone calls in The Lost and Damned"},
    {@"LongMenuLists", "GTAIV_LONG_MENU_LISTS", @"Menu lists show 32 rows before scrolling"},
    {@"DeathMusic", "GTAIV_DEATH_MUSIC", @"Death and arrest music (cut on PC)", false},
    {@"EditorFreeCamera", "GTAIV_EDITOR_FREE_CAMERA", @"Video editor: free camera without bounds"},
    {@"ErrorsToLog", "GTAIV_ERRORS_TO_LOG", @"Game errors only to the log, no error box (may crash later)", false},
    {@"NoPhysicsSleep", "GTAIV_NO_PHYSICS_SLEEP", @"Cars roll to a stop instead of freezing (costs CPU)", false},
    {@"TurnIndicators", "GTAIV_TURN_INDICATORS", @"Turn indicators when steering, hazard lights after a crash", false},
    {@"TurnIndicatorsManual", "GTAIV_TURN_INDICATORS_MANUAL", @"Turn indicators by hand: [ and ] or bumpers, both for hazards", false},
    {@"RouteIndicators", "GTAIV_TURN_INDICATORS_ROUTE", @"Route indicators: with a GPS route, the turn indicator comes on before the turn and towards the kerb on arriving (needs Turn indicators, not by hand)", false},
    {@"KeepWheelsTurned", "GTAIV_KEEP_WHEELS_TURNED", @"Parked vehicles keep their wheels turned", false},
    {@"FFToneMapping", "GTAIV_FF_TONE_MAPPING", @"FusionFix shaders: tone mapping (highlights not clipped)", false, true},
    {@"FFNoBloomColorShift", "GTAIV_FF_NO_BLOOM_COLOR_SHIFT", @"FusionFix shaders: bloom keeps its colour with tone mapping", true, true},
    {@"FFAutoExposure", "GTAIV_FF_AUTO_EXPOSURE", @"FusionFix shaders: exposure adapts as on consoles", true, true},
    {@"FFDefinition", "GTAIV_FF_DEFINITION", @"FusionFix shaders: definition (sharper image)", true, true},
    {@"FFUnclampLighting", "GTAIV_FF_UNCLAMP_LIGHTING", @"FusionFix shaders: unclamped sunlight", false, true},
    {@"FFSmoothShorelines", "GTAIV_FF_SMOOTH_SHORELINES", @"FusionFix shaders: smooth shorelines", true, true},
    {@"FFSmoothLightVolumes", "GTAIV_FF_SMOOTH_LIGHT_VOLUMES", @"FusionFix shaders: smooth light volumes", true, true},
    {@"FFCascadeShadows", "GTAIV_FF_CASCADE_SHADOWS", @"FusionFix shaders: cascaded shadow fixes (atlas, ranges)", true, true},
    {@"DrivebyFovFix", "GTAIV_DRIVEBY_FOV_FIX", @"Drive-by shots land in the crosshair when high up"},
    {@"AlwaysRun", "GTAIV_ALWAYS_RUN", @"Always run (tap sprint: run or jog, hold: sprint)", false},
    {@"ExtendedSniper", "GTAIV_EXTENDED_SNIPER", @"Sniper rifles aim over the shoulder; Jump toggles the scope", false},
    {@"TreeShadows", "GTAIV_TREE_SHADOWS", @"Trees and bushes cast real shadows (needs FusionFix shaders)", true, true},
    {@"NoImpostorHeightCutoff", "GTAIV_NO_IMPOSTOR_HEIGHT_CUTOFF", @"Distant traffic on every road at any height"},
    {@"FFHighResShadows", "GTAIV_FF_HIGH_RES_SHADOWS", @"FusionFix shaders: high resolution cascaded shadows (heavy)", false, true},
    {@"FFVolumetricFog", "GTAIV_FF_VOLUMETRIC_FOG", @"FusionFix shaders: volumetric fog", false, true},
    {@"OutOfCommissionFix", "GTAIV_OUT_OF_COMMISSION_FIX", @"Final mission: helicopter climb not harder at high frame rates"},
    {@"PhoneCheats", "GTAIV_PHONE_CHEATS", @"Phone cheats: beta gloves (458-555-0100), beta hair (288-555-0100)"},
    {@"AltDialogue", "GTAIV_ALT_DIALOGUE", @"Missions play the alternative car banter", false},
    {@"AimProjectileFix", "GTAIV_AIM_PROJECTILE_FIX", @"No crash when someone other than the player aims a thrown weapon"},
};

// The options that need the extended shaders (Fix::hidden): not shown, as
// those shaders are off (GTAIV_FF_SHADERS, work in progress) and the
// options would do nothing.
inline bool FixShown(const Fix& f) {
  return !f.hidden;
}
// The iPad's Fixes list (launcher_ios.mm): the shown fixes but the Mac's
// own, keeping running behind other windows (iOS suspends an app that is
// not in front).
inline bool IpadFixShown(const Fix& f) {
  return FixShown(f) && ![f.key isEqualToString:@"RunInBackground"];
}
inline NSInteger ShownFixCount() {
  NSInteger n = 0;
  for (const Fix& f : kFixes) n += IpadFixShown(f);
  return n;
}
inline const Fix& ShownFix(NSInteger row) {
  for (const Fix& f : kFixes)
    if (IpadFixShown(f) && row-- == 0) return f;
  return kFixes[0];
}

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

// The mods' folder (mods.cpp reads it from the port's data directory:
// $XDG_DATA_HOME/gtaiv, else ~/Library/Application Support/gtaiv - the
// runtime's DataDir).
NSString* ModsFolder() {
  NSString* root;
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x)
    root = @(x);
  else
    root = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support"];
  return [root stringByAppendingPathComponent:@"gtaiv/drive_c/mods"];
}

// The defaults of every option (the player's choices override them).
void RegisterDefaults(NSUserDefaults* d) {
  NSMutableDictionary* defaults =
      [NSMutableDictionary dictionaryWithDictionary:@{kHudKey : @YES, kTouchKey : @YES, kSkipIntroKey : @YES, kFastLoadingKey : @YES, kModsKey : @YES, kStunPunchKey : @YES, kExecutionsKey : @YES, kContinueKey : @YES, kMetalFXScaleKey : @1, kTaaSharpeningKey : @2, kAntiAliasingKey : @3}];
  for (const Fix& f : kFixes) defaults[f.key] = @(f.on);
  for (const ShaderChoice& sc : kShaderChoices) defaults[sc.key] = @(sc.def);
  [d registerDefaults:defaults];
}

// The options as the game reads them: environment variables of this process.
void ApplyOptions(NSUserDefaults* d) {
  setenv("GTAIV_FULLSCREEN", [d boolForKey:kWindowKey] ? "window" : "desktop", 1);
  setenv("GTAIV_TOUCH", [d boolForKey:kTouchKey] ? "1" : "0", 1);
  setenv("GTAIV_OVER_NOTCH", [d boolForKey:kOverNotchKey] ? "1" : "0", 1);
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
  setenv("GTAIV_MODS", [d boolForKey:kModsKey] ? "1" : "0", 1);
  setenv("GTAIV_STUN_PUNCH", [d boolForKey:kStunPunchKey] ? "1" : "0", 1);
  setenv("GTAIV_EXECUTIONS", [d boolForKey:kExecutionsKey] ? "1" : "0", 1);
  for (const Fix& f : kFixes) setenv(f.env, [d boolForKey:f.key] ? "1" : "0", 1);
  for (const ShaderChoice& sc : kShaderChoices) {
    const NSInteger v = std::clamp<NSInteger>([d integerForKey:sc.key], 0, NSInteger(sc.items.count) - 1);
    setenv(sc.env, std::to_string(v).c_str(), 1);
  }

}

}  // namespace
