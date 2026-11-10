// An injected constructor must run in the game, never in its cleanup helper.
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

__attribute__((constructor)) static void RecordProcess() {
  const char* path = std::getenv("LIBERTY_MEASURE_TEST");
  if (!path) return;
  FILE* output = std::fopen(path, "a");
  if (!output) std::abort();
  std::fprintf(output, "%d\n", getpid());
  std::fclose(output);
}
