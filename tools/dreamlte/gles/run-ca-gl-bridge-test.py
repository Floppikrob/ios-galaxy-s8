from pathlib import Path
import tempfile,subprocess,re
root=Path(__file__).resolve().parent;s=(root/'ca-gl-bridge.c').read_text()
with tempfile.TemporaryDirectory(prefix='s8-ca-bridge-') as tmp:
 tmp=Path(tmp);(tmp/'CaHooks.production.inc').write_text(re.sub(r'^#include.*\n','',s,flags=re.M))
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(tmp),str(root/'test-ca-gl-bridge.cpp'),'-o',str(tmp/'test')],check=True)
 subprocess.run([str(tmp/'test')],check=True)
