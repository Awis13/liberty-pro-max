// The Mac app's launcher: the first window of "Liberty Pro Max.app". It
// picks the game folder and the port's options, then starts the game as a
// new instance of the app with them (--no-launcher, through Launch
// Services): the game gets a process of its own from the start, so options
// read when it loads (the Metal HUD) apply, SDL sets up the application as
// it expects, and macOS turns Game Mode on for it.
//
// Shown when the executable runs from the app bundle; with "Skip this
// window" set, only while Option is held at launch.
//
// While it is open, the shaders and pipelines earlier runs compiled are
// compiled again in the background if the caches for this macOS version
// and GPU lack them (after an OS update, they are all gone): PrewarmCaches
// (runtime/gfx/prewarm.h), shown as a line under the buttons. Play or
// Quit stops it at once; the game, a new instance of this app, shares
// macOS's cache of compiled shaders with it.

#import <AppKit/AppKit.h>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "launcher_options.h"
#include "save_paths.h"
#include "runtime/gfx/prewarm.h"

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
      // nil if the path is not valid UTF-8: the saved folder is used.
      a.game_dir = [NSString stringWithUTF8String:argv[++i]];
      if (!a.game_dir) std::fprintf(stderr, "gtaiv: --game-dir is not valid UTF-8, ignored\n");
    } else if (!std::strcmp(argv[i], "--")) {
      for (i++; i < argc; i++) a.game_args.push_back(argv[i]);
    }
  }
  return a;
}

// Stores a choice only where it is not the default (RegisterDefaults; a
// key without one reads as 0): an option left at its default follows it
// when a later version changes it.
void Store(NSUserDefaults* d, NSString* key, id value) {
  id def = [[d volatileDomainForName:NSRegistrationDomain] objectForKey:key];
  if ([value isEqual:def ?: @0]) [d removeObjectForKey:key];
  else [d setObject:value forKey:key];
}

// Set once the settings hold only what differs from the defaults.
NSString* const kChangesOnlyKey = @"StoredChangesOnly";

extern "C" void GtaivSyncSaves(const char* when);  // save_sync_mac.mm

// Runs the game: this executable again, with the options in its
// environment. Returns only if that failed.
void Launch(NSString* folder, NSUserDefaults* d, const Args& args) {
  ApplyOptions(d);
  // The newest saves from the player's iPad and Apple TV first.
  GtaivSyncSaves("before the game");

  // The game's log: ~/Library/Logs/gtaiv.log unless given (main.cpp). The
  // last runs' logs are kept beside it, gtaiv.1.log the run before, up to
  // gtaiv.4.log: a crash's log is still there after the next start.
  if (!std::getenv("GTAIV_LOG")) {
    NSString* logs = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs"];
    auto path = [&](int k) {
      return std::string([logs stringByAppendingPathComponent:(k ? [NSString stringWithFormat:@"gtaiv.%d.log", k]
                                                                 : @"gtaiv.log")]
                             .fileSystemRepresentation);
    };
    for (int k = 3; k >= 0; k--) std::rename(path(k).c_str(), path(k + 1).c_str());
    setenv("GTAIV_LOG", path(0).c_str(), 0);
  }

  // A new instance of the app started by Launch Services, as a double click
  // would: macOS then treats the game as the game it is (Game Mode). Run
  // again in this process (execv), it did not.
  NSMutableArray<NSString*>* arguments =
      [NSMutableArray arrayWithObjects:@"--no-launcher", @"--game-dir", folder, nil];
  // The game's own arguments are put together in the game's process
  // (main.cpp, on --no-launcher): the port's defaults, the player's
  // commandline.txt as the game reads it (its bytes, whatever their
  // encoding), then those given here after "--", passed on.
  if (!args.game_args.empty()) {
    [arguments addObject:@"--"];
    for (const std::string& a : args.game_args) {
      // An argument that is not valid UTF-8 has no NSString to pass on.
      if (NSString* s = [NSString stringWithUTF8String:a.c_str()]) [arguments addObject:s];
      else std::fprintf(stderr, "gtaiv: a game argument is not valid UTF-8, left out\n");
    }
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

// The shaders prepared while the launcher is open (PrewarmCaches): its
// stop flag, and its end (nil: not started).
std::atomic<bool> g_prewarm_cancel{false};
dispatch_semaphore_t g_prewarm_done = nil;

// Stopped, waited for 200 ms at most (it stops within about one compile).
void StopPrewarm() {
  if (!g_prewarm_done) return;
  g_prewarm_cancel = true;
  if (dispatch_semaphore_wait(g_prewarm_done, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)) == 0)
    dispatch_semaphore_signal(g_prewarm_done);  // (for a second caller)
}

}  // namespace

@interface GtaivLauncher : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) NSTextField* folderField;
@property(strong) NSTextField* problem;
@property(strong) NSPopUpButton* display;
@property(strong) NSPopUpButton* vsync;
@property(strong) NSPopUpButton* frameLimit;
@property(strong) NSPopUpButton* language;
@property(strong) NSPopUpButton* antialiasing;
@property(strong) NSPopUpButton* metalfxScale;
@property(strong) NSPopUpButton* sharpening;
@property(strong) NSPopUpButton* consoleGamma;
@property(strong) NSButton* hud;
@property(strong) NSButton* skipIntro;
@property(strong) NSButton* continueSave;
@property(strong) NSButton* overNotch;
@property(strong) NSButton* mods;
@property(strong) NSTextField* modsNote;
@property(strong) NSTextField* episodesNote;
@property(strong) NSGridRow* episodesNoteRow;
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
@property(strong) NSTextField* preparing;
@property(strong) NSStackView* stack;
@property(copy) NSString* folder;
@property NSModalResponse response;
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
  self.window.releasedWhenClosed = NO;
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
  self.frameLimit = [self popup:@[ @"Off", @"30 fps", @"60 fps", @"120 fps" ]
                       selected:std::clamp<NSInteger>([d integerForKey:kFrameLimitKey], 0, 3)];
  self.language = [self popup:@[ @"Same as the system", @"English" ] selected:[d boolForKey:kSystemLanguageKey] ? 0 : 1];
  self.antialiasing = [self popup:@[
    @"Off", @"SMAA (light)", @"MetalFX (temporal)", @"TAA (temporal, light)", @"SMAA + MetalFX spatial (fast)"
  ]
                         selected:std::clamp<NSInteger>([d integerForKey:kAntiAliasingKey], 0, 4)];
  self.antialiasing.target = self;
  self.antialiasing.action = @selector(antialiasingChanged:);
  self.metalfxScale = [self popup:@[ @"Native (100%)", @"Quality (67%)", @"Balanced (58%)", @"Performance (50%)" ]
                         selected:std::clamp<NSInteger>([d integerForKey:kMetalFXScaleKey], 0, 3)];
  self.sharpening = [self popup:@[ @"Off", @"Low", @"Medium", @"High" ]
                       selected:std::clamp<NSInteger>([d integerForKey:kTaaSharpeningKey], 0, 3)];
  [self antialiasingChanged:nil];
  self.consoleGamma = [self popup:@[ @"Off", @"Xbox 360", @"PS3" ]
                         selected:std::clamp<NSInteger>([d integerForKey:kConsoleGammaKey], 0, 2)];
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
  // macOS keeps a full screen Space below a display's notch; a borderless
  // window covers it all. (Shown only with a notch, below.)
  self.overNotch = [NSButton checkboxWithTitle:@"Use the whole screen, behind the notch" target:nil action:nil];
  self.overNotch.state = [d boolForKey:kOverNotchKey] ? NSControlStateValueOn : NSControlStateValueOff;
  self.continueSave = [NSButton checkboxWithTitle:@"Continue from the last save (no main menu)"
                                          target:nil
                                          action:nil];
  self.continueSave.state = [d boolForKey:kContinueKey] ? NSControlStateValueOn : NSControlStateValueOff;

  // The player's mods (mods.cpp).
  self.mods = [NSButton checkboxWithTitle:@"Load mods" target:nil action:nil];
  self.mods.state = [d boolForKey:kModsKey] ? NSControlStateValueOn : NSControlStateValueOff;
  NSButton* modsFolder = [NSButton buttonWithTitle:@"Open Mods Folder" target:self action:@selector(openMods:)];
  self.modsNote = [NSTextField
      wrappingLabelWithString:@"Each folder there is a mod: its files replace the game's at the same path. "
                               "Your game folder is not changed."];
  self.modsNote.textColor = NSColor.secondaryLabelColor;
  self.modsNote.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  // The folder the game reads its saves from (SGTA400 to SGTA414).
  NSButton* savesFolder = [NSButton buttonWithTitle:@"Open Saves Folder" target:self action:@selector(openSaves:)];

  NSGridView* grid = [NSGridView gridViewWithViews:@[
    @[ [self label:@"Game folder:"], folderRow ],
    @[ [self label:@"Display:"], self.display ],
    @[ NSGridCell.emptyContentView, self.overNotch ],
    @[ [self label:@"Vertical sync:"], self.vsync ],
    @[ [self label:@"Frame limit:"], self.frameLimit ],
    @[ [self label:@"Anti-aliasing:"], self.antialiasing ],
    @[ [self label:@"MetalFX resolution:"], self.metalfxScale ],
    @[ NSGridCell.emptyContentView, aaNote ],
    @[ [self label:@"Sharpening:"], self.sharpening ],
    @[ [self label:@"Console gamma:"], self.consoleGamma ],
    @[ NSGridCell.emptyContentView, self.hud ],
    @[ [self label:@"Start:"], self.skipIntro ],
    @[ NSGridCell.emptyContentView, self.continueSave ],
    @[ [self label:@"Language:"], self.language ],
    @[ [self label:@"Mods:"], self.mods ],
    @[ NSGridCell.emptyContentView, modsFolder ],
    @[ NSGridCell.emptyContentView, self.modsNote ],
    @[ [self label:@"Saves:"], savesFolder ],
  ]];
  grid.rowSpacing = 10;
  BOOL notch = NO;
  for (NSScreen* s in NSScreen.screens) notch = notch || s.safeAreaInsets.top > 0;
  [grid cellForView:self.overNotch].row.hidden = !notch;
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
  // Under the episodes' option, only for a game folder with an episode's
  // folder the game will not take (showFolder).
  self.episodesNote = [NSTextField wrappingLabelWithString:@""];
  for (NSTextField* n : @[ aaNote, self.modsNote, meleeNote, self.episodesNote ]) n.preferredMaxLayoutWidth = kNoteWidth;
  for (NSTextField* n : @[ shadowNote, reflNote, fovNote, loadNote, meleeNote, self.episodesNote ]) {
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
    [self.shaderChoices addObject:p];  // (not shown: FixShown)
  }
  // The fixes on the Advanced tab, the port's own additions on Extras.
  self.fixes = [NSMutableArray new];
  NSMutableArray<NSArray<NSView*>*>* extraRows = [NSMutableArray new];
  BOOL fixesLabelled = NO;
  for (const Fix& f : kFixes) {
    NSButton* b = [NSButton checkboxWithTitle:f.title target:nil action:nil];
    b.state = [d boolForKey:f.key] ? NSControlStateValueOn : NSControlStateValueOff;
    [self.fixes addObject:b];
    if (f.extra) {
      // The tab's width: longer titles wrap rather than end in "...".
      b.lineBreakMode = NSLineBreakByWordWrapping;
      [b.widthAnchor constraintLessThanOrEqualToConstant:kWindowWidth - 100].active = YES;
      [extraRows addObject:@[ b ]];
      if ([f.key isEqualToString:@"Episodes"]) [extraRows addObject:@[ self.episodesNote ]];
    } else if (AdvancedFixShown(f)) {
      [rows addObject:@[ fixesLabelled ? NSGridCell.emptyContentView : [self label:@"Fixes:"], b ]];
      fixesLabelled = YES;
    }
  }
  NSGridView* extras = [NSGridView gridViewWithViews:extraRows];
  extras.rowSpacing = 10;
  // The note is indented under its checkbox.
  NSGridCell* noteCell = [extras cellForView:self.episodesNote];
  self.episodesNoteRow = noteCell.row;
  noteCell.xPlacement = NSGridCellPlacementNone;
  noteCell.customPlacementConstraints =
      @[ [self.episodesNote.leadingAnchor constraintEqualToAnchor:extras.leadingAnchor constant:20] ];
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
  for (NSArray* tab in @[ @[ @"General", grid ], @[ @"Extras", extras ], @[ @"Advanced", advScroll ] ]) {
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

  // The shaders being prepared (StartPrewarm), shown only meanwhile.
  self.preparing = [NSTextField labelWithString:@""];
  self.preparing.textColor = NSColor.secondaryLabelColor;
  self.preparing.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
  self.preparing.hidden = YES;

  NSStackView* all = [NSStackView
      stackViewWithViews:@[ title, subtitle, self.tabs, self.problem, self.skip, buttons, self.preparing ]];
  self.stack = all;
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

// The line under the buttons: `text`, or none (nil). The window grows or
// shrinks by its height, its top staying where it is.
- (void)showPreparing:(NSString*)text {
  const BOOL hide = text == nil;
  if (!hide) self.preparing.stringValue = text;
  if (self.preparing.hidden == hide) return;
  self.preparing.hidden = hide;
  [self.window layoutIfNeeded];
  NSRect frame = self.window.frame;
  const NSRect content = [self.window contentRectForFrameRect:frame];
  NSRect fitted = [self.window frameRectForContentRect:NSMakeRect(content.origin.x, content.origin.y,
                                                                  content.size.width, self.stack.fittingSize.height)];
  fitted.origin.y = NSMaxY(frame) - fitted.size.height;
  [self.window setFrame:fitted display:YES];
}

- (void)showFolder {
  self.folderField.stringValue = self.folder.length ? self.folder.stringByAbbreviatingWithTildeInPath : @"Not chosen";
  const bool ok = HasGame(self.folder);
  self.problem.stringValue = ok ? @"" : @"Choose the folder of your Grand Theft Auto IV (PC) install: the one with GTAIV.exe.";
  self.problem.hidden = ok;
  self.play.enabled = ok;
  NSString* note = EpisodesNote(self.folder);
  self.episodesNote.stringValue = note ?: @"";
  self.episodesNoteRow.hidden = note == nil;
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
  Store(d, kWindowKey, @(self.display.indexOfSelectedItem == 1));
  Store(d, kOverNotchKey, @(self.overNotch.state == NSControlStateValueOn));
  Store(d, kVsyncKey, @(self.vsync.indexOfSelectedItem));
  Store(d, kFrameLimitKey, @(self.frameLimit.indexOfSelectedItem));
  Store(d, kSystemLanguageKey, @(self.language.indexOfSelectedItem == 0));
  Store(d, kAntiAliasingKey, @(self.antialiasing.indexOfSelectedItem));
  Store(d, kMetalFXScaleKey, @(self.metalfxScale.indexOfSelectedItem));
  Store(d, kTaaSharpeningKey, @(self.sharpening.indexOfSelectedItem));
  Store(d, kConsoleGammaKey, @(self.consoleGamma.indexOfSelectedItem));
  Store(d, kHudKey, @(self.hud.state == NSControlStateValueOn));
  Store(d, kSkipIntroKey, @(self.skipIntro.state == NSControlStateValueOn));
  Store(d, kContinueKey, @(self.continueSave.state == NSControlStateValueOn));
  Store(d, kModsKey, @(self.mods.state == NSControlStateValueOn));
  Store(d, kShadowsKey, @(self.shadows.indexOfSelectedItem));
  Store(d, kReflectionsKey, @(self.reflections.indexOfSelectedItem));
  Store(d, kFovKey, @(self.fov.indexOfSelectedItem));
  Store(d, kFastLoadingKey, @(self.fastLoading.state == NSControlStateValueOn));
  Store(d, kStunPunchKey, @(self.stunPunch.state == NSControlStateValueOn));
  Store(d, kExecutionsKey, @(self.executions.state == NSControlStateValueOn));
  for (NSUInteger i = 0; i < self.fixes.count; i++)
    Store(d, kFixes[i].key, @(self.fixes[i].state == NSControlStateValueOn));
  for (NSUInteger i = 0; i < self.shaderChoices.count; i++)
    Store(d, kShaderChoices[i].key, @(self.shaderChoices[i].indexOfSelectedItem));
  Store(d, kSkipKey, @(self.skip.state == NSControlStateValueOn));
  [d synchronize];
  [self finishWithResponse:NSModalResponseOK];
}

// Sharpening follows TAA and MetalFX only: greyed with the others.
- (void)antialiasingChanged:(id)sender {
  self.sharpening.enabled = SharpeningApplies(self.antialiasing.indexOfSelectedItem);
}

- (void)openMods:(id)sender {
  [NSFileManager.defaultManager createDirectoryAtPath:ModsFolder() withIntermediateDirectories:YES
                                           attributes:nil error:nil];
  [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:ModsFolder()]];
}

// The saves' folder of the profile the game reads (save_paths.h), made if
// the game has not run yet.
- (void)openSaves:(id)sender {
  NSString* saves = [[DataFolder() stringByAppendingPathComponent:@(save_paths::kSavesRel)]
      stringByAppendingPathComponent:@(std::string(save_paths::kGameProfile).c_str())];
  [NSFileManager.defaultManager createDirectoryAtPath:saves withIntermediateDirectories:YES attributes:nil error:nil];
  [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:saves]];
}

// If the caches lack shaders earlier runs compiled: compiled again in the
// background, the count shown under the buttons until done.
- (void)startPrewarm {
  g_prewarm_done = dispatch_semaphore_create(0);
  dispatch_semaphore_t done = g_prewarm_done;
  __weak GtaivLauncher* weakSelf = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    if (!g_prewarm_cancel.load() && recomp::gfx::PrewarmNeeded()) {
      // The count, shown on the main thread (at most one update waiting).
      auto shown = std::make_shared<std::atomic<bool>>(false);
      auto counts = std::make_shared<std::atomic<uint64_t>>(0);
      recomp::gfx::PrewarmCaches(
          [weakSelf, shown, counts](size_t n, size_t total, const char*) {
            counts->store(uint64_t(n) << 32 | uint64_t(total));
            if (shown->exchange(true)) return;
            dispatch_async(dispatch_get_main_queue(), ^{
              shown->store(false);
              const uint64_t c = counts->load();
              if (!g_prewarm_cancel.load())
                [weakSelf showPreparing:[NSString stringWithFormat:@"Preparing shaders… %llu / %llu",
                                                                   (unsigned long long)(c >> 32),
                                                                   (unsigned long long)(c & 0xFFFFFFFFu)]];
            });
          },
          g_prewarm_cancel);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf showPreparing:nil];
    });
    dispatch_semaphore_signal(done);
  });
}

- (void)finishWithResponse:(NSModalResponse)response {
  self.response = response;
  [NSApp stop:nil];
  // Wake run: so it observes stop: even when no more user input arrives.
  [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
      modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}

- (void)quit:(id)sender {
  [self.window orderOut:nil];
  [self finishWithResponse:NSModalResponseCancel];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  // Accept system quit so logout and shutdown receive a successful reply.
  [self.window orderOut:nil];
  StopPrewarm();
  return NSTerminateNow;
}

- (BOOL)windowShouldClose:(NSWindow*)window {
  [self quit:nil];
  return YES;
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
    // The older on/off setting, before the defaults are registered (with
    // them, the anti-aliasing always has a value).
    if ([d objectForKey:kMetalFXKey] && ![d objectForKey:kAntiAliasingKey])
      [d setInteger:[d boolForKey:kMetalFXKey] ? 2 : 0 forKey:kAntiAliasingKey];
    RegisterDefaults(d);
    // Earlier versions stored every option on Play, the defaults too, so a
    // default changed later never reached them. Once: the options whose
    // default went from on to off are cleared if on, and every value equal
    // to its default goes (ClearedOnce; the older MetalFX setting too,
    // carried over above). Since then only changes are stored (Store): an
    // option the player never changed follows its default.
    if (![d boolForKey:kChangesOnlyKey]) {
      for (NSString* k in ClearedOnce(d.dictionaryRepresentation, [d volatileDomainForName:NSRegistrationDomain]))
        [d removeObjectForKey:k];
      [d removeObjectForKey:kMetalFXKey];
      [d setBool:YES forKey:kChangesOnlyKey];
    }
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
    NSMenuItem* quitItem = [appMenu addItemWithTitle:@"Quit Liberty Pro Max" action:@selector(quit:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    NSApp.mainMenu = bar;

    GtaivLauncher* l = [GtaivLauncher new];
    NSApp.delegate = l;
    quitItem.target = l;
    l.folder = folder;
    [l build:d];
    // Dev aids: GTAIV_LAUNCHER_SNAPSHOT=file.png draws the window there
    // (unshown) and exits, GTAIV_LAUNCHER_SNAPSHOT_EXTRAS and
    // GTAIV_LAUNCHER_SNAPSHOT_ADVANCED its other tabs to their files;
    // GTAIV_LAUNCHER_AUTOPLAY=1 presses Play.
    if (const char* snap = std::getenv("GTAIV_LAUNCHER_SNAPSHOT")) {
      NSView* v = l.window.contentView;
      v.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
      const std::pair<int, const char*> shots[] = {
          {0, snap},
          {1, std::getenv("GTAIV_LAUNCHER_SNAPSHOT_EXTRAS")},
          {2, std::getenv("GTAIV_LAUNCHER_SNAPSHOT_ADVANCED")},
      };
      for (const auto& [tab, file] : shots) {
        if (!file) continue;
        [l.tabs selectTabViewItemAtIndex:tab];
        [l.window layoutIfNeeded];
        NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
        NSGraphicsContext* ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = ctx;
        [NSColor.windowBackgroundColor setFill];
        NSRectFill(v.bounds);
        [NSGraphicsContext restoreGraphicsState];
        [v displayRectIgnoringOpacity:v.bounds inContext:ctx];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(file) atomically:YES];
      }
      std::exit(0);
    }
    if (const char* a = std::getenv("GTAIV_LAUNCHER_AUTOPLAY"); a && *a == '1') {
      Launch(l.folder, d, args);
      std::exit(1);
    }
    [l.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [l startPrewarm];
    // A modal session suppresses Command-Q and defers system quit Apple events.
    [NSApp run];
    StopPrewarm();  // Play or Quit: at once
    if (l.response != NSModalResponseOK) std::exit(0);
    [l.window orderOut:nil];
    Launch(l.folder, d, args);
    NSAlert* a = [NSAlert new];
    a.messageText = @"The game could not be started.";
    a.informativeText = @(std::strerror(errno));
    [a runModal];
    std::exit(1);
  }
}
