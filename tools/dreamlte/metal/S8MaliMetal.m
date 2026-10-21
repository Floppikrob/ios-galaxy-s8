/*
 * S8MaliMetal.bundle: Metal on the Galaxy S8's Mali-G71 for iOS.
 *
 * Metal.framework (MTLRegisterDevices) matches every IOAcceleratorES
 * service, loads /System/Library/Extensions/<MetalPluginName>.bundle and
 * instantiates <MetalPluginClassName>, which must subclass Metal's
 * _MTLDevice, with -initWithAcceleratorPort:. The S8 kernel publishes such
 * a service for the Mali with boot-arg s8metal=1 (S8MaliAccelerator in
 * SweetFramebuffer.cpp); the GPU itself is reached through /dev/s8mali
 * (tools/dreamlte/s8mali_ioctl.h).
 *
 * Stage 1 (this file): a diagnostic device. _MTLDevice already implements
 * library loading, limits and family queries; every message this device
 * does not implement yet is logged ("S8 MALI| MTL ... unimplemented") and
 * answered with nil/0 by a stand-in object, so one boot lists exactly what
 * CoreAnimation needs from a Metal device.
 */
#import "sdk/objc_min.h"
#include <stdarg.h>
#include "../s8mali_ioctl.h"

int open(const char *, int, ...);
int ioctl(int, unsigned long, ...);
int vsnprintf(char *, size_t, const char *, va_list);
#define O_RDWR 2

static int s8_fd = -1;

static void s8_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void s8_log(const char *fmt, ...)
{
	if (s8_fd < 0)
		s8_fd = open("/dev/s8mali", O_RDWR);
	if (s8_fd < 0)
		return;
	struct s8mali_log l;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(l.text, sizeof(l.text), fmt, ap);
	va_end(ap);
	ioctl(s8_fd, S8MALI_LOG, &l);
}

/* ---- stand-in for anything not implemented yet ---- */

static id s8_nil_imp(id self, SEL sel, ...)
{
	(void)self;
	(void)sel;
	return (id)0;
}

/* Answers every message with 0/nil (struct returns are left undefined:
 * stage 1 only maps what is asked for). */
@interface S8MaliUnimplemented : NSObject
@end

@implementation S8MaliUnimplemented
+ (BOOL)resolveInstanceMethod:(SEL)sel
{
	class_addMethod(self, sel, (IMP)s8_nil_imp, "@@:");
	return YES;
}
@end

static S8MaliUnimplemented *s8_unimplemented;

/* ---- the device ---- */

@interface S8MaliDevice : _MTLDevice {
	unsigned int _s8port;
	unsigned int _s8calls;
}
- (instancetype)initWithAcceleratorPort:(unsigned int)port;
@end

@implementation S8MaliDevice

- (instancetype)initWithAcceleratorPort:(unsigned int)port
{
	self = [super init];
	if (self == nil)
		return nil;
	_s8port = port;
	s8_log("MTL device created on accelerator port 0x%x (S8MaliMetal stage 1)", port);
	return self;
}

- (id)forwardingTargetForSelector:(SEL)sel
{
	if (_s8calls++ < 400)
		s8_log("MTL -[MTLDevice %s] unimplemented", sel_getName(sel));
	if (s8_unimplemented == nil)
		s8_unimplemented = [[S8MaliUnimplemented alloc] init];
	return s8_unimplemented;
}

@end
