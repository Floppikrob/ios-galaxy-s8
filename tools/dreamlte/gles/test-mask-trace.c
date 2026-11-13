#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/uio.h>
#include <unistd.h>
#include "MaskGL.production.inc"
#define GL_VERTEX_SHADER 0x8b31
#define GL_FLOAT 0x1406
#define GL_FLOAT_VEC2 0x8b50
#define GL_FLOAT_VEC3 0x8b51
#define GL_FLOAT_VEC4 0x8b52
#define GL_FLOAT_MAT2 0x8b5a
#define GL_FLOAT_MAT3 0x8b5b
#define GL_FLOAT_MAT4 0x8b5c
#define GL_TEXTURE0 0x84c0
#define GL_VIEWPORT 0xba2
#define GL_SCISSOR_BOX 0xc10
#define GL_SCISSOR_TEST 0xc11
#define GL_BLEND 0xbe2
#define GL_BLEND_SRC_RGB 0x80c9
#define GL_BLEND_DST_RGB 0x80c8
#define EAGL_GEN_DECLARATIONS
#include "eagl_gen.h"
#undef EAGL_GEN_DECLARATIONS
static char logtext[1<<20];static size_t logused;
static void say(const char*f,...) __attribute__((format(printf,1,2)));
static void say(const char*f,...){va_list ap;va_start(ap,f);int n=vsnprintf(logtext+logused,sizeof(logtext)-logused,f,ap);va_end(ap);assert(n>=0&&(size_t)n<sizeof(logtext)-logused);logused+=(size_t)n;}
static uint64_t now_us(void){static uint64_t n;return n+=100;}
static bool dream_mask_client_read(uintptr_t address,void*out,size_t length){struct iovec local={out,length},remote={(void*)address,length};return process_vm_readv(getpid(),&local,1,&remote,1,0)==(ssize_t)length;}
struct Surface{uint8_t bytes[8];bool locked;};typedef struct Surface*IOSurfaceRef;
struct image{IOSurfaceRef surface;unsigned format,type;bool alpha_first,invert,shared;};
struct eagl{GLuint program,source_names[256];bool fragment[256];const char*sources[256];struct{GLuint id;unsigned flags,seen;}mask_program[64];unsigned mask_draws,frames,draws;};
static GLuint source_texture;static struct Surface surface={{255,255,255,128,255,255,255,255},false};static struct image source_image={&surface,GL_RGBA,GL_UNSIGNED_BYTE,false,false,false};
static struct image*image_of(struct eagl*e,GLuint t){(void)e;return t==source_texture?&source_image:NULL;}
static unsigned IOSurfaceGetID(IOSurfaceRef s){(void)s;return 40;}static unsigned IOSurfaceGetPixelFormat(IOSurfaceRef s){(void)s;return 0x42475241;}
static size_t IOSurfaceGetWidth(IOSurfaceRef s){(void)s;return 2;}static size_t IOSurfaceGetHeight(IOSurfaceRef s){(void)s;return 1;}
static size_t IOSurfaceGetBytesPerRow(IOSurfaceRef s){(void)s;return 8;}static size_t IOSurfaceGetBytesPerElement(IOSurfaceRef s){(void)s;return 4;}
static size_t IOSurfaceGetAllocSize(IOSurfaceRef s){(void)s;return 8;}static void*IOSurfaceGetBaseAddress(IOSurfaceRef s){return s->bytes;}
static unsigned IOSurfaceGetPlaneCount(IOSurfaceRef s){(void)s;return 0;}
static int IOSurfaceLock(IOSurfaceRef s,unsigned flags,void*p){(void)flags;(void)p;assert(!s->locked);s->locked=true;return 0;}
static int IOSurfaceUnlock(IOSurfaceRef s,unsigned flags,void*p){(void)flags;(void)p;assert(s->locked);s->locked=false;return 0;}
enum{kIOSurfaceLockReadOnly=1};
#include "MaskUniform.production.inc"
#include "dream-mask-trace.inc"
typedef void*EGLDisplay;typedef void*EGLConfig;typedef void*EGLSurface;typedef void*EGLContext;
extern void*eglGetProcAddress(const char*);extern unsigned eglInitialize(EGLDisplay,int*,int*);extern unsigned eglBindAPI(unsigned);
extern unsigned eglChooseConfig(EGLDisplay,const int*,EGLConfig*,int,int*);extern EGLSurface eglCreatePbufferSurface(EGLDisplay,EGLConfig,const int*);extern EGLContext eglCreateContext(EGLDisplay,EGLConfig,EGLContext,const int*);extern unsigned eglMakeCurrent(EGLDisplay,EGLSurface,EGLSurface,EGLContext);extern unsigned eglTerminate(EGLDisplay);
static GLuint shader(GLenum kind,const char*s){GLuint n=glCreateShader(kind);glShaderSource(n,1,&s,NULL);glCompileShader(n);GLint ok=0;glGetShaderiv(n,0x8b81,&ok);if(!ok){char msg[4096];glGetShaderInfoLog(n,sizeof(msg),NULL,msg);puts(msg);}assert(ok);return n;}
static const char vs[]="#version 300 es\nprecision highp float;uniform mat4 vertex_matrix;in vec2 vertex_position;in vec4 vertex_color;in vec2 vertex_texcoord0;out vec2 uv;out vec4 color;void main(){gl_Position=vertex_matrix*vec4(vertex_position,0,1);uv=vertex_texcoord0;color=vertex_color;}\n";
static const char fs[]="#version 300 es\nprecision highp float;uniform sampler2D texture0;in vec2 uv;in vec4 color;out vec4 result;vec4 circleImage(vec2 p){return vec4(1.-smoothstep(.65,.75,length(p)));}void main(){result=texture(texture0,uv*.5+.5)*color*circleImage(uv);}\n";
static void attach(GLuint fb,GLuint texture){glBindFramebuffer(GL_FRAMEBUFFER,fb);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,0x8058,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);}
static void readimage(GLuint fb,uint8_t*out){glBindFramebuffer(GL_READ_FRAMEBUFFER,fb);glBindBuffer(0x88eb,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(0xd03,0);glPixelStorei(0xd04,0);glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,out);assert(!glGetError());}
int main(void){
 setenv("LIBGL_ALWAYS_SOFTWARE","1",1);
 EGLDisplay(*get)(unsigned,void*,const int*)=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");assert(get);
 EGLDisplay d=get(0x31dd,NULL,NULL);int major,minor;assert(eglInitialize(d,&major,&minor));assert(eglBindAPI(0x30a0));
 int attrs[]={0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038},n=0;EGLConfig config;
 assert(eglChooseConfig(d,attrs,&config,1,&n)&&n);int sa[]={0x3057,32,0x3056,32,0x3038},ca[]={0x3098,3,0x3038};EGLSurface surf=eglCreatePbufferSurface(d,config,sa);EGLContext ctx=eglCreateContext(d,config,NULL,ca);assert(surf&&ctx&&eglMakeCurrent(d,surf,surf,ctx));
 GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glBindAttribLocation(p,0,"vertex_position");glBindAttribLocation(p,1,"vertex_color");glBindAttribLocation(p,2,"vertex_texcoord0");glLinkProgram(p);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);assert(ok);glUseProgram(p);
 struct eagl e={0};e.program=p;e.draws=1;e.source_names[v%256]=v;e.source_names[f%256]=f;e.sources[v%256]=vs;e.sources[f%256]=fs;e.fragment[f%256]=true;dream_mask_link(&e,p);assert(e.mask_program[0].id==p&&e.mask_program[0].flags==1);
 e.mask_program[0].id=0;e.mask_program[5].id=p;e.mask_program[5].seen=2;dream_mask_link(&e,p);assert(!e.mask_program[0].id&&e.mask_program[5].id==p&&e.mask_program[5].seen==0);
 GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};glUniformMatrix4fv(glGetUniformLocation(p,"vertex_matrix"),1,0,identity);glUniform1i(glGetUniformLocation(p,"texture0"),0);
 GLuint tex[4],fb[2],buffers[4];glGenTextures(4,tex);source_texture=tex[0];glGenFramebuffers(2,fb);attach(fb[0],tex[1]);attach(fb[1],tex[2]);glBindFramebuffer(GL_FRAMEBUFFER,fb[0]);
 glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex[0]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,1,0,GL_RGBA,GL_UNSIGNED_BYTE,surface.bytes);glTexParameteri(GL_TEXTURE_2D,0x2801,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,0x2800,GL_NEAREST);
 struct Vertex{float x,y;uint16_t color[4];float u,v;};struct Vertex vertices[4]={{-1,-1,{0x3800,0x3c00,0x3800,0x3800},-1,-1},{1,-1,{0x3800,0x3c00,0x3800,0x3800},1,-1},{-1,1,{0x3800,0x3c00,0x3800,0x3800},-1,1},{1,1,{0x3800,0x3c00,0x3800,0x3800},1,1}};uint16_t indices[]={0,1,2,2,1,3};
 glGenBuffers(4,buffers);glBindBuffer(0x8892,buffers[0]);glBufferData(0x8892,sizeof(vertices),vertices,0x88e4);
 glVertexAttribPointer(0,2,GL_FLOAT,0,sizeof(struct Vertex),(void*)0);glVertexAttribPointer(1,4,0x140b,0,sizeof(struct Vertex),(void*)8);glVertexAttribPointer(2,2,GL_FLOAT,0,sizeof(struct Vertex),(void*)16);for(unsigned i=0;i<3;i++)glEnableVertexAttribArray(i);
 glBindBuffer(0x8893,buffers[1]);glBufferData(0x8893,sizeof(indices),indices,0x88e4);glBindBuffer(0x8892,buffers[2]);glBufferData(0x8892,256,NULL,0x88e4);
 glViewport(0,0,32,32);glScissor(0,0,32,32);glEnable(GL_SCISSOR_TEST);glEnable(GL_BLEND);glBlendFuncSeparate(1,0x303,1,0x303);glClearColor(.1f,.2f,.3f,1);glClear(GL_COLOR_BUFFER_BIT);glDrawElements(GL_TRIANGLES,6,0x1403,0);
 uint8_t baseline[4096],observed[4096];readimage(fb[0],baseline);glClear(GL_COLOR_BUFFER_BIT);
 glBindFramebuffer(GL_READ_FRAMEBUFFER,fb[1]);glActiveTexture(GL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,tex[3]);glBindBuffer(0x88eb,buffers[3]);glBufferData(0x88eb,4096,NULL,0x88e4);glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,13);glPixelStorei(0xd03,2);glPixelStorei(0xd04,3);
 assert(!glGetError());dream_mask_draw(&e,GL_TRIANGLES,0x1403,0,0,6);assert(!glGetError()&&e.mask_draws==1&&!surface.locked);
 GLenum fields[]={GL_READ_FRAMEBUFFER_BINDING,GL_DRAW_FRAMEBUFFER_BINDING,0x84e0,GL_TEXTURE_BINDING_2D,0x8894,0x8895,0x88ed,GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,0xd03,0xd04,0x8b8d};GLint expected[]={(GLint)fb[1],(GLint)fb[0],GL_TEXTURE0+3,(GLint)tex[3],(GLint)buffers[2],(GLint)buffers[1],(GLint)buffers[3],8,13,2,3,(GLint)p};
 for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++){GLint value=0;glGetIntegerv(fields[i],&value);assert(value==expected[i]);}
 assert(glIsEnabled(GL_BLEND)&&glIsEnabled(GL_SCISSOR_TEST));readimage(fb[0],observed);assert(!memcmp(baseline,observed,sizeof(baseline)));
 assert(strstr(logtext,"DREAM MASK DRAW:")&&strstr(logtext,"DREAM MASK SOURCE:")&&strstr(logtext,"name=vertex_color")&&strstr(logtext,"values=0.5,1,0.5,0.5")&&strstr(logtext,"readable=1")&&strstr(logtext,"after=1 sample=4")&&!strstr(logtext,"QUERY_ERROR")&&!strstr(logtext,"error=500")&&!strstr(logtext,"error=502"));
 GLfloat value[4];assert(dream_mask_attribute(0,1,value)&&value[0]==1&&value[1]==-1);uint8_t byte;assert(!dream_mask_bytes(0x8893,buffers[1],sizeof(indices),&byte,1));assert(!dream_mask_bytes(0x8892,0,1,&byte,1));assert(isinf(dream_mask_half(0x7c00))&&isnan(dream_mask_half(0x7c01))&&dream_mask_half(0x8000)==0);
 size_t before=logused;e.mask_draws=10;dream_mask_draw(&e,GL_TRIANGLES,0x1403,0,0,6);assert(logused==before&&e.mask_draws==10);
 puts("PASS: actual diagnostic with host Mesa ES3; VBO/EBO/half alpha/source/uniform/bounds/pixels; identical final image; split FBO, textures, pack PBO/row/skips restored; safe invalid reads and ten-draw stop");
 eglMakeCurrent(d,NULL,NULL,NULL);eglTerminate(d);return 0;
}
