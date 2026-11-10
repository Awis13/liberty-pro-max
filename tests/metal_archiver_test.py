"""Real archiver growth, process quiescence, and archive reuse in a unique bundle."""
import hashlib
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from performance_compare import conflicts

EXECUTABLE = Path(sys.argv.pop(1)).resolve()
PROBE = Path(sys.argv.pop(1)).resolve()


class ArchiverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="liberty-archiver-")
        self.base = Path(self.tmp.name)
        self.identity = "io.github.awis13.liberty.archiver-test." + uuid.uuid4().hex
        contents = self.base / "Archiver Test.app/Contents"
        (contents / "MacOS").mkdir(parents=True)
        self.binary = contents / "MacOS/gtaiv"
        shutil.copy2(EXECUTABLE, self.binary)
        (contents / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleIdentifier": self.identity, "CFBundleExecutable": "gtaiv",
            "CFBundlePackageType": "APPL"}))
        self.appcache = Path(subprocess.check_output(["getconf", "DARWIN_USER_CACHE_DIR"], text=True).strip()) / self.identity
        self.archiver = self.appcache / "com.apple.gpuarchiver"
        self.archive = self.base / "pipeline.metallib"
        self.processes = []

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=10)
            for stream in (process.stdin, process.stdout, process.stderr):
                if stream and not stream.closed:
                    stream.close()
        self.wait_reapers()
        self.wait_empty()
        if self.appcache.exists():
            shutil.rmtree(self.appcache)
        self.tmp.cleanup()

    def run_app(self, *args, success=True):
        result = subprocess.run([str(self.binary), *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def hold(self, unmanaged=False, env=None):
        process = subprocess.Popen([str(self.binary), *(["unmanaged"] if unmanaged else []), "hold"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        self.processes.append(process)
        self.assertEqual(process.stdout.readline(), b"ready\n")
        return process

    def wait_reapers(self):
        # Empty workspaces alone do not mean a newly spawned helper has exited;
        # wait for every helper before removing its unique diagnostic namespace.
        pattern = "^" + re.escape(str(self.binary)) + r" --metal-archiver-reap [0-9]+$"
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            result = subprocess.run(["pgrep", "-f", pattern], capture_output=True)
            if result.returncode == 1:
                return
            self.assertEqual(result.returncode, 0, result.stderr)
            time.sleep(.05)
        self.fail("Archiver helper did not exit")

    def workspaces(self):
        return list(self.archiver.glob("gpuarchiver-*"))

    def wait_empty(self):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if not self.archiver.is_symlink() and not list(self.archiver.glob("*")):
                return
            time.sleep(.05)
        self.fail("Archiver workspaces were not reclaimed after process exit")

    def marker(self):
        self.archiver.mkdir(parents=True, exist_ok=True)
        marker = self.archiver / "gpuarchiver-fixture/PersistentState"
        marker.parent.mkdir()
        marker.write_bytes(b"temporary workspace")
        return marker

    def test_real_growth_and_reuse_after_cleanup(self):
        self.run_app("unmanaged", "build", self.archive)
        count = len(self.workspaces())
        allocated = sum(p.stat().st_blocks * 512 for p in self.archiver.rglob("*") if p.is_file())
        print(f"baseline: workspaces={count}, allocated_bytes={allocated}", flush=True)
        self.assertGreaterEqual(count, 3, "Probe did not reproduce GPU archiver growth")
        before = (self.archive.stat().st_mtime_ns, hashlib.sha256(self.archive.read_bytes()).hexdigest())
        self.run_app("reuse", self.archive)
        self.wait_empty()
        self.assertEqual((self.archive.stat().st_mtime_ns, hashlib.sha256(self.archive.read_bytes()).hexdigest()), before)
        for _ in range(2):
            self.run_app("build", self.archive)
            self.wait_empty()
            self.run_app("reuse", self.archive)
            self.wait_empty()
        print("managed: workspaces=0 after each exit; saved archive computes 42 after cleanup", flush=True)

    def test_injected_probe_records_only_parent(self):
        output = self.base / "probe-pids.txt"
        process = self.hold(env=dict(os.environ, DYLD_INSERT_LIBRARIES=str(PROBE),
                                     LIBERTY_MEASURE_TEST=str(output)))
        listing = subprocess.check_output(["ps", "-axo", "pid=,ppid=,args="], text=True)
        helper = [line.split(None, 2)[0] for line in listing.splitlines()
                  if len(line.split(None, 2)) == 3 and line.split(None, 2)[1:] ==
                  [str(process.pid), f"{self.binary} --metal-archiver-reap {process.pid}"]]
        self.assertEqual(len(helper), 1, "Expected the managed game's cleanup helper")
        self.assertFalse([line for line in conflicts(process.pid, self.binary)
                          if line.split()[0] in helper])
        process.stdin.close()
        self.assertEqual(process.wait(timeout=10), 0)
        self.wait_reapers()
        self.assertEqual(output.read_text().splitlines(), [str(process.pid)])

    def test_live_sessions_defer_cleanup_and_killed_session_is_reaped(self):
        first = self.hold()
        marker = self.marker()
        second = self.hold()
        self.assertTrue(marker.exists())
        second.stdin.close(); self.assertEqual(second.wait(timeout=10), 0)
        time.sleep(.2)
        self.assertTrue(marker.exists(), "Worker exit cleaned a live game's workspaces")
        first.kill(); first.wait(timeout=10)
        self.wait_empty()

    def test_older_build_process_defers_cleanup(self):
        legacy = self.hold(unmanaged=True)
        marker = self.marker()
        self.run_app("reuse", self.make_archive())
        time.sleep(.2)
        self.assertTrue(marker.exists())
        legacy.stdin.close(); legacy.wait(timeout=10)
        self.run_app("reuse", self.archive)
        self.wait_empty()

    def make_archive(self):
        self.run_app("unmanaged", "build", self.archive)
        return self.archive

    def test_symlinks_preserve_external_files_and_owned_archives(self):
        outside = self.base / "outside"
        outside.mkdir()
        sentinel = outside / "keep"
        sentinel.write_text("keep")
        self.appcache.mkdir()
        self.archiver.symlink_to(outside, target_is_directory=True)
        self.run_app("hold")
        self.assertEqual(sentinel.read_text(), "keep")
        self.assertTrue(self.archiver.is_symlink())
        self.archiver.unlink()
        marker = self.marker()
        (marker.parent / "link").symlink_to(outside, target_is_directory=True)
        owned = self.appcache / "metal/keep.metallib"
        owned.parent.mkdir(); owned.write_text("owned archive")
        self.run_app("hold")
        self.wait_empty()
        self.assertEqual(sentinel.read_text(), "keep")
        self.assertEqual(owned.read_text(), "owned archive")

    def test_filesystem_failure_is_retried_on_next_idle_session(self):
        marker = self.marker()
        marker.parent.chmod(0o500)
        try:
            result = self.run_app("hold")
            self.assertIn("cleanup incomplete", result.stderr)
            self.assertTrue(marker.exists())
        finally:
            marker.parent.chmod(0o700)
        self.run_app("hold")
        self.wait_empty()

    def test_unsafe_lock_and_namespace_fail_closed(self):
        self.appcache.mkdir()
        sentinel = self.base / "keep"
        sentinel.write_text("keep")
        lock = self.appcache / "liberty-metal-archiver.lock"
        lock.symlink_to(sentinel)
        self.run_app("hold", success=False)
        self.assertEqual(sentinel.read_text(), "keep")
        lock.unlink(); self.appcache.rmdir()
        self.appcache.symlink_to(self.base, target_is_directory=True)
        self.run_app("hold", success=False)
        self.appcache.unlink()


if __name__ == "__main__":
    unittest.main()
