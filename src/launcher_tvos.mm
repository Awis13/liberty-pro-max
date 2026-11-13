// The Apple TV app's launcher: a settings screen with the port's
// options (the Mac launcher's, launcher_options.h, plus the device's
// resolution and frame limit) and a Play button. Shown on the main thread before the game
// starts (main.cpp, in SDL's main); the run loop turns until Play, then the
// options go into the environment and the game starts in this process.

#import <UIKit/UIKit.h>

#include <TargetConditionals.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "launcher_mobile_options.h"
#include "runtime/gfx/prewarm.h"

namespace {

// The game's resolution (its -width/-height) and frame limit.
struct Resolution {
  int w, h;
  NSString* title;
};
const std::array<Resolution, 4> kIpadResolutions = {{
    {1536, 1152, @"1536×1152"},
    {1792, 1344, @"1792×1344"},
    {2048, 1536, @"2048×1536"},
    {2732, 2048, @"2732×2048"},
}};

bool IsPhone() { return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone; }

// The resolutions the launcher offers. On an iPhone they are also the
// display modes the game is given besides the usual ones (IosPhoneDisplay).
const std::array<Resolution, 4>& Resolutions() {
  static const auto tv = [] {
    std::array<Resolution,4> result;
    for (size_t i = 0; i < result.size(); ++i) {
      const auto r = kTvResolutions[i];
      result[i] = {r.w,r.h,[NSString stringWithFormat:@"%d×%d",r.w,r.h]};
    }
    return result;
  }();
  return tv;
}
bool g_play = false;

NSString* GraphicsHint() {
  return @"Lower resolutions keep 60 fps steadier in busy streets. SMAA smooths edges cheaply; TAA also "
         @"steadies wires, fences and leaves.";
}

}  // namespace

@protocol LpmShaderActions
- (void)compileShaders:(id)sender;
- (void)updateShaderControls;
@end

static void UpdateTvActionButton(UIButton* button) {
  UIButtonConfiguration* style = button.configuration;
  style.baseBackgroundColor = button.isFocused ? UIColor.whiteColor : [UIColor colorWithWhite:0.16 alpha:1];
  style.baseForegroundColor = !button.enabled ? [UIColor colorWithWhite:0.45 alpha:1]
      : button.isFocused ? UIColor.blackColor : UIColor.whiteColor;
  button.configuration = style;
}

@interface LpmLauncher : UIViewController <UITableViewDataSource, UITableViewDelegate>
@property(strong) UITableView* table;
@property(strong) UIButton* playButton;
@property(strong) NSIndexPath* lastFocusedRow;
@property NSInteger displayedSection;
@property(copy) NSString* shaderNote;
@property BOOL shaderCompileEnabled;
- (UITableViewCell*)tvCell:(NSIndexPath*)ip;


@end

@implementation LpmLauncher {
  NSUserDefaults* _d;
}

// (kShaders is not shown: FixShown.)
typedef NS_ENUM(NSInteger, LpmSection) { kGame, kExtrasSection, kGraphics, kFixesSection, kSectionCount, kShaders };

- (instancetype)init {
  if ((self = [super init])) _d = NSUserDefaults.standardUserDefaults;
  return self;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
  self.view.backgroundColor = UIColor.blackColor;
  // Tabs contain settings only. The tab controller owns the shared
  // logo, title and Play button outside its transitioning child views.
  self.table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
  self.table.rowHeight = 96;
  self.table.remembersLastFocusedIndexPath = YES;
  self.table.dataSource = self;
  self.table.delegate = self;
  self.table.backgroundColor = UIColor.blackColor;
  self.table.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:self.table];
  UILayoutGuide* g = self.view.safeAreaLayoutGuide;
  UILayoutGuide* brandSpace = [UILayoutGuide new];
  [self.view addLayoutGuide:brandSpace];
  NSLayoutConstraint* menuBottom = [self.table.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-24];
  if (self.displayedSection == kGraphics) {
    // A non-focusable table footer can be clipped below the final row on
    // tvOS. Keep the complete, wrapping hint outside the scrolling list.
    UILabel* hint = [UILabel new];
    hint.text = GraphicsHint();
    hint.font = [UIFont systemFontOfSize:24];
    hint.textColor = [UIColor colorWithWhite:0.85 alpha:1];
    hint.numberOfLines = 0;
    hint.lineBreakMode = NSLineBreakByWordWrapping;
    hint.translatesAutoresizingMaskIntoConstraints = NO;
    [hint setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisVertical];
    [self.view addSubview:hint];
    [NSLayoutConstraint activateConstraints:@[
      [hint.leadingAnchor constraintEqualToAnchor:self.table.leadingAnchor constant:24],
      [hint.trailingAnchor constraintEqualToAnchor:self.table.trailingAnchor constant:-24],
      [hint.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-24],
    ]];
    menuBottom = [self.table.bottomAnchor constraintEqualToAnchor:hint.topAnchor constant:-16];
  }
  [NSLayoutConstraint activateConstraints:@[
    [brandSpace.leadingAnchor constraintEqualToAnchor:g.leadingAnchor],
    [brandSpace.widthAnchor constraintEqualToAnchor:g.widthAnchor multiplier:0.27],
    [self.table.leadingAnchor constraintEqualToAnchor:brandSpace.trailingAnchor constant:81],
    [self.table.trailingAnchor constraintEqualToAnchor:g.trailingAnchor],
    [self.table.topAnchor constraintEqualToAnchor:g.topAnchor constant:40],
    menuBottom,
  ]];
  if (self.displayedSection == kGame)
    [(id<LpmShaderActions>)self.tabBarController updateShaderControls];
}

- (void)play:(id)sender {
  NSString* root = [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject
      stringByAppendingPathComponent:@"game"];
  if (MobileTransferInProgress(root)) {
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Installation in progress"
        message:@"The game files are still being copied. Please wait for the transfer to finish before playing."
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
    return;
  }
  [_d synchronize];
  g_play = true;
}

// ---- rows

- (NSInteger)numberOfSectionsInTableView:(UITableView*)t {
  return 1;
}

- (NSString*)tableView:(UITableView*)t titleForHeaderInSection:(NSInteger)s {
  // The native tab bar already names the visible section.
  return nil;
  switch (s) {
    case kGame: return @"Game";
    case kExtrasSection: return @"Extras";
    case kGraphics: return @"Graphics";
    case kFixesSection: return @"Fixes";
    default: return @"FusionFix shaders";
  }
}

- (NSString*)tableView:(UITableView*)t titleForFooterInSection:(NSInteger)s {
  return nil;  // The TV's graphics hint is pinned below the table.
}

// Game: (title, key) rows; switches except for the language menu.
static NSArray* GameRows() { return MobileGameRows(TARGET_OS_TV); }

// Graphics: segmented choices (title, key, items).
static NSArray* GraphicsRows() {
  NSMutableArray* res = [NSMutableArray array];
  for (const Resolution& r : Resolutions()) [res addObject:r.title];
  return MobileGraphicsRows(TARGET_OS_TV,res);
}

- (NSInteger)tableView:(UITableView*)t numberOfRowsInSection:(NSInteger)s {
  s = self.displayedSection;
  switch (s) {
    case kGame: return GameRows().count + (TARGET_OS_TV ? 1 : 0);
    case kExtrasSection: return ExtraCount();
    case kGraphics: return GraphicsRows().count;
    case kFixesSection: return MobileFixCount(TARGET_OS_TV);
    default: return std::size(kShaderChoices);
  }
}

// The anti-aliasing control's segment for the stored value (0 off, 1 SMAA,
// 3 TAA on the Mac's scale) and back.
static NSInteger AaSegment(NSInteger v) { return v == 1 ? 1 : v == 3 ? 2 : 0; }
static NSInteger AaValue(NSInteger seg) { return seg == 1 ? 1 : seg == 2 ? 3 : 0; }

- (UITableViewCell*)tableView:(UITableView*)t cellForRowAtIndexPath:(NSIndexPath*)ip {
  return [self tvCell:ip];
}


// tvOS focuses whole rows. Touch-only switches and popup buttons stay in
// the iOS branch above; Select toggles a boolean or opens its choices.
- (BOOL)isShaderRow:(NSIndexPath*)ip {
  return self.displayedSection == kGame && ip.row == NSInteger(GameRows().count);
}
- (NSArray*)tvRow:(NSIndexPath*)ip {
  if ([self isShaderRow:ip]) return @[@"Compile Shaders",@"CompileShaders",@[]];
  if (self.displayedSection == kGame) {
    NSArray* row = GameRows()[ip.row];
    return @[row[0],row[1],@[]];
  }
  if (self.displayedSection == kGraphics) return GraphicsRows()[ip.row];
  const Fix& f = self.displayedSection == kExtrasSection ? ExtraAt(ip.row) : MobileFix(ip.row,true);
  return @[f.title,f.key,@[]];
}
- (NSInteger)tvValue:(NSString*)key {
  if ([key isEqual:kAntiAliasingKey]) return AaSegment([_d integerForKey:key]);
  if ([key isEqual:kHudKey]) return [_d boolForKey:key] ? 1 : 0;
  return [_d integerForKey:key];
}
- (void)updateTvCellColors:(UITableViewCell*)cell focused:(BOOL)focused {
  UIColor* color = focused ? UIColor.blackColor : UIColor.whiteColor;
  const BOOL disabledAction = [cell.accessibilityIdentifier isEqual:@"CompileShaders"] && !self.shaderCompileEnabled;
  cell.textLabel.textColor = disabledAction && !focused ? [UIColor colorWithWhite:0.45 alpha:1] : color;
  cell.detailTextLabel.textColor = color;
  cell.tintColor = color;
}
- (void)configureTvCell:(UITableViewCell*)cell at:(NSIndexPath*)ip {
  NSArray* row = [self tvRow:ip];
  NSString* key = row[1];
  NSArray* items = row[2];
  cell.textLabel.text = row[0];
  cell.textLabel.font = [UIFont systemFontOfSize:30 weight:UIFontWeightMedium];
  cell.textLabel.numberOfLines = 2;
  const BOOL shaderAction = [self isShaderRow:ip];
  cell.detailTextLabel.font = [UIFont systemFontOfSize:shaderAction ? 24 : 29 weight:UIFontWeightBold];
  cell.detailTextLabel.numberOfLines = shaderAction ? 2 : 1;
  NSString* value;
  if (shaderAction) {
    value = self.shaderNote ?: @"Checking shader cache…";
    cell.accessoryType = self.shaderCompileEnabled ? UITableViewCellAccessoryDisclosureIndicator : UITableViewCellAccessoryNone;
    cell.accessibilityTraits = UIAccessibilityTraitButton | (self.shaderCompileEnabled ? 0 : UIAccessibilityTraitNotEnabled);
  } else if (items.count) {
    value = items[std::clamp<NSInteger>([self tvValue:key],0,NSInteger(items.count)-1)];
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
  } else {
    const BOOL on = [_d boolForKey:key];
    value = on ? @"On" : @"Off";
    cell.accessoryType = UITableViewCellAccessoryNone;
  }
  if ([key isEqual:kTaaSharpeningKey] && !SharpeningApplies([_d integerForKey:kAntiAliasingKey]))
    value = @"Available with TAA";
  cell.detailTextLabel.text = value;
  cell.accessibilityIdentifier = key;
  cell.accessibilityLabel = row[0];
  cell.accessibilityValue = value;
  [self updateTvCellColors:cell focused:cell.isFocused];
}
- (UITableViewCell*)tvCell:(NSIndexPath*)ip {
  const BOOL shaderAction = [self isShaderRow:ip];
  NSString* reuse = shaderAction ? @"shaderAction" : @"setting";
  UITableViewCell* cell = [self.table dequeueReusableCellWithIdentifier:reuse];
  if (!cell) cell = [[UITableViewCell alloc] initWithStyle:shaderAction ? UITableViewCellStyleSubtitle : UITableViewCellStyleValue1 reuseIdentifier:reuse];
  [self configureTvCell:cell at:ip];
  return cell;
}
- (CGFloat)tableView:(UITableView*)table heightForRowAtIndexPath:(NSIndexPath*)ip {
  return [self isShaderRow:ip] ? 132 : 96;
}
- (NSArray<id<UIFocusEnvironment>>*)preferredFocusEnvironments {
  UITableViewCell* row = self.lastFocusedRow ? [self.table cellForRowAtIndexPath:self.lastFocusedRow] : nil;
  return row ? @[row] : self.table ? @[self.table] : [super preferredFocusEnvironments];
}
- (void)tableView:(UITableView*)table didUpdateFocusInContext:(UITableViewFocusUpdateContext*)context
 withAnimationCoordinator:(UIFocusAnimationCoordinator*)coordinator {
  if (context.nextFocusedIndexPath) self.lastFocusedRow = context.nextFocusedIndexPath;
  UITableViewCell* previous = context.previouslyFocusedIndexPath
      ? [table cellForRowAtIndexPath:context.previouslyFocusedIndexPath] : nil;
  UITableViewCell* next = context.nextFocusedIndexPath
      ? [table cellForRowAtIndexPath:context.nextFocusedIndexPath] : nil;
  [coordinator addCoordinatedAnimations:^{
    [self updateTvCellColors:previous focused:NO];
    [self updateTvCellColors:next focused:YES];
  } completion:nil];
}
- (void)refreshTvRows {
  for (NSIndexPath* ip in self.table.indexPathsForVisibleRows)
    [self configureTvCell:[self.table cellForRowAtIndexPath:ip] at:ip];
}
- (void)tableView:(UITableView*)table didSelectRowAtIndexPath:(NSIndexPath*)ip {
  // Activation changes the setting, not the row's persistent selection.
  // Leave UIKit's focus highlight in charge as the remote moves on.
  [table deselectRowAtIndexPath:ip animated:NO];
  self.lastFocusedRow = ip;
  if ([self isShaderRow:ip]) {
    // Keep the read-only row focusable so its status can be scrolled into
    // view, but activation is disabled when no compilation is needed.
    if (self.shaderCompileEnabled)
      [(id<LpmShaderActions>)self.tabBarController compileShaders:nil];
    return;
  }
  NSArray* row = [self tvRow:ip];
  NSString* key = row[1];
  NSArray* items = row[2];
  if ([key isEqual:kTaaSharpeningKey] && !SharpeningApplies([_d integerForKey:kAntiAliasingKey])) {
    UIAlertController* info = [UIAlertController alertControllerWithTitle:row[0]
        message:@"Choose TAA in Anti-aliasing to adjust sharpening." preferredStyle:UIAlertControllerStyleAlert];
    [info addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:info animated:YES completion:nil];
    return;
  }
  if (!items.count) {
    [_d setBool:![_d boolForKey:key] forKey:key];
    [self refreshTvRows];
    return;
  }
  UIAlertController* choices = [UIAlertController alertControllerWithTitle:row[0] message:nil
                                                           preferredStyle:UIAlertControllerStyleAlert];
  const NSInteger selected = std::clamp<NSInteger>([self tvValue:key],0,NSInteger(items.count)-1);
  __weak LpmLauncher* weakSelf = self;
  for (NSInteger i = 0; i < NSInteger(items.count); i++) {
    NSString* title = i == selected ? [@"✓  " stringByAppendingString:items[i]] : items[i];
    [choices addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
      LpmLauncher* controller = weakSelf;
      if (!controller) return;
      if ([key isEqual:kHudKey]) [controller->_d setBool:i == 1 forKey:key];
      else [controller->_d setInteger:[key isEqual:kAntiAliasingKey] ? AaValue(i) : i forKey:key];
      [controller refreshTvRows];
    }]];
  }
  [choices addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
  [self presentViewController:choices animated:YES completion:nil];
}

@end



struct TvShaderTask {
  std::atomic<bool> cancel{false}, queued{false};
  std::mutex statusMutex;
  recomp::gfx::PrewarmStatus status;
};

@interface LpmShaderProgress : UIViewController
@property(strong) UILabel* status;
@property(strong) UIProgressView* progress;
@property(strong) UIButton* cancelButton;
@property(copy) void (^cancelHandler)(void);
@end

@implementation LpmShaderProgress
- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.blackColor;
  UILabel* title = [UILabel new];
  title.text = @"Compile Shaders";
  title.textColor = UIColor.whiteColor;
  title.font = [UIFont systemFontOfSize:40 weight:UIFontWeightSemibold];
  self.status = [UILabel new];
  self.status.text = @"Reading shader cache…";
  self.status.font = [UIFont systemFontOfSize:26];
  self.status.textColor = UIColor.whiteColor;
  self.status.numberOfLines = 0;
  self.status.textAlignment = NSTextAlignmentCenter;
  self.progress = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
  self.progress.progressTintColor = UIColor.whiteColor;
  self.progress.accessibilityLabel = @"Shader compilation progress";
  self.cancelButton = [UIButton buttonWithType:UIButtonTypeSystem];
  UIButtonConfiguration* cancelConfiguration = [UIButtonConfiguration filledButtonConfiguration];
  cancelConfiguration.baseBackgroundColor = UIColor.whiteColor;
  cancelConfiguration.baseForegroundColor = UIColor.blackColor;
  self.cancelButton.configuration = cancelConfiguration;
  [self.cancelButton setTitle:@"Cancel" forState:UIControlStateNormal];
  [self.cancelButton addTarget:self action:@selector(cancel:) forControlEvents:UIControlEventPrimaryActionTriggered];
  for (UIView* view in @[title,self.status,self.progress,self.cancelButton]) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:view];
    [view.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor].active = YES;
  }
  [NSLayoutConstraint activateConstraints:@[
    [title.bottomAnchor constraintEqualToAnchor:self.view.centerYAnchor constant:-100],
    [self.status.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:24],
    [self.status.widthAnchor constraintEqualToConstant:800],
    [self.progress.topAnchor constraintEqualToAnchor:self.status.bottomAnchor constant:32],
    [self.progress.widthAnchor constraintEqualToConstant:700],
    [self.cancelButton.topAnchor constraintEqualToAnchor:self.progress.bottomAnchor constant:56],
    [self.cancelButton.widthAnchor constraintEqualToConstant:300],
    [self.cancelButton.heightAnchor constraintEqualToConstant:80],
  ]];
}
- (NSArray<id<UIFocusEnvironment>>*)preferredFocusEnvironments { return @[self.cancelButton]; }
- (void)cancel:(id)sender {
  if (self.cancelHandler) self.cancelHandler();
  self.status.text = @"Finishing shader preparation…";
}
- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
  for (UIPress* press in presses) if (press.type == UIPressTypeMenu) { [self cancel:nil]; return; }
  [super pressesBegan:presses withEvent:event];
}
@end

@interface LpmTvLauncher : UITabBarController <LpmShaderActions>
@property(strong) UIButton* playButton;
@property(strong) UIFocusGuide* launchGuide;
@property(strong) NSLayoutConstraint* panelTop;
@property(strong) NSLayoutConstraint* panelBottom;
@property(copy) NSArray<UIView*>* launchPanelViews;
@property recomp::gfx::PrewarmCacheState shaderCacheState;
@property BOOL checkingShaderCache;
@property(copy) NSString* shaderMessage;
@property(strong) UIFocusGuide* tabAccessGuide;
@property(strong) NSLayoutConstraint* tabGuideRight;
@property(strong) NSLayoutConstraint* tabGuideTop;
@property(strong) NSLayoutConstraint* tabGuideBottom;
@property BOOL receivedFocus;
@end

@implementation LpmTvLauncher {
  std::shared_ptr<TvShaderTask> _shaderTask;
}
- (void)viewDidLoad {
  [super viewDidLoad];
  self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
  self.view.backgroundColor = UIColor.blackColor;
  UIImageView* logo = [[UIImageView alloc] initWithImage:[UIImage imageWithContentsOfFile:
      [NSBundle.mainBundle pathForResource:@"LauncherLogo" ofType:@"png"]]];
  logo.contentMode = UIViewContentModeScaleAspectFit;
  UILabel* title = [UILabel new];
  title.text = @"Liberty Pro Max";
  title.font = [UIFont systemFontOfSize:40 weight:UIFontWeightSemibold];
  title.textColor = UIColor.whiteColor;
  title.numberOfLines = 2;
  UIButton* play = [UIButton buttonWithType:UIButtonTypeSystem];
  UIButtonConfiguration* configuration = [UIButtonConfiguration filledButtonConfiguration];
  configuration.title = @"Play";
  configuration.baseBackgroundColor = UIColor.whiteColor;
  configuration.baseForegroundColor = UIColor.blackColor;
  configuration.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  configuration.titleTextAttributesTransformer = ^NSDictionary*(NSDictionary* attributes) {
    NSMutableDictionary* result = attributes.mutableCopy;
    result[NSFontAttributeName] = [UIFont systemFontOfSize:40 weight:UIFontWeightSemibold];
    return result;
  };
  play.configuration = configuration;
  play.configurationUpdateHandler = ^(UIButton* button) { UpdateTvActionButton(button); };
  [play setNeedsUpdateConfiguration];
  play.accessibilityIdentifier = @"Play";
  [play addTarget:self action:@selector(play:) forControlEvents:UIControlEventPrimaryActionTriggered];
  self.playButton = play;
  UIView* divider = [UIView new];
  divider.backgroundColor = [UIColor colorWithWhite:0.22 alpha:1];
  self.launchPanelViews = @[logo,title,play,divider];
  for (UIView* view in self.launchPanelViews) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:view];
  }
  UILayoutGuide* g = self.view.safeAreaLayoutGuide;
  UILayoutGuide* brand = [UILayoutGuide new];
  [self.view addLayoutGuide:brand];
  self.panelTop = [brand.topAnchor constraintEqualToAnchor:self.view.topAnchor];
  self.panelBottom = [brand.bottomAnchor constraintEqualToAnchor:self.view.topAnchor];
  [NSLayoutConstraint activateConstraints:@[
    [brand.leadingAnchor constraintEqualToAnchor:g.leadingAnchor],
    [brand.widthAnchor constraintEqualToAnchor:g.widthAnchor multiplier:0.27],
    self.panelTop, self.panelBottom,
    [logo.topAnchor constraintEqualToAnchor:brand.topAnchor],
    [logo.centerXAnchor constraintEqualToAnchor:brand.centerXAnchor],
    [logo.widthAnchor constraintEqualToAnchor:brand.widthAnchor multiplier:0.88],
    [logo.heightAnchor constraintEqualToAnchor:logo.widthAnchor],
    [title.topAnchor constraintEqualToAnchor:logo.bottomAnchor constant:12],
    [title.leadingAnchor constraintEqualToAnchor:brand.leadingAnchor],
    [title.trailingAnchor constraintEqualToAnchor:brand.trailingAnchor],
    [play.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:48],
    [play.leadingAnchor constraintEqualToAnchor:brand.leadingAnchor],
    [play.trailingAnchor constraintEqualToAnchor:brand.trailingAnchor],
    [play.heightAnchor constraintEqualToConstant:100],
    [divider.leadingAnchor constraintEqualToAnchor:brand.trailingAnchor constant:48],
    [divider.widthAnchor constraintEqualToConstant:1],
    [divider.topAnchor constraintEqualToAnchor:brand.topAnchor],
    [divider.bottomAnchor constraintEqualToAnchor:brand.bottomAnchor],
  ]];
  // Left from the first tab reaches Play without entering a menu. Up
  // from Play returns to the tab bar; other tabs retain native navigation.
  self.tabAccessGuide = [UIFocusGuide new];
  self.tabAccessGuide.enabled = NO;
  [self.view addLayoutGuide:self.tabAccessGuide];
  self.tabGuideRight = [self.tabAccessGuide.trailingAnchor constraintEqualToAnchor:self.view.leadingAnchor];
  self.tabGuideTop = [self.tabAccessGuide.topAnchor constraintEqualToAnchor:self.view.topAnchor];
  self.tabGuideBottom = [self.tabAccessGuide.bottomAnchor constraintEqualToAnchor:self.view.topAnchor];
  [NSLayoutConstraint activateConstraints:@[
    [self.tabAccessGuide.leadingAnchor constraintEqualToAnchor:g.leadingAnchor],
    self.tabGuideRight,self.tabGuideTop,self.tabGuideBottom,
  ]];
  self.launchGuide = [UIFocusGuide new];
  self.launchGuide.enabled = NO;
  self.launchGuide.preferredFocusEnvironments = @[play];
  [self.view addLayoutGuide:self.launchGuide];
  [NSLayoutConstraint activateConstraints:@[
    [self.launchGuide.leadingAnchor constraintEqualToAnchor:brand.trailingAnchor],
    [self.launchGuide.trailingAnchor constraintEqualToAnchor:brand.trailingAnchor constant:81],
    [self.launchGuide.topAnchor constraintEqualToAnchor:brand.topAnchor],
    [self.launchGuide.bottomAnchor constraintEqualToAnchor:brand.bottomAnchor],
  ]];
  [self refreshShaderCache];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  // Native tab bars adjust their children's safe areas. Use the selected
  // page's content bounds so the shared panel stays aligned with every
  // menu without putting it inside a tab's transition container.
  UIView* page = self.selectedViewController.view;
  if (!page.window || CGRectIsEmpty(page.bounds)) return;
  [page layoutIfNeeded];
  CGRect content = [page convertRect:page.safeAreaLayoutGuide.layoutFrame toView:self.view];
  self.panelTop.constant = CGRectGetMinY(content) + 40;
  self.panelBottom.constant = CGRectGetMaxY(content) - 24;
  for (UIView* view in self.launchPanelViews) [self.view bringSubviewToFront:view];
  CGRect bar = [self.tabBar convertRect:self.tabBar.bounds toView:self.view];
  self.tabGuideTop.constant = CGRectGetMinY(bar);
  self.tabGuideBottom.constant = CGRectGetMaxY(bar);
  if (self.tabGuideRight.constant <= CGRectGetMinX(content))
    self.tabGuideRight.constant = CGRectGetMinX(content) + content.size.width * 0.27;
}
- (NSArray<id<UIFocusEnvironment>>*)preferredFocusEnvironments {
  return !self.receivedFocus && self.playButton ? @[self.playButton] : [super preferredFocusEnvironments];
}
- (void)didUpdateFocusInContext:(UIFocusUpdateContext*)context
      withAnimationCoordinator:(UIFocusAnimationCoordinator*)coordinator {
  [super didUpdateFocusInContext:context withAnimationCoordinator:coordinator];
  if (context.nextFocusedView) self.receivedFocus = YES;
  LpmLauncher* page = (LpmLauncher*)self.selectedViewController;
  self.launchGuide.enabled = [context.nextFocusedView isDescendantOfView:page.view];
  const BOOL firstTab = self.selectedIndex == 0 && [context.nextFocusedView isDescendantOfView:self.tabBar];
  const BOOL play = context.nextFocusedView == self.playButton;
  self.tabAccessGuide.enabled = firstTab || play;
  self.tabAccessGuide.preferredFocusEnvironments = play ? @[self.tabBar] : @[self.playButton];
  if (firstTab) {
    CGRect item = [context.nextFocusedView convertRect:context.nextFocusedView.bounds toView:self.view];
    self.tabGuideRight.constant = std::max(CGRectGetMinX(self.view.safeAreaLayoutGuide.layoutFrame) + 1,
                                         CGRectGetMinX(item) - 1);
  }
}
- (void)play:(id)sender {
  if (_shaderTask) return;
  [(LpmLauncher*)self.selectedViewController play:sender];
}
- (void)updateShaderControls {
  using State = recomp::gfx::PrewarmCacheState;
  for (LpmLauncher* page in self.viewControllers) {
    if (page.displayedSection != kGame || !page.isViewLoaded) continue;
    NSString* note = @"Shader compilation unavailable.";
    BOOL enabled = NO;
    if (self.checkingShaderCache) note = @"Checking shader cache…";
    else if (_shaderTask) note = @"Compiling shaders…";
    else if (self.shaderCacheState == State::Ready) note = @"All cached shaders are compiled.";
    else if (self.shaderCacheState == State::Empty) note = @"Play the game to record shaders first.";
    else if (self.shaderCacheState == State::Needed) {
      note = self.shaderMessage ?: @"Some cached shaders need compilation.";
      enabled = YES;
    }
    page.shaderNote = note;
    page.shaderCompileEnabled = enabled;
    [page refreshTvRows];
  }
}
- (void)refreshShaderCache {
  self.checkingShaderCache = YES;
  [self updateShaderControls];
  __weak LpmTvLauncher* weakSelf = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
    recomp::gfx::PrewarmCacheState state;
    @autoreleasepool { state = recomp::gfx::InspectPrewarmCache(); }
    using State = recomp::gfx::PrewarmCacheState;
    std::fprintf(stderr,"launcher: shader cache: %s\n",state == State::Ready ? "ready" :
        state == State::Needed ? "needs compilation" : state == State::Empty ? "empty" : "unavailable");
    dispatch_async(dispatch_get_main_queue(), ^{
      LpmTvLauncher* launcher = weakSelf;
      if (!launcher) return;
      launcher.shaderCacheState = state;
      launcher.checkingShaderCache = NO;
      [launcher updateShaderControls];
    });
  });
}
- (void)compileShaders:(id)sender {
  if (_shaderTask || self.checkingShaderCache || self.shaderCacheState != recomp::gfx::PrewarmCacheState::Needed) return;
  auto task = std::make_shared<TvShaderTask>();
  _shaderTask = task;
  [self updateShaderControls];
  LpmShaderProgress* dialog = [LpmShaderProgress new];
  dialog.modalPresentationStyle = UIModalPresentationFullScreen;
  dialog.modalInPresentation = YES;
  dialog.cancelHandler = ^{ task->cancel = true; };
  __weak LpmTvLauncher* weakSelf = self;
  __weak LpmShaderProgress* weakDialog = dialog;
  [self presentViewController:dialog animated:YES completion:^{
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
      recomp::gfx::PrewarmResult result;
      recomp::gfx::PrewarmCacheState cacheState;
      @autoreleasepool {
        recomp::gfx::PrewarmArchiveLimits limits;
        limits.workers = 1;
        limits.max_unsaved = 64;
        limits.drain_on_close = true;
        result = recomp::gfx::PrewarmCaches(nullptr, task->cancel, {}, limits,
            [task,weakDialog](const recomp::gfx::PrewarmStatus& status) {
          { std::lock_guard<std::mutex> lock(task->statusMutex); task->status = status; }
          if (task->queued.exchange(true)) return;
          dispatch_async(dispatch_get_main_queue(), ^{
            task->queued = false;
            if (task->cancel.load()) return;
            LpmShaderProgress* progress = weakDialog;
            recomp::gfx::PrewarmStatus status;
            { std::lock_guard<std::mutex> lock(task->statusMutex); status = task->status; }
            using Phase = recomp::gfx::PrewarmStatus::Phase;
            NSString* detail = status.phase == Phase::Shaders
                ? [NSString stringWithFormat:@"Preparing shader functions: %zu / %zu",
                    status.functions_done,status.functions_total]
                : status.phase == Phase::Saving ? @"Saving compiled pipelines…"
                : status.phase == Phase::Complete ? @"Checking saved pipelines…"
                : @"Compiling remaining pipelines…";
            progress.status.text = [NSString stringWithFormat:@"Pipelines prepared: %zu / %zu\n%@",
                status.pipelines_ready,status.pipelines_total,detail];
            progress.progress.progress = status.pipelines_total
                ? float(status.pipelines_ready) / float(status.pipelines_total) : 0;
          });
        });
        cacheState = recomp::gfx::InspectPrewarmCache();
      }
      // The worker and archive writer have finished before the launcher
      // becomes usable again. Gameplay never overlaps compilation.
      dispatch_async(dispatch_get_main_queue(), ^{
        LpmTvLauncher* launcher = weakSelf;
        if (!launcher) return;
        launcher.shaderCacheState = cacheState;
        launcher.shaderMessage = result == recomp::gfx::PrewarmResult::Cancelled
            ? @"Cancelled. Some cached shaders still need compilation."
            : result == recomp::gfx::PrewarmResult::Busy ? @"Shader cache is in use. Try again later."
            : result == recomp::gfx::PrewarmResult::Incomplete
                ? @"Some pipelines could not be compiled or saved. Try again; see log for details." : nil;
        [launcher dismissViewControllerAnimated:YES completion:^{
          launcher->_shaderTask.reset();
          [launcher updateShaderControls];
          [launcher setNeedsFocusUpdate];
        }];
      });
    });
  }];
}
@end

// main.cpp: whether this is an iPhone, and then its resolutions as a
// display mode list ("WxH,WxH,...").
bool IosPhoneDisplay(std::string* modes) {
  if (!IsPhone()) return false;
  modes->clear();
  for (const Resolution& r : Resolutions()) {
    if (!modes->empty()) *modes += ',';
    *modes += std::to_string(r.w) + 'x' + std::to_string(r.h);
  }
  return true;
}

// main.cpp (iPad, iPhone, Apple TV): the launcher until Play, then the
// options into the environment; the game's resolution goes on its command
// line (launcher_args: after the defaults and the player's commandline.txt,
// so the resolution chosen here wins; game_args.h).
void RunIosLauncher(std::vector<std::string>* launcher_args) {
  @autoreleasepool {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    RegisterMobileDefaults(d,TARGET_OS_TV);
    // Metal reads HUD support at its first device creation, including
    // the launcher's read-only cache inspection. Load that support now;
    // the game's layer explicitly enables or disables the overlay from
    // the final saved choice after Play (RECOMP_METAL_HUD).
    setenv("MTL_HUD_ENABLED", "1", 1);

    // The app's window scene (SDL's UIKit startup may connect it a moment
    // after calling main); a window outside a scene is never shown.
    // GTAIV_SKIP_LAUNCHER=1 (test runs started over the cable): straight
    // to the game with the saved options.
    const char* skip = std::getenv("GTAIV_SKIP_LAUNCHER");
    if (skip && *skip == '1') g_play = true;
    UIWindowScene* scene = nil;
    for (int i = 0; i < 200 && !scene && !g_play; i++) {
      for (UIScene* s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class]) scene = (UIWindowScene*)s;
      if (!scene)
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
    UIWindow* window = scene ? [[UIWindow alloc] initWithWindowScene:scene]
                             : [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    LpmTvLauncher* tabs = [LpmTvLauncher new];
    tabs.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    NSMutableArray<UIViewController*>* pages = [NSMutableArray array];
    NSArray<NSString*>* names = @[@"Game",@"Extras",@"Graphics",@"Fixes"];
    NSArray<NSString*>* symbols = @[@"gamecontroller",@"square.stack",@"display",@"wrench.and.screwdriver"];
    for (NSInteger section : {kGame,kGraphics,kFixesSection,kExtrasSection}) {
      LpmLauncher* page = [LpmLauncher new];
      page.displayedSection = section;
      page.tabBarItem = [[UITabBarItem alloc] initWithTitle:names[section]
          image:[UIImage systemImageNamed:symbols[section]] tag:section];
      [pages addObject:page];
    }
    tabs.viewControllers = pages;
    window.rootViewController = tabs;
    window.windowLevel = UIWindowLevelNormal + 1;
    [window makeKeyAndVisible];
    while (!g_play) {
      @autoreleasepool {
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
      }
    }
    window.hidden = YES;
    window.rootViewController = nil;

    ApplyMobileOptions(d,TARGET_OS_TV,int(UIScreen.mainScreen.maximumFramesPerSecond));
    std::fprintf(stderr, "launcher: applied continue=%d skip-intro=%d fast-loading=%d hud=%d episodes=%d\n",
        [d boolForKey:kContinueKey], [d boolForKey:kSkipIntroKey], [d boolForKey:kFastLoadingKey],
        [d boolForKey:kHudKey], [d boolForKey:@"Episodes"]);
    const auto& resolutions = Resolutions();
    const Resolution& r = resolutions[std::clamp<NSInteger>([d integerForKey:MobileResolutionKey(TARGET_OS_TV)], 0, NSInteger(resolutions.size()) - 1)];
    *launcher_args = {"-width", std::to_string(r.w), "-height", std::to_string(r.h)};
  }
}
