#!/usr/bin/env python3
"""Validate completed captures and save portable comparison evidence."""
import argparse
import csv
from datetime import datetime, timezone
from os.path import relpath
import hashlib
import json
from pathlib import Path
import re
import statistics

from performance_compare import frame_summary, memory_summary, read_csv
from shader_capture import DRIVER, LOOKUP, PRELOAD, PERSIST, LIBRARY_PERSIST, USE, digest


def diagnostic(events, start, end, feature):
    if not feature:
        return {"encoder_driver": None, "reason": "Main has no feature driver/cache counters"}
    driver = []
    libraries = {k: 0 for k in ["hit", "miss", "memory", "unavailable"]}
    writes = {k: 0 for k in ["saved", "failed", "busy", "deferred"]}
    draws = {k: 0 for k in ["runtime", "archive", "preloaded"]}
    preload = None
    preload_accesses = {}
    crossing_driver_calls = 0
    for timestamp, line in events:
        if match := PRELOAD.search(line):
            preload = {"ready": int(match[1]), "skipped": int(match[2]), "ms": float(match[3])}
        if timestamp > end:
            continue
        match = LOOKUP.search(line)
        driver_match = DRIVER.search(line)
        access = None
        if match and match[4] == "preload":
            access = (match[1] + "/" + match[2], float(match[3]))
        elif driver_match and driver_match[3] == "preload":
            access = ("driver/" + driver_match[1], float(driver_match[2]))
        if access:
            key, ms = access
            counters = preload_accesses.setdefault(key, {"calls": 0, "total_ms": 0, "max_ms": 0})
            counters["calls"] += 1
            counters["total_ms"] += ms
            counters["max_ms"] = max(counters["max_ms"], ms)
        if (match := LOOKUP.search(line)) and match[1] == "library":
            libraries[match[2]] += 1
        if match := LIBRARY_PERSIST.search(line):
            writes[match[1]] += 1
        if timestamp < start:
            continue
        if (match := DRIVER.search(line)) and match[3] == "encoder":
            if timestamp - start >= float(match[2]) * 1e6:
                driver.append((match[1], float(match[2])))
            else:
                crossing_driver_calls += 1
        if (match := USE.search(line)) and match[1] == "draws":
            for origin, count in zip(draws, match.groups()[1:]):
                draws[origin] += int(count)
    return {"preload": preload, "preload_accesses": preload_accesses,
            "library_lookups_startup_and_gameplay": libraries,
            "offline_publications_startup_and_gameplay": writes, "gameplay_encoded_draws": draws,
            "driver_calls_crossing_gameplay_start_excluded": crossing_driver_calls,
            "encoder_driver": {result: {"calls": sum(k == result for k, _ in driver),
                       "over_2ms": sum(k == result and ms > 2 for k, ms in driver),
                       "total_ms": sum(ms for k, ms in driver if k == result)}
                        for result in ["hit", "miss", "rejected"]}}


def analyze(raw, excluded=()):
    runs = []
    for summary in sorted((raw / "capped").glob("block-*/*/summary.json")):
        if any(summary.is_relative_to(path) for path in excluded):
            continue
        meta = json.loads(summary.read_text())
        if not meta.get("accepted"):
            continue
        folder = summary.parent
        pair = json.loads((folder.parent / "pair.json").read_text())
        start, end = meta["gameplay_monotonic_ns"], meta["end_gameplay_monotonic_ns"]
        events = [json.loads(line) for line in (folder / "events.jsonl").read_text().splitlines()]
        expected = folder.parent / "diagnostic.app/Contents/Frameworks"
        loaded = {}
        for _, line in events:
            if match := re.search(r"dyld\[\d+\]: <([^>]+)> (.+/librecomp_[^/]+\.dylib)$", line):
                path = Path(match[2])
                if path.parent != expected:
                    raise ValueError("Renderer/runtime escaped private bundle: " + str(path))
                if digest(path) != meta["runtime_dylibs"][path.name]:
                    raise ValueError("Loaded image hash changed")
                loaded[path.name] = match[1]
        assert all(k in loaded for k in ["librecomp_runtime.dylib", "librecomp_gfx.dylib", "librecomp_hlsl.dylib"])
        assert pair["metal_cache"]["metal_cache_created"]
        positions = []
        cameras = []
        for timestamp, line in events:
            if start <= timestamp <= end and (match := re.search(r"bench: player at ([\d.-]+),([\d.-]+),([\d.-]+) heading ([\d.-]+)", line)):
                positions.append([float(v) for v in match.groups()])
            if start <= timestamp <= end and (match := re.search(r"bench: fixed camera ([\d.-]+),([\d.-]+),([\d.-]+) fov=([\d.-]+) active=([01])", line)):
                cameras.append([float(v) for v in match.groups()])
        if not positions:
            raise ValueError("No player-position verification")
        distance = max(sum((a-b)**2 for a, b in zip(pos, [758.47, -216.85, 5.82]))**.5 for pos in positions)
        if distance > 2:
            raise ValueError("Player moved outside matched scene")
        if not cameras or any(abs(a-b) > .02 for camera in cameras
                              for a, b in zip(camera, [765.47, -216.85, 11.32, 55, 1])):
            raise ValueError("Fixed-camera verification missing or changed")
        frame_rows = read_csv(folder / "frames.csv")
        frames = frame_summary(frame_rows, start, end)
        early_end = min(end, start + 10_000_000_000)
        memory_rows = [r for r in read_csv(folder / "memory.csv") if r["monotonic_ns"] <= end]
        memory = memory_summary(memory_rows, start, end)
        bins = {}
        for row in memory_rows:
            second = int((row["monotonic_ns"] - meta["start_monotonic_ns"])/1e9)
            bins.setdefault(second, []).append(row)
        timeline = [[second, *[round(statistics.median(row[k] for row in rows)/2**20, 3)
                                for k in ["footprint_bytes", "rss_bytes", "compressed_bytes"]],
                     max(row["pressure_level"] for row in rows)] for second, rows in sorted(bins.items())]
        hitches = [[round((r["monotonic_ns"]-start)/1e9, 6), r["interval_ns"]/1e6]
                   for r in frame_rows if r["interval_ns"] > 33_333_000
                   and r["monotonic_ns"]-r["interval_ns"] >= start and r["monotonic_ns"] <= end]
        cache = next(r for r in pair["runs"] if r["state"] == folder.name)
        runs.append({"id": folder.parent.name + "/" + folder.name,
                     "block": pair["block"], "condition": pair["condition"], "state": folder.name,
                     "process_start_unix_s": meta["start_unix_s"],
                     "gameplay_start_unix_s": meta["start_unix_s"] + meta["seconds_to_gameplay"],
                     "seconds_to_gameplay": meta["seconds_to_gameplay"],
                     "seconds_to_preload_completion": meta.get("seconds_to_preload_completion"),
                     "frames": frames, "memory": memory,
                     "early_gameplay_seconds": (early_end - start) / 1e9,
                     "early_gameplay": frame_summary(frame_rows, start, early_end),
                     "hitch_trace_columns": ["end_seconds_since_gameplay", "duration_ms"], "hitch_trace": hitches,
                     "memory_timeline_columns": ["process_elapsed_second", "footprint_MiB", "RSS_MiB", "compressed_MiB", "max_pressure_flag"],
                     "memory_timeline_1s": timeline,
                     "diagnostics": diagnostic(events, start, end, pair["condition"] != "main"),
                     "player_positions": positions, "max_position_distance_m": distance,
                     "fixed_camera_samples_xyz_fov_active": cameras,
                     "loaded_private_uuids": loaded, "executable_sha256": meta["executable_sha256"],
                     "runtime_dylibs_sha256": meta["runtime_dylibs"],
                     "probe_sha256": meta["probe_sha256"],
                     "cache_reuse": {k: v for k, v in cache.items() if k != "output"},
                     "bundle_identifier": pair["metal_cache"]["bundle_identifier"],
                     "prior_suspended_attempts": pair.get("interrupted_attempts", []),
                     "raw_files": {str(p.relative_to(raw)): {"sha256": digest(p), "bytes": p.stat().st_size}
                                   for p in folder.iterdir() if p.is_file()}})
    return runs


def label_cohort(run, name):
    return {**run, "id": name + "/" + run["id"], "cohort": name,
            "chart_group": name + "/" + run["condition"] + "/" + run["state"],
            "raw_files": {"cohorts/" + name + "/" + path: entry for path, entry in run["raw_files"].items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("--output", type=Path, help="Report path (defaults to RAW/comparison.json)")
    parser.add_argument("--exclude", action="append", default=[], metavar="DIRECTORY=REASON",
                        help="Record why a capture directory is excluded")
    parser.add_argument("--planned-runs", type=int, default=12)
    parser.add_argument("--cohort", action="append", help="Read named cohorts/NAME without pooling implementations")
    args = parser.parse_args()
    raw = args.raw.resolve()
    args.output = args.output or raw / "comparison.json"
    reasons = {}
    for exclusion in args.exclude:
        directory, separator, reason = exclusion.partition("=")
        if not separator or not directory or not reason:
            parser.error("--exclude requires DIRECTORY=REASON")
        path = (raw / directory).resolve()
        if not path.is_relative_to(raw):
            parser.error("--exclude directory must be inside RAW")
        reasons[directory] = reason
    excluded_paths = [(raw / directory).resolve() for directory in reasons]
    if args.cohort:
        runs = [label_cohort(run, name) for name in args.cohort
                for run in analyze(raw / "cohorts" / name, excluded_paths)]
        source_inputs = {name: json.loads((raw / "cohorts" / name / "initial-inputs.json").read_text())["refs"]
                         for name in args.cohort}
    else:
        runs = analyze(raw, excluded_paths)
        source_inputs = json.loads((raw / "initial-inputs.json").read_text())["refs"]
    excluded = []
    for directory, reason in reasons.items():
        for path in sorted((raw / directory).glob("**/metadata.json")):
            meta = json.loads(path.read_text())
            record = {"raw_directory": str(path.parent.relative_to(raw)), "reason": reason,
                      "capture_error": meta.get("error"), "included_in_comparison": False,
                      "executable_sha256": meta.get("executable_sha256"),
                      "metadata_sha256": digest(path)}
            summary = path.with_name("summary.json")
            if summary.exists():
                result = json.loads(summary.read_text())
                record.update(seconds_to_gameplay=result["seconds_to_gameplay"],
                              frames=result["frames"], memory=result["memory"])
            excluded.append(record)
    data = {"schema_version": 2, "date": datetime.now(timezone.utc).date().isoformat(), "source_inputs": source_inputs,
            "raw_directory": relpath(raw, args.output.resolve().parent),
            "planned_runs": args.planned_runs, "completed_runs": len(runs), "runs": runs, "excluded_runs": excluded,
            "units": {"memory_timeline": "MiB = 1048576 bytes", "system_pageouts": "pages",
                      "system_swapouts": "pages", "system_compressions": "pages", "system_swap_bytes": "bytes"},
            "order_deviation": None,
            "camera_protocol": {"position": [765.47, -216.85, 11.32], "target": [757.47, -216.85, 6.62],
                                "fov_degrees": 55, "control": "Same scripted camera reapplied per benchmark frame; coordinates/FOV/active state checked before gameplay and periodically"}}
    if args.cohort:
        labels = {("empty", "first"): "no preparation", ("empty", "restart"): "runtime reuse",
                  ("prepared", "first"): "full preload", ("prepared", "restart"): "observed preload",
                  ("main", "first"): "fresh", ("main", "restart"): "restart"}
        groups = {}
        for run in runs:
            groups.setdefault(run["chart_group"], [run["chart_group"],
                run["cohort"].capitalize() + " · " + labels[run["condition"], run["state"]],
                args.cohort.index(run["cohort"]) % 6 + 1])
        data.update(chart_groups=list(groups.values()), chart_title="Metal coverage and preload · matched cohorts")
    args.output.write_text(json.dumps(data, indent=2) + "\n")
    rows = []
    for run in runs:
        f, m = run["frames"], run["memory"]
        preload = run["diagnostics"].get("preload") or {}
        driver = run["diagnostics"].get("encoder_driver") or {}
        rows.append({"id": run["id"], "cohort": run.get("cohort"),
                     "condition": run["condition"], "state": run["state"],
                     "startup_s": run["seconds_to_gameplay"], "fps": f["fps"],
                     "p99_ms": f["frame_ms"]["p99"], "max_ms": f["frame_ms"]["max"],
                     "hitches_over_33ms": f["hitches"]["33.333"]["count"],
                     "early_10s_hitches_over_33ms": run["early_gameplay"]["hitches"]["33.333"]["count"],
                     "preload_ms": preload.get("ms"), "preload_ready": preload.get("ready"),
                     "preload_skipped": preload.get("skipped"),
                     "encoder_driver_fallbacks": driver.get("miss", {}).get("calls"),
                     "settled_footprint_MiB": m["settled_median_bytes"]["footprint_bytes"]/2**20,
                     "settled_RSS_MiB": m["settled_median_bytes"]["rss_bytes"]/2**20,
                     "settled_compressed_MiB": m["settled_median_bytes"]["compressed_bytes"]/2**20,
                     "peak_footprint_MiB": m["kernel_lifetime_footprint_peak_bytes"]/2**20,
                     "system_swapouts_delta_pages": m["system_delta"]["system_swapouts"]})
    if rows:
        with args.output.with_suffix(".csv").open("w") as file:
            writer = csv.DictWriter(file, fieldnames=list(rows[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
    print(json.dumps({"completed": len(runs), "output": str(args.output)}))


if __name__ == "__main__":
    main()
