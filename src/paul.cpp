// The PAUL layer of GTAIV.exe 1.0.8.0 (Rockstar's copy protection, with
// Paul.dll). At start it copies a 0x77000-byte block of x86 code from
// .data into fresh memory, relocates it and calls it through a table of 63
// entry points; the game's local file device (fiDeviceLocal, 0x4701A0..)
// opens, reads and seeks files through wrappers around that table, which
// scramble the data when called from outside the expected code range, and
// it now and then checks a file's signature, corrupting state on failure.
// Code built at run time cannot be lifted, and the checks are meaningless
// for the user's own install, so the wrappers are replaced here by the
// Win32 calls they stand for (all cdecl, the caller pops).

#include <cstdint>

#include "runtime/hle/hle.h"
#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

uint32_t StackArg(const Ctx& c, int i) { return Rd32(c.esp + 4 + 4 * uint32_t(i)); }

// kernel32!name(args from the caller's frame), its result in eax.
void Forward(Ctx& c, const char* name, int nargs) {
  uint64_t a[7] = {};
  for (int i = 0; i < nargs; i++) a[i] = StackArg(c, i);
  const gaddr fn = hle::ThunkFor("kernel32.dll", name);
  uint64_t r = 0;
  switch (nargs) {
    case 1: r = CallGuest(c, fn, {a[0]}); break;
    case 2: r = CallGuest(c, fn, {a[0], a[1]}); break;
    case 4: r = CallGuest(c, fn, {a[0], a[1], a[2], a[3]}); break;
    case 5: r = CallGuest(c, fn, {a[0], a[1], a[2], a[3], a[4]}); break;
    case 7: r = CallGuest(c, fn, {a[0], a[1], a[2], a[3], a[4], a[5], a[6]}); break;
  }
  c.eax = uint32_t(r);
  c.esp += 4;  // ret (cdecl)
}

void Return(Ctx& c, uint32_t v) {
  c.eax = v;
  c.esp += 4;
}

}  // namespace

// Initialisation (called once from the game's start-up, 0x455EE0): 1 = ok.
void f_00D18223(Ctx& c) { Return(c, 1); }
// Loads Paul.dll and calls into the relocated block: 1 = ok.
void f_00D176A0(Ctx& c) { Return(c, 1); }
// A command-line switch handed to the block.
void f_00D16A9F(Ctx& c) { Return(c, 0); }

// File signature checks (name): 1 = valid.
void f_00D17BFF(Ctx& c) { Return(c, 1); }
void f_00D17BD7(Ctx& c) { Return(c, 1); }

void f_00D17CD3(Ctx& c) { Forward(c, "CreateFileA", 7); }
void f_00D17E6B(Ctx& c) { Forward(c, "CloseHandle", 1); }
void f_00D17E92(Ctx& c) { Forward(c, "SetFilePointer", 4); }
void f_00D17D38(Ctx& c) { Forward(c, "ReadFile", 5); }
void f_00D17EBA(Ctx& c) { Forward(c, "GetFileSize", 2); }

}  // namespace recomp::gen
