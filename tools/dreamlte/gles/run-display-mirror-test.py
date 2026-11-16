from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).resolve().parent;x=Path(sys.argv[1])
s=(x/'iokit/Kernel/arm/SweetFramebuffer.cpp').read_text()
h=(x/'iokit/Kernel/arm/SweetSurface.h').read_text();c=(x/'iokit/Kernel/arm/SweetSurface.cpp').read_text()
assert 'displayMirror=NULL;' in h and 'OSSafeReleaseNULL(displayMirror);' in c
a=s.index('static SweetSurface *dream_b3a8_stage;');b=s.index('static void sweet_stage_probe(',a)
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'B3a8Display.production.inc').write_text(s[a:b])
 blit=(x/'iokit/Kernel/arm/DreamMaliBlit.inc').read_text();a=blit.index('static bool dream_gpu_frame_source_room(');b=blit.index('static int dream_gpu_frame_copy(',a)
 (d/'MirrorRoom.production.inc').write_text(blit[a:b])
 exe=d/'mirror'
 subprocess.run(['g++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(r),'-I'+str(d),'-I'+str(x/'iokit/Kernel/arm'),str(r/'test-display-mirror.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
