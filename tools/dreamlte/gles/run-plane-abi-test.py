from pathlib import Path
import tempfile,subprocess,sys
root=Path(__file__).resolve().parent;xnu=Path(sys.argv[1])
eagl=(root/'eagl.c').read_text()
def run(source,before=False):
    s=Path(source).read_text()
    with tempfile.TemporaryDirectory(prefix='s8-plane-abi-') as tmp:
        tmp=Path(tmp)
        a=s.index('static void\nsweet_surface_fill_result(');b=s.index('\nstatic IOReturn\nsweet_struct_out',a)
        (tmp/'PlaneResult.production.inc').write_text(s[a:b])
        a=eagl.index('static bool\nb3a8_planes(');b=eagl.index('/* Fill a texture, which is bound,',a)
        (tmp/'B3a8Planes.production.inc').write_text(eagl[a:b])
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-multichar','-fsanitize=address,undefined',
            '-I'+str(root),'-I'+str(tmp),str(root/'test-plane-abi.cpp'),'-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test')]+(['--expect-reject'] if before else []),check=True)
if len(sys.argv)>2:run(sys.argv[2],True)
run(xnu/'iokit/Kernel/arm/SweetSurface.cpp')
