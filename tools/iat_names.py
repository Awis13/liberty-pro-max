#!/usr/bin/env python3
"""Names the imports at IAT addresses of a PE32 image.

Usage: python3 -I tools/iat_names.py GTAIV.exe 0xd6b468 [0x...]
"""
import struct
import sys


def main():
    d = open(sys.argv[1], 'rb').read()
    want = {int(a, 16) for a in sys.argv[2:]}
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    base = struct.unpack_from('<I', d, pe + 0x34)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        vs, va, rs, raw = struct.unpack_from('<IIII', d, o + 8)
        secs.append((va, max(vs, rs), raw))

    def off(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return raw + rva - va
        raise ValueError(hex(rva))

    o = off(struct.unpack_from('<I', d, pe + 24 + 104)[0])
    while True:
        oft, _, _, name, ft = struct.unpack_from('<IIIII', d, o)
        o += 20
        if not name:
            break
        dll = d[off(name):d.index(b'\0', off(name))].decode()
        t, i = oft or ft, 0
        while True:
            v = struct.unpack_from('<I', d, off(t) + 4 * i)[0]
            if not v:
                break
            iat = base + ft + 4 * i
            if iat in want:
                nm = '#%d' % (v & 0xffff) if v & 0x80000000 else \
                    d[off(v) + 2:d.index(b'\0', off(v) + 2)].decode()
                print('%#x %s!%s' % (iat, dll, nm))
            i += 1


if __name__ == '__main__':
    main()
