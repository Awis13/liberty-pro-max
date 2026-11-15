// The iPad and iPhone app's launcher: a settings screen with the port's
// options (the Mac launcher's, launcher_options.h, plus the device's
// resolution and frame limit) and a Play button. Shown on the main thread before the game
// starts (main.cpp, in SDL's main); the run loop turns until Play, then the
// options go into the environment and the game starts in this process.

#import <UIKit/UIKit.h>

#include <TargetConditionals.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "achievements.h"
#include "launcher_options.h"

namespace {

// The game's resolution (its -width/-height) and frame limit.
NSString* const kIosResolutionKey = @"IosResolution";  // index into Resolutions()
// 0: 60 (the display's own pace where it shows no more), 1: 40, 2: 30,
// 3: off (no limiter; the display still paces).
NSString* const kIosFrameLimitKey = @"IosFrameLimit";
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
  if (!IsPhone()) return kIpadResolutions;
  // Use the phone's landscape aspect at every quality level. The renderer
  // preserves the back buffer's aspect, so the iPad's 4:3 presets leave bars.
  static const auto phone = [] {
    const CGSize size = UIScreen.mainScreen.nativeBounds.size;
    const double width = std::max(size.width, size.height);
    const double height = std::min(size.width, size.height);
    const double scales[] = {0.5, 2.0 / 3.0, 5.0 / 6.0, 1.0};
    std::array<Resolution, 4> result;
    for (size_t i = 0; i < result.size(); ++i) {
      const int w = std::max(2, int(width * scales[i]) / 2 * 2);
      const int h = std::max(2, int(height * scales[i]) / 2 * 2);
      result[i] = {w, h, [NSString stringWithFormat:@"%d×%d", w, h]};
    }
    return result;
  }();
  return phone;
}
// GTAIV_FPS_LIMIT for a stored frame limit (0: none). The first choice is
// 60: no limiter on a 60 Hz screen, which paces the frames itself (as
// before there were faster screens here), a limiter of 60 on a faster one.
int FrameLimit(NSInteger choice) {
  switch (choice) {
    case 1: return 40;
    case 2: return 30;
    case 3: return 0;
    default: return UIScreen.mainScreen.maximumFramesPerSecond > 60 ? 60 : 0;
  }
}

bool g_play = false;

}  // namespace

#if !TARGET_OS_TV
@interface LpmAchievements : UIViewController <UITableViewDataSource, UITableViewDelegate>
@end

@interface LpmLauncher : UIViewController <UITableViewDataSource, UITableViewDelegate>
@property(strong) UITableView* table;
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

  UIImageView* logo = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"LauncherLogo.png"]];
  logo.contentMode = UIViewContentModeScaleAspectFit;
  logo.translatesAutoresizingMaskIntoConstraints = NO;
  UILabel* title = [UILabel new];
  title.text = @"Liberty Pro Max";
  title.font = [UIFont systemFontOfSize:34 weight:UIFontWeightBold];
  title.textColor = UIColor.whiteColor;
  title.translatesAutoresizingMaskIntoConstraints = NO;

  UIButton* play = [UIButton buttonWithType:UIButtonTypeSystem];
  UIButtonConfiguration* pc = [UIButtonConfiguration filledButtonConfiguration];
  pc.title = @"Play";
  pc.baseBackgroundColor = UIColor.whiteColor;
  pc.baseForegroundColor = UIColor.blackColor;
  pc.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  pc.titleTextAttributesTransformer = ^NSDictionary*(NSDictionary* a) {
    NSMutableDictionary* m = a.mutableCopy;
    m[NSFontAttributeName] = [UIFont systemFontOfSize:24 weight:UIFontWeightSemibold];
    return m;
  };
  play.configuration = pc;
  play.translatesAutoresizingMaskIntoConstraints = NO;
  [play addTarget:self action:@selector(play:) forControlEvents:UIControlEventTouchUpInside];

  self.table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
  self.table.dataSource = self;
  self.table.delegate = self;
  self.table.backgroundColor = UIColor.blackColor;
  self.table.translatesAutoresizingMaskIntoConstraints = NO;

  for (UIView* v in @[ logo, title, self.table, play ]) [self.view addSubview:v];
  UILayoutGuide* g = self.view.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [logo.topAnchor constraintEqualToAnchor:g.topAnchor constant:16],
    [logo.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:32],
    [logo.widthAnchor constraintEqualToConstant:96],
    [logo.heightAnchor constraintEqualToConstant:96],
    [title.centerYAnchor constraintEqualToAnchor:logo.centerYAnchor],
    [title.leadingAnchor constraintEqualToAnchor:logo.trailingAnchor constant:20],
    [play.centerYAnchor constraintEqualToAnchor:logo.centerYAnchor],
    [play.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-32],
    [play.widthAnchor constraintEqualToConstant:220],
    [play.heightAnchor constraintEqualToConstant:64],
    [self.table.topAnchor constraintEqualToAnchor:logo.bottomAnchor constant:12],
    [self.table.leadingAnchor constraintEqualToAnchor:g.leadingAnchor],
    [self.table.trailingAnchor constraintEqualToAnchor:g.trailingAnchor],
    [self.table.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
  ]];
}

- (void)play:(id)sender {
  [_d synchronize];
  g_play = true;
}

// ---- rows

- (NSInteger)numberOfSectionsInTableView:(UITableView*)t {
  return kSectionCount;
}

- (NSString*)tableView:(UITableView*)t titleForHeaderInSection:(NSInteger)s {
  switch (s) {
    case kGame: return @"Game";
    case kExtrasSection: return @"Extras";
    case kGraphics: return @"Graphics";
    case kFixesSection: return @"Fixes";
    default: return @"FusionFix shaders";
  }
}

- (NSString*)tableView:(UITableView*)t titleForFooterInSection:(NSInteger)s {
  if (s == kGraphics)
    return @"Lower resolutions keep 60 fps steadier in busy streets. SMAA smooths edges cheaply; TAA also "
           @"steadies wires, fences and leaves.";
  // An episode's folder the game will not take, if the game's folder
  // (main.cpp: Documents/game) has one.
  if (s == kExtrasSection)
    return EpisodesNote([NSHomeDirectory() stringByAppendingPathComponent:@"Documents/game"]);
  return nil;
}

// Game: (title, key, kind) rows; kind 0 a switch.
static NSArray* GameRows() {
  return @[
    @[ @"Touch controls (hidden with a controller)", kTouchKey ], @[ @"Continue from the last save", kContinueKey ], @[ @"Skip the start screens", kSkipIntroKey ],
    @[ @"Game in the device's language (off: English)", kSystemLanguageKey ],
    @[ @"Mods", kModsKey ], @[ @"Fast loading", kFastLoadingKey ],
    @[ @"Stun punch on anyone", kStunPunchKey ], @[ @"Executions on anyone", kExecutionsKey ]
  ];
}

// Graphics: segmented choices (title, key, items).
static NSArray* GraphicsRows() {
  NSMutableArray* res = [NSMutableArray array];
  for (const Resolution& r : Resolutions()) [res addObject:r.title];
  return @[
    @[ @"Resolution", kIosResolutionKey, res ],
    @[ @"Anti-aliasing", kAntiAliasingKey, @[ @"Off", @"SMAA", @"TAA" ] ],
    @[ @"Sharpening", kTaaSharpeningKey, @[ @"Off", @"Low", @"Medium", @"High" ] ],
    @[ @"Console gamma", kConsoleGammaKey, @[ @"Off", @"Xbox 360", @"PS3" ] ],
    @[ @"Frame limit", kIosFrameLimitKey, @[ @"60", @"40", @"30", @"Off" ] ],
    @[ @"Shadows", kShadowsKey, @[ @"Game's", @"High" ] ],
    @[ @"Reflections", kReflectionsKey, @[ @"Game's", @"High" ] ],
    @[ @"Field of view", kFovKey, @[ @"+0", @"+5", @"+10", @"+15", @"+20" ] ],
    @[ @"Performance overlay", kHudKey, @[ @"Off", @"On" ] ],
  ];
}

- (NSInteger)tableView:(UITableView*)t numberOfRowsInSection:(NSInteger)s {
  switch (s) {
    case kGame: return GameRows().count;
    case kExtrasSection: return ExtraCount() + 1;  // the last row opens the list
    case kGraphics: return GraphicsRows().count;
    case kFixesSection: return ShownFixCount();
    default: return std::size(kShaderChoices);
  }
}

// The anti-aliasing control's segment for the stored value (0 off, 1 SMAA,
// 3 TAA on the Mac's scale) and back.
static NSInteger AaSegment(NSInteger v) { return v == 1 ? 1 : v == 3 ? 2 : 0; }
static NSInteger AaValue(NSInteger seg) { return seg == 1 ? 1 : seg == 2 ? 3 : 0; }

- (UITableViewCell*)tableView:(UITableView*)t cellForRowAtIndexPath:(NSIndexPath*)ip {
  UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
  cell.selectionStyle = UITableViewCellSelectionStyleNone;
  cell.backgroundColor = [UIColor colorWithWhite:0.11 alpha:1];
  cell.textLabel.numberOfLines = 0;
  if (ip.section == kExtrasSection && ip.row == ExtraCount()) {
    cell.textLabel.text = @"Achievements";
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    return cell;
  }
  NSString* key;
  if (ip.section == kGame || ip.section == kExtrasSection || ip.section == kFixesSection) {
    if (ip.section == kGame) {
      NSArray* row = GameRows()[ip.row];
      cell.textLabel.text = row[0];
      key = row[1];
    } else {
      const Fix& f = ip.section == kExtrasSection ? ExtraAt(ip.row) : ShownFix(ip.row);
      // (On the iPad the saves go through iCloud Drive: save_cloud.mm.)
      cell.textLabel.text = [f.key isEqualToString:@"SaveSync"] ? @"Saves synced with your Mac through iCloud Drive" : f.title;
      key = f.key;
    }
    UISwitch* sw = [UISwitch new];
    sw.on = [_d boolForKey:key];
    sw.accessibilityIdentifier = key;
    [sw addTarget:self action:@selector(switched:) forControlEvents:UIControlEventValueChanged];
    cell.accessoryView = sw;
    return cell;
  }
  NSArray* items;
  NSInteger value;
  if (ip.section == kGraphics) {
    NSArray* row = GraphicsRows()[ip.row];
    cell.textLabel.text = row[0];
    key = row[1];
    items = row[2];
    value = [key isEqualToString:kAntiAliasingKey] ? AaSegment([_d integerForKey:key])
            : [key isEqualToString:kHudKey]       ? ([_d boolForKey:key] ? 1 : 0)
                                                  : [_d integerForKey:key];
  } else {
    const ShaderChoice& sc = kShaderChoices[ip.row];
    cell.textLabel.text = [sc.title stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@":"]];
    key = sc.key;
    items = sc.items;
    value = [_d integerForKey:key];
  }
  UISegmentedControl* seg = [[UISegmentedControl alloc] initWithItems:items];
  seg.selectedSegmentIndex = std::clamp<NSInteger>(value, 0, NSInteger(items.count) - 1);
  seg.accessibilityIdentifier = key;
  // Sharpening follows TAA only here (no MetalFX on the iPad): greyed with
  // the others.
  if ([key isEqualToString:kTaaSharpeningKey]) seg.enabled = SharpeningApplies([_d integerForKey:kAntiAliasingKey]);
  [seg addTarget:self action:@selector(segmented:) forControlEvents:UIControlEventValueChanged];
  [seg sizeToFit];
  cell.accessoryView = seg;
  return cell;
}

- (void)tableView:(UITableView*)t didSelectRowAtIndexPath:(NSIndexPath*)ip {
  if (ip.section != kExtrasSection || ip.row != ExtraCount()) return;
  [t deselectRowAtIndexPath:ip animated:YES];
  LpmAchievements* list = [LpmAchievements new];
  list.modalPresentationStyle = UIModalPresentationPageSheet;
  [self presentViewController:list animated:YES completion:nil];
}

- (void)switched:(UISwitch*)sw {
  [_d setBool:sw.on forKey:sw.accessibilityIdentifier];
}

- (void)segmented:(UISegmentedControl*)seg {
  NSString* key = seg.accessibilityIdentifier;
  if ([key isEqualToString:kAntiAliasingKey]) {
    [_d setInteger:AaValue(seg.selectedSegmentIndex) forKey:key];
    [self.table reloadSections:[NSIndexSet indexSetWithIndex:kGraphics] withRowAnimation:UITableViewRowAnimationNone];
  } else if ([key isEqualToString:kHudKey])
    [_d setBool:seg.selectedSegmentIndex == 1 forKey:key];
  else
    [_d setInteger:seg.selectedSegmentIndex forKey:key];
}

@end

@implementation LpmAchievements {
  NSMutableArray<NSDictionary*>* _groups;
  UILabel* _summary;
  UITableView* _table;
  UISegmentedControl* _filter;
  UIButton* _style;
  UIButton* _place;
  NSUserDefaults* _d;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  _d = NSUserDefaults.standardUserDefaults;
  self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
  self.view.backgroundColor = UIColor.blackColor;
  _summary = [UILabel new];
  _summary.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  _summary.textColor = UIColor.whiteColor;
  _summary.numberOfLines = 0;
  _summary.translatesAutoresizingMaskIntoConstraints = NO;
  _style = [UIButton buttonWithType:UIButtonTypeSystem];
  _style.translatesAutoresizingMaskIntoConstraints = NO;
  [_style addTarget:self action:@selector(cycleStyle:) forControlEvents:UIControlEventTouchUpInside];
  _place = [UIButton buttonWithType:UIButtonTypeSystem];
  _place.translatesAutoresizingMaskIntoConstraints = NO;
  [_place addTarget:self action:@selector(cyclePlace:) forControlEvents:UIControlEventTouchUpInside];
  _filter = [[UISegmentedControl alloc] initWithItems:@[ @"All", @"Unlocked", @"Locked" ]];
  _filter.selectedSegmentIndex = 0;
  _filter.translatesAutoresizingMaskIntoConstraints = NO;
  [_filter addTarget:self action:@selector(reload) forControlEvents:UIControlEventValueChanged];
  _table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
  _table.dataSource = self;
  _table.delegate = self;
  _table.backgroundColor = UIColor.blackColor;
  _table.translatesAutoresizingMaskIntoConstraints = NO;
  UIButton* close = [UIButton buttonWithType:UIButtonTypeSystem];
  [close setTitle:@"Done" forState:UIControlStateNormal];
  close.translatesAutoresizingMaskIntoConstraints = NO;
  [close addTarget:self action:@selector(close:) forControlEvents:UIControlEventTouchUpInside];
  UIButton* reset = [UIButton buttonWithType:UIButtonTypeSystem];
  [reset setTitle:@"Reset" forState:UIControlStateNormal];
  reset.translatesAutoresizingMaskIntoConstraints = NO;
  [reset addTarget:self action:@selector(reset:) forControlEvents:UIControlEventTouchUpInside];
  for (UIView* v in @[ close, _style, _place, _filter, _summary, _table, reset ]) [self.view addSubview:v];
  UILayoutGuide* g = self.view.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [close.topAnchor constraintEqualToAnchor:g.topAnchor constant:8],
    [close.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-20],
    [_style.topAnchor constraintEqualToAnchor:close.bottomAnchor constant:4],
    [_style.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:24],
    [_place.topAnchor constraintEqualToAnchor:_style.bottomAnchor constant:4],
    [_place.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:24],
    [_filter.topAnchor constraintEqualToAnchor:_place.bottomAnchor constant:8],
    [_filter.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:24],
    [_filter.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-24],
    [_summary.topAnchor constraintEqualToAnchor:_filter.bottomAnchor constant:8],
    [_summary.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:24],
    [_summary.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-24],
    [_table.topAnchor constraintEqualToAnchor:_summary.bottomAnchor constant:8],
    [_table.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [_table.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
    [_table.bottomAnchor constraintEqualToAnchor:reset.topAnchor constant:-8],
    [reset.centerXAnchor constraintEqualToAnchor:g.centerXAnchor],
    [reset.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-12],
  ]];
  [self showStyle];
  [self showPlace];
  [self reload];
}

- (void)showStyle {
  const NSInteger i = AchievementStyleIndex([_d stringForKey:kAchievementStyleKey]);
  [_style setTitle:[NSString stringWithFormat:@"Style: %@", AchievementStyleTitles()[i]] forState:UIControlStateNormal];
}

- (void)cycleStyle:(id)sender {
  NSInteger i = AchievementStyleIndex([_d stringForKey:kAchievementStyleKey]);
  i = (i + 1) % NSInteger(AchievementStyleKeys().count);
  [_d setObject:AchievementStyleKeys()[i] forKey:kAchievementStyleKey];
  [self showStyle];
}

- (void)showPlace {
  const NSInteger i = AchievementPositionIndex([_d stringForKey:kAchievementPositionKey]);
  [_place setTitle:[NSString stringWithFormat:@"Position: %@", AchievementPositionTitles()[i]]
          forState:UIControlStateNormal];
}

- (void)cyclePlace:(id)sender {
  NSInteger i = AchievementPositionIndex([_d stringForKey:kAchievementPositionKey]);
  i = (i + 1) % NSInteger(AchievementPositionKeys().count);
  [_d setObject:AchievementPositionKeys()[i] forKey:kAchievementPositionKey];
  [self showPlace];
}

- (void)reload {
  const auto store = gtaiv::achievements::Store::Load(gtaiv::achievements::DefaultPath());
  const auto all = gtaiv::achievements::Rows(store);
  const NSInteger filter = _filter.selectedSegmentIndex;
  NSMutableString* summary = [NSMutableString string];
  _groups = [NSMutableArray new];
  const gtaiv::achievements::Game order[] = {gtaiv::achievements::Game::iv, gtaiv::achievements::Game::tlad,
                                             gtaiv::achievements::Game::tbogt, gtaiv::achievements::Game::unknown};
  for (auto game : order) {
    int total = 0, open = 0, points = 0, earned = 0;
    NSMutableArray* items = [NSMutableArray new];
    for (const auto& row : all) {
      if (row.game != game) continue;
      total++;
      if (row.points > 0) points += row.points;
      if (row.unlocked) {
        open++;
        if (row.points > 0) earned += row.points;
      }
      if (filter == 1 && !row.unlocked) continue;
      if (filter == 2 && row.unlocked) continue;
      NSString* when = @"";
      if (row.unlocked && row.unlocked_at > 0)
        when = [NSDateFormatter localizedStringFromDate:[NSDate dateWithTimeIntervalSince1970:row.unlocked_at]
                                               dateStyle:NSDateFormatterMediumStyle
                                               timeStyle:NSDateFormatterShortStyle];
      NSString* side = row.points < 0 ? (row.unlocked ? @"Unlocked" : @"Locked")
                                      : [NSString stringWithFormat:@"%d G", row.points];
      if (row.unlocked && when.length) side = [side stringByAppendingFormat:@"\n%@", when];
      else if (!row.unlocked) side = [side stringByAppendingString:@"\nLocked"];
      [items addObject:@{@"name" : @(row.name.c_str()), @"detail" : @(row.description.c_str()), @"side" : side,
                         @"on" : @(row.unlocked)}];
    }
    if (total == 0) continue;
    NSString* title = game == gtaiv::achievements::Game::iv ? @"GTA IV"
                      : game == gtaiv::achievements::Game::tlad ? @"The Lost and Damned"
                      : game == gtaiv::achievements::Game::tbogt ? @"The Ballad of Gay Tony"
                                                                : @"Other";
    [summary appendFormat:@"%@: %d/%d, %d/%d G\n", title, open, total, earned, points];
    if (items.count == 0) continue;
    [_groups addObject:@{
      @"title" : [NSString stringWithFormat:@"%@  %d/%d, %d/%d G", title, open, total, earned, points],
      @"rows" : items,
    }];
  }
  _summary.text = summary.length ? summary : @"No achievements in the table.";
  [_table reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView*)t {
  return _groups.count;
}

- (NSString*)tableView:(UITableView*)t titleForHeaderInSection:(NSInteger)s {
  return _groups[s][@"title"];
}

- (NSInteger)tableView:(UITableView*)t numberOfRowsInSection:(NSInteger)s {
  return [_groups[s][@"rows"] count];
}

- (UITableViewCell*)tableView:(UITableView*)t cellForRowAtIndexPath:(NSIndexPath*)ip {
  UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
  NSDictionary* row = _groups[ip.section][@"rows"][ip.row];
  cell.backgroundColor = [UIColor colorWithWhite:0.11 alpha:1];
  cell.textLabel.text = row[@"name"];
  cell.textLabel.numberOfLines = 0;
  cell.detailTextLabel.text = row[@"detail"];
  cell.detailTextLabel.numberOfLines = 0;
  cell.detailTextLabel.textColor = UIColor.secondaryLabelColor;
  UILabel* side = [UILabel new];
  side.text = row[@"side"];
  side.numberOfLines = 0;
  side.textAlignment = NSTextAlignmentRight;
  side.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
  side.textColor = [row[@"on"] boolValue] ? [UIColor colorWithRed:0.93 green:0.84 blue:0.55 alpha:1] : UIColor.secondaryLabelColor;
  [side sizeToFit];
  cell.accessoryView = side;
  cell.selectionStyle = UITableViewCellSelectionStyleNone;
  return cell;
}

- (void)close:(id)sender {
  [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)reset:(id)sender {
  UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Reset local achievements?"
      message:@"This clears the list stored on this device. Save files are not changed. Achievements already earned in a save are not added back here until the game awards them again."
      preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
  [alert addAction:[UIAlertAction actionWithTitle:@"Reset" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) {
    auto store = gtaiv::achievements::Store::Load(gtaiv::achievements::DefaultPath());
    store.Clear();
    if (!store.Save()) std::fprintf(stderr, "achievement: could not reset %s\n", store.path.c_str());
    [self reload];
  }]];
  [self presentViewController:alert animated:YES completion:nil];
}

@end

#endif  // !TARGET_OS_TV

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
    RegisterDefaults(d);
#if TARGET_OS_TV
    // Apple TV: no settings screen yet; its own defaults (1920x1080, SMAA,
    // the episodes on: TvDefaults).
    [d registerDefaults:TvDefaults()];
    ApplyOptions(d);
    setenv("GTAIV_VSYNC", "1", 1);
    *launcher_args = {"-width", "1920", "-height", "1080"};
#else
    // The defaults here: the second resolution (the iPad's 1792x1344, two
    // thirds of an iPhone's screen) at 60, SMAA (TAA costs the GPU
    // time a 60 fps frame lacks here), the rest as on the Mac.
    [d registerDefaults:@{kIosResolutionKey : @1, kIosFrameLimitKey : @0}];
    if (![d objectForKey:@"IosDefaultsV1"]) {
      [d setInteger:1 forKey:kAntiAliasingKey];
      [d setBool:NO forKey:kHudKey];
      [d setBool:YES forKey:@"IosDefaultsV1"];
    }
    // The graphics menu's settings apply from the start (the game itself
    // works them out only when that menu opens: until then everything ran
    // at its lowest, and the player's choices seemed lost on every start).
    if (![d objectForKey:@"IosDefaultsV2"]) {
      [d setBool:YES forKey:@"FixDrawDistance"];
      [d setBool:YES forKey:@"IosDefaultsV2"];
    }

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
    window.rootViewController = [LpmLauncher new];
    window.windowLevel = UIWindowLevelNormal + 1;
    [window makeKeyAndVisible];
    while (!g_play) {
      @autoreleasepool {
        [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
      }
    }
    window.hidden = YES;
    window.rootViewController = nil;

    ApplyOptions(d);
    // The Mac's vsync choice does not apply: here the flag only makes a
    // frame wait for the display's next refresh (there is no tearing to
    // turn on), also with the frame limit off.
    setenv("GTAIV_VSYNC", "1", 1);
    // Nor does its keeping running behind other windows (not shown here;
    // iOS suspends an app that is not in front), even if saved on before.
    setenv("GTAIV_RUN_IN_BACKGROUND", "0", 1);
    const int limit = FrameLimit([d integerForKey:kIosFrameLimitKey]);
    if (limit) setenv("GTAIV_FPS_LIMIT", std::to_string(limit).c_str(), 1);
    const auto& resolutions = Resolutions();
    const Resolution& r = resolutions[std::clamp<NSInteger>([d integerForKey:kIosResolutionKey], 0, NSInteger(resolutions.size()) - 1)];
    *launcher_args = {"-width", std::to_string(r.w), "-height", std::to_string(r.h)};
#endif
  }
}
