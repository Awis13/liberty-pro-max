// The game's own command line, put together by main.cpp in the game's
// process behind a launcher (the Mac app's, which starts the game with
// --no-launcher; the iPad's, iPhone's and Apple TV's). Plain C++ (no AppKit
// or UIKit): tests/game_args_test.cpp checks it.
//
// How GTA IV 1.0.8.0 reads its arguments (verified in the lifted code):
// - The CRT splits GetCommandLineA by the Windows rules (parse_cmdline,
//   00CF4917; then __cwild, 00CFDEA2, setargv.obj: an argument with '*' or
//   '?' that matches files becomes their names).
// - FUN_00455D60 then reads commandline.txt from the game folder, but
//   only when there is no argument at all (argc <= 1). With an argument
//   the file is never opened. (A first argument "@name" reads that file
//   instead, its arguments first and the rest of the command line after.)
// - FUN_00455BB0 splits such a file: spaces, tabs, CR and LF separate
//   arguments; an argument that starts with '"' runs to the next '"' (no
//   escapes; a missing one ends it at the end of the file); one that starts
//   with ';', '#' or "//" is a comment to the end of its line (only at the
//   start of an argument: "-a;b" is one argument); one that starts with '@'
//   is replaced by that file's arguments (a file that cannot be read gives
//   none). The file is read into a 512 KB buffer, at most 0x7FFE arguments.
//   Its bytes are taken as they are, no text encoding: an ANSI en dash
//   (0x96) starts an option like '-' does (FUN_00461DB0), and a UTF-16 file
//   ends at its first zero byte, one argument of junk the game ignores.
// - FUN_00461DB0 (sysParam) goes through the arguments in order: a name
//   (case-insensitive) after '-' takes its value from "-name=value" or the
//   next argument that is not itself a "-name". Every occurrence stores its
//   value over the one before: of a repeated option, the last one counts.
//   (-availablevidmem: atoi of the value times 1 MB, 00402A65.)
//
// Behind a launcher the game always has arguments, so it no longer reads
// commandline.txt itself: main.cpp reads it the same way, byte for byte, and
// puts its arguments on the command line. (gtaiv run by hand without
// --no-launcher passes only what follows "--", as before: with nothing
// there the game reads the file itself, with something it does not.) The
// order, each later one winning:
//   1. the port's defaults (DefaultGameArgs): no settings restrictions and
//      the video memory the graphics menu budgets with;
//   2. the player's commandline.txt: their choice over the defaults (a
//      different -availablevidmem, say);
//   3. what the launcher sets itself: -width and -height, the resolution
//      chosen in the iPad's and iPhone's launcher (and given to the game as
//      its display modes) or the Apple TV's fixed 1920x1080; a file's -width
//      would otherwise disagree with them;
//   4. the arguments given after "--" by whoever started the app by hand.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gtaiv {

// The arguments of a response file's text, split as the game splits
// commandline.txt (above). An unquoted argument "@name" is replaced by
// the arguments of read(name)'s text, 8 deep at most (two includes in one
// file deliberately differ from the game: it reads every include of a file
// into the same place, so the second overwrites the first; here both are
// kept); read reads at most
// max_bytes (what is left of the game's buffer) and returns false for a
// file it cannot read (it is not called if empty). A UTF-8 byte order mark
// at the start of a text is skipped (the game would take it as part of the
// first argument, which it then ignores).
using ReadFile = std::function<bool(const std::string& name, size_t max_bytes, std::string* text)>;
std::vector<std::string> SplitResponseText(std::string_view text, const ReadFile& read = {});

// A file in the game folder by its path there ('/' or '\' between the
// parts), each part matched without regard to case as the game's own file
// lookups are: the path as given where the volume finds it so, else with
// the names on disk; empty if there is no such file, or the path is
// absolute, has a drive or a ".." part.
std::string FindGamePath(const std::string& game_dir, const std::string& name);

// <game_dir>/commandline.txt's arguments; none if there is no such file.
// Its "@name" includes are files in the game folder too (FindGamePath).
std::vector<std::string> ReadCommandLineFile(const std::string& game_dir);

// The port's defaults: -norestrictions (every graphics setting can be
// chosen) and -availablevidmem with GTAIV_VIDMEM (MB) or 2047, the most a
// signed 32-bit byte count holds (without it the game budgets for 512 MB
// and keeps textures at medium). -nomemrestrict is not among them: the
// port's NoMemRestrict fix (GTAIV_NO_MEM_RESTRICT, on by default) does what
// it does, and given here it would stay on with that option turned off.
std::vector<std::string> DefaultGameArgs();

// The game's arguments behind a launcher: the defaults, then the player's
// commandline.txt in game_dir, then `last` (the launcher's own arguments,
// then any given after "--").
std::vector<std::string> LauncherGameArgs(const std::string& game_dir,
                                          const std::vector<std::string>& last);

// One argument as the game's CRT splits it back (quoted only when it has
// to be: empty, or with a space, tab, newline or '"').
std::string QuoteArg(const std::string& arg);

// The arguments as one command line (QuoteArg each, a space between).
std::string JoinArgs(const std::vector<std::string>& args);

}  // namespace gtaiv
