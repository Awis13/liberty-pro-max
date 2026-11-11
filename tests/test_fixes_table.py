"""tools/fixes_table.py: the launcher's options as the README lists them."""
import importlib.util
from pathlib import Path
import re
import unittest


spec = importlib.util.spec_from_file_location(
    "fixes_table", Path(__file__).resolve().parents[1] / "tools/fixes_table.py"
)
fixes_table = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixes_table)

SRC = r'''
const Fix kFixes[] = {
    {@"FixLadders", "GTAIV_LADDER_FIX", @"Ladders: no stalls"},
    {@"DeathMusic", "GTAIV_DEATH_MUSIC", @"Death music \"cut\"", false},
    {@"TreeShadows", "GTAIV_TREE_SHADOWS", @"Trees cast shadows", true, false, true},
    {@"Episodes", "GTAIV_EPISODES", @"The episodes", false, true},
    {@"EpisodeRadioOwn", "GTAIV_EPISODE_RADIO_OWN", @"Episodes' radio songs", true, true},
    {@"Mystery", "GTAIV_MYSTERY", @"Not in a group"},
};
const ShaderChoice kShaderChoices[] = {
    {@"FFMotionBlur", "GTAIV_MOTION_BLUR", @"Motion blur:", @[ @"Off", @"Full" ], 1},
};
'''


class ParseTests(unittest.TestCase):
    def test_fields(self):
        fixes, choices = fixes_table.parse(SRC)
        got = {f.env: (f.title, f.on, f.extra, f.hidden) for f in fixes}
        self.assertEqual(got['LADDER_FIX'], ('Ladders: no stalls', True, False, False))
        self.assertEqual(got['DEATH_MUSIC'], ('Death music "cut"', False, False, False))
        self.assertEqual(got['TREE_SHADOWS'], ('Trees cast shadows', True, False, True))
        self.assertEqual(got['EPISODES'], ('The episodes', False, True, False))
        self.assertEqual(got['EPISODE_RADIO_OWN'], ("Episodes' radio songs", True, True, False))
        self.assertEqual(choices, [('MOTION_BLUR', 'Motion blur', ['Off', 'Full'], 1)])


class TableTests(unittest.TestCase):
    def setUp(self):
        self.lines = fixes_table.table(*fixes_table.parse(SRC))

    def group(self, title):
        start = self.lines.index(f'#### {title}')
        end = next((i for i in range(start + 1, len(self.lines)) if self.lines[i].startswith('#### ')),
                   len(self.lines))
        return [l for l in self.lines[start:end] if l.startswith('| ') and l != '| Option | Default |']

    def test_extras_have_their_own_group_with_their_defaults(self):
        self.assertEqual(self.group('Extras'),
                         ['| The episodes | off |', "| Episodes' radio songs | on |"])

    def test_extras_and_hidden_switches_stay_out_of_the_other_groups(self):
        text = '\n'.join(l for l in self.lines if l not in self.group('Extras'))
        self.assertNotIn('The episodes', text)
        self.assertNotIn('Trees cast shadows', text)

    def test_the_rest_in_their_groups(self):
        self.assertEqual(self.group('High frame rates'), ['| Ladders: no stalls | on |'])
        self.assertIn('| Death music "cut" | off |', self.group('Gameplay'))
        self.assertEqual(self.group('Other'), ['| Not in a group | on |'])
        self.assertIn('| Motion blur: Off, Full | Full |', self.group('Lights, shadows and effects'))

    def test_count_includes_the_extras(self):
        self.assertTrue(self.lines[0].startswith('6 switches and choices'), self.lines[0])

    def test_extras_group_comes_last(self):
        heads = [l for l in self.lines if l.startswith('#### ')]
        self.assertEqual(heads[-1], '#### Extras')


class ReadmeTests(unittest.TestCase):
    def test_readme_matches_the_options(self):
        root = Path(__file__).resolve().parents[1]
        lines = fixes_table.table(*fixes_table.parse((root / 'src/launcher_options.h').read_text()))
        readme = (root / 'README.md').read_text()
        a = readme.index('<!-- fixes:begin -->') + len('<!-- fixes:begin -->')
        b = readme.index('<!-- fixes:end -->')
        self.assertEqual(readme[a:b], '\n' + '\n'.join(lines) + '\n')


class FallbackTests(unittest.TestCase):
    """Each OptionOn("GTAIV_X", fallback) in src/ falls back to the launcher
    table's default: a run without the launchers' environment (the plain
    executable) gets what the table says."""

    # Read with OptionOn but not switches of the table, with why.
    NOT_IN_TABLE = {
        'GTAIV_FF_SHADERS': 'the extended shaders, unfinished: no launcher option',
        'GTAIV_TURN_LOG': 'a log of the turn indicators for development',
        'GTAIV_MODS': "the General tab's Load mods (kModsKey), on, not a kFixes row",
    }

    def test_fallbacks_match_the_table(self):
        root = Path(__file__).resolve().parents[1]
        fixes, _ = fixes_table.parse((root / 'src/launcher_options.h').read_text())
        table = {'GTAIV_' + f.env: f.on for f in fixes}
        seen = set()
        for path in sorted((root / 'src').iterdir()):
            if path.suffix not in ('.cpp', '.mm', '.h'):
                continue
            for m in re.finditer(r'OptionOn\(\s*"(GTAIV_\w+)",\s*(true|false)\s*\)', path.read_text(errors='replace')):
                env, fallback = m.group(1), m.group(2) == 'true'
                seen.add(env)
                if env in self.NOT_IN_TABLE:
                    continue
                self.assertIn(env, table, f'{path.name}: {env} is in neither the table nor NOT_IN_TABLE')
                self.assertEqual(fallback, table[env], f'{path.name}: {env} falls back to {fallback}, '
                                                       f'the table says {table[env]}')
        self.assertIn('GTAIV_EPISODES', seen)
        self.assertIn('GTAIV_PHONE_CHEATS', seen)
        for env in self.NOT_IN_TABLE:
            self.assertIn(env, seen, f'{env} is listed but no longer read with OptionOn')
            self.assertNotIn(env, table)


if __name__ == '__main__':
    unittest.main()
