#!/usr/bin/env python3
"""Run one order-balanced block; full preparation must already be done manually."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import time
from types import SimpleNamespace

from performance_compare import capture, conflicts
from shader_capture import digest, isolated_app, seed_recordings, snapshot_cache


def inventory(cache):
    return {str(p.relative_to(cache)): {"bytes": p.stat().st_size,
            "sha256": digest(p), "mtime_ns": p.stat().st_mtime_ns}
            for p in cache.rglob("*.metallib")}


def legacy_inventory(cache):
    return {p.name: {"bytes": p.stat().st_size, "sha256": digest(p)}
            for p in (cache / "legacy").glob("*.bin")}


def pair(condition, args):
    name = f"block-{args.block}-{condition}"
    output = args.output.resolve() / name
    cache = output / "cache"
    if args.resume and output.exists():
        meta = json.loads((output / "pair.json").read_text())
        isolation = meta["metal_cache"]
        app = output / "diagnostic.app/Contents/MacOS/gtaiv"
        meta["resumed_utc"] = datetime.now(timezone.utc).isoformat()
        meta["interrupted_attempts"] = [str(p.relative_to(output)) for p in output.glob("*-suspended")]
    else:
        output.mkdir(parents=True, exist_ok=False)
        executable = args.main if condition == "main" else args.feature
        app, isolation = isolated_app(executable.resolve(), output, "diagnostic")
        if condition == "prepared":
            if any(seed_recordings(args.prepared_cache).values()):
                raise RuntimeError("Prepared first-play seed contains gameplay observations")
            snapshot_cache(args.prepared_cache.resolve(), cache)
        else:
            cache.mkdir()
        meta = {"condition": condition, "block": args.block,
                "started_utc": datetime.now(timezone.utc).isoformat(), "metal_cache": isolation,
                "prepared_seed": str(args.prepared_cache.resolve()) if condition == "prepared" else None,
                "fps_limit": args.fps_limit, "runs": []}
    (output / "pair.json").write_text(json.dumps(meta, indent=2) + "\n")
    for state in ["first", "restart"]:
        if any(run["state"] == state for run in meta["runs"]):
            continue
        if shutil.disk_usage(output).free < 2 * 1024**3:
            raise RuntimeError("Insufficient disk reserve for capture")
        if conflicts():
            raise RuntimeError("Another measurement/build is active")
        before = inventory(cache)
        (output / (state + "-cache-before.json")).write_text(json.dumps(before, indent=2))
        legacy_before = legacy_inventory(cache)
        (output / (state + "-legacy-before.json")).write_text(json.dumps(legacy_before, indent=2))
        result = capture(SimpleNamespace(output=output / state, executable=app, game=args.game,
                         probe=args.probe, cache=cache, data_seed=None, fps_limit=args.fps_limit,
                         scene=args.scene, seconds=args.seconds, startup_timeout=args.startup_timeout))
        after = inventory(cache)
        (output / (state + "-cache-after.json")).write_text(json.dumps(after, indent=2))
        legacy_after = legacy_inventory(cache)
        (output / (state + "-legacy-after.json")).write_text(json.dumps(legacy_after, indent=2))
        unchanged = sum(after.get(k) == v for k, v in before.items())
        meta["runs"].append({"state": state, "output": str(output / state),
                             "seconds_to_gameplay": result["seconds_to_gameplay"],
                             "gpu_files_before": len(before), "gpu_files_after": len(after),
                             "prior_gpu_files_unchanged": unchanged})
        meta["runs"][-1].update(legacy_cache_before=legacy_before, legacy_cache_after=legacy_after)
        metal = Path(isolation["cache_path"]) / "com.apple.metal"
        files = [p for p in metal.rglob("*") if p.is_file()]
        meta["metal_cache"].update(metal_cache_created=bool(files), file_count=len(files),
                                    bytes=sum(p.stat().st_size for p in files))
        (output / "pair.json").write_text(json.dumps(meta, indent=2) + "\n")
        if not files:
            raise RuntimeError("Per-bundle Metal cache was not created")
        print(json.dumps({"run": name + "/" + state, "fps": result["frames"]["fps"],
              "hitches": result["frames"]["hitches"]["33.333"]["count"],
              "footprint_MiB": result["memory"]["settled_median_bytes"]["footprint_bytes"]/2**20,
              "gpu_files": len(after)}), flush=True)
        time.sleep(10)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for option in ["main", "feature", "game", "output", "probe", "prepared-cache"]:
        p.add_argument("--" + option, type=Path, required=True)
    p.add_argument("--block", type=int, choices=[1, 2], required=True)
    p.add_argument("--conditions", nargs="+", choices=["main", "empty", "prepared"])
    p.add_argument("--resume", action="store_true", help="Keep completed captures; resume an interrupted pair")
    p.add_argument("--fps-limit", type=int, default=60)
    p.add_argument("--seconds", type=float, default=60)
    p.add_argument("--startup-timeout", type=float, default=600)
    p.add_argument("--scene", default="758.47,-216.85,5.82,93.5,12,0")
    args = p.parse_args()
    conditions = args.conditions or (["main", "empty", "prepared"] if args.block == 1
                                     else ["prepared", "empty", "main"])
    for condition in conditions:
        pair(condition, args)
