// launcher_options (src/launcher_options.h): the Extras, their defaults,
// the lists they stay out of, and what the Mac launcher clears once when it
// moves to storing only changed options.

#include "launcher_options.h"

#include <cstdio>

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
    @"PhoneCheats", @"PlayerFullDamage"
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
  for (NSString* k in DefaultsTurnedOff()) {
    EXPECT(Find(k) != nullptr);
    EXPECT(Find(k) && !Find(k)->on);
  }
}

// The Apple TV's own, over those: with no settings screen there, the
// episodes are on; the phone cheats and the episodes' radio stay as
// everywhere.
void TestTvDefaults() {
  NSUserDefaults* d = [[NSUserDefaults alloc] initWithSuiteName:@"launcher_options_test.unused"];
  RegisterDefaults(d);
  [d registerDefaults:TvDefaults()];
  EXPECT([d boolForKey:@"Episodes"]);
  EXPECT(![d boolForKey:@"PhoneCheats"]);
  EXPECT([d boolForKey:@"EpisodeRadioOwn"]);
  EXPECT(TvDefaults()[@"PhoneCheats"] == nil);
  EXPECT(TvDefaults()[@"EpisodeRadioOwn"] == nil);
  // Every key it sets is an option's.
  NSDictionary* all = [d volatileDomainForName:NSRegistrationDomain];
  for (NSString* k in TvDefaults()) EXPECT(all[k] != nil);
}

bool Has(NSArray* a, NSString* k) { return [a containsObject:k]; }

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

}  // namespace

int main() {
  @autoreleasepool {
    TestExtras();
    TestListsApart();
    TestDefaults();
    TestTvDefaults();
    TestClearedOnce();
  }
  if (g_failures) return 1;
  std::puts("launcher_options_test: ok");
  return 0;
}
