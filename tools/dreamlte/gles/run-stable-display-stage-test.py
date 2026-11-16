from pathlib import Path
import subprocess,sys,tempfile,json
r=Path(__file__).resolve().parent;x=Path(sys.argv[1])
s=(x/'iokit/Kernel/arm/SweetFramebuffer.cpp').read_text() if len(sys.argv)<3 else Path(sys.argv[2]).read_text()
a=s.index('static SweetSurface *dream_b3a8_stage;');b=s.index('static void sweet_stage_probe(',a)
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'Display.production.inc').write_text(s[a:b])
 exe=d/'stage'
 subprocess.run(['g++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(d),'-I'+str(x/'iokit/Kernel/arm'),str(r/'test-stable-display-stage.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
