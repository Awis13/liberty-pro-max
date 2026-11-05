// Exercise the real recording/encoding path with original shaders, no game data.
#import <Foundation/Foundation.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "runtime/gfx/device.h"

using namespace recomp;
static void Require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "depth output test: %s\n", message); std::exit(1); }
}
static hlsl::Program Compile(const char* source, const char* profile) {
  hlsl::Program p;
  std::string error;
  Require(hlsl::Compile(source, {}, "main", profile, 0, &p, &error), error.c_str());
  return p;
}
int main() {
  char home[] = "/tmp/gtaiv-depth-XXXXXX";
  Require(mkdtemp(home) != nullptr, "temporary home unavailable");
  setenv("HOME", home, 1);
  setenv("CFFIXED_USER_HOME", home, 1);
  @autoreleasepool {
    // Keep the baseline renderer cache in this process's temporary home.
    NSString* root = @(home);
    NSString* cache = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    Require([cache hasPrefix:[root stringByAppendingString:@"/"]], "cache must use the isolated home");
    Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    SDL_Window* window = SDL_CreateWindow("Depth output regression", 32, 32, SDL_WINDOW_METAL | SDL_WINDOW_HIDDEN);
    Require(window != nullptr, SDL_GetError());
    auto dev = gfx::CreateMetalDevice(window);
    Require(dev != nullptr, "Metal device unavailable");
    dev->ResetSwapChain(32, 32, false);
    const auto vs = Compile("float4 main(float4 p : POSITION) : POSITION { return p; }", "vs_3_0");
    const auto ps = Compile(R"(
      struct Out { float4 color : COLOR0; float depth : DEPTH; };
      Out main() { Out o; o.color = float4(1,0,0,1); o.depth = 0.75; return o; }
    )", "ps_3_0");
    Require(ps.writes_depth, "fixture must write shader depth");
    const auto missing_input = Compile(R"(
      struct Out { float4 color : COLOR0; float depth : DEPTH; };
      Out main(float2 uv : TEXCOORD0) {
        Out o; o.color = float4(1,uv.x,0,1); o.depth = 0.75; return o;
      }
    )", "ps_3_0");
    const auto green = Compile("float4 main() : COLOR0 { return float4(0,1,0,1); }", "ps_3_0");
    const auto blue = Compile("float4 main() : COLOR0 { return float4(0,0,1,1); }", "ps_3_0");
    gfx::ShaderDraw draw;
    draw.vs = dev->CreateShader(vs);
    const auto green_id = dev->CreateShader(green), blue_id = dev->CreateShader(blue);
    draw.prim = 4; draw.primitives = 1; draw.vertex_count = 3;
    float vertices[3][4] = {{-1,-1,0.25,1}, {3,-1,0.25,1}, {-1,3,0.25,1}};
    const gfx::VertexInput input{0, 0, 3};
    draw.inputs = &input; draw.input_count = 1;
    draw.streams[0] = {reinterpret_cast<const uint8_t*>(vertices), sizeof vertices, sizeof vertices[0]};
    dev->SetRenderState(22, 1);  // no culling
    dev->SetRenderState(25, 5);  // greater than zero
    dev->SetRenderState(7, 1);   // depth testing and writing when attached
    dev->SetRenderState(14, 1);
    dev->SetRenderState(23, 2);  // LESS
    dev->SetViewport({0, 0, 32, 32, 0, 1});
    const auto depth = dev->CreateDepth(32, 32, gfx::Format::D24S8);
    Require(depth != 0, "depth buffer unavailable");
    auto expect = [&](uint32_t expected, const char* message) {
      uint32_t pixel = 0;
      dev->ReadBack({16,16,17,17}, &pixel, 4, gfx::Format::A8R8G8B8);
      Require(pixel == expected, message);
    };
    // Reuse each shader across depthless -> depth -> depthless passes.
    // Repeat after destruction to exercise ownership of both variant kinds.
    for (int repeat = 0; repeat < 2; repeat++) for (const auto* program : {&ps, &missing_input}) {
      const auto pixel_shader = dev->CreateShader(*program);
      for (bool alpha : {true, false}) for (auto attachment : {gfx::DepthId(0), depth, gfx::DepthId(0)}) {
        dev->SetRenderTarget(0, attachment);
        dev->SetRenderState(15, alpha);
        dev->SetRenderState(24, 0);
        dev->Clear(0, nullptr, 1 | (attachment ? 2 : 0), 0xff000000, 1, 0);
        for (auto& vertex : vertices) vertex[2] = 0.25;
        draw.ps = pixel_shader;
        dev->DrawShader(draw);
        expect(0xffff0000, "shader lost color output across depth attachment changes");
        if (attachment) {
          // Shader depth 0.75 must override vertex depth 0.25: green at
          // 0.5 passes, then blue at 0.9 fails against the updated depth.
          for (auto& vertex : vertices) vertex[2] = 0.5;
          draw.ps = green_id;
          dev->DrawShader(draw);
          expect(0xff00ff00, "shader depth output was lost after rebinding a depth buffer");
          for (auto& vertex : vertices) vertex[2] = 0.9;
          draw.ps = blue_id;
          dev->DrawShader(draw);
          expect(0xff00ff00, "depth testing stopped working");
        } else {
          dev->Clear(0, nullptr, 1, 0xff000000, 1, 0);
          dev->SetRenderState(24, 255);  // GREATER must discard alpha 1
          dev->DrawShader(draw);
          expect(alpha ? 0xff000000 : 0xffff0000, "depthless variant changed alpha-test behavior");
        }
      }
      dev->DestroyShader(pixel_shader);
    }
    // Local shadow maps also use a scalar floating-point color target.
    const auto shadow = dev->CreateTexture(32, 32, 1, gfx::Format::R32F, true);
    Require(shadow != 0, "shadow target unavailable");
    draw.ps = dev->CreateShader(ps);
    dev->SetRenderTarget(shadow, 0);
    dev->SetRenderState(15, 0);
    dev->Clear(0, nullptr, 1, 0xff000000, 1, 0);
    dev->DrawShader(draw);
    // ReadBack expects a BGRA target; convert the scalar target first.
    dev->StretchRect({shadow, 0}, {0,0,32,32}, {0,0}, {0,0,32,32}, false);
    dev->SetRenderTarget(0, 0);
    expect(0xffff0000, "scalar shadow target lost color output");
    // Leave distinct lean shader compilations queued when the device dies.
    // Shutdown must finish them before destroying their maps and mutexes.
    for (int i = 0; i < 32; i++) {
      const auto source = "float4 main() : COLOR0 { return float4(" + std::to_string(i) + ".0/32.0,0,0,1); }";
      dev->CreateShader(Compile(source.c_str(), "ps_3_0"));
    }
    dev.reset();
    SDL_DestroyWindow(window);
    SDL_Quit();
    [NSFileManager.defaultManager removeItemAtPath:root error:nil];
  }
}
