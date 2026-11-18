#include "dream_b3a8_unpack_vector.h"

/* Large CPU-origin images pass through a temporary shared BGRA surface.
 * Allocate the persistent texture with NULL data, then fill it by the GPU.
 * This gives Mesa's delayed-mapping render storage a chance to stay private
 * and CPU-unmapped; it does NOT promise that its BO cache chooses a fresh BO.
 * Keep the CoreAnimation texture name, parameters, pixels and orientation.
 */
struct dream_upload_planes {
    const uint8_t *rgb, *alpha;
    size_t rgb_size, rgb_stride, alpha_size, alpha_stride;
    bool invert;
};

static bool
dream_staged_upload(struct eagl *e, struct image *image, const uint8_t *pixels, size_t row,
    const struct dream_upload_planes *planes)
{
    unsigned w=image->width,h=image->height;
    GLenum internal=internal_format(image->internal,image->format);
    uint64_t extra=(((uint64_t)w*4+63)&~63ULL)*h;
    if (e->api<3||!share_surfaces||(!pixels&&!planes)||image->to!=GL_TEXTURE_2D||image->type!=GL_UNSIGNED_BYTE||
        (planes&&image->format!=GL_BGRA)||
        (image->format!=GL_BGRA&&image->format!=GL_RGBA)||
        (internal!=GL_BGRA&&internal!=GL_RGBA&&internal!=0x8058)||
        !w||!h||w>16384||h>16384||(uint64_t)w*h<65536||(!planes&&row<(uint64_t)w*4)||
        extra>image_bytes_most||e->image_bytes>image_bytes_most-extra) return false;
    GLint bound=0,unpack=0,oldrow=0,oldalign=0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    glGetIntegerv(0x88ef,&unpack); // GL_PIXEL_UNPACK_BUFFER_BINDING
    if ((GLuint)bound!=image->texture||!image->texture||unpack) return false;
    IOSurfaceRef staging=b3a8_bgra_surface(w,h);
    if (!staging) return false;
    if (IOSurfaceGetAllocSize(staging)>image_bytes_most-e->image_bytes) {CFRelease(staging);return false;}
    bool ok=IOSurfaceLock(staging,0,NULL)==0;
    if (ok) {
        uint8_t *dst=(uint8_t *)IOSurfaceGetBaseAddress(staging);
        size_t stride=IOSurfaceGetBytesPerRow(staging);
        ok=dst&&dream_b3a8_span(IOSurfaceGetAllocSize(staging),stride,w,h,4);
        if (ok && planes) {
            // Decode straight into the shared upload buffer while the original is locked.
            ok = dream_b3a8_unpack_vector(dst, IOSurfaceGetAllocSize(staging), stride,
                planes->rgb, planes->rgb_size, planes->rgb_stride,
                planes->alpha, planes->alpha_size, planes->alpha_stride, w, h, planes->invert);
        }
        for (unsigned y=0;ok&&!planes&&y<h;++y) {
            const uint8_t *src=pixels+(size_t)y*row;
            uint8_t *out=dst+(size_t)y*stride;
            if (image->format==GL_BGRA) memcpy(out,src,(size_t)w*4);
            else for (unsigned x=0;x<w;++x) {
                out[4*x]=src[4*x+2];out[4*x+1]=src[4*x+1];
                out[4*x+2]=src[4*x];out[4*x+3]=src[4*x+3];
            }
        }
        IOSurfaceUnlock(staging,ok?0:kIOSurfaceLockReadOnly,NULL);
    }
    GLuint source=0;
    if (ok) ok=b3a8_import(e,staging,&source);
    if (ok) {
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&oldrow);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&oldalign);
        GLenum prior=glGetError();(void)prior;
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(image->to,0,(GLint)internal,image->width,image->height,0,
            image->format,image->type,NULL);
        ok=glGetError()==0;
        glPixelStorei(GL_UNPACK_ROW_LENGTH,oldrow);glPixelStorei(GL_UNPACK_ALIGNMENT,oldalign);
        if (ok) ok=b3a8_blit(source,image->texture,image->width,image->height,false);
    }
    // b3a8_blit waits for completion before either the temporary texture or
    // backing surface can be released. Failed blits retain driver DMA storage.
    if (source) glDeleteTextures(1,&source);
    CFRelease(staging);
    static unsigned direct_reports;
    if (planes && ok && (++direct_reports <= 16 || direct_reports % 120 == 0))
        say("DREAM GLES UPLOAD DIRECT: b3a8=%u size=%ux%u completed=1\n", direct_reports, w, h);
    static unsigned reports;
    if (reports++<24) say("DREAM GLES UPLOAD: staged=%u size=%ux%u bytes=%llu; identity-kept=1 fallback=%u\n",
        ok,w,h,(unsigned long long)extra,!ok);
    return ok;
}
