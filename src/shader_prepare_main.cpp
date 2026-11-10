#include "shader_profile_mac.h"
int main(int argc, char** argv) {
  int result = recomp::gfx::MetalPreparationCommand(argc, argv, GtaivMetalPreparationProfile());
  return result < 0 ? 2 : result;
}
