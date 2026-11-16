// The natural colour filter of a timecycle line (src/colour_filter.h).

#include <cstdio>

#include "colour_filter.h"

int main() {
  namespace cf = gtaiv_colour_filter;
  cf::Line l{{109, 120, 122}, {0.76f, 0.7f}, {0.8f, 0.78f}};
  cf::Natural(&l);
  const bool ok = l.cc[0] == 128 && l.cc[1] == 128 && l.cc[2] == 128 && l.desaturation[0] == 0.76f &&
                  l.desaturation[1] == 0.7f && l.gamma[0] == 0.8f && l.gamma[1] == 0.78f;
  std::printf("%s the colour correction neutral, the desaturation and the gamma the game's\n", ok ? "ok" : "FAIL");
  std::puts(ok ? "colour_filter: ok" : "colour_filter: FAILED");
  return ok ? 0 : 1;
}
