from pathlib import Path
import subprocess,tempfile,sys
x=Path(sys.argv[1]);s=(x/'bsd/kern/kern_mib.c').read_text();a=s.index('\tcase HW_MACHINE:',s.index('#if XNU_TARGET_OS_OSX && defined(__arm64__) && !defined(ARM64_BOARD_CONFIG_SWEET)'));b=s.index('\tcase HW_MODEL:',a);body=s[a:b]
prefix='''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define HW_MACHINE 1
#define EINVAL 22
#define PLATFORM_IOS 2
struct req {int p;} request,*req=&request;
static int valid=1,platform=PLATFORM_IOS;
static char result[65];
size_t strlcpy(char *d,const char *s,size_t n){size_t k=strlen(s);if(n){size_t take=k<n-1?k:n-1;memcpy(d,s,take);d[take]=0;}return k;}
int PEGetMachineName(char *p,int n){if(!valid)return 0;snprintf(p,n,"iPhone10,3");return 1;}
int proc_platform(int p){(void)p;return platform;}
#define SYSCTL_OUT(r,p,n) (memcpy(result,p,n),0)
int run(void){char dummy[65];switch(HW_MACHINE){
'''
tail='''}return 99;}
int main(void){assert(run()==0);assert(strcmp(result,EXPECT)==0);platform=0;assert(run()==0);assert(strcmp(result,EXPECT_MAC)==0);
#ifdef ARM64_BOARD_CONFIG_A5XELTE
valid=0;assert(run()==EINVAL);
#endif
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.c';p.write_text(prefix+body+tail)
 for name,defs in [('native',['-DARM64_BOARD_CONFIG_A5XELTE=1','-DEXPECT="iPhone10,3"','-DEXPECT_MAC="iPhone10,3"']),('mac',['-DEXPECT="iPad8,6"','-DEXPECT_MAC="arm64"'])]:
  binary=Path(tmp)/name
  subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*defs,str(p),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True)
print('PASS: actual HW_MACHINE case native-iOS DT identity, failed property errors, unrelated Mac/iOS-on-Mac answers retained')
