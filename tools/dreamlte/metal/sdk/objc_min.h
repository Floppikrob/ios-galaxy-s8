/*
 * The few Objective-C runtime and Foundation declarations the S8 Metal
 * plugin needs (no iOS SDK on the build host). ABI: arm64 iOS, non-fragile
 * ivars, no ARC.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#define nil ((id)0)
typedef unsigned long NSUInteger;
typedef long NSInteger;
typedef id (*IMP)(id, SEL, ...);

const char *sel_getName(SEL sel);
const char *class_getName(Class cls);
Class object_getClass(id obj);
BOOL class_addMethod(Class cls, SEL name, IMP imp, const char *types);

__attribute__((objc_root_class))
@interface NSObject {
	Class isa;
}
+ (instancetype)alloc;
+ (Class)class;
- (instancetype)init;
- (instancetype)retain;
- (oneway void)release;
- (instancetype)autorelease;
- (void)dealloc;
- (Class)class;
- (BOOL)respondsToSelector:(SEL)sel;
- (id)forwardingTargetForSelector:(SEL)sel;
+ (BOOL)resolveInstanceMethod:(SEL)sel;
@end

/* Metal's own base device class (exported by Metal.framework): it
 * implements library loading, limits and family queries; a vendor plugin
 * subclasses it and implements -initWithAcceleratorPort: and the rest. */
@interface _MTLDevice : NSObject
- (instancetype)init;
@end
