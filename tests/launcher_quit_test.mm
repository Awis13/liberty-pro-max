// Exercise the production launcher's application loop and menu in a
// temporary app bundle. The Python driver checks that the process really exits.
#import <AppKit/AppKit.h>
#import "../src/launcher_mac.mm"

extern "C" void GtaivSyncSaves(const char*) {}

namespace {
GtaivLauncher* launcher;
void Require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::_Exit(1); }
}
void CheckShutdown() {
  Require(launcher && !launcher.window.visible, "Quit must hide the launcher window");
}
void RequestQuit(NSString* action) {
  if ([action isEqual:@"keyboard"]) {
    NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
        modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:launcher.window.windowNumber
        context:nil characters:@"q" charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
    [NSApp sendEvent:event];
  } else if ([action isEqual:@"window"]) {
    [launcher.window performClose:nil];
  } else {
    // Let the driver send Quit from another process so it can check the reply
    // even after the launcher exits.
    std::puts("ready");
    std::fflush(stdout);
  }
}

void SendQuit(pid_t pid) {
  // The same quit Apple event as the Dock, without Launch Services registration.
  NSAppleEventDescriptor* event = [NSAppleEventDescriptor appleEventWithEventClass:kCoreEventClass
      eventID:kAEQuitApplication targetDescriptor:[NSAppleEventDescriptor descriptorWithProcessIdentifier:pid]
      returnID:kAutoGenerateReturnID transactionID:kAnyTransactionID];
  NSError* error = nil;
  NSAppleEventDescriptor* reply = [event sendEventWithOptions:NSAppleEventSendWaitForReply timeout:5 error:&error];
  if (error) std::fprintf(stderr, "%s\n", error.description.UTF8String);
  Require(reply && !error, "System must receive a quit reply");
  const int32_t code = [reply paramDescriptorForKeyword:keyErrorNumber].int32Value;
  if (code) std::fprintf(stderr, "Quit reply error: %d\n", code);
  Require(code == 0, "Launcher must accept system quit, including logout and shutdown");
}
}

int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc == 3 && !std::strcmp(argv[1], "dock-request")) {
      SendQuit(static_cast<pid_t>(std::strtol(argv[2], nullptr, 10)));
      return 0;
    }
    Require(argc == 2, "Expected a quit action");
    NSString* action = @(argv[1]);
    // Isolate preferences so this never skips the launcher or changes options.
    NSString* domain = NSBundle.mainBundle.bundleIdentifier;
    [NSUserDefaults.standardUserDefaults removePersistentDomainForName:domain];
    // The test bundle is disposable; restoration prompts would run a separate
    // modal loop and intercept Quit before the launcher's loop receives it.
    [NSUserDefaults.standardUserDefaults setVolatileDomain:@{
      @"ApplePersistenceIgnoreState": @YES, @"NSQuitAlwaysKeepsWindows": @NO,
    } forName:NSArgumentDomain];
    std::atexit(CheckShutdown);
    // Request Quit after AppKit finishes launching. A timer queued before
    // RunLauncherIfWanted can fire inside its startup event loop instead.
    [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidFinishLaunchingNotification
        object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* notification) {
      dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        for (NSWindow* window in NSApp.windows)
          if ([window.delegate isKindOfClass:GtaivLauncher.class]) launcher = (GtaivLauncher*)window.delegate;
        Require(launcher != nil && launcher.window.visible, "Production launcher must be visible");
        RequestQuit(action);
      });
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
      Require(NO, "Quit must exit the launcher within its shutdown deadline");
    });
    RunLauncherIfWanted(argc, argv);
    Require(NO, "Production launcher must exit on Quit");
  }
}
