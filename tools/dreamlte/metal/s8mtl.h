/*
 * S8MaliMetal internals: the Metal objects of the Galaxy S8 Mali driver and
 * the C helpers they share (logging, GPU memory, the /dev/s8mali device).
 *
 * The classes subclass Metal's own private base classes the way Apple's
 * drivers do: _MTLDevice (limits, families, library loading),
 * _MTLCommandQueue / _MTLCommandBuffer (commit, scheduling, completion
 * handlers), _MTLCommandEncoder, _MTLResource, _MTLSamplerState and
 * _MTLDepthStencilState. A driver implements object creation, the encoders'
 * commands and -submitCommandBuffers:count:, which runs the work and
 * reports completion back to the base queue.
 */
#pragma once
#import "sdk/metal_min.h"
#include "../s8mali_ioctl.h"

/* ---- C helpers (s8mtl_core.c) ---- */
void s8_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int s8_fd(void);
/* GPU memory: write-combined CPU mapping + GPU address (VA = PA). Small
 * blocks come from shared slabs, large ones are their own allocation. */
typedef struct {
	uint64_t gpu;
	void *cpu;
	uint64_t size;
	int big;
} s8_mem;
int s8_mem_alloc(s8_mem *m, uint64_t size, uint64_t align);
void s8_mem_free(s8_mem *m);
uint64_t s8_now_ns(void);
/* Bytes per pixel of a MTLPixelFormat (0 = unknown/compressed). */
unsigned s8_format_bpp(NSUInteger format);
/* Calls a Metal deallocator block (void (^)(void *pointer, NSUInteger length)). */
void s8_call_deallocator(id block, void *pointer, NSUInteger length);

/* Each class logs messages it does not implement (first 400 overall) and
 * hands them to a stand-in that answers nil/0. */
id s8_forward(id self, SEL sel);
#define S8_FORWARDING \
	- (id)forwardingTargetForSelector:(SEL)sel { return s8_forward(self, sel); }

/* ---- objects ---- */

@protocol MTLDevice
@end
@protocol MTLDeviceSPI <MTLDevice>
@end
@protocol MTLCommandQueue
@end
@protocol MTLCommandQueueSPI <MTLCommandQueue>
@end
@protocol MTLCommandBuffer
@end
@protocol MTLCommandBufferSPI <MTLCommandBuffer>
@end
@protocol MTLCommandEncoder
@end
@protocol MTLRenderCommandEncoder <MTLCommandEncoder>
@end
@protocol MTLRenderCommandEncoderSPI <MTLRenderCommandEncoder>
@end
@protocol MTLBlitCommandEncoder <MTLCommandEncoder>
@end
@protocol MTLBlitCommandEncoderSPI <MTLBlitCommandEncoder>
@end
@protocol MTLComputeCommandEncoder <MTLCommandEncoder>
@end
@protocol MTLResource
@end
@protocol MTLResourceSPI <MTLResource>
@end
@protocol MTLBuffer <MTLResource>
@end
@protocol MTLTexture <MTLResource>
@end
@protocol MTLTextureSPI <MTLTexture>
@end
@protocol MTLLibrary
@end
@protocol MTLLibrarySPI <MTLLibrary>
@end
@protocol MTLFunction
@end
@protocol MTLFunctionSPI <MTLFunction>
@end
@protocol MTLRenderPipelineState
@end
@protocol MTLRenderPipelineStateSPI <MTLRenderPipelineState>
@end
@protocol MTLComputePipelineState
@end

@interface S8MaliDevice : _MTLDevice <MTLDeviceSPI> {
@public
	unsigned int _s8port;
}
- (instancetype)initWithAcceleratorPort:(unsigned int)port;
@end

@interface S8CommandQueue : _MTLCommandQueue <MTLCommandQueueSPI> {
	id _s8device;
	id _completionQueue;
	void *_completion;      /* dispatch_queue_t for completions */
}
- (instancetype)initWithS8Device:(id)device;
@end

@interface S8Buffer : _MTLResource <MTLBuffer> {
@public
	id _s8device;
	s8_mem _mem;
	NSUInteger _length;
	NSUInteger _options;
	id _deallocator;        /* NoCopy buffers: the caller's memory */
	void *_nocopy;
}
- (instancetype)initWithDevice:(id)device length:(NSUInteger)length options:(NSUInteger)options;
- (instancetype)initWithDevice:(id)device bytesNoCopy:(void *)p length:(NSUInteger)length
    options:(NSUInteger)options deallocator:(id)dealloc;
@end

@interface S8Texture : _MTLResource <MTLTextureSPI> {
@public
	id _s8device;
	NSUInteger _width, _height, _depth, _format, _type, _usage, _levels, _arrayLength, _storage;
	NSUInteger _rowBytes;
	uint8_t *_cpu;          /* level 0 */
	uint64_t _gpu;          /* 0: not GPU-visible (IOSurface, stage 2) */
	s8_mem _mem;
	void *_iosurface;       /* IOSurfaceRef, retained */
	NSUInteger _plane;
	id _parent;             /* texture views share their parent's memory */
}
- (instancetype)initWithDevice:(id)device descriptor:(id)desc;
- (instancetype)initWithDevice:(id)device descriptor:(id)desc iosurface:(void *)surface plane:(NSUInteger)plane;
- (instancetype)initWithDevice:(id)device descriptor:(id)desc buffer:(S8Buffer *)buffer
    offset:(NSUInteger)offset bytesPerRow:(NSUInteger)bpr;
@end

@interface S8SamplerState : _MTLSamplerState
@end

@interface S8DepthStencilState : _MTLDepthStencilState
@end

@interface S8Library : _MTLObjectWithLabel <MTLLibrarySPI> {
	id _s8device;
	unsigned _nfuncs;
	struct { char name[64]; uint8_t type; } *_funcs;      /* from the metallib */
}
- (instancetype)initWithDevice:(id)device path:(const char *)path;
@end

@interface S8Function : _MTLObjectWithLabel <MTLFunctionSPI> {
@public
	id _s8device;
	id _name;               /* NSString */
	NSUInteger _type;
	char _constants[1024];  /* function constants, "name=0xbits;..." */
}
@end

@interface S8RenderPipelineState : NSObject <MTLRenderPipelineStateSPI> {
@public
	id _s8device;
	id _vertex, _fragment;  /* S8Function */
	NSUInteger _colorFormat;
	BOOL _blending;
	unsigned _id;
}
@end

/* One recorded render pass (stage 2: clears execute, draws are counted). */
struct s8_pass {
	int kind;               /* 1 render, 2 blit copy buffer, 3 blit copy texture */
	id target;              /* S8Texture, retained */
	NSUInteger load;
	MTLClearColor clear;
	unsigned draws, pipelines;
	id src, dst;            /* blits, retained */
	NSUInteger srcOff, dstOff, size;
	MTLOrigin so, dO;
	MTLSize sz;
};

@interface S8CommandBuffer : _MTLCommandBuffer <MTLCommandBufferSPI> {
@public
	id _s8device;
	struct s8_pass *_passes;
	unsigned _npasses, _cap;
}
- (unsigned)s8_newPass;
- (struct s8_pass *)s8_pass:(unsigned)index;
- (void)s8_execute;
@end

@interface S8RenderEncoder : _MTLCommandEncoder <MTLRenderCommandEncoderSPI> {
	unsigned _pass;         /* index: the pass array may move */
	S8CommandBuffer *_cb;
}
- (instancetype)initWithCommandBuffer:(S8CommandBuffer *)cb descriptor:(id)desc;
@end

@interface S8BlitEncoder : _MTLCommandEncoder <MTLBlitCommandEncoderSPI> {
	S8CommandBuffer *_cb;
}
- (instancetype)initWithS8CommandBuffer:(S8CommandBuffer *)cb;
@end

@interface S8ComputeEncoder : _MTLCommandEncoder <MTLComputeCommandEncoder>
@end

@interface S8ComputePipelineState : NSObject <MTLComputePipelineState> {
@public
	id _s8device;
}
@end
