#!/usr/bin/env python3
"""List the files in GTA IV's RPF2/RPF3 archives (their table of contents).

    rpf_list.py GAME_DIR ARCHIVE.rpf [...]

RPF3 (the audio archives) keeps a hash of each name instead of the name.
The table of contents is AES-256 (ECB) encrypted, 16 times over, with a
key the game's executable holds: found by its SHA-1, kept in
GAME_DIR/.rpf_key (not printed). Decryption by the system's openssl.
"""
import hashlib
import os
import struct
import subprocess
import sys

KEY_SHA1 = "dea375ef1e6ef2223a1221c2c575c47bf17efa5e"


def find_key(game_dir):
    cache = os.path.join(game_dir, ".rpf_key")
    if os.path.exists(cache):
        return open(cache, "rb").read()
    data = open(os.path.join(game_dir, "GTAIV.exe"), "rb").read()
    for i in range(0, len(data) - 32, 4):
        if hashlib.sha1(data[i:i + 32]).hexdigest() == KEY_SHA1:
            key = data[i:i + 32]
            with open(cache, "wb") as f:
                f.write(key)
            return key
    sys.exit("rpf_list: no key in GTAIV.exe")


def decrypt(key, blob):
    for _ in range(16):
        blob = subprocess.run(["openssl", "enc", "-d", "-aes-256-ecb", "-nopad", "-K", key.hex()],
                              input=blob, capture_output=True, check=True).stdout
    return blob


def entries(path, key):
    with open(path, "rb") as f:
        head = f.read(20)
        magic, toc_size, count, _, encrypted = struct.unpack("<IIIII", head)
        if head[:4] not in (b"RPF2", b"RPF3"):
            raise ValueError("not RPF2 or RPF3")
        hashed = head[:4] == b"RPF3"
        f.seek(2048)
        toc = f.read((toc_size + 15) & ~15)
    if encrypted:
        toc = decrypt(key, toc)
    names = toc[count * 16:]
    out = []
    for i in range(count):
        name_off, a, b, c = struct.unpack_from("<IIII", toc, i * 16)
        if hashed:
            name = f"#{name_off:08X}"
        else:
            end = names.find(b"\0", name_off)
            name = names[name_off:end].decode("latin-1")
        is_dir = bool(b & 0x80000000)
        out.append((name, is_dir, a if not is_dir else None))
    return out


def main():
    game_dir = sys.argv[1]
    key = find_key(game_dir)
    for path in [a for a in sys.argv[2:] if a != "-v"]:
        files = [(n, s) for n, d, s in entries(path, key) if not d]
        big = [s for n, s in files if (s or 0) > 1 << 20]
        print(f"{os.path.basename(path)}: {len(files)} files, {len(big)} over 1 MB, "
              f"{sum(s or 0 for n, s in files) >> 20} MB")
        if "-v" in sys.argv:
            for n, s in files:
                print(f"   {n}  ({(s or 0) // 1024} KB)")


if __name__ == "__main__":
    main()
