#pragma once
// Manual shader preparation shared by native launchers. The runtime owns
// compilation; this model serializes UI state and callbacks on the main
// thread and does not unlock Play until archive writes have finished.
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#include "runtime/gfx/prewarm.h"
#include <atomic>
#include <memory>
#include <mutex>

struct LpmShaderWork {
  std::atomic<bool> cancel{false}, queued{false};
  std::mutex mutex;
  recomp::gfx::PrewarmStatus status;
};

@interface LpmShaderPreparation : NSObject
@property(nonatomic, readonly) BOOL checking;
@property(nonatomic, readonly) BOOL running;
@property(nonatomic, readonly) BOOL canCompile;
@property(nonatomic, readonly) BOOL cancelling;
@property(nonatomic, readonly) recomp::gfx::PrewarmCacheState state;
@property(nonatomic, readonly) recomp::gfx::PrewarmStatus status;
@property(nonatomic, readonly, copy) NSString* note;
@property(nonatomic, copy) void (^changed)(void);
- (instancetype)initWithCacheDirectory:(NSString*)directory;
- (void)refresh;
- (void)start;
- (void)cancel;
@end

@implementation LpmShaderPreparation {
  NSString* _directory;
  NSString* _failure;
  std::shared_ptr<LpmShaderWork> _work;
}
- (instancetype)init { return [self initWithCacheDirectory:nil]; }
- (instancetype)initWithCacheDirectory:(NSString*)directory {
  if ((self = [super init])) { _directory = [directory copy]; _state = recomp::gfx::PrewarmCacheState::Unavailable; }
  return self;
}
- (BOOL)canCompile {
  return !self.checking && !self.running && self.state == recomp::gfx::PrewarmCacheState::Needed;
}
- (BOOL)cancelling { return _work && _work->cancel.load(); }
- (NSString*)note {
  using State = recomp::gfx::PrewarmCacheState;
  if (self.checking) return @"Checking shader cache…";
  if (self.running) return self.cancelling ? @"Cancelling; saving completed work…" : @"Compiling shaders…";
  if (self.state == State::Ready) return @"All cached shaders are compiled.";
  if (self.state == State::Empty) return @"Play the game to record shaders first.";
  if (_failure) return _failure;
  return self.state == State::Needed ? @"Some cached shaders need compilation." : @"Shader compilation unavailable.";
}
- (void)notify { if (self.changed) self.changed(); }
- (void)refresh {
  if (self.running || self.checking) return;
  _checking = YES; [self notify];
  const std::string directory = _directory ? _directory.fileSystemRepresentation : "";
  __weak LpmShaderPreparation* weak = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
    auto state = recomp::gfx::InspectPrewarmCache(directory);
    dispatch_async(dispatch_get_main_queue(), ^{
      LpmShaderPreparation* model = weak; if (!model) return;
      model->_state = state; model->_checking = NO; [model notify];
    });
  });
}
- (void)start {
  if (!self.canCompile) return;
  auto work = std::make_shared<LpmShaderWork>();
  _work = work; _running = YES; _failure = nil; _status = {}; [self notify];
  const std::string directory = _directory ? _directory.fileSystemRepresentation : "";
  __weak LpmShaderPreparation* weak = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
    recomp::gfx::PrewarmArchiveLimits limits;
    limits.drain_on_close = true;
#if TARGET_OS_IOS || TARGET_OS_TV
    limits.workers = 1; limits.max_unsaved = 64;
#endif
    auto result = recomp::gfx::PrewarmCaches(nullptr,work->cancel,directory,limits,
        [work,weak](const recomp::gfx::PrewarmStatus& status) {
      { std::lock_guard<std::mutex> lock(work->mutex); work->status = status; }
      if (work->queued.exchange(true)) return;
      dispatch_async(dispatch_get_main_queue(), ^{
        work->queued = false;
        LpmShaderPreparation* model = weak;
        if (!model || model->_work != work) return;
        { std::lock_guard<std::mutex> lock(work->mutex); model->_status = work->status; }
        [model notify];
      });
    });
    auto state = recomp::gfx::InspectPrewarmCache(directory);
    dispatch_async(dispatch_get_main_queue(), ^{
      LpmShaderPreparation* model = weak;
      if (!model || model->_work != work) return;
      { std::lock_guard<std::mutex> lock(work->mutex); model->_status = work->status; }
      model->_state = state; model->_running = NO;
      using Result = recomp::gfx::PrewarmResult;
      model->_failure = result == Result::Cancelled ? @"Cancelled. Completed pipelines were kept."
          : result == Result::Busy ? @"Shader cache is in use. Try again later."
          : result == Result::Incomplete ? @"Some pipelines could not be compiled or saved. See the log for details."
          : result == Result::Unavailable ? @"Shader compilation unavailable." : nil;
      model->_work.reset(); [model notify];
    });
  });
}
- (void)cancel {
  if (!_work) return;
  _work->cancel = true; [self notify];
}
@end
