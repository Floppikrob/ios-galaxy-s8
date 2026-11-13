/*
 * What src/eagl.c asks of the OpenGL ES library behind it besides the gl* functions: see
 * src/gl_osmesa.c, the one implementation there is.
 */
#ifndef HAKIOS_GL_H
#define HAKIOS_GL_H

struct hakios_gl;

struct hakios_gl *hakios_gl_create(int version);              /* a context of OpenGL ES 2 or 3; NULL: none to be had */
int hakios_gl_make_current(struct hakios_gl *gl);             /* ... the calling thread's (NULL: none); 0 on failure */
void hakios_gl_destroy(struct hakios_gl *gl);
const char *hakios_gl_library(void);                           /* for the log */

/*
 * The texture bound to GL_TEXTURE_2D made of an IOSurface's own memory: what is drawn into the
 * texture is in the surface, what is in the surface is what the texture shows, and nobody copies.
 * For a library whose driver can do that (a GPU's) and a surface it can do it with. 0: not
 * done; the texture is to be filled and read back with OpenGL ES calls.
 */
int hakios_gl_surface_texture(struct hakios_gl *gl, uint32_t surface);
/*
 * The frame just drawn into the surface's texture (hakios_gl_surface_texture) is shown by the host
 * itself, in a window of its own: 1 when it is (nothing to copy back for the display), 0 when the
 * library has no such host or the surface is not the display's. (SCGPU: the Scion app's gfxstream.)
 */
int hakios_gl_present(struct hakios_gl *gl, uint32_t surface);
/*
 * The end of a frame whose surfaces were all shown by the host (hakios_gl_present), instead of
 * waiting for the GPU: the library waits only until the host has taken in the frame before, so the
 * next frame is made here while the host draws this one. 1 when it did, 0 when it cannot (wait for
 * the frame as before).
 */
int hakios_gl_pace(struct hakios_gl *gl);

/*
 * CoreAnimation draws a layer's pixels with one of ninety "functions" (a fragment shader each); these
 * are the names its Metal renderer has for them, four characters a function, in their order.
 */
#define HAKIOS_CA_FUNCTIONS \
	"clr img ida cim cid mua m1a lrp lrc lra add spl s1a cma bds bco bim c33 c3b c4b c4p c3a c3x c4x bia grl ds4 ds6 " \
	"ds8 ups db2 db4 nb7 n11 n15 n19 n23 n27 vrd vrb sb3 axl rad rac con vcm vdk vlt map mpc crv hue clm hom opa met " \
	"srl ega sdr alt p3c edr hrm lut ovr ovl lub aup alm stl pc0 pc1 prc psl ps0 ps1 psm psr pcr pbl peo bib asg a75 " \
	"a50 atc ipc apl 3dl cst "

#endif
