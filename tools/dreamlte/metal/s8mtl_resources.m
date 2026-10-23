/*
 * S8MaliMetal resources: buffers, textures, samplers, depth-stencil and
 * pipeline state objects.
 *
 * Buffers and textures live in /dev/s8mali GPU memory (one physical copy,
 * write-combined for the CPU, GPU address = physical). Textures are linear,
 * rows aligned to 64 bytes; mip levels beyond 0 are not stored yet. A
 * texture made from an IOSurface uses the surface's own memory through its
 * CPU mapping (stage 2: not yet mapped for the GPU).
 */
#import "s8mtl.h"

void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *IOSurfaceGetBaseAddressOfPlane(void *surface, size_t plane);
size_t IOSurfaceGetBytesPerRowOfPlane(void *surface, size_t plane);
size_t IOSurfaceGetAllocSize(void *surface);
uint32_t IOSurfaceGetID(void *surface);
const void *CFRetain(const void *);
void CFRelease(const void *);

/* ---- buffers ---- */

@implementation S8Buffer

- (instancetype)initWithDevice:(id)device length:(NSUInteger)length options:(NSUInteger)options
{
	self = [super init];
	if (self == nil)
		return nil;
	if (s8_mem_alloc(&_mem, length, 256) != 0) {
		[self release];
		return nil;
	}
	_s8device = device;
	_length = length;
	_options = options;
	return self;
}

/* The caller's memory is used by the CPU only (stage 2: the GPU path will
 * copy it into GPU memory when a draw reads it). */
- (instancetype)initWithDevice:(id)device bytesNoCopy:(void *)p length:(NSUInteger)length
    options:(NSUInteger)options deallocator:(id)dealloc
{
	self = [super init];
	if (self == nil)
		return nil;
	_s8device = device;
	_length = length;
	_options = options;
	_nocopy = p;
	_mem.cpu = p;
	_deallocator = [dealloc copy];
	return self;
}

- (void)dealloc
{
	if (_nocopy != NULL) {
		s8_call_deallocator(_deallocator, _nocopy, _length);
		[_deallocator release];
		_mem.cpu = NULL;
	} else {
		s8_mem_free(&_mem);
	}
	[super dealloc];
}

S8_FORWARDING

- (void *)contents { return _mem.cpu; }
- (NSUInteger)length { return _length; }
- (uint64_t)gpuAddress { return _mem.gpu; }
- (id)device { return _s8device; }
- (NSUInteger)storageMode { return (_options >> 4) & 0xf; }
- (NSUInteger)cpuCacheMode { return _options & 0xf; }
- (NSUInteger)hazardTrackingMode { return 0; }
- (NSUInteger)resourceOptions { return _options; }
- (NSUInteger)allocatedSize { return _mem.size ? _mem.size : _length; }
- (id)heap { return nil; }
- (NSUInteger)heapOffset { return 0; }
- (BOOL)isAliasable { return NO; }
- (void)makeAliasable { }
- (void)didModifyRange:(NSRange)range { (void)range; }
- (NSUInteger)setPurgeableState:(NSUInteger)state { (void)state; return 2; /* NonVolatile */ }
- (NSUInteger)protectionOptions { return 0; }

/* A texture over this buffer's memory (linear, the caller's row pitch). */
- (id)newLinearTextureWithDescriptor:(id)desc offset:(NSUInteger)offset bytesPerRow:(NSUInteger)bpr
    bytesPerImage:(NSUInteger)bpi
{
	(void)bpi;
	S8Texture *t = [[S8Texture alloc] initWithDevice:_s8device descriptor:desc buffer:self
	    offset:offset bytesPerRow:bpr];
	return t;
}

@end

/* ---- textures ---- */

@implementation S8Texture

static void s8_texture_from_descriptor(S8Texture *t, id device, id desc)
{
	t->_s8device = device;
	t->_width = [desc width];
	t->_height = [desc height];
	t->_depth = [desc depth];
	t->_format = [desc pixelFormat];
	t->_type = [desc textureType];
	t->_usage = [desc usage];
	t->_levels = [desc mipmapLevelCount];
	t->_arrayLength = [desc arrayLength];
	t->_storage = [desc storageMode];
	if (t->_depth == 0)
		t->_depth = 1;
	if (t->_levels == 0)
		t->_levels = 1;
}

- (instancetype)initWithDevice:(id)device descriptor:(id)desc
{
	self = [super init];
	if (self == nil)
		return nil;
	s8_texture_from_descriptor(self, device, desc);
	unsigned bpp = s8_format_bpp(_format);
	_rowBytes = ((uint64_t)_width * bpp + 63) & ~63ULL;
	uint64_t size = (uint64_t)_rowBytes * _height * _depth;
	if (size == 0) {
		s8_log("MTL texture %lux%lu fmt %lu storage %lu: no memory", (unsigned long)_width,
		    (unsigned long)_height, (unsigned long)_format, (unsigned long)_storage);
		return self;
	}
	if (s8_mem_alloc(&_mem, size, 64) != 0) {
		[self release];
		return nil;
	}
	_cpu = _mem.cpu;
	_gpu = _mem.gpu;
	return self;
}

- (instancetype)initWithDevice:(id)device descriptor:(id)desc iosurface:(void *)surface plane:(NSUInteger)plane
{
	static unsigned logged;
	self = [super init];
	if (self == nil)
		return nil;
	s8_texture_from_descriptor(self, device, desc);
	_iosurface = surface;
	_plane = plane;
	if (surface != NULL) {
		CFRetain(surface);
		_cpu = IOSurfaceGetBaseAddressOfPlane(surface, plane);
		_rowBytes = IOSurfaceGetBytesPerRowOfPlane(surface, plane);
	}
	if (logged++ < 24)
		s8_log("MTL IOSurface %u texture %lux%lu fmt %lu plane %lu bpr %lu usage 0x%lx",
		    surface ? IOSurfaceGetID(surface) : 0, (unsigned long)_width, (unsigned long)_height,
		    (unsigned long)_format, (unsigned long)plane, (unsigned long)_rowBytes,
		    (unsigned long)_usage);
	return self;
}

- (void)dealloc
{
	if (_parent != nil)
		[_parent release];
	else
		s8_mem_free(&_mem);
	if (_iosurface != NULL)
		CFRelease(_iosurface);
	[super dealloc];
}

S8_FORWARDING

- (id)device { return _s8device; }
- (NSUInteger)width { return _width; }
- (NSUInteger)height { return _height; }
- (NSUInteger)depth { return _depth; }
- (NSUInteger)pixelFormat { return _format; }
- (NSUInteger)textureType { return _type; }
- (NSUInteger)mipmapLevelCount { return _levels; }
- (NSUInteger)arrayLength { return _arrayLength ? _arrayLength : 1; }
- (NSUInteger)sampleCount { return 1; }
- (NSUInteger)usage { return _usage; }
- (NSUInteger)storageMode { return _storage; }
- (NSUInteger)cpuCacheMode { return 0; }
- (NSUInteger)hazardTrackingMode { return 0; }
- (NSUInteger)resourceOptions { return _storage << 4; }
- (void *)iosurface { return _iosurface; }
- (NSUInteger)iosurfacePlane { return _plane; }
- (BOOL)isFramebufferOnly { return NO; }
- (BOOL)isShareable { return NO; }
- (BOOL)isAliasable { return NO; }
- (void)makeAliasable { }
- (BOOL)allowGPUOptimizedContents { return NO; }
- (NSUInteger)allocatedSize { return _mem.size; }
- (id)parentTexture { return _parent; }
- (id)buffer { return nil; }
- (id)heap { return nil; }
- (NSUInteger)rotation { return 0; }
- (NSUInteger)setPurgeableState:(NSUInteger)state { (void)state; return 2; }
- (NSUInteger)s8_rowBytes { return _rowBytes; }
- (NSUInteger)protectionOptions { return 0; }

- (instancetype)initWithDevice:(id)device descriptor:(id)desc buffer:(S8Buffer *)buffer
    offset:(NSUInteger)offset bytesPerRow:(NSUInteger)bpr
{
	self = [super init];
	if (self == nil)
		return nil;
	s8_texture_from_descriptor(self, device, desc);
	_rowBytes = bpr;
	_cpu = (uint8_t *)buffer->_mem.cpu + offset;
	_gpu = buffer->_mem.gpu ? buffer->_mem.gpu + offset : 0;
	_parent = [buffer retain];
	return self;
}

static void s8_copy_rows(uint8_t *dst, NSUInteger dstPitch, const uint8_t *src, NSUInteger srcPitch,
    NSUInteger rowLen, NSUInteger rows)
{
	for (NSUInteger y = 0; y < rows; y++)
		memcpy(dst + y * dstPitch, src + y * srcPitch, rowLen);
}

- (void)replaceRegion:(MTLRegion)r mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice
    withBytes:(const void *)bytes bytesPerRow:(NSUInteger)bpr bytesPerImage:(NSUInteger)bpi
{
	(void)slice;
	(void)bpi;
	if (level != 0 || _cpu == NULL || bytes == NULL)
		return;
	unsigned bpp = s8_format_bpp(_format);
	s8_copy_rows(_cpu + r.origin.y * _rowBytes + r.origin.x * bpp, _rowBytes, bytes, bpr,
	    r.size.width * bpp, r.size.height);
}

- (void)replaceRegion:(MTLRegion)r mipmapLevel:(NSUInteger)level withBytes:(const void *)bytes
    bytesPerRow:(NSUInteger)bpr
{
	[self replaceRegion:r mipmapLevel:level slice:0 withBytes:bytes bytesPerRow:bpr bytesPerImage:0];
}

- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)bpr bytesPerImage:(NSUInteger)bpi
    fromRegion:(MTLRegion)r mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice
{
	(void)bpi;
	(void)slice;
	if (level != 0 || _cpu == NULL || bytes == NULL)
		return;
	unsigned bpp = s8_format_bpp(_format);
	s8_copy_rows(bytes, bpr, _cpu + r.origin.y * _rowBytes + r.origin.x * bpp, _rowBytes,
	    r.size.width * bpp, r.size.height);
}

- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)bpr fromRegion:(MTLRegion)r mipmapLevel:(NSUInteger)level
{
	[self getBytes:bytes bytesPerRow:bpr bytesPerImage:0 fromRegion:r mipmapLevel:level slice:0];
}

- (id)newTextureViewWithPixelFormat:(NSUInteger)format
{
	S8Texture *v = [[S8Texture alloc] init];
	v->_s8device = _s8device;
	v->_width = _width;
	v->_height = _height;
	v->_depth = _depth;
	v->_format = format;
	v->_type = _type;
	v->_usage = _usage;
	v->_levels = _levels;
	v->_arrayLength = _arrayLength;
	v->_storage = _storage;
	v->_rowBytes = _rowBytes;
	v->_cpu = _cpu;
	v->_gpu = _gpu;
	v->_parent = [self retain];
	return v;
}

@end

/* ---- state objects ---- */

@implementation S8SamplerState
- (instancetype)initWithDevice:(id)device samplerDescriptor:(id)desc
{
	self = [super initWithDevice:device samplerDescriptor:desc];
	if (self == nil)
		return nil;
	_s8.min_linear = [desc minFilter] == 1;
	_s8.mag_linear = [desc magFilter] == 1;
	_s8.mip = (uint8_t)[desc mipFilter];
	_s8.address[0] = (uint8_t)[desc sAddressMode];
	_s8.address[1] = (uint8_t)[desc tAddressMode];
	_s8.address[2] = (uint8_t)[desc rAddressMode];
	_s8.normalized = [desc normalizedCoordinates];
	_s8.lod_min = [desc lodMinClamp];
	_s8.lod_max = [desc lodMaxClamp];
	return self;
}
S8_FORWARDING
@end

int s8_texture_ref(S8Texture *t, struct s8d_texture *out)
{
	static unsigned logged;
	int ioctl(int, unsigned long, ...);
	if (t == nil)
		return -1;
	out->width = (uint32_t)t->_width;
	out->height = (uint32_t)t->_height;
	out->format = (uint32_t)t->_format;
	out->row_bytes = (uint32_t)t->_rowBytes;
	out->gpu = t->_gpu;
	if (out->gpu == 0 && t->_iosurface != NULL) {
		/* The window stays valid until 16 other surfaces were mapped: map
		 * at bind time, the command buffer runs right after encoding. */
		struct s8mali_map_surface m = { .id = IOSurfaceGetID(t->_iosurface), .flags = S8MALI_MAP_FLUSH };
		if (s8_fd() < 0 || ioctl(s8_fd(), S8MALI_MAP_SURFACE, &m) != 0) {
			if (logged++ < 10)
				s8_log("MTL IOSurface %u: no GPU mapping", m.id);
			return -1;
		}
		out->gpu = m.gpu_va;
		if (t->_plane != 0 && logged++ < 10)
			s8_log("MTL IOSurface %u plane %lu: plane offsets not handled", m.id, (unsigned long)t->_plane);
		if (m.bytes_per_row)
			out->row_bytes = m.bytes_per_row;
	}
	return out->gpu ? 0 : -1;
}

@implementation S8DepthStencilState
S8_FORWARDING
@end

@implementation S8RenderPipelineState
- (void)dealloc
{
	s8d_pipeline_free(_gpu);
	[_vertex release];
	[_fragment release];
	[super dealloc];
}
S8_FORWARDING
- (id)device { return _s8device; }
- (id)label { return nil; }
- (NSUInteger)maxTotalThreadsPerThreadgroup { return 256; }
- (BOOL)supportIndirectCommandBuffers { return NO; }
@end

@implementation S8ComputePipelineState
S8_FORWARDING
- (id)device { return _s8device; }
- (id)label { return nil; }
- (NSUInteger)maxTotalThreadsPerThreadgroup { return 256; }
- (NSUInteger)threadExecutionWidth { return 4; }
- (NSUInteger)staticThreadgroupMemoryLength { return 0; }
@end
