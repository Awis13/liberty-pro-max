#!/usr/bin/env python3
"""Summarises a Time Profiler trace of the game by thread and function.

Usage: python3 -I tools/profile_summary.py TRACE.trace [threads] [top]

Exports the time-profile table with `xctrace export` (set DEVELOPER_DIR
when Xcode is not the selected developer directory) and prints, for the
`threads` busiest threads (default 6), their share of all samples, the
functions with the most self time and the most inclusive time (lifted
game functions appear as recomp::gen::f_<VA>), and for each thread the
split between game code, the runtime's HLE and the rest.
"""
import collections
import subprocess
import sys
import xml.etree.ElementTree as ET


def export(trace):
    xpath = '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]'
    return subprocess.run(['xcrun', 'xctrace', 'export', '--input', trace, '--xpath', xpath],
                          capture_output=True, check=True).stdout


def category(name, binary):
    if 'recomp::gen::' in name:
        return 'game (lifted)'
    if binary in ('librecomp_d3d9.dylib', 'librecomp_gfx.dylib'):
        return 'd3d9 / gfx layer'
    if binary == 'librecomp_x87.dylib':
        return 'x87'
    if binary.startswith('librecomp_') or binary == 'gtaiv':
        return 'runtime other'
    if binary in ('libSDL3.0.dylib', 'Metal', 'IOGPU', 'QuartzCore') or binary.startswith('AGX'):
        return 'Metal / SDL'
    if binary in ('libsystem_kernel.dylib', 'libsystem_pthread.dylib'):
        return 'kernel (waits, syscalls)'
    return 'system / other'


def main():
    trace = sys.argv[1]
    nthreads = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 15
    root = ET.fromstring(export(trace))
    ids = {}  # id -> element (xctrace writes each value once, then refers to it)

    def resolve(e):
        if e is None:
            return None
        if 'ref' in e.attrib:
            return ids.get(e.attrib['ref'])
        if 'id' in e.attrib:
            ids[e.attrib['id']] = e
        for child in e:
            resolve(child)
        return e

    per_thread = collections.Counter()
    self_t = collections.defaultdict(collections.Counter)
    incl_t = collections.defaultdict(collections.Counter)
    cat_t = collections.defaultdict(collections.Counter)
    total = 0
    for row in root.iter('row'):
        thread = resolve(row.find('thread'))
        bt = resolve(row.find('tagged-backtrace'))
        if bt is None:
            bt = resolve(row.find('backtrace'))
        weight = resolve(row.find('weight'))
        w = int(weight.text) if weight is not None and weight.text else 1
        tname = thread.attrib.get('fmt', '?') if thread is not None else '?'
        frames = []  # (name, binary), innermost first
        if bt is not None:
            for f in bt.findall('frame'):
                f = resolve(f)
                if f is None:
                    continue
                b = resolve(f.find('binary'))
                frames.append((f.attrib.get('name', '?'), b.attrib.get('name', '?') if b is not None else '?'))
        total += w
        per_thread[tname] += w
        if frames:
            self_t[tname][frames[0][0]] += w
            cat_t[tname][category(*frames[0])] += w
            for name in set(n for n, _ in frames):
                incl_t[tname][name] += w
    print('samples (weight) total: %d' % total)
    for tname, w in per_thread.most_common(nthreads):
        print('\n=== %s: %.1f%% of all' % (tname, 100.0 * w / total))
        print('  self time by kind: ' + ', '.join(
            '%s %.0f%%' % (k, 100.0 * v / w) for k, v in cat_t[tname].most_common()))
        print('  top self:')
        for name, v in self_t[tname].most_common(top):
            print('    %5.1f%%  %s' % (100.0 * v / w, name[:110]))
        print('  top inclusive (game functions):')
        game = [(n, v) for n, v in incl_t[tname].most_common() if 'gen::f_' in n]
        for name, v in game[:top]:
            print('    %5.1f%%  %s' % (100.0 * v / w, name[:110]))


if __name__ == '__main__':
    main()
