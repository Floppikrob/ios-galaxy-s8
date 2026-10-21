#!/usr/bin/env python3
"""Read an Apple .metallib (MTLB container): list functions, extract AIR bitcode.

  metallib.py LIB                 list functions (name, type, bitcode size)
  metallib.py LIB OUTDIR          also write OUTDIR/<name>.bc for each function

Layout (format as documented by the MetalLibraryArchive project): header
"MTLB", u16 platform, u16 version..., u64 file size at 0x10, then
(offset, size) u64 pairs for the function list (0x18), public metadata
(0x28), private metadata (0x38) and bitcode (0x48). The function list is
u32 count, then per function u32 length and tagged fields (4-char tag,
u16 size, data) up to ENDT: NAME, TYPE (0 vertex, 1 fragment, 2 kernel...),
HASH, MDSZ (u64 bitcode size), OFFT (three u64 offsets into the public
metadata, private metadata and bitcode sections), VERS, ...
"""
import os, struct, sys

TYPES = {0: 'vertex', 1: 'fragment', 2: 'kernel', 3: 'unqualified', 4: 'visible',
         5: 'extern', 6: 'intersection', 7: 'mesh', 8: 'object'}


def parse(data):
    assert data[:4] == b'MTLB', 'not a metallib'
    (flist, _), (pub, _), (priv, _), (bc, _) = (struct.unpack_from('<QQ', data, o) for o in (0x18, 0x28, 0x38, 0x48))
    count, = struct.unpack_from('<I', data, flist)
    off = flist + 4
    funcs = []
    for _ in range(count):
        length, = struct.unpack_from('<I', data, off)
        p, end = off + 4, off + length
        f = {}
        while p < end:
            tag = data[p:p + 4].decode('latin1')
            if tag == 'ENDT':
                break
            size, = struct.unpack_from('<H', data, p + 4)
            v = data[p + 6:p + 6 + size]
            if tag == 'NAME':
                f['name'] = v.rstrip(b'\0').decode()
            elif tag == 'TYPE':
                f['type'] = TYPES.get(v[0], v[0])
            elif tag == 'MDSZ':
                f['size'], = struct.unpack('<Q', v)
            elif tag == 'OFFT':
                f['pub'], f['priv'], f['bc'] = struct.unpack('<3Q', v)
            else:
                f.setdefault('tags', {})[tag] = v
            p += 6 + size
        f['bitcode'] = data[bc + f['bc']:bc + f['bc'] + f['size']]
        funcs.append(f)
        off = end
    return funcs


def main():
    data = open(sys.argv[1], 'rb').read()
    funcs = parse(data)
    for f in funcs:
        print(f"{f['type']:<10} {f['size']:>7} {f['name']}")
    print(f'{len(funcs)} functions')
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        for f in funcs:
            open(os.path.join(sys.argv[2], f['name'] + '.bc'), 'wb').write(f['bitcode'])


if __name__ == '__main__':
    main()
