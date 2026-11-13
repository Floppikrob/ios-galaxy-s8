from pathlib import Path
import tempfile,subprocess,os
root=Path(__file__).resolve().parent;s=(root/'eagl.c').read_text()
with tempfile.TemporaryDirectory(prefix='mask-trace-') as tmp:
    tmp=Path(tmp)
    a=s.index('typedef unsigned int GLenum');b=s.index('#define VBLUR_LEVELS',a);(tmp/'MaskGL.production.inc').write_text(s[a:b])
    a=s.index('static unsigned\nuniform_floats(');b=s.index('/*',a);(tmp/'MaskUniform.production.inc').write_text(s[a:b])
    subprocess.run(['gcc','-std=c11','-DDREAM_MASK_TRACE=1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root),'-I'+str(tmp),str(root/'test-mask-trace.c'),'-Wl,-l:libGL.so.1','-Wl,-l:libEGL.so.1','-lm','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))

# The default build must compile with only draw entry points: any accidental
# diagnostic query/read/map dependency makes this fixture fail to link.
with tempfile.TemporaryDirectory(prefix='mask-trace-off-') as tmp:
    tmp=Path(tmp)
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root),str(root/'test-mask-trace-off.c'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
