#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using GLuint=unsigned;using GLint=int;using GLfloat=float;using GLenum=unsigned;
constexpr unsigned GL_TEXTURE0=100,GL_TEXTURE_2D=101,GL_TEXTURE_WIDTH=102,GL_TEXTURE_HEIGHT=103,GL_BLEND=104,GL_BLEND_SRC_RGB=105,GL_BLEND_DST_RGB=106,GL_BLEND_SRC_ALPHA=107,GL_BLEND_DST_ALPHA=108,GL_VERTEX_ATTRIB_ARRAY_ENABLED=109,GL_ONE=1,GL_ONE_MINUS_SRC_ALPHA=111;
constexpr unsigned FUNCTION_VIBRANT_MATRIX=45;
enum{GROUP_NONE,GROUP_FRESH,GROUP_VIBRANT};
struct group_texture{unsigned state=GROUP_NONE;float colours[20]{};};
struct eagl{
 unsigned colour=3,framebuffer=7,program=42,bound[4]{2,0,0,0},unit=3;const float*matrix=nullptr;
 group_texture*groups=nullptr;bool group_original=false;int group_original_mode=-1;bool group_blend=false;
 int group_blend_func[4]{};unsigned group_cover=61,group_over=62;
 int group_cover_matrix=20,group_cover_texmat=21,group_over_matrix=22,group_over_texmat=23,group_over_colours=24;
};
static group_texture from,into;static bool group_vibrant_on=true,fetch_offered=true,blending=true,missing;
static float function_values[90][20];static unsigned char function_given[90];
static unsigned group_covered,group_laid,group_flattened,attribute_changes,uses,programs,active_unit;
static int mode_value,blend_func[4]{1,2,3,4};static unsigned current_program=42;
static void say(const char*,...){}static const char*census_name(unsigned){return "img";}
static unsigned char function_state[32];static void*state=function_state;static void*context[3]{nullptr,nullptr,state};static void*ca_context=context;
static group_texture*group_of(eagl*,unsigned t){return t==2?&from:t==3?&into:nullptr;}
static void group_flatten(eagl*,unsigned,group_texture*g){group_flattened++;g->state=GROUP_NONE;}
static void group_read(eagl*,unsigned){}
static bool group_programs(eagl*){programs++;return true;}
static bool group_uniform(eagl*,const char*,float*v,unsigned n){for(unsigned k=0;k<n;k++)v[k]=0;v[0]=v[n==16?5:1]=1;return true;}
static void clear_now(eagl*){}
static int glGetUniformLocation(unsigned,const char*n){return missing?-1:!strcmp(n,"dream_group_mode")?11:12;}
static void glUniform4fv(int p,int n,const float*){assert((p==12&&n==5)||p==21||p==23||p==24);}
static void glUniform1i(int p,int n){assert(p==11);mode_value=n;}
static void glUniformMatrix4fv(int,int,int,const float*){}
static void glActiveTexture(unsigned n){active_unit=n;}
static void glGetTexLevelParameteriv(unsigned,int,unsigned n,int*v){*v=n==GL_TEXTURE_WIDTH?64:32;}
static void glUseProgram(unsigned p){uses++;current_program=p;}
static bool glIsEnabled(unsigned){return blending;}
static void glGetIntegerv(unsigned n,int*v){assert(n>=GL_BLEND_SRC_RGB&&n<=GL_BLEND_DST_ALPHA);*v=blend_func[n-GL_BLEND_SRC_RGB];}
static void glEnable(unsigned){blending=true;}static void glDisable(unsigned){blending=false;}
static void glBlendFunc(unsigned s,unsigned d){blend_func[0]=blend_func[2]=s;blend_func[1]=blend_func[3]=d;}
static void glBlendFuncSeparate(unsigned a,unsigned b,unsigned c,unsigned d){blend_func[0]=a;blend_func[1]=b;blend_func[2]=c;blend_func[3]=d;}
static void glGetVertexAttribiv(unsigned,unsigned,int*v){*v=0;}
static void glEnableVertexAttribArray(unsigned){attribute_changes++;}
static void glDisableVertexAttribArray(unsigned){attribute_changes++;}
#include "dream-group-mask.inc"
#include "GroupBeginEnd.production.inc"
static void reset(){from={};into={};from.state=GROUP_VIBRANT;blending=true;missing=false;attribute_changes=uses=programs=group_flattened=0;current_program=42;mode_value=0;blend_func[0]=1;blend_func[1]=2;blend_func[2]=3;blend_func[3]=4;function_state[16]=3;}
static void restored(){assert(mode_value==0&&blending&&blend_func[0]==1&&blend_func[1]==2&&blend_func[2]==3&&blend_func[3]==4&&current_program==42);}
int main(){
 for(int cover=0;cover<2;cover++){
  reset();eagl e;if(cover)into.state=GROUP_FRESH;int enabled[4]{1,1,1,1};
  assert(group_draw_begin(&e,enabled)==1&&e.group_original&&mode_value==(cover?1:2));
  assert(current_program==42&&uses==0&&attribute_changes==0&&programs==0&&blending==(bool)cover);
  group_draw_end(&e,enabled);restored();assert(!e.group_original&&attribute_changes==0);
 }
 reset();missing=true;eagl e;int enabled[4]{1,1,1,1};assert(group_draw_begin(&e,enabled)==0&&!e.group_original&&group_flattened==1&&from.state==GROUP_NONE);restored();assert(!uses&&!attribute_changes);
 reset();eagl direct;direct.program=0;current_program=0;into.state=GROUP_FRESH;function_state[16]=45;function_given[45]=20;
 assert(group_draw_begin(&direct,enabled)==1&&!direct.group_original&&programs==1&&current_program==61&&attribute_changes==3&&mode_value==0);
 group_draw_end(&direct,enabled);assert(blending&&current_program==0&&attribute_changes==6&&mode_value==0&&active_unit==GL_TEXTURE0+direct.unit);
 puts("PASS: actual group begin/end preserves original program, attributes and coverage mode; cover/overlay blend restoration; missing protocol falls back without bare-quad override; existing direct vibrant path retained");
}
