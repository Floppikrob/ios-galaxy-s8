from pathlib import Path
import subprocess,sys,tempfile
x=Path(sys.argv[1]);s=(x/'osfmk/arm/pmap/pmap.c').read_text();a=s.index('void\npmap_sync_page_attributes_phys(');b=s.index('\n}',a)+2;func=s[a:b]
old=func.replace('pmap_sync_page_attributes_phys','old_pmap_sync_page_attributes_phys').replace('((vm_offset_t)pp << PAGE_SHIFT)','(vm_offset_t)(pp << PAGE_SHIFT)')
assert old!=func
prefix="""#include <stdint.h>
#include <stdio.h>
#include <assert.h>
typedef uint64_t vm_offset_t;typedef uint32_t ppnum_t;
#define TRUE 1
#define PAGE_SIZE (1ULL<<PAGE_SHIFT)
static uint64_t address;static unsigned count;
static void flush_dcache(vm_offset_t a,unsigned n,int phys){assert(n==PAGE_SIZE&&phys==TRUE);address=a;count++;}
"""
tail="""int main(void){
 const uint64_t lo=0x880000000ULL,hi=0x900000000ULL;
 for(uint64_t pa=lo;pa<hi;pa+=PAGE_SIZE){pmap_sync_page_attributes_phys((ppnum_t)(pa>>PAGE_SHIFT));assert(address==pa);}
 assert(count==(hi-lo)/PAGE_SIZE);
 uint64_t pa=0x8be1d3000ULL&~(PAGE_SIZE-1);
 old_pmap_sync_page_attributes_phys((ppnum_t)(pa>>PAGE_SHIFT));assert(address==(pa&0xffffffffULL)&&address!=pa);
 pmap_sync_page_attributes_phys((ppnum_t)(pa>>PAGE_SHIFT));assert(address==pa);
 pmap_sync_page_attributes_phys((ppnum_t)(0xfffff000ULL>>PAGE_SHIFT));assert(address==(0xfffff000ULL&~(PAGE_SIZE-1)));
 puts("PASS: actual cache-sync function retains every high2GiB page; old32bit shift reproduces photo truncation; low32bit unchanged");}
"""
with tempfile.TemporaryDirectory() as d:
 for shift in (12,14):
  p=Path(d)/'pa.c';exe=Path(d)/('pa'+str(shift));p.write_text(prefix+func+old+tail)
  subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DPAGE_SHIFT='+str(shift),str(p),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
