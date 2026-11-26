#!/usr/bin/env python3
"""Copy the game's files into the iPad app's Documents/game (DEST: on Apple
TV, Library/Caches/game) with devicectl.

  ios_push_game.py GAME_DIR DEVICE BUNDLE_ID

Only what the port reads goes over (GTAIV.exe, its .cfg, common/, pc/,
movies/, update/, TLAD/, TBoGT/, index.bin, lang.ini, metadata.dat, commandline.txt, .rpf_key);
symbolic links are followed. devicectl stages a folder on the Mac's system
disk before sending it, so a folder is sent in one call only when it is under
CHUNK bytes; bigger folders are split and their loose files go one by one.
A folder with no files anywhere below it is left out (devicectl cannot copy
one); a zero-byte file is sent, and a folder that cannot be read is an
error, not an empty one. Before every call the Mac's free space is checked.
Finished items are recorded in a state file next to this script's output
(STATE), so a rerun continues where it stopped. Use the cable: over Wi-Fi it
crawls.
"""
import os
import shutil
import subprocess
import sys
import threading
import tempfile
from concurrent.futures import ThreadPoolExecutor

ENTRIES = [".rpf_key", "commandline.txt", "GTAIV.exe", "GTAIV.exe.cfg", "index.bin",
           "lang.ini", "metadata.dat", "common", "movies", "pc", "update", "TLAD", "TBoGT"]
CHUNK = 1 << 30          # send a folder whole below 1 GB
MIN_FREE = 15 << 30      # stop when the Mac has less than 15 GB free


def unreadable(error):
    """os.walk's onerror: a folder below that cannot be read stops the plan
    (left out, the folder above would go over without it)."""
    raise error


def size_of(path):
    """The bytes of the files below path; a folder that cannot be read raises."""
    if os.path.isfile(path):
        return os.path.getsize(path)
    total = 0
    for root, _, files in os.walk(path, onerror=unreadable, followlinks=True):
        for name in files:
            total += os.path.getsize(os.path.join(root, name))
    return total


def has_files(path):
    """Whether any file is below path; a folder that cannot be read raises."""
    return any(files for _, _, files in os.walk(path, onerror=unreadable, followlinks=True))


def plan(src, rel):
    """Yield (relative path, bytes) items to send, folders under CHUNK whole."""
    full = os.path.join(src, rel)
    if not os.path.isdir(full):
        yield rel, os.path.getsize(full)
        return
    size = size_of(full)
    # devicectl cannot copy a directory with no files. Preserve zero-byte
    # files, but leave empty directory trees out of the transfer plan.
    if size == 0 and not has_files(full):
        return
    if size < CHUNK:
        yield rel, size
        return
    for name in sorted(os.listdir(full)):
        yield from plan(src, os.path.join(rel, name))


def main():
    src, device = os.path.realpath(sys.argv[1]), sys.argv[2]
    app = sys.argv[3]
    state = os.environ.get("STATE", os.path.join(os.getcwd(), "ios_push_game.state"))
    dest = os.environ.get("DEST", "Documents/game")
    # PARALLEL=n copies at once: devicectl sends one file at a time through
    # its tunnel (about 5 MB/s to an Apple TV on Ethernet), not the link's.
    parallel = int(os.environ.get("PARALLEL", "1"))
    done = set()
    if os.path.exists(state):
        with open(state) as f:
            done = set(line.rstrip("\n") for line in f)
    try:
        items = [it for e in ENTRIES if os.path.exists(os.path.join(src, e)) for it in plan(src, e)]
    except OSError as e:
        print(f"STOP: cannot read {e.filename}: {e.strerror} (give it read access, then run this again)",
              flush=True)
        sys.exit(1)
    total = sum(size for _, size in items)
    sent = [sum(size for item, size in items if item in done)]
    lock = threading.Lock()
    failed = []

    def transfer_status(value):
        # Keep Play from starting the TV game against an incomplete copy.
        # Leave "copying" in place on errors so the same STATE can resume.
        with tempfile.TemporaryDirectory(prefix="gtaiv-transfer-") as directory:
            marker = os.path.join(directory, ".gtaiv-transfer-status")
            with open(marker, "w") as f:
                f.write(value + "\n")
            subprocess.run(["xcrun", "devicectl", "device", "copy", "to", "--device", device,
                            "--domain-type", "appDataContainer", "--domain-identifier", app,
                            "--source", marker, "--destination", f"{dest}/.gtaiv-transfer-status",
                            "--timeout", "120"], check=True, capture_output=True, text=True)

    transfer_status("copying")

    def send(job):
        item, size = job
        if failed:
            return
        with lock:
            free = shutil.disk_usage("/").free
            if free < MIN_FREE + size * parallel:
                failed.append(f"STOP: Mac free {free >> 30} GB, next item {size >> 20} MB: {item}")
                return
        cmd = ["xcrun", "devicectl", "device", "copy", "to", "--device", device,
               "--domain-type", "appDataContainer", "--domain-identifier", app,
               "--source", os.path.realpath(os.path.join(src, item)),
               "--destination", f"{dest}/{item}", "--timeout", "3600"]
        result = subprocess.run(cmd, capture_output=True, text=True)
        with lock:
            if result.returncode != 0:
                failed.append(f"FAIL {item}: {(result.stderr or result.stdout).strip()[-400:]}")
                return
            with open(state, "a") as f:
                f.write(item + "\n")
            sent[0] += size
            print(f"ok {sent[0] * 100 // total}% {sent[0] >> 20}/{total >> 20} MB  {item}", flush=True)

    with ThreadPoolExecutor(max_workers=parallel) as pool:
        list(pool.map(send, [it for it in items if it[0] not in done]))
    if failed:
        print(failed[0], flush=True)
        sys.exit(1)
    transfer_status("ready")
    print("DONE", flush=True)


if __name__ == "__main__":
    main()
