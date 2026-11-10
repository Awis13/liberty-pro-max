#!/usr/bin/env python3
"""Measure full launcher cache checks against an unchanged, populated cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("game", type=Path)
    parser.add_argument("cache", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--min-speedup", type=float, default=5)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    env = {k: v for k, v in os.environ.items() if not k.startswith("RECOMP_")}
    command = [str(args.worker.resolve()), "--shader-preparation", "status",
               "--game-dir", str(args.game.resolve()), "--cache-root", str(args.cache.resolve())]

    def check():
        started = time.monotonic()
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120, check=True)
        elapsed = time.monotonic() - started
        event = json.loads(result.stdout)
        if event["status"] != "Prepared" or event["failed"]:
            raise RuntimeError(f"Expected a populated Prepared cache: {event}")
        return elapsed, event

    # Prime both paths equally; timings still include a fresh process each time.
    _, event = check()
    state = Path(event["log"]).parent / "state.json"
    state_hash = hashlib.sha256(state.read_bytes()).hexdigest()
    samples = []
    for _ in range(args.runs):
        elapsed, current = check()
        if current != event or hashlib.sha256(state.read_bytes()).hexdigest() != state_hash:
            raise RuntimeError("Cache changed during measurement")
        samples.append(elapsed)
        print(f"status: {elapsed:.3f}s", flush=True)
    content = hashlib.sha256((args.game / "GTAIV.exe").read_bytes())
    for file in sorted((args.game / "common/shaders").glob("win32_30*/*.fxc")):
        content.update(str(file.relative_to(args.game)).encode())
        content.update(file.read_bytes())
    workload = {"state_sha256": state_hash, "game_sha256": content.hexdigest(),
                "status": event["status"], "size": event["size"]}
    summary = {"seconds": samples, "median_seconds": statistics.median(samples), "workload": workload,
               "cpu_count": os.cpu_count(),
               "worker_sha256": hashlib.sha256(args.worker.read_bytes()).hexdigest(), "command": command}
    if args.baseline:
        baseline = json.loads(args.baseline.read_text())
        if baseline["workload"] != workload:
            raise RuntimeError("Baseline workload differs")
        summary["speedup"] = baseline["median_seconds"] / summary["median_seconds"]
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    if args.baseline and summary["speedup"] < args.min_speedup:
        raise SystemExit(f"Speedup below {args.min_speedup:g}x target")


if __name__ == "__main__":
    main()
