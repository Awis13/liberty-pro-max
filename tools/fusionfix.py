#!/usr/bin/env python3
"""Prepare the required FusionFix data and the native frontend overlays."""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from zipfile import ZipFile

VERSION = "5.1.2"
ARCHIVE = "GTAIV.EFLC.FusionFix.zip"
URL = f"https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/download/v{VERSION}/{ARCHIVE}"
SHA256 = "138f4067a2d2f09df2df80c4378407ad7c742be0e3b99f090633d50820bb2450"
MENU = Path("common/data/frontend_menus.xml")
FONT = MENU.with_name("frontend_pc.dat")


def verify(archive):
    digest = hashlib.sha256()
    with archive.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != SHA256:
        raise ValueError(f"Expected the official FusionFix {VERSION} ZIP. SHA-256 mismatch for {archive}.")


def acquire(cache):
    if local := os.getenv("GTAIV_FUSIONFIX_ZIP"):
        archive = Path(local)
        verify(archive)
        return archive
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / f"FusionFix-{VERSION}.zip"
    if archive.exists():
        verify(archive)
        return archive
    with tempfile.TemporaryDirectory(dir=cache) as tmp:
        download = Path(tmp) / ARCHIVE
        subprocess.run(
            ["/usr/bin/curl", "-fL", "--retry", "2", "--output", download, URL], check=True
        )
        verify(download)
        download.replace(archive)
    return archive


def find_path(root, relative):
    for part in relative.parts:
        if not root.is_dir():
            return None
        root = next((p for p in root.iterdir() if p.name.lower() == part.lower()), None)
        if root is None:
            return None
    return root if root.is_file() else None


def merge_menu(original, fusionfix, supported):
    stock = ET.parse(original)
    menus = {row.get("enum"): row for row in stock.findall("sMenuScreen/*")}
    for page in ET.fromstring(fusionfix).findall("sMenuScreen/*"):
        target = menus.get(page.get("enum"))
        if target is None:
            continue
        for row in page:
            if row.get("value") not in supported:
                continue
            position = next(
                (i for i, item in enumerate(target) if item.get("label") == "MO_EXITGAM"),
                len(target),
            )
            target.insert(position, row)
    return stock


def prepare(game, archive, options, output):
    supported = set(re.findall(r"^FUSION_OPTION\((PREF_[A-Z0-9_]+),", options.read_text(), re.M))
    if not supported:
        raise ValueError("FusionFix's native menu preferences are missing.")
    if not find_path(game, MENU):
        raise ValueError(f"The GTA IV frontend is missing from {game}.")
    output.mkdir(parents=True, exist_ok=True)
    with ZipFile(archive) as release:
        for info in release.infolist():
            path = PurePosixPath(info.filename)
            if not path.parts or path.parts[0] != "update" or info.is_dir():
                continue
            if path.is_absolute() or ".." in path.parts or "\\" in info.filename:
                raise ValueError(f"Invalid FusionFix archive path {info.filename!r}.")
            release.extract(info, output)

        for episode in (Path(), Path("TLAD"), Path("TBoGT")):
            source = find_path(game, episode / MENU)
            if source is None:
                continue
            layout = output / "frontend" / episode / MENU
            layout.parent.mkdir(parents=True, exist_ok=True)
            merged = merge_menu(source, release.read((Path("update") / episode / MENU).as_posix()), supported)
            merged.write(layout, encoding="utf-8")
            try:
                font = release.read((Path("update") / episode / FONT).as_posix())
            except KeyError:
                continue
            layout.with_name(FONT.name).write_bytes(font)
    (output / "version").write_text(VERSION + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--options", type=Path, default=Path(__file__).resolve().parents[1] / "src/fusionfix_options.inc")
    args = parser.parse_args()
    try:
        archive = acquire(args.cache)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=args.output.parent) as tmp:
            prepared = Path(tmp) / "fusionfix"
            prepare(args.game, archive, args.options, prepared)
            if args.output.exists():
                shutil.rmtree(args.output)
            prepared.rename(args.output)
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"FusionFix setup failed. {error}\n")


if __name__ == "__main__":
    main()
