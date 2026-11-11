"""tools/shader_bench_report.py: a shader benchmark's summary from its log."""
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from contextlib import redirect_stdout


spec = importlib.util.spec_from_file_location(
    "shader_bench_report", Path(__file__).resolve().parents[1] / "tools/shader_bench_report.py"
)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)

PERF = ("warning: gfx: perf: per frame 5 draws (0 of them depth from w, 0 not for another projection; "
        "0.31 ms recording them, 60.50 us each), 34 recorded ops, 1.0 render passes, 0.0 uploads ahead + "
        "0.0 in order (0.00 MB, 0.00 MB in order); encoder busy 1% (0.02 ms a frame); in {frames} frames: "
        "7 textures + 1 depth buffers created, 0 arena buffers (0 MB) + 4 made ahead, {shaders} shaders "
        "({sms} ms, {msl} MSL compiles), {pipes} pipelines compiled while drawing ({pms} ms, max {pmax}), "
        "{ahead} built ahead used")

LOG = "\n".join([
    "farm: shader-bench measure waits: a build runs",
    "gfx: 12 translated shaders from earlier runs",
    "load: loading screen up at 0.83 s",
    "gfx: pipeline 3 compiled in 25.0 ms (0 built ahead so far)",
    "warning: hitch: frame 40 took 120.5 ms (t=1.20 s)",
    "load: loading screen down at 30.50 s",
    "load: loading screen up at 30.60 s",
    PERF.format(frames=100, shaders=900, sms="4000.5", msl=850, pipes=40, pms="300.0", pmax="45.0", ahead=3),
    "gfx: pipeline 9 compiled in 3.0 ms (0 built ahead so far)",
    "load: loading screen down at 41.25 s",
    "warning: fps: 3839.60 at t=10.2 s (interval ms: min 0.007 p1 0.011 median 0.249 p99 1.081 max 184.624; "
    "6.9 PeekMessage a frame; GPU 0.60 ms a frame, 751 MB; footprint 2133 MB)",
    "warning: gtaiv: bench: player placed at -421.90,1494.00,18.90 heading 90.0",
    "gfx: pipeline 50 compiled in 20.0 ms (2 built ahead so far)",
    "gfx: pipeline 51 compiled in 19.9 ms (2 built ahead so far)",
    "warning: hitch: frame 900 took 55.0 ms (t=21.00 s)",
    "warning: hitch: frame 950 took 49.0 ms (t=22.00 s)",
    "warning: hitch: frame 999 took 50.0 ms (t=23.00 s)",
    PERF.format(frames=500, shaders=10, sms="20.0", msl=2, pipes=5, pms="60.5", pmax="22.0", ahead=7),
    "warning: fps: 100.00 at t=20.2 s (interval ms: min 6.8 p1 7.0 median 9.0 p99 12.0 max 55.0; "
    "1.0 PeekMessage a frame)",
    "warning: fps: 120.00 at t=30.2 s (interval ms: min 6.8 p1 7.0 median 8.0 p99 30.0 max 50.0; "
    "1.0 PeekMessage a frame)",
    "warning: fps: 80.00 at t=40.2 s (interval ms: min 6.8 p1 7.0 median 12.0 p99 20.0 max 90.0; "
    "1.0 PeekMessage a frame)",
])


class SummaryTests(unittest.TestCase):
    def setUp(self):
        self.s = report.summarize(LOG, "cold")

    def test_first_scene(self):
        self.assertEqual(self.s["scene_marker"], "bench placement")
        self.assertEqual(self.s["first_scene_s"], 41.25)
        self.assertEqual(self.s["msl_from_earlier_runs"], 12)
        self.assertFalse(self.s["farm_interrupted"])

    def test_compiles(self):
        self.assertEqual(self.s["all"]["compiles_logged"], 4)
        self.assertEqual(self.s["all"]["compiles_slow"], 2)  # 25.0 and 20.0; 19.9 is under
        self.assertEqual(self.s["all"]["compile_worst_ms"], 25.0)
        self.assertEqual(self.s["scene"]["compiles_slow"], 1)
        self.assertEqual(self.s["scene"]["compile_worst_ms"], 20.0)

    def test_perf_totals(self):
        p = self.s["all"]["perf"]
        self.assertEqual(p["reports"], 2)
        self.assertEqual(p["frames"], 600)
        self.assertEqual(p["shaders"], 910)
        self.assertEqual(p["shader_ms"], 4020.5)
        self.assertEqual(p["msl_compiles"], 852)
        self.assertEqual(p["pipelines_while_drawing"], 45)
        self.assertEqual(p["pipeline_ms"], 360.5)
        self.assertEqual(p["pipeline_max_ms"], 45.0)
        self.assertEqual(p["built_ahead_used"], 10)
        self.assertEqual(self.s["scene"]["perf"]["shaders"], 10)
        self.assertEqual(self.s["scene"]["perf"]["pipeline_max_ms"], 22.0)

    def test_hitches(self):
        self.assertEqual(self.s["all"]["hitches"], 3)  # 120.5, 55, 50; 49 is under
        self.assertEqual(self.s["all"]["hitch_worst_ms"], 120.5)
        self.assertEqual(self.s["scene"]["hitches"], 2)
        self.assertEqual(self.s["scene"]["hitch_worst_ms"], 55.0)

    def test_fps(self):
        sc = self.s["scene"]
        self.assertEqual(sc["fps_reports"], 3)  # the one before the placement is not the scene's
        self.assertEqual(sc["fps_median"], 100.0)
        self.assertEqual(sc["fps_p1"], 80.0)
        self.assertEqual(sc["interval_p99_worst_ms"], 30.0)
        self.assertEqual(sc["interval_max_worst_ms"], 90.0)
        self.assertEqual(self.s["all"]["fps_reports"], 4)

    def test_without_markers(self):
        s = report.summarize("warning: hitch: frame 1 took 70.0 ms (t=0.50 s)\n"
                             "farm: the game was started by hand: measurement stopped, queued again\n")
        self.assertEqual(s["scene_marker"], "none (whole log)")
        self.assertIsNone(s["first_scene_s"])
        self.assertEqual(s["scene"]["hitches"], 1)
        self.assertIsNone(s["scene"]["compile_worst_ms"])
        self.assertIsNone(s["scene"]["fps_median"])
        self.assertTrue(s["farm_interrupted"])

    def test_scene_after_last_loading_screen(self):
        s = report.summarize("warning: hitch: frame 1 took 70.0 ms (t=0.50 s)\n"
                             "load: loading screen down at 12.00 s\n"
                             "warning: hitch: frame 2 took 80.0 ms (t=0.90 s)\n")
        self.assertEqual(s["scene_marker"], "last loading screen down")
        self.assertEqual(s["first_scene_s"], 12.0)
        self.assertEqual(s["scene"]["hitches"], 1)
        self.assertEqual(s["all"]["hitches"], 2)

    def test_main_writes_json_and_table(self):
        with tempfile.TemporaryDirectory() as d:
            log, out = os.path.join(d, "game.log"), os.path.join(d, "summary.json")
            with open(log, "w") as f:
                f.write(LOG)
            buf = io.StringIO()
            with redirect_stdout(buf):
                report.main([log, "--json", out, "--label", "cold"])
            with open(out) as f:
                self.assertEqual(json.load(f)["all"]["compiles_slow"], 2)
            self.assertIn("pipelines >= 20 ms (all)", buf.getvalue())
            self.assertIn("cold", buf.getvalue())
            buf = io.StringIO()
            with redirect_stdout(buf):
                report.main(["--table", out, out])
            self.assertEqual(buf.getvalue().splitlines()[0].count("cold"), 2)


if __name__ == "__main__":
    unittest.main()
