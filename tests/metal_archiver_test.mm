#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "metal_archiver_mac.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv) {
  bool unmanaged = argc > 1 && !strcmp(argv[1], "unmanaged");
  if (!unmanaged) {
    if (int result = GtaivMetalArchiverSession(argc, argv); result >= 0) return result;
  } else { ++argv; --argc; }
  @autoreleasepool {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "hold")) {
      puts("ready"); fflush(stdout);
      char byte; return read(STDIN_FILENO, &byte, 1) < 0;
    }
    if (argc != 3) return 2;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { fputs("No Metal device\n", stderr); return 3; }
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:
      @"#include <metal_stdlib>\nusing namespace metal; kernel void check(device uint* out [[buffer(0)]]) { out[0] = 42; }"
      options:nil error:&error];
    auto descriptor = [MTLComputePipelineDescriptor new];
    descriptor.computeFunction = [library newFunctionWithName:@"check"];
    NSURL* url = [NSURL fileURLWithPath:@(argv[2])];
    auto archiveDescriptor = [MTLBinaryArchiveDescriptor new];
    if (!strcmp(argv[1], "reuse")) archiveDescriptor.url = url;
    for (int i = 0; i < 3; ++i) {
      id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:archiveDescriptor error:&error];
      if (!archive) { fprintf(stderr, "%s\n", error.description.UTF8String); return 4; }
      if (!strcmp(argv[1], "build")) {
        descriptor.binaryArchives = @[];
        if (![archive addComputePipelineFunctionsWithDescriptor:descriptor error:&error] ||
            ![archive serializeToURL:url error:&error]) return 5;
      }
      descriptor.binaryArchives = @[archive];
      id<MTLComputePipelineState> state = [device newComputePipelineStateWithDescriptor:descriptor
        options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:&error];
      if (!state) { fprintf(stderr, "%s\n", error.description.UTF8String); return 6; }
      id<MTLBuffer> buffer = [device newBufferWithLength:sizeof(unsigned) options:MTLResourceStorageModeShared];
      id<MTLCommandQueue> queue = [device newCommandQueue];
      id<MTLCommandBuffer> commands = [queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
      [encoder setComputePipelineState:state];
      [encoder setBuffer:buffer offset:0 atIndex:0];
      [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
      [encoder endEncoding]; [commands commit]; [commands waitUntilCompleted];
      if (commands.status != MTLCommandBufferStatusCompleted || *(unsigned*)buffer.contents != 42) return 7;
    }
    puts("Metal archive verified: result=42");
  }
}
