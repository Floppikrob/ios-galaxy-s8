from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'transport'
 subprocess.run(['g++','-std=c++17','-pthread','-Wno-attributes','-fsanitize=address,undefined','-I'+str(r),str(r/'test-display-mirror-transport.cpp'),'-o',str(exe)],check=True)
 for arg in [[],['no-service'],['open-fail']]:subprocess.run([str(exe),*arg],check=True)
