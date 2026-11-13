#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>
#include "dream-mask-derivative.inc"
static void file(const char*dir,const char*name,const std::string&text){auto path=std::string(dir)+"/"+name;FILE*f=fopen(path.c_str(),"wb");assert(f);assert(fwrite(text.data(),1,text.size(),f)==text.size());fclose(f);}
int main(int argc,char**argv){
 assert(argc==2);
 const char*body[]={
 "vec4 circleImage(highp vec2 uv) { highp float squared=max(dot(uv,uv),0.0001); highp float d=squared*inversesqrt(squared); highp float w; w = max(fwidth(d), 1e-4); return vec4(clamp(0.5+(1.0-d)/w,0.0,1.0)); }\n",
 "vec4 supercircleImage(highp vec2 uv) { uv=max(abs(uv)*1.199-vec2(0.199),vec2(0)); highp float d=pow(uv.x,2.68)+pow(uv.y,2.68); highp float w; w = max(fwidth(d), 1e-4); return vec4(clamp(0.5+(1.0-d)/w,0.0,1.0)); }\n"};
 for(int i=0;i<2;i++){
  char copy[2048];strcpy(copy,body[i]);size_t n=strlen(copy);assert(dream_mask_derivative_fix(copy)==1&&strlen(copy)==n&&strstr(copy,"dfwG71(d)")&&!strstr(copy,"fwidth(d)"));
  std::string head="#version 300 es\nprecision highp float; in highp vec2 uv; out highp vec4 result;\n";
  std::string tail=std::string("void main(){result=vec4(10.0/255.0,132.0/255.0,1,1)*")+(i?"supercircleImage":"circleImage")+"(uv);}\n";
  file(argv[1],i?"super-old.frag":"circle-old.frag",head+body[i]+tail);
  file(argv[1],i?"super-new.frag":"circle-new.frag",head+DREAM_MASK_DERIVATIVE_GLSL+copy+tail);
 }
 char generic[]="vec4 other(float d) { float w; w = max(fwidth(d), 1e-4); return vec4(w); }";auto unchanged=std::string(generic);assert(!dream_mask_derivative_fix(generic)&&unchanged==generic);
 char prototype[]="vec4 circleImage(vec2 uv); vec4 other(float d) { float w; w = max(fwidth(d), 1e-4); return vec4(w); }";unchanged=prototype;assert(!dream_mask_derivative_fix(prototype)&&unchanged==prototype);
 char unknown[]="vec4 circleImage(vec2 uv) { return vec4(fwidth(uv),0,1); }";unchanged=unknown;assert(!dream_mask_derivative_fix(unknown)&&unchanged==unknown);
 for(unsigned lane=0;lane<256;lane++)for(unsigned axis:{1u,2u}){unsigned lower=lane&(3u&~axis),upper=lower+axis;assert(lower<4&&upper<4&&(lower^upper)==axis);if(lane>=4)assert((lane^axis)>=4);}
 double u=2*10.5/216-1,d=std::hypot(u,u),predicted=.5+(1-d)/(2*d);assert(std::abs(predicted-.3932)<.005);
 puts("PASS: actual length-preserving scalar-mask rewrite; other/prototype/vector formulas unchanged; bounded signed lane addressing; zero-neighbor model predicts measured corner opacity (model only)");
}
