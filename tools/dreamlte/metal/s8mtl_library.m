/*
 * S8MaliMetal libraries and functions.
 *
 * The driver keeps its own library objects: it reads the metallib's
 * function list (names and types; tools/dreamlte/metal/metallib.py shows
 * the format) and records which function and which function-constant
 * values each request names. Shaders themselves are compiled ahead of
 * time for the Mali (stage 3), keyed by exactly these requests.
 */
#import "s8mtl.h"

int open(const char *, int, ...);
int close(int);
long read(int, void *, unsigned long);
long long lseek(int, long long, int);
void *malloc(size_t);
void *calloc(size_t, size_t);
void free(void *);
void *memcpy(void *, const void *, size_t);
int strcmp(const char *, const char *);
size_t strlen(const char *);

static uint8_t *s8_read_file(const char *path, size_t *len)
{
	int fd = open(path, 0);
	if (fd < 0)
		return NULL;
	long long size = lseek(fd, 0, 2);
	lseek(fd, 0, 0);
	uint8_t *b = size > 0 ? malloc((size_t)size) : NULL;
	size_t got = 0;
	while (b != NULL && got < (size_t)size) {
		long n = read(fd, b + got, (size_t)size - got);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	close(fd);
	if (b != NULL && got != (size_t)size) {
		free(b);
		return NULL;
	}
	*len = got;
	return b;
}

static uint64_t s8_u64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static uint32_t s8_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t s8_u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

@implementation S8Library

- (instancetype)initWithDevice:(id)device path:(const char *)path
{
	self = [super init];
	if (self == nil)
		return nil;
	_s8device = device;
	size_t len = 0;
	uint8_t *d = path ? s8_read_file(path, &len) : NULL;
	if (d != NULL && len > 0x58 && s8_u32(d) == 0x424c544d /* "MTLB" */) {
		uint64_t list = s8_u64(d + 0x18);
		uint32_t count = list + 4 <= len ? s8_u32(d + list) : 0;
		_funcs = calloc(count ? count : 1, sizeof(*_funcs));
		uint64_t off = list + 4;
		for (uint32_t i = 0; i < count && off + 4 <= len; i++) {
			uint32_t flen = s8_u32(d + off);
			uint64_t p = off + 4, end = off + flen;
			while (p + 6 <= end && p + 6 <= len && s8_u32(d + p) != 0x54444e45 /* "ENDT" */) {
				uint16_t sz = s8_u16(d + p + 4);
				if (s8_u32(d + p) == 0x454d414e /* "NAME" */ && sz < sizeof(_funcs[0].name))
					memcpy(_funcs[_nfuncs].name, d + p + 6, sz);
				else if (s8_u32(d + p) == 0x45505954 /* "TYPE" */ && sz >= 1)
					_funcs[_nfuncs].type = d[p + 6];
				p += 6 + sz;
			}
			_nfuncs++;
			off = end;
		}
	}
	free(d);
	s8_log("MTL library %s: %u functions", path ? path : "?", _nfuncs);
	return self;
}

- (void)dealloc
{
	free(_funcs);
	[super dealloc];
}

S8_FORWARDING

- (id)device { return _s8device; }
- (NSUInteger)type { return 0; }

/* metallib TYPE: 0 vertex, 1 fragment, 2 kernel -> MTLFunctionType. */
- (S8Function *)s8_function:(id)name constants:(id)constants
{
	const char *n = [name UTF8String];
	unsigned i;
	for (i = 0; i < _nfuncs && n != NULL; i++)
		if (strcmp(_funcs[i].name, n) == 0)
			break;
	if (n == NULL || i == _nfuncs) {
		s8_log("MTL library: no function %s", n ? n : "(null)");
		return nil;
	}
	S8Function *f = [[S8Function alloc] init];
	f->_s8device = _s8device;
	f->_name = [name retain];
	f->_type = _funcs[i].type == 0 ? MTLFunctionTypeVertex :
	    _funcs[i].type == 1 ? MTLFunctionTypeFragment : MTLFunctionTypeKernel;
	if (constants != nil) {
		const char *c = [[constants description] UTF8String];
		size_t l = c ? strlen(c) : 0;
		if (l >= sizeof(f->_constants))
			l = sizeof(f->_constants) - 1;
		if (c != NULL)
			memcpy(f->_constants, c, l);
		f->_constants[l] = 0;
	}
	return f;
}

- (id)newFunctionWithName:(id)name
{
	return [self s8_function:name constants:nil];
}

- (id)newFunctionWithName:(id)name constantValues:(id)constants error:(id *)error
{
	if (error != NULL)
		*error = nil;
	return [self s8_function:name constants:constants];
}

- (id)newFunctionWithDescriptor:(id)desc error:(id *)error
{
	if (error != NULL)
		*error = nil;
	return [self s8_function:[desc name] constants:[desc constantValues]];
}

@end

@implementation S8Function

- (void)dealloc
{
	[_name release];
	[super dealloc];
}

S8_FORWARDING

- (id)device { return _s8device; }
- (id)name { return _name; }
- (NSUInteger)functionType { return _type; }
- (NSUInteger)options { return 0; }
- (id)vertexAttributes { return nil; }
- (id)stageInputAttributes { return nil; }
- (NSInteger)patchType { return 0; }
- (NSInteger)patchControlPointCount { return -1; }

@end
