"""manifest/gtaiv.manifest against src/: functions of the game the port
wraps. `replace VA name` makes the lifter emit the game's function as
f_VA_lifted and leaves f_VA to the port; a wrapper in src/ that calls
f_VA_lifted is reached by the game's callers only with that line."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def replaced():
    """The addresses the manifest has a `replace` line for, with how often."""
    count = {}
    for line in (ROOT / 'manifest/gtaiv.manifest').read_text().splitlines():
        m = re.match(r'replace\s+([0-9A-Fa-f]{8})\b', line)
        if m:
            count[m.group(1).upper()] = count.get(m.group(1).upper(), 0) + 1
    return count


def sources():
    """Every source file below src/, by its path from there."""
    src = ROOT / 'src'
    return {p.relative_to(src).as_posix(): p.read_text(errors='replace')
            for p in sorted(src.rglob('*')) if p.suffix in ('.cpp', '.h', '.mm', '.inc')}


class WrapperTests(unittest.TestCase):
    def test_every_lifted_original_named_in_src_is_replaced_once(self):
        count = replaced()
        named = set()
        for text in sources().values():
            named.update(m.upper() for m in re.findall(r'\bf_([0-9A-Fa-f]{8})_lifted\b', text))
        # The port wraps dozens of functions: fewer than this and src/ was
        # not read (moved, or the pattern no longer matches), and the loop
        # below would pass having checked nothing.
        self.assertGreater(len(named), 10, 'hardly any f_VA_lifted found below src/')
        for va in sorted(named):
            self.assertEqual(count.get(va, 0), 1,
                             f'src/ names f_{va}_lifted, so the manifest needs exactly one '
                             f'`replace {va}` line; it has {count.get(va, 0)}')

    def test_render_target_creation_is_the_ports(self):
        # 00423BA0, where the water surface's two render targets are kept
        # at the game's size (src/fixes.cpp, src/water_surface.h).
        va = '00423BA0'
        self.assertEqual(replaced().get(va, 0), 1, f'`replace {va}` lines in the manifest')
        defined = [name for name, text in sources().items()
                   if re.search(r'^void f_%s\(Ctx& c\) \{' % va, text, re.M)]
        self.assertEqual(defined, ['fixes.cpp'], f'the files below src/ that define f_{va}')
        # Not also translated by tools/x86n.py, which would define it too.
        starts = {line.split()[0].upper() for line in (ROOT / 'manifest/native.list').read_text().splitlines()
                  if line.strip()}
        # The list has hundreds of functions: fewer than this and it was
        # not read, and "not in it" would say nothing.
        self.assertGreater(len(starts), 100, 'manifest/native.list is all but empty')
        self.assertNotIn(va, starts, f'{va} is in manifest/native.list too')


if __name__ == '__main__':
    unittest.main()
