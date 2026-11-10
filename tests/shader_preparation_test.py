#!/usr/bin/env python3
"""macOS GPU integration tests using hand-assembled, original shader fixtures.

  python3 tests/shader_preparation_test.py PATH/TO/recomp_shader_prepare

No game content or installed game is needed. Tests use an isolated temp cache.
"""
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

WORKER = ""


def dst(kind, n=0):
    return 0x80000000 | ((kind & 7) << 28) | ((kind & 24) << 8) | 0xF0000 | n


def src(kind, n=0):
    return (dst(kind, n) & ~0xF0000) | 0xE40000


def fixture(path, pixels=1, broken=False, technique="test", states=(), writes_depth=False,
            vertex_offset=0, pixel_offset=0):
    # vs_3_0: declare POSITION input/output, mov output,input; END.
    vertex = [0xFFFE0300, 0x0200001F, 0x80000000, dst(1),
              0x0200001F, 0x80000000, dst(6), 0x02000001, dst(6), src(1), 0xFFFF]
    if vertex_offset:
        value = struct.unpack("<I", struct.pack("<f", vertex_offset))[0]
        vertex[-1:] = [0x05000051, dst(2), value, 0, 0, 0,
                       0x03000002, dst(6), src(1), src(2), 0xFFFF]
    data = bytearray(b"rgxa")
    data += b"\x01"
    bytecode = struct.pack(f"<{len(vertex)}I", *vertex)
    data += b"\x00" + struct.pack("<HH", len(bytecode), 0) + bytecode
    data += bytes([pixels + 1]) + b"\x00" * 5
    for i in range(pixels):
        # ps_3_0: def c0, value,0,0,1; mov oC0,c0; END.
        value = struct.unpack("<I", struct.pack("<f", (i + 1 + pixel_offset) / 32))[0]
        pixel = [0xFFFF0300, 0x05000051, dst(2), value, 0, 0, 0x3F800000,
                 0x02000001, dst(8), src(2), 0xFFFF]
        if writes_depth:
            pixel[-1:] = [0x02000001, dst(9), src(2), 0xFFFF]
        code = struct.pack(f"<{len(pixel)}I", *pixel)
        data += b"\x00" + struct.pack("<HH", len(code), 0) + code
    # Empty global/local variable tables and one technique with actual pairs.
    name = technique.encode() + b"\x00"
    data += b"\x00\x00\x01" + bytes([len(name)]) + name + bytes([pixels])
    for i in range(pixels):
        data += bytes([0, i + 1, len(states)])
        for state, value in states:
            data += struct.pack("<II", state, value)
    if broken:
        data = data[:8]
    path.write_bytes(data)


def embedded_effect(executable, effect):
    """Original PE32 fixture with a named RAGE resource, no game bytes."""
    data = bytearray(0x400 + len(effect))
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", data, 0x86, 1)  # section count
    struct.pack_into("<H", data, 0x94, 0xE0)  # optional header size
    struct.pack_into("<H", data, 0x98, 0x10B)
    struct.pack_into("<I", data, 0x98 + 28, 0x400000)
    struct.pack_into("<4I", data, 0x178 + 8, len(data) - 0x200, 0x1000,
                     len(data) - 0x200, 0x200)
    name = b"win32_30/rage_perlinnoise.fxc\0"
    data[0x240:0x240 + len(name)] = name
    struct.pack_into("<4I", data, 0x200, 0x401040, 0x401200, len(effect), 0)
    data[0x400:] = effect
    executable.write_bytes(data)


class PreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not WORKER:
            raise RuntimeError("Pass the recomp_shader_prepare executable path")

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="liberty-shaders-")
        self.base = Path(self.tmp.name)
        self.game = self.base / "game"
        self.effects = self.game / "common/shaders/win32_30"
        self.effects.mkdir(parents=True)
        (self.game / "GTAIV.exe").write_bytes(b"original test fixture, no game content")
        self.effect = self.effects / "test.fxc"
        fixture(self.effect)
        self.cache = self.base / "cache"
        self.owned = self.cache / "metal"
        self.env = dict(os.environ)
        # Stable settings regardless of the invoking shell's renderer overrides.
        for key in list(self.env):
            if key.startswith("RECOMP_"):
                del self.env[key]

    def tearDown(self):
        self.tmp.cleanup()

    def args(self, action):
        return [WORKER, "--shader-preparation", action, "--game-dir", str(self.game),
                "--cache-root", str(self.cache)]

    def run_worker(self, action="prepare", expected=0):
        result = subprocess.run(self.args(action), env=self.env, capture_output=True, text=True, timeout=180)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(events, result.stderr)
        self.assertEqual(result.returncode, expected, result.stderr[-3000:])
        return events

    def status(self):
        return self.run_worker("status")[-1]["status"]

    def binaries(self):
        return {str(p.relative_to(self.owned)): (p.stat().st_mtime_ns, hashlib.sha256(p.read_bytes()).hexdigest())
                for p in self.owned.rglob("*.metallib")}

    def test_preparation_restart_reuse_corruption_and_clean(self):
        self.assertEqual(self.status(), "Not prepared")
        events = self.run_worker()
        self.assertEqual(events[0]["phase"], "Discovering")
        self.assertIsNone(events[0]["total"])
        self.assertEqual(events[-1]["status"], "Prepared")
        self.assertEqual(events[-1]["completed"], events[-1]["total"])
        self.assertEqual({e.get("phase") for e in events},
                         {"Discovering", "Compiling shaders", "Building pipelines"})
        for event in events:
            if isinstance(event.get("total"), int):
                self.assertLessEqual(event["completed"], event["total"])
        binaries = self.binaries()
        self.assertTrue(binaries)
        self.assertEqual(self.status(), "Prepared")  # fresh worker, persisted status
        recipe_file = next(self.owned.rglob("prepared-recipes-v1.bin"))
        recipes = recipe_file.read_bytes()
        self.assertTrue(recipes and len(recipes) % 48 == 0)
        for offset in range(0, len(recipes), 48):
            self.assertEqual(struct.unpack_from("<II", recipes, offset + 16), (0, 0))
        checksum = Path(str(recipe_file) + ".sha256").read_text()
        self.assertEqual(hashlib.sha256(recipes).hexdigest(), checksum)
        self.run_worker()
        self.assertEqual(self.binaries(), binaries)  # no library/archive rebuilds
        library = next(self.owned.rglob("libraries/*.metallib"))
        library.write_bytes(b"invalid cache entry")
        self.assertEqual(self.status(), "Needs preparation")
        self.run_worker()
        self.assertEqual(self.status(), "Prepared")
        recipe_file.write_bytes(b"invalid prebuild recipe metadata")
        self.assertEqual(self.status(), "Needs preparation")
        self.run_worker()
        self.assertEqual(self.status(), "Prepared")
        self.env["RECOMP_RENDER_SCALE"] = "0.5"
        self.assertEqual(self.status(), "Needs preparation")
        del self.env["RECOMP_RENDER_SCALE"]
        self.assertEqual(self.status(), "Prepared")
        (self.cache / "unrelated").write_text("keep")
        self.run_worker("clean")
        self.assertFalse(self.owned.exists())
        self.assertEqual((self.cache / "unrelated").read_text(), "keep")
        self.assertEqual(self.status(), "Not prepared")

    def test_reported_size_counts_hard_linked_artifacts_once(self):
        self.run_worker()
        result = self.run_worker("status")[-1]
        entries = [p for p in self.owned.rglob("*") if p.is_file() and not p.is_symlink()]
        unique = {(p.stat().st_dev, p.stat().st_ino): p.stat().st_size for p in entries}
        self.assertEqual(result["size"], sum(unique.values()))
        self.assertLess(result["size"], sum(p.stat().st_size for p in entries))

    def test_status_checks_all_binary_bytes_and_rejects_missing_entries(self):
        self.run_worker()
        library = next(self.owned.glob("v1/*/libraries/*.metallib"))
        original = library.read_bytes()
        stamp = library.stat()
        for offset in (0, len(original) // 2, len(original) - 1):
            with self.subTest(offset=offset):
                changed = bytearray(original)
                changed[offset] ^= 1
                library.write_bytes(changed)
                os.utime(library, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
                self.assertEqual(self.status(), "Needs preparation")
                library.write_bytes(original)
                self.assertEqual(self.status(), "Prepared")
        for changed in (original[:-1], b"", None):
            with self.subTest(length=None if changed is None else len(changed)):
                if changed is None:
                    library.unlink()
                else:
                    library.write_bytes(changed)
                self.assertEqual(self.status(), "Needs preparation")
                library.write_bytes(original)
                self.assertEqual(self.status(), "Prepared")

    def test_effect_pass_attachments_and_write_masks_are_prepared(self):
        fixture(self.effect, technique="deferred_draw", states=((19, 7), (20, 0), (21, 3), (22, 15)))
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        candidates = [p for p in entries.values() if p["vertex"].endswith("/main0")]
        self.assertTrue(any([a[0] for a in p["attachments"]] == [80, 80, 80, 25] and
                            [a[8] for a in p["attachments"]] == [14, 0, 12, 15]
                            for p in candidates), "FXC pass masks/MRT attachments were discarded")

    def test_effect_pass_blending_is_prepared(self):
        fixture(self.effect, technique="reflection_draw", states=((10, 1), (4, 9), (5, 10), (19, 7)))
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        self.assertTrue(any(p["vertex"].endswith("/main0") and p["attachments"][0][:5] == [80, 1, 6, 7, 0] and
                            p["attachments"][0][8] == 14 for p in entries.values()),
                        "FXC pass blend factors were discarded")

    def test_deferred_runtime_mask_overrides_are_prepared(self):
        fixture(self.effect, technique="deferred_draw")
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        self.assertTrue(any([a[0] for a in p["attachments"]] == [80, 80, 80, 25] and
                            [a[8] for a in p["attachments"]] == [14, 14, 14, 15]
                            for p in entries.values()), "Common RGB G-buffer override is missing")

    def test_particle_ldr_and_source_alpha_additive_states_are_prepared(self):
        fixture(self.effect, technique="draw")
        self.effect.rename(self.effects / "gpuptfx_test.fxc")
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        self.assertTrue(any(p["vertex"].endswith("/main0") and
                            p["attachments"][0][:5] == [80, 1, 4, 1, 0] and p["depth"] == 260
                            for p in entries.values()), "LDR source-alpha additive particles are missing")

    def test_gta_particle_unlit_additive_override_is_prepared(self):
        fixture(self.effect, technique="unlit_draw")
        self.effect.rename(self.effects / "gta_rmptfx_litsprite.fxc")
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        self.assertTrue(any(not p.get("core") and
                            p["attachments"][0][:4] == [115, True, 4, 1] and p["depth"] == 260
                            for p in entries.values()), "GTA-prefixed particle additive override is missing")

    def test_depth_writing_effect_skips_depthless_candidates(self):
        fixture(self.effect, technique="GTACompositePostFx", writes_depth=True)
        self.effect.rename(self.effects / "rage_postfx.fxc")
        self.assertEqual(self.run_worker()[-1]["status"], "Prepared")

    def test_reflection_hdr_override_is_prepared(self):
        fixture(self.effect, technique="reflection_draw", states=((10, 1), (4, 5), (5, 6)))
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        self.assertTrue(any(p["vertex"].endswith("/main0") and
                            p["attachments"][0][:4] == [115, False, 1, 0] and
                            p["depth"] == p["stencil"] == 260 and p["samples"] == 1
                            for p in entries.values()), "HDR reflection override is missing")

    def test_wd_scalar_alpha_override_is_prepared(self):
        fixture(self.effect, technique="wd_drawskinned", states=((10, 0),))
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        self.assertTrue(any(p["vertex"].endswith("/main0") and
                            p["attachments"][0] == [55, True, 4, 5, 0, 4, 5, 0, 15] and
                            p["depth"] == p["stencil"] == 260
                            for p in entries.values()), "Scalar shadow override is missing")

    def test_embedded_effects_are_prepared_without_recordings(self):
        fixture(self.effect, technique="draw")
        first = self.run_worker()
        count = max(e["total"] for e in first if e["phase"] == "Compiling shaders")
        embedded = self.base / "embedded.fxc"
        fixture(embedded, pixels=2, technique="draw", pixel_offset=10)
        embedded_effect(self.game / "GTAIV.exe", embedded.read_bytes())
        second = self.run_worker()
        self.assertGreater(max(e["total"] for e in second if e["phase"] == "Compiling shaders"), count)
        entries = json.loads(sorted(self.owned.rglob("prepared-descriptors.json"),
                                    key=lambda p: p.stat().st_mtime_ns)[-1].read_text())
        self.assertTrue(any(p["vertex"].endswith("/main0") and p["depth"] == 0 and
                            p["attachments"][0][:4] == [80, False, 1, 0]
                            for p in entries.values()), "Embedded Perlin depthless pass is missing")
        self.assertFalse(list(self.owned.rglob("sources/*.json")))
        self.assertFalse(list(self.owned.rglob("descriptors/*.json")))

    def test_geometry_stencil_pair_is_prepared(self):
        self.effect.unlink()
        geometry = self.effects / "gta_default.fxc"
        fixture(geometry, technique="draw", vertex_offset=1)
        self.run_worker()
        entries = json.loads(next(self.owned.rglob("prepared-descriptors.json")).read_text())
        vertex = next(p["vertex"] for p in entries.values() if not p.get("core"))
        geometry.unlink()
        fixture(self.effects / "rage_atmoscatt_clouds.fxc", technique="draw_stencil", pixel_offset=10)
        self.run_worker()
        catalogs = sorted(self.owned.rglob("prepared-descriptors.json"), key=lambda p: p.stat().st_mtime_ns)
        entries = json.loads(catalogs[-1].read_text())
        fragment = next(p["fragment"] for p in entries.values() if not p.get("core"))
        fixture(geometry, technique="draw", vertex_offset=1)
        self.run_worker()
        catalog = max(self.owned.rglob("prepared-descriptors.json"), key=lambda p: p.stat().st_mtime_ns)
        entries = json.loads(catalog.read_text())
        masks = {p["attachments"][0][8] for p in entries.values()
                 if p["vertex"] == vertex and p["fragment"] == fragment and
                 p["attachments"][0][:4] == [80, True, 1, 1] and p["depth"] == p["stencil"] == 260}
        self.assertEqual(masks, {0, 15}, "Geometry stencil/color-mask pairs are missing")

    def test_cancel_preserves_entries_and_resumes(self):
        fixture(self.effect, pixels=16)
        process = subprocess.Popen(self.args("prepare"), env=self.env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, bufsize=1)
        try:
            deadline = time.monotonic() + 90
            while True:
                self.assertLess(time.monotonic(), deadline)
                line = process.stdout.readline()
                self.assertTrue(line, "worker exited before cancellable work")
                event = json.loads(line)
                if event.get("phase") == "Compiling shaders" and event.get("completed", 0) >= 4:
                    break
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=20)
            self.assertEqual(process.returncode, 130, stderr)
            self.assertEqual(json.loads(stdout.splitlines()[-1])["status"], "Incomplete")
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        completed = self.binaries()
        self.assertTrue(completed)
        self.assertEqual(self.status(), "Incomplete")
        self.run_worker()
        self.assertEqual(self.status(), "Prepared")
        resumed = self.binaries()
        for key, value in completed.items():
            self.assertEqual(resumed[key], value)

    def test_interrupted_worker_preserves_entries(self):
        fixture(self.effect, pixels=16)
        process = subprocess.Popen(self.args("prepare"), env=self.env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, bufsize=1)
        try:
            while True:
                line = process.stdout.readline()
                self.assertTrue(line, "worker exited before interruption")
                event = json.loads(line)
                if event.get("phase") == "Compiling shaders" and event.get("completed", 0) >= 4:
                    break
            # Another process cannot clean a cache while this worker owns it.
            result = self.run_worker("clean", expected=2)[-1]
            self.assertIn("busy", result["message"])
            process.kill()
            process.communicate(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        binaries = self.binaries()
        # Only entries with a published checksum are committed before SIGKILL.
        completed = {}
        for key, value in binaries.items():
            checksum = Path(str(self.owned / key) + ".sha256")
            if checksum.exists() and checksum.read_text() == value[1]:
                completed[key] = value
        self.assertTrue(completed)
        self.assertEqual(self.status(), "Incomplete")
        self.run_worker()
        self.assertEqual(self.status(), "Prepared")
        resumed = self.binaries()
        for key, value in completed.items():
            self.assertEqual(resumed[key], value)

    def test_independent_failure_log_and_retry(self):
        self.run_worker()
        scope = next((self.owned / "v1").iterdir())
        sources = scope / "sources"
        sources.mkdir(exist_ok=True)
        injected = sources / "failed.json"
        injected.write_text(json.dumps({"source": "this is not valid Metal", "macros": {}}))
        self.assertEqual(self.status(), "Needs preparation")
        entries = self.binaries()
        result = self.run_worker(expected=1)[-1]
        self.assertEqual(result["status"], "Completed with errors")
        self.assertEqual(result["failed"], 1)
        self.assertIn("Renderer library", Path(result["log"]).read_text())
        self.assertIn(str(injected), Path(result["log"]).read_text())
        self.assertEqual(self.binaries(), entries)
        injected.write_text(json.dumps({"source": "#include <metal_stdlib>\nusing namespace metal;\nkernel void test_kernel() {}", "macros": {}}))
        self.run_worker()
        self.assertEqual(self.status(), "Prepared")

    def test_recorded_pipeline_reuses_library_without_source_record(self):
        self.run_worker()
        scope = next((self.owned / "v1").iterdir())
        sources = scope / "sources"
        sources.mkdir(exist_ok=True)
        source = ("#include <metal_stdlib>\nusing namespace metal;\n"
                  "vertex float4 recorded_vs(uint i [[vertex_id]]) { return float4(0, 0, 0, 1); }\n"
                  "fragment float4 recorded_fs() { return float4(1); }\n")
        key = hashlib.sha256((source + "\nmetal2.1;fastMath=1;").encode()).hexdigest()
        source_file = sources / (key + ".json")
        source_file.write_text(json.dumps({"source": source, "macros": {}}))
        self.run_worker()
        source_file.unlink()  # Older/interrupted runs can leave only the compiled library.
        descriptors = scope / "descriptors"
        descriptors.mkdir(exist_ok=True)
        record = {"vertex": key + "/recorded_vs", "fragment": key + "/recorded_fs",
                  "attachments": [[80, False, 1, 0, 0, 1, 0, 0, 15]] +
                                 [[0, False, 1, 0, 0, 1, 0, 0, 15]] * 3,
                  "depth": 0, "stencil": 0, "samples": 1}
        (descriptors / "cached-only.json").write_text(json.dumps(record))
        before = self.binaries()
        self.run_worker()
        self.assertEqual(self.status(), "Prepared")
        catalog = json.loads((scope / "prepared-descriptors.json").read_text())
        self.assertTrue(any(p["vertex"] == record["vertex"] for p in catalog.values()))
        for name, value in before.items():
            self.assertEqual(self.binaries()[name], value)
        self.assertIn("libraries/" + key + ".metallib",
                      json.loads((scope / "state.json").read_text())["entries"])

    def test_recorded_pipeline_failure_identifies_record_stage_and_recovery(self):
        self.run_worker()
        scope = next((self.owned / "v1").iterdir())
        record = next(iter(json.loads((scope / "prepared-descriptors.json").read_text()).values()))
        missing = "a" * 64
        record["fragment"] = missing + "/missing_fragment"
        descriptors = scope / "descriptors"
        descriptors.mkdir(exist_ok=True)
        path = descriptors / "missing-library.json"
        path.write_text(json.dumps(record))
        result = self.run_worker(expected=1)[-1]
        self.assertEqual(result["failed"], 1)
        log = Path(result["log"]).read_text()
        for detail in [str(path), "fragment", missing, "missing_fragment", "library",
                       "sources/" + missing + ".json", "attachments", "retry"]:
            self.assertIn(detail, log)
        path.unlink()
        self.run_worker()
        self.assertEqual(self.status(), "Prepared")

    def test_recorded_pipeline_reports_missing_function_and_malformed_fields(self):
        self.run_worker()
        scope = next((self.owned / "v1").iterdir())
        original = next(iter(json.loads((scope / "prepared-descriptors.json").read_text()).values()))
        descriptors = scope / "descriptors"
        descriptors.mkdir(exist_ok=True)
        path = descriptors / "invalid-record.json"
        cases = [("fragment", original["fragment"].split("/")[0] + "/absent_function", "does not exist"),
                 ("attachments", [], "exactly four"),
                 ("samples", {}, "samples must be numeric"),
                 ("samples", 0, "greater than zero"),
                 ("depth", "invalid", "depth must be numeric")]
        for field, value, reason in cases:
            with self.subTest(field=field, value=value):
                record = dict(original)
                record[field] = value
                path.write_text(json.dumps(record))
                result = self.run_worker(expected=1)[-1]
                self.assertEqual(result["failed"], 1)
                log = Path(result["log"]).read_text()
                self.assertIn(str(path), log)
                self.assertIn(reason, log)
                self.assertIn("Building pipelines", log)

    def test_required_files_and_unwritable_cache(self):
        self.effect.unlink()
        result = self.run_worker(expected=2)[-1]
        self.assertEqual(result["status"], "Incomplete")
        self.assertIn("No .fxc", result["message"])
        fixture(self.effect)
        self.cache = self.base / "not-a-directory"
        self.cache.write_bytes(b"a file, not a writable directory")
        result = self.run_worker(expected=2)[-1]
        self.assertEqual(result["status"], "Incomplete")
        self.assertTrue(result["message"])


if __name__ == "__main__":
    WORKER = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else ""
    unittest.main()
