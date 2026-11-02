#!/usr/bin/env python3
"""Find byte patterns in a PE's code: virtual addresses of each match.

    findpat.py GTAIV.exe "F3 0F 2C 44 24 04 ? ? 91" ["..."]

`?` (or `??`) matches any byte. Searches the executable sections only.
"""
import struct
import sys


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    off = pe + 24 + opt
    for i in range(nsec):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, off + 40 * i)
        chars = struct.unpack_from("<I", data, off + 40 * i + 36)[0]
        if chars & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            yield base + va, data[raw:raw + min(vsize, rsize)]


def find(data, pattern):
    toks = pattern.split()
    want = [None if t.strip("?") == "" else int(t, 16) for t in toks]
    first = next((i for i, b in enumerate(want) if b is not None), 0)
    hits = []
    for va, code in sections(data):
        start = 0
        while True:
            i = code.find(bytes([want[first]]), start)
            if i < 0:
                break
            j = i - first
            if j >= 0 and j + len(want) <= len(code) and all(
                    w is None or code[j + k] == w for k, w in enumerate(want)):
                hits.append(va + j)
            start = i + 1
    return hits


def main():
    data = open(sys.argv[1], "rb").read()
    for p in sys.argv[2:]:
        hits = find(data, p)
        print(f"{p}: {len(hits)} match(es): " + " ".join(f"{h:08X}" for h in hits[:8]))


if __name__ == "__main__":
    main()
