from pathlib import Path
import sys,tempfile,subprocess
root=Path(__file__).resolve().parent;xnu=Path(sys.argv[1])
def run(source,before=False):
    s=Path(source).read_text()
    with tempfile.TemporaryDirectory(prefix='s8-create-address-') as tmp:
        tmp=Path(tmp)
        for name,start,end in [('CreateAddress.production.inc','IOReturn\nIOSurfaceRootUserClient::create(', '\nIOReturn\nIOSurfaceRootUserClient::wrapClientImage('),
            ('CreateAddressFree.production.inc','void\nSweetSurface::free()', '\nuintptr_t\nSweetSurface::kernelAddress')]:
            a=s.index(start);b=s.index(end,a);(tmp/name).write_text(s[a:b])
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-format','-fsanitize=address,undefined',
            '-I'+str(tmp),'-I'+str(xnu/'iokit/Kernel/arm'),str(root/'test-create-address.cpp'),'-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test')]+(['--expect-zero'] if before else []),check=True)
if len(sys.argv)>2:run(sys.argv[2],True)
run(xnu/'iokit/Kernel/arm/SweetSurface.cpp')
