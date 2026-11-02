// Help while waiting: a thread that waits for its jobs (00460D50) first
// runs those of them that no worker has taken yet, the way a worker would
// (00460AC0), instead of sleeping until a worker gets to them. Main waits
// so for the render list (25-50 jobs a frame on 3 workers) and the
// physics step. GTAIV_JOBS_HELP=0 turns it off.
//
// The game's job pools (gtaDefSched and the others, 0x18E7514[n]) each
// queue their jobs in a ring at pool+8:
//   +0x000 256 job slot pointers   +0x400 CRITICAL_SECTION
//   +0x420 last written index      +0x424 last read index
//   +0x428 queued count            +0x42C semaphore, one count a job
// A worker waits on the semaphore, then pops the ring under the critical
// section. Here a job is taken only if it is at the ring's head and one of
// the waited ones, with the semaphore count taken first (a zero-timeout
// wait), so a worker never finds an empty ring.
//
// A job slot (0xA0 bytes) holds the job's name, its function, 0x94 bytes
// of arguments and, at +0x9C, the event set when it is done. The worker
// copies the slot to its frame, points arguments+0x38 at its scratch
// buffer (the pool's size, worker+0x18) and calls function(&arguments).
// The helper builds the same frame and scratch on its own guest stack, so
// it helps only with room for them (workers' 64 KB stacks do not).

#include <cstdint>
#include <cstdlib>
#include <initializer_list>

#include "runtime/lifted.h"
#include "runtime/runtime.h"

namespace recomp::gen {

namespace {

constexpr uint32_t kPools = 0x18E7514;      // job pool pointers
constexpr uint32_t kPoolCount = 0x018E7534;
constexpr uint32_t kWaitForSingleObject = 0x00D6B1D4;  // IAT entries
constexpr uint32_t kSetEvent = 0x00D6B220;
constexpr uint32_t kLeaveCs = 0x00D6B230;
constexpr uint32_t kEnterCs = 0x00D6B234;
constexpr uint32_t kStackMargin = 0x10000;  // left for the job itself

bool HelpOn() {
  static const bool on = [] {
    const char* e = std::getenv("GTAIV_JOBS_HELP");
    return !e || *e != '0';
  }();
  return on;
}

uint64_t Import(Ctx& c, uint32_t iat, std::initializer_list<uint64_t> args) {
  return CallGuest(c, Rd32(iat), args);
}

// Takes the job at the head of `queue` if it is one of `jobs`; 0 if none.
uint32_t TakeHead(Ctx& c, uint32_t queue, uint32_t count, uint32_t jobs) {
  if (Rd32(queue + 0x428) == 0) return 0;  // racy peek, rechecked below
  const uint32_t cs = queue + 0x400;
  const bool locked = Rd32(cs) != 0;  // as the game: no lock before it is made
  if (locked) Import(c, kEnterCs, {cs});
  uint32_t job = 0;
  if (Rd32(queue + 0x428) != 0) {
    const uint32_t next = (Rd32(queue + 0x424) + 1) & 0xFF;
    const uint32_t head = Rd32(queue + next * 4);
    bool ours = false;
    for (uint32_t i = 0; i < count && !ours; i++) ours = Rd32(jobs + i * 4) == head;
    if (ours && Import(c, kWaitForSingleObject, {Rd32(queue + 0x42C), 0}) == 0) {
      Wr32(queue + 0x424, next);
      Wr32(queue + 0x428, Rd32(queue + 0x428) - 1);
      job = head;
    }
  }
  if (locked) Import(c, kLeaveCs, {cs});
  return job;
}

void Run(Ctx& c, uint32_t pool, uint32_t job) {
  const uint32_t scratch_size = Rd32(Rd32(pool) + 0x18);  // first worker's
  const uint32_t esp = c.esp;
  c.esp = (esp - scratch_size) & ~0xFu;
  const uint32_t scratch = c.esp;
  c.esp = (c.esp - 0xA0) & ~0xFu;
  const uint32_t frame = c.esp;  // the worker's +0x20: the slot's copy
  for (uint32_t i = 0; i < 0x9C; i += 4) Wr32(frame + i, Rd32(job + i));
  Wr32(frame + 0x40, scratch);  // arguments+0x38
  CallGuest(c, Rd32(frame + 4), {frame + 8});
  c.esp = esp;
  Import(c, kSetEvent, {Rd32(job + 0x9C)});
}

}  // namespace

// Before a wait for `count` jobs (cdecl: count, jobs, wait all).
void HelpWithJobs(Ctx& c) {
  if (!HelpOn()) return;
  const uint32_t count = Rd32(c.esp + 4), jobs = Rd32(c.esp + 8);
  if (count == 0) return;
  const uint32_t pools = Rd32(kPoolCount);
  Thread* t = CurrentThread();
  if (!t || pools == 0) return;
  for (bool took = true; took;) {
    took = false;
    for (uint32_t p = 0; p < pools; p++) {
      const uint32_t pool = Rd32(kPools + p * 4);
      if (!pool) continue;
      const uint32_t need = Rd32(Rd32(pool) + 0x18) + 0xA0 + kStackMargin;
      if (c.esp - t->stack_lo < need) continue;
      if (const uint32_t job = TakeHead(c, pool + 8, count, jobs)) {
        Run(c, pool, job);
        took = true;
      }
    }
  }
}

}  // namespace recomp::gen
