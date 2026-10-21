#!/usr/bin/env python3
"""Pack files for the Galaxy S8 boot overlay (bsd/dev/dreamlte_s8.c).

  mkoverlay.py OUT.ovl ENTRY...

ENTRY is  dir:/abs/path:MODE:UID:GID  or  file:/abs/path:MODE:UID:GID:LOCAL_FILE
(MODE octal). The kernel creates directories (existing ones only get their
owner and mode) and writes files, in the order given, before launchd.

Archive: "S8OVL1\\0\\0", u32 count, u32 0, then per entry u32 kind (1 dir,
2 file), mode, uid, gid, u64 size, u32 pathlen (NUL included), u32 0, the
path padded to 8, the data padded to 8.
"""
import struct, sys


def pad8(b):
    return b + b'\0' * (-len(b) & 7)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = bytearray()
    entries = sys.argv[2:]
    for e in entries:
        f = e.split(':')
        kind, path, mode, uid, gid = f[0], f[1], int(f[2], 8), int(f[3]), int(f[4])
        if not path.startswith('/'):
            sys.exit(f'{path}: path must be absolute')
        data = b''
        if kind == 'file':
            data = open(f[5], 'rb').read()
        elif kind != 'dir':
            sys.exit(f'{e}: unknown kind')
        p = path.encode() + b'\0'
        out += struct.pack('<4IQII', 1 if kind == 'dir' else 2, mode, uid, gid, len(data), len(p), 0)
        out += pad8(p) + pad8(data)
        print(f'{kind:4} {path} {mode:o} {uid}:{gid} {len(data)} bytes')
    open(sys.argv[1], 'wb').write(b'S8OVL1\0\0' + struct.pack('<II', len(entries), 0) + out)


if __name__ == '__main__':
    main()
