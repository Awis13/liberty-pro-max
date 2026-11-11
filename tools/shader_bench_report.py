#!/usr/bin/env python3
"""The summary of one shader benchmark run (tools/shader_bench.sh): what
the game's log says about compiling shaders and pipelines, and the stutter.

  shader_bench_report.py game.log [--json summary.json] [--label cold]
  shader_bench_report.py --table a/summary.json b/summary.json

Read from the log (the runtime's switches the script sets):
- "gfx: pipeline N compiled in X ms": pipelines compiled while drawing that
  took over 2 ms (counted from 20 ms, and the worst);
- "gfx: perf: ... in N frames: ... S shaders (X ms, M MSL compiles), P
  pipelines compiled while drawing (X ms, max Y)" (<ID>_PERF, every 2 s):
  summed, the max the largest;
- the same lines' "; pipeline archive: N shards, H found, M missed (X ms),
  A added, P to add, last saved in S ms (B MB)" (the runtime's pipeline
  archive): found, missed and their ms summed (each report counts its own
  2 s); shards, added (in all since the start), to add and the last save
  as the section's last report has them (a save of -1 ms: none yet);
- "hitch: frame N took X ms (t=T s)" (<ID>_HITCH=50): frames from 50 ms;
- "fps: F at t=T s (interval ms: min . p1 . median . ...)" (<ID>_FPS_REPORT);
- "load: loading screen down at T s" (GTAIV_LOAD_LOG=1, seconds since the
  game started): the time to the first scene is the last one before
- "gtaiv: bench: player placed at ..." (GTAIV_BENCH): the scene starts.

"scene" counts only what the log has after the scene started (lines after
the placement; without one, after the last loading screen went down;
without either, the whole log). The log's clocks differ (hitches and fps
from the first frame shown, loading screens from the start), so the scene
is told apart by the lines' order, not by their times.
"""
import argparse
import json
import re
import sys

COMPILE_SLOW_MS = 20.0
HITCH_MS = 50.0

RE_COMPILE = re.compile(r"gfx: pipeline \d+ compiled in ([\d.]+) ms")
RE_PERF = re.compile(
    r"gfx: perf: .*?in (\d+) frames: .*?(\d+) shaders \(([\d.]+) ms, (\d+) MSL compiles\), "
    r"(\d+) pipelines compiled while drawing \(([\d.]+) ms, max ([\d.]+)\)"
    r"(?:, (\d+) built ahead used)?")
RE_ARCHIVE = re.compile(
    r"; pipeline archive: (\d+) shards, (\d+) found, (\d+) missed \(([\d.]+) ms\), (\d+) added, "
    r"(\d+) to add, last saved in (-?[\d.]+) ms \(([\d.]+) MB\)")
RE_HITCH = re.compile(r"hitch: frame \d+ took ([\d.]+) ms \(t=([\d.]+) s\)")
RE_FPS = re.compile(r"fps: ([\d.]+) at t=([\d.]+) s \(interval ms: min [\d.]+ p1 ([\d.]+) median ([\d.]+) "
                    r"p99 ([\d.]+) max ([\d.]+)")
RE_LOAD_DOWN = re.compile(r"load: loading screen down at ([\d.]+) s")
RE_PLACED = re.compile(r"gtaiv: bench: player placed at")
RE_FARM_STOPPED = re.compile(r"farm: .*measurement stopped")
RE_MSL_CACHED = re.compile(r"gfx: (\d+) translated shaders from earlier runs")


def percentile(values, p):
    """The nearest-rank percentile (as the runtime's own reports pick it)."""
    if not values:
        return None
    v = sorted(values)
    return v[int(p * (len(v) - 1) + 0.5)]


def _empty_perf():
    return {"reports": 0, "frames": 0, "shaders": 0, "shader_ms": 0.0, "msl_compiles": 0,
            "pipelines_while_drawing": 0, "pipeline_ms": 0.0, "pipeline_max_ms": 0.0,
            "built_ahead_used": 0}


def _empty_archive():
    return {"reports": 0, "shards": None, "found": 0, "missed": 0, "missed_ms": 0.0, "added": None,
            "to_add": None, "last_save_ms": None, "last_save_mb": None}


def _section(lines):
    compiles, hitches, fps = [], [], []
    perf = _empty_perf()
    archive = _empty_archive()
    for line in lines:
        m = RE_COMPILE.search(line)
        if m:
            compiles.append(float(m.group(1)))
            continue
        m = RE_PERF.search(line)
        if m:
            perf["reports"] += 1
            perf["frames"] += int(m.group(1))
            perf["shaders"] += int(m.group(2))
            perf["shader_ms"] += float(m.group(3))
            perf["msl_compiles"] += int(m.group(4))
            perf["pipelines_while_drawing"] += int(m.group(5))
            perf["pipeline_ms"] += float(m.group(6))
            perf["pipeline_max_ms"] = max(perf["pipeline_max_ms"], float(m.group(7)))
            if m.group(8):
                perf["built_ahead_used"] += int(m.group(8))
            a = RE_ARCHIVE.search(line)
            if a:
                archive["reports"] += 1
                archive["shards"] = int(a.group(1))
                archive["found"] += int(a.group(2))
                archive["missed"] += int(a.group(3))
                archive["missed_ms"] += float(a.group(4))
                archive["added"] = int(a.group(5))
                archive["to_add"] = int(a.group(6))
                if float(a.group(7)) >= 0:
                    archive["last_save_ms"] = float(a.group(7))
                    archive["last_save_mb"] = float(a.group(8))
            continue
        m = RE_HITCH.search(line)
        if m:
            hitches.append({"ms": float(m.group(1)), "t": float(m.group(2))})
            continue
        m = RE_FPS.search(line)
        if m:
            fps.append({"fps": float(m.group(1)), "t": float(m.group(2)), "p1_ms": float(m.group(3)),
                        "median_ms": float(m.group(4)), "p99_ms": float(m.group(5)),
                        "max_ms": float(m.group(6))})
    perf["shader_ms"] = round(perf["shader_ms"], 1)
    perf["pipeline_ms"] = round(perf["pipeline_ms"], 1)
    archive["missed_ms"] = round(archive["missed_ms"], 1)
    slow = [c for c in compiles if c >= COMPILE_SLOW_MS]
    big = [h for h in hitches if h["ms"] >= HITCH_MS]
    rates = [f["fps"] for f in fps]
    return {
        "compiles_logged": len(compiles),
        "compiles_slow": len(slow),
        "compile_worst_ms": max(compiles) if compiles else None,
        "perf": perf,
        "archive": archive,
        "hitches": len(big),
        "hitch_worst_ms": max(h["ms"] for h in big) if big else None,
        "fps_reports": len(fps),
        "fps_p1": percentile(rates, 0.01),
        "fps_median": percentile(rates, 0.5),
        "interval_p99_worst_ms": max(f["p99_ms"] for f in fps) if fps else None,
        "interval_max_worst_ms": max(f["max_ms"] for f in fps) if fps else None,
    }


def summarize(text, label=None):
    lines = text.splitlines()
    placed = next((i for i, line in enumerate(lines) if RE_PLACED.search(line)), None)
    downs = [(i, float(m.group(1))) for i, line in enumerate(lines) for m in [RE_LOAD_DOWN.search(line)] if m]
    before = [d for d in downs if placed is None or d[0] < placed]
    if placed is not None:
        start, marker = placed + 1, "bench placement"
    elif downs:
        start, marker = downs[-1][0] + 1, "last loading screen down"
    else:
        start, marker = 0, "none (whole log)"
    cached = [int(m.group(1)) for line in lines for m in [RE_MSL_CACHED.search(line)] if m]
    return {
        "label": label,
        "scene_marker": marker,
        "first_scene_s": before[-1][1] if before else None,
        "first_scene_from": "last loading screen down before the scene, s since start" if before else
                            "not in the log (no load: lines; GTAIV_LOAD_LOG=1)",
        "msl_from_earlier_runs": cached[0] if cached else 0,
        "farm_interrupted": any(RE_FARM_STOPPED.search(line) for line in lines),
        "all": _section(lines),
        "scene": _section(lines[start:]),
    }


def _fmt(v, digits=1):
    if v is None:
        return "-"
    if isinstance(v, float):
        return f"{v:.{digits}f}"
    return str(v)


def _archive(s, key):
    """The whole run's archive figure (none: no archive in the log, or a
    summary from before the archive)."""
    a = s["all"].get("archive")
    return a[key] if a and a["reports"] else None


ROWS = [
    ("time to first scene (s)", lambda s: s["first_scene_s"]),
    ("MSL from earlier runs", lambda s: s["msl_from_earlier_runs"]),
    ("pipelines >= 20 ms (all)", lambda s: s["all"]["compiles_slow"]),
    ("worst pipeline ms (all)", lambda s: s["all"]["compile_worst_ms"]),
    ("pipelines >= 20 ms (scene)", lambda s: s["scene"]["compiles_slow"]),
    ("worst pipeline ms (scene)", lambda s: s["scene"]["compile_worst_ms"]),
    ("PERF shaders", lambda s: s["all"]["perf"]["shaders"]),
    ("PERF shader ms", lambda s: s["all"]["perf"]["shader_ms"]),
    ("PERF MSL compiles", lambda s: s["all"]["perf"]["msl_compiles"]),
    ("PERF pipelines while drawing", lambda s: s["all"]["perf"]["pipelines_while_drawing"]),
    ("PERF pipeline ms", lambda s: s["all"]["perf"]["pipeline_ms"]),
    ("PERF pipeline max ms", lambda s: s["all"]["perf"]["pipeline_max_ms"]),
    ("PERF built ahead used", lambda s: s["all"]["perf"]["built_ahead_used"]),
    ("archive shards", lambda s: _archive(s, "shards")),
    ("archive found", lambda s: _archive(s, "found")),
    ("archive missed", lambda s: _archive(s, "missed")),
    ("archive missed ms", lambda s: _archive(s, "missed_ms")),
    ("archive added", lambda s: _archive(s, "added")),
    ("archive to add", lambda s: _archive(s, "to_add")),
    ("archive last save ms", lambda s: _archive(s, "last_save_ms")),
    ("archive last save MB", lambda s: _archive(s, "last_save_mb")),
    ("hitches >= 50 ms (all)", lambda s: s["all"]["hitches"]),
    ("worst hitch ms (all)", lambda s: s["all"]["hitch_worst_ms"]),
    ("hitches >= 50 ms (scene)", lambda s: s["scene"]["hitches"]),
    ("worst hitch ms (scene)", lambda s: s["scene"]["hitch_worst_ms"]),
    ("fps p1 (scene reports)", lambda s: s["scene"]["fps_p1"]),
    ("fps median (scene reports)", lambda s: s["scene"]["fps_median"]),
    ("worst interval p99 ms (scene)", lambda s: s["scene"]["interval_p99_worst_ms"]),
]


def table(summaries):
    heads = [s.get("label") or f"run {i + 1}" for i, s in enumerate(summaries)]
    width = max(len(r[0]) for r in ROWS)
    cols = [max(8, len(h)) for h in heads]
    out = ["".ljust(width) + "".join("  " + h.rjust(c) for h, c in zip(heads, cols))]
    for name, get in ROWS:
        out.append(name.ljust(width) + "".join("  " + _fmt(get(s)).rjust(c) for s, c in zip(summaries, cols)))
    notes = sorted({s["scene_marker"] for s in summaries})
    out.append(f"(scene from: {', '.join(notes)})")
    if any(s["farm_interrupted"] for s in summaries):
        out.append("WARNING: the farm stopped a measurement and started it again: not comparable")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("logs", nargs="*", help="game.log files")
    ap.add_argument("--json", help="write the summary (one log) here")
    ap.add_argument("--label", help="the run's name in the table")
    ap.add_argument("--table", nargs="+", metavar="SUMMARY_JSON", help="a table of earlier summaries")
    o = ap.parse_args(argv)
    if o.table:
        summaries = [json.load(open(p)) for p in o.table]
    elif o.logs:
        summaries = []
        for p in o.logs:
            with open(p, errors="replace") as f:
                summaries.append(summarize(f.read(), o.label if len(o.logs) == 1 else p))
        if o.json:
            if len(summaries) != 1:
                ap.error("--json takes one log")
            with open(o.json, "w") as f:
                json.dump(summaries[0], f, indent=2)
                f.write("\n")
    else:
        ap.error("a game.log or --table")
    print(table(summaries))
    return 0


if __name__ == "__main__":
    sys.exit(main())
