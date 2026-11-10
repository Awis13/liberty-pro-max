#import <CommonCrypto/CommonDigest.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include "shader_profile_mac.h"
#include "metal_baseline_mac.h"
#include "metal_effects_mac.h"

namespace {
using namespace recomp;
using namespace recomp::gfx;
namespace fs = std::filesystem;
std::string Hash(const std::string& s) {
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(s.data(), CC_LONG(s.size()), digest);
  std::string h;
  for (unsigned char b : digest) { char x[3]; std::snprintf(x, sizeof x, "%02x", b); h += x; }
  return h;
}
std::string Read(const fs::path& p) {
  // libc++ file streams can split even istream::read into small buffered reads
  // and read ahead when seeking. Use the opened file's size and direct reads.
  int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) throw std::runtime_error("Cannot read " + p.string() + ". Check the game installation and permissions.");
  struct Close { int fd; ~Close() { close(fd); } } closer{fd};
  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0)
    throw std::runtime_error("Cannot size " + p.string());
  std::string bytes(size_t(info.st_size), '\0');
  size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t n = read(fd, bytes.data() + offset, bytes.size() - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) throw std::runtime_error("Cannot completely read " + p.string());
    offset += size_t(n);
  }
  return bytes;
}
std::vector<fs::path> Effects(const std::string& game) {
  fs::path base = fs::path(game) / "common/shaders";
  if (!fs::is_directory(base / "win32_30"))
    throw std::runtime_error("Missing common/shaders/win32_30. Choose a complete GTA IV 1.0.8.0 installation.");
  std::vector<fs::path> files;
  for (const auto& d : fs::directory_iterator(base)) {
    if (!d.is_directory() || !d.path().filename().string().starts_with("win32_30")) continue;
    for (const auto& f : fs::directory_iterator(d))
      if (f.is_regular_file() && f.path().extension() == ".fxc") files.push_back(f.path());
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) throw std::runtime_error("No .fxc shader files found. Restore common/shaders from the game installation.");
  return files;
}
struct Reader {
  std::string bytes;
  size_t pos = 0;
  uint8_t Byte() { if (pos >= bytes.size()) throw std::runtime_error("Truncated FXC effect"); return uint8_t(bytes[pos++]); }
  uint16_t Word() { uint16_t a = Byte(); return a | uint16_t(Byte()) << 8; }
  uint32_t Dword() { uint32_t a = Word(); return a | uint32_t(Word()) << 16; }
  std::string String() { auto s = Take(Byte()); if (!s.empty() && !s.back()) s.pop_back(); return s; }
  void Parameters() {
    unsigned count = Byte();
    for (unsigned i = 0; i < count; i++) {
      Take(2); String(); String();
      unsigned annotations = Byte();
      for (unsigned j = 0; j < annotations; j++) {
        String(); unsigned type = Byte();
        if (type == 2) String(); else Take(4);
      }
      unsigned values = Byte(); Take(size_t(values) * 4);
    }
  }
  std::string Take(size_t n) {
    if (n > bytes.size() - pos) throw std::runtime_error("Truncated FXC fragment");
    std::string s = bytes.substr(pos, n); pos += n; return s;
  }
};
struct EffectPass {
  size_t vertex, pixel;
  std::string technique;
  std::map<uint32_t, uint32_t> states;
};
struct EffectWork {
  std::string name;
  std::vector<size_t> vertex, pixel;
  std::vector<EffectPass> passes;
};

struct InputShader {
  std::string name;
  hlsl::Program program;
  uint64_t hash = 0;
};
struct Discovery {
  std::string game;
  MetalPreparationInputs inputs;
  std::vector<InputShader> shaders;
  std::vector<EffectWork> effects;
  std::map<std::string, size_t> token_index;
  std::map<uint64_t, size_t> shader_index;
  std::set<std::string> planned;
  void Fail(const std::string& name, const std::string& message) { inputs.errors.emplace_back(name, message); }
  void Plan(const MetalRecipe& recipe) {
    std::string bytes(reinterpret_cast<const char*>(&recipe), sizeof recipe);
    if (planned.insert(bytes).second) inputs.recipes.push_back(recipe);
  }
  void Discover() {
    auto files = Effects(game);
    std::vector<std::pair<std::string, std::string>> sources;
    for (const auto& file : files) sources.emplace_back(fs::relative(file, game).string(), Read(file));
    auto embedded = MetalEmbeddedEffects(Read(fs::path(game) / "GTAIV.exe"));
    sources.insert(sources.end(), std::make_move_iterator(embedded.begin()), std::make_move_iterator(embedded.end()));
    for (const auto& [file, bytes] : sources) {

      EffectWork effect; effect.name = fs::path(file).stem().string();
      try {
        Reader r{bytes};
        if (r.Take(4) != "rgxa") throw std::runtime_error("Expected an rgxa FXC effect");
        for (unsigned stage = 0; stage < 2; stage++) {
          unsigned count = r.Byte();
          for (unsigned i = 0; i < count; i++) {
            unsigned vars = r.Byte();
            for (unsigned v = 0; v < vars; v++) { r.Take(4); unsigned n = r.Byte(); r.Take(n); }
            unsigned n = r.Word(); r.Word();
            std::string blob = r.Take(n);
            auto& stage_entries = stage ? effect.pixel : effect.vertex;
            stage_entries.push_back(SIZE_MAX);
            if (!n) continue;
            auto [it, added] = token_index.emplace(Hash(blob), shaders.size());
            if (added) {
              InputShader work;
              work.name = file + (stage ? ":ps" : ":vs") + std::to_string(i);
              try {
                if (n % 4 || n < 8) throw std::runtime_error("Invalid shader token count");
                std::vector<uint32_t> tokens(n / 4); std::memcpy(tokens.data(), blob.data(), n);
                auto parsed = hlsl::ReadBytecode([&](size_t k) { return tokens[k]; }, tokens.size());
                if (parsed.size() != tokens.size()) throw std::runtime_error("Shader is missing its END token");
                std::string error;
                if (!hlsl::TranslateBytecode(tokens.data(), tokens.size(), &work.program, &error)) throw std::runtime_error(error);
                work.hash = MetalShaderIdentity(work.program);
                shader_index[work.hash] = shaders.size();
              } catch (const std::exception& e) { Fail(work.name, e.what()); }
              shaders.push_back(std::move(work));
            }
            stage_entries.back() = it->second;
          }
        }
        // FXC variables precede the technique table. Each technique pass
        // names its exact VS/PS fragment slots; pixel slot zero is empty.
        r.Parameters(); r.Parameters();
        unsigned techniques = r.Byte();
        for (unsigned t = 0; t < techniques; t++) {
          auto technique = r.String(); unsigned passes = r.Byte();
          for (unsigned pass = 0; pass < passes; pass++) {
            unsigned vs = r.Byte(), ps = r.Byte(), values = r.Byte();
            std::map<uint32_t, uint32_t> states;
            for (unsigned i = 0; i < values; i++) { auto key = r.Dword(); states[key] = r.Dword(); }
            if (vs >= effect.vertex.size() || ps >= effect.pixel.size())
              throw std::runtime_error("FXC pass references an invalid shader slot");
            if (effect.vertex[vs] != SIZE_MAX && effect.pixel[ps] != SIZE_MAX)
              effect.passes.push_back({effect.vertex[vs], effect.pixel[ps], technique, std::move(states)});
          }
        }
        // Some effects append per-fragment names after the technique table.
        if (r.pos < r.bytes.size())
          for (size_t n = 0; n < effect.vertex.size() + effect.pixel.size() - 1; n++) r.String();
        if (r.pos != r.bytes.size()) throw std::runtime_error("Unexpected data after FXC techniques");
        effects.push_back(std::move(effect));
      } catch (const std::exception& e) { Fail(file, e.what()); }
    }
  }
  void Baseline() {
    // The stencil shader is also bound over ordinary world geometry. Derive
    // those pairs from named passes, including the executable's effects,
    // rather than expanding all vertex/pixel combinations or using recordings.
    std::vector<EffectWork> stencil_overrides;
    for (const auto& geometry : effects) if (geometry.name == "gta_default") {
      EffectWork extra; extra.name = "geometry_stencil";
      for (const auto& sky : effects) if (sky.name == "rage_atmoscatt_clouds")
        for (const auto& stencil : sky.passes) if (stencil.technique == "draw_stencil")
          for (const auto& draw : geometry.passes) if (draw.technique == "draw" || draw.technique == "drawskinned")
            if (!hlsl::VaryingMask(shaders[stencil.pixel].program))
              extra.passes.push_back({draw.vertex, stencil.pixel, "geometry_stencil", {}});
      stencil_overrides.push_back(std::move(extra));
    }
    effects.insert(effects.end(), std::make_move_iterator(stencil_overrides.begin()),
                   std::make_move_iterator(stencil_overrides.end()));
    // Declared technique/pass pairs, deduplicated across platform directories.
    // Variant translation is counted explicitly in the shader phase.
    for (const auto& effect : effects) for (const auto& declared : effect.passes) {
      const auto vi = declared.vertex, pi = declared.pixel;

      uint64_t vh = shaders[vi].hash, ph = shaders[pi].hash;
      if (!vh || !ph) continue;
      const uint32_t written = hlsl::VaryingMask(shaders[vi].program) & hlsl::VaryingMask(shaders[pi].program);
      if (written != hlsl::VaryingMask(shaders[pi].program)) {
        hlsl::Program variant = shaders[pi].program;
        variant.glsl = hlsl::GlslWithDefaultInputs(variant, written);
        ph = MetalShaderIdentity(variant);
        if (!shader_index.contains(ph)) {
          InputShader work;
          work.name = shaders[pi].name + " (default inputs " + std::to_string(written) + ")";
          work.program = std::move(variant); work.hash = ph;
          shader_index[ph] = shaders.size(); shaders.push_back(std::move(work));
        }
      }
      for (const auto& pass : MetalBaselinePasses(effect.name, declared.technique, shaders[pi].program.color_outputs)) {
        if (!pass.depth && shaders[pi].program.writes_depth) continue;
        MetalRecipe r{}; r.vs = vh; r.ps = ph;
        r.key.depth = pass.depth;
        r.key.stencil = pass.depth == MTLPixelFormatDepth32Float_Stencil8 ? pass.depth : MTLPixelFormatInvalid;
        auto value = [&](uint32_t key, uint32_t fallback) { auto it = declared.states.find(key); return !pass.effect_states || it == declared.states.end() ? fallback : it->second; };
        // RAGE's FXC state slots are not D3DRS indices. Slots 4/5, 10,
        // 19..22 and 23 carry blend factors, enable, write masks and blend op.
        r.key.blend = value(10, pass.blend) != 0;
        r.key.src = r.key.src_a = MTLBlendFactorOne;
        r.key.dst = r.key.dst_a = MTLBlendFactorZero;
        r.key.op = r.key.op_a = MTLBlendOperationAdd;
        if (r.key.blend) {
          auto src = value(4, uint32_t(pass.src) + 1), dst = value(5, uint32_t(pass.dst) + 1);
          if (src == 12) { src = 5; dst = 6; } else if (src == 13) { src = 6; dst = 5; }
          bool alpha = pass.color[0] == MTLPixelFormatBGRA8Unorm || pass.color[0] == MTLPixelFormatRGBA16Float;
          r.key.src = r.key.src_a = MetalEffectBlend(src, alpha);
          r.key.dst = r.key.dst_a = MetalEffectBlend(dst, alpha);
          auto op = value(23, 1);
          r.key.op = r.key.op_a = op >= 2 && op <= 5 ? uint8_t(op - 1) : uint8_t(MTLBlendOperationAdd);
        }
        const auto& pixel = shaders[pi].program;
        for (int i = 0; i < 4; i++) if (pass.color[i] && (!pass.shader_targets || (pixel.color_outputs & (1u << i)))) {
          r.key.color[i] = pass.color[i];
          r.key.mask[i] = i == 0 && pass.write_mask != 255 ? pass.write_mask : MetalEffectMask(value(19 + i, 15));
        }
        auto plan = [&](MetalRecipe recipe) {
          Plan(recipe);
          // Match alpha testing disabled without expanding shader pairings.
          if (pixel.stage == hlsl::Stage::Pixel) { recipe.key.lean = 1; Plan(recipe); }
        };
        plan(r);
        // Common render-state overrides use a handful of complete mask profiles,
        // not the Cartesian product of per-attachment masks. Preserve the R16F
        // fourth G-buffer attachment and its full write mask.
        if (!pass.shader_targets && r.key.color[0] == MTLPixelFormatBGRA8Unorm &&
            r.key.color[1] == MTLPixelFormatBGRA8Unorm && r.key.color[2] == MTLPixelFormatBGRA8Unorm &&
            r.key.color[3] == MTLPixelFormatR16Float) {
          for (const auto& masks : {std::array<uint8_t, 4>{14, 14, 14, 15}, {14, 15, 15, 15},
                                    {1, 0, 0, 15}, {14, 0, 12, 15}}) {
            auto masked = r;
            std::copy(masks.begin(), masks.end(), masked.key.mask);
            plan(masked);
          }
        } else if (!pass.shader_targets && r.key.color[0] && !r.key.color[1] && r.key.depth &&
                   (effect.name == "gta_trees" || effect.name == "deferred_lighting" ||
                    effect.name.starts_with("gta_hair_sorted_alpha"))) {
          // Depth-only draws with a bound color target keep its format but mask
          // out writes. Scalar shadow targets also use red/alpha-only writes.
          auto masked = r;
          if (effect.name == "gta_trees" || effect.name == "deferred_lighting") {
            masked.key.mask[0] = MTLColorWriteMaskNone; plan(masked);
          }
          if (r.key.color[0] == MTLPixelFormatR32Float) {
            masked.key.mask[0] = MTLColorWriteMaskAlpha; plan(masked);
          }
        }
      }
    }

  }
};
}  // namespace

recomp::gfx::MetalPreparationProfile GtaivMetalPreparationProfile() {
  recomp::gfx::MetalPreparationProfile profile;
  profile.cache_namespace = "gtaiv";
  profile.identity = GTAIV_METAL_PROFILE_ID;
  profile.content_identity = [](const std::string& game) {
    std::string content = Hash(Read(fs::path(game) / "GTAIV.exe"));
    for (const auto& file : Effects(game)) content += fs::relative(file, game).string() + Hash(Read(file));
    return content;
  };
  profile.discover = [](const std::string& game) {
    Discovery discovery; discovery.game = game;
    discovery.Discover(); discovery.Baseline();
    for (auto& shader : discovery.shaders)
      if (shader.hash) discovery.inputs.shaders.push_back({std::move(shader.name), std::move(shader.program)});
    return std::move(discovery.inputs);
  };
  return profile;
}
