// Diagnostic-only injected probe. See docs/benchmarking.md.
// No renderer policy changes; the same binary samples both builds.
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <libproc.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace {
FILE* frames;
FILE* memory;
mach_timebase_info_data_t scale;
uint64_t Now() {
  return uint64_t(double(mach_absolute_time()) * scale.numer / scale.denom);
}
void* Sample(void*) {
  uint64_t last_flush = 0;
  while (true) {
    const uint64_t sample_start = Now();
    task_vm_info_data_t vm{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const int task_status = task_info(mach_task_self(), TASK_VM_INFO,
                                      reinterpret_cast<task_info_t>(&vm), &count);
    rusage_info_v4 usage{};
    const int usage_status = proc_pid_rusage(getpid(), RUSAGE_INFO_V4,
                                             reinterpret_cast<rusage_info_t*>(&usage));
    vm_statistics64_data_t host{};
    mach_msg_type_number_t host_count = HOST_VM_INFO64_COUNT;
    const int host_status = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                         reinterpret_cast<host_info64_t>(&host), &host_count);
    int pressure = 0;
    size_t pressure_size = sizeof pressure;
    const int pressure_status = sysctlbyname("kern.memorystatus_vm_pressure_level",
                                             &pressure, &pressure_size, nullptr, 0);
    xsw_usage swap{};
    size_t swap_size = sizeof swap;
    const int swap_status = sysctlbyname("vm.swapusage", &swap, &swap_size, nullptr, 0);
    const uint64_t now = Now();
    fprintf(memory, "%llu,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%d,%d,%d,%d,%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)now, task_status, usage_status,
            (unsigned long long)vm.phys_footprint, (unsigned long long)vm.resident_size,
            (unsigned long long)vm.compressed, (unsigned long long)vm.ledger_phys_footprint_peak,
            (unsigned long long)usage.ri_phys_footprint,
            (unsigned long long)usage.ri_lifetime_max_phys_footprint,
            host_status, pressure_status, pressure, swap_status,
            (unsigned long long)swap.xsu_used, (unsigned long long)host.pageouts,
            (unsigned long long)host.swapouts, (unsigned long long)host.compressions,
            (unsigned long long)(now - sample_start));
    if (now - last_flush >= 1000000000) { fflush(memory); last_flush = now; }
    usleep(100000);
  }
}
__attribute__((constructor)) void Start() {
  const char* frame_path = getenv("LIBERTY_MEASURE_FRAMES");
  const char* memory_path = getenv("LIBERTY_MEASURE_MEMORY");
  mach_timebase_info(&scale);
  if (frame_path) {
    frames = fopen(frame_path, "w");
    if (!frames) abort();
    setvbuf(frames, nullptr, _IOFBF, 65536);
    fprintf(frames, "monotonic_ns,interval_ns,previous_probe_ns\n");
  }
  if (memory_path) {
    memory = fopen(memory_path, "w");
    if (!memory) abort();
    fprintf(memory, "monotonic_ns,task_status,rusage_status,footprint_bytes,rss_bytes,compressed_bytes,footprint_peak_bytes,rusage_footprint_bytes,rusage_peak_bytes,host_status,pressure_status,pressure_level,swap_status,system_swap_bytes,system_pageouts,system_swapouts,system_compressions,sample_cost_ns\n");
    pthread_t thread;
    if (pthread_create(&thread, nullptr, Sample, nullptr)) abort();
    pthread_detach(thread);
  }
}
}  // namespace

// Called after PresentFrame's normal event processing in isolated source copies.
extern "C" void LibertyMeasureFrame() {
  if (!frames) return;
  static uint64_t last = 0, last_flush = 0, previous_cost = 0;
  const uint64_t now = Now();
  if (last) fprintf(frames, "%llu,%llu,%llu\n", (unsigned long long)now,
                    (unsigned long long)(now - last), (unsigned long long)previous_cost);
  last = now;
  if (now - last_flush >= 1000000000) { fflush(frames); last_flush = now; }
  previous_cost = Now() - now;
}
