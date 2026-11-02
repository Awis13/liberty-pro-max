#!/usr/bin/env python3
"""The iPad app's icon from the game's own logo: the IV of loadingscreens.wtd's
gta.dds (tools/wtd_extract.py), its bevelled letters on black, 1024x1024.
  make_ios_icon.py GAME_DIR OUT_DIR   (needs Pillow; writes OUT_DIR/icon1024.png)"""
import os, subprocess, sys
from collections import deque
from PIL import Image, ImageFilter

game, out = sys.argv[1], sys.argv[2]
tex = os.path.join(out, 'loadingscreens')
subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'wtd_extract.py'),
                os.path.join(game, 'pc/textures/loadingscreens.wtd'), tex], check=True, capture_output=True)
src = Image.open(next(os.path.join(tex, f) for f in os.listdir(tex) if f.endswith('_gta.dds.png'))).convert('RGBA')
iv = src.crop((280, 296, 500, 452))  # the IV and what surrounds it
bg = Image.new('RGBA', iv.size, (0, 0, 0, 255)); bg.alpha_composite(iv); rgb = bg.convert('RGB')
bright = rgb.convert('L').point(lambda v: 255 if v > 60 else 0)
opened = bright.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))  # no thin outer stroke
W, H = opened.size; px = opened.load()
seen = [[False] * W for _ in range(H)]; keep = Image.new('L', (W, H), 0); kp = keep.load()
row = H // 2 + 5
for x0 in range(W):  # the letters: components crossing the middle row at full height
    if px[x0, row] and not seen[row][x0]:
        comp = []; q = deque([(x0, row)]); seen[row][x0] = True
        while q:
            x, y = q.popleft(); comp.append((x, y))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H and px[nx, ny] and not seen[ny][nx]:
                    seen[ny][nx] = True; q.append((nx, ny))
        if max(c[1] for c in comp) - min(c[1] for c in comp) > H * 0.5:
            for c in comp: kp[c] = 255
keep = keep.filter(ImageFilter.MaxFilter(3))
glyph = Image.composite(rgb, Image.new('RGB', rgb.size), keep).crop(keep.getbbox())
g = glyph.load()
bright_col = lambda x: max(g[x, y][0] for y in range(glyph.height)) > 40
x0 = next(x for x in range(glyph.width) if bright_col(x))
x1 = next((x for x in range(x0, glyph.width) if not bright_col(x)), x0)
if x1 - x0 < glyph.width // 8:  # a sliver of "auto"'s o before the I
    glyph = glyph.crop((x1, 0, glyph.width, glyph.height))
glyph = glyph.crop(glyph.convert('L').point(lambda v: 255 if v > 20 else 0).getbbox())
S = 1024; icon = Image.new('RGB', (S, S))
w = int(S * 0.68); h = int(glyph.height * w / glyph.width)
icon.paste(glyph.resize((w, h), Image.LANCZOS), ((S - w) // 2, (S - h) // 2))
icon.save(os.path.join(out, 'icon1024.png'))
