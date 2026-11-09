"""Transfer-plan regressions; no Apple device or game installation required."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "ios_push_game", Path(__file__).resolve().parents[1] / "tools/ios_push_game.py"
)
upload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upload)


class TransferPlanTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def plan(self, relative):
        return list(upload.plan(self.root, relative))

    def test_empty_directory_is_skipped(self):
        (self.root / "empty").mkdir()
        self.assertEqual(self.plan("empty"), [])

    def test_empty_directory_tree_is_skipped(self):
        (self.root / "empty/nested/leaf").mkdir(parents=True)
        self.assertEqual(self.plan("empty"), [])

    def test_zero_byte_file_is_preserved(self):
        (self.root / "zero").touch()
        self.assertEqual(self.plan("zero"), [("zero", 0)])

    def test_directory_with_zero_byte_file_is_preserved(self):
        (self.root / "data/nested").mkdir(parents=True)
        (self.root / "data/nested/zero").touch()
        self.assertEqual(self.plan("data"), [("data", 0)])

    def test_symlink_to_directory_with_zero_byte_file_is_preserved(self):
        (self.root / "target").mkdir()
        (self.root / "target/zero").touch()
        (self.root / "link").symlink_to(self.root / "target", target_is_directory=True)
        self.assertEqual(self.plan("link"), [("link", 0)])

    def test_chunked_plan_skips_empty_tree_and_preserves_other_files(self):
        (self.root / "pc/data/face/nested").mkdir(parents=True)
        (self.root / "pc/data/zero").touch()
        (self.root / "pc/data/content").write_bytes(b"game")
        with patch.object(upload, "CHUNK", 4):
            self.assertEqual(self.plan("pc"), [
                ("pc/data/content", 4), ("pc/data/zero", 0)
            ])


if __name__ == "__main__":
    unittest.main()
