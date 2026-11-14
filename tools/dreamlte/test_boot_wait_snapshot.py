#!/usr/bin/env python3
"""Exercise the exact bounded frame walker stored in the port patch."""
from pathlib import Path
import subprocess,tempfile
repo=Path(__file__).resolve().parents[2]
patch=(repo/'xnu/patches/0183-dreamlte-bounded-boot-wait-snapshot.patch').read_text()
section=patch.split('+++ b/osfmk/kern/DreamBootWaitWalk.h\n',1)[1].split('\n--- ',1)[0]
header='\n'.join(line[1:] for line in section.splitlines() if line.startswith('+'))+'\n'
fixture=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "walk.h"
static unsigned calls,fail=99,cycle;
static int reader(uint64_t fp,uint64_t out[2],void *arg){
 (void)arg;unsigned i=(unsigned)((fp-0x1000)/16);calls++;
 assert(fp>=0x1000&&!(fp&15)&&i<30);
 if(i==fail)return 0;
 out[0]=cycle?fp:fp+16;out[1]=0x180000000ULL+4*i;return 1;
}
int main(void){uint64_t out[20]={0};
 assert(dream_boot_wait_walk(0x1000,out,20,reader,0)==12&&calls==12&&out[11]==0x18000002cULL);
 calls=0;fail=2;assert(dream_boot_wait_walk(0x1000,out,12,reader,0)==2&&calls==3);
 calls=0;fail=99;cycle=1;assert(dream_boot_wait_walk(0x1000,out,12,reader,0)==1&&calls==1);
 calls=0;assert(!dream_boot_wait_walk(0x1001,out,12,reader,0)&&!calls);
 assert(!dream_boot_wait_walk((1ULL<<39)-16,out,12,reader,0)&&!calls);
 assert(!dream_boot_wait_walk(UINT64_MAX-15,out,12,reader,0)&&!calls);
 assert(!dream_boot_wait_walk(0,out,12,reader,0)&&!calls);
 assert(!dream_boot_wait_walk(0x1000,out,0,reader,0)&&!calls);
 puts("PASS: production walker depth bound, missing page, cycle, alignment, user limit, overflow, null and zero capacity");
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'walk.h').write_text(header);(p/'test.c').write_text(fixture)
 subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
