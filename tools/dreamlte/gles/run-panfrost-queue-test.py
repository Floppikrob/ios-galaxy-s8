from pathlib import Path
import subprocess,sys,tempfile
r=Path(__file__).resolve().parent;x=Path(sys.argv[1])
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'queue'
 subprocess.run(['g++','-std=c++17','-pthread','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(x/'iokit/Kernel/arm'),str(r/'test-panfrost-queue.cpp'),'-o',str(exe)],check=True)
 for arg in [[],['startfail'],['allocfail']]:subprocess.run([str(exe),*arg],check=True)
