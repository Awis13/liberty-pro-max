// launcher_options (src/launcher_options.h): the Extras, their defaults,
// the lists they stay out of, what the Mac launcher clears once when it
// moves to storing only changed options, the sharpening as the runtime
// gets it, and the episodes' note for a game folder on disk.

#include "launcher_options.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "launcher_options_test.mm:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

const Fix* Find(NSString* key) {
  for (const Fix& f : kFixes)
    if ([f.key isEqualToString:key]) return &f;
  return nullptr;
}

// The Extras, in the order shown: off, but the episodes' own radio songs.
void TestExtras() {
  NSArray<NSString*>* want = @[
    @"Episodes", @"EpisodeRadioOwn", @"TurnIndicators", @"TurnIndicatorsManual", @"RouteIndicators", @"PadIcons",
    @"PadFeedback", @"PhoneCheats", @"PlayerFullDamage"
  ];
  EXPECT(ExtraCount() == NSInteger(want.count));
  for (NSInteger i = 0; i < ExtraCount() && i < NSInteger(want.count); i++) {
    const Fix& f = ExtraAt(i);
    EXPECT([f.key isEqualToString:want[i]]);
    EXPECT(f.on == [f.key isEqualToString:@"EpisodeRadioOwn"]);
    EXPECT(!f.hidden);
  }
}

// Extras are on neither the Advanced tab nor the iPad's Fixes list.
void TestListsApart() {
  NSInteger advanced = 0, ipad = 0;
  for (const Fix& f : kFixes) {
    if (f.extra) {
      EXPECT(!AdvancedFixShown(f));
      EXPECT(!IpadFixShown(f));
    }
    advanced += AdvancedFixShown(f);
    ipad += IpadFixShown(f);
  }
  EXPECT(ShownFixCount() == ipad);
  EXPECT(ipad == advanced - 1);  // the Mac's RunInBackground
  for (NSInteger i = 0; i < ShownFixCount(); i++) EXPECT(!ShownFix(i).extra);
  // Each key once.
  NSMutableSet* keys = [NSMutableSet set];
  for (const Fix& f : kFixes) {
    EXPECT(![keys containsObject:f.key]);
    [keys addObject:f.key];
  }
}

// The defaults registered for every option.
void TestDefaults() {
  NSUserDefaults* d = [[NSUserDefaults alloc] initWithSuiteName:@"launcher_options_test.unused"];
  RegisterDefaults(d);
  NSDictionary* defaults = [d volatileDomainForName:NSRegistrationDomain];
  EXPECT([defaults[@"Episodes"] isEqual:@NO]);
  EXPECT([defaults[@"PhoneCheats"] isEqual:@NO]);
  EXPECT([defaults[@"PlayerFullDamage"] isEqual:@NO]);
  EXPECT([defaults[@"EpisodeRadioOwn"] isEqual:@YES]);
  EXPECT([defaults[kModsKey] isEqual:@YES]);
  EXPECT([defaults[kSystemLanguageKey] isEqual:@YES]);  // the game in the system's language
  for (NSString* k in DefaultsTurnedOff()) {
    EXPECT(Find(k) != nullptr);
    EXPECT(Find(k) && !Find(k)->on);
  }
}

// The Apple TV's own, over those: with no settings screen there, the
// episodes are on; the phone cheats and the episodes' radio stay as
// everywhere. (The registration domain is the process's: this runs last.)
void TestTvDefaults() {
  NSUserDefaults* d = [[NSUserDefaults alloc] initWithSuiteName:@"launcher_options_test.unused"];
  RegisterDefaults(d);
  // Every key it sets is an option's: one RegisterDefaults registers, read
  // before TvDefaults is registered over them.
  NSDictionary* table = [[d volatileDomainForName:NSRegistrationDomain] copy];
  for (NSString* k in TvDefaults()) EXPECT(table[k] != nil);
  EXPECT([table[@"Episodes"] isEqual:@NO]);
  [d registerDefaults:TvDefaults()];
  EXPECT([d boolForKey:@"Episodes"]);
  EXPECT(![d boolForKey:@"PhoneCheats"]);
  EXPECT([d boolForKey:@"EpisodeRadioOwn"]);
  EXPECT(TvDefaults()[@"PhoneCheats"] == nil);
  EXPECT(TvDefaults()[@"EpisodeRadioOwn"] == nil);
}

// The sharpening: its key kept from when it was TAA's (the players'
// choices stay), medium by default, each level's amount in
// RECOMP_SHARPNESS whatever the anti-aliasing, and which anti-aliasing
// choices it applies to (the option greyed for the others).
void TestSharpening() {
  EXPECT([kTaaSharpeningKey isEqualToString:@"TaaSharpening"]);
  NSUserDefaults* d = [[NSUserDefaults alloc] initWithSuiteName:@"launcher_options_test.sharpening"];
  [d removePersistentDomainForName:@"launcher_options_test.sharpening"];
  RegisterDefaults(d);
  EXPECT([d integerForKey:kTaaSharpeningKey] == 2);
  const char* want[] = {"0", "0.5", "0.75", "0.9"};
  for (NSInteger aa : {0, 1, 2, 3, 4})
    for (NSInteger level = 0; level < 4; level++) {
      [d setInteger:aa forKey:kAntiAliasingKey];
      [d setInteger:level forKey:kTaaSharpeningKey];
      unsetenv("RECOMP_SHARPNESS");
      ApplyOptions(d);
      const char* got = std::getenv("RECOMP_SHARPNESS");
      EXPECT(got && std::strcmp(got, want[level]) == 0);
    }
  EXPECT(std::strcmp(SharpnessFor(-1), "0") == 0);
  EXPECT(std::strcmp(SharpnessFor(9), "0.9") == 0);
  EXPECT(!SharpeningApplies(0));  // off
  EXPECT(!SharpeningApplies(1));  // SMAA
  EXPECT(SharpeningApplies(2));   // MetalFX
  EXPECT(SharpeningApplies(3));   // TAA
  EXPECT(SharpeningApplies(4));   // SMAA + MetalFX spatial
  [d removePersistentDomainForName:@"launcher_options_test.sharpening"];
}

bool Has(NSArray* a, NSString* k) { return [a containsObject:k]; }

void TestLauncherLanguage() {
  NSString* suite = [@"launcher_options_test.language." stringByAppendingString:NSUUID.UUID.UUIDString];
  NSUserDefaults* d = [[NSUserDefaults alloc] initWithSuiteName:suite];
  RegisterDefaults(d);
  EXPECT(LauncherLanguageIndex(d) == 0);
  [d setBool:NO forKey:kSystemLanguageKey];
  EXPECT(LauncherLanguageIndex(d) == 1);  // migrate the old English-only preference
  [d setObject:@"system" forKey:kGameLanguageKey];
  EXPECT(LauncherLanguageIndex(d) == 0);  // a new choice supersedes the legacy boolean

  auto envIs = [](const char* key, const char* value) {
    const char* got = std::getenv(key);
    return got && std::strcmp(got, value) == 0;
  };
  const char* ids[] = {"", "0x0409", "0x040C", "0x0407", "0x0410", "0x0C0A", "0x0419"};
  EXPECT(LauncherLanguageTitles().count == 7);
  for (NSInteger i = 1; i < 7; ++i) {
    [d setObject:LauncherLanguageCode(i) forKey:kGameLanguageKey];
    ApplyOptions(d);
    ApplyLauncherLanguage(d);
    EXPECT(LauncherLanguageIndex(d) == i);
    EXPECT(envIs("GTAIV_LANG_ID", ids[i]));
    EXPECT(envIs("GTAIV_SYSTEM_LANGUAGE", "0"));
    EXPECT(envIs("GTAIV_LAUNCHER_LANGUAGE", "1"));
  }
  EXPECT([LauncherLanguageCode(6) isEqualToString:@"ru"]);
  // Returning from a manual choice must not leave Russian forced in the
  // environment passed to the next game process.
  [d setObject:@"system" forKey:kGameLanguageKey];
  ApplyLauncherLanguage(d);
  EXPECT(std::getenv("GTAIV_LANG_ID") == nullptr);
  EXPECT(std::getenv("GTAIV_LAUNCHER_LANGUAGE") == nullptr);
  EXPECT(envIs("GTAIV_SYSTEM_LANGUAGE", "1"));
  // An external diagnostic override remains supported in automatic mode.
  setenv("GTAIV_LANG_ID", "0x040C", 1);
  ApplyLauncherLanguage(d);
  EXPECT(envIs("GTAIV_LANG_ID", "0x040C"));
  for (id invalid in @[ @"unknown", @42 ]) {
    [d setObject:invalid forKey:kGameLanguageKey];
    EXPECT(LauncherLanguageIndex(d) == 0);
  }
  unsetenv("GTAIV_LANG_ID");
  [d removePersistentDomainForName:suite];
}

// The one move to storing only changes: what earlier versions stored on
// Play, the old defaults among it.
void TestClearedOnce() {
  NSDictionary* defaults = @{@"Episodes" : @NO, @"PhoneCheats" : @NO, @"PlayerFullDamage" : @NO,
                             @"EpisodeRadioOwn" : @YES, @"FixLadders" : @YES, @"AlwaysRun" : @NO};
  // Everything stored as the old version had it on Play: the episodes and
  // the phone cheats on (their old default), the player's own choices.
  NSDictionary* stored = @{@"Episodes" : @YES, @"PhoneCheats" : @YES, @"PlayerFullDamage" : @NO,
                           @"EpisodeRadioOwn" : @NO, @"FixLadders" : @YES, @"AlwaysRun" : @YES};
  NSArray* cleared = ClearedOnce(stored, defaults);
  EXPECT(Has(cleared, @"Episodes"));      // an old default: the new one
  EXPECT(Has(cleared, @"PhoneCheats"));   // the same
  EXPECT(Has(cleared, @"FixLadders"));    // equal to its default
  EXPECT(Has(cleared, @"PlayerFullDamage"));
  EXPECT(!Has(cleared, @"EpisodeRadioOwn"));  // turned off by the player: kept
  EXPECT(!Has(cleared, @"AlwaysRun"));        // turned on by the player: kept
  // Each key once, though both rules take it.
  NSUInteger n = 0;
  for (NSString* k in cleared) n += [k isEqualToString:@"PlayerFullDamage"];
  EXPECT(n == 1);
  // Nothing stored: nothing but the defaults themselves to clear.
  NSArray* none = ClearedOnce(defaults, defaults);
  for (NSString* k in none) EXPECT(defaults[k] != nil);
  EXPECT(!Has(ClearedOnce(@{}, defaults), @"Episodes"));
}

// The episodes' note for a game folder on disk: none for a folder without
// episodes or with complete ones; for an episode's folder with setup2.xml
// and no list, the folder and the file. Names are found without case.
void TestEpisodesNote() {
  NSFileManager* fm = NSFileManager.defaultManager;
  // A throwaway folder in the current directory (the build directory under ctest).
  NSString* game = [fm.currentDirectoryPath stringByAppendingPathComponent:@"launcher_options_test.game"];
  [fm removeItemAtPath:game error:nil];
  auto file = [&](NSString* rel) {
    NSString* path = [game stringByAppendingPathComponent:rel];
    [fm createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    EXPECT([fm createFileAtPath:path contents:[NSData data] attributes:nil]);
  };
  auto says = [](NSString* note, NSString* what) { return note && [note rangeOfString:what].location != NSNotFound; };
  EXPECT(EpisodesNote(nil) == nil);
  EXPECT(EpisodesNote(@"") == nil);
  EXPECT(EpisodesNote(game) == nil);  // no such folder
  file(@"GTAIV.exe");
  EXPECT(EpisodesNote(game) == nil);  // no episodes
  file(@"TLAD/setup2.xml");
  NSString* note = EpisodesNote(game);
  EXPECT(says(note, @"TLAD") && says(note, @"E1_List.txt") && !says(note, @"E2_List.txt"));
  // A folder of that name is not the file.
  [fm createDirectoryAtPath:[game stringByAppendingPathComponent:@"TLAD/E1_List.txt"] withIntermediateDirectories:YES attributes:nil error:nil];
  EXPECT(EpisodesNote(game) != nil);
  [fm removeItemAtPath:[game stringByAppendingPathComponent:@"TLAD/E1_List.txt"] error:nil];
  file(@"TLAD/e1_list.TXT");  // without case
  EXPECT(EpisodesNote(game) == nil);
  file(@"tbogt/SETUP2.XML");  // without case
  note = EpisodesNote(game);
  EXPECT(says(note, @"TBoGT") && says(note, @"E2_List.txt") && !says(note, @"E1_List.txt"));
  file(@"tbogt/E2_List.txt");
  EXPECT(EpisodesNote(game) == nil);
  [fm removeItemAtPath:game error:nil];
}

}  // namespace

int main() {
  @autoreleasepool {
    TestExtras();
    TestListsApart();
    TestDefaults();
    TestClearedOnce();
    TestSharpening();
    TestEpisodesNote();
    TestTvDefaults();
    TestLauncherLanguage();
  }
  if (g_failures) return 1;
  std::puts("launcher_options_test: ok");
  return 0;
}
