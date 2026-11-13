/*
 * The OpenGL ES library behind src/eagl.c when that library is Mesa as tools/mk-mesa.sh builds
 * it: any of its drivers that draws into memory, reached through its off-screen interface
 * (OSMesa, given an OpenGL ES profile by tools/mesa/osmesa-es-profile.patch).
 *
 * This file is all that knows which library draws. eagl.c wants four things besides the gl*
 * functions themselves: a context of a version of OpenGL ES, that context made the calling
 * thread's, a context taken away again, and a name for the log. A library with another way to
 * its contexts gets another file like this one.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hakios.gl.h"

/* GL/osmesa.h of Mesa */
#define OSMESA_RGBA                     0x1908
#define OSMESA_FORMAT                   0x22
#define OSMESA_DEPTH_BITS               0x30
#define OSMESA_STENCIL_BITS             0x31
#define OSMESA_PROFILE                  0x33
#define OSMESA_CONTEXT_MAJOR_VERSION    0x36
#define OSMESA_CONTEXT_MINOR_VERSION    0x37
#define OSMESA_ES_PROFILE               0x38
#define GL_UNSIGNED_BYTE                0x1401
#define GL_TEXTURE_2D                   0x0de1
#define GL_VENDOR                       0x1f00
#define GL_RENDERER                     0x1f01

typedef struct osmesa_context *OSMesaContext;
extern OSMesaContext OSMesaCreateContextAttribs(const int *attributes, OSMesaContext share);
extern unsigned char OSMesaMakeCurrent(OSMesaContext context, void *buffer, unsigned type, int width, int height);
extern void OSMesaDestroyContext(OSMesaContext context);
extern void (*OSMesaGetProcAddress(const char *name))(void);
extern const unsigned char *glGetString(unsigned name);
extern unsigned glGetError(void);

/*
 * OSMesa draws its "window" into memory handed to it. Nothing here draws into a window (the
 * window server draws into textures of its own, see eagl.c): a few pixels will do.
 */
#define WINDOW  16

struct hakios_gl {
	OSMesaContext context;
	unsigned char window[WINDOW * WINDOW * 4];
};

struct hakios_gl *
hakios_gl_create(int version)
{
	const int attributes[] = {
		OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 24, OSMESA_STENCIL_BITS, 8,
		OSMESA_PROFILE, OSMESA_ES_PROFILE, OSMESA_CONTEXT_MAJOR_VERSION, version, OSMESA_CONTEXT_MINOR_VERSION, 0,
		0
	};
	struct hakios_gl *gl = calloc(1, sizeof(*gl));

	if (gl != NULL && (gl->context = OSMesaCreateContextAttribs(attributes, NULL)) == NULL) {
		free(gl);
		gl = NULL;
	}
	return gl;
}

int
hakios_gl_make_current(struct hakios_gl *gl)
{
	if (gl == NULL) {
		return OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
	}
	return OSMesaMakeCurrent(gl->context, gl->window, GL_UNSIGNED_BYTE, WINDOW, WINDOW);
}

void
hakios_gl_destroy(struct hakios_gl *gl)
{
	OSMesaDestroyContext(gl->context);
	free(gl);
}

const char *
hakios_gl_library(void)
{
	return "Mesa, off-screen (OSMesa)";
}

/*
 * Which of the library's drivers draws, told by the strings of the context (glGetString):
 *   freedreno   the POCO's Adreno, through the kernel's HakiosGPU: GL_VENDOR "freedreno"
 *   panfrost    the Galaxy S9's Mali-G72, through HakiosMali: GL_RENDERER "Mali-G72 (Panfrost)"
 *   software    softpipe or llvmpipe, which draw in memory of their own with this processor
 * and so what each is given. The two GPU drivers make a texture of an IOSurface's own memory
 * (hakios_gl_surface_texture() below), the software ones are not asked. Both GPU drivers offer
 * EXT_shader_framebuffer_fetch, which eagl.c hands on to CoreAnimation (fetch_offered there). The
 * workaround freedreno needs for it -- a flush before the first draw that reads the framebuffer,
 * which otherwise read pixels of another tile -- is in freedreno itself (fd6_draw.cc,
 * tools/mesa/freedreno-darwin.patch); panfrost's (tools/mesa/panfrost-darwin.patch) carries none
 * of freedreno's, and here the two are treated alike.
 */
enum driver { DRIVER_SOFTWARE, DRIVER_FREEDRENO, DRIVER_PANFROST, DRIVER_OTHER };

static enum driver
which_driver(void)
{
	const char *vendor = (const char *)glGetString(GL_VENDOR);
	const char *renderer = (const char *)glGetString(GL_RENDERER);

	if (renderer == NULL || strstr(renderer, "softpipe") != NULL || strstr(renderer, "llvmpipe") != NULL) {
		return DRIVER_SOFTWARE;
	}
	if (vendor != NULL && strcmp(vendor, "freedreno") == 0) {
		return DRIVER_FREEDRENO;
	}
	if (strstr(renderer, "Panfrost") != NULL) {
		return DRIVER_PANFROST;
	}
	return DRIVER_OTHER;
}

/*
 * OES_EGL_image, with the IOSurface's ID as the image (tools/mesa/freedreno-darwin.patch and
 * panfrost-darwin.patch: the kernel's GPU driver makes a buffer of the surface's memory). Only
 * the GPU drivers are asked: the others refuse every time.
 */
int
hakios_gl_surface_texture(struct hakios_gl *gl, uint32_t surface)
{
	static void (*image_target)(unsigned target, void *image);
	static int asked, shares;

	(void)gl;
	if (!asked) {
		enum driver driver = which_driver();

		asked = 1;
		image_target = (void (*)(unsigned, void *))OSMesaGetProcAddress("glEGLImageTargetTexture2DOES");
		shares = driver == DRIVER_FREEDRENO || driver == DRIVER_PANFROST;
	}
	if (image_target == NULL || !shares) {
		return 0;
	}
	while (glGetError() != 0) {
	}
	image_target(GL_TEXTURE_2D, (void *)(uintptr_t)surface);
	return glGetError() == 0;
}

/* Mesa draws in this process: there is no host to show anything. */
int
hakios_gl_present(struct hakios_gl *gl, uint32_t surface)
{
	(void)gl;
	(void)surface;
	return 0;
}

int
hakios_gl_pace(struct hakios_gl *gl)
{
	(void)gl;
	return 0;
}
