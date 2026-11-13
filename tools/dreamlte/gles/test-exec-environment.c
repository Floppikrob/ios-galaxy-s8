#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#define EXECUTABLE_KEY "executable_path="
#define UIO_SYSSPACE 1
#define TRUE 1
#define CAST_USER_ADDR_T(x) ((uintptr_t)(x))
struct image_params {char *ip_strings,*ip_endargv,*ip_strendp;int ip_envc,ip_argspace,ip_strspace;};
static int enabled=1;
static void PE_parse_boot_argn(const char *n,void *v,size_t s){assert(!strcmp(n,"s8gl")&&s==sizeof(int));*(int *)v=enabled;}
static int exec_add_user_string(struct image_params *p,uintptr_t src,int seg,int args){
 assert(seg==UIO_SYSSPACE&&args==TRUE);
 const char *s=(const char *)src;size_t n=strlen(s)+1;
 assert(n<=(size_t)p->ip_argspace&&n<=(size_t)p->ip_strspace);
 memcpy(p->ip_strendp,s,n);p->ip_strendp+=n;p->ip_argspace-=(int)n;p->ip_strspace-=(int)n;return 0;
}
#include "DreamGlesExecEnv.inc"
static char buffer[4096];
static struct image_params setup(const char *path,const char **env,int count){
 memset(buffer,0xa5,sizeof(buffer));snprintf(buffer,512,"%s%s",EXECUTABLE_KEY,path);
 char *envstart=buffer+512,*at=envstart;
 for(int i=0;i<count;i++){size_t n=strlen(env[i])+1;memcpy(at,env[i],n);at+=n;}
 return (struct image_params){buffer,envstart,at,count,(int)(buffer+sizeof(buffer)-at),(int)(buffer+sizeof(buffer)-at)};
}
static int matches(struct image_params *p,const char *want){int n=0;char *s=p->ip_endargv;for(int i=0;i<p->ip_envc;i++){n+=!strcmp(s,want);s+=strlen(s)+1;}return n;}
int main(void){
 const char *existing[]={"DYLD_INSERT_LIBRARIES=/redwood/lib/libdream.gles.dylib","PAN_MESA_DEBUG=noafbc","DYLD_PRINT_LIBRARIES=1","PRESERVE=yes"};
 struct image_params p=setup("/usr/libexec/backboardd",existing,4);int count=p.ip_envc,space=p.ip_argspace;char *end=p.ip_strendp;
 assert(!dream_gles_exec_env(&p,8));assert(p.ip_envc==count&&p.ip_argspace==space&&p.ip_strendp==end);
 assert(matches(&p,"DYLD_PRINT_SEARCHING=1")==1&&matches(&p,"PRESERVE=yes")==1);
 assert(!dream_gles_exec_env(&p,8)&&p.ip_envc==count); // Idempotent, no duplicate entries.
 p=setup("/usr/libexec/backboardd",NULL,0);space=p.ip_argspace;end=p.ip_strendp;
 assert(!dream_gles_exec_env(&p,4)&&p.ip_envc==3);
 assert(space-p.ip_argspace==(p.ip_strendp-end)+3*4);
 assert(matches(&p,existing[0])==1&&matches(&p,"PAN_MESA_DEBUG=noafbc")==1);
 p=setup("/usr/libexec/backboardd",NULL,0);enabled=0;end=p.ip_strendp;assert(!dream_gles_exec_env(&p,8)&&p.ip_strendp==end&&p.ip_envc==0);enabled=1;
 p=setup("/usr/libexec/xpcproxy",existing,4);assert(!dream_gles_exec_env(&p,8)&&matches(&p,existing[2])==1);
 p=setup("/private/tmp/backboardd",NULL,0);assert(!dream_gles_exec_env(&p,8)&&!p.ip_envc);
 const char *custom[]={"DYLD_INSERT_LIBRARIES=/private/other.dylib","DYLD_PRINT_SEARCHING=0","PAN_MESA_DEBUG=custom"};
 p=setup("/usr/libexec/backboardd",custom,3);assert(!dream_gles_exec_env(&p,8)&&p.ip_envc==3);assert(matches(&p,custom[0])==1&&matches(&p,custom[1])==1);
 p=setup("/usr/libexec/backboardd",NULL,0);p.ip_argspace=8;assert(dream_gles_exec_env(&p,8)==E2BIG&&!p.ip_envc);
 p=setup("/usr/libexec/backboardd",NULL,0);p.ip_strspace=1;assert(dream_gles_exec_env(&p,8)==E2BIG&&!p.ip_envc);
 p=setup("/usr/libexec/backboardd",NULL,0);assert(dream_gles_exec_env(&p,16)==EINVAL&&!p.ip_envc);
 p=setup("/usr/libexec/backboardd",NULL,0);p.ip_envc=1;assert(dream_gles_exec_env(&p,8)==EINVAL);
 puts("PASS: exact target/opt-in, existing environment preservation, no duplicates, searching redirect, 32/64-bit pointer accounting, capacity and malformed bounds");
}
