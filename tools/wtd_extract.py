#!/usr/bin/env python3
# List and extract the textures of a GTA IV .wtd (RSC5, type 8) as PNG.
import sys, struct, zlib, io, os
from PIL import Image
path, out = sys.argv[1], sys.argv[2]
raw = open(path, 'rb').read()
assert raw[:4] == b'RSC\x05', raw[:4]
flags = struct.unpack_from('<I', raw, 8)[0]
sys_size = (flags & 0x7FF) << (((flags >> 11) & 0xF) + 8)
gfx_size = ((flags >> 15) & 0x7FF) << (((flags >> 26) & 0xF) + 8)
data = zlib.decompress(raw[12:])
sysd, gfxd = data[:sys_size], data[sys_size:]
def P(v):
    if v >> 28 == 5: return ('s', v & 0x0FFFFFFF)
    if v >> 28 == 6: return ('g', v & 0x0FFFFFFF)
    return None
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
tex_arr, tex_n = u32(sysd, 0x18), u16(sysd, 0x1C)
seg, arr = P(tex_arr)
os.makedirs(out, exist_ok=True)
for i in range(tex_n):
    seg, t = P(u32(sysd, arr + 4 * i))
    name_p = P(u32(sysd, t + 0x14))
    name = sysd[name_p[1]:sysd.index(b'\0', name_p[1])].decode('latin1')
    w, h = u16(sysd, t + 0x1C), u16(sysd, t + 0x1E)
    fmt = sysd[t + 0x20:t + 0x24]
    stride, mips = u16(sysd, t + 0x24), sysd[t + 0x27]
    dp = None
    for off in (0x48,):
        p = P(u32(sysd, t + off))
        if p and p[0] == 'g': dp = p[1]; break
    print(i, name, w, h, fmt, stride, mips, dp)
    if dp is None: continue
    if fmt in (b'DXT1', b'DXT3', b'DXT5'):
        bs = 8 if fmt == b'DXT1' else 16
        size = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * bs
        hdr = struct.pack('<4sI', b'DDS ', 124) + struct.pack('<IIIIII', 0x1007 | 0x80000, h, w, size, 0, 1) + b'\0' * 44
        hdr += struct.pack('<II4sIIIII', 32, 4, fmt, 0, 0, 0, 0, 0) + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
        img = Image.open(io.BytesIO(hdr + gfxd[dp:dp + size]))
    else:
        size = stride * h
        img = Image.frombytes('RGBA', (w, h), gfxd[dp:dp + size], 'raw', 'BGRA')
    base = os.path.basename(name).replace(':', '_').replace('/', '_')
    img.save(os.path.join(out, f'{i:03d}_{base}.png'))
