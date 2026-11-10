#!/usr/bin/env python3
"""Compare an empty feature cache with manual preparation in one fixed scene.

  python3 tools/shader_capture.py BUILD/gtaiv GAME_DIR OUTPUT_DIR

Requires a logged-in macOS session. Uses isolated game data, disables save sync,
and leaves the user's saves and caches alone. Output must be a new directory.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import queue
import re
import shutil
import subprocess
import threading
import time
import tempfile
import uuid

LOOKUP = re.compile(r"gfx: shader-cache (library|pipeline|compute) (hit|miss|memory|ready|unavailable) ([\d.]+) ms(?: phase=([\w-]+) thread=(\d+))?")
USE = re.compile(r"gfx: shader-use (draws|binds) runtime=(\d+) archive=(\d+) preloaded=(\d+)")
LIBRARY_PERSIST = re.compile(r"gfx: shader-persist library (saved|failed|busy|deferred)")
PERSIST = re.compile(r"gfx: shader-persist (pipeline|compute) (saved|failed|busy)")
PRELOAD = re.compile(r"gfx: shader-preload completed ready=(\d+) skipped=(\d+) ([\d.]+) ms")
DRIVER = re.compile(r"gfx: shader-cache driver (hit|miss|rejected) ([\d.]+) ms phase=([\w-]+) thread=(\d+)")
ENCODING_PIPELINE = re.compile(r"gfx: pipeline \d+ (?:compiled|created) in ([\d.]+) ms")
HITCH = re.compile(r"hitch: frame \d+ took ([\d.]+) ms")
FRAME_INTERVALS = re.compile(r"interval ms: min [\d.]+ p1 [\d.]+ median ([\d.]+) p99 ([\d.]+) max ([\d.]+)")
VEHICLE = re.compile(r"bench: player vehicle=([01])")


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def snapshot_cache(source, destination):
    # Prepared binaries are immutable; replacing an entry uses atomic rename.
    # Hard links keep isolated benchmark copies from duplicating large binaries.
    def copy(source_file, destination_file):
        if Path(source_file).suffix in {".metallib", ".sha256", ".msl"}:
            try:
                os.link(source_file, destination_file)
                return destination_file
            except OSError:
                pass
        return shutil.copy2(source_file, destination_file)
    shutil.copytree(source, destination, copy_function=copy)


def isolated_app(executable, output, name):
    """Give this run a new app identity; never delete shared Metal caches."""
    bundle = executable.parent.parent.parent
    if bundle.suffix != ".app" or not (bundle / "Contents/Info.plist").is_file():
        raise ValueError("Fresh Metal cache capture requires an executable inside a .app bundle")
    identity = "io.github.awis13.liberty.shader-validation." + uuid.uuid4().hex
    cache = Path(subprocess.check_output(["getconf", "DARWIN_USER_CACHE_DIR"], text=True).strip()) / identity
    if cache.exists():
        raise RuntimeError("Diagnostic Metal cache already exists: " + str(cache))
    target = output / (name + ".app")
    shutil.copytree(bundle, target, symlinks=True)
    plist = target / "Contents/Info.plist"
    info = plistlib.loads(plist.read_bytes())
    info["CFBundleIdentifier"] = identity
    plist.write_bytes(plistlib.dumps(info))
    subprocess.run(["/usr/bin/codesign", "--force", "--deep", "--sign", "-", str(target)],
                   check=True, capture_output=True, text=True)
    return target / "Contents/MacOS" / executable.name, {
        "bundle_identifier": identity, "cache_path": str(cache), "cache_existed_before": False,
        "bundle_info_sha256": digest(plist)}


def seed_recordings(cache):
    return {kind: len(list(cache.glob("metal/v1/*/" + kind + "/*")))
            for kind in ("sources", "descriptors", "runtime-pipelines")}


def run_capture(name, args, env, cache):
    executable, isolation = args.executable, None
    if getattr(args, "fresh_metal_cache", False):
        executable, isolation = isolated_app(executable, args.output, name)
    data = args.output / (name + "-data")
    if args.data_seed:
        shutil.copytree(args.data_seed, data)
    else:
        data.mkdir()
    shots = args.output / (name + "-frames")
    shots.mkdir()
    live_input = args.output / (name + "-input.txt")
    live_input.write_text("")
    snapshot = Path(tempfile.gettempdir()) / ("liberty-frame-" + uuid.uuid4().hex + ".bmp")
    game_env = dict(env, RECOMP_METAL_CACHE_ROOT=str(cache), XDG_DATA_HOME=str(data),
                    GTAIV_BENCH=args.scene, GTAIV_BENCH_GAMEPLAY="1", GTAIV_FPS_REPORT="5",
                    GTAIV_SHOT_EVERY="0", GTAIV_INPUT_LIVE=str(live_input), GTAIV_FPS_LIMIT="60", GTAIV_CONTINUE="1",
                    GTAIV_SKIP_INTRO="1", GTAIV_SAVE_SYNC="0", GTAIV_BACKGROUND="1", GTAIV_HITCH="33.333",
                    GTAIV_FULLSCREEN="window", GTAIV_HIDDEN="1", RECOMP_SHADER_CACHE_REPORT="1")
    game_env.pop("GTAIV_LOG", None)
    command = [str(executable), "--no-launcher", "--game-dir", str(args.game), "--",
               "-windowed", "-width", "1280", "-height", "720", "-norestrictions", "-availablevidmem", "2047"]
    records = {"startup": [], "gameplay": []}
    drivers = {phase: [] for phase in records}
    uses = {phase: {kind: dict.fromkeys(("runtime", "archive", "preloaded"), 0)
                    for kind in ("draws", "binds")} for phase in records}
    persistence = {phase: dict.fromkeys(("saved", "failed", "busy"), 0) for phase in records}
    preload = None
    preload_resident_bytes = None
    library_persistence = {phase: dict.fromkeys(("saved", "failed", "busy", "deferred"), 0) for phase in records}
    encoding_pipelines = {phase: [] for phase in records}
    hitches = {phase: [] for phase in records}
    frame_reports = {phase: [] for phase in records}
    events = queue.Queue()
    process = subprocess.Popen(command, env=game_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, bufsize=1)
    def read():
        for line in process.stdout:
            events.put((time.monotonic(), line))
        events.put((time.monotonic(), None))
    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    started = time.monotonic()
    wall_started = time.time()
    last_poll = started
    def checked_time():
        nonlocal last_poll
        now = time.monotonic()
        # Darwin uptime and wall clocks can account for system sleep differently.
        # A suspended capture cannot attribute that delay to Metal compilation.
        if abs((time.time() - wall_started) - (now - started)) > 2 or now - last_poll > 10:
            raise RuntimeError(name + ": system suspension or clock discontinuity invalidated capture")
        last_poll = now
        return now
    placed = None
    player_vehicle = None
    try:
        with open(args.output / (name + ".log"), "w") as log:
            while True:
                now = checked_time()
                if placed is not None and now - placed >= args.seconds:
                    break
                if now - started > args.startup_timeout and placed is None:
                    raise RuntimeError(name + ": did not reach the benchmark scene; inspect its log")
                remaining = (placed + args.seconds if placed is not None else started + args.startup_timeout) - now
                try:
                    timestamp, line = events.get(timeout=max(0.001, min(1, remaining)))
                except queue.Empty:
                    continue
                if placed is not None and timestamp >= placed + args.seconds:
                    break
                if line is None:
                    raise RuntimeError(name + ": game exited before capture completed")
                log.write(line)
                log.flush()
                match = VEHICLE.search(line)
                if match:
                    player_vehicle = int(match[1])
                    if placed is not None and player_vehicle:
                        raise RuntimeError(name + ": player vehicle state changed during measurement")
                if "bench: gameplay ready" in line and placed is None:
                    if player_vehicle != 0:
                        raise RuntimeError(name + ": player vehicle state was not normalized before measurement")
                    placed = timestamp
                    log.write("capture: gameplay measurement begins\n")
                phase = "gameplay" if placed is not None else "startup"
                match = ENCODING_PIPELINE.search(line)
                if match:
                    encoding_pipelines[phase].append(float(match[1]))
                match = HITCH.search(line)
                # A frame spanning scene placement includes startup work.
                if match and (placed is None or float(match[1]) <= (timestamp - placed) * 1000):
                    hitches[phase].append(float(match[1]))
                match = FRAME_INTERVALS.search(line)
                # Skip the first FPS window crossing the scene-placement boundary.
                if match and (placed is None or timestamp - placed >= 5):
                    frame_reports[phase].append(tuple(float(v) for v in match.groups()))
                match = LOOKUP.search(line)
                if match:
                    kind, result, ms, work_phase, thread = match.groups()
                    records[phase].append(
                        {"kind": kind, "result": result, "ms": float(ms),
                         "phase": work_phase or "unknown", "thread": thread})
                match = DRIVER.search(line)
                if match:
                    result, ms, work_phase, thread = match.groups()
                    drivers[phase].append({"result": result, "ms": float(ms), "phase": work_phase, "thread": thread})
                match = USE.search(line)
                if match:
                    for origin, count in zip(("runtime", "archive", "preloaded"), match.groups()[1:]):
                        uses[phase][match[1]][origin] += int(count)
                match = PERSIST.search(line)
                if match:
                    persistence[phase][match[2]] += 1
                match = LIBRARY_PERSIST.search(line)
                if match:
                    library_persistence[phase][match[1]] += 1
                match = PRELOAD.search(line)
                if match:
                    preload = {"ready": int(match[1]), "skipped": int(match[2]), "ms": float(match[3])}
                    try:
                        preload_resident_bytes = int(subprocess.check_output(
                            ["/bin/ps", "-o", "rss=", "-p", str(process.pid)], text=True, stderr=subprocess.DEVNULL)) * 1024
                    except (OSError, ValueError, subprocess.CalledProcessError):
                        pass
            log.write("capture: gameplay measurement ends; requesting frame sample\n")
            log.flush()
            # Readback happens after the timed interval, so it cannot add a
            # screenshot-induced hitch to either measurement.
            with live_input.open("a") as commands:
                commands.write("shot " + str(snapshot) + "\n")
            deadline = time.monotonic() + 10
            captured = False
            while time.monotonic() < deadline and process.poll() is None:
                checked_time()
                if snapshot.exists():
                    with snapshot.open("rb") as image:
                        header = image.read(6)
                    if len(header) == 6 and header[:2] == b"BM":
                        size = int.from_bytes(header[2:6], "little")
                        if size > 54 and snapshot.stat().st_size >= size:
                            shutil.copy2(snapshot, shots / "gameplay.bmp")
                            captured = True
                            break
                time.sleep(0.05)
            if not captured:
                raise RuntimeError(name + ": no gameplay frame sample was produced")
    finally:
        stop(process)
        reader.join(timeout=5)
        process.stdout.close()
        snapshot.unlink(missing_ok=True)
    def summarize(phase, items):
        disk = [x for x in items if x["result"] in {"hit", "miss"}]
        encoder = [x for x in drivers[phase] if x["phase"] == "encoder"]
        return {"lookups": len(disk), "hits": sum(x["result"] == "hit" for x in items),
                "misses": sum(x["result"] == "miss" for x in items),
                "misses_over_threshold": sum(x["result"] == "miss" and x["ms"] > args.threshold_ms for x in items),
                "all_lookups_over_threshold": sum(x["ms"] > args.threshold_ms for x in disk),
                "lookup_ms": round(sum(x["ms"] for x in disk), 3),
                "max_lookup_ms": max((x["ms"] for x in disk), default=0),
                "disk_accesses_by_kind": {kind: {result: sum(x["kind"] == kind and x["result"] == result for x in disk)
                                                 for result in ("hit", "miss")} for kind in ("library", "pipeline", "compute")},
                "lookups_by_work_phase": {work_phase: sum(x["phase"] == work_phase for x in items)
                                          for work_phase in sorted({x["phase"] for x in items})},
                "ready_pipeline_lookups": sum(x["kind"] == "pipeline" and x["result"] == "ready" for x in items),
                "memory_library_lookups": sum(x["kind"] == "library" and x["result"] == "memory" for x in items),
                "shader_draws": uses[phase]["draws"], "shader_binds": uses[phase]["binds"],
                "runtime_archive_writes": persistence[phase],
                "runtime_library_writes": library_persistence[phase],
                "encoder_driver": {result: {
                    "calls": sum(x["result"] == result for x in encoder),
                    "ms": round(sum(x["ms"] for x in encoder if x["result"] == result), 3),
                    "over_threshold": sum(x["result"] == result and x["ms"] > args.threshold_ms for x in encoder)}
                    for result in ("hit", "miss", "rejected")},
                "encoding_pipeline_calls_over_threshold": sum(ms > args.threshold_ms for ms in encoding_pipelines[phase]),
                "frame_hitches_over_33ms": len(hitches[phase]),
                "max_frame_hitch_ms": max(hitches[phase], default=0),
                "max_reported_frame_p99_ms": max((v[1] for v in frame_reports[phase]), default=None)}
    result = {phase: summarize(phase, items) for phase, items in records.items()}
    result["startup_preload"] = preload
    result["resident_bytes_at_preload"] = preload_resident_bytes
    result["seconds_to_gameplay"] = round(placed - started, 3)
    result["player_vehicle_state"] = "on-foot"
    if isolation:
        metal_cache = Path(isolation["cache_path"]) / "com.apple.metal"
        files = [p for p in metal_cache.rglob("*") if p.is_file()]
        isolation.update(metal_cache_created=metal_cache.is_dir(), metal_cache_file_count=len(files),
                         metal_cache_bytes=sum(p.stat().st_size for p in files),
                         executable_sha256=digest(executable))
        if not files:
            raise RuntimeError("Fresh bundle did not create its own Metal cache; isolation is unverified")
        result["metal_cache_isolation"] = isolation
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--prepared-cache", type=Path, help="Reuse a completed preparation instead of building it again")
    parser.add_argument("--data-seed", type=Path, help="Copy the same XDG_DATA_HOME snapshot into each run")
    parser.add_argument("--scene", default="758.47,-216.85,5.82,93.5,12,0")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--prime-system-cache", action=argparse.BooleanOptionalAction, default=True,
                        help="Run both shader-library paths before measuring to reduce system-cache/order bias")
    parser.add_argument("--fresh-metal-cache", action="store_true",
                        help="Use a new signed diagnostic bundle per run; verify separate Metal caches, skip priming")
    parser.add_argument("--first-play", action="store_true", help="Require a seed without gameplay recordings or runtime archives")
    parser.add_argument("--order", choices=("cold-first", "prepared-first"), default="cold-first")
    parser.add_argument("--startup-timeout", type=int, default=240)
    parser.add_argument("--threshold-ms", type=float, default=2)
    args = parser.parse_args()
    if args.fresh_metal_cache:
        args.prime_system_cache = False
    capture_script_sha256 = digest(Path(__file__))
    args.executable = args.executable.resolve()
    args.game = args.game.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    prepared = args.prepared_cache.resolve() if args.prepared_cache else args.output / "prepared-seed"
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    env.update(GTAIV_GFX="metal", RECOMP_UPSCALER="taa")
    # Prepare first. Both runtime-library paths also need their own priming
    # run: Metal may cache source-built and offline-built libraries differently.
    action = "status" if args.prepared_cache else "prepare"
    command = [str(args.executable), "--shader-preparation", action, "--game-dir", str(args.game), "--cache-root", str(prepared)]
    with open(args.output / "preparation.jsonl", "w") as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    lines = (args.output / "preparation.jsonl").read_text().splitlines()
    final = json.loads(lines[-1])
    if result.returncode or final.get("status") != "Prepared":
        raise RuntimeError("Preparation did not finish successfully; inspect preparation.jsonl")
    recordings = seed_recordings(prepared)
    if args.first_play and any(recordings.values()):
        raise RuntimeError("First-play capture requires a baseline seed without gameplay recordings or runtime archives")
    warmup_results = {}
    if args.prime_system_cache:
        warmup_results["cold"] = run_capture("prime-cold", args, env, args.output / "prime-cold-cache")
        prime_prepared = args.output / "prime-prepared-cache"
        snapshot_cache(prepared, prime_prepared)
        warmup_results["prepared"] = run_capture("prime-prepared", args, env, prime_prepared)
    measured_prepared = args.output / "prepared-runtime-cache"
    snapshot_cache(prepared, measured_prepared)
    summary = {"captured_at_utc": datetime.now(timezone.utc).isoformat(),
               "macos_version": platform.mac_ver()[0], "architecture": platform.machine(),
               "capture_script_sha256": capture_script_sha256,
               "executable_sha256": digest(args.executable), "game_exe_sha256": digest(args.game / "GTAIV.exe"),
               "runtime_library_sha256": {p.name: digest(p) for directory in
                                           (args.executable.parent.parent / "Frameworks", args.executable.parent / "x86recomp")
                                           if directory.is_dir() for p in sorted(directory.glob("librecomp_*.dylib"))},
               "scene": args.scene, "gameplay_seconds": args.seconds, "threshold_ms": args.threshold_ms, "frame_hitch_threshold_ms": 33.333,
               "settings": {k: v for k, v in env.items() if k.startswith(("GTAIV_", "RECOMP_"))},
               "scope": "Fresh diagnostic bundle and verified separate Metal cache for each run" if args.fresh_metal_cache else
                        "Feature cache cold versus a prepared snapshot; source and offline-library paths primed independently" if args.prime_system_cache else "System cache not explicitly primed",
               "first_play": args.first_play, "seed_recordings": recordings, "run_order": args.order,
               "player_control_verified": True, "system_cache_primed": args.prime_system_cache,
               "warmup_results": warmup_results}
    for name in (("cold", "prepared") if args.order == "cold-first" else ("prepared", "cold")):
        summary[name] = run_capture(name, args, env, args.output / "cold-cache" if name == "cold" else measured_prepared)
    cold, prepared = (summary[name]["gameplay"] for name in ("cold", "prepared"))
    summary["fewer_gameplay_compilation_stalls"] = (
        prepared["encoder_driver"]["miss"]["over_threshold"] < cold["encoder_driver"]["miss"]["over_threshold"] and
        prepared["encoding_pipeline_calls_over_threshold"] < cold["encoding_pipeline_calls_over_threshold"])
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
