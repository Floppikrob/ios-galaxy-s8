from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).resolve().parent;x=Path(sys.argv[1]);s=(x/'iokit/Kernel/arm/DreamPanfrostClient.inc').read_text()
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 for name,start,end in [('WCCreate.production.inc','IOReturn\nHakiosMaliUserClient::createBO(', '/*\n * The pixels of an IOSurface'),('WCMap.production.inc','IOReturn\nHakiosMaliUserClient::clientMemoryForType(', 'IOReturn\nHakiosMaliUserClient::externalMethod(')]:
  a=s.index(start);b=s.index(end,a);(d/name).write_text(s[a:b])
 exe=d/'wc'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-format','-fsanitize=address,undefined','-I'+str(d),'-I'+str(x/'iokit/Kernel/arm'),str(r/'test-panfrost-wc.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
