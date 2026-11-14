#!/usr/bin/env python3
"""Exercise the production DREAMLTE AMFI policy with ASan/UBSan host stubs."""
from pathlib import Path
import subprocess,sys,tempfile
x=Path(sys.argv[1])
s=(x/'bsd/kern/sweet_amfi.c').read_text()
a=s.index('static int dream_gles_dyld_policy(')
b=s.index('static const struct mac_policy_ops dream_gles_dyld_ops',a)
func=s[a:b]
prefix=r"""#include <stdint.h>
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
"""
tail=r"""static void test_dyld(void){uint64_t flags=0;struct{uint64_t input;user_addr_t output;}args={0,(user_addr_t)&flags};
struct proc p={"PosterBoard","com.apple.PosterBoard"};
assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==0&&flags==0x12);
p.identity="other";assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);p.identity=0;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);
p.identity="com.apple.PosterBoard";p.name="PosterBoardX";assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);
p.name="PosterBoard";assert(dream_gles_dyld_policy(&p,1,(user_addr_t)&args)==ENOPOLICY);dream_gles_policy=0;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);dream_gles_policy=1;
in_error=14;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==14);in_error=0;out_error=14;assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==14);out_error=0;
p.name="backboardd";p.identity=0;assert(!dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)&&flags==0xff);
p.name="SpringBoard";assert(dream_gles_dyld_policy(&p,0x5a,(user_addr_t)&args)==ENOPOLICY);
puts("PASS: actual policy exact PosterBoard identity/name/0x5a/opt-in, flags0x12, copy errors, old compositor0xff/unrelated ENOPOLICY");}

int main(void){
 test_dyld();
 struct {uint64_t before,state,after;} output={0x11223344,0,0x55667788};
 user_addr_t arg=(user_addr_t)&output.state;
 struct proc p={"lsd","com.apple.lsd"};
 assert(dream_gles_dyld_policy(&p,0x60,arg)==0 && output.state==0x86);
 assert(output.before==0x11223344 && output.after==0x55667788);
 p.name="pkd";p.identity="com.apple.pluginkit.pkd";
 output.state=0;in_error=14;
 assert(dream_gles_dyld_policy(&p,0x60,arg)==0 && output.state==0x86);in_error=0;
 out_error=14;output.state=0;
 assert(dream_gles_dyld_policy(&p,0x60,arg)==14 && !output.state);out_error=0;
 dream_gles_policy=0;assert(dream_gles_dyld_policy(&p,0x60,arg)==ENOPOLICY);dream_gles_policy=1;
 const char *names[]={"lsd","pkd","lsdX","pkdX","PosterBoard","backboardd","SpringBoard","installd","other"};
 const char *ids[]={NULL,"", "com.apple.lsd","com.apple.pluginkit.pkd","com.apple.PosterBoard","other"};
 for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)for(unsigned j=0;j<sizeof(ids)/sizeof(*ids);j++){
  p.name=names[i];p.identity=ids[j];output.state=0;
  int allowed=ids[j]&&((!strcmp(names[i],"lsd")&&!strcmp(ids[j],"com.apple.lsd"))||(!strcmp(names[i],"pkd")&&!strcmp(ids[j],"com.apple.pluginkit.pkd")));
  assert(dream_gles_dyld_policy(&p,0x60,arg)==(allowed?0:ENOPOLICY));assert(output.state==(allowed?0x86:0));
 }
 p.name="lsd";p.identity="com.apple.lsd";
 for(int call=0;call<256;call++)if(call!=0x60)assert(dream_gles_dyld_policy(&p,call,arg)==ENOPOLICY);
 assert(output.before==0x11223344 && output.after==0x55667788);
 puts("PASS: actual 0x60 policy: signed lsd/pkd only, opt-in, direct 8-byte output/canaries, no copyin, EFAULT, 54 identity/name pairs, unrelated calls denied; original dyld regression passed");
}
"""

with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'catalog.c';exe=Path(tmp)/'catalog'
 p.write_text(prefix+func+tail)
 subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-format','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
