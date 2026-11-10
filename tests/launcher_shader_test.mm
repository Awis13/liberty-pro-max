// Process/UI lifecycle test. Its worker emits the real JSON protocol and waits
// for cancellation. Controls are exercised on AppKit's native event loops.
#import <AppKit/AppKit.h>
#import "shader_preparation_mac.h"
#import "../src/launcher_mac.mm"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#import <objc/runtime.h>

extern "C" void GtaivSyncSaves(const char*) {}

@interface GtaivShaderPreparation (PresentationTest)
- (void)consume:(NSData*)line;
- (void)setRunning:(BOOL)running;
@end

// Replace only the modal folder chooser; exercise the real acceptance handler.
static NSString* chosenFolder;
@interface FolderTestPanel : NSOpenPanel
@end
@implementation FolderTestPanel
- (NSModalResponse)runModal { return NSModalResponseOK; }
- (NSURL*)URL { return [NSURL fileURLWithPath:chosenFolder]; }
@end
static id TestOpenPanel(id, SEL) { return [FolderTestPanel new]; }

namespace {
volatile sig_atomic_t cancelled = 0;
void Cancel(int) { cancelled = 1; }
void Require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

void OptionEnvironmentTest() {
  RegisterDefaults(NSUserDefaults.standardUserDefaults);
  GtaivLauncher* launcher = [GtaivLauncher new];
  launcher.folder = @"/tmp";
  [launcher build:NSUserDefaults.standardUserDefaults];
  NSDictionary* (^environment)(void) = [launcher.preparation valueForKey:@"environment"];
  NSArray<NSString*>* scales = @[@"1.0", @"0.67", @"0.58", @"0.5"];
  for (NSInteger aa : {3, 0, 1}) {
    unsetenv("RECOMP_RENDER_SCALE");
    [launcher.antialiasing selectItemAtIndex:aa];
    NSDictionary* fresh = environment();
    Require(fresh[@"RECOMP_RENDER_SCALE"] == nil, "Fresh non-MetalFX settings must have no render scale");
    for (NSInteger metalFX : {2, 4}) {
      for (NSInteger scale = 0; scale < NSInteger(scales.count); ++scale) {
        [launcher.antialiasing selectItemAtIndex:metalFX];
        [launcher.metalfxScale selectItemAtIndex:scale];
        NSDictionary* metal = environment();
        Require([metal[@"RECOMP_RENDER_SCALE"] isEqual:scales[scale]], "MetalFX must apply the selected render scale");
        [launcher.antialiasing selectItemAtIndex:aa];
        NSDictionary* switched = environment();
        Require(switched[@"RECOMP_RENDER_SCALE"] == nil, "Switching from MetalFX must clear its render scale");
        Require([switched isEqualToDictionary:fresh],
                "Switching from MetalFX must produce the same environment as fresh non-MetalFX settings");
        Require([environment() isEqualToDictionary:fresh], "Repeated option application must preserve the environment");
      }
    }
  }
  std::puts("Launcher option environments are independent of previous selections");
}

void WaitFor(BOOL (^condition)(void)) {
  const double deadline = NSProcessInfo.processInfo.systemUptime + 5;
  while (!condition() && NSProcessInfo.processInfo.systemUptime < deadline)
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
  Require(condition(), "Folder refresh did not finish before its deadline");
}

void FolderRefreshTest() {
  RegisterDefaults(NSUserDefaults.standardUserDefaults);
  GtaivLauncher* launcher = [GtaivLauncher new];
  launcher.folder = @"/tmp/previous-game";
  [launcher build:NSUserDefaults.standardUserDefaults];
  launcher.tabs.delegate = nil;
  [launcher.tabs selectTabViewItemWithIdentifier:@"Shaders"];
  launcher.tabs.delegate = launcher;
  auto ui = launcher.preparation;
  NSString* log = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [@"Old installation error" writeToFile:log atomically:YES encoding:NSUTF8StringEncoding error:nil];
  NSData* oldResult = [NSJSONSerialization dataWithJSONObject:
      @{@"status": @"Prepared", @"log": log} options:0 error:nil];
  Method method = class_getClassMethod(NSOpenPanel.class, @selector(openPanel));
  IMP original = method_setImplementation(method, reinterpret_cast<IMP>(TestOpenPanel));
  for (int stage : {0, 1, 2}) {
    const bool inFlight = stage != 0;
    if (inFlight) {
      [launcher.antialiasing selectItemAtIndex:2];
      setenv("GTAIV_TEST_DELAY_STATUS", "1", 1);
      [ui refresh];
      unsetenv("GTAIV_TEST_DELAY_STATUS");
      if (stage == 2) WaitFor(^BOOL { return [[ui valueForKey:@"worker"] isRunning]; });
    }
    [ui consume:oldResult];
    Require([(NSButton*)[ui valueForKey:@"showLog"] isEnabled], "Old log fixture must be visible");
    [launcher.antialiasing selectItemAtIndex:1];
    chosenFolder = @[@"/tmp/new-game", @"/tmp/newer-game", @"/tmp/newest-game"][stage];
    [launcher choose:nil];
    Require([launcher.folder isEqual:chosenFolder], "Chooser must accept the new game folder");
    Require([[[ui valueForKey:@"status"] stringValue] isEqual:@"Checking shader cache…"],
            "Changing game folder must invalidate Prepared while Shaders remains selected");
    Require(![(NSButton*)[ui valueForKey:@"showLog"] isEnabled], "Changing folder must immediately clear the old log");
    if (inFlight) {
      // A buffered result can arrive after the chooser has accepted a new path.
      [ui consume:oldResult];
      Require(![(NSButton*)[ui valueForKey:@"showLog"] isEnabled], "Obsolete status results must not restore the old log");
    }
    WaitFor(^BOOL { return ![ui valueForKey:@"worker"]; });
    Require([[[ui valueForKey:@"status"] stringValue] isEqual:@"Not prepared"], "New installation must replace old status");
    Require(![(NSButton*)[ui valueForKey:@"showLog"] isEnabled], "New installation must not inherit the old log");
    Require([[ui valueForKey:@"message"] isEqual:chosenFolder], "Final worker must query the newly chosen installation");
    Require([launcher.tabs.selectedTabViewItem.identifier isEqual:@"Shaders"], "Refresh must preserve the selected tab");
  }
  method_setImplementation(method, original);
  [NSFileManager.defaultManager removeItemAtPath:log error:nil];
  std::puts("Game folder changes refresh status and discard obsolete results");
}

void PresentationTest() {
  RegisterDefaults(NSUserDefaults.standardUserDefaults);
  GtaivLauncher* launcher = [GtaivLauncher new];
  launcher.folder = @"/tmp";
  [launcher build:NSUserDefaults.standardUserDefaults];
  launcher.tabs.delegate = nil;
  [launcher.tabs selectTabViewItemWithIdentifier:@"Shaders"];
  GtaivShaderPreparation* ui = launcher.preparation;
  NSString* log = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [@"Synthetic shader failure\n" writeToFile:log atomically:YES encoding:NSUTF8StringEncoding error:nil];
  NSArray<NSDictionary*>* fixtures = @[
    @{@"status": @"Not prepared", @"size": @0},
    @{@"status": @"Needs preparation", @"size": @428000000},
    @{@"status": @"Preparing", @"phase": @"Discovering", @"total": NSNull.null},
    @{@"status": @"Preparing", @"phase": @"Compiling shaders", @"completed": @342, @"total": @864},
    @{@"status": @"Preparing", @"phase": @"Building pipelines", @"completed": @1850, @"total": @3200},
    @{@"status": @"Prepared", @"phase": @"Building pipelines", @"completed": @3200, @"total": @3200, @"size": @428000000},
    @{@"status": @"Incomplete"},
    @{@"status": @"Completed with errors", @"failed": @3, @"log": log},
    @{@"status": @"Incomplete", @"message": @"Metal compiler is unavailable. Install the Metal Toolchain in Xcode Settings > Components, or run xcodebuild -downloadComponent MetalToolchain.", @"log": log},
    @{@"status": @"Incomplete", @"message": @"Less than 1 GiB is free on the cache volume. Free disk space or choose a cache folder on another volume, then rerun preparation. Completed entries are kept.", @"log": log},
    @{@"status": @"Incomplete", @"message": @"Shader cache is busy or unwritable. Close other preparation workers and check the cache folder permissions."},
    @{@"status": @"Incomplete", @"message": [@"Cannot write shader cache. " stringByPaddingToLength:2400 withString:@"Compiler diagnostic details. " startingAtIndex:0], @"log": log},
    @{@"status": @"Not prepared", @"size": @0},
  ];
  for (NSString* appearance in @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]) {
    launcher.window.appearance = [NSAppearance appearanceNamed:appearance];
    for (NSDictionary* fixture in fixtures) {
      NSMutableDictionary* message = [@{@"message": @"", @"log": @""} mutableCopy];
      [message addEntriesFromDictionary:fixture];
      BOOL running = [message[@"status"] isEqual:@"Preparing"];
      [ui setRunning:running];
      [ui consume:[NSJSONSerialization dataWithJSONObject:message options:0 error:nil]];
      [launcher.window layoutIfNeeded];
      NSButton* prepare = [ui valueForKey:@"prepare"];
      NSButton* cancel = [ui valueForKey:@"cancel"];
      NSBox* error = [ui valueForKey:@"errorBox"];
      BOOL failed = [message[@"status"] isEqual:@"Completed with errors"] || [message[@"message"] length];
      Require(prepare.hidden == running && cancel.hidden == !running, "Preparation and cancellation must share the primary action slot");
      Require(error.hidden == !failed, "Only failed results must show the error panel");
      Require(![(NSView*)[ui valueForKey:@"progressSection"] isHidden] == running,
              "Only active preparation must show phase progress");
      Require([(NSButton*)[ui valueForKey:@"showLog"] isEnabled] == !![message[@"log"] length], "Log actions must reflect an existing log file");
      Require(ui.view.frame.size.height >= ui.view.fittingSize.height, "Every shader state must fit the launcher's original frame");
      NSButton* choose = [ui valueForKey:@"choose"];
      Require(choose.frame.size.width <= choose.fittingSize.width + 1, "The cache picker must keep a compact button at the trailing edge");
      for (NSString* key in @[@"status", @"summary", @"prepare", @"cancel", @"errorBox", @"cachePath", @"clean", @"choose", @"help"]) {
        NSView* view = [ui valueForKey:key];
        if (!view.isHiddenOrHasHiddenAncestor) {
          NSRect frame = [view convertRect:view.bounds toView:ui.view];
          // AppKit alignment rects inset text fields and button bezels.
          if (!NSContainsRect(NSInsetRect(ui.view.bounds, -4, -4), frame))
            NSLog(@"%@ in %@: %@ outside %@", key, message[@"status"], NSStringFromRect(frame), NSStringFromRect(ui.view.bounds));
          Require(NSContainsRect(NSInsetRect(ui.view.bounds, -4, -4), frame), "Shader controls must stay inside the tab in every state");
        }
      }
      if ([message[@"status"] isEqual:@"Prepared"])
        Require([prepare.title isEqual:@"Prepare again"], "Prepared must offer another preparation run");
    }
  }
  [NSFileManager.defaultManager removeItemAtPath:log error:nil];
  [launcher.window orderOut:nil];
  std::puts("Shader presentation passed in light and dark appearances");
}

NSButton* FindButton(NSView* view, NSString* title) {
  if ([view isKindOfClass:NSButton.class] && [((NSButton*)view).title isEqual:title]) return (NSButton*)view;
  for (NSView* child in view.subviews) if (NSButton* button = FindButton(child, title)) return button;
  return nil;
}

void CheckCleanConfirmation(GtaivShaderPreparation* ui, NSWindow* window) {
  NSButton* clean = [ui valueForKey:@"clean"];
  [clean performClick:nil];
  Require(window.attachedSheet && ![ui valueForKey:@"worker"], "Clean must wait for confirmation before starting a worker");
  NSButton* cancel = FindButton(window.attachedSheet.contentView, @"Cancel");
  Require(cancel && window.attachedSheet.defaultButtonCell == cancel.cell,
          "Return must choose the non-destructive action");
  [cancel performClick:nil];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    Require(!window.attachedSheet && ![ui valueForKey:@"worker"], "Dismissing confirmation must leave the cache alone");
    [clean performClick:nil];
    NSButton* confirm = FindButton(window.attachedSheet.contentView, @"Clean cache");
    Require(confirm != nil, "Clean confirmation must expose its destructive action");
    [confirm performClick:nil];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 600 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      Require([[[ui valueForKey:@"status"] stringValue] isEqual:@"Not prepared"] && ![ui valueForKey:@"worker"],
              "Confirmed cleaning must run the worker and return to Not prepared");
      Require(![(NSButton*)[ui valueForKey:@"showLog"] isEnabled], "Cleaning must clear the log action");
      [NSApp stopModalWithCode:NSModalResponseOK];
    });
  });
}

void CheckPages(GtaivLauncher* launcher, NSString* selected) {
  [launcher.window layoutIfNeeded];
  Require([launcher.tabs.selectedTabViewItem.identifier isEqual:selected], "Requested tab must be selected");
  for (NSString* title in launcher.tabs.pages) {
    NSView* page = launcher.tabs.pages[title];
    Require(page.superview == launcher.tabs, "Tab switches must preserve each page's constraint tree");
    Require(page.hidden == ![title isEqual:selected], "Only the selected page must be visible");
    Require(NSEqualRects(page.frame, launcher.tabs.contentRect), "Pages must fit the native tab bezel");
    if (![title isEqual:@"Advanced"])
      Require(page.bounds.size.height >= page.fittingSize.height, "Tab content must fit without clipping");
  }
  if ([selected isEqual:@"General"]) {
    NSPoint point = [launcher.hud convertPoint:NSMakePoint(NSMidX(launcher.hud.bounds), NSMidY(launcher.hud.bounds))
                                       toView:launcher.tabs.superview];
    NSView* hit = [launcher.tabs hitTest:point];
    Require(hit == launcher.hud || [hit isDescendantOf:launcher.hud], "Selected tab controls must receive mouse clicks");
  }
  if ([launcher.window.firstResponder isKindOfClass:NSView.class])
    Require(![(NSView*)launcher.window.firstResponder isHiddenOrHasHiddenAncestor], "Focus must not remain in a hidden tab");
}

int LauncherTest(NSString* scenario) {
  RegisterDefaults(NSUserDefaults.standardUserDefaults);
  GtaivLauncher* launcher = [GtaivLauncher new];
  NSString* folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
  [NSData.data writeToFile:[folder stringByAppendingPathComponent:@"GTAIV.exe"] atomically:YES];
  launcher.folder = folder;
  [launcher build:NSUserDefaults.standardUserDefaults];
  __block BOOL closed = NO;
  void (^close)(void) = ^{
    [[launcher.window standardWindowButton:NSWindowCloseButton] performClick:nil];
    Require(!launcher.window.visible, "Red close button must close the window immediately");
    closed = YES;
  };
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    if ([scenario isEqual:@"idle"]) { close(); return; }
    if ([scenario isEqual:@"launch"]) {
      [launcher.tabs selectTabViewItemWithIdentifier:@"Shaders"];
      close();  // cancellation may precede the worker's asynchronous launch
      return;
    }
    if ([scenario isEqual:@"status"]) {
      setenv("GTAIV_TEST_STUBBORN_STATUS", "1", 1);
      [launcher.tabs selectTabViewItemWithIdentifier:@"Shaders"];
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        Require([[launcher.preparation valueForKey:@"worker"] isRunning], "Stubborn status worker must be running");
        close();
      });
      return;
    }
    for (NSString* name in @[@"Advanced", @"General", @"Shaders", @"General", @"Advanced", @"Shaders"]) {
      double start = NSProcessInfo.processInfo.systemUptime;
      [launcher.tabs selectTabViewItemWithIdentifier:name];
      CheckPages(launcher, name);
      if ([name isEqual:@"General"]) [launcher.window makeFirstResponder:launcher.hud];
      std::printf("Tab %s: %.1f ms\n", name.UTF8String,
                  1000 * (NSProcessInfo.processInfo.systemUptime - start));
    }
    // Snapshot selection disables the launcher delegate; page visibility must
    // still follow native selection, including keyboard/programmatic selection.
    launcher.tabs.delegate = nil;
    [launcher.tabs selectTabViewItemAtIndex:1];
    CheckPages(launcher, @"Advanced");
    launcher.tabs.delegate = launcher;
    NSScrollView* scroll = (NSScrollView*)launcher.tabs.pages[@"Advanced"].subviews.firstObject;
    [scroll.contentView scrollToPoint:NSMakePoint(0, 300)];
    NSPoint position = scroll.contentView.bounds.origin;
    [launcher.tabs selectTabViewItemWithIdentifier:@"General"];
    [launcher.tabs selectTabViewItemWithIdentifier:@"Advanced"];
    Require(NSEqualPoints(position, scroll.contentView.bounds.origin), "Advanced must retain its scroll position");
    [launcher.preparation stopWithCompletion:^{
      NSInteger savedAA = [NSUserDefaults.standardUserDefaults integerForKey:kAntiAliasingKey];
      [launcher.antialiasing selectItemAtIndex:1];
      [[launcher.preparation valueForKey:@"prepare"] performClick:nil];
      Require(launcher.preparation.preparing && !launcher.play.enabled && !launcher.chooseFolder.enabled,
              "Preparing must keep Play and folder changes disabled");
      for (NSString* name in @[@"General", @"Advanced", @"Shaders"] ) {
        [launcher.tabs selectTabViewItemWithIdentifier:name];
        CheckPages(launcher, name);
      }
      NSTask* worker = [launcher.preparation valueForKey:@"worker"];
      Require([worker.environment[@"RECOMP_UPSCALER"] isEqual:@"smaa"], "Worker must use current UI options");
      Require([NSUserDefaults.standardUserDefaults integerForKey:kAntiAliasingKey] == savedAA,
              "Worker snapshots must not write preferences during navigation");
      [launcher.antialiasing selectItemAtIndex:3];
      Require([worker.environment[@"RECOMP_UPSCALER"] isEqual:@"smaa"], "Running worker settings must remain frozen");
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        Require([[[launcher.preparation valueForKey:@"phase"] stringValue] isEqual:@"Discovering"],
                "Worker progress must arrive while navigation remains enabled");
        close();
      });
    }];
  });
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    Require(NO, "Red close button must exit the launcher event loop within the shutdown deadline");
  });
  [launcher.window makeKeyAndOrderFront:nil];
  [NSApp run];
  NSModalResponse result = launcher.response;
  Require(closed && result == NSModalResponseCancel && !launcher.preparation.preparing &&
          ![launcher.preparation valueForKey:@"worker"], "Closing must stop the worker before returning");
  [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
  std::puts("Launcher navigation and close passed");
  return 0;
}
}
int main(int argc, char** argv) {
  if (argc > 1 && !std::strcmp(argv[1], "--shader-preparation")) {
    if (argc > 2 && !std::strcmp(argv[2], "clean")) {
      std::puts("{\"status\":\"Not prepared\",\"size\":0}");
      return 0;
    }
    if (argc > 2 && !std::strcmp(argv[2], "status")) {
      if (std::getenv("GTAIV_TEST_DELAY_STATUS")) usleep(300000);
      if (argc > 4 && std::strstr(argv[4], "new")) {
        if (!std::getenv("RECOMP_UPSCALER") || std::strcmp(std::getenv("RECOMP_UPSCALER"), "smaa")) return 1;
        std::printf("{\"status\":\"Not prepared\",\"message\":\"%s\"}\n", argv[4]);
        return 0;
      }
      if (std::getenv("GTAIV_TEST_STUBBORN_STATUS")) {
        std::signal(SIGTERM, SIG_IGN);
        for (int i = 0; i < 200; i++) usleep(50000);
      }
      std::puts("{\"status\":\"Not prepared\"}");
      return 0;
    }
    std::signal(SIGTERM, Cancel);
    std::puts("{\"status\":\"Preparing\",\"phase\":\"Discovering\",\"completed\":0,\"total\":null}");
    std::fflush(stdout);
    for (int i = 0; i < 200 && !cancelled; i++) usleep(50000);
    std::puts("{\"status\":\"Incomplete\",\"phase\":\"Compiling shaders\",\"completed\":1,\"total\":5}");
    return cancelled ? 130 : 1;
  }
  @autoreleasepool {
    [NSApplication sharedApplication];
    if (argc > 1 && !std::strcmp(argv[1], "--folder-refresh")) { FolderRefreshTest(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "--options")) { OptionEnvironmentTest(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "--presentation")) { PresentationTest(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "--launcher")) {
      return LauncherTest(argc > 2 ? @(argv[2]) : @"navigation");
    }
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 700, 500)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    GtaivShaderPreparation* ui = [[GtaivShaderPreparation alloc] initWithGameFolder:^{ return @"/tmp"; }
        environment:^{ return NSProcessInfo.processInfo.environment; }];
    window.contentView = ui.view;
    __block BOOL playEnabled = YES;
    ui.runningChanged = ^(BOOL running) { playEnabled = !running; };
    NSButton* prepare = [ui valueForKey:@"prepare"];
    NSButton* clean = [ui valueForKey:@"clean"];
    NSButton* cancel = [ui valueForKey:@"cancel"];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      [prepare performClick:nil];
      Require(ui.preparing && !playEnabled && !clean.enabled && !prepare.enabled && !cancel.hidden,
              "Preparing must disable Play/Clean and expose Cancel");
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      Require([[[ui valueForKey:@"phase"] stringValue] isEqualToString:@"Discovering"], "Worker progress must reach the modal UI");
      [cancel performClick:nil];
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      Require(!ui.preparing && playEnabled && clean.enabled && prepare.enabled && cancel.hidden,
              "Cancellation must restore Play/Clean");
      Require([[[ui valueForKey:@"status"] stringValue] isEqualToString:@"Incomplete"], "Cancellation must display Incomplete");
      CheckCleanConfirmation(ui, window);
    });
    [NSApp runModalForWindow:window];
    std::puts("Launcher shader lifecycle passed");
  }
}
