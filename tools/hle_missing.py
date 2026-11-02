#!/usr/bin/env python3
"""Imports of the lifted modules that x86recomp's HLE does not implement yet.

Usage: python3 -I tools/hle_missing.py IMPORTS.tsv vendor/x86recomp/src/runtime/hle
IMPORTS.tsv: module<TAB>dll<TAB>name (or #ordinal), one line per import.
"""
import sys,os,re,collections
rows=[l.rstrip('\n').split('\t') for l in open(sys.argv[1])]
src=''
for f in os.listdir(sys.argv[2]):
    if f.endswith('.cpp'): src+=open(os.path.join(sys.argv[2],f),errors='ignore').read()
have=set()
for m in re.finditer(r'\bHLE(?:64|32)?\(\s*"([^"]+)"\s*,\s*([A-Za-z_0-9]+)',src): have.add((m.group(1).lower(),m.group(2)))
for m in re.finditer(r'\bHLE_NAMED[A-Z0-9]*\(\s*"([^"]+)"\s*,\s*"([^"]+)"',src): have.add((m.group(1).lower(),m.group(2)))
for m in re.finditer(r'\bRECOMP_WS2\(\s*(\d+)\s*,\s*([A-Za-z_0-9]+)',src): have|={('ws2_32.dll',m.group(2)),('ws2_32.dll','#'+m.group(1))}
for m in re.finditer(r'Registrar\s+\w+\{\s*"([^"]+)"\s*,\s*"([^"]+)"',src): have.add((m.group(1).lower(),m.group(2)))
lifted=set()
miss=collections.defaultdict(set)
for mod,dll,fn in rows:
    d=dll.lower()
    if d in lifted: continue
    if (d,fn) not in have: miss[d].add(fn)
tot=sum(len(v) for v in miss.values()); print('HLE entries',len(have),'missing',tot)
for d in sorted(miss,key=lambda k:-len(miss[k])): print(f"{d:14} {len(miss[d]):3}  {' '.join(sorted(miss[d]))}")
