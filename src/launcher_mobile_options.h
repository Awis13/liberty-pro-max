#pragma once
// The same launcher settings on iPhone, iPad and Apple TV. UIKit supplies
// the controls; the model and environment mapping can be tested on the Mac.
#include "launcher_options.h"
#include <array>

namespace {
NSString* const kIosResolutionKey = @"IosResolution";
NSString* const kIosFrameLimitKey = @"IosFrameLimit";
NSString* const kTvResolutionKey = @"TvResolution";
NSString* const kTvFrameLimitKey = @"TvFrameLimit";
struct TvResolution { int w, h; };
constexpr std::array<TvResolution,3> kTvResolutions = {{{1280,720},{1600,900},{1920,1080}}};
inline NSString* MobileResolutionKey(bool tv) { return tv ? kTvResolutionKey : kIosResolutionKey; }
inline NSString* MobileFrameLimitKey(bool tv) { return tv ? kTvFrameLimitKey : kIosFrameLimitKey; }
// ios_push_game marks an interrupted or ongoing copy; installations made
// by other tools remain compatible when the marker is absent.
inline bool MobileTransferInProgress(NSString* gameRoot) {
  NSString* path = [gameRoot stringByAppendingPathComponent:@".gtaiv-transfer-status"];
  if (![NSFileManager.defaultManager fileExistsAtPath:path]) return false;
  NSString* status = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
  return ![[status stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]
      isEqualToString:@"ready"];
}
// Only offer the explicit recovery action when there is an executable to
// try. This does not claim the remaining payload is complete or remove the
// marker: a resumed uploader still owns its copying/ready transition.
inline bool MobileTransferRecoveryAvailable(NSString* gameRoot) {
  NSString* path = [gameRoot stringByAppendingPathComponent:@"GTAIV.exe"];
  NSDictionary* attributes = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil];
  return [attributes[NSFileType] isEqual:NSFileTypeRegular] && [attributes[NSFileSize] unsignedLongLongValue] > 0;
}
inline NSArray* MobileGameRows(bool tv) {
  NSMutableArray* rows = [NSMutableArray array];
  if (!tv) [rows addObject:@[@"Touch controls (hidden with a controller)",kTouchKey]];
  [rows addObjectsFromArray:@[
    @[@"Continue from the last save",kContinueKey], @[@"Skip the start screens",kSkipIntroKey],
    @[@"Game in the device's language (off: English)",kSystemLanguageKey], @[@"Mods",kModsKey], @[@"Fast loading",kFastLoadingKey],
    @[@"Stun punch on anyone",kStunPunchKey], @[@"Executions on anyone",kExecutionsKey]]];
  return rows;
}
inline NSArray* MobileGraphicsRows(bool tv, NSArray* resolutions) {
  return @[
    @[@"Resolution",MobileResolutionKey(tv),resolutions],
    @[@"Anti-aliasing",kAntiAliasingKey,@[@"Off",@"SMAA",@"TAA"]],
    @[@"Sharpening",kTaaSharpeningKey,@[@"Off",@"Low",@"Medium",@"High"]],
    @[@"Console gamma",kConsoleGammaKey,@[@"Off",@"Xbox 360",@"PS3"]],
    @[@"Frame limit",MobileFrameLimitKey(tv),@[@"60",@"40",@"30",@"Off"]],
    @[@"Shadows",kShadowsKey,@[@"Game's",@"High"]],
    @[@"Reflections",kReflectionsKey,@[@"Game's",@"High"]],
    @[@"Field of view",kFovKey,@[@"+0",@"+5",@"+10",@"+15",@"+20"]],
    @[@"Performance overlay",kHudKey,@[@"Off",@"On"]]];
}
inline bool MobileFixShown(const Fix& f, bool tv) {
  return IpadFixShown(f) && !(tv && [f.key isEqual:@"SaveSync"]);
}
inline NSInteger MobileFixCount(bool tv) {
  NSInteger n = 0;
  for (const Fix& f : kFixes) n += MobileFixShown(f,tv);
  return n;
}
inline const Fix& MobileFix(NSInteger index, bool tv) {
  for (const Fix& f : kFixes) if (MobileFixShown(f,tv) && index-- == 0) return f;
  return kFixes[0];
}
inline void RegisterMobileDefaults(NSUserDefaults* d, bool tv) {
  RegisterDefaults(d);
  if (tv) {
    [d registerDefaults:TvDefaults()];
    [d registerDefaults:@{kTvResolutionKey:@2,kTvFrameLimitKey:@3}];
  } else [d registerDefaults:@{kIosResolutionKey:@1,kIosFrameLimitKey:@0}];
}
inline int MobileFrameLimit(NSInteger choice, int refresh) {
  switch (std::clamp<NSInteger>(choice,0,3)) {
    case 1:return 40;
    case 2:return 30;
    case 3:return 0;
    default:return refresh > 60 ? 60 : 0;
  }
}
inline void ApplyMobileOptions(NSUserDefaults* d, bool tv, int refresh) {
  ApplyOptions(d);
  ApplyLauncherHud(d);
  // The TV launcher already used Metal to inspect/prewarm the cache.
  // The game layer therefore applies this explicit setting as well as
  // the launch-time MTL_HUD_ENABLED environment variable.
  if (tv) setenv("RECOMP_METAL_HUD", [d boolForKey:kHudKey] ? "1" : "0", 1);
  setenv("GTAIV_VSYNC","1",1);
  setenv("GTAIV_RUN_IN_BACKGROUND","0",1);
  if (tv) { setenv("GTAIV_TOUCH","0",1); setenv("GTAIV_SAVE_SYNC","0",1); }
  const int limit = MobileFrameLimit([d integerForKey:MobileFrameLimitKey(tv)],refresh);
  if (limit) setenv("GTAIV_FPS_LIMIT",std::to_string(limit).c_str(),1);
  else unsetenv("GTAIV_FPS_LIMIT");
}
}
