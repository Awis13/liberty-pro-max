#import "shader_preparation_mac.h"
#include <signal.h>

namespace {
NSString* const kCacheRootKey = @"ShaderCacheRoot";
NSString* const kScopeNote = @"Prepared covers the current shader and recipe set. New combinations may still compile during gameplay.";
NSTextField* Note(NSString* text) {
  NSTextField* label = [NSTextField wrappingLabelWithString:text];
  label.preferredMaxLayoutWidth = 620;
  label.textColor = NSColor.secondaryLabelColor;
  return label;
}
NSStackView* Column(NSArray<NSView*>* views, CGFloat spacing) {
  NSStackView* stack = [NSStackView stackViewWithViews:views];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = spacing;
  return stack;
}
NSStackView* Row(NSArray<NSView*>* views, CGFloat spacing) {
  NSStackView* stack = [NSStackView stackViewWithViews:views];
  stack.alignment = NSLayoutAttributeCenterY;
  stack.distribution = NSStackViewDistributionFill;
  stack.spacing = spacing;
  return stack;
}
NSView* Spacer() {
  NSView* view = [NSView new];
  [view setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
  return view;
}
NSBox* Group(NSView* content) {
  NSBox* box = [NSBox new];
  box.boxType = NSBoxCustom;
  box.titlePosition = NSNoTitle;
  box.borderWidth = 1;
  box.cornerRadius = 8;
  box.fillColor = NSColor.controlBackgroundColor;
  box.borderColor = NSColor.separatorColor;
  box.contentViewMargins = NSZeroSize;
  content.translatesAutoresizingMaskIntoConstraints = NO;
  [box.contentView addSubview:content];
  [NSLayoutConstraint activateConstraints:@[
    [content.leadingAnchor constraintEqualToAnchor:box.contentView.leadingAnchor],
    [content.trailingAnchor constraintEqualToAnchor:box.contentView.trailingAnchor],
    [content.topAnchor constraintEqualToAnchor:box.contentView.topAnchor],
    [content.bottomAnchor constraintEqualToAnchor:box.contentView.bottomAnchor],
  ]];
  return box;
}
void LinkStyle(NSButton* button) {
  button.bordered = NO;
  button.contentTintColor = NSColor.linkColor;
  button.alignment = NSTextAlignmentLeft;
}
}

@interface GtaivShaderPreparation ()
@property(nonatomic, strong) NSView* view;
@property(nonatomic) BOOL preparing;
@property(nonatomic, copy) NSString* (^gameFolder)(void);
@property(nonatomic, copy) NSDictionary* (^environment)(void);
@property(nonatomic, strong) NSButton* prepare;
@property(nonatomic, strong) NSButton* cancel;
@property(nonatomic, strong) NSButton* clean;
@property(nonatomic, strong) NSButton* choose;
@property(nonatomic, strong) NSButton* showLog;
@property(nonatomic, strong) NSButton* noticeLog;
@property(nonatomic, strong) NSTextField* status;
@property(nonatomic, strong) NSImageView* statusIndicator;
@property(nonatomic, strong) NSTextField* summary;
@property(nonatomic, strong) NSTextField* phase;
@property(nonatomic, strong) NSTextField* counts;
@property(nonatomic, strong) NSStackView* progressSection;
@property(nonatomic, strong) NSTextField* cachePath;
@property(nonatomic, strong) NSTextField* size;
@property(nonatomic, strong) NSTextField* error;
@property(nonatomic, strong) NSTextField* errorTitle;
@property(nonatomic, strong) NSBox* errorBox;
@property(nonatomic, strong) NSTextField* help;
@property(nonatomic, strong) NSProgressIndicator* progress;
@property(nonatomic, copy) NSString* preparationStatus;
@property(nonatomic, copy) NSString* message;
@property(nonatomic) NSInteger failed;
@property(nonatomic, strong) NSTask* worker;
@property(nonatomic, copy) NSString* action;
@property(nonatomic, copy) NSString* logPath;
@property(nonatomic) BOOL cancelling;
@property(nonatomic) BOOL receivedResult;
@property(nonatomic) BOOL refreshPending;
@property(nonatomic, copy) void (^stopCompletion)(void);
@end

@implementation GtaivShaderPreparation
+ (NSString*)cacheRoot {
  return [NSUserDefaults.standardUserDefaults stringForKey:kCacheRootKey] ?:
      [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"gtaiv"];
}
- (instancetype)initWithGameFolder:(NSString* (^)(void))folder environment:(NSDictionary* (^)(void))environment {
  if (!(self = [super init])) return nil;
  self.gameFolder = folder; self.environment = environment;
  self.preparationStatus = @"Not prepared";
  self.status = [NSTextField labelWithString:@"Not prepared"];
  self.status.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
  self.status.lineBreakMode = NSLineBreakByTruncatingTail;
  self.statusIndicator = [NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"circle.fill" accessibilityDescription:nil]];
  [self.statusIndicator setAccessibilityElement:NO];
  [self.statusIndicator.widthAnchor constraintEqualToConstant:8].active = YES;
  [self.statusIndicator.heightAnchor constraintEqualToConstant:8].active = YES;
  self.summary = Note(@"");
  self.summary.preferredMaxLayoutWidth = 440;
  self.prepare = [NSButton buttonWithTitle:@"Precompile shaders" target:self action:@selector(start:)];
  self.cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancel:)];
  self.cancel.hidden = YES;
  self.phase = [NSTextField labelWithString:@""];
  self.phase.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  self.phase.textColor = NSColor.secondaryLabelColor;
  self.phase.lineBreakMode = NSLineBreakByTruncatingTail;
  self.counts = [NSTextField labelWithString:@""];
  self.counts.font = [NSFont monospacedDigitSystemFontOfSize:NSFont.smallSystemFontSize weight:NSFontWeightRegular];
  self.counts.textColor = NSColor.secondaryLabelColor;
  self.counts.toolTip = @"Completed attempts in this phase, including failures.";
  self.progress = [NSProgressIndicator new];
  self.progress.style = NSProgressIndicatorStyleBar;
  self.progress.controlSize = NSControlSizeSmall;
  self.progress.minValue = 0; self.progress.maxValue = 1;
  self.cachePath = [NSTextField labelWithString:@""];
  self.cachePath.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  self.cachePath.textColor = NSColor.secondaryLabelColor;
  self.cachePath.selectable = YES;
  self.cachePath.lineBreakMode = NSLineBreakByTruncatingMiddle;
  self.cachePath.toolTip = @"Only the metal subfolder is generated and cleaned.";
  self.choose = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(chooseCache:)];
  NSButton* finder = [NSButton buttonWithTitle:@"Show in Finder" target:self action:@selector(showCache:)];
  self.size = [NSTextField labelWithString:@"0 bytes"];
  self.size.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  self.size.textColor = NSColor.secondaryLabelColor;
  self.clean = [NSButton buttonWithTitle:@"Clean cache…" target:self action:@selector(cleanCache:)];
  self.showLog = [NSButton buttonWithTitle:@"Open error log" target:self action:@selector(openLog:)];
  self.showLog.enabled = NO;
  LinkStyle(finder); LinkStyle(self.showLog);
  NSStackView* statusLine = Row(@[self.statusIndicator, self.status], 7);
  NSStackView* summary = Column(@[statusLine, self.summary], 5);
  NSStackView* actions = Row(@[self.prepare, self.cancel], 8);
  [actions setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView* header = Row(@[summary, actions], 16);
  header.alignment = NSLayoutAttributeTop;
  [summary setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
  [self.summary setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  for (NSButton* button in @[self.prepare, self.cancel, self.choose, self.clean]) {
    [button setContentCompressionResistancePriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
    [button setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
  }
  NSStackView* phase = Row(@[self.phase, Spacer(), self.counts], 8);
  self.progressSection = Column(@[self.progress, phase], 8);

  self.errorTitle = [NSTextField labelWithString:@"Preparation stopped"];
  self.errorTitle.font = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
  self.error = Note(@"");
  self.error.font = [NSFont systemFontOfSize:12];
  self.error.selectable = YES;
  self.error.maximumNumberOfLines = 4;
  self.error.lineBreakMode = NSLineBreakByWordWrapping;
  self.noticeLog = [NSButton buttonWithTitle:@"Open error log" target:self action:@selector(openLog:)];
  LinkStyle(self.noticeLog);
  NSStackView* notice = Column(@[self.errorTitle, self.error, self.noticeLog], 5);
  notice.edgeInsets = NSEdgeInsetsMake(11, 12, 11, 12);
  self.errorBox = Group(notice);

  NSTextField* cacheTitle = [NSTextField labelWithString:@"Shader cache"];
  cacheTitle.font = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
  NSStackView* cacheHeading = Row(@[cacheTitle, Spacer(), self.size], 8);
  NSTextField* locationTitle = [NSTextField labelWithString:@"Location"];
  locationTitle.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
  NSStackView* location = Column(@[locationTitle, self.cachePath], 4);
  [location setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
  [self.cachePath setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView* locationRow = Row(@[location, self.choose], 12);
  locationRow.edgeInsets = NSEdgeInsetsMake(12, 14, 12, 14);
  NSBox* separator = [NSBox new]; separator.boxType = NSBoxSeparator;
  NSStackView* maintenance = Row(@[finder, self.showLog, Spacer(), self.clean], 12);
  maintenance.edgeInsets = NSEdgeInsetsMake(10, 14, 10, 14);
  NSStackView* cacheRows = Column(@[locationRow, separator, maintenance], 0);
  NSBox* cacheBox = Group(cacheRows);
  NSTextField* ownership = Note(@"Only the metal folder is generated and cleaned.");
  ownership.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  NSStackView* cache = Column(@[cacheHeading, cacheBox, ownership], 6);
  self.help = Note([@"Preparation is optional. " stringByAppendingString:kScopeNote]);
  self.help.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  NSStackView* stack = Column(@[header, self.progressSection, self.errorBox, cache, self.help], 19);
  self.view = [NSView new];
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [self.view.widthAnchor constraintGreaterThanOrEqualToConstant:620],
    [self.view.heightAnchor constraintGreaterThanOrEqualToConstant:380],
    [stack.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [stack.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
    [stack.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [stack.bottomAnchor constraintLessThanOrEqualToAnchor:self.view.bottomAnchor],
    [self.summary.widthAnchor constraintEqualToAnchor:summary.widthAnchor],
    [self.cachePath.widthAnchor constraintEqualToAnchor:location.widthAnchor],
    [self.error.widthAnchor constraintEqualToAnchor:notice.widthAnchor constant:-24],
  ]];
  for (NSView* section in @[header, self.progressSection, self.errorBox, cache, self.help])
    [section.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
  for (NSView* row in @[locationRow, separator, maintenance])
    [row.widthAnchor constraintEqualToAnchor:cacheRows.widthAnchor].active = YES;
  for (NSView* row in @[cacheHeading, cacheBox, ownership])
    [row.widthAnchor constraintEqualToAnchor:cache.widthAnchor].active = YES;
  [self.progress.widthAnchor constraintEqualToAnchor:self.progressSection.widthAnchor].active = YES;
  [phase.widthAnchor constraintEqualToAnchor:self.progressSection.widthAnchor].active = YES;
  [self updatePath];
  [self updatePresentation];
  return self;
}
- (void)updatePresentation {
  BOOL busy = self.worker != nil || self.preparing;
  BOOL prepared = [self.preparationStatus isEqualToString:@"Prepared"];
  BOOL partial = [self.preparationStatus isEqualToString:@"Completed with errors"];
  BOOL incomplete = [self.preparationStatus isEqualToString:@"Incomplete"];
  BOOL interrupted = incomplete && [self.phase.stringValue isEqualToString:@"Preparation worker stopped unexpectedly."];
  BOOL failed = self.message.length || interrupted;
  BOOL showError = !busy && (failed || partial);
  self.status.stringValue = partial
      ? [NSString stringWithFormat:@"Completed with errors: %ld failed", (long)self.failed] : self.preparationStatus;
  self.prepare.title = prepared ? @"Prepare again" : (failed || partial) ? @"Retry preparation" :
      incomplete ? @"Continue preparation" : @"Precompile shaders";
  self.summary.stringValue = @"Compile shaders and known Metal pipelines ahead of time to reduce compilation during play.";
  NSColor* color = NSColor.secondaryLabelColor;
  if (prepared) { self.summary.stringValue = kScopeNote; color = NSColor.systemGreenColor; }
  else if (partial) {
    self.summary.stringValue = @"Other entries were prepared successfully. Run preparation again to reuse completed entries and retry failures.";
    color = NSColor.systemOrangeColor;
  } else if (incomplete) {
    self.summary.stringValue = @"Completed entries are kept. Prepare again to validate and reuse them, then finish the remaining work.";
    color = failed ? NSColor.systemRedColor : NSColor.systemOrangeColor;
  } else if ([self.preparationStatus isEqualToString:@"Needs preparation"]) {
    self.summary.stringValue = @"The cache no longer covers the current shaders, settings, or recipe set. Prepare again to reuse compatible entries.";
    color = NSColor.systemOrangeColor;
  }
  if (busy) {
    color = NSColor.controlAccentColor;
    if (self.preparing) {
      self.summary.stringValue = self.cancelling ? @"Waiting for the worker to stop. Completed cache entries will be kept." :
          [self.phase.stringValue isEqualToString:@"Compiling shaders"] ? @"Preparing Metal libraries for the shaders in your game installation." :
          [self.phase.stringValue isEqualToString:@"Building pipelines"] ? @"Preparing the known shader combinations used by the renderer." :
          @"Reading your game installation and discovering shaders to prepare.";
    } else {
      BOOL cleaning = [self.action isEqualToString:@"clean"];
      self.status.stringValue = cleaning ? @"Cleaning shader cache…" : @"Checking shader cache…";
      self.prepare.title = cleaning ? @"Cleaning…" : @"Checking…";
      self.summary.stringValue = cleaning ? @"Removing only the metal folder." :
          @"Checking the current game files, settings, and cached entries.";
    }
  }
  self.statusIndicator.contentTintColor = color;
  self.prepare.hidden = self.preparing;
  self.prepare.enabled = !busy;
  self.cancel.hidden = !self.preparing;
  self.cancel.enabled = self.preparing && !self.cancelling;
  self.cancel.title = self.cancelling ? @"Cancelling…" : @"Cancel";
  self.clean.enabled = !busy;
  self.choose.enabled = !busy;
  self.progress.hidden = !self.preparing;
  self.progressSection.hidden = !self.preparing;
  self.errorBox.hidden = !showError;
  self.errorTitle.stringValue = partial ? [NSString stringWithFormat:@"%ld attempt%@ failed", (long)self.failed, self.failed == 1 ? @"" : @"s"] : @"Preparation stopped";
  self.error.stringValue = partial ? @"Completed entries are kept. The error log lists the failures." :
      self.message.length ? self.message : interrupted ? self.phase.stringValue : @"";
  self.error.toolTip = self.error.stringValue;
  NSColor* errorColor = partial ? NSColor.systemOrangeColor : NSColor.systemRedColor;
  self.errorBox.fillColor = [errorColor colorWithAlphaComponent:0.06];
  self.errorBox.borderColor = [errorColor colorWithAlphaComponent:0.25];
  self.showLog.enabled = self.logPath.length && [NSFileManager.defaultManager fileExistsAtPath:self.logPath];
  self.noticeLog.hidden = !self.showLog.enabled;
  self.help.hidden = busy || prepared || showError;
}
- (void)updatePath {
  NSString* owned = [[GtaivShaderPreparation cacheRoot] stringByAppendingPathComponent:@"metal"];
  self.cachePath.stringValue = owned.stringByAbbreviatingWithTildeInPath;
  self.cachePath.toolTip = owned;
}
- (void)setRunning:(BOOL)running {
  self.preparing = running;
  [self updatePresentation];
  NSWindow* window = self.view.window;
  if (window.firstResponder == (running ? self.prepare : self.cancel))
    [window makeFirstResponder:running ? self.cancel : self.prepare];
  if (self.runningChanged) self.runningChanged(running);
}
- (void)refresh {
  [self updatePath];
  // Preparation owns its frozen settings until completion/cancellation. Status
  // results, however, can become obsolete while the folder picker is open.
  if (self.preparing) { self.refreshPending = YES; return; }
  self.logPath = nil; self.message = @""; self.phase.stringValue = @"";
  if (!self.worker) [self run:@"status"];
  else {
    self.refreshPending = YES;
    [self updatePresentation];
  }
}
- (void)start:(id)sender { if (!self.worker) [self run:@"prepare"]; }
- (void)cleanCache:(id)sender {
  if (self.worker || self.preparing || !self.view.window) return;
  NSAlert* alert = [NSAlert new];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"Clean shader cache?";
  alert.informativeText = [NSString stringWithFormat:
      @"Remove %@ from the generated shader cache? Preparation will be needed again. Game files and other renderer caches are kept.\n\n%@",
      self.size.stringValue, self.cachePath.stringValue];
  [alert addButtonWithTitle:@"Clean cache"].hasDestructiveAction = YES;
  NSButton* cancel = [alert addButtonWithTitle:@"Cancel"];
  [alert layout];
  alert.window.defaultButtonCell = (NSButtonCell*)cancel.cell;
  cancel.keyEquivalent = @"\r";
  alert.buttons.firstObject.keyEquivalent = @"";
  [alert beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response) {
    if (response == NSAlertFirstButtonReturn && !self.worker && !self.preparing && self.view.window.visible)
      [self run:@"clean"];
  }];
}
- (void)chooseCache:(id)sender {
  if (self.worker) return;
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseDirectories = YES; panel.canChooseFiles = NO; panel.canCreateDirectories = YES;
  panel.message = @"Choose a location for the metal cache folder.";
  panel.directoryURL = [NSURL fileURLWithPath:[GtaivShaderPreparation cacheRoot]];
  if ([panel runModal] == NSModalResponseOK) {
    [NSUserDefaults.standardUserDefaults setObject:panel.URL.path forKey:kCacheRootKey];
    [self refresh];
  }
}
- (void)showCache:(id)sender {
  NSString* owned = [[GtaivShaderPreparation cacheRoot] stringByAppendingPathComponent:@"metal"];
  NSString* path = [NSFileManager.defaultManager fileExistsAtPath:owned] ? owned : [GtaivShaderPreparation cacheRoot];
  [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:path]]];
}
- (void)openLog:(id)sender {
  if (self.logPath.length) [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:self.logPath]];
}
- (void)cancel:(id)sender {
  if (!self.worker || !self.preparing || self.cancelling) return;
  self.cancelling = YES; self.cancel.enabled = NO; self.phase.stringValue = @"Cancelling…";
  [self updatePresentation];
  [self terminateWorker];
}
- (void)terminateWorker {
  NSTask* task = self.worker;
  if (task.running) [task terminate];
  // Metal compilation may be inside a synchronous driver call. Killing this
  // worker is safe: completed entries were atomically published individually.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    if (self.worker == task && task.running) kill(task.processIdentifier, SIGKILL);
  });
}
- (void)stopWithCompletion:(void (^)(void))completion {
  if (!self.worker) { completion(); return; }
  self.stopCompletion = completion;
  self.refreshPending = NO;
  if (self.preparing) [self cancel:nil];
  else [self terminateWorker];
}
- (void)consume:(NSData*)line {
  if (self.refreshPending && [self.action isEqualToString:@"status"]) return;
  id message = [NSJSONSerialization JSONObjectWithData:line options:0 error:nil];
  if (![message isKindOfClass:NSDictionary.class]) return;
  NSString* status = message[@"status"];
  if (![status isKindOfClass:NSString.class]) return;
  self.receivedResult = ![status isEqualToString:@"Preparing"];
  if (self.cancelling) return;
  self.preparationStatus = status;
  self.failed = [message[@"failed"] integerValue];
  NSString* phase = message[@"phase"];
  if ([phase isKindOfClass:NSString.class]) {
    NSNumber* total = message[@"total"];
    BOOL known = [total isKindOfClass:NSNumber.class];
    self.progress.indeterminate = !known;
    if (known) {
      self.progress.maxValue = MAX(1, total.doubleValue);
      self.progress.doubleValue = [message[@"completed"] doubleValue];
      self.counts.stringValue = [NSString stringWithFormat:@"%@ / %@",
          [NSNumberFormatter localizedStringFromNumber:message[@"completed"] ?: @0 numberStyle:NSNumberFormatterDecimalStyle],
          [NSNumberFormatter localizedStringFromNumber:total numberStyle:NSNumberFormatterDecimalStyle]];
    } else { self.counts.stringValue = @""; [self.progress startAnimation:nil]; }
    self.phase.stringValue = phase;
  }
  if ([message[@"size"] isKindOfClass:NSNumber.class])
    self.size.stringValue = [NSByteCountFormatter stringFromByteCount:[message[@"size"] longLongValue] countStyle:NSByteCountFormatterCountStyleFile];
  if ([message[@"message"] isKindOfClass:NSString.class]) self.message = message[@"message"];
  if ([message[@"log"] isKindOfClass:NSString.class]) self.logPath = message[@"log"];
  [self updatePresentation];
}
- (void)run:(NSString*)action {
  self.action = action; self.receivedResult = NO; self.cancelling = NO;
  self.message = @"";
  const BOOL prepares = [action isEqualToString:@"prepare"];
  if (prepares) {
    self.preparationStatus = @"Preparing"; self.phase.stringValue = @"Discovering"; self.counts.stringValue = @"";
    [self setRunning:YES];
    self.progress.indeterminate = YES; [self.progress startAnimation:nil];
  }
  // Even status is queried in a worker: hashing game content must never block
  // the launcher main thread. Each invocation gets the current UI settings.
  NSTask* task = [NSTask new];
  task.executableURL = NSBundle.mainBundle.executableURL;
  task.arguments = @[@"--shader-preparation", action, @"--game-dir", self.gameFolder() ?: @"",
                     @"--cache-root", [GtaivShaderPreparation cacheRoot]];
  task.environment = self.environment();
  NSPipe* output = [NSPipe pipe]; task.standardOutput = output;
  task.standardError = [NSFileHandle fileHandleWithNullDevice];
  self.worker = task;
  [self updatePresentation];
  // Process launch can also block in Launch Services. Keep it and pipe reads
  // off the main thread, with progress and completion delivered in order.
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    NSError* error = nil;
    if (![task launchAndReturnError:&error]) {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (self.worker != task) return;
        self.worker = nil;
        BOOL refresh = self.refreshPending; self.refreshPending = NO;
        self.preparationStatus = @"Incomplete"; self.message = error.localizedDescription;
        [self.progress stopAnimation:nil];
        [self setRunning:NO];
        if (self.stopCompletion) {
          void (^done)(void) = self.stopCompletion; self.stopCompletion = nil; done();
        } else if (refresh) [self refresh];
      });
      return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      // Closing/cancelling may have happened before the task finished launching.
      if (self.worker == task && (self.cancelling || self.stopCompletion)) [self terminateWorker];
    });
    NSMutableData* pending = [NSMutableData new];
    for (;;) {
      NSData* data = [output.fileHandleForReading availableData];
      if (!data.length) break;
      [pending appendData:data];
      for (;;) {
        const uint8_t* bytes = static_cast<const uint8_t*>(pending.bytes);
        const uint8_t* end = static_cast<const uint8_t*>(memchr(bytes, '\n', pending.length));
        if (!end) break;
        NSUInteger length = end - bytes;
        NSData* line = [pending subdataWithRange:NSMakeRange(0, length)];
        [pending replaceBytesInRange:NSMakeRange(0, length + 1) withBytes:nullptr length:0];
        dispatch_async(dispatch_get_main_queue(), ^{ if (self.worker == task) [self consume:line]; });
      }
    }
    [task waitUntilExit];
    [output.fileHandleForReading closeFile];
    dispatch_async(dispatch_get_main_queue(), ^{
      if (self.worker != task) return;
      if (self.cancelling || !self.receivedResult) {
        self.preparationStatus = @"Incomplete";
        self.phase.stringValue = self.cancelling ? @"Cancelled. Completed entries are kept." : @"Preparation worker stopped unexpectedly.";
      }
      self.worker = nil;
      [self.progress stopAnimation:nil];
      if ([self.action isEqualToString:@"clean"]) {
        self.logPath = nil; self.showLog.enabled = NO; self.phase.stringValue = @"";
      }
      [self setRunning:NO];
      if (self.stopCompletion) {
        void (^done)(void) = self.stopCompletion; self.stopCompletion = nil; done();
      } else if (self.refreshPending) {
        self.refreshPending = NO; [self refresh];
      }
    });
  });
}
@end
