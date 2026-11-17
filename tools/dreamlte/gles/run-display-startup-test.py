from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).resolve().parent;x=Path(sys.argv[1]);s=(x/'iokit/Kernel/arm/DreamDisplayIommu.inc').read_text()
a=s.index('static bool df_wait(');b=s.index('// Only entered after the full one-frame round trip',a);s=s[a:b]
a=s.index('static void df_write(');b=s.index('\n}',a)+2
s=s[:a]+'static void df_write(volatile uint32_t*r,unsigned off,uint32_t v){df_test_write(r,off,v);}'+s[b:]
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'DisplayStartup.production.inc').write_text(s);exe=d/'startup'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Wno-unused-variable','-Wno-misleading-indentation','-fsanitize=address,undefined','-I'+str(d),str(r/'test-display-startup.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
