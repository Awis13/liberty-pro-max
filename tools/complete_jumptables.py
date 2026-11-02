#!/usr/bin/env python3
"""Fills in the switch tables Ghidra left without targets.

Usage: python3 -I tools/complete_jumptables.py GTAIV.exe JUMPTABLES.tsv

JUMPTABLES.tsv is ExportJumpTables' output (jmp<TAB>targets, targets
comma separated, empty when Ghidra could not resolve the table). Every
`jmp dword ptr [table + reg*4]` in .text whose line has no targets gets them
from the image: the table's size comes from the bound check before the jump
(`cmp reg, N` / `push N; pop r2; cmp reg, r2`, then `ja`), or, without one,
entries are read while they point into .text near the jump (at most 256).
The original file is kept as JUMPTABLES.tsv.ghidra; the completed one
replaces it. Disassembly by llvm-objdump (AT&T syntax).
"""
import os
import re
import struct
import subprocess
import sys


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    n = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    out = []
    for i in range(n):
        o = pe + 24 + opt + 40 * i
        name = data[o:o + 8].rstrip(b'\0').decode()
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        out.append((name, base + va, max(vsize, rsize), raw, rsize))
    return out


def reader(data, secs):
    def rd32(va):
        for _, sva, size, raw, rsize in secs:
            if sva <= va and va + 4 <= sva + size:
                off = va - sva
                if off + 4 > rsize:
                    return None
                return struct.unpack_from('<I', data, raw + off)[0]
        return None
    return rd32


JMP = re.compile(r'^\s*([0-9a-f]+):\s+jmpl\s+\*0x([0-9a-f]+)\(,%(e[a-z]{2}),4\)')
CMP_IMM = re.compile(r'cmpl\s+\$0x([0-9a-f]+),\s*%(e[a-z]{2})$')
CMP_REG = re.compile(r'cmpl\s+%(e[a-z]{2}),\s*%(e[a-z]{2})$')
PUSH = re.compile(r'pushl\s+\$0x([0-9a-f]+)$')
POP = re.compile(r'popl\s+%(e[a-z]{2})$')
INSN = re.compile(r'^\s*([0-9a-f]+):\s+(.*)$')


def bound(prev, reg):
    """Number of entries from the cmp/ja just before the jump, or None."""
    if not prev or not prev[-1].startswith('ja'):
        return None
    pushed = {}
    for i, text in enumerate(prev[:-1]):
        m = PUSH.search(text)
        if m and i + 1 < len(prev) - 1:
            p = POP.search(prev[i + 1])
            if p:
                pushed[p.group(1)] = int(m.group(1), 16)
    for text in reversed(prev[:-1]):
        m = CMP_IMM.search(text)
        if m and m.group(2) == reg:
            return int(m.group(1), 16) + 1
        m = CMP_REG.search(text)  # AT&T: cmpl %src, %dst compares dst with src
        if m and m.group(2) == reg and m.group(1) in pushed:
            return pushed[m.group(1)] + 1
    return None


def main():
    exe, tsv = sys.argv[1], sys.argv[2]
    data = open(exe, 'rb').read()
    secs = sections(data)
    rd32 = reader(data, secs)
    text = next(s for s in secs if s[0] == '.text')
    tlo, thi = text[1], text[1] + text[2]

    keep = tsv + '.ghidra'
    if not os.path.exists(keep):
        os.replace(tsv, keep)
    rows = [l.rstrip('\n').split('\t') for l in open(keep)]
    header, rows = rows[0], rows[1:]
    known = {int(r[0], 16): (r[1] if len(r) > 1 else '') for r in rows}

    tool = os.environ.get('OBJDUMP', 'llvm-objdump')
    dis = subprocess.run([tool, '-d', '--no-show-raw-insn', exe], capture_output=True,
                         text=True, check=True).stdout.splitlines()
    filled = from_bound = 0
    prev = []
    for line in dis:
        m = JMP.match(line)
        if m:
            jva, table, reg = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
            if not known.get(jva):
                n = bound(prev, reg)
                targets = []
                for i in range(n or 256):
                    t = rd32(table + 4 * i)
                    if t is None or not (tlo <= t < thi) or (n is None and abs(t - jva) > 0x10000):
                        break
                    if t not in targets:  # cases sharing a label: listed once
                        targets.append(t)
                if targets:
                    known[jva] = ','.join('%08X' % t for t in targets)
                    filled += 1
                    from_bound += n is not None
        im = INSN.match(line)
        if im:
            prev = (prev + [im.group(2).strip()])[-8:]
    with open(tsv, 'w') as f:
        f.write('\t'.join(header) + '\n')
        for jva in sorted(known):
            f.write('%08X\t%s\n' % (jva, known[jva]))
    print('jump tables filled: %d (%d sized by their bound check)' % (filled, from_bound))


if __name__ == '__main__':
    main()
