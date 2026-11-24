// achievements.h: the GFWL table, the JSON store, the toast clock and the
// chime. No game files and no Metal.

#include "achievements.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "achievements_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

std::string Read(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // namespace

int main() {
  using namespace gtaiv::achievements;

  int iv_points = 0, tlad_points = 0, tbogt_points = 0;
  EXPECT(CatalogCount() == 65);
  for (int i = 1; i <= 65; i++) {
    const Entry* row = Find(uint32_t(i));
    EXPECT(row && int(row->id) == i);
    EXPECT(row && row->points > 0 && row->name && row->name[0] && row->description && row->description[0]);
    if (!row) continue;
    if (i <= 50) {
      EXPECT(row->game == Game::iv);
      iv_points += row->points;
    } else if (i <= 55) {
      EXPECT(row->game == Game::tlad);
      tlad_points += row->points;
    } else {
      EXPECT(row->game == Game::tbogt);
      tbogt_points += row->points;
    }
    EXPECT(Catalog()[i - 1].id == row->id);
  }
  EXPECT(iv_points == 1000);
  EXPECT(tlad_points == 250);
  EXPECT(tbogt_points == 250);
  EXPECT(Find(0) == nullptr);
  EXPECT(Find(66) == nullptr);
  EXPECT(Find(1) && std::strcmp(Find(1)->name, "Off The Boat") == 0);
  EXPECT(Find(51) && std::strcmp(Find(51)->name, "TLAD: One Percenter") == 0);
  EXPECT(Find(65) && std::strcmp(Find(65)->name, "TBoGT: Gold Star") == 0);
  EXPECT(ToastDetail(5, "Off The Boat") == "5G - Off The Boat");
  EXPECT(ToastDetail(-1, "Achievement 9") == "Achievement 9");

  const std::string dir = "/tmp/gtaiv-achievements-test";
  std::system(("rm -rf " + dir).c_str());
  std::system(("mkdir -p " + dir + "/savegames").c_str());
  const std::string save = dir + "/savegames/SGTA400";
  {
    std::ofstream out(save, std::ios::binary);
    out << "save-bytes";
  }
  const std::string path = dir + "/achievements.json";
  EXPECT(path.find("savegames") == std::string::npos);

  Store store = Store::Load(path);
  EXPECT(store.unlocks().empty());
  EXPECT(store.Add(1, 1'700'000'000));
  EXPECT(!store.Add(1, 1'800'000'000));  // the first time stays
  EXPECT(store.Add(999, 1'700'000'100));
  EXPECT(store.Add(53, 1'700'000'050));
  EXPECT(store.Save());
  EXPECT(Read(save) == "save-bytes");

  Store again = Store::Load(path);
  EXPECT(again.unlocks().size() == 3);
  EXPECT(again.IsUnlocked(1) && again.unlocks()[0].unlocked_at == 1'700'000'000);
  EXPECT(again.unlocks()[0].game == Game::iv);
  EXPECT(again.IsUnlocked(53) && again.unlocks()[1].game == Game::tlad);
  EXPECT(again.IsUnlocked(999) && again.unlocks()[2].game == Game::unknown);

  const std::string body = Read(path);
  EXPECT(body.find("\"id\": 1") != std::string::npos);
  EXPECT(body.find("\"game\": \"tlad\"") != std::string::npos);
  EXPECT(body.find("savegames") == std::string::npos);

  // Extra keys and odd spacing still read.
  {
    std::ofstream out(path);
    out << "{\n  \"version\": 1, \"note\": \"kept\",\n  \"unlocks\": [\n"
           "    { \"unlocked_at\": \"2023-11-14T00:00:00Z\", \"id\": 40, \"game\": \"iv\" }\n"
           "  ]\n}\n";
  }
  Store spaced = Store::Load(path);
  EXPECT(spaced.IsUnlocked(40));
  EXPECT(spaced.unlocks().size() == 1);
  EXPECT(spaced.unlocks()[0].unlocked_at > 1'600'000'000);

  const auto rows = Rows(spaced);
  EXPECT(rows.size() == 65);
  EXPECT(rows[39].unlocked && rows[39].id == 40 && rows[39].points == 100);
  EXPECT(!rows[0].unlocked);
  const Score score = Summarize(spaced);
  EXPECT(score.catalog_count == 65);
  EXPECT(score.catalog_points == 1500);
  EXPECT(score.unlocked_known == 1);
  EXPECT(score.unlocked_points == 100);
  EXPECT(score.unlocked_unknown == 0);

  auto page = UnlockedSlice(spaced, 0, 10);
  EXPECT(page.size() == 1 && page[0].id == 40 && page[0].points == 100);
  EXPECT(UnlockedSlice(spaced, 1, 10).empty());
  EXPECT(EnumBufferBytes(4) > 36u * 4);

  uint32_t low = 0, high = 0;
  UnixToFileTime(0, &low, &high);
  EXPECT(low == 0xD53E8000u);
  EXPECT(high == 0x019DB1DEu);

  // A file that is not ours is moved aside, not parsed as unlocks.
  {
    std::ofstream out(path);
    out << "this is not the list";
  }
  Store broken = Store::Load(path);
  EXPECT(broken.unlocks().empty());
  EXPECT(!broken.save_blocked);
  EXPECT(Read(path + ".bak") == "this is not the list");
  EXPECT(broken.Add(2, 1'700'000'200));
  EXPECT(broken.Save());
  EXPECT(Read(path).find("\"id\": 2") != std::string::npos);
  EXPECT(Read(save) == "save-bytes");

  Store clearing = Store::Load(path);
  clearing.Clear();
  EXPECT(clearing.Save());
  EXPECT(Store::Load(path).unlocks().empty());

  std::vector<Row> with_unknown = Rows(Store::Load(path));
  {
    Store extra = Store::Load(path);
    extra.Add(4242, 1'710'000'000);
    extra.Save();
    with_unknown = Rows(extra);
  }
  EXPECT(with_unknown.size() == 66);
  EXPECT(with_unknown.back().id == 4242);
  EXPECT(with_unknown.back().points < 0);
  EXPECT(with_unknown.back().name == "Achievement 4242");
  EXPECT(Summarize(Store::Load(path)).unlocked_unknown == 1);

  EXPECT(ParseStyle("xbox_b") == Style::XboxB);
  EXPECT(ParseStyle("PS3") == Style::Ps3);
  EXPECT(ParseStyle("nope") == Style::XboxA);
  EXPECT(std::strcmp(StyleKey(Style::Steam), "steam") == 0);
  const Motion born = MotionAt(Style::XboxA, 0);
  EXPECT(born.visible && born.orb == 0.f && born.pill == 0.f);
  const Motion grown = MotionAt(Style::XboxA, 250 + 350);
  EXPECT(grown.orb == 1.f && grown.pill == 1.f && grown.slide == 1.f);
  EXPECT(!MotionAt(Style::XboxA, StyleDuration(Style::XboxA)).visible);
  const Motion ps = MotionAt(Style::Ps3, 0);
  EXPECT(ps.visible && ps.slide == 0.f);
  EXPECT(MotionAt(Style::Ps3, 400 + 100).slide == 1.f);
  const ToastBox xbox = PlaceToast(Style::XboxA, 1, 1280, 720);
  EXPECT(xbox.w == 432 && xbox.h == 82);
  EXPECT(xbox.x == (1280 - 432) / 2);
  EXPECT(xbox.y == 720 - 58 - 82);
  const ToastBox ps3 = PlaceToast(Style::Ps3, 1, 1280, 720);
  EXPECT(ps3.w == 300 && ps3.h == 72 && ps3.y == 55);
  EXPECT(ps3.x == 1280 - 68 - 300);
  EXPECT(PlaceToast(Style::Ps3, 0, 1280, 720).x == 1280);
  const ToastBox steam = PlaceToast(Style::Steam, 1, 1280, 720);
  EXPECT(steam.w == 300 && steam.h == 80);
  EXPECT(steam.x == 1280 - 16 - 300);
  EXPECT(PlaceToast(Style::Steam, 0, 1280, 720).y == 720);
  const ToastCopy rare = CopyFor(Style::XboxE, 100, "Key To The City", "done", false);
  EXPECT(rare.line1 == "Rare Achievement Unlocked");
  EXPECT(rare.line2 == "100G - Key To The City");
  const ToastCopy trophy = CopyFor(Style::Ps3, 5, "Off The Boat", "d", false);
  EXPECT(trophy.line1 == "You have earned a trophy.");
  EXPECT(trophy.line2 == "Off The Boat");
  EXPECT(trophy.line2.find('G') == std::string::npos || trophy.line2 == "Off The Boat");
  const ToastCopy steam_copy = CopyFor(Style::Steam, 5, "Off The Boat", "Complete the first mission.", false);
  EXPECT(steam_copy.line1 == "Achievement Unlocked!");
  EXPECT(steam_copy.line3 == "Complete the first mission.");
  EXPECT(CopyFor(Style::XboxA, -1, "Infernus", "", true).line1 == "Entered");
  EXPECT(CopyFor(Style::XboxA, -1, "Infernus", "", true).line2 == "Infernus");
  EXPECT(TitleCaseLabel("SULTAN RS") == "Sultan RS");
  EXPECT(TitleCaseLabel("INFERNUS") == "Infernus");
  EXPECT(ParsePlace("top_right") == Place::TopRight);
  EXPECT(ParsePlace("nope") == Place::Default);
  EXPECT(std::strcmp(PlaceKey(Place::Bottom), "bottom") == 0);
  uint8_t src[8] = {9, 8, 7, 6, 1, 2, 3, 4};
  uint8_t dst[8] = {};
  CopyToastRows(src, dst, 1, 2);
  EXPECT(dst[0] == 9 && dst[4] == 1);  // row 0 stays the top row
  setenv("GTAIV_ACHIEVEMENT_POSITION", "top_left", 1);
  const ToastBox corner = PlaceToast(Style::XboxA, 1, 1280, 720);
  const int margin = int(std::lround(0.06f * 720.f));
  EXPECT(corner.x == margin && corner.y == margin);
  EXPECT(PlaceToast(Style::XboxA, 0, 1280, 720).x == -corner.w);
  setenv("GTAIV_ACHIEVEMENT_POSITION", "top", 1);
  EXPECT(PlaceToast(Style::Ps3, 0, 1280, 720).y == -PlaceToast(Style::Ps3, 1, 1280, 720).h);
  setenv("GTAIV_ACHIEVEMENT_POSITION", "bottom", 1);
  EXPECT(PlaceToast(Style::Steam, 0, 1280, 720).y == 720);
  const ToastBox bottom = PlaceToast(Style::Steam, 1, 1280, 720);
  EXPECT(bottom.y == 720 - margin - bottom.h);
  EXPECT(bottom.x == (1280 - bottom.w) / 2);
  setenv("GTAIV_ACHIEVEMENT_POSITION", "bottom_right", 1);
  const ToastBox br = PlaceToast(Style::XboxA, 1, 1280, 720);
  EXPECT(br.x == 1280 - margin - br.w && br.y == 720 - margin - br.h);
  EXPECT(PlaceToast(Style::XboxA, 0, 1280, 720).x == 1280);
  unsetenv("GTAIV_ACHIEVEMENT_POSITION");

  ToastQueue queue;
  queue.Push("Achievement Unlocked", ToastDetail(5, "Off The Boat"), 1000);
  queue.Push("Achievement Unlocked", ToastDetail(10, "Pool Shark"), 1000);
  const ToastFrame start = queue.At(1000);
  EXPECT(start.visible && start.chime && start.orb == 0.f && start.pill == 0.f);
  EXPECT(start.detail == ToastDetail(5, "Off The Boat"));
  EXPECT(!queue.At(1000).chime);
  const int64_t xbox_ms = StyleDuration(Style::XboxA);
  EXPECT(queue.At(1000 + 250 + 350).pill == 1.f);
  EXPECT(queue.At(1000 + xbox_ms - 1).visible);
  const ToastFrame next = queue.At(1000 + xbox_ms);
  EXPECT(next.visible && next.chime);
  EXPECT(next.detail == ToastDetail(10, "Pool Shark"));
  EXPECT(!queue.At(1000 + xbox_ms + xbox_ms).visible);

  const Pcm chime = MakeUnlockChime();
  EXPECT(chime.sample_rate == 44100);
  EXPECT(chime.samples.size() > 44100 / 3 && chime.samples.size() < 44100);
  int peak = 0, tail = 0;
  for (size_t i = 0; i < chime.samples.size(); i++) {
    const int v = std::abs(int(chime.samples[i]));
    peak = std::max(peak, v);
    if (i > chime.samples.size() * 9 / 10) tail = std::max(tail, v);
  }
  EXPECT(peak > 2000 && peak < 20000);
  EXPECT(tail * 4 < peak);

  const char* old_dir = std::getenv("GTAIV_ACHIEVEMENTS_DIR");
  const std::string saved_dir = old_dir ? old_dir : "";
  setenv("GTAIV_ACHIEVEMENTS_DIR", "/tmp/gtaiv-achievements-dir", 1);
  EXPECT(DefaultPath() == "/tmp/gtaiv-achievements-dir/achievements.json");
  EXPECT(DefaultPath().find("savegames") == std::string::npos);
  if (old_dir) setenv("GTAIV_ACHIEVEMENTS_DIR", saved_dir.c_str(), 1);
  else unsetenv("GTAIV_ACHIEVEMENTS_DIR");

  const char* old_on = std::getenv("GTAIV_ACHIEVEMENTS");
  const std::string saved_on = old_on ? old_on : "";
  unsetenv("GTAIV_ACHIEVEMENTS");
  EXPECT(Enabled());
  setenv("GTAIV_ACHIEVEMENTS", "0", 1);
  EXPECT(!Enabled());
  setenv("GTAIV_ACHIEVEMENTS", "1", 1);
  EXPECT(Enabled());
  if (old_on) setenv("GTAIV_ACHIEVEMENTS", saved_on.c_str(), 1);
  else unsetenv("GTAIV_ACHIEVEMENTS");

  setenv("GTAIV_ACHIEVEMENTS_DIR", dir.c_str(), 1);
  setenv("GTAIV_ACHIEVEMENTS", "0", 1);
  ResetSessionForTests();
  EXPECT(!RecordUnlock(7));
  setenv("GTAIV_ACHIEVEMENTS", "1", 1);
  ResetSessionForTests();
  unsetenv("GTAIV_ACHIEVEMENT_TEST");
  EXPECT(RecordUnlock(7));
  EXPECT(!RecordUnlock(7));
  // The load presents long before the city. A toast queued then stays
  // queued, and starts kAfterSceneMs after the first scene.
  EXPECT(!PresentToastAt(false, 1000).visible);
  EXPECT(!PresentToastAt(true, 1000).visible);
  EXPECT(!PresentToastAt(true, 1000 + kAfterSceneMs - 1).visible);
  const ToastFrame shown = PresentToastAt(true, 1000 + kAfterSceneMs);
  EXPECT(shown.visible && shown.chime && shown.detail == ToastDetail(30, "Wheelie Rider"));
  // An unlock after that shows on the next present, not after another wait.
  EXPECT(RecordUnlock(2));
  const ToastFrame later = PresentToastAt(true, 1000 + kAfterSceneMs + StyleDuration(Style::XboxA));
  EXPECT(later.visible && later.chime && later.detail == ToastDetail(10, "One Hundred And Eighty"));
  setenv("GTAIV_ACHIEVEMENT_TEST", "40", 1);
  ResetSessionForTests();
  EXPECT(!PresentToastAt(false, 0).visible);  // not armed during the load
  EXPECT(!PresentToastAt(true, 50).visible);
  const ToastFrame test = PresentToastAt(true, 50 + kAfterSceneMs);
  EXPECT(test.visible && test.chime);
  EXPECT(test.detail == ToastDetail(100, "Key To The City"));
  EXPECT(Store::Load(DefaultPath()).IsUnlocked(7));
  EXPECT(Store::Load(DefaultPath()).IsUnlocked(2));
  EXPECT(!Store::Load(DefaultPath()).IsUnlocked(40));  // the test toast is not stored
  const std::string before_vehicle = Read(DefaultPath());
  setenv("GTAIV_ACHIEVEMENT_STYLE", "steam", 1);
  QueueVehicleToast("Infernus");
  const ToastFrame vehicle = PresentToastAt(true, 50 + kAfterSceneMs + StyleDuration(Style::XboxA));
  EXPECT(vehicle.visible && vehicle.chime);
  EXPECT(vehicle.style == Style::Steam);
  EXPECT(vehicle.headline == "Entered");
  EXPECT(vehicle.detail == "Infernus");
  EXPECT(Read(DefaultPath()) == before_vehicle);
  unsetenv("GTAIV_ACHIEVEMENT_STYLE");
  unsetenv("GTAIV_ACHIEVEMENT_TEST");
  unsetenv("GTAIV_ACHIEVEMENTS_DIR");
  if (old_on) setenv("GTAIV_ACHIEVEMENTS", saved_on.c_str(), 1);
  else unsetenv("GTAIV_ACHIEVEMENTS");

  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("ok\n");
  return 0;
}
