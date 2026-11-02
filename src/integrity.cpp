// Start-up integrity checks of GTAIV.exe 1.0.8.0 (FUN_0048FC30 and the
// gameplay code that repeats them). The game must have been started by
// LaunchGTAIV.exe (its parent process, "SRLC10", whose image must hash to
// one of six known values, "EFC2x"), and the Authenticode
// signatures of
// GTAIV.exe, gta4Browser.exe, gtaEncoder.exe and binkw32.dll must name
// Rockstar Games, Inc. (issuer Entrust Code Signing CA - OVCS1, a fixed
// serial number). The files here are the user's own install, run by the
// recompiled code rather than as signed images, so the check is answered
// as passed; a failure would end the game ("VDS10") or, from gameplay
// code, change it quietly.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

// bool __fastcall VerifySigner(int* token, const wchar_t* file, State* s,
// int* key) (ret 8): with matching tokens, marks the state as checked
// (+0x28 = +0x24) and reports the signer valid.
void f_004906B0(Ctx& c) {
  const uint32_t token = c.ecx, state = Rd32(c.esp + 4), key = Rd32(c.esp + 8);
  bool ok = false;
  if (Rd32(token) == Rd32(state + 0x24) && Rd32(key) == Rd32(state + 0x2C)) {
    Wr32(state + 0x28, Rd32(state + 0x24));
    ok = true;
  }
  c.eax = (c.eax & ~0xFFu) | (ok ? 1u : 0u);
  c.esp += 4 + 8;  // ret 8
}

// bool __stdcall CheckLauncher(State* s) (ret 4): the parent process is
// LaunchGTAIV.exe; marks the state (+8 = +4). There is no launcher here.
void f_0048F900(Ctx& c) {
  const uint32_t state = Rd32(c.esp + 4);
  Wr32(state + 8, Rd32(state + 4));
  c.eax = (c.eax & ~0xFFu) | 1u;
  c.esp += 4 + 4;  // ret 4
}

// FUN_0048FC30 [48FF22, 4900DE): finds the parent process's image
// (NtQueryInformationProcess), reads it and compares its hash with the
// launcher's. Skipped (the block has no lasting effect on the stack or on
// the registers used after it).
void hook_0048FF22(Ctx& c) { (void)c; }

// ---- LaunchGTAIV.exe's part while the game runs
//
// The launcher shares the state above with the game (a file mapping,
// "-=[SMS_GTAIV.exe_SMS]=-", read by FUN_00D49410 into 0x1792E38): +4 its
// window, +0x10 the message it answers. Gameplay code (level and mission
// starts, the player's vehicle) sends it requests in the block at
// 0x1792E04 and relies on what it does: action 100 sets the dword at +0x14
// to 1, action 18 copies a 64-byte table to the address at +0x18, action
// 51 returns +0x14 plus +0x18. Without them the game quietly breaks
// itself (the camera sways as if drunk, cars accelerate on their own).
// Here the launcher's window is a handle no real window has, and
// GtaivSendMessage (main.cpp's GameConfig::send_message) answers for it
// as XLivelessAddon's (GTAmodding) stand-in window does.

namespace {

constexpr uint32_t kLauncherState = 0x01792E38;
constexpr uint32_t kLauncherWindow = 0x7FFF1A00;  // not a handle the runtime gives out
constexpr uint32_t kLauncherMessage = 43858;      // what XLivelessAddon uses

}  // namespace

// The state read from the launcher's mapping (there is none here): then
// its window and message.
void f_00D49410_lifted(Ctx& c);
void f_00D49410(Ctx& c) {
  f_00D49410_lifted(c);
  Wr32(kLauncherState + 0x04, kLauncherWindow);
  Wr32(kLauncherState + 0x10, kLauncherMessage);
}

// The launcher's sequence checks (XLivelessAddon's "messed sequences"):
// passed. bool __cdecl f(void).
void f_00B4FAA0(Ctx& c) {
  c.eax = (c.eax & ~0xFFu) | 1u;
  c.esp += 4;
}
void f_00B4FAC0(Ctx& c) { f_00B4FAA0(c); }
void f_00B4FAD0(Ctx& c) { f_00B4FAA0(c); }
void f_00B4FB00(Ctx& c) { f_00B4FAA0(c); }

}  // namespace recomp::gen

bool GtaivSendMessage(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam,
                      uint32_t* result) {
  using namespace recomp;
  (void)wparam;
  if (hwnd != recomp::gen::kLauncherWindow) return false;
  *result = 1;  // other messages: the launcher's window procedure's default
  if (msg != recomp::gen::kLauncherMessage || !lparam) return true;
  static const uint8_t kTable[64] = {
      0x08, 0, 0, 0, 0x0B, 0, 0, 0, 0xF1, 0, 0, 0, 0x01, 0, 0, 0, 0xF0, 0, 0, 0, 0x02, 0, 0, 0,
      0x07, 0, 0, 0, 0x0C, 0, 0, 0, 0x03, 0, 0, 0, 0xF3, 0, 0, 0, 0x09, 0, 0, 0, 0x06, 0, 0, 0,
      0x0A, 0, 0, 0, 0x05, 0, 0, 0, 0x04, 0, 0, 0, 0xF2, 0, 0, 0};
  const uint32_t action = Rd32(lparam + 0x10), a = Rd32(lparam + 0x14), b = Rd32(lparam + 0x18);
  *result = 0;
  static uint32_t seen = 0;  // a log line per action, the first time
  const uint32_t bit = action == 100 ? 1 : action == 18 ? 2 : action == 51 ? 4 : 8;
  if (!(seen & bit)) {
    seen |= bit;
    std::fprintf(stderr, "gtaiv: launcher request %u (%08X, %08X)\n", action, a, b);
  }
  switch (action) {
    case 100:
      if (a) Wr32(a, 1);
      break;
    case 18:
      if (b) std::memcpy(GPtr(b), kTable, sizeof kTable);
      break;
    case 51:
      *result = a + b;
      break;
  }
  return true;
}
