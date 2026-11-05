"""Run native quit requests against the real launcher in an isolated app bundle."""
import os
import pathlib
import plistlib
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
        process = subprocess.Popen([str(binary), action], start_new_session=True)
        try:
            if process.wait(timeout=9):
                raise RuntimeError(f"Launcher {action} quit failed")
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        print(f"Launcher {action} quit passed")


if __name__ == "__main__":
    main()
