from pathlib import Path
import subprocess,sys,tempfile
x=Path(sys.argv[1]);root=Path(__file__).resolve().parent
client=(x/'iokit/Kernel/arm/DreamPanfrostClient.inc').read_text()
call='if(selector==kPanfrostSubmit&&r.submit.jc) dpf_bind_render_cpu();'
assert client.count(call)==1
assert client.index(call)<client.index('IOLockLock(gMaliLock);',client.index(call))<client.index('if (fRequests++')
with tempfile.TemporaryDirectory() as d:
 out=Path(d)/'test'
 subprocess.run(['g++','-std=c++17','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-iquote',str(x/'osfmk/kern'),'-iquote',str(x/'iokit/Kernel/arm'),str(root/'test-render-cpu.cpp'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
