from pathlib import Path
import tempfile,subprocess,sys
root=Path(__file__).resolve().parent
s=(root/'eagl.c').read_text()
with tempfile.TemporaryDirectory(prefix='group-mask-') as tmp:
    tmp=Path(tmp)
    for name,start,end in [
        ('ShaderParts.production.inc','static int full_precision =', 'static GLuint\nmy_glCreateShader('),
        ('GroupBeginEnd.production.inc','static int\ngroup_draw_begin(', 'static bool\nplain_draw_begin('),
        ('ShaderSource.production.inc','static void\nmy_glShaderSource(', 'static void\nsay_long(')]:
        a=s.index(start);b=s.index(end,a);(tmp/name).write_text(s[a:b])
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(root),'-I'+str(tmp),str(root/'test-group-mask.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test'),str(tmp)],check=True)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(root),'-I'+str(tmp),str(root/'test-group-mask-state.cpp'),'-o',str(tmp/'state')],check=True)
    subprocess.run([str(tmp/'state')],check=True)
    if len(sys.argv)>1:
        subprocess.run([sys.executable,str(root/'test-group-mask-render.py'),str(tmp)],check=True)
