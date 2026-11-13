from pathlib import Path
import sys,tempfile,subprocess
source=Path(sys.argv[1]);cpp=Path(__file__).with_name('test-wrap-image.cpp')
s=(source/'iokit/Kernel/arm/SweetSurface.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='s8-wrap-contract-') as tmp:
 tmp=Path(tmp)
 a=s.index('void\nSweetSurface::free()');b=s.index('\nuintptr_t\nSweetSurface::kernelAddress',a);(tmp/'SurfaceFree.production.inc').write_text(s[a:b])
 a=s.index('static void\nsweet_surface_fill_result(');b=s.index('\nstatic IOReturn\nsweet_struct_out',a);(tmp/'SurfaceResult.production.inc').write_text(s[a:b])
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(tmp),'-I'+str(source/'iokit/Kernel/arm'),str(cpp),'-o',str(tmp/'test')],check=True)
 subprocess.run([str(tmp/'test')],check=True)
