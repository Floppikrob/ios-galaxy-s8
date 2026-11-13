#ifndef DREAM_MASK_TRACE
#define DREAM_MASK_TRACE 0
#endif
#if DREAM_MASK_TRACE
/* Bounded observations of real shape draws. No shader/output substitutions.
 * Client memory is read through the task API; VBO/EBO offsets are bounds checked
 * against their GL buffer. All reads are diagnostic and stop after ten draws.
 */
static unsigned
dream_mask_flags(const char *s)
{
    if (!s) return 0;
    return (strstr(s,"circleImage(") ? 1u : 0u) |
        (strstr(s,"supercircleImage(") ? 2u : 0u) |
        (strstr(s,"squareImage(") ? 4u : 0u) |
        (strstr(s,"renderImage(") ? 8u : 0u);
}

static void
dream_mask_link(struct eagl *e, GLuint program)
{
    GLuint attached[8] = {0}; GLsizei count = 0; unsigned flags = 0;
    glGetAttachedShaders(program,8,&count,attached);
    for (GLsizei i=0;i<count && i<8;i++) {
        unsigned slot=attached[i]%256;
        if (e->source_names[slot]==attached[i] && e->fragment[slot]) flags |= dream_mask_flags(e->sources[slot]);
    }
    unsigned slot=64;
    for(unsigned i=0;i<64;i++)if(e->mask_program[i].id==program){slot=i;break;}
    if(slot==64)for(unsigned i=0;i<64;i++)if(!e->mask_program[i].id){slot=i;break;}
    if(slot<64){e->mask_program[slot].id=flags?program:0;e->mask_program[slot].flags=flags;e->mask_program[slot].seen=0;}
}

static bool
dream_mask_bytes(GLenum target, GLuint buffer, uintptr_t address, void *out, size_t length)
{
    if (!buffer) return dream_mask_client_read(address,out,length);
    if (address>0x7fffffff || length>0x7fffffff-address) return false;
    GLint previous=0,size=0,mapped=0;
    glGetIntegerv(target==0x8892?0x8894:0x8895,&previous);
    glBindBuffer(target,buffer);glGetBufferParameteriv(target,0x8764,&size);glGetBufferParameteriv(target,0x88bc,&mapped);
    bool ok=false;
    if (!mapped && size>=0 && address+length<=(size_t)size) {
        void *p=glMapBufferRange(target,(GLintptr)address,(GLsizeiptr)length,1); /* MAP_READ_BIT */
        if (p) {memcpy(out,p,length);ok=glUnmapBuffer(target)!=0;}
    }
    glBindBuffer(target,(GLuint)previous);
    return ok;
}

static float
dream_mask_half(uint16_t x)
{
    uint32_t sign=(uint32_t)(x&0x8000)<<16,exp=(x>>10)&31,m=x&1023,bits;
    if (!exp) return (x&0x8000?-1.f:1.f)*(float)m/16777216.f;
    bits=sign|((exp==31?255:exp+112)<<23)|(m<<13);
    float v;memcpy(&v,&bits,4);return v;
}

static unsigned
dream_mask_element(GLenum type)
{
    switch(type) {case 0x1400:case 0x1401:return 1;case 0x1402:case 0x1403:case 0x140b:case 0x8d61:return 2;
    case 0x1404:case 0x1405:case 0x1406:return 4;default:return 0;}
}

static bool
dream_mask_attribute(GLuint attribute, unsigned vertex, GLfloat value[4])
{
    GLint enabled=0,size=0,type=0,stride=0,normalized=0,buffer=0;void *pointer=NULL;
    glGetVertexAttribiv(attribute,0x8622,&enabled);
    if (!enabled) {glGetVertexAttribfv(attribute,0x8626,value);return true;}
    glGetVertexAttribiv(attribute,0x8623,&size);glGetVertexAttribiv(attribute,0x8625,&type);
    glGetVertexAttribiv(attribute,0x8624,&stride);glGetVertexAttribiv(attribute,0x886a,&normalized);
    glGetVertexAttribiv(attribute,0x889f,&buffer);glGetVertexAttribPointerv(attribute,0x8645,&pointer);
    unsigned bytes=dream_mask_element((GLenum)type);uint8_t raw[16];
    if (!bytes || size<1 || size>4 || stride<0 || vertex>1048576) return false;
    size_t step=stride?(size_t)stride:(size_t)size*bytes;
    uintptr_t at=(uintptr_t)pointer;
    if (step && vertex>(UINTPTR_MAX-at)/step) return false;
    at+=step*vertex;
    if (!dream_mask_bytes(0x8892,(GLuint)buffer,at,raw,(size_t)size*bytes)) return false;
    value[0]=value[1]=value[2]=0;value[3]=1;
    for (int i=0;i<size;i++) {
        float v=0;
        switch(type) {
        case 0x1406:memcpy(&v,raw+4*i,4);break;
        case 0x140b:case 0x8d61:{uint16_t n;memcpy(&n,raw+2*i,2);v=dream_mask_half(n);break;}
        case 0x1400:{int8_t n;memcpy(&n,raw+i,1);v=normalized?(n==-128?-1.f:n/127.f):n;break;}
        case 0x1401:{uint8_t n=raw[i];v=normalized?n/255.f:n;break;}
        case 0x1402:{int16_t n;memcpy(&n,raw+2*i,2);v=normalized?(n==-32768?-1.f:n/32767.f):n;break;}
        case 0x1403:{uint16_t n;memcpy(&n,raw+2*i,2);v=normalized?n/65535.f:n;break;}
        case 0x1404:{int32_t n;memcpy(&n,raw+4*i,4);v=normalized?(n==INT32_MIN?-1.f:n/2147483647.f):(float)n;break;}
        case 0x1405:{uint32_t n;memcpy(&n,raw+4*i,4);v=normalized?n/4294967295.f:(float)n;break;}
        }
        value[i]=v;
    }
    return true;
}

static void
dream_mask_source(struct eagl *e, GLuint program)
{
    GLuint attached[8]={0};GLsizei count=0;
    glGetAttachedShaders(program,8,&count,attached);
    for (GLsizei i=0;i<count && i<8;i++) {
        unsigned slot=attached[i]%256;
        if (e->source_names[slot]!=attached[i] || !e->sources[slot]) continue;
        const char *s=e->sources[slot];size_t length=strlen(s),limit=length<16384?length:16384;
        say("DREAM MASK SOURCE: program=%u shader=%u fragment=%u bytes=%zu captured=%zu\n",program,attached[i],e->fragment[slot],length,limit);
        for (size_t at=0;at<limit;at+=96) {
            char hex[193];size_t n=limit-at<96?limit-at:96;
            static const char digits[]="0123456789abcdef";
            for (size_t k=0;k<n;k++){unsigned c=(unsigned char)s[at+k];hex[2*k]=digits[c>>4];hex[2*k+1]=digits[c&15];}
            hex[2*n]=0;say("DREAM MASK SRC: p=%u s=%u off=%zu hex=%s\n",program,attached[i],at,hex);
        }
    }
}

static void
dream_mask_uniforms(GLuint program)
{
    GLint count=0;glGetProgramiv(program,0x8b86,&count);
    for (GLint i=0;i<count && i<40;i++) {
        char name[128]={0};GLint size=0;GLenum type=0;
        glGetActiveUniform(program,(GLuint)i,sizeof(name),NULL,&size,&type,name);
        GLint place=glGetUniformLocation(program,name);if(place<0)continue;
        unsigned n=uniform_floats(type);
        if (!n) {GLint value[16]={0};glGetUniformiv(program,place,value);say("DREAM MASK U: p=%u name=%s type=%x size=%d int=%d\n",program,name,type,size,value[0]);continue;}
        for (int k=0;k<size && k<5;k++) {
            char indexed[160];const char *key=name;
            if(size>1){size_t base=strcspn(name,"[");snprintf(indexed,sizeof(indexed),"%.*s[%d]",(int)base,name,k);key=indexed;}
            GLint location=glGetUniformLocation(program,key);if(location<0)continue;
            GLfloat values[16]={0};char text[400]={0};size_t used=0;glGetUniformfv(program,location,values);
            for(unsigned c=0;c<n && c<16;c++)used+=(size_t)snprintf(text+used,sizeof(text)-used," %.7g",(double)values[c]);
            say("DREAM MASK U: p=%u name=%s type=%x values=%s\n",program,key,type,text);
        }
    }
}

/* CPU-side IOSurface samples describe the uploaded source, not proof of the
 * current GPU texture. The distinction is explicit in the record. */
static void
dream_mask_textures(struct eagl *e)
{
    GLint active=0;glGetIntegerv(0x84e0,&active);
    for(unsigned unit=0;unit<4;unit++) {
        GLint texture=0,w=0,h=0,internal=0,swizzle[4]={0},min=0,mag=0,wrap[2]={0};
        glActiveTexture(GL_TEXTURE0+unit);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);if(!texture)continue;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,0x1000,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,0x1001,&h);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,0x1003,&internal);
        for(unsigned c=0;c<4;c++)glGetTexParameteriv(GL_TEXTURE_2D,0x8e42+c,&swizzle[c]);
        glGetTexParameteriv(GL_TEXTURE_2D,0x2801,&min);glGetTexParameteriv(GL_TEXTURE_2D,0x2800,&mag);
        glGetTexParameteriv(GL_TEXTURE_2D,0x2802,&wrap[0]);glGetTexParameteriv(GL_TEXTURE_2D,0x2803,&wrap[1]);
        say("DREAM MASK TEX: unit=%u id=%d size=%dx%d internal=%x swizzle=%x,%x,%x,%x filter=%x,%x wrap=%x,%x\n",unit,texture,w,h,internal,swizzle[0],swizzle[1],swizzle[2],swizzle[3],min,mag,wrap[0],wrap[1]);
        struct image *image=image_of(e,(GLuint)texture);if(!image)continue;
        IOSurfaceRef surface=image->surface;
        say("DREAM MASK CPU-SOURCE: tex=%d surface=%u fmt=%x size=%zux%zu row=%zu bpe=%zu image_format=%x type=%x alpha_first=%u invert=%u shared=%u\n",texture,IOSurfaceGetID(surface),IOSurfaceGetPixelFormat(surface),IOSurfaceGetWidth(surface),IOSurfaceGetHeight(surface),IOSurfaceGetBytesPerRow(surface),IOSurfaceGetBytesPerElement(surface),image->format,image->type,image->alpha_first,image->invert,image->shared);
        if(IOSurfaceGetPlaneCount(surface) || IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL))continue;
        size_t width=IOSurfaceGetWidth(surface),height=IOSurfaceGetHeight(surface),row=IOSurfaceGetBytesPerRow(surface),bpe=IOSurfaceGetBytesPerElement(surface),alloc=IOSurfaceGetAllocSize(surface);
        const uint8_t *base=(const uint8_t*)IOSurfaceGetBaseAddress(surface);
        if(base && width && height && (bpe==1||bpe==2||bpe==4))for(unsigned p=0;p<3;p++) {
            size_t x=p*(width-1)/2,y=p*(height-1)/2;
            uint64_t offset=(uint64_t)y*row+(uint64_t)x*bpe;uint8_t v[4]={0};
            if(offset<=alloc && bpe<=alloc-offset && dream_mask_client_read((uintptr_t)base+(uintptr_t)offset,v,bpe))
                say("DREAM MASK CPU-BYTES: tex=%d x=%zu y=%zu bytes=%u,%u,%u,%u count=%zu\n",texture,x,y,v[0],v[1],v[2],v[3],bpe);
        }
        IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
    }
    glActiveTexture((GLenum)active);
}

struct dream_mask_observation {bool active;unsigned number;GLuint program;GLint xy[5][2];uint64_t began;};

static void
dream_mask_pixels(const struct dream_mask_observation *o, bool after)
{
    GLint read=0,draw=0,readbuffer=0,pack=0,row=0,align=0,skiprow=0,skippixel=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)draw);glGetIntegerv(0x0c02,&readbuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glGetIntegerv(0x88ed,&pack);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_ALIGNMENT,&align);
    glGetIntegerv(0x0d03,&skiprow);glGetIntegerv(0x0d04,&skippixel);
    glBindBuffer(0x88eb,0);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(0x0d03,0);glPixelStorei(0x0d04,0);
    for(unsigned i=0;i<5;i++) {
        uint8_t pixel[4]={0};glReadPixels(o->xy[i][0],o->xy[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        GLenum error=glGetError();say("DREAM MASK PIXEL: draw=%u after=%u sample=%u xy=%d,%d rgba=%u,%u,%u,%u error=%x\n",o->number,after,i,o->xy[i][0],o->xy[i][1],pixel[0],pixel[1],pixel[2],pixel[3],error);
    }
    glPixelStorei(GL_PACK_ROW_LENGTH,row);glPixelStorei(GL_PACK_ALIGNMENT,align);glPixelStorei(0x0d03,skiprow);glPixelStorei(0x0d04,skippixel);glBindBuffer(0x88eb,(GLuint)pack);
    glReadBuffer((GLenum)readbuffer);glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)read);
}

static struct dream_mask_observation
dream_mask_before(struct eagl *e, GLenum mode, GLenum type, const void *indices, GLint first, GLsizei count)
{
    struct dream_mask_observation o={0};unsigned slot=64;
    if(!e->program || e->mask_draws>=10 || count<=0 || count>96 || first<0)return o;
    for(unsigned i=0;i<64;i++)if(e->mask_program[i].id==e->program){slot=i;break;}
    if(slot==64 || e->mask_program[slot].seen>=2 || (e->mask_program[slot].seen && e->frames<80))return o;
    o.began=now_us();GLuint program=e->program;GLint live=0;glGetIntegerv(0x8b8d,&live);
    if((GLuint)live!=program)return o;
    o.number=++e->mask_draws;o.program=program;
    GLenum prior=glGetError();
    if(!e->mask_program[slot].seen)dream_mask_source(e,program);
    e->mask_program[slot].seen++;
    GLint viewport[4]={0},box[4]={0},blend[4]={0};GLboolean write[4]={0};
    glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,box);glGetBooleanv(0x0c23,write);
    glGetIntegerv(GL_BLEND_SRC_RGB,&blend[0]);glGetIntegerv(GL_BLEND_DST_RGB,&blend[1]);glGetIntegerv(0x80cb,&blend[2]);glGetIntegerv(0x80ca,&blend[3]);
    say("DREAM MASK DRAW: id=%u frame=%u ordinal=%u program=%u flags=%x mode=%x count=%d indexed=%u prior_error=%x viewport=%d,%d,%d,%d scissor=%u:%d,%d,%d,%d blend=%u:%x,%x,%x,%x colorwrite=%u,%u,%u,%u depth=%u stencil=%u\n",o.number,e->frames+1,e->draws,program,e->mask_program[slot].flags,mode,count,type!=0,prior,viewport[0],viewport[1],viewport[2],viewport[3],glIsEnabled(GL_SCISSOR_TEST),box[0],box[1],box[2],box[3],glIsEnabled(GL_BLEND),blend[0],blend[1],blend[2],blend[3],write[0],write[1],write[2],write[3],glIsEnabled(0x0b71),glIsEnabled(0x0b90));
    dream_mask_uniforms(program);dream_mask_textures(e);
    unsigned vertices[96];bool readable=true;
    if(type) {
        GLint buffer=0;glGetIntegerv(0x8895,&buffer);unsigned bytes=dream_mask_element(type);uint8_t raw[384]={0};
        if((type!=0x1401 && type!=0x1403 && type!=0x1405) || !bytes || !dream_mask_bytes(0x8893,(GLuint)buffer,(uintptr_t)indices,raw,(size_t)count*bytes))readable=false;
        else for(int i=0;i<count;i++){uint32_t v=0;memcpy(&v,raw+i*bytes,bytes);vertices[i]=v;}
    } else for(int i=0;i<count;i++)vertices[i]=(unsigned)first+(unsigned)i;
    GLfloat matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};GLint location=glGetUniformLocation(program,"vertex_matrix");if(location>=0)glGetUniformfv(program,location,matrix);
    GLfloat low[2]={1e20f,1e20f},high[2]={-1e20f,-1e20f};GLint position=glGetAttribLocation(program,"vertex_position");
    if(position<0)readable=false;
    for(int i=0;readable && i<count;i++) {
        GLfloat p[4],clip[4]={0};if(!dream_mask_attribute((GLuint)position,vertices[i],p)){readable=false;break;}
        for(unsigned c=0;c<4;c++)for(unsigned k=0;k<4;k++)clip[c]+=matrix[4*k+c]*p[k];
        if(!isfinite(clip[0])||!isfinite(clip[1])||!isfinite(clip[3])||clip[3]==0){readable=false;break;}
        for(unsigned c=0;c<2;c++){float v=viewport[c]+(clip[c]/clip[3]+1)*viewport[c+2]*.5f;if(!isfinite(v)){readable=false;break;}if(v<low[c])low[c]=v;if(v>high[c])high[c]=v;}
    }
    GLint attributes=0;glGetProgramiv(program,0x8b89,&attributes);
    for(GLint a=0;a<attributes && a<12;a++) {
        char name[96]={0};GLint size=0;GLenum attrtype=0;glGetActiveAttrib(program,(GLuint)a,sizeof(name),NULL,&size,&attrtype,name);
        GLint index=glGetAttribLocation(program,name);if(index<0)continue;
        GLint enabled=0,buffer=0,components=0,format=0,stride=0,normalized=0;
        glGetVertexAttribiv((GLuint)index,0x8622,&enabled);glGetVertexAttribiv((GLuint)index,0x889f,&buffer);glGetVertexAttribiv((GLuint)index,0x8623,&components);glGetVertexAttribiv((GLuint)index,0x8625,&format);glGetVertexAttribiv((GLuint)index,0x8624,&stride);glGetVertexAttribiv((GLuint)index,0x886a,&normalized);
        say("DREAM MASK ATTR: draw=%u name=%s location=%d enabled=%d buffer=%d size=%d type=%x stride=%d normalized=%d\n",o.number,name,index,enabled,buffer,components,format,stride,normalized);
        if(readable)for(int i=0;i<count && i<6;i++){GLfloat v[4]={0};bool ok=dream_mask_attribute((GLuint)index,vertices[i],v);say("DREAM MASK V: draw=%u attr=%s vertex=%u ok=%u values=%.7g,%.7g,%.7g,%.7g\n",o.number,name,vertices[i],ok,(double)v[0],(double)v[1],(double)v[2],(double)v[3]);}
    }
    /* Read only inside a texture attachment whose dimensions are known. */
    GLint target=0,targettype=0,level=0,face=0,bound=0,width=0,height=0;
    GLenum complete=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0x8cd0,&targettype);
    if(targettype==0x1702) {
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0x8cd2,&level);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0x8cd3,&face);
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0x8cd1,&target);
        if(!face){glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);glBindTexture(GL_TEXTURE_2D,(GLuint)target);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,0x1000,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,0x1001,&height);glBindTexture(GL_TEXTURE_2D,(GLuint)bound);}
    }
    say("DREAM MASK BOUNDS: draw=%u readable=%u target=%d level=%d face=%x status=%x size=%dx%d screen=%.7g,%.7g,%.7g,%.7g\n",o.number,readable,target,level,face,complete,width,height,(double)low[0],(double)low[1],(double)high[0],(double)high[1]);
    if(readable && complete==GL_FRAMEBUFFER_COMPLETE && width>0 && height>0 && high[0]>low[0] && high[1]>low[1]) {
        static const float point[5][2]={{.05f,.05f},{.95f,.05f},{.5f,.5f},{.05f,.95f},{.95f,.95f}};
        for(unsigned i=0;i<5;i++)for(unsigned c=0;c<2;c++){double v=low[c]+(high[c]-low[c])*point[i][c];int limit=c?height:width;if(v<0)v=0;if(v>=limit)v=limit-1;o.xy[i][c]=(GLint)v;}
        GLenum query_error=glGetError();if(query_error)say("DREAM MASK QUERY_ERROR: draw=%u error=%x\n",o.number,query_error);
        o.active=true;dream_mask_pixels(&o,false);
    }
    return o;
}

static void
dream_mask_draw(struct eagl *e, GLenum mode, GLenum type, const void *indices, GLint first, GLsizei count)
{
    struct dream_mask_observation o=dream_mask_before(e,mode,type,indices,first,count);
    if(type)glDrawElements(mode,count,type,indices);else glDrawArrays(mode,first,count);
    if(o.active)dream_mask_pixels(&o,true);
    if(o.number)say("DREAM MASK END: draw=%u pixels_sampled=%u diagnostic_us=%llu bounded_total=%u\n",o.number,o.active,(unsigned long long)(now_us()-o.began),e->mask_draws);
}

#else
/* Normal builds never sample or map draw inputs. */
static void dream_mask_link(struct eagl *e, GLuint program) { (void)e; (void)program; }
static void dream_mask_draw(struct eagl *e, GLenum mode, GLenum type, const void *indices, GLint first, GLsizei count)
{
    (void)e;
    if (type) glDrawElements(mode,count,type,indices); else glDrawArrays(mode,first,count);
}
#endif
