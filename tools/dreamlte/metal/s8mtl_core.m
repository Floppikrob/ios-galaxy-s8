/*
 * S8MaliMetal: logging, the /dev/s8mali device, GPU memory and small
 * helpers shared by the Metal objects (s8mtl.h).
 */
#import "s8mtl.h"
#include <stdarg.h>

int open(const char *, int, ...);
int ioctl(int, unsigned long, ...);
int vsnprintf(char *, size_t, const char *, va_list);
void *memset(void *, int, size_t);
uint64_t mach_absolute_time(void);
#define O_RDWR 2

static int s8_dev = -1;
static unsigned s8_forwarded;

int s8_fd(void)
{
	if (s8_dev < 0)
		s8_dev = open("/dev/s8mali", O_RDWR);
	return s8_dev;
}

void s8_log(const char *fmt, ...)
{
	if (s8_fd() < 0)
		return;
	struct s8mali_log l;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(l.text, sizeof(l.text), fmt, ap);
	va_end(ap);
	ioctl(s8_dev, S8MALI_LOG, &l);
}

/* mach_absolute_time counts the S8's 26 MHz architected timer. */
uint64_t s8_now_ns(void)
{
	return mach_absolute_time() * 1000 / 26;
}

/* ---- stand-in for messages not implemented yet ---- */

static id s8_nil_imp(id self, SEL sel, ...)
{
	(void)self;
	(void)sel;
	return (id)0;
}

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

id s8_forward(id self, SEL sel)
{
	if (s8_forwarded++ < 400)
		s8_log("MTL -[%s %s] unimplemented", class_getName(object_getClass(self)), sel_getName(sel));
	if (s8_unimplemented == nil)
		s8_unimplemented = [[S8MaliUnimplemented alloc] init];
	return s8_unimplemented;
}

/* ---- GPU memory ---- */

/* Blocks up to 64 KiB come from 4 MiB slabs in power-of-two size classes
 * (256 B .. 64 KiB) with per-class free lists; larger ones are their own
 * /dev/s8mali allocation. */
#define S8_SLAB         (4u << 20)
#define S8_CLASSES      9               /* 256 << 0 .. 256 << 8 = 64 KiB */
struct s8_free { struct s8_free *next; uint64_t gpu; };
static struct s8_free *s8_freelist[S8_CLASSES];
static uint8_t *s8_slab_cpu;
static uint64_t s8_slab_gpu, s8_slab_used = S8_SLAB;
static uint64_t s8_bytes_live;

static int s8_raw_alloc(uint64_t size, uint64_t *gpu, void **cpu)
{
	struct s8mali_alloc a = { .size = size };
	if (s8_fd() < 0 || ioctl(s8_dev, S8MALI_ALLOC, &a) != 0)
		return -1;
	*gpu = a.gpu_va;
	*cpu = (void *)(uintptr_t)a.user_va;
	return 0;
}

int s8_mem_alloc(s8_mem *m, uint64_t size, uint64_t align)
{
	memset(m, 0, sizeof(*m));
	if (size == 0)
		size = 1;
	if (size > (64u << 10) || align > 256) {
		uint64_t len = (size + 0xffff) & ~0xffffULL;
		if (s8_raw_alloc(len, &m->gpu, &m->cpu) != 0) {
			s8_log("MTL GPU memory: %llu KiB allocation failed (%llu KiB live)",
			    (unsigned long long)(len >> 10), (unsigned long long)(s8_bytes_live >> 10));
			return -1;
		}
		m->size = len;
		m->big = 1;
		s8_bytes_live += len;
		return 0;
	}
	unsigned c = 0;
	while ((256u << c) < size)
		c++;
	uint64_t len = 256u << c;
	if (s8_freelist[c] != NULL) {
		struct s8_free *f = s8_freelist[c];
		s8_freelist[c] = f->next;
		m->cpu = f;
		m->gpu = f->gpu;
	} else {
		if (s8_slab_used + len > S8_SLAB) {
			if (s8_raw_alloc(S8_SLAB, &s8_slab_gpu, (void **)&s8_slab_cpu) != 0) {
				s8_log("MTL GPU memory: slab allocation failed");
				return -1;
			}
			s8_slab_used = 0;
			s8_bytes_live += S8_SLAB;
		}
		m->cpu = s8_slab_cpu + s8_slab_used;
		m->gpu = s8_slab_gpu + s8_slab_used;
		s8_slab_used += len;
	}
	m->size = len;
	memset(m->cpu, 0, len);
	return 0;
}

void s8_mem_free(s8_mem *m)
{
	if (m->cpu == NULL)
		return;
	if (m->big) {
		struct s8mali_free f = { .gpu_va = m->gpu };
		ioctl(s8_dev, S8MALI_FREE, &f);
		s8_bytes_live -= m->size;
	} else {
		unsigned c = 0;
		while ((256u << c) < m->size)
			c++;
		struct s8_free *f = m->cpu;
		f->gpu = m->gpu;
		f->next = s8_freelist[c];
		s8_freelist[c] = f;
	}
	memset(m, 0, sizeof(*m));
}

unsigned s8_format_bpp(NSUInteger f)
{
	switch (f) {
	case MTLPixelFormatA8Unorm:
	case MTLPixelFormatR8Unorm:
	case MTLPixelFormatStencil8:
		return 1;
	case MTLPixelFormatRG8Unorm:
	case MTLPixelFormatR16Float:
		return 2;
	case MTLPixelFormatRGBA8Unorm:
	case MTLPixelFormatRGBA8Unorm_sRGB:
	case MTLPixelFormatBGRA8Unorm:
	case MTLPixelFormatBGRA8Unorm_sRGB:
	case MTLPixelFormatRGB10A2Unorm:
	case MTLPixelFormatBGR10A2Unorm:
	case MTLPixelFormatDepth32Float:
	case MTLPixelFormatBGR10_XR:
		return 4;
	case MTLPixelFormatRGBA16Float:
	case MTLPixelFormatBGRA10_XR:
		return 8;
	default:
		return 4;       /* logged by the caller */
	}
}

/* Blocks (Apple ABI): isa, flags, reserved, invoke, ... */
struct s8_block {
	void *isa;
	int flags, reserved;
	void (*invoke)(void *block, void *pointer, NSUInteger length);
};

void s8_call_deallocator(id block, void *pointer, NSUInteger length)
{
	if (block != nil) {
		struct s8_block *b = (struct s8_block *)block;
		b->invoke(b, pointer, length);
	}
}
