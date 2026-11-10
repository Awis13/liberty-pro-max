#!/usr/bin/env python3
"""Controlled diagnostic captures; does not build or start full preparation.

Create signed bundles with shader_capture.isolated_app, then pass the same bundle
and cache to a subsequent invocation for warm reuse. Use a new output directory.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import queue
import re
import signal
import statistics
import subprocess
import threading
import time

from shader_capture import digest, stop


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[int((len(ordered) - 1) * fraction + .5)]


def frame_summary(rows, start_ns, end_ns):
    # Keep complete intervals only, including the long ones. Never turn a
    # five-second p99 into a mean FPS or retain a startup-spanning frame.
    frames = [r for r in rows if r["monotonic_ns"] <= end_ns
              and r["monotonic_ns"] - r["interval_ns"] >= start_ns]
    if not frames:
        raise ValueError("No complete gameplay frame intervals")
    ms = [r["interval_ns"] / 1e6 for r in frames]
    slowest = sorted(ms, reverse=True)[:max(1, math.ceil(len(ms) * .01))]
    return {"frames": len(ms), "covered_seconds": sum(ms) / 1000,
            "fps": len(ms) * 1000 / sum(ms),
            "one_percent_low_fps": 1000 / statistics.mean(slowest),
            "frame_ms": {"min": min(ms), "mean": statistics.mean(ms),
                         **{name: percentile(ms, q) for name, q in
                            [("p50", .5), ("p95", .95), ("p99", .99), ("p999", .999)]},
                         "max": max(ms)},
            "hitches": {str(threshold): {"count": sum(v > threshold for v in ms),
                       "total_ms": sum(v for v in ms if v > threshold),
                       "excess_ms": sum(max(0, v - threshold) for v in ms)}
                        for threshold in [33.333, 50, 100]},
            "probe_cost_ns": {"median": statistics.median(r["previous_probe_ns"] for r in frames),
                              "max": max(r["previous_probe_ns"] for r in frames)}}


def read_csv(path):
    with path.open() as source:
        return [{k: int(v) for k, v in r.items()} for r in csv.DictReader(source)]


def memory_summary(rows, start_ns, end_ns):
    game = [r for r in rows if start_ns <= r["monotonic_ns"] <= end_ns]
    settled = [r for r in game if r["monotonic_ns"] >= end_ns - 10_000_000_000]
    if not settled:
        raise ValueError("Missing settled memory samples")
    statuses = ["task_status", "rusage_status", "host_status", "pressure_status", "swap_status"]
    if any(r[k] for r in rows for k in statuses):
        raise ValueError("Memory API failure; retain raw samples, reject summary")
    return {"samples": len(rows), "gameplay_samples": len(game),
            "interval_ms_median": statistics.median((b["monotonic_ns"]-a["monotonic_ns"])/1e6
                                                     for a, b in zip(rows, rows[1:])),
            "sampling_api_elapsed_us_median": statistics.median(r["sample_cost_ns"] for r in rows)/1000,
            "settled_window_seconds": 10,
            "settled_median_bytes": {k: statistics.median(r[k] for r in settled)
                                     for k in ["footprint_bytes", "rss_bytes", "compressed_bytes"]},
            "sampled_peak_bytes": {k: max(r[k] for r in rows)
                                   for k in ["footprint_bytes", "rss_bytes", "compressed_bytes"]},
            "gameplay_peak_bytes": {k: max(r[k] for r in game)
                                    for k in ["footprint_bytes", "rss_bytes", "compressed_bytes"]},
            "kernel_lifetime_footprint_peak_bytes": max(r["rusage_peak_bytes"] for r in rows),
            "system_pressure_samples": {str(k): sum(r["pressure_level"] == k for r in rows)
                                        for k in sorted({r["pressure_level"] for r in rows})},
            "gameplay_pressure_samples": {str(k): sum(r["pressure_level"] == k for r in game)
                                          for k in sorted({r["pressure_level"] for r in game})},
            "system_delta": {k: rows[-1][k] - rows[0][k] for k in
                             ["system_swap_bytes", "system_pageouts", "system_swapouts", "system_compressions"]},
            "gameplay_system_delta": {k: game[-1][k] - game[0][k] for k in
                                      ["system_swap_bytes", "system_pageouts", "system_swapouts", "system_compressions"]},
            "footprint_api_max_difference_bytes": max(abs(r["footprint_bytes"]-r["rusage_footprint_bytes"])
                                                       for r in rows)}


def conflicts(owned_pid=None, owned_executable=None):
    listing = subprocess.check_output(["ps", "-axo", "pid=,ppid=,comm="], text=True)
    blocked = {"gtaiv", "recomp_shader_prepare", "gtaiv_shader_runtime_test",
               "shader_runtime_test", "shader_runtime_next_build_test", "shader_prepare_next_build_test",
               "metal_prepare_test", "metal_recipe_reuse_test",
               "gtaiv_launcher_shader_test", "ninja", "cmake", "clang", "clang++", "ld", "ram_probe"}
    overlap = []
    for line in listing.splitlines():
        fields = line.split(None, 2)
        if len(fields) != 3:
            continue
        pid, parent, executable = fields
        if int(pid) == owned_pid or Path(executable).name not in blocked:
            continue
        if owned_executable is not None and int(parent) == owned_pid and executable == str(owned_executable):
            # Only this game's exact cleanup helper is part of the capture.
            # Other games, workers and helpers must still invalidate the run.
            result = subprocess.run(["ps", "-p", pid, "-o", "args="],
                                    capture_output=True, text=True)
            if result.returncode == 0 and result.stdout.strip() == (
                    f"{owned_executable} --metal-archiver-reap {owned_pid}"):
                continue
        overlap.append(line)
    return overlap


def capture(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    if conflicts():
        raise RuntimeError("Another game/build/probe workload is active")
    args.executable = args.executable.resolve()
    args.cache = args.cache.resolve()
    args.probe = args.probe.resolve()
    data = out / "data"
    if args.data_seed:
        import shutil
        shutil.copytree(args.data_seed, data)
    else:
        data.mkdir()
    live = out / "input.txt"
    live.write_text("")
    # Every app setting is explicit or comes from an identical empty data seed.
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("GTAIV_", "RECOMP_", "DYLD_", "MTL_", "LIBERTY_MEASURE_", "XDG_"))}
    env.update(GTAIV_GFX="metal", RECOMP_UPSCALER="taa", RECOMP_SHADER_CACHE_REPORT="1",
               RECOMP_METAL_CACHE_ROOT=str(args.cache), XDG_DATA_HOME=str(data),
               GTAIV_BENCH=args.scene, GTAIV_BENCH_GAMEPLAY="1", GTAIV_FPS_REPORT="5",
               GTAIV_SHOT_EVERY="0", GTAIV_INPUT_LIVE=str(live), GTAIV_FPS_LIMIT=str(args.fps_limit),
               GTAIV_CONTINUE="1", GTAIV_SKIP_INTRO="1", GTAIV_SAVE_SYNC="0", GTAIV_BACKGROUND="1",
               GTAIV_FULLSCREEN="window", GTAIV_HIDDEN="1", GTAIV_HITCH="0",
               DYLD_INSERT_LIBRARIES=str(args.probe), LIBERTY_MEASURE_FRAMES=str(out / "frames.csv"),
               LIBERTY_MEASURE_MEMORY=str(out / "memory.csv"), DYLD_PRINT_LIBRARIES="1",
               DYLD_LIBRARY_PATH=str(args.executable.parent.parent / "Frameworks"),
               LIBERTY_MEASURE_LEGACY_CACHE=str(args.cache / "legacy"))
    env["LIBERTY_MEASURE_FIXED_CAMERA"] = "1"
    command = [str(args.executable), "--no-launcher", "--game-dir", str(args.game.resolve()), "--",
               "-windowed", "-width", "1280", "-height", "720", "-norestrictions", "-availablevidmem", "2047"]
    meta = {"name": out.name, "command": command, "environment": {k: v for k, v in env.items()
             if k.startswith(("GTAIV_", "RECOMP_", "DYLD_", "LIBERTY_MEASURE_", "XDG_"))},
            "executable_sha256": digest(args.executable), "probe_sha256": digest(args.probe),
            "scene": args.scene, "seconds": args.seconds, "fps_limit": args.fps_limit,
            "runtime_dylibs": {p.name: digest(p) for p in
                              (args.executable.parent.parent / "Frameworks").glob("*.dylib")}}
    events = queue.Queue()
    start_ns = time.monotonic_ns()
    wall = time.time()
    meta.update(start_monotonic_ns=start_ns, start_unix_s=wall)
    process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, bufsize=1, start_new_session=True)
    meta["pid"] = process.pid
    def reader():
        for line in process.stdout:
            events.put((time.monotonic_ns(), line))
        events.put((time.monotonic_ns(), None))
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    awake = subprocess.Popen(["caffeinate", "-i", "-t", "1200", "-w", str(process.pid)])
    ready = None
    vehicle = None
    camera_ready = False
    last = time.monotonic()
    next_guard = last
    snapshot = out / "gameplay.bmp"
    requested = None
    try:
        with (out / "game.log").open("w") as log, (out / "events.jsonl").open("w") as timed:
            while True:
                now = time.monotonic()
                if now - last > 10 or abs((time.time()-wall)-(now-start_ns/1e9)) > 2:
                    raise RuntimeError("System suspension/clock discontinuity")
                last = now
                if now >= next_guard:
                    overlap = conflicts(process.pid, args.executable)
                    if overlap:
                        meta["conflicts"] = overlap
                        raise RuntimeError("Concurrent workload invalidated capture")
                    next_guard = now + 2
                if ready is None and now - start_ns/1e9 > args.startup_timeout:
                    raise RuntimeError("Gameplay startup timed out")
                if ready is not None and now >= ready/1e9 + args.seconds and requested is None:
                    requested = now
                    meta["end_gameplay_monotonic_ns"] = ready + int(args.seconds*1e9)
                    with live.open("a") as commands:
                        commands.write("shot " + str(snapshot) + "\n")
                if requested is not None:
                    if snapshot.exists() and snapshot.stat().st_size > 54 and now-requested > 2:
                        header = snapshot.read_bytes()[:6]
                        if header[:2] == b"BM" and snapshot.stat().st_size >= int.from_bytes(header[2:6], "little"):
                            break
                    if now - requested > 12:
                        raise RuntimeError("Missing post-window gameplay frame")
                try:
                    timestamp, line = events.get(timeout=.1)
                except queue.Empty:
                    continue
                if line is None:
                    raise RuntimeError("Game exited before capture completed")
                log.write(line)
                timed.write(json.dumps([timestamp, line.rstrip()]) + "\n")
                match = re.search(r"bench: player vehicle=([01])", line)
                if match:
                    vehicle = int(match[1])
                    if ready and vehicle:
                        raise RuntimeError("Player entered vehicle during measurement")
                match = re.search(r"bench: fixed camera ([\d.-]+),([\d.-]+),([\d.-]+) fov=([\d.-]+) active=([01])", line)
                if match:
                    observed = [float(v) for v in match.groups()]
                    target = [float(v) for v in args.scene.split(",")[:3]]
                    expected = [target[0]+7, target[1], target[2]+5.5, 55, 1]
                    camera_ready = all(abs(a-b) < .02 for a, b in zip(observed, expected))
                    if not camera_ready:
                        raise RuntimeError("Scripted camera changed during measurement")
                if "bench: gameplay ready" in line and ready is None:
                    if vehicle != 0:
                        raise RuntimeError("Missing on-foot normalization")
                    if not camera_ready:
                        raise RuntimeError("Missing fixed-camera normalization")
                    ready = timestamp
                    meta.update(gameplay_monotonic_ns=ready, seconds_to_gameplay=(ready-start_ns)/1e9)
                    print(json.dumps({"run": out.name, "gameplay_seconds": meta["seconds_to_gameplay"]}), flush=True)
                if "shader-preload completed" in line:
                    meta["seconds_to_preload_completion"] = (timestamp-start_ns)/1e9
                    meta["preload_line"] = line.strip()
        meta["accepted"] = True
    except BaseException as error:
        meta.update(accepted=False, error=str(error))
        raise
    finally:
        # Matches the earlier harness: capture shutdown is forced, publication
        # may be incomplete. Reuse only entries actually published before stop.
        stop(process)
        # Include only this launch's offline compiler descendants, if any.
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        stop(awake)
        thread.join(timeout=2)
        process.stdout.close()
        meta["returncode_after_stop"] = process.returncode
        meta["end_unix_s"] = time.time()
        (out / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")
    try:
        frames = read_csv(out / "frames.csv")
        memory = read_csv(out / "memory.csv")
        meta["frames"] = frame_summary(frames, ready, meta["end_gameplay_monotonic_ns"])
        # Exclude screenshot readback from all reported memory peaks as well.
        meta["memory"] = memory_summary([r for r in memory if r["monotonic_ns"] <=
                                        meta["end_gameplay_monotonic_ns"]], ready,
                                        meta["end_gameplay_monotonic_ns"])
    except Exception as error:
        meta.update(accepted=False, error=str(error))
        (out / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")
        raise
    (out / "summary.json").write_text(json.dumps(meta, indent=2) + "\n")
    return meta


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--data-seed", type=Path)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--startup-timeout", type=float, default=240)
    parser.add_argument("--fps-limit", type=int, default=60)
    parser.add_argument("--scene", default="758.47,-216.85,5.82,93.5,12,0")
    capture(parser.parse_args())
