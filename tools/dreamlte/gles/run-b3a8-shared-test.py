from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parent
s=(root/'eagl.c').read_text()
with tempfile.TemporaryDirectory(prefix='b3a8-shared-') as tmp:
    tmp=Path(tmp)
    for name,start,end in [
        ('B3a8Planes.production.inc','static bool\nb3a8_planes(', '/* Fill a texture, which is bound,'),
        ('B3a8Mirror.production.inc','static bool\nb3a8_mirror_output(', '/* What has been drawn into a texture')]:
        a=s.index(start);b=s.index(end,a);(tmp/name).write_text(s[a:b])
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-multichar','-Wno-missing-field-initializers',
        '-fsanitize=address,undefined','-I'+str(root),'-I'+str(tmp),str(root/'test-b3a8-shared.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
