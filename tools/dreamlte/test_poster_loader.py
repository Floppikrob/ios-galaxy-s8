from pathlib import Path
import tempfile,subprocess,sys
x=Path(sys.argv[1]);s=(x/'bsd/kern/sweet_amfi.c').read_text();a=s.index('static int dream_gles_dyld_policy(');b=s.index('static const struct mac_policy_ops dream_gles_dyld_ops',a);func=s[a:b]
prefix='''#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define ENOPOLICY 123
typedef uintptr_t user_addr_t;
struct proc{const char *name,*identity;};
static struct proc *current;static int dream_gles_policy=1,in_error=0,out_error=0;
static int proc_pid(struct proc*p){current=p;return 1;}
static void proc_name(int n,char*out,size_t size){(void)n;snprintf(out,size,"%s",current->name);}
static const char*csproc_get_identity(struct proc*p){return p->identity;}
static int copyin(user_addr_t a,void*b,size_t n){if(in_error)return in_error;memcpy(b,(void*)a,n);return 0;}
static int copyout(const void*a,user_addr_t b,size_t n){if(out_error)return out_error;memcpy((void*)b,a,n);return 0;}
'''
tail='''int main(void){uint64_t flags=0;struct{uint64_t input;user_addr_t output;}args={0,(user_addr_t)&flags};
struct proc p={"PosterBoard","com.apple.PosterBoard"};
assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==0&&flags==0x12);
p.identity="other";assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);p.identity=0;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);
p.identity="com.apple.PosterBoard";p.name="PosterBoardX";assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);
p.name="PosterBoard";assert(dream_gles_dyld_policy(&p,1,(user_addr_t)&args)==ENOPOLICY);dream_gles_policy=0;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);dream_gles_policy=1;
in_error=14;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==14);in_error=0;out_error=14;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==14);out_error=0;
p.name="backboardd";p.identity=0;assert(!dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)&&flags==0xff);
p.name="SpringBoard";assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);
puts("PASS: actual policy exact PosterBoard identity/name/0x5a/opt-in, flags0x12, copy errors, old compositor0xff/unrelated ENOPOLICY");}
'''
s=(x/'bsd/dev/mem.c').read_text();a=s.index('static char sweet_null_names[128];');b=s.index('\n#endif',s.index('\n\treturn false;',a));tee=s[a:b]
tp='''#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
static const char *override=0,*process="PosterBoard";
static int PE_parse_boot_argn(const char*n,void*v,size_t z){assert(!strcmp(n,"sweetnull"));if(!override)return 0;snprintf(v,z,"%s",override);return 1;}
static void proc_selfname(char*p,int n){snprintf(p,n,"%s",process);}
'''
tt='''int main(void){char name[32];
#ifdef DREAMLTE
assert(sweet_null_tee(name,sizeof(name)));
#else
assert(!sweet_null_tee(name,sizeof(name)));
#endif
process="backboardd";assert(sweet_null_tee(name,sizeof(name)));process="SpringBoard";assert(sweet_null_tee(name,sizeof(name)));process="PosterBoardX";assert(!sweet_null_tee(name,sizeof(name)));
override="-";sweet_null_parsed=0;process="backboardd";assert(!sweet_null_tee(name,sizeof(name)));
override="PosterBoard";sweet_null_parsed=0;process="PosterBoard";assert(sweet_null_tee(name,sizeof(name)));process="backboardd";assert(!sweet_null_tee(name,sizeof(name)));
puts("PASS: actual null tee exact names/default target scope/explicit override and disable");}
'''
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp)
 for name,code,defs in [('policy',prefix+func+tail,[]),('tee-native',tp+tee+tt,['-DDREAMLTE=1']),('tee-other',tp+tee+tt,[])]:
  p=tmp/(name+'.c');p.write_text(code);exe=tmp/name
  subprocess.run(['gcc','-D_GNU_SOURCE','-std=c11','-Wall','-Wextra','-Werror','-Wno-format','-fsanitize=address,undefined',*defs,str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
