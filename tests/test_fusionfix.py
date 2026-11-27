import importlib.util
from pathlib import Path
import tempfile
import unittest


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

    def test_rejects_a_corrupt_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / "FusionFix.zip"
            archive.write_bytes(b"incomplete download")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                fusionfix.verify(archive)
