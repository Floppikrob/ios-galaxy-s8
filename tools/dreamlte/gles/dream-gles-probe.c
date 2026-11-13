/* A real GLES vertex/tiler/fragment + pixel-readback gate before EAGL hooks. */
#include <OpenGLES/ES2/gl.h>
#include <stdio.h>
#include <string.h>
#include "hakios.gl.h"
static unsigned compile(GLenum kind,const char *source) {
    unsigned s=glCreateShader(kind);int good=0;
    glShaderSource(s,1,&source,0);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&good);
    if(!good){char log[512];glGetShaderInfoLog(s,sizeof(log),0,log);fprintf(stderr,"DREAM GLES PROBE: shader failed %s\n",log);return 0;}
    return s;
}
int dream_gles_probe(void) {
    struct hakios_gl *ctx=hakios_gl_create(3);
    if(!ctx||!hakios_gl_make_current(ctx)){fprintf(stderr,"DREAM GLES PROBE: no context; EAGL unchanged\n");return 0;}
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    fprintf(stderr,"DREAM GLES PROBE: renderer=%s version=%s\n",renderer?renderer:"NULL",glGetString(GL_VERSION));
    int good=renderer&&strstr(renderer,"Panfrost")&&strstr(renderer,"G71");
    GLuint tex=0,fb=0,vbo=0,p=0,vs=0,fs=0;
    if(good) {
        glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
        glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
        vs=compile(GL_VERTEX_SHADER,"attribute vec2 pos; void main(){gl_Position=vec4(pos,0.0,1.0);}");
        fs=compile(GL_FRAGMENT_SHADER,"precision mediump float; void main(){gl_FragColor=vec4(1.0,0.0,0.0,1.0);}");
        p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glBindAttribLocation(p,0,"pos");glLinkProgram(p);
        glGetProgramiv(p,GL_LINK_STATUS,&good);
        good=good&&vs&&fs&&glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if(good) {
            const float triangle[]={-1,-1,1,-1,0,1};
            glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),triangle,GL_STATIC_DRAW);
            glUseProgram(p);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);
            glViewport(0,0,32,32);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);glFinish();
            unsigned char middle[4]={0},corner[4]={0};
            glReadPixels(16,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,middle);glReadPixels(0,31,1,1,GL_RGBA,GL_UNSIGNED_BYTE,corner);
            unsigned err=glGetError();
            good=!err&&middle[0]>240&&middle[1]<8&&middle[2]<8&&corner[0]<8&&corner[2]>240;
            fprintf(stderr,"DREAM GLES PROBE: triangle center=%u,%u,%u,%u corner=%u,%u,%u,%u error=%x %s\n",
                middle[0],middle[1],middle[2],middle[3],corner[0],corner[1],corner[2],corner[3],err,good?"PASS":"FAIL");
        }
    }
    if(vbo)glDeleteBuffers(1,&vbo);if(p)glDeleteProgram(p);if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);
    if(fb)glDeleteFramebuffers(1,&fb);if(tex)glDeleteTextures(1,&tex);
    hakios_gl_make_current(0);hakios_gl_destroy(ctx);
    if(!good)fprintf(stderr,"DREAM GLES PROBE: gate failed; EAGL methods unchanged\n");
    return good;
}
