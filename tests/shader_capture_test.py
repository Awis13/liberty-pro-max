#!/usr/bin/env python3
"""Exercise capture boundaries and cache isolation without game content or a GPU."""
import importlib.util
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

MODULE = Path(__file__).resolve().parents[1] / "tools/shader_capture.py"
spec = importlib.util.spec_from_file_location("shader_capture", MODULE)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

FAKE_GAME = r'''#!/usr/bin/env python3
import os,struct,time
from pathlib import Path
print("gtaiv: bench: player placed at 0,0,0",flush=True)
print("gfx: shader-preload completed ready=3 skipped=1 15.000 ms",flush=True)
print("gfx: shader-cache pipeline miss 5.000 ms",flush=True)
if not os.environ.get("OLD_MARKER_ONLY"):
    print("gtaiv: bench: player vehicle=" + ("1" if os.environ.get("IN_CAR") else "0"),flush=True)
    print("gtaiv: bench: gameplay ready at 0,0,0",flush=True)
    print("gfx: shader-cache pipeline hit 0.250 ms",flush=True)
    print("gfx: shader-cache pipeline ready 0.010 ms phase=runtime thread=22",flush=True)
    print("gfx: shader-use draws runtime=1 archive=2 preloaded=3",flush=True)
    print("gfx: shader-use binds runtime=1 archive=1 preloaded=2",flush=True)
    print("gfx: shader-persist pipeline saved",flush=True)
    print("gfx: shader-persist library saved",flush=True)
    print("gfx: shader-cache pipeline miss 10.000 ms",flush=True)
    print("gfx: shader-cache driver miss 8.000 ms phase=encoder thread=22",flush=True)
    print("gfx: shader-cache driver hit 0.100 ms phase=preload thread=11",flush=True)
    print("gfx: pipeline 12 compiled in 10.0 ms",flush=True)
    print("hitch: frame 41 took 1000.0 ms",flush=True)
    time.sleep(.06)
    if os.environ.get("ENTER_CAR"):
        print("gtaiv: bench: player vehicle=1",flush=True)
    print("hitch: frame 42 took 40.0 ms",flush=True)
live=Path(os.environ["GTAIV_INPUT_LIVE"])
while True:
    commands=live.read_text()
    if commands.startswith("shot "):
        path=Path(commands.strip().removeprefix("shot "))
        pixels=b"\xff\xff\xff\x00"
        header=b"BM"+struct.pack("<IHHI",58,0,0,54)
        info=struct.pack("<IiiHHIIiiII",40,1,1,1,32,0,4,0,0,0,0)
        path.write_bytes(header+info+pixels)
        print("gfx: shader-cache pipeline miss 999.000 ms",flush=True)
        while True: time.sleep(.02)
    time.sleep(.01)
'''


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="liberty-capture-test-")
        self.base = Path(self.tmp.name)
        game = self.base / "fake-game"
        game.write_text(FAKE_GAME)
        game.chmod(0o755)
        self.args = SimpleNamespace(output=self.base, executable=game, game=self.base,
                                    data_seed=None, scene="0,0,0", seconds=0.1,
                                    startup_timeout=2, threshold_ms=2)

    def tearDown(self):
        self.tmp.cleanup()

    def test_cache_events_preserve_work_phase_and_thread(self):
        for phase in ("first-use", "encoder", "preload"):
            with self.subTest(phase=phase):
                lookup = capture.LOOKUP.search(
                    f"gfx: shader-cache pipeline miss 5.250 ms phase={phase} thread=123")
                self.assertIsNotNone(lookup)
                self.assertEqual(lookup.groups(), ("pipeline", "miss", "5.250", phase, "123"))
                driver = capture.DRIVER.search(
                    f"gfx: shader-cache driver miss 4.125 ms phase={phase} thread=123")
                self.assertIsNotNone(driver)
                self.assertEqual(driver.groups(), ("miss", "4.125", phase, "123"))

    def test_control_gate_and_post_measurement_readback(self):
        result = capture.run_capture("test", self.args, dict(os.environ), self.base / "cache")
        self.assertEqual(result["startup"]["lookups"], 1)
        self.assertEqual(result["gameplay"]["lookups"], 2)
        self.assertEqual(result["gameplay"]["ready_pipeline_lookups"], 1)
        self.assertEqual(result["gameplay"]["shader_draws"], {"runtime": 1, "archive": 2, "preloaded": 3})
        self.assertEqual(result["gameplay"]["shader_binds"], {"runtime": 1, "archive": 1, "preloaded": 2})
        self.assertEqual(result["gameplay"]["runtime_archive_writes"]["saved"], 1)
        self.assertEqual(result["gameplay"]["runtime_library_writes"]["saved"], 1)
        self.assertEqual(result["gameplay"]["lookups_by_work_phase"]["runtime"], 1)
        self.assertEqual(result["startup_preload"], {"ready": 3, "skipped": 1, "ms": 15})
        self.assertEqual(result["gameplay"]["misses_over_threshold"], 1)
        self.assertEqual(result["gameplay"]["encoding_pipeline_calls_over_threshold"], 1)
        self.assertEqual(result["gameplay"]["encoder_driver"]["miss"], {"calls": 1, "ms": 8, "over_threshold": 1})
        self.assertEqual(result["gameplay"]["encoder_driver"]["hit"]["calls"], 0)
        self.assertEqual(result["gameplay"]["frame_hitches_over_33ms"], 1)
        self.assertEqual(result["gameplay"]["max_lookup_ms"], 10)
        self.assertEqual((self.base / "test-frames/gameplay.bmp").stat().st_size, 58)

    def test_old_cutscene_marker_does_not_start_measurement(self):
        self.args.startup_timeout = 0.1
        with self.assertRaisesRegex(RuntimeError, "did not reach the benchmark scene"):
            capture.run_capture("old", self.args, dict(os.environ, OLD_MARKER_ONLY="1"), self.base / "cache")

    def test_clock_discontinuity_invalidates_capture(self):
        real_time = capture.time.time
        calls = 0
        def shifted_time():
            nonlocal calls
            calls += 1
            return real_time() + (30 if calls > 2 else 0)
        with mock.patch.object(capture.time, "time", side_effect=shifted_time):
            with self.assertRaisesRegex(RuntimeError, "clock discontinuity"):
                capture.run_capture("suspend", self.args, dict(os.environ), self.base / "cache")

    def test_vehicle_state_cannot_start_or_change_during_measurement(self):
        for flag in ("IN_CAR", "ENTER_CAR"):
            with self.subTest(flag=flag), self.assertRaisesRegex(RuntimeError, "vehicle state"):
                capture.run_capture(flag, self.args, dict(os.environ, **{flag: "1"}), self.base / flag)

    def test_runtime_metadata_is_isolated_from_prepared_seed(self):
        source, target = self.base / "seed", self.base / "runtime"
        source.mkdir()
        (source / "state.json").write_text('{"status":"Prepared"}')
        (source / "shader.metallib").write_bytes(b"immutable compiled entry")
        capture.snapshot_cache(source, target)
        (target / "state.json").write_text('{"status":"Incomplete"}')
        self.assertIn("Prepared", (source / "state.json").read_text())
        self.assertEqual((target / "shader.metallib").read_bytes(), (source / "shader.metallib").read_bytes())

    def test_first_play_seed_detects_gameplay_metadata(self):
        seed = self.base / "metal/v1/scope"
        for kind in ("sources", "descriptors", "runtime-pipelines"):
            (seed / kind).mkdir(parents=True)
            (seed / kind / "entry").write_text("recorded")
        self.assertEqual(capture.seed_recordings(self.base),
                         {"sources": 1, "descriptors": 1, "runtime-pipelines": 1})


if __name__ == "__main__":
    unittest.main()
