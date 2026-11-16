from pathlib import Path
import tempfile,subprocess,sys
root=Path(__file__).resolve().parent
xnu=Path(sys.argv[1]);s=(xnu/'iokit/Kernel/arm/SweetFramebuffer.cpp').read_text()
assert (root/'dream_b3a8.h').read_bytes()==(xnu/'iokit/Kernel/arm/DreamB3a8.h').read_bytes()
with tempfile.TemporaryDirectory(prefix='b3a8-display-') as tmp:
    tmp=Path(tmp);a=s.index('static SweetSurface *dream_b3a8_stage;');b=s.index('static void sweet_stage_probe(',a)
    (tmp/'B3a8Display.production.inc').write_text(s[a:b])
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(root),'-I'+str(tmp),'-I'+str(xnu/'iokit/Kernel/arm'),str(root/'test-b3a8-display.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
