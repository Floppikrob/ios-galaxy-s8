from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).resolve().parent;x=Path(sys.argv[1]);s=(x/'iokit/Kernel/arm/DreamPanfrostClient.inc').read_text()
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 a=s.index('HakiosMaliUserClient::freeBuffer(');a=s.rindex('void',0,a);b=s.index('/*\n * A handle',a)
 (d/'Free.production.inc').write_text(s[a:b])
 a=s.index('case kPanfrostSubmit: {');a=s.index('uint64_t seq=0;',a);b=s.index('\n\t\tbreak;\n\t}',a)
 (d/'Submit.production.inc').write_text(s[a:b])
 exe=d/'ownership'
 subprocess.run(['g++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(d),'-I'+str(x/'iokit/Kernel/arm'),str(r/'test-panfrost-ownership.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
