#pragma once
// GTA IV 1.0.8.0 baseline, revision 4. Shader pairs come from FXC technique
// passes. These common states cover the base game's G-buffer, transparent
// HDR geometry, and additive HDR lighting. Gameplay records other states.
#include <cctype>
#include <string>
#include <vector>
namespace recomp::gfx {
struct MetalBaselinePass {
  MTLPixelFormat color[4];
  MTLPixelFormat depth;
  bool blend;
  MTLBlendFactor src, dst;
  bool effect_states = true, shader_targets = false;
  uint8_t write_mask = 255;  // Optional renderer override for attachment zero.
};
inline constexpr MetalBaselinePass kMetalBaseline[] = {
  {{MTLPixelFormatBGRA8Unorm, MTLPixelFormatBGRA8Unorm, MTLPixelFormatBGRA8Unorm, MTLPixelFormatR16Float},
    MTLPixelFormatDepth32Float_Stencil8, false, MTLBlendFactorOne, MTLBlendFactorZero},
  {{MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float},
    MTLPixelFormatDepth32Float_Stencil8, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha},
  {{MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float},
    MTLPixelFormatDepth32Float_Stencil8, true, MTLBlendFactorOne, MTLBlendFactorOne},
};
// RAGE technique families identify the pass purpose; target formats are supplied
// by the renderer rather than the FXC file. Keep a finite set of candidates for
// each family instead of applying every format/blend to every shader pair.
inline std::vector<MetalBaselinePass> MetalBaselinePasses(std::string file, std::string technique, uint32_t outputs) {
  for (char& c : file) c = char(std::tolower(static_cast<unsigned char>(c)));
  for (char& c : technique) c = char(std::tolower(static_cast<unsigned char>(c)));
  auto has = [&](const char* s) { return technique.find(s) != std::string::npos; };
  std::vector<MetalBaselinePass> passes;
  auto add = [&](MTLPixelFormat format, bool depth, bool blend = false) {
    MetalBaselinePass p{{format, MTLPixelFormatInvalid, MTLPixelFormatInvalid, MTLPixelFormatInvalid},
      depth ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid, blend,
      blend ? MTLBlendFactorSourceAlpha : MTLBlendFactorOne,
      blend ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorZero};
    // Extra attachments are legal even when the shader doesn't write them.
    // The renderer's pipeline key describes the whole render pass.
    if (outputs & ~1u) for (int i = 1; i < 4; i++) p.color[i] = format;
    passes.push_back(p);
  };
  if (has("deferred")) {
    passes.push_back(kMetalBaseline[0]);
    auto alpha = kMetalBaseline[0]; alpha.blend = true;
    alpha.src = MTLBlendFactorSourceAlpha; alpha.dst = MTLBlendFactorOneMinusSourceAlpha;
    passes.push_back(alpha);
  } else if (technique.starts_with("unlit") || has("impostershadow")) {
    add(MTLPixelFormatR32Float, true);
  } else if (has("reflection") || has("lightweight") || technique.starts_with("wd_") || has("paraboloid")) {
    add(MTLPixelFormatBGRA8Unorm, true);
    add(MTLPixelFormatBGRA8Unorm, true, true);
    add(MTLPixelFormatR32Float, true);
  } else if (file == "rage_postfx" || file == "rage_im" || file == "gta_im" || file == "rage_bink" ||
             file == "rage_fastmipmap" || file == "cascadeconvert" || file == "shadowsmartblit") {
    for (auto format : {MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA16Float, MTLPixelFormatR32Float, MTLPixelFormatR16Float}) {
      add(format, false); add(format, true);
      if (file == "rage_im" || file == "gta_im" || file == "rage_bink") { add(format, false, true); add(format, true, true); }
    }
  } else if (has("draw") || file == "deferred_lighting" || file == "water" || file.starts_with("gpuptfx") || file.starts_with("rmptfx")) {
    add(MTLPixelFormatRGBA16Float, true);
    add(MTLPixelFormatRGBA16Float, true, true);
    auto additive = passes.back(); additive.src = additive.dst = MTLBlendFactorOne; passes.push_back(additive);
    add(MTLPixelFormatR16Float, true, true);
  } else {
    passes.assign(std::begin(kMetalBaseline), std::end(kMetalBaseline));
  }
  // Renderer overrides are family-specific: applying these to every Draw
  // technique would retain tens of thousands of mostly unused startup states.
  const bool particles = file.starts_with("gpuptfx") || file.starts_with("rmptfx") || file.starts_with("gta_rmptfx");
  auto override = [&](MTLPixelFormat format, bool blend, MTLBlendFactor src, MTLBlendFactor dst,
                      uint8_t mask = 255, bool depth = true) {
    MetalBaselinePass p{{format, MTLPixelFormatInvalid, MTLPixelFormatInvalid, MTLPixelFormatInvalid},
      depth ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid, blend, src, dst, false};
    p.write_mask = mask; passes.push_back(p);
  };
  if (has("reflection"))
    override(MTLPixelFormatRGBA16Float, false, MTLBlendFactorOne, MTLBlendFactorZero);
  if (technique.starts_with("wd_"))
    override(MTLPixelFormatR32Float, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha);
  if (file == "rage_perlinnoise")
    override(MTLPixelFormatBGRA8Unorm, false, MTLBlendFactorOne, MTLBlendFactorZero, 255, false);
  if (file == "geometry_stencil") {
    passes.clear();
    override(MTLPixelFormatBGRA8Unorm, true, MTLBlendFactorOne, MTLBlendFactorOne);
    override(MTLPixelFormatBGRA8Unorm, true, MTLBlendFactorOne, MTLBlendFactorOne, MTLColorWriteMaskNone);
    return passes;
  }
  if (particles || file == "gta_trees" || file.find("atmoscatt") != std::string::npos ||
      file == "gta_im" || file == "rage_im" || file == "deferred_lighting") {
    for (auto format : {MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA16Float}) {
      override(format, false, MTLBlendFactorOne, MTLBlendFactorZero);
      override(format, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha);
      override(format, true, MTLBlendFactorOne, MTLBlendFactorOne);
      if (particles) override(format, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOne);
    }
    if (particles) override(MTLPixelFormatR32Float, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha);
  }
  if (file.starts_with("gta_emissive"))
    override(MTLPixelFormatRGBA16Float, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOne);
  if (file == "gta_im" || file == "rage_im") {
    override(MTLPixelFormatBGRA8Unorm, true, MTLBlendFactorOne, MTLBlendFactorZero, MTLColorWriteMaskAlpha);
    override(MTLPixelFormatBGRA8Unorm, true, MTLBlendFactorDestinationColor, MTLBlendFactorOneMinusDestinationColor, 14);
    override(MTLPixelFormatBGRA8Unorm, true, MTLBlendFactorDestinationColor, MTLBlendFactorOne, 14);
  }
  if (file == "deferred_lighting") {
    override(MTLPixelFormatBGRA8Unorm, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha, 10);
    override(MTLPixelFormatRGBA16Float, false, MTLBlendFactorOne, MTLBlendFactorZero, 255, false);
    override(MTLPixelFormatRGBA16Float, true, MTLBlendFactorSourceAlpha, MTLBlendFactorOneMinusSourceAlpha, 255, false);
  }
  if (file == "watertex") override(MTLPixelFormatBGRA8Unorm, false, MTLBlendFactorOne, MTLBlendFactorZero, 255, false);
  // Draw code can override effect state and reuse pairs in other passes.
  // Retain the original common candidates as well as the declared-state ones.
  for (auto p : kMetalBaseline) {
    p.effect_states = false; p.shader_targets = true; passes.push_back(p);
  }
  return passes;
}
inline MTLBlendFactor MetalEffectBlend(uint32_t value, bool alpha) {
  switch (value) {
    case 1: return MTLBlendFactorZero;
    case 2: return MTLBlendFactorOne;
    case 3: return MTLBlendFactorSourceColor;
    case 4: return MTLBlendFactorOneMinusSourceColor;
    case 5: return MTLBlendFactorSourceAlpha;
    case 6: return MTLBlendFactorOneMinusSourceAlpha;
    case 7: return alpha ? MTLBlendFactorDestinationAlpha : MTLBlendFactorOne;
    case 8: return alpha ? MTLBlendFactorOneMinusDestinationAlpha : MTLBlendFactorZero;
    case 9: return MTLBlendFactorDestinationColor;
    case 10: return MTLBlendFactorOneMinusDestinationColor;
    case 11: return MTLBlendFactorSourceAlphaSaturated;
    case 14: return MTLBlendFactorBlendColor;
    case 15: return MTLBlendFactorOneMinusBlendColor;
    default: return MTLBlendFactorOne;
  }
}
inline uint8_t MetalEffectMask(uint32_t value) {
  return uint8_t(((value & 1) << 3) | ((value & 2) << 1) | ((value & 4) >> 1) | ((value & 8) >> 3));
}
}  // namespace recomp::gfx
