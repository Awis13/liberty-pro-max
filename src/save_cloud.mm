// Saves through iCloud Drive (GTAIV_SAVE_SYNC, on): the iPad app's
// iCloud container (shown in iCloud Drive as "Liberty Pro Max") holds a
// copy of every save, and each device, Mac and iPad, keeps its own saves
// and that copy in step:
//   - at start, before the game reads its saves: newer copies come down;
//   - while it runs, every 20 s, and when it quits: newer saves go up.
// Per file (savegames/user_*/SGTA4nn and ControlMap.dat) the newer one wins
// by the time it was written on the device that wrote it, kept in a sidecar
// (<file>.json: time, SHA-256, device) because iCloud does not carry file
// times; a copy whose content does not match its sidecar yet (still on its
// way) is left for the next round. The loser of every replacement, on
// either side, goes to Backups/<time> <device>/ in the container first, so
// no save is lost to a clash. Nothing is ever deleted.
//
// The container: the iPad app's own (its entitlement); on the Mac, the
// folder iCloud Drive keeps for it under ~/Library/Mobile Documents
// (GTAIV_ICLOUD_CONTAINER: its name, e.g. iCloud~io~github~you~libertypromax;
// default: the one there with "libertypromax" in its name).

#import <CommonCrypto/CommonDigest.h>
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {

NSString* const kSavesRel =
    @"drive_c/Documents and Settings/Player/Local Settings/Application Data/Rockstar Games/GTA IV/savegames";

void Say(NSString* s) { std::fprintf(stderr, "save cloud: %s\n", s.UTF8String); }

bool On() {
  const char* e = std::getenv("GTAIV_SAVE_SYNC");
  return !(e && *e == '0');
}

// The port's data folder: XDG_DATA_HOME/gtaiv (set on iOS by main.cpp), or
// the Mac's Application Support/gtaiv.
NSString* DataDir() {
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return [@(x) stringByAppendingPathComponent:@"gtaiv"];
  return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/gtaiv"];
}

NSString* DeviceName() {
#if TARGET_OS_OSX
  return NSHost.currentHost.localizedName ?: @"Mac";
#else
  return NSProcessInfo.processInfo.hostName ?: @"iPad";
#endif
}

// The container's Documents folder, or nil (no iCloud, or not signed in).
NSString* CloudDir() {
  static NSString* dir = [] () -> NSString* {
    NSFileManager* fm = NSFileManager.defaultManager;
    if (const char* t = std::getenv("GTAIV_ICLOUD_DIR"); t && *t) return @(t);  // tests: any folder
#if TARGET_OS_OSX
    NSString* mobile = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Mobile Documents"];
    NSString* name = nil;
    if (const char* e = std::getenv("GTAIV_ICLOUD_CONTAINER"); e && *e) name = @(e);
    if (!name)
      for (NSString* n in [fm contentsOfDirectoryAtPath:mobile error:nil])
        if ([n rangeOfString:@"libertypromax"].location != NSNotFound) name = n;
    if (!name) return nil;
    return [[mobile stringByAppendingPathComponent:name] stringByAppendingPathComponent:@"Documents"];
#else
    NSURL* u = [fm URLForUbiquityContainerIdentifier:nil];  // slow the first time: not on the main thread
    return u ? [u.path stringByAppendingPathComponent:@"Documents"] : nil;
#endif
  }();
  return dir;
}

NSString* Sha256(NSString* path) {
  NSData* d = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil];
  if (!d) return nil;
  unsigned char h[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(d.bytes, CC_LONG(d.length), h);
  NSMutableString* s = [NSMutableString string];
  for (unsigned char c : h) [s appendFormat:@"%02x", c];
  return s;
}

bool IsSave(NSString* rel) {
  NSString* name = rel.lastPathComponent;
  return [rel hasPrefix:@"user_"] && rel.pathComponents.count == 2 &&
         ([name hasPrefix:@"SGTA4"] || [name isEqualToString:@"ControlMap.dat"]) && ![name hasSuffix:@".json"];
}

NSDate* MTime(NSString* path) {
  return [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil].fileModificationDate;
}

// A cloud copy's sidecar: its write time and hash, or nil when the copy
// and its sidecar do not match (yet).
NSDictionary* CloudMeta(NSString* file) {
  NSData* d = [NSData dataWithContentsOfFile:[file stringByAppendingString:@".json"]];
  NSDictionary* m = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
  if (![m isKindOfClass:NSDictionary.class] || !m[@"time"] || !m[@"sha256"]) return nil;
  if (![Sha256(file) isEqualToString:m[@"sha256"]]) return nil;
  return m;
}

// Not yet on this device: iCloud keeps a placeholder; ask for the file.
void Fetch(NSString* file) {
#if !TARGET_OS_OSX
  NSFileManager* fm = NSFileManager.defaultManager;
  NSURL* u = [NSURL fileURLWithPath:file];
  [fm startDownloadingUbiquitousItemAtURL:u error:nil];
  [fm startDownloadingUbiquitousItemAtURL:[NSURL fileURLWithPath:[file stringByAppendingString:@".json"]] error:nil];
#else
  (void)file;  // the Mac's iCloud Drive reads dataless files in on access
#endif
}

void Backup(NSString* cloud, NSString* path, NSString* rel, NSString* side) {
  static NSString* stamp = [] {
    NSDateFormatter* f = [NSDateFormatter new];
    f.dateFormat = @"yyyy-MM-dd HH-mm-ss";
    return [f stringFromDate:NSDate.date];
  }();
  NSString* dest = [[[cloud stringByAppendingPathComponent:@"Backups"]
      stringByAppendingPathComponent:[NSString stringWithFormat:@"%@ %@ (%@)", stamp, DeviceName(), side]]
      stringByAppendingPathComponent:rel];
  NSFileManager* fm = NSFileManager.defaultManager;
  [fm createDirectoryAtPath:dest.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil
                      error:nil];
  [fm removeItemAtPath:dest error:nil];
  [fm copyItemAtPath:path toPath:dest error:nil];
}

// Copies `from` over `to` through a temporary file next to it.
bool Replace(NSString* from, NSString* to) {
  NSFileManager* fm = NSFileManager.defaultManager;
  [fm createDirectoryAtPath:to.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil
                      error:nil];
  NSString* tmp = [to stringByAppendingString:@".part"];
  [fm removeItemAtPath:tmp error:nil];
  if (![fm copyItemAtPath:from toPath:tmp error:nil]) return false;
  [fm removeItemAtPath:to error:nil];
  return [fm moveItemAtPath:tmp toPath:to error:nil];
}

std::mutex g_mu;

// iCloud renames a folder or file two devices create at once ("user_x 2",
// "SGTA401 2"): every such name is the same save as the one without the
// number.
NSString* Canonical(NSString* rel) {
  static NSRegularExpression* re = [NSRegularExpression regularExpressionWithPattern:@" [0-9]+$" options:0 error:nil];
  NSMutableArray* parts = [NSMutableArray array];
  for (NSString* p in rel.pathComponents) {
    NSString* base = p, *ext = @"";
    if ([p hasSuffix:@".json"]) {
      base = [p substringToIndex:p.length - 5];
      ext = @".json";
    }
    base = [re stringByReplacingMatchesInString:base options:0 range:NSMakeRange(0, base.length) withTemplate:@""];
    [parts addObject:[base stringByAppendingString:ext]];
  }
  return [NSString pathWithComponents:parts];
}

// One round: `down` takes newer cloud copies, `up` sends newer saves.
void Round(bool down, bool up) {
  std::lock_guard<std::mutex> lock(g_mu);
  @autoreleasepool {
    NSString* cloud = CloudDir();
    if (!cloud) return;
    NSString* cloudSaves = [cloud stringByAppendingPathComponent:@"Saves"];
    NSString* local = [DataDir() stringByAppendingPathComponent:kSavesRel];
    NSFileManager* fm = NSFileManager.defaultManager;

    // The cloud's copies of each save, under any of its names.
    NSMutableDictionary<NSString*, NSMutableArray<NSString*>*>* copies = [NSMutableDictionary dictionary];
    for (NSString* rel in [fm enumeratorAtPath:cloudSaves]) {
      NSString* r = rel;
      NSString* name = r.lastPathComponent;
      if ([name hasPrefix:@"."] && [name hasSuffix:@".icloud"]) {  // a placeholder: .NAME.icloud
        r = [r.stringByDeletingLastPathComponent
            stringByAppendingPathComponent:[name substringWithRange:NSMakeRange(1, name.length - 8)]];
        Fetch([cloudSaves stringByAppendingPathComponent:r]);
        continue;
      }
      NSString* c = Canonical(r);
      if (!IsSave(c)) continue;
      if (!copies[c]) copies[c] = [NSMutableArray array];
      [copies[c] addObject:[cloudSaves stringByAppendingPathComponent:r]];
    }
    NSMutableSet<NSString*>* rels = [NSMutableSet setWithArray:copies.allKeys];
    for (NSString* rel in [fm enumeratorAtPath:local])
      if (IsSave(rel) && [Canonical(rel) isEqualToString:rel]) [rels addObject:rel];

    for (NSString* rel in rels) {
      NSString* lp = [local stringByAppendingPathComponent:rel];
      NSString* canonicalCopy = [cloudSaves stringByAppendingPathComponent:rel];
      const bool haveLocal = [fm fileExistsAtPath:lp];
      // The newest complete cloud copy.
      NSString* cp = nil;
      NSDictionary* meta = nil;
      for (NSString* candidate in copies[rel]) {
        NSDictionary* m = CloudMeta(candidate);
        if (m && (!meta || [m[@"time"] doubleValue] > [meta[@"time"] doubleValue])) {
          meta = m;
          cp = candidate;
        }
      }
      const double lt = haveLocal ? floor(MTime(lp).timeIntervalSince1970) : 0;
      const double ct = meta ? [meta[@"time"] doubleValue] : 0;
      if (down && meta && ct > lt + 1) {
        if (haveLocal) Backup(cloud, lp, rel, @"replaced here");
        if (Replace(cp, lp)) {
          [fm setAttributes:@{NSFileModificationDate : [NSDate dateWithTimeIntervalSince1970:ct]} ofItemAtPath:lp
                      error:nil];
          Say([NSString stringWithFormat:@"%@ from iCloud (%@)", rel, meta[@"device"]]);
        }
      } else if (up && haveLocal && lt > ct + 1) {
        if (meta) Backup(cloud, cp, rel, @"replaced in iCloud");
        NSDictionary* m = @{@"time" : @(lt), @"sha256" : Sha256(lp) ?: @"", @"device" : DeviceName()};
        if (Replace(lp, canonicalCopy)) {
          // The save's own time on the copy as well: when two devices send
          // the same file before seeing each other's, iCloud keeps the one
          // modified last, the newer save; the other stays a conflict
          // version, and its sidecar no longer matching skips it.
          [fm setAttributes:@{NSFileModificationDate : [NSDate dateWithTimeIntervalSince1970:lt]}
               ofItemAtPath:canonicalCopy error:nil];
          [[NSJSONSerialization dataWithJSONObject:m options:0 error:nil]
              writeToFile:[canonicalCopy stringByAppendingString:@".json"] atomically:YES];
          Say([NSString stringWithFormat:@"%@ to iCloud", rel]);
        }
      }
    }
  }
}

std::atomic<bool> g_running{false};

}  // namespace

// Asks iCloud for every cloud copy not on this device yet and waits for
// them (at most `seconds`).
void FetchAll(double seconds) {
  NSString* cloudSaves = [CloudDir() stringByAppendingPathComponent:@"Saves"];
  NSFileManager* fm = NSFileManager.defaultManager;
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(long(seconds * 1000));
  for (;;) {
    int missing = 0;
    for (NSString* rel in [fm enumeratorAtPath:cloudSaves]) {
      NSString* name = rel.lastPathComponent;
      if (![name hasPrefix:@"."] || ![name hasSuffix:@".icloud"]) continue;
      NSString* real = [rel.stringByDeletingLastPathComponent
          stringByAppendingPathComponent:[name substringWithRange:NSMakeRange(1, name.length - 8)]];
      NSString* full = [cloudSaves stringByAppendingPathComponent:real];
      if (![fm fileExistsAtPath:full]) {
        missing++;
        [fm startDownloadingUbiquitousItemAtURL:[NSURL fileURLWithPath:full] error:nil];
      }
    }
    if (!missing || std::chrono::steady_clock::now() > end) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

// Before the game reads its saves: newer copies down (and any save made
// elsewhere since up). Blocking: at most about 15 s when iCloud is slow.
extern "C" void GtaivCloudSavesStart() {
  if (!On()) return;
  // (The container lookup can take a while the first time.)
  __block NSString* dir = nil;
  dispatch_semaphore_t got = dispatch_semaphore_create(0);
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    dir = CloudDir();
    dispatch_semaphore_signal(got);
  });
  if (dispatch_semaphore_wait(got, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0 || !dir) {
    Say(@"no iCloud container (iCloud Drive off, or the app without its iCloud entitlement)");
    return;
  }
  FetchAll(10);
  Round(true, true);
#if !TARGET_OS_OSX
  // The iPad app sent to the background may not come back: saves up then.
  [NSNotificationCenter.defaultCenter addObserverForName:@"UIApplicationDidEnterBackgroundNotification" object:nil
                                                   queue:nil usingBlock:^(NSNotification*) { Round(false, true); }];
#endif
  // While the game runs: new saves up every 20 s.
  if (!g_running.exchange(true))
    std::thread([] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        Round(false, true);
      }
    }).detach();
}

// When the game quits (or the iPad app goes to the background).
extern "C" void GtaivCloudSavesFlush() {
  if (On() && CloudDir()) Round(false, true);
}
