// The Mac app's launcher: the first window of "Liberty Pro Max.app". It
// picks the game folder and the port's options, then starts the game as a
// new instance of the app with them (--no-launcher, through Launch
// Services): the game gets a process of its own from the start, so options
// read when it loads (the Metal HUD) apply, SDL sets up the application as
// it expects, and macOS turns Game Mode on for it.
//
// Shown when the executable runs from the app bundle; with "Skip this
// window" set, only while Option is held at launch.

#import <AppKit/AppKit.h>

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "launcher_options.h"

namespace {

bool HasGame(NSString* dir) {
  return dir.length &&
         [NSFileManager.defaultManager fileExistsAtPath:[dir stringByAppendingPathComponent:@"GTAIV.exe"]];
}

// The arguments this process was given: a game folder to start with and
// the game's own (after "--"), passed on.
struct Args {
  NSString* game_dir = nil;
  std::vector<std::string> game_args;
};

Args ParseArgs(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--game-dir") && i + 1 < argc) {
      a.game_dir = @(argv[++i]);
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) a.game_args.push_back(argv[i]);
    }
  }
  return a;
}

extern "C" void GtaivSyncSaves(const char* when);  // save_sync_mac.mm

// Runs the game: this executable again, with the options in its
// environment. Returns only if that failed.
void Launch(NSString* folder, NSUserDefaults* d, const Args& args) {
  ApplyOptions(d);
  // The newest saves from the player's iPad and Apple TV first.
  GtaivSyncSaves("before the game");

  // The game's log: ~/Library/Logs/gtaiv.log unless given (main.cpp).
  setenv("GTAIV_LOG", [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/gtaiv.log"].fileSystemRepresentation, 0);

  // A new instance of the app started by Launch Services, as a double click
  // would: macOS then treats the game as the game it is (Game Mode). Run
  // again in this process (execv), it did not.
  NSMutableArray<NSString*>* arguments =
      [NSMutableArray arrayWithObjects:@"--no-launcher", @"--game-dir", folder, nil];
  if (!args.game_args.empty()) {
    [arguments addObject:@"--"];
    for (const std::string& a : args.game_args) [arguments addObject:@(a.c_str())];
  }
  NSWorkspaceOpenConfiguration* config = [NSWorkspaceOpenConfiguration configuration];
  config.createsNewApplicationInstance = YES;
  config.arguments = arguments;
  config.environment = NSProcessInfo.processInfo.environment;  // with the options set above
  config.activates = YES;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  __block NSError* failed = nil;
  [NSWorkspace.sharedWorkspace openApplicationAtURL:NSBundle.mainBundle.bundleURL
                                      configuration:config
                                  completionHandler:^(NSRunningApplication*, NSError* error) {
                                    failed = error;
                                    dispatch_semaphore_signal(done);
                                  }];
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  if (failed) std::fprintf(stderr, "gtaiv: launching the game: %s\n", failed.localizedDescription.UTF8String);
  std::fflush(nullptr);
  std::exit(failed ? 1 : 0);
}

}  // namespace

@interface GtaivLauncher : NSObject <NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTextField* folderField;
@property(strong) NSTextField* problem;
@property(strong) NSPopUpButton* display;
@property(strong) NSPopUpButton* vsync;
@property(strong) NSPopUpButton* antialiasing;
@property(strong) NSPopUpButton* metalfxScale;
@property(strong) NSPopUpButton* taaSharpening;
@property(strong) NSButton* hud;
@property(strong) NSButton* skipIntro;
@property(strong) NSButton* continueSave;
@property(strong) NSButton* originalRadio;
@property(strong) NSTextField* radioNote;
@property(strong) NSTabView* tabs;
@property(strong) NSPopUpButton* shadows;
@property(strong) NSPopUpButton* reflections;
@property(strong) NSPopUpButton* fov;
@property(strong) NSButton* fastLoading;
@property(strong) NSButton* stunPunch;
@property(strong) NSButton* executions;
@property(strong) NSMutableArray<NSButton*>* fixes;
@property(strong) NSMutableArray<NSPopUpButton*>* shaderChoices;
@property(strong) NSButton* skip;
@property(strong) NSButton* play;
@property(copy) NSString* folder;
@end

// A document view laid out from the top (the Advanced tab scrolls).
@interface GtaivFlippedView : NSView
@end
@implementation GtaivFlippedView
- (BOOL)isFlipped {
  return YES;
}
@end

@implementation GtaivLauncher

// The window's width; notes wrap within the grid's second column.
constexpr CGFloat kWindowWidth = 760, kNoteWidth = 520;

- (NSTextField*)label:(NSString*)text {
  NSTextField* t = [NSTextField labelWithString:text];
  t.alignment = NSTextAlignmentRight;
  return t;
}

- (NSPopUpButton*)popup:(NSArray<NSString*>*)items selected:(NSInteger)i {
  NSPopUpButton* p = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  [p addItemsWithTitles:items];
  [p selectItemAtIndex:i];
  return p;
}

- (void)build:(NSUserDefaults*)d {
  self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 620, 560)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
  self.window.title = @"Liberty Pro Max";
  self.window.delegate = self;

  NSTextField* title = [NSTextField labelWithString:@"Liberty Pro Max"];
  title.font = [NSFont systemFontOfSize:22 weight:NSFontWeightBold];
  NSTextField* subtitle = [NSTextField labelWithString:@"Grand Theft Auto IV, running natively on your Mac"];
  subtitle.textColor = NSColor.secondaryLabelColor;

  self.folderField = [NSTextField labelWithString:@""];
  self.folderField.lineBreakMode = NSLineBreakByTruncatingMiddle;
  [self.folderField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                             forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSButton* choose = [NSButton buttonWithTitle:@"Choose…" target:self action:@selector(choose:)];
  NSStackView* folderRow = [NSStackView stackViewWithViews:@[ self.folderField, choose ]];

  self.display = [self popup:@[ @"Full screen", @"Window" ] selected:[d boolForKey:kWindowKey] ? 1 : 0];
  self.vsync = [self popup:@[ @"As set in the game", @"On", @"Off" ]
                  selected:std::clamp<NSInteger>([d integerForKey:kVsyncKey], 0, 2)];
  self.antialiasing = [self popup:@[
    @"Off", @"SMAA (light)", @"MetalFX (temporal)", @"TAA (temporal, light)", @"SMAA + MetalFX spatial (fast)"
  ]
                         selected:std::clamp<NSInteger>([d integerForKey:kAntiAliasingKey], 0, 4)];
  self.metalfxScale = [self popup:@[ @"Native (100%)", @"Quality (67%)", @"Balanced (58%)", @"Performance (50%)" ]
                         selected:std::clamp<NSInteger>([d integerForKey:kMetalFXScaleKey], 0, 3)];
  self.taaSharpening = [self popup:@[ @"Off", @"Low", @"Medium", @"High" ]
                          selected:std::clamp<NSInteger>([d integerForKey:kTaaSharpeningKey], 0, 3)];
  NSTextField* aaNote = [NSTextField
      wrappingLabelWithString:@"SMAA smooths edges cheaply. TAA also steadies wires, fences and leaves at little cost; "
                      @"MetalFX does it best and, below native resolution, also raises the frame rate. "
                      @"SMAA + MetalFX spatial upscales without history: the fastest below native resolution."];
  aaNote.textColor = NSColor.secondaryLabelColor;
  aaNote.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  self.hud = [NSButton checkboxWithTitle:@"Show the performance overlay (frame rate, frame times)"
                                  target:nil
                                  action:nil];
  self.hud.state = [d boolForKey:kHudKey] ? NSControlStateValueOn : NSControlStateValueOff;

  self.skipIntro = [NSButton checkboxWithTitle:@"Skip the logo and legal screens" target:nil action:nil];
  self.skipIntro.state = [d boolForKey:kSkipIntroKey] ? NSControlStateValueOn : NSControlStateValueOff;
  self.continueSave = [NSButton checkboxWithTitle:@"Continue from the last save (no main menu)"
                                          target:nil
                                          action:nil];
  self.continueSave.state = [d boolForKey:kContinueKey] ? NSControlStateValueOn : NSControlStateValueOff;

  // The 2008 radio the 2018 update cut: served from the imported payload
  // while the game runs (radio.cpp); a no-op until it is imported.
  self.originalRadio = [NSButton checkboxWithTitle:@"Original 2008 radio songs (Vladivostok FM and the rest)"
                                           target:nil
                                           action:nil];
  self.originalRadio.state = [d boolForKey:kRadioKey] ? NSControlStateValueOn : NSControlStateValueOff;
  self.radioNote = [NSTextField
      wrappingLabelWithString:HasRadioPayload()
          ? @"The songs the 2018 update cut, played from files outside your install."
          : @"Not ready yet: import the payload with tools/radio_import.sh first."];
  self.radioNote.textColor = HasRadioPayload() ? NSColor.secondaryLabelColor : NSColor.systemRedColor;
  self.radioNote.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];

  NSGridView* grid = [NSGridView gridViewWithViews:@[
    @[ [self label:@"Game folder:"], folderRow ],
    @[ [self label:@"Display:"], self.display ],
    @[ [self label:@"Vertical sync:"], self.vsync ],
    @[ [self label:@"Anti-aliasing:"], self.antialiasing ],
    @[ [self label:@"MetalFX resolution:"], self.metalfxScale ],
    @[ NSGridCell.emptyContentView, aaNote ],
    @[ [self label:@"TAA sharpening:"], self.taaSharpening ],
    @[ NSGridCell.emptyContentView, self.hud ],
    @[ [self label:@"Start:"], self.skipIntro ],
    @[ NSGridCell.emptyContentView, self.continueSave ],
    @[ [self label:@"Radio:"], self.originalRadio ],
    @[ NSGridCell.emptyContentView, self.radioNote ],
  ]];
  grid.rowSpacing = 10;
  grid.columnSpacing = 10;
  [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  grid.rowAlignment = NSGridRowAlignmentFirstBaseline;

  // Advanced: the port's improvements, each on its own.
  self.shadows = [self popup:@[ @"As in the game", @"High (twice the resolution)" ]
                    selected:std::clamp<NSInteger>([d integerForKey:kShadowsKey], 0, 1)];
  NSTextField* shadowNote = [NSTextField labelWithString:@"Sharper shadow edges; about 3 ms of GPU time a frame."];
  self.reflections = [self popup:@[ @"As in the game", @"High (twice the resolution)" ]
                        selected:std::clamp<NSInteger>([d integerForKey:kReflectionsKey], 0, 1)];
  NSTextField* reflNote = [NSTextField labelWithString:@"Sharper reflections on water and cars."];
  self.fov = [self popup:@[ @"As in the game", @"+5°", @"+10°", @"+15°", @"+20°" ]
                selected:std::clamp<NSInteger>([d integerForKey:kFovKey], 0, 4)];
  NSTextField* fovNote = [NSTextField labelWithString:@"A wider view for wide and ultrawide screens."];
  self.fastLoading = [NSButton checkboxWithTitle:@"Fast loading" target:nil action:nil];
  self.fastLoading.state = [d boolForKey:kFastLoadingKey] ? NSControlStateValueOn : NSControlStateValueOff;
  NSTextField* loadNote = [NSTextField labelWithString:@"No idle waits while loading screens are up."];
  self.stunPunch = [NSButton checkboxWithTitle:@"Stun punch on anyone" target:nil action:nil];
  self.stunPunch.state = [d boolForKey:kStunPunchKey] ? NSControlStateValueOn : NSControlStateValueOff;
  self.executions = [NSButton checkboxWithTitle:@"Executions on anyone" target:nil action:nil];
  self.executions.state = [d boolForKey:kExecutionsKey] ? NSControlStateValueOn : NSControlStateValueOff;
  NSTextField* meleeNote = [NSTextField
      wrappingLabelWithString:@"Moves the game keeps for a few missions: sneak up unarmed (or with a pistol), lock on, attack."];
  for (NSTextField* n : @[ aaNote, self.radioNote, meleeNote ]) n.preferredMaxLayoutWidth = kNoteWidth;
  for (NSTextField* n : @[ shadowNote, reflNote, fovNote, loadNote, meleeNote ]) {
    n.textColor = NSColor.secondaryLabelColor;
    n.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  }
  NSMutableArray<NSArray<NSView*>*>* rows = [NSMutableArray arrayWithArray:@[
    @[ [self label:@"Shadows:"], self.shadows ],
    @[ NSGridCell.emptyContentView, shadowNote ],
    @[ [self label:@"Reflections:"], self.reflections ],
    @[ NSGridCell.emptyContentView, reflNote ],
    @[ [self label:@"Field of view:"], self.fov ],
    @[ NSGridCell.emptyContentView, fovNote ],
    @[ [self label:@"Loading:"], self.fastLoading ],
    @[ NSGridCell.emptyContentView, loadNote ],
    @[ [self label:@"Combat:"], self.stunPunch ],
    @[ NSGridCell.emptyContentView, self.executions ],
    @[ NSGridCell.emptyContentView, meleeNote ],
  ]];
  self.shaderChoices = [NSMutableArray new];
  for (const ShaderChoice& sc : kShaderChoices) {
    NSPopUpButton* p = [self popup:sc.items
                          selected:std::clamp<NSInteger>([d integerForKey:sc.key], 0, NSInteger(sc.items.count) - 1)];
    [self.shaderChoices addObject:p];
    [rows addObject:@[ [self label:sc.title], p ]];
  }
  NSTextField* shaderNote = [NSTextField labelWithString:@"With FusionFix's shaders (in the update folder)."];
  shaderNote.textColor = NSColor.secondaryLabelColor;
  shaderNote.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  [rows addObject:@[ NSGridCell.emptyContentView, shaderNote ]];
  self.fixes = [NSMutableArray new];
  for (const Fix& f : kFixes) {
    NSButton* b = [NSButton checkboxWithTitle:f.title target:nil action:nil];
    b.state = [d boolForKey:f.key] ? NSControlStateValueOn : NSControlStateValueOff;
    [self.fixes addObject:b];
    [rows addObject:@[ self.fixes.count == 1 ? [self label:@"Fixes:"] : NSGridCell.emptyContentView, b ]];
  }
  NSGridView* adv = [NSGridView gridViewWithViews:rows];
  adv.rowSpacing = 10;
  adv.columnSpacing = 10;
  [adv columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
  adv.rowAlignment = NSGridRowAlignmentFirstBaseline;

  // The Advanced grid scrolls: it outgrows the tab.
  NSScrollView* advScroll = [NSScrollView new];
  advScroll.hasVerticalScroller = YES;
  advScroll.drawsBackground = NO;
  advScroll.borderType = NSNoBorder;
  GtaivFlippedView* advDoc = [GtaivFlippedView new];
  adv.translatesAutoresizingMaskIntoConstraints = NO;
  [advDoc addSubview:adv];
  advDoc.translatesAutoresizingMaskIntoConstraints = NO;
  advScroll.documentView = advDoc;
  [NSLayoutConstraint activateConstraints:@[
    [adv.leadingAnchor constraintEqualToAnchor:advDoc.leadingAnchor],
    [adv.topAnchor constraintEqualToAnchor:advDoc.topAnchor],
    [adv.bottomAnchor constraintEqualToAnchor:advDoc.bottomAnchor constant:-14],
    [advDoc.widthAnchor constraintEqualToAnchor:advScroll.contentView.widthAnchor],
  ]];

  self.tabs = [NSTabView new];
  for (NSArray* tab in @[ @[ @"General", grid ], @[ @"Advanced", advScroll ] ]) {
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:tab[0]];
    item.label = tab[0];
    NSView* page = [NSView new];
    NSView* g = tab[1];
    g.translatesAutoresizingMaskIntoConstraints = NO;
    [page addSubview:g];
    [NSLayoutConstraint activateConstraints:@[
      [g.leadingAnchor constraintEqualToAnchor:page.leadingAnchor constant:14],
      [g.topAnchor constraintEqualToAnchor:page.topAnchor constant:14],
    ]];
    if (g == advScroll)  // fills the page, scrolls inside
      [NSLayoutConstraint activateConstraints:@[
        [g.trailingAnchor constraintEqualToAnchor:page.trailingAnchor constant:-4],
        [g.bottomAnchor constraintEqualToAnchor:page.bottomAnchor constant:-4],
      ]];
    else  // the page grows to the whole grid
      [NSLayoutConstraint activateConstraints:@[
        [g.trailingAnchor constraintLessThanOrEqualToAnchor:page.trailingAnchor constant:-14],
        [g.bottomAnchor constraintLessThanOrEqualToAnchor:page.bottomAnchor constant:-14],
      ]];
    item.view = page;
    [self.tabs addTabViewItem:item];
  }

  self.problem = [NSTextField labelWithString:@""];
  self.problem.textColor = NSColor.systemRedColor;

  self.skip = [NSButton checkboxWithTitle:@"Skip this window next time (hold Option at launch to see it again)"
                                   target:nil
                                   action:nil];
  self.skip.state = [d boolForKey:kSkipKey] ? NSControlStateValueOn : NSControlStateValueOff;

  NSButton* quit = [NSButton buttonWithTitle:@"Quit" target:self action:@selector(quit:)];
  quit.keyEquivalent = @"\e";
  self.play = [NSButton buttonWithTitle:@"Play" target:self action:@selector(play:)];
  self.play.keyEquivalent = @"\r";
  NSView* spacer = [NSView new];
  [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView* buttons = [NSStackView stackViewWithViews:@[ spacer, quit, self.play ]];

  NSStackView* all = [NSStackView stackViewWithViews:@[ title, subtitle, self.tabs, self.problem, self.skip, buttons ]];
  all.orientation = NSUserInterfaceLayoutOrientationVertical;
  all.alignment = NSLayoutAttributeLeading;
  all.spacing = 14;
  [all setCustomSpacing:4 afterView:title];
  [all setCustomSpacing:20 afterView:subtitle];
  all.edgeInsets = NSEdgeInsetsMake(24, 28, 22, 28);
  all.translatesAutoresizingMaskIntoConstraints = NO;
  NSView* content = self.window.contentView;
  [content addSubview:all];
  [NSLayoutConstraint activateConstraints:@[
    [all.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
    [all.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    [all.topAnchor constraintEqualToAnchor:content.topAnchor],
    [all.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
    [buttons.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-56],
    [self.tabs.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-56],
    [self.tabs.heightAnchor constraintGreaterThanOrEqualToConstant:360],
    [all.widthAnchor constraintEqualToConstant:kWindowWidth],
    [self.folderField.widthAnchor constraintGreaterThanOrEqualToConstant:300],
  ]];
  [self showFolder];
  // As tall as its content (the General tab's grid sets the minimum).
  [self.window layoutIfNeeded];
  [self.window setContentSize:all.fittingSize];
  [self.window center];
}

- (void)showFolder {
  self.folderField.stringValue = self.folder.length ? self.folder.stringByAbbreviatingWithTildeInPath : @"Not chosen";
  const bool ok = HasGame(self.folder);
  self.problem.stringValue = ok ? @"" : @"Choose the folder of your Grand Theft Auto IV (PC) install: the one with GTAIV.exe.";
  self.problem.hidden = ok;
  self.play.enabled = ok;
}

- (void)choose:(id)sender {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseDirectories = YES;
  panel.canChooseFiles = NO;
  panel.message = @"Choose the folder with GTAIV.exe";
  if (self.folder.length) panel.directoryURL = [NSURL fileURLWithPath:self.folder];
  if ([panel runModal] == NSModalResponseOK) {
    self.folder = panel.URL.path;
    [self showFolder];
  }
}

- (void)play:(id)sender {
  NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
  [d setObject:self.folder forKey:kFolderKey];
  [d setBool:self.display.indexOfSelectedItem == 1 forKey:kWindowKey];
  [d setInteger:self.vsync.indexOfSelectedItem forKey:kVsyncKey];
  [d setInteger:self.antialiasing.indexOfSelectedItem forKey:kAntiAliasingKey];
  [d setInteger:self.metalfxScale.indexOfSelectedItem forKey:kMetalFXScaleKey];
  [d setInteger:self.taaSharpening.indexOfSelectedItem forKey:kTaaSharpeningKey];
  [d setBool:self.hud.state == NSControlStateValueOn forKey:kHudKey];
  [d setBool:self.skipIntro.state == NSControlStateValueOn forKey:kSkipIntroKey];
  [d setBool:self.continueSave.state == NSControlStateValueOn forKey:kContinueKey];
  [d setBool:self.originalRadio.state == NSControlStateValueOn forKey:kRadioKey];
  [d setInteger:self.shadows.indexOfSelectedItem forKey:kShadowsKey];
  [d setInteger:self.reflections.indexOfSelectedItem forKey:kReflectionsKey];
  [d setInteger:self.fov.indexOfSelectedItem forKey:kFovKey];
  [d setBool:self.fastLoading.state == NSControlStateValueOn forKey:kFastLoadingKey];
  [d setBool:self.stunPunch.state == NSControlStateValueOn forKey:kStunPunchKey];
  [d setBool:self.executions.state == NSControlStateValueOn forKey:kExecutionsKey];
  for (NSUInteger i = 0; i < self.fixes.count; i++)
    [d setBool:self.fixes[i].state == NSControlStateValueOn forKey:kFixes[i].key];
  for (NSUInteger i = 0; i < self.shaderChoices.count; i++)
    [d setInteger:self.shaderChoices[i].indexOfSelectedItem forKey:kShaderChoices[i].key];
  [d setBool:self.skip.state == NSControlStateValueOn forKey:kSkipKey];
  [d synchronize];
  [NSApp stopModalWithCode:NSModalResponseOK];
}

- (void)quit:(id)sender {
  [NSApp stopModalWithCode:NSModalResponseCancel];
}

- (void)windowWillClose:(NSNotification*)n {
  [NSApp stopModalWithCode:NSModalResponseCancel];
}

@end

// main.cpp: before anything else. Returns if there is no launcher to show
// (not run from the app bundle, or --no-launcher); otherwise starts the
// game in a new process image or exits.
void RunLauncherIfWanted(int argc, char** argv) {
  for (int i = 1; i < argc; i++)
    if (!std::strcmp(argv[i], "--no-launcher")) return;
  @autoreleasepool {
    if (![NSBundle.mainBundle.bundlePath.pathExtension isEqualToString:@"app"]) return;
    const Args args = ParseArgs(argc, argv);
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    RegisterDefaults(d);
    if ([d objectForKey:kMetalFXKey] && ![d objectForKey:kAntiAliasingKey])  // the older on/off setting
      [d setInteger:[d boolForKey:kMetalFXKey] ? 2 : 0 forKey:kAntiAliasingKey];
    // The folder: given, else the last one, else the repository's run/game
    // next to the build ("out/build/<preset>/Liberty Pro Max.app").
    NSString* folder = args.game_dir ?: [d stringForKey:kFolderKey];
    if (!HasGame(folder)) {
      NSString* repo = [[[NSBundle.mainBundle.bundlePath stringByDeletingLastPathComponent]
          stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
      NSString* guess = [repo.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"run/game"];
      if (HasGame(guess)) folder = guess;
    }

    const bool option = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
    if ([d boolForKey:kSkipKey] && HasGame(folder) && !option) {
      Launch(folder, d, args);
      std::exit(1);
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSMenu* bar = [NSMenu new];
    NSMenuItem* appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu* appMenu = [NSMenu new];
    [appMenu addItemWithTitle:@"Quit Liberty Pro Max" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    NSApp.mainMenu = bar;

    GtaivLauncher* l = [GtaivLauncher new];
    l.folder = folder;
    [l build:d];
    // Dev aids: GTAIV_LAUNCHER_SNAPSHOT=file.png draws the window there
    // (unshown) and exits; GTAIV_LAUNCHER_AUTOPLAY=1 presses Play.
    if (const char* snap = std::getenv("GTAIV_LAUNCHER_SNAPSHOT")) {
      NSView* v = l.window.contentView;
      [l.window layoutIfNeeded];
      v.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
      NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
      NSGraphicsContext* ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
      [NSGraphicsContext saveGraphicsState];
      NSGraphicsContext.currentContext = ctx;
      [NSColor.windowBackgroundColor setFill];
      NSRectFill(v.bounds);
      [NSGraphicsContext restoreGraphicsState];
      [v displayRectIgnoringOpacity:v.bounds inContext:ctx];
      [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(snap) atomically:YES];
      if (const char* snap2 = std::getenv("GTAIV_LAUNCHER_SNAPSHOT_ADVANCED")) {
        [l.tabs selectTabViewItemAtIndex:1];
        [l.window layoutIfNeeded];
        NSBitmapImageRep* rep2 = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        NSGraphicsContext* ctx2 = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep2];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = ctx2;
        [NSColor.windowBackgroundColor setFill];
        NSRectFill(v.bounds);
        [NSGraphicsContext restoreGraphicsState];
        [v displayRectIgnoringOpacity:v.bounds inContext:ctx2];
        [[rep2 representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(snap2) atomically:YES];
      }
      std::exit(0);
    }
    if (const char* a = std::getenv("GTAIV_LAUNCHER_AUTOPLAY"); a && *a == '1') {
      Launch(l.folder, d, args);
      std::exit(1);
    }
    [l.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    const NSModalResponse r = [NSApp runModalForWindow:l.window];
    if (r != NSModalResponseOK) std::exit(0);
    [l.window orderOut:nil];
    Launch(l.folder, d, args);
    NSAlert* a = [NSAlert new];
    a.messageText = @"The game could not be started.";
    a.informativeText = @(std::strerror(errno));
    [a runModal];
    std::exit(1);
  }
}
