#!/usr/bin/env python3
"""Compile every function of a metallib for the Mali-G71 into one shader cache.

  build-shaders.py METALLIB AIR2NIR OUT.s8sc [--only NAME,...] [-j N]

CoreAnimation's shaders (QuartzCore default.metallib) are compiled once on
the build host with air2nir -D: function constants are read at run time
from a uniform buffer, so a single binary per function serves every
specialisation CoreAnimation asks for.

Varyings are linked by name. A vertex and a fragment function must agree
on the slot of each name, so all functions share one table; there are more
names than the 32 VAR slots, so names that never meet in one function share
a slot (greedy colouring of the "used together" graph).

The cache also holds "@preload", the frame shader the driver uses to reload
a render target (MTLLoadActionLoad).

Output (little endian): "S8SC", u32 version=1, u32 count, count x
{ char name[64], u32 offset, u32 size } (offsets from the file start),
then the air2nir S8SH blobs.
"""
import os, re, struct, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metallib

VAR_SLOTS = 32
VARYING = re.compile(rb'!"air\.(?:vertex_output|fragment_input)", !"([^"]+)"')


def varyings_of(bitcode, tmp, name):
    path = os.path.join(tmp, name + '.bc')
    open(path, 'wb').write(bitcode)
    ll = subprocess.run(['llvm-dis-18', path, '-o', '-'], capture_output=True, check=True).stdout
    return sorted({m.decode() for m in VARYING.findall(ll)})


def colour(sets):
    names = sorted({n for s in sets for n in s}, key=lambda n: -sum(n in s for s in sets))
    slot = {}
    for n in names:
        used = {slot[m] for s in sets if n in s for m in s if m in slot}
        free = [i for i in range(VAR_SLOTS) if i not in used]
        if not free:
            sys.exit(f'no varying slot left for {n}')
        slot[n] = free[0]
    return slot


def main():
    args = sys.argv[1:]
    only, jobs = None, os.cpu_count() or 4
    if '--only' in args:
        i = args.index('--only')
        only = set(args[i + 1].split(','))
        del args[i:i + 2]
    if '-j' in args:
        i = args.index('-j')
        jobs = int(args[i + 1])
        del args[i:i + 2]
    lib, air2nir, out = args
    funcs = [f for f in metallib.parse(open(lib, 'rb').read()) if f['type'] in ('vertex', 'fragment')]
    with tempfile.TemporaryDirectory() as tmp:
        sets = [varyings_of(f['bitcode'], tmp, f['name']) for f in funcs]
        slots = colour(sets)
        table = ','.join(f'{n}={s}' for n, s in sorted(slots.items(), key=lambda x: x[1]))
        todo = [f for f in funcs if only is None or f['name'] in only]
        todo.append({'name': '@preload'})       # the driver's render target reload shader

        def build(f):
            o = os.path.join(tmp, f['name'] + '.s8sh')
            r = subprocess.run([air2nir, lib, f['name'], o, '-D', '-v', table],
                               capture_output=True, text=True)
            msg = [l for l in r.stderr.splitlines() if 'unsupported' in l or 'error' in l.lower()
                   or 'air2nir: fail' in l]
            ok = r.returncode == 0 and os.path.exists(o)
            return f['name'], (open(o, 'rb').read() if ok else None), r.returncode, msg, r.stderr

        results = list(ThreadPoolExecutor(jobs).map(build, todo))
    good = [(n, b) for n, b, *_ in results if b is not None]
    for n, b, rc, msg, err in results:
        if b is None:
            last = (msg or err.strip().splitlines()[-1:] or ['?'])
            print(f'FAIL {n} (exit {rc}): {"; ".join(sorted(set(last)))[:300]}')
    head = 12 + 72 * len(good)
    blob, index, off = b'', b'', head
    for n, b in good:
        index += n.encode()[:63].ljust(64, b'\0') + struct.pack('<II', off, len(b))
        blob += b
        off += len(b)
    with open(out, 'wb') as fh:
        fh.write(b'S8SC' + struct.pack('<II', 1, len(good)) + index + blob)
    print(f'{out}: {len(good)}/{len(todo)} functions, {off} bytes, {len(slots)} varyings in '
          f'{max(slots.values()) + 1} slots')


if __name__ == '__main__':
    main()
