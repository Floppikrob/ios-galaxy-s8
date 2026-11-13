from pathlib import Path
import tempfile,subprocess,sys
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mask-derivative-') as tmp:
    tmp=Path(tmp)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root),str(root/'test-mask-derivative.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test'),str(tmp)],check=True)
    subprocess.run([sys.executable,str(root/'test-mask-derivative-render.py'),str(tmp)],check=True)
