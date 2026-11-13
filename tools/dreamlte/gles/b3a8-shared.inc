/* A GPU-written BGRA staging surface avoids glReadPixels and its RGBA shuffle.
 * The original RGB10+A8 surface remains authoritative for all other clients.
 * Import is enabled only after an actual GPU blit passes both row orientations.
 */
static IOSurfaceRef
b3a8_bgra_surface(unsigned width, unsigned height)
{
    if (!width || !height || width > 16384 || height > 16384) return NULL;
    const void *keys[] = {CFSTR("IOSurfaceWidth"), CFSTR("IOSurfaceHeight"),
        CFSTR("IOSurfaceBytesPerElement"), CFSTR("IOSurfaceBytesPerRow"), CFSTR("IOSurfacePixelFormat")};
    int64_t values[] = {width, height, 4, (int64_t)(((uint64_t)width * 4 + 63) & ~63ULL), 'BGRA'};
    CFNumberRef numbers[5] = {0};
    bool ok = true;
    for (unsigned i = 0; i < 5; ++i) {
        numbers[i] = CFNumberCreate(NULL, kCFNumberSInt64Type, &values[i]);
        if (!numbers[i]) ok = false;
    }
    CFDictionaryRef props = ok ? CFDictionaryCreate(NULL, keys, (const void **)numbers, 5,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks) : NULL;
    IOSurfaceRef surface = props ? IOSurfaceCreate(props) : NULL;
    if (props) CFRelease(props);
    for (unsigned i = 0; i < 5; ++i) if (numbers[i]) CFRelease(numbers[i]);
    return surface;
}

static bool
b3a8_import(struct eagl *e, IOSurfaceRef surface, GLuint *texture)
{
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    bool ok = *texture && hakios_gl_surface_texture(e->gl, IOSurfaceGetID(surface));
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    if (!ok && *texture) { glDeleteTextures(1, texture); *texture = 0; }
    return ok;
}

/* Used for the probe and real output. Preserve split FBO and scissor state. */
static bool
b3a8_blit(GLuint from, GLuint to, GLsizei width, GLsizei height, bool invert)
{
    GLint read = 0, draw = 0;
    GLuint fb[2] = {0};
    GLboolean scissor = glIsEnabled(0x0c11); /* GL_SCISSOR_TEST */
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    GLenum prior = glGetError();
    glGenFramebuffers(2, fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, from, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[1]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to, 0);
    bool ok = fb[0] && fb[1] &&
        glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glDisable(0x0c11);
        glBlitFramebuffer(0, 0, width, height, 0, invert ? height : 0, width,
            invert ? 0 : height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        /* CPU encoding below must see the completed GPU writes. */
        glFinish();
    }
    GLenum error = glGetError();
    if (scissor) glEnable(0x0c11);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glDeleteFramebuffers(2, fb);
    if (!ok || error) say("DREAM GLES SHARED: blit refused fbo=%u error=%#x prior=%#x\n", ok, error, prior);
    return ok && !error;
}

static bool
b3a8_shared_probe(struct eagl *e)
{
    if (e->b3a8_probe) return e->b3a8_probe > 0;
    e->b3a8_probe = -1;
    enum { W = 33, H = 17 };
    uint8_t pixels[W * H * 4];
    for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x) {
        unsigned at = (y * W + x) * 4;
        pixels[at] = (x * 7 + y * 3) & 255;
        pixels[at + 1] = (x * 2 + y * 11) & 255;
        pixels[at + 2] = (x * 5 + y * 13) & 255;
        pixels[at + 3] = (x * 3 + y * 17 + 29) & 255;
    }
    IOSurfaceRef surface = b3a8_bgra_surface(W, H);
    GLuint source = 0, mirror = 0;
    GLint bound = 0, row = 0, align = 0, unpack_buffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glGetIntegerv(0x88ef, &unpack_buffer); /* GL_PIXEL_UNPACK_BUFFER_BINDING */
    bool ok = surface && b3a8_import(e, surface, &mirror);
    if (ok) {
        glGenTextures(1, &source);
        glBindTexture(GL_TEXTURE_2D, source);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glBindBuffer(0x88ec, 0); /* GL_PIXEL_UNPACK_BUFFER */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        ok = source && !glGetError();
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row); glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    glBindBuffer(0x88ec, (GLuint)unpack_buffer);
    for (unsigned invert = 0; ok && invert < 2; ++invert) {
        ok = b3a8_blit(source, mirror, W, H, invert);
        if (!ok) break;
        if (IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL)) { ok = false; break; }
        const uint8_t *base = (const uint8_t *)IOSurfaceGetBaseAddress(surface);
        size_t stride = IOSurfaceGetBytesPerRow(surface);
        ok = base && dream_b3a8_span(IOSurfaceGetAllocSize(surface), stride, W, H, 4);
        for (unsigned y = 0; ok && y < H; ++y) for (unsigned x = 0; ok && x < W; ++x) {
            const uint8_t *src = pixels + ((invert ? H - 1 - y : y) * W + x) * 4;
            const uint8_t *dst = base + stride * y + x * 4;
            ok = dst[0] == src[2] && dst[1] == src[1] && dst[2] == src[0] && dst[3] == src[3];
        }
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
    }
    if (source) glDeleteTextures(1, &source);
    if (mirror) glDeleteTextures(1, &mirror);
    if (surface) CFRelease(surface);
    e->b3a8_probe = ok ? 1 : -1;
    say("DREAM GLES SHARED: GPU BGRA probe %s pixels=%u orientations=2; fallback=RGBA readback\n",
        ok ? "PASS" : "FAIL", W * H);
    return ok;
}

static bool
b3a8_make_mirror(struct eagl *e, struct image *image)
{
    uint64_t extra = (((uint64_t)image->width * 4 + 63) & ~63ULL) * image->height;
    if (!blit_targets || !share_surfaces || !image->target || image->alpha_first ||
        image->plane || image->width <= 0 || image->height <= 0 ||
        image->format != GL_BGRA || image->type != GL_UNSIGNED_BYTE ||
        (size_t)image->width != IOSurfaceGetWidth(image->surface) ||
        (size_t)image->height != IOSurfaceGetHeight(image->surface) ||
        extra > image_bytes_most || e->image_bytes > image_bytes_most - extra || !b3a8_shared_probe(e)) return false;
    IOSurfaceRef surface = b3a8_bgra_surface(image->width, image->height);
    if (!surface) return false;
    extra = IOSurfaceGetAllocSize(surface);
    if (extra > image_bytes_most || e->image_bytes > image_bytes_most - extra ||
        !b3a8_import(e, surface, &image->mirror)) { CFRelease(surface); return false; }
    image->mirror_surface = surface;
    image->bytes += extra; e->image_bytes += extra;
    return true;
}
