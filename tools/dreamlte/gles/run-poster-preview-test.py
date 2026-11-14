from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parent
xnu=Path(sys.argv[1])
with tempfile.TemporaryDirectory() as tmp:
    for name in ['test-poster-exec.c','test-poster-preview-bounds.c']:
        binary=Path(tmp)/name
        subprocess.run(['gcc','-D_GNU_SOURCE','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root),'-I'+str(xnu/'bsd/kern'),str(root/name),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print('PASS: actual exec helper target/preservation/independent attempt gates; bounded preview geometry')
