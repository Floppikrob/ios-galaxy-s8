#!/usr/bin/env python3
"""Exercise the production startup gate without notify services or a phone."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'tools/dreamlte/gles/poster-preview.m').read_text()
a=s.index('static unsigned startup_phase');b=s.index('/* Frameworks are not applications',a)
gate=s[a:b].replace('__attribute__((minsize)) ','')
assert 'if(!poster_startup_ready())return;\n    @autoreleasepool' in s
prefix=r'''
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <string.h>
#include <stdarg.h>
#define NSEC_PER_SEC 1000000000ULL
#define DISPATCH_TIME_NOW 0
#define DISPATCH_QUEUE_PRIORITY_LOW 0
static unsigned registered,cancelled,state_reads,scheduled,allowed,queries;
static uint32_t register_error,state_error;
static uint64_t state_pid,delay;
static int kill_result,kill_error,token;
static void (*callback)(void*);
static int quiet(void *unused,const char *format,...){(void)unused;(void)format;return 0;}
#define fprintf quiet
static uint32_t notify_register_check(const char *name,int *out){assert(!strcmp(name,"com.apple.springboard.finishedstartup"));registered++;*out=token=7;return register_error;}
static uint32_t notify_get_state(int t,uint64_t *out){assert(t==token);state_reads++;*out=state_pid;return state_error;}
static uint32_t notify_cancel(int t){assert(t==token);cancelled++;return 0;}
static int fake_kill(pid_t p,int sig){assert(p>0&&sig==0);errno=kill_error;return kill_result;}
#define kill fake_kill
static uint64_t dispatch_time(int now,uint64_t delta){assert(!now);return delta;}
static void* dispatch_get_global_queue(int p,int f){assert(!p&&!f);return 0;}
static void dispatch_after_f(uint64_t when,void *q,void *arg,void (*fn)(void*)){assert(!q&&!arg);delay=when;callback=fn;scheduled++;}
static void provider_diagnostic(void*);
'''
tests=r'''
static void provider_diagnostic(void *unused){(void)unused;queries++;if(poster_startup_ready())allowed++;}
static void reset(void){startup_phase=startup_checks=0;startup_token=-1;registered=cancelled=state_reads=scheduled=allowed=queries=0;register_error=state_error=0;state_pid=delay=0;kill_result=kill_error=0;callback=0;}
int main(void){
 reset();assert(!poster_startup_ready()&&scheduled==1&&delay==2*NSEC_PER_SEC&&allowed==0&&cancelled==0);
 reset();state_pid=27;assert(!poster_startup_ready()&&startup_phase==1&&delay==5*NSEC_PER_SEC&&cancelled==1&&allowed==0);
 unsigned calls=state_reads;assert(!poster_startup_ready()&&state_reads==calls);callback(0);assert(startup_phase==2&&queries==1&&allowed==1);assert(poster_startup_ready()&&state_reads==calls);
 reset();register_error=1;assert(!poster_startup_ready()&&startup_phase==3&&!scheduled&&!allowed);assert(!poster_startup_ready()&&registered==1);
 reset();state_error=2;assert(!poster_startup_ready()&&startup_phase==3&&cancelled==1&&!scheduled&&!allowed);
 reset();state_pid=27;kill_result=-1;kill_error=ESRCH;assert(!poster_startup_ready()&&delay==2*NSEC_PER_SEC&&!allowed);
 reset();state_pid=27;kill_result=-1;kill_error=EPERM;assert(!poster_startup_ready()&&startup_phase==1&&delay==5*NSEC_PER_SEC);
 reset();state_pid=0x80000000ULL;assert(!poster_startup_ready()&&startup_phase==0&&delay==2*NSEC_PER_SEC&&!allowed);
 reset();for(unsigned i=0;i<120;i++)assert(!poster_startup_ready());assert(startup_phase==3&&cancelled==1&&scheduled==119&&!allowed);
 puts("PASS: production startup gate waits, checks live PID, settles, fails closed, bounds polling and keeps queries behind readiness");
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(prefix+gate+tests)
 subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
