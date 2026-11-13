#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
using GLuint=unsigned;using GLint=int;using GLsizei=int;using GLfloat=float;using GLchar=char;using GLenum=unsigned;
static std::string assembled;static int mode_value,uploads,uniform_calls;static bool missing_mode,missing_matrix;
static GLint glGetUniformLocation(GLuint,const char*n){return !strcmp(n,"dream_group_mode")?(missing_mode?-1:11):(missing_matrix?-1:12);}
static void glUniform4fv(GLint p,int n,const float*v){assert(p==12&&n==5&&v);uploads++;}
static void glUniform1i(GLint p,int n){assert(p==11);mode_value=n;uniform_calls++;}
static void glShaderSource(unsigned,int n,const char*const*s,const int*){assembled.clear();for(int i=0;i<n;i++)if(s[i])assembled+=s[i];}
static void say(const char*,...){}
#define EAGL_ENTER(e,n) ((void)0)
#define EAGL_LEAVE(e,n) ((void)0)
static bool group_vibrant_on=true,fetch_offered=true;
struct eagl{bool fragment[256]{};const char*sources[256]{};};
#include "ShaderParts.production.inc"
#include "dream-group-mask.inc"
#define calloc(a,b) ((char*)std::calloc(a,b))
#include "ShaderSource.production.inc"
#undef calloc
static const char vertex[]= "attribute highp vec4 vertex_position;\nvarying highp vec2 uv;\nvoid main(){gl_Position=vertex_position;uv=vertex_position.xy;}\n";
static const char fragment[]= "precision highp float;\nvarying highp vec2 uv;\nuniform sampler2D texture1;\nvoid main(){if(uv.x < -0.8) discard; float radius=length(uv);float round_mask=1.-smoothstep(.65,.75,radius);float a=round_mask*texture2D(texture1,uv*.5+.5).a*.5;gl_FragData[0]=vec4(.2*a,.6*a,.8*a,a);}\n";
static void save(const char*dir,const char*name){std::string path=std::string(dir)+"/"+name;FILE*f=fopen(path.c_str(),"wb");assert(f);assert(fwrite(assembled.data(),1,assembled.size(),f)==assembled.size());fclose(f);}
int main(int argc,char**argv){
 assert(argc==2);eagl e;e.fragment[2]=true;
 const char*v[]={vertex};my_glShaderSource(&e,1,1,v,nullptr);save(argv[1],"mask.vert");assert(assembled.find("dream_group_mode")==std::string::npos);
 const char*f[]={fragment};my_glShaderSource(&e,2,1,f,nullptr);save(argv[1],"mask.frag");
 assert(assembled.find(fragment)!=std::string::npos&&assembled.find("dream_group_mode")!=std::string::npos);
 assert(assembled.find("#extension GL_EXT_shader_framebuffer_fetch : require")<assembled.find("layout(location"));
 const char*fetch="#extension GL_EXT_shader_framebuffer_fetch : require\nprecision highp float;\nvoid main(){gl_FragData[0]=gl_LastFragData[0];}\n";
 const char*g[]={fetch};my_glShaderSource(&e,2,1,g,nullptr);assert(assembled.find("dream_group_mode")==std::string::npos&&assembled.find("hakios_last[0] = hakios_colour")!=std::string::npos);
 fetch_offered=false;my_glShaderSource(&e,2,1,f,nullptr);assert(assembled.find("dream_group_mode")==std::string::npos&&assembled.find("inout highp vec4 hakios_colour")==std::string::npos);
 fetch_offered=true;group_vibrant_on=false;my_glShaderSource(&e,2,1,f,nullptr);assert(assembled.find("dream_group_mode")==std::string::npos);
 float colours[20]{};assert(dream_group_original(42,true,colours)==11&&mode_value==1&&uploads==1);
 assert(dream_group_original(42,false,colours)==11&&mode_value==2&&uploads==2);
 int calls=uniform_calls;missing_matrix=true;assert(dream_group_original(42,true,colours)==-1&&uniform_calls==calls&&uploads==2);
 missing_matrix=false;missing_mode=true;assert(dream_group_original(42,true,colours)==-1&&uniform_calls==calls&&uploads==2);
 missing_mode=false;assert(dream_group_original(42,true,nullptr)==-1&&uniform_calls==calls&&uploads==2);
 for(auto*p:e.sources)free((void*)p);
 puts("PASS: actual shader assembly retains original mask/discard/second texture; mode-zero/fetch/capability paths; atomic group uniform activation");
}
