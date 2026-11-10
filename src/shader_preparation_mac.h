#import <AppKit/AppKit.h>

// Launcher UI and process orchestration only. Cache semantics live in x86recomp.
@interface GtaivShaderPreparation : NSObject
@property(nonatomic, readonly) NSView* view;
@property(nonatomic, readonly) BOOL preparing;
@property(nonatomic, copy) void (^runningChanged)(BOOL);
- (instancetype)initWithGameFolder:(NSString* (^)(void))folder environment:(NSDictionary* (^)(void))environment;
- (void)refresh;
- (void)stopWithCompletion:(void (^)(void))completion;
+ (NSString*)cacheRoot;
@end
