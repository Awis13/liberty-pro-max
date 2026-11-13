#include "launcher_mobile_options.h"
#include <cstdio>
#include <cstring>

int main() {
  @autoreleasepool {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",what); failures++; } };
    NSString* suite = [@"launcher-mobile-test-" stringByAppendingString:NSUUID.UUID.UUIDString];
    NSUserDefaults* d = [[NSUserDefaults alloc] initWithSuiteName:suite];
    RegisterMobileDefaults(d, true);
    check([d integerForKey:kTvResolutionKey] == 2,"TV keeps 1080p default");
    check([d integerForKey:kTvFrameLimitKey] == 3,"TV keeps uncapped default");
    check([d integerForKey:kAntiAliasingKey] == 1,"TV keeps SMAA");
    check(![d boolForKey:kHudKey],"HUD follows its setting, not forced");
    check(MobileGameRows(false).count == MobileGameRows(true).count + 1,"TV omits touch controls only");
    bool language = false;
    for (NSArray* row in MobileGameRows(true)) {
      check(![row[1] isEqual:kTouchKey],"no touch option on TV");
      language |= [row[1] isEqual:kSystemLanguageKey];
    }
    check(language,"TV shares language selector");
    check(kTvResolutions[2].w == 1920 && kTvResolutions[2].h == 1080,"TV index maps to 1080p");
    for (const auto& r : kTvResolutions) check(r.w * 9 == r.h * 16,"TV modes are 16:9");
    for (NSInteger i = 0; i < MobileFixCount(true); i++)
      check(![MobileFix(i,true).key isEqual:@"SaveSync"],"no unsupported iCloud save sync on TV");
    [d setInteger:1 forKey:kTvResolutionKey];
    [d setInteger:1 forKey:kTvFrameLimitKey];
    [d setBool:YES forKey:kHudKey];
    [d setBool:YES forKey:kSystemLanguageKey];
    RegisterMobileDefaults(d,true);
    check([d integerForKey:kTvResolutionKey] == 1,"saved choices survive defaults");
    ApplyMobileOptions(d,true,60);
    check(std::strcmp(getenv("GTAIV_FPS_LIMIT"),"40") == 0,"40 FPS reaches runtime");
    check(std::strcmp(getenv("MTL_HUD_ENABLED"),"1") == 0,"HUD reaches runtime");
    check(getenv("RECOMP_METAL_HUD") && std::strcmp(getenv("RECOMP_METAL_HUD"),"1") == 0,
          "TV HUD selection reaches the layer even after Metal initialized in the launcher");
    [d setBool:NO forKey:kHudKey];
    ApplyMobileOptions(d,true,60);
    check(getenv("RECOMP_METAL_HUD") && std::strcmp(getenv("RECOMP_METAL_HUD"),"0") == 0,
          "TV HUD Off explicitly disables the layer overlay");
    check(std::strcmp(getenv("GTAIV_SYSTEM_LANGUAGE"),"1") == 0,"system language setting reaches runtime");
    check(std::strcmp(getenv("GTAIV_TOUCH"),"0") == 0,"touch remains off on TV");
    [d setInteger:3 forKey:kTvFrameLimitKey];
    ApplyMobileOptions(d,true,60);
    check(getenv("GTAIV_FPS_LIMIT") == nullptr,"Off clears an earlier cap");
    check(MobileFrameLimit(0,120) == 60 && MobileFrameLimit(0,60) == 0,"60 follows refresh behavior");
    check(MobileFrameLimit(2,60) == 30 && MobileFrameLimit(3,120) == 0,"30 and Off map correctly");
    // Every visible boolean reaches the runtime in both directions. This
    // also checks separate tab controllers sharing the same defaults.
    for (NSArray* row in MobileGameRows(true)) {
      NSString* key = row[1];
      if ([key isEqual:kSystemLanguageKey]) continue;
      const char* env = [key isEqual:kContinueKey] ? "GTAIV_CONTINUE"
          : [key isEqual:kSkipIntroKey] ? "GTAIV_SKIP_INTRO"
          : [key isEqual:kModsKey] ? "GTAIV_MODS"
          : [key isEqual:kFastLoadingKey] ? "GTAIV_FAST_LOADING"
          : [key isEqual:kStunPunchKey] ? "GTAIV_STUN_PUNCH" : "GTAIV_EXECUTIONS";
      for (bool on : {false,true}) {
        [d setBool:on forKey:key];
        ApplyMobileOptions(d,true,60);
        check(getenv(env) && std::strcmp(getenv(env),on ? "1" : "0") == 0,env);
      }
    }
    for (const Fix& f : kFixes) {
      if (!f.extra && !MobileFixShown(f,true)) continue;
      for (bool on : {false,true}) {
        [d setBool:on forKey:f.key];
        ApplyMobileOptions(d,true,60);
        check(getenv(f.env) && std::strcmp(getenv(f.env),on ? "1" : "0") == 0,f.env);
      }
    }
    [d removePersistentDomainForName:suite];
    NSString* install = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    [NSFileManager.defaultManager createDirectoryAtPath:install withIntermediateDirectories:YES attributes:nil error:nil];
    NSString* status = [install stringByAppendingPathComponent:@".gtaiv-transfer-status"];
    check(!MobileTransferInProgress(install),"existing manual installs need no transfer marker");
    [@"copying\n" writeToFile:status atomically:YES encoding:NSUTF8StringEncoding error:nil];
    check(MobileTransferInProgress(install),"partial transfer blocks Play");
    [@"" writeToFile:status atomically:YES encoding:NSUTF8StringEncoding error:nil];
    check(MobileTransferInProgress(install),"truncated marker blocks Play");
    [@"ready\n" writeToFile:status atomically:YES encoding:NSUTF8StringEncoding error:nil];
    check(!MobileTransferInProgress(install),"completed transfer allows Play");
    [NSFileManager.defaultManager removeItemAtPath:install error:nil];
    return failures ? 1 : 0;
  }
}
