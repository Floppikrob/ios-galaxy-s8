#include <assert.h>
#include <stdio.h>
typedef unsigned GLuint;typedef unsigned GLenum;typedef int GLint;typedef int GLsizei;
struct eagl { int untouched; };
static unsigned arrays,elements;static const void *actual_indices;
static void glDrawArrays(GLenum mode,GLint first,GLsizei count){assert(mode==4&&first==7&&count==6);arrays++;}
static void glDrawElements(GLenum mode,GLsizei count,GLenum type,const void*indices){assert(mode==4&&count==6&&type==0x1403);elements++;actual_indices=indices;}
#include "dream-mask-trace.inc"
int main(void){struct eagl e={42};int indices[3]={0};dream_mask_link(&e,9);dream_mask_draw(&e,4,0,0,7,6);dream_mask_draw(&e,4,0x1403,indices,0,6);assert(arrays==1&&elements==1&&actual_indices==indices&&e.untouched==42);puts("PASS: default build only forwards original draws, with no diagnostic GL query/read/map dependencies");return 0;}
