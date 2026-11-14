// episode_files (src/episode_files.h): whether an episode is installed in
// its folder, from the files that are there, and what the launchers tell
// the player of a folder the game will not take.

#include "episode_files.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Expect(bool ok, const char* what, int line) {
  if (ok) return;
  std::fprintf(stderr, "episode_files_test.cpp:%d: FAILED: %s\n", line, what);
  g_failures++;
}
#define EXPECT(x) Expect((x), #x, __LINE__)

using episode_files::Find;
using episode_files::Installed;
using episode_files::kTBoGT;
using episode_files::kTLAD;
using episode_files::State;

// A game folder with these files and no others (paths with backslashes,
// compared exactly: matching without case is not Find's).
struct Files {
  std::set<std::string> there;
  bool operator()(const std::string& path) const { return there.count(path) != 0; }
};

// The names the game's code has (0088F440: "\E1_List.txt" at 00D892AC for
// mask 2, "\E2_List.txt" at 00D892BC for mask 4; "setup2.xml" at 00D893B4).
void TestTheGamesNames() {
  EXPECT(std::strcmp(episode_files::Folder(kTLAD), "TLAD") == 0);
  EXPECT(std::strcmp(episode_files::Folder(kTBoGT), "TBoGT") == 0);
  EXPECT(std::strcmp(episode_files::ListName(kTLAD), "E1_List.txt") == 0);
  EXPECT(std::strcmp(episode_files::ListName(kTBoGT), "E2_List.txt") == 0);
  EXPECT(std::strcmp(episode_files::kSetup, "setup2.xml") == 0);
  EXPECT(episode_files::Folder(0) == nullptr && episode_files::Folder(3) == nullptr);
  EXPECT(episode_files::ListName(0) == nullptr && episode_files::ListName(3) == nullptr);
}

// The game's mask: 2 TLAD, 4 TBoGT; with both bits TLAD, tested first.
void TestTheMask() {
  using episode_files::EpisodeOfMask;
  EXPECT(EpisodeOfMask(2) == kTLAD);
  EXPECT(EpisodeOfMask(4) == kTBoGT);
  EXPECT(EpisodeOfMask(6) == kTLAD);
  EXPECT(EpisodeOfMask(0) == 0);
  EXPECT(EpisodeOfMask(1) == 0);
  EXPECT(EpisodeOfMask(8) == 0);
}

// Both files in the episode's own folder: installed.
void TestBothFiles() {
  const Files tlad{{"TLAD\\setup2.xml", "TLAD\\E1_List.txt"}};
  EXPECT(Find(kTLAD, tlad) == State::kInstalled);
  EXPECT(Installed(kTLAD, tlad));
  const Files tbogt{{"TBoGT\\setup2.xml", "TBoGT\\E2_List.txt"}};
  EXPECT(Find(kTBoGT, tbogt) == State::kInstalled);
  EXPECT(Installed(kTBoGT, tbogt));
}

// setup2.xml and no list: not installed, and told apart from no folder.
void TestSetupWithoutTheList() {
  const Files tlad{{"TLAD\\setup2.xml"}};
  EXPECT(Find(kTLAD, tlad) == State::kNoList);
  EXPECT(!Installed(kTLAD, tlad));
  const Files tbogt{{"TBoGT\\setup2.xml"}};
  EXPECT(Find(kTBoGT, tbogt) == State::kNoList);
  EXPECT(!Installed(kTBoGT, tbogt));
}

// The list and no setup2.xml, or neither: absent.
void TestWithoutSetup() {
  const Files list_only{{"TLAD\\E1_List.txt", "TBoGT\\E2_List.txt"}};
  EXPECT(Find(kTLAD, list_only) == State::kAbsent);
  EXPECT(Find(kTBoGT, list_only) == State::kAbsent);
  EXPECT(!Installed(kTLAD, list_only) && !Installed(kTBoGT, list_only));
  const Files none{};
  EXPECT(Find(kTLAD, none) == State::kAbsent);
  EXPECT(Find(kTBoGT, none) == State::kAbsent);
  EXPECT(!Installed(kTLAD, none) && !Installed(kTBoGT, none));
}

// Each episode its own list: the other episode's does not count.
void TestTheOtherEpisodesList() {
  const Files swapped{{"TLAD\\setup2.xml", "TLAD\\E2_List.txt", "TBoGT\\setup2.xml", "TBoGT\\E1_List.txt"}};
  EXPECT(Find(kTLAD, swapped) == State::kNoList);
  EXPECT(Find(kTBoGT, swapped) == State::kNoList);
}

// Each episode its own folder: one episode's files say nothing of the
// other, and files in the game's own folder nothing of either.
void TestTheOtherEpisodesFolder() {
  const Files tlad{{"TLAD\\setup2.xml", "TLAD\\E1_List.txt"}};
  EXPECT(Find(kTBoGT, tlad) == State::kAbsent);
  const Files tbogt{{"TBoGT\\setup2.xml", "TBoGT\\E2_List.txt"}};
  EXPECT(Find(kTLAD, tbogt) == State::kAbsent);
  const Files root{{"setup2.xml", "E1_List.txt", "E2_List.txt"}};
  EXPECT(Find(kTLAD, root) == State::kAbsent);
  EXPECT(Find(kTBoGT, root) == State::kAbsent);
  // TLAD installed, TBoGT with setup2.xml alone: each its own answer.
  const Files mixed{{"TLAD\\setup2.xml", "TLAD\\E1_List.txt", "TBoGT\\setup2.xml"}};
  EXPECT(Find(kTLAD, mixed) == State::kInstalled);
  EXPECT(Find(kTBoGT, mixed) == State::kNoList);
}

// No such episode: absent, and no file asked for.
void TestNoEpisode() {
  for (uint32_t episode : {0u, 3u}) {
    int asked = 0;
    EXPECT(Find(episode, [&](const std::string&) { return asked++, true; }) == State::kAbsent);
    EXPECT(asked == 0);
  }
}

// What is asked of the files, for an installed episode: setup2.xml and the
// list, in the episode's folder, nothing else.
void TestWhatIsAsked() {
  std::vector<std::string> asked;
  EXPECT(Find(kTBoGT, [&](const std::string& path) { return asked.push_back(path), true; }) == State::kInstalled);
  EXPECT(asked == (std::vector<std::string>{"TBoGT\\setup2.xml", "TBoGT\\E2_List.txt"}));
}

bool Says(const std::string& note, const char* what) { return note.find(what) != std::string::npos; }

// The episodes the launchers have something to say of: those with
// setup2.xml and no list, each on its own. No folder, an installed one and
// a folder with the list alone (not an episode's folder at all) are none.
void TestMissingLists() {
  using episode_files::MissingLists;
  const uint32_t tlad = 1u << kTLAD, tbogt = 1u << kTBoGT;
  EXPECT(MissingLists(Files{}) == 0);
  EXPECT(MissingLists(Files{{"TLAD\\setup2.xml", "TLAD\\E1_List.txt", "TBoGT\\setup2.xml", "TBoGT\\E2_List.txt"}}) == 0);
  EXPECT(MissingLists(Files{{"TLAD\\setup2.xml"}}) == tlad);
  EXPECT(MissingLists(Files{{"TBoGT\\setup2.xml"}}) == tbogt);
  EXPECT(MissingLists(Files{{"TLAD\\setup2.xml", "TBoGT\\setup2.xml"}}) == (tlad | tbogt));
  EXPECT(MissingLists(Files{{"TLAD\\E1_List.txt", "TBoGT\\E2_List.txt"}}) == 0);
  // One installed, the other without its list: the other alone.
  EXPECT(MissingLists(Files{{"TLAD\\setup2.xml", "TLAD\\E1_List.txt", "TBoGT\\setup2.xml"}}) == tbogt);
  EXPECT(MissingLists(Files{{"TLAD\\setup2.xml", "TBoGT\\setup2.xml", "TBoGT\\E2_List.txt"}}) == tlad);
}

// The note: nothing for no such episode; else each folder with the file it
// lacks, and no other episode's file.
void TestMissingListNote() {
  using episode_files::MissingListNote;
  const uint32_t tlad = 1u << kTLAD, tbogt = 1u << kTBoGT;
  EXPECT(MissingListNote(0).empty());
  const std::string one = MissingListNote(tlad);
  EXPECT(Says(one, "The TLAD folder") && Says(one, "E1_List.txt") && !Says(one, "E2_List.txt"));
  const std::string two = MissingListNote(tbogt);
  EXPECT(Says(two, "The TBoGT folder") && Says(two, "E2_List.txt") && !Says(two, "E1_List.txt"));
  const std::string both = MissingListNote(tlad | tbogt);
  EXPECT(Says(both, "The TLAD folder") && Says(both, "E1_List.txt"));
  EXPECT(Says(both, "the TBoGT folder") && Says(both, "E2_List.txt"));
  // Each folder before its own file.
  EXPECT(both.find("TLAD") < both.find("E1_List.txt") && both.find("E1_List.txt") < both.find("TBoGT"));
  EXPECT(both.find("TBoGT") < both.find("E2_List.txt"));
  // What to do, in each.
  for (const std::string& note : {one, two, both}) EXPECT(Says(note, "Complete Edition"));
  // Bits that are no episode's say nothing.
  EXPECT(MissingListNote(1u | 8u).empty());
}

}  // namespace

int main() {
  TestTheGamesNames();
  TestTheMask();
  TestBothFiles();
  TestSetupWithoutTheList();
  TestWithoutSetup();
  TestTheOtherEpisodesList();
  TestTheOtherEpisodesFolder();
  TestNoEpisode();
  TestWhatIsAsked();
  TestMissingLists();
  TestMissingListNote();
  if (g_failures) return 1;
  std::puts("episode_files_test: ok");
  return 0;
}
