from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).resolve().parent;x=Path(sys.argv[1])
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 source=(x/'iokit/Kernel/arm/DreamPanfrostClient.inc').read_text()
 a=source.index('if (selector == kHakiosMaliWait || selector == kPanfrostWaitBO) {');b=source.index('    if(selector==kPanfrostSubmit',a)
 (d/'WaitBO.production.inc').write_text(source[a:b])
 exe=d/'sync'
 subprocess.run(['g++','-std=c++17','-pthread','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(d),'-I'+str(x/'iokit/Kernel/arm'),str(r/'test-mirror-consumer-sync.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
