// Saves shared between the Mac and the player's Apple TV
// (GTAIV_SAVE_SYNC, the launcher's "Saves synced" option, on): before the
// game starts (the launcher) and after it quits (main.cpp), the Mac finds
// its paired devices with this port's app (a bundle id with
// "libertypromax") through Xcode's devicectl, by cable or network, and per
// save file (savegames/user_*/SGTA4nn and ControlMap.dat) the newer copy
// wins: copied to the side that has an older one or none. A Mac copy about
// to be replaced is kept first in <data>/save-backups/<time>/. Files are
// never deleted (devicectl cannot). Without Xcode, or with no device in
// reach, nothing happens.

#import <Foundation/Foundation.h>

#include <cstdio>
#include <cstdlib>

#include "save_paths.h"

namespace {

NSString* const kSavesRel = @(save_paths::kSavesRel);

void Say(NSString* s) { std::fprintf(stderr, "save sync: %s\n", s.UTF8String); }

// The port's data folder (the runtime's DataDir on macOS).
NSString* DataDir() {
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return [@(x) stringByAppendingPathComponent:@"gtaiv"];
  return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/gtaiv"];
}

// xcrun devicectl ARGS --json-output FILE: its "result", or nil.
NSDictionary* Devicectl(NSArray<NSString*>* args) {
  NSString* json = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSTask* t = [NSTask new];
  t.executableURL = [NSURL fileURLWithPath:@"/usr/bin/xcrun"];
  NSMutableArray* a = [NSMutableArray arrayWithObject:@"devicectl"];
  [a addObjectsFromArray:args];
  [a addObjectsFromArray:@[ @"--timeout", @"20", @"--quiet", @"--json-output", json ]];
  t.arguments = a;
  t.standardOutput = NSFileHandle.fileHandleWithNullDevice;
  t.standardError = NSFileHandle.fileHandleWithNullDevice;
  if (![t launchAndReturnError:nil]) return nil;
  [t waitUntilExit];
  NSData* d = [NSData dataWithContentsOfFile:json];
  [NSFileManager.defaultManager removeItemAtPath:json error:nil];
  if (t.terminationStatus != 0 || !d) return nil;
  NSDictionary* j = [NSJSONSerialization JSONObjectWithData:d options:0 error:nil];
  return [j isKindOfClass:NSDictionary.class] ? j[@"result"] : nil;
}

bool DevicectlCopy(NSArray<NSString*>* args) {
  return Devicectl(args) != nil;
}

bool IsSave(NSString* rel) {
  NSString* name = rel.lastPathComponent;
  return rel.pathComponents.count == 2 && save_paths::IsProfileFolder(rel.pathComponents[0].UTF8String) &&
         ([name hasPrefix:@"SGTA4"] || [name isEqualToString:@"ControlMap.dat"]);
}

// The saves on the Mac: relative path -> modification time.
NSDictionary<NSString*, NSDate*>* LocalSaves(NSString* root) {
  NSMutableDictionary* out = [NSMutableDictionary dictionary];
  NSDirectoryEnumerator* e = [NSFileManager.defaultManager enumeratorAtPath:root];
  for (NSString* rel in e) {
    if (!IsSave(rel)) continue;
    NSDictionary* attrs = [NSFileManager.defaultManager attributesOfItemAtPath:[root stringByAppendingPathComponent:rel]
                                                                         error:nil];
    if ([attrs.fileType isEqualToString:NSFileTypeRegular]) out[rel] = attrs.fileModificationDate;
  }
  return out;
}

void SyncDevice(NSString* device, NSString* name, NSString* app, NSString* deviceRoot, NSString* localRoot,
                NSString* backups) {
  NSDictionary* listing = Devicectl(@[ @"device", @"info", @"files", @"--device", device, @"--domain-type",
                                       @"appDataContainer", @"--domain-identifier", app, @"--subdirectory",
                                       deviceRoot ]);
  NSMutableDictionary<NSString*, NSDate*>* remote = [NSMutableDictionary dictionary];
  NSISO8601DateFormatter* iso = [NSISO8601DateFormatter new];
  iso.formatOptions = NSISO8601DateFormatWithInternetDateTime | NSISO8601DateFormatWithFractionalSeconds;
  for (NSDictionary* f in listing[@"files"]) {
    NSString* rel = f[@"relativePath"];
    if (![f[@"resources"][@"isDirectory"] boolValue] && IsSave(rel)) {
      NSString* when = f[@"metadata"][@"lastModDate"];
      NSDate* d = [iso dateFromString:when];
      if (!d) d = [[NSISO8601DateFormatter new] dateFromString:when];  // without fractions
      if (d) remote[rel] = d;
    }
  }
  // (No listing: the app has no saves folder yet, as on a new device.)
  NSDictionary<NSString*, NSDate*>* local = LocalSaves(localRoot);
  NSMutableSet* all = [NSMutableSet setWithArray:local.allKeys];
  [all addObjectsFromArray:remote.allKeys];
  NSFileManager* fm = NSFileManager.defaultManager;
  for (NSString* rel in all) {
    NSDate* l = local[rel];
    NSDate* r = remote[rel];
    NSString* path = [localRoot stringByAppendingPathComponent:rel];
    // Seconds only: devicectl lists whole seconds.
    const double lt = l ? floor(l.timeIntervalSince1970) : 0, rt = r ? floor(r.timeIntervalSince1970) : 0;
    if (rt > lt + 1) {  // the device's is newer: to the Mac
      if (l) {
        NSString* keep = [backups stringByAppendingPathComponent:rel];
        [fm createDirectoryAtPath:keep.stringByDeletingLastPathComponent withIntermediateDirectories:YES
                       attributes:nil error:nil];
        [fm copyItemAtPath:path toPath:keep error:nil];
      }
      NSString* tmp = [path stringByAppendingString:@".sync"];
      [fm createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES
                     attributes:nil error:nil];
      if (DevicectlCopy(@[ @"device", @"copy", @"from", @"--device", device, @"--domain-type",
                           @"appDataContainer", @"--domain-identifier", app, @"--source",
                           [deviceRoot stringByAppendingPathComponent:rel], @"--destination", tmp ])) {
        [fm removeItemAtPath:path error:nil];
        [fm moveItemAtPath:tmp toPath:path error:nil];
        [fm setAttributes:@{NSFileModificationDate : r} ofItemAtPath:path error:nil];
        Say([NSString stringWithFormat:@"%@ from %@", rel, name]);
      } else {
        [fm removeItemAtPath:tmp error:nil];
        Say([NSString stringWithFormat:@"%@ from %@: copy failed", rel, name]);
      }
    } else if (lt > rt + 1) {  // the Mac's is newer: to the device
      if (DevicectlCopy(@[ @"device", @"copy", @"to", @"--device", device, @"--domain-type",
                           @"appDataContainer", @"--domain-identifier", app, @"--source", path,
                           @"--destination", [deviceRoot stringByAppendingPathComponent:rel] ]))
        Say([NSString stringWithFormat:@"%@ to %@", rel, name]);
      else
        Say([NSString stringWithFormat:@"%@ to %@: copy failed", rel, name]);
    }
  }
}

}  // namespace

// Blocking; `when` is for the log ("before the game", "after the game").
extern "C" void GtaivSyncSaves(const char* when) {
  const char* on = std::getenv("GTAIV_SAVE_SYNC");
  if (on && *on == '0') return;
  @autoreleasepool {
    if (![NSFileManager.defaultManager isExecutableFileAtPath:@"/usr/bin/xcrun"]) return;
    NSDictionary* list = Devicectl(@[ @"list", @"devices" ]);
    if (!list) return;  // no Xcode
    NSString* localRoot = [DataDir() stringByAppendingPathComponent:kSavesRel];
    NSDateFormatter* stamp = [NSDateFormatter new];
    stamp.dateFormat = @"yyyy-MM-dd_HH-mm-ss";
    NSString* backups = [[DataDir() stringByAppendingPathComponent:@"save-backups"]
        stringByAppendingPathComponent:[stamp stringFromDate:NSDate.date]];
    Say([NSString stringWithFormat:@"%s", when]);
    for (NSDictionary* dev in list[@"devices"]) {
      NSString* platform = dev[@"hardwareProperties"][@"platform"];
      if (![dev[@"hardwareProperties"][@"reality"] isEqual:@"physical"] ||
          ![platform isEqual:@"tvOS"] ||  // (the iPad: through iCloud, save_cloud.mm)
          [dev[@"connectionProperties"][@"tunnelState"] isEqual:@"unavailable"])
        continue;
      NSString* device = dev[@"identifier"];
      NSString* name = dev[@"deviceProperties"][@"name"] ?: device;
      NSDictionary* apps = Devicectl(@[ @"device", @"info", @"apps", @"--device", device ]);
      for (NSDictionary* a in apps[@"apps"]) {
        NSString* bundle = a[@"bundleIdentifier"];
        if ([bundle rangeOfString:@"libertypromax"].location == NSNotFound) continue;
        // Its data: Documents/gtaiv on the iPad, Library/Caches/gtaiv on Apple TV.
        NSString* data = [platform isEqual:@"tvOS"] ? @"Library/Caches/gtaiv" : @"Documents/gtaiv";
        SyncDevice(device, name, bundle, [data stringByAppendingPathComponent:kSavesRel], localRoot, backups);
      }
    }
  }
}
