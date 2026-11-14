from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'staged'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-multichar','-Wno-missing-field-initializers','-fsanitize=address,undefined','-I'+str(r),str(r/'test-staged-upload.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
