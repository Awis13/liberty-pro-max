"""Run native quit requests against the real launcher in an isolated app bundle."""
import os
import pathlib
import plistlib
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import uuid


def main():
    executable, action = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="gtaiv-quit-") as directory:
        contents = pathlib.Path(directory) / "Quit Test.app" / "Contents"
        binary = contents / "MacOS" / "launcher_quit_test"
        binary.parent.mkdir(parents=True)
        shutil.copy2(executable, binary)
        with (contents / "Info.plist").open("wb") as file:
            plistlib.dump({
                "CFBundleExecutable": binary.name,
                "CFBundleIdentifier": "io.github.awis13.launcher-quit-test." + uuid.uuid4().hex,
                "CFBundleName": "Launcher Quit Test",
                "CFBundlePackageType": "APPL",
            }, file)
        process = subprocess.Popen(
            [str(binary), action], start_new_session=True,
            stdout=subprocess.PIPE if action == "dock" else None,
        )
        try:
            if action == "dock":
                ready, _, _ = select.select([process.stdout], [], [], 5)
                if not ready or process.stdout.readline() != b"ready\n":
                    raise RuntimeError("Launcher did not become ready for system quit")
                subprocess.run(
                    [str(binary), "dock-request", str(process.pid)],
                    check=True, timeout=6,
                )
            if process.wait(timeout=9):
                raise RuntimeError(f"Launcher {action} quit failed")
        finally:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait()
        print(f"Launcher {action} quit passed")


if __name__ == "__main__":
    main()
