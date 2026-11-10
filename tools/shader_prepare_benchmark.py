#!/usr/bin/env python3
"""Time actual preparation phases in a new, isolated cache per invocation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path, help="New directory for cache, logs and timings")
    parser.add_argument("--baseline", type=Path, help="Earlier summary.json to compare")
    parser.add_argument("--min-speedup", type=float, default=5)
    parser.add_argument("--shaders-only", action="store_true", help="Cancel after shader compilation; compare that phase only")
    parser.add_argument("--renderer", type=Path, help="Renderer dylib (defaults to the worker build's x86recomp directory)")
    args = parser.parse_args()
    renderer = args.renderer or args.worker.resolve().parent / "x86recomp/librecomp_gfx.dylib"
    renderer_hash = hashlib.sha256(renderer.read_bytes()).hexdigest()
    args.output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    # Scheduling overrides do not change shader settings or binary identities.
    env.update({k: v for k, v in os.environ.items() if k.startswith("RECOMP_METAL_")})
    started = time.monotonic()
    phase, phase_start = "Discovering", started
    phases, counts = {}, {}
    with (args.output / "stderr.log").open("w") as errors, (args.output / "events.jsonl").open("w") as events:
        process = subprocess.Popen([str(args.worker.resolve()), "--shader-preparation", "prepare",
                                    "--game-dir", str(args.game.resolve()),
                                    "--cache-root", str((args.output / "cache").resolve())],
                                   env=env, stdout=subprocess.PIPE, stderr=errors, text=True)
        final, stopped = {}, False
        for line in process.stdout:
            now = time.monotonic()
            event = json.loads(line)
            events.write(json.dumps({"seconds": now - started, **event}) + "\n")
            events.flush()
            current = event.get("phase", phase)
            if current != phase:
                phases[phase] = now - phase_start
                print(f"{phase}: {phases[phase]:.3f}s", flush=True)
                phase, phase_start = current, now
            if isinstance(event.get("total"), int):
                counts[current] = event["total"]
            if args.shaders_only and current == "Building pipelines" and not stopped:
                process.terminate()
                stopped = True
            final = event
        result = process.wait()
    finished = time.monotonic()
    phases[phase] = finished - phase_start
    content = hashlib.sha256()
    for file in sorted((args.game / "common/shaders").glob("win32_30*/*.fxc")):
        content.update(str(file.relative_to(args.game)).encode())
        content.update(file.read_bytes())
    summary = {"seconds": finished - started, "phases": phases, "counts": counts,
               "status": final.get("status"), "failed": final.get("failed"), "exit_code": result,
               "metric": "Compiling shaders" if args.shaders_only else "total",
               "cpu_count": os.cpu_count(),
               "worker_sha256": hashlib.sha256(args.worker.read_bytes()).hexdigest(),
               "renderer_sha256": renderer_hash,
               "game_exe_sha256": hashlib.sha256((args.game / "GTAIV.exe").read_bytes()).hexdigest(),
               "shader_content_sha256": content.hexdigest(),
               "scheduling": {k: v for k, v in env.items() if k.startswith("RECOMP_METAL_")}}
    if args.baseline:
        baseline = json.loads(args.baseline.read_text())
        if (baseline["counts"] != counts or baseline["game_exe_sha256"] != summary["game_exe_sha256"] or
                baseline["shader_content_sha256"] != summary["shader_content_sha256"]):
            raise RuntimeError("Baseline workload differs")
        summary["speedup"] = (baseline["phases"]["Compiling shaders"] / phases["Compiling shaders"]
                              if args.shaders_only else baseline["seconds"] / summary["seconds"])
        summary["phase_speedups"] = {k: v / phases[k] for k, v in baseline["phases"].items()
                                     if not args.shaders_only or k == "Compiling shaders"}
    if hashlib.sha256(renderer.read_bytes()).hexdigest() != renderer_hash:
        raise RuntimeError("Renderer changed during measurement")
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    expected = (130, "Incomplete") if args.shaders_only else (0, "Prepared")
    if (result, summary["status"]) != expected or summary["failed"] != 0:
        raise SystemExit("Preparation failed; inspect stderr.log")
    if args.baseline and summary["speedup"] < args.min_speedup:
        raise SystemExit(f"Speedup below {args.min_speedup:g}x target")


if __name__ == "__main__":
    main()
