#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "launcher_shader_preparation.h"
#include "runtime/gfx/metal_cache.h"
#include <cstdio>
#include <cstring>

int main() { @autoreleasepool {
  if (!MTLCreateSystemDefaultDevice()) return 77;
  NSString* dir = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  LpmShaderPreparation* model = [[LpmShaderPreparation alloc] initWithCacheDirectory:dir];
  int failures = 0;
  auto check = [&](bool ok, const char* msg) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",msg); failures++; } };
  auto wait = [&] {
    NSDate* end = [NSDate dateWithTimeIntervalSinceNow:20];
    while ((model.checking || model.running) && end.timeIntervalSinceNow > 0)
      [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    check(!model.checking && !model.running,"background work returns before timeout");
  };
  [model refresh];
  check(model.checking && !model.canCompile,"inspection disables compilation until its result");
  wait();
  check(model.state == recomp::gfx::PrewarmCacheState::Empty && !model.canCompile,"an empty cache is not called compiled");
  const char* vs = "#include <metal_stdlib>\nusing namespace metal; vertex float4 main0(uint i [[vertex_id]]) {return float4(0,0,0,1);}";
  const char* ps = "#include <metal_stdlib>\nusing namespace metal; fragment float4 main0() {return float4(1);}";
  const std::string path = dir.fileSystemRepresentation;
  FILE* f = std::fopen(recomp::gfx::MslCacheFile(path).c_str(),"wb");
  for (auto [h,s] : {std::pair<uint64_t,const char*>{1,vs},{2,ps}}) {
    uint32_t n = std::strlen(s);std::fwrite(&h,8,1,f);std::fwrite(&n,4,1,f);std::fwrite(s,1,n,f);
  }
  std::fclose(f);
  recomp::gfx::PipelineRecipe recipe{};recipe.vs=1;recipe.ps=2;recipe.key.color[0]=MTLPixelFormatBGRA8Unorm;recipe.key.mask[0]=15;
  f=std::fopen(recomp::gfx::RecipeFile(path).c_str(),"wb");std::fwrite(&recipe,sizeof recipe,1,f);std::fclose(f);
  [model refresh];wait();check(model.canCompile,"recorded unsaved pipeline enables compilation");
  __block bool progress = false, saved = false;
  model.changed = ^{
    if (model.running && model.status.pipelines_total == 1) progress = true;
    if (!model.running && model.state == recomp::gfx::PrewarmCacheState::Ready) saved = true;
  };
  [model start];check(model.running && !model.canCompile,"start locks out duplicate compilation");wait();
  check(progress && saved,"real runtime progress reaches a persisted ready state");
  check(model.state == recomp::gfx::PrewarmCacheState::Ready && !model.canCompile,"completion disables recompilation");
  model.changed = nil;[model refresh];wait();check(!model.canCompile,"readiness survives a fresh inspection");
  [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
  return failures ? 1 : 0;
} }
