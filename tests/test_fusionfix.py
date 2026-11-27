import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
from zipfile import ZipFile


spec = importlib.util.spec_from_file_location(
    "fusionfix", Path(__file__).resolve().parents[1] / "tools/fusionfix.py"
)
fusionfix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fusionfix)


class MenuOverlayTests(unittest.TestCase):
    def test_keeps_stock_screens_and_only_ports_implemented_preferences(self):
        stock = """<FrontendMenu version="1">
          <sMenuDisplayValue><menu enum="MENU_DISPLAY_ON_OFF"/></sMenuDisplayValue>
          <sMenuScreen>
            <menupc enum="SCR_CONTROLS"><optionspc label="MO_KBOPT"/></menupc>
            <menupc enum="SCR_GAME"><optionspc label="MO_EXITGAM"/></menupc>
          </sMenuScreen>
        </FrontendMenu>"""
        fusion = b"""<FrontendMenu version="1"><sMenuScreen>
          <menupc enum="SCR_CONTROLS">
            <optionspc value="PREF_ALWAYSRUN" label="Always Run"/>
            <optionspc value="PREF_RAWINPUT" label="Raw Input"/>
          </menupc>
          <menupc enum="SCR_GAME"><optionspc value="PREF_ALWAYSRUN" label="Test"/></menupc>
          <menupc enum="SCR_UNSUPPORTED"><optionspc value="PREF_ALWAYSRUN"/></menupc>
        </sMenuScreen></FrontendMenu>"""
        with tempfile.TemporaryDirectory() as tmp:
            original = Path(tmp) / "frontend_menus.xml"
            original.write_text(stock)
            menu = fusionfix.merge_menu(original, fusion, {"PREF_ALWAYSRUN"})
        self.assertEqual(
            [x.get("label") for x in menu.findall("sMenuScreen/menupc/*")],
            ["MO_KBOPT", "Always Run", "Test", "MO_EXITGAM"],
        )
        self.assertIsNotNone(menu.find("sMenuDisplayValue/menu[@enum='MENU_DISPLAY_ON_OFF']"))

    def test_custom_displays_use_free_native_slots_for_both_menus(self):
        stock = """<FrontendMenu><sMenuDisplayValue>
          <menu enum="MENU_DISPLAY_EXTRA_0"/>
        </sMenuDisplayValue><sMenuScreen>
          <menupc enum="SCR_ADVANCED"/><menupc enum="SCR_TGRAPHICS"/>
        </sMenuScreen></FrontendMenu>"""
        fusion = b"""<FrontendMenu><sMenuDisplayValue>
          <menupc enum="MENU_DISPLAY_FRAMELIMIT">
            <options text="MO_OFF" action="ACTION_NONE" value="0"/>
            <options text="30" action="ACTION_NONE" value="2"/>
          </menupc>
        </sMenuDisplayValue><sMenuScreen>
          <menupc enum="SCR_ADVANCED">
            <optionspc value="PREF_FPS_LIMIT_PRESET" displayValue="MENU_DISPLAY_FRAMELIMIT"/>
          </menupc>
          <menupc enum="SCR_TGRAPHICS">
            <optionspc value="PREF_FPS_LIMIT_PRESET" displayValue="MENU_DISPLAY_FRAMELIMIT"/>
          </menupc>
        </sMenuScreen></FrontendMenu>"""
        with tempfile.TemporaryDirectory() as tmp:
            original = Path(tmp) / "frontend_menus.xml"
            original.write_text(stock)
            menu = fusionfix.merge_menu(original, fusion, {"PREF_FPS_LIMIT_PRESET"})
        self.assertEqual(
            [x.get("enum") for x in menu.findall("sMenuDisplayValue/*")],
            ["MENU_DISPLAY_EXTRA_0", "MENU_DISPLAY_EXTRA_1"],
        )
        self.assertEqual(
            [x.get("displayValue") for x in menu.findall("sMenuScreen/*/*")],
            ["MENU_DISPLAY_EXTRA_1", "MENU_DISPLAY_EXTRA_1"],
        )

    def test_rejects_a_corrupt_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "FusionFix.zip"
            archive.write_bytes(b"incomplete download")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                fusionfix.verify(archive)

    def test_shader_rows_are_bundled_separately_for_each_episode(self):
        stock = """<FrontendMenu><sMenuDisplayValue/><sMenuScreen>
          <menupc enum="SCR_ADVANCED"/>
        </sMenuScreen></FrontendMenu>"""
        upstream = """<FrontendMenu><sMenuScreen><menupc enum="SCR_ADVANCED">
          <optionspc value="PREF_FPS_LIMIT_PRESET"/>
          <optionspc value="PREF_SHADOWFILTER"/>
        </menupc></sMenuScreen></FrontendMenu>"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = root / "game"
            archive = root / "FusionFix.zip"
            output = root / "output"
            options = root / "options.inc"
            options.write_text(
                "FUSION_OPTION(PREF_FPS_LIMIT_PRESET, FPS, 0, 12)\n"
                "FUSION_SHADER_OPTION(PREF_SHADOWFILTER, FILTER, 0, 2)\n"
            )
            episodes = (Path(), Path("TLAD"), Path("TBoGT"))
            with ZipFile(archive, "w") as release:
                for episode in episodes:
                    menu = episode / fusionfix.MENU
                    (game / menu).parent.mkdir(parents=True)
                    (game / menu).write_text(stock)
                    release.writestr((Path("update") / menu).as_posix(), upstream)
            fusionfix.prepare(game, archive, options, output)
            for episode in episodes:
                with self.subTest(episode=episode):
                    menu = episode / fusionfix.MENU
                    regular = ET.parse(output / "frontend" / menu)
                    shaders = ET.parse(output / "frontend/shaders" / menu)
                    rows = "sMenuScreen/menupc/optionspc"
                    self.assertEqual([row.get("value") for row in regular.findall(rows)], ["PREF_FPS_LIMIT_PRESET"])
                    self.assertEqual([row.get("value") for row in shaders.findall(rows)],
                                     ["PREF_FPS_LIMIT_PRESET", "PREF_SHADOWFILTER"])
