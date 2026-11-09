#!/usr/bin/env python3
"""Make tvOS layered icons from the supplied full GTA IV logo.

  make_tvos_icon.py ICON_DIR [ARTWORK]   (needs Pillow)

Writes ICON_DIR/tvos/Assets.xcassets and ICON_DIR/icon-tvos.png (preview).
ARTWORK defaults to port/ios/tvos/AppIcon.png. Its original transparency is
preserved, and a solid black back layer fills the TV tile.
"""
import json
from pathlib import Path
import sys

from PIL import Image


INFO = {"author": "xcode", "version": 1}


def metadata(directory, **contents):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "Contents.json").write_text(
        json.dumps({**contents, "info": INFO}, indent=2) + "\n"
    )


def layers(logo, size, background):
    """Keep the mark inside the focus safe area; let tvOS supply the mask."""
    width, height = size
    mark = logo.copy()
    mark.thumbnail((round(width * 0.76), round(height * 0.76)), Image.Resampling.LANCZOS)
    front = Image.new("RGBA", size)
    front.alpha_composite(mark, ((width - mark.width) // 2, (height - mark.height) // 2))
    return front, Image.new("RGB", size, background)


def generate(icon_dir, artwork=None):
    icon_dir = Path(icon_dir)
    artwork = artwork or Path(__file__).resolve().parents[1] / "port/ios/tvos/AppIcon.png"
    with Image.open(artwork) as source:
        logo = source.convert("RGBA")
    background = "black"

    catalog = icon_dir / "tvos/Assets.xcassets"
    brand = catalog / "AppIcon.brandassets"
    metadata(catalog)
    assets = []
    for name, size, scales in (
        ("Small", (400, 240), (1, 2)),
        ("Large", (1280, 768), (1,)),
    ):
        stack = brand / f"{name}.imagestack"
        metadata(stack, layers=[{"filename": f"{layer}.imagestacklayer"}
                                for layer in ("Front", "Back")])
        images = {"Front": [], "Back": []}
        for scale in scales:
            front, back = layers(logo, tuple(value * scale for value in size), background)
            for layer, image in (("Front", front), ("Back", back)):
                image_set = stack / f"{layer}.imagestacklayer/Content.imageset"
                image_set.mkdir(parents=True, exist_ok=True)
                filename = f"{layer}@{scale}x.png"
                image.save(image_set / filename)
                images[layer].append({"filename": filename, "idiom": "tv", "scale": f"{scale}x"})
        for layer, entries in images.items():
            metadata(stack / f"{layer}.imagestacklayer")
            metadata(stack / f"{layer}.imagestacklayer/Content.imageset", images=entries)
        assets.append({"filename": stack.name, "idiom": "tv", "role": "primary-app-icon",
                       "size": f"{size[0]}x{size[1]}"})

    for name, size, role in (
        ("TopShelf", (1920, 720), "top-shelf-image"),
        ("TopShelfWide", (2320, 720), "top-shelf-image-wide"),
    ):
        image_set = brand / f"{name}.imageset"
        image_set.mkdir(parents=True, exist_ok=True)
        images = []
        for scale in (1, 2):
            front, back = layers(logo, tuple(value * scale for value in size), background)
            back.paste(front, (0, 0), front)
            filename = f"{name}@{scale}x.png"
            back.save(image_set / filename)
            images.append({"filename": filename, "idiom": "tv", "scale": f"{scale}x"})
        metadata(image_set, images=images)
        assets.append({"filename": image_set.name, "idiom": "tv", "role": role,
                       "size": f"{size[0]}x{size[1]}"})
    metadata(brand, assets=assets)

    front, preview = layers(logo, (1280, 768), background)
    preview.paste(front, (0, 0), front)
    preview.save(icon_dir / "icon-tvos.png")
    return catalog


if __name__ == "__main__":
    print(generate(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None))
