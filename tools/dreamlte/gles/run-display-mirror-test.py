from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).resolve().parent;x=Path(sys.argv[1])
s=(x/'iokit/Kernel/arm/SweetFramebuffer.cpp').read_text()
h=(x/'iokit/Kernel/arm/SweetSurface.h').read_text();c=(x/'iokit/Kernel/arm/SweetSurface.cpp').read_text()
assert 'displayMirror=NULL;' in h and 'OSSafeReleaseNULL(displayMirror);' in c
a=s.index('static SweetSurface *dream_b3a8_stage;');b=s.index('static void sweet_stage_probe(',a)
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'B3a8Display.production.inc').write_text(s[a:b])
 cpu=(x/'iokit/Kernel/arm/SweetSurface.cpp').read_text();a=cpu.index('IOReturn\nIOSurfaceRootUserClient::trapLock(');b=cpu.index('IOReturn\nIOSurfaceRootUserClient::trapRelease(',a)
 (d/'CPULocks.production.inc').write_text(cpu[a:b])
 exe=d/'mirror'
 subprocess.run(['g++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(r),'-I'+str(d),'-I'+str(x/'iokit/Kernel/arm'),str(r/'test-display-mirror.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
