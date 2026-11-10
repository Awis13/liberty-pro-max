#!/usr/bin/env python3
"""Check the measurement math against startup contamination and tail spikes."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from subprocess import CompletedProcess

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from performance_compare import conflicts, frame_summary, memory_summary
from performance_chart import measurement_points
from performance_analyze import diagnostic, label_cohort


class Measurements(unittest.TestCase):
    def test_workload_guard_accepts_only_owned_cleanup_helper(self):
        executable = Path("/test/Liberty Pro Max.app/Contents/MacOS/gtaiv")
        listing = (f"100 1 {executable}\n101 100 {executable}\n"
                   f"102 100 {executable}\n103 99 {executable}\n"
                   "104 100 /other/gtaiv\n105 1 /bin/clang\n")
        with patch("performance_compare.subprocess.check_output", return_value=listing), patch(
                "performance_compare.subprocess.run", side_effect=[
                    CompletedProcess([], 0, f"{executable} --metal-archiver-reap 100\n"),
                    CompletedProcess([], 0, f"{executable} --no-launcher\n"),
                ]):
            self.assertEqual([line.split()[0] for line in conflicts(100, executable)],
                             ["102", "103", "104", "105"])
        with patch("performance_compare.subprocess.check_output", return_value=listing):
            self.assertEqual(len(conflicts()), 6)

    def test_complete_intervals_and_fps_use_elapsed_time(self):
        rows = [{"monotonic_ns": end * 1_000_000, "interval_ns": dt * 1_000_000,
                 "previous_probe_ns": 1000} for end, dt in
                [(10, 500), (30, 20), (50, 20), (150, 100), (170, 20)]]
        result = frame_summary(rows, 10_000_000, 150_000_000)
        self.assertEqual(result["frames"], 3)
        self.assertAlmostEqual(result["fps"], 3 / .14)
        self.assertEqual(result["hitches"]["33.333"]["count"], 1)
        self.assertEqual(result["hitches"]["100"]["count"], 0)
        self.assertEqual(result["frame_ms"]["max"], 100)
        self.assertEqual(result["one_percent_low_fps"], 10)

    def test_missing_memory_or_failed_api_is_not_zero(self):
        with self.assertRaisesRegex(ValueError, "Missing settled"):
            memory_summary([], 0, 100)
        row = dict.fromkeys(["task_status", "rusage_status", "host_status", "pressure_status", "swap_status"], 0)
        row.update(monotonic_ns=10, task_status=5)
        with self.assertRaisesRegex(ValueError, "API failure"):
            memory_summary([row], 0, 100)

    def test_chart_keeps_implementation_cohorts_separate(self):
        common = {"condition": "prepared", "state": "first", "seconds_to_gameplay": 10,
                  "frames": {"fps": 60, "hitches": {"33.333": {"count": 0, "total_ms": 0}}},
                  "memory": {"settled_median_bytes": {"footprint_bytes": 1048576},
                             "system_pressure_samples": {"1": 1, "4": 1}}}
        common.update(id="block-1-prepared/first", raw_files={"capped/first/frames.csv": {"sha256": "fixture"}})
        runs = [label_cohort(common, name) for name in ["reference", "coverage", "optimized"]]
        self.assertEqual(len({r["id"] for r in runs}), 3)
        self.assertIn("cohorts/reference/capped/first/frames.csv", runs[0]["raw_files"])
        points = measurement_points({"runs": runs})
        self.assertEqual(len({p["group"] for p in points}), 3)
        self.assertTrue(all(p["pressure"] == 50 for p in points))
        self.assertTrue(all(p["preload"] is None and p["early_hitches"] is None for p in points))

    def test_early_window_keeps_complete_intervals(self):
        rows = [{"monotonic_ns": end, "interval_ns": dt, "previous_probe_ns": 0}
                for end, dt in [(105, 10), (120, 15), (10_000_000_100, 20), (10_000_000_101, 20)]]
        result = frame_summary(rows, 100, 10_000_000_100)
        self.assertEqual(result["frames"], 2)

    def test_preload_attribution_does_not_count_as_gameplay_work(self):
        events = [(1, "gfx: shader-cache pipeline hit 3.000 ms phase=preload thread=1"),
                  (2, "gfx: shader-cache driver hit 1.000 ms phase=preload thread=1"),
                  (3, "gfx: shader-preload completed ready=1 skipped=0 4.000 ms"),
                  (20_000_000, "gfx: shader-cache driver miss 2.000 ms phase=encoder thread=1")]
        result = diagnostic(events, 10_000_000, 30_000_000, True)
        self.assertEqual(result["preload_accesses"]["pipeline/hit"]["total_ms"], 3)
        self.assertEqual(result["preload_accesses"]["driver/hit"]["total_ms"], 1)
        self.assertEqual(result["encoder_driver"]["miss"]["calls"], 1)
        self.assertEqual(result["encoder_driver"]["hit"]["calls"], 0)


if __name__ == "__main__":
    unittest.main()
