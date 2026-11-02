#!/usr/bin/env python3
"""Extract the compiled D3D9 shaders from GTA IV's .fxc effect files.

    fxc_shaders.py OUT_DIR FILE.fxc|DIR [...]

Each effect becomes OUT_DIR/<effect>/ with vs<N>.vso and ps<N>.pso, the
shader functions as CreateVertexShader / CreatePixelShader take them, so
the runtime's shader checker (recomp_shader_check, one directory per
effect: its vertex and pixel shaders are paired) can translate them all.
A directory argument takes every .fxc in it.

The .fxc layout ("rgxa"): a byte count of vertex fragments, then per
fragment a byte count of variables (each: type byte, index byte, a
16-bit register, a length-prefixed name) and two 16-bit sizes followed
by the shader; then the pixel fragments the same way. A fragment of
size 0 is an empty slot (no shader). The parameters and techniques that
follow are not needed here.
"""
import os
import struct
import sys


def fragments(data):
    if data[:4] != b"rgxa":
        raise ValueError("not an rgxa effect")
    p = 4
    out = []
    for stage in ("vs", "ps"):
        n = data[p]
        p += 1
        for k in range(n):
            variables = data[p]
            p += 1
            for _ in range(variables):
                p += 4
                p += 1 + data[p]
            size, _ = struct.unpack_from("<HH", data, p)
            p += 4
            if p + size > len(data):
                raise ValueError("fragment past the end of the file")
            out.append((stage, k, data[p:p + size]))
            p += size
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out_dir = sys.argv[1]
    files = []
    for a in sys.argv[2:]:
        if os.path.isdir(a):
            files += sorted(os.path.join(a, f) for f in os.listdir(a) if f.lower().endswith(".fxc"))
        else:
            files.append(a)
    total = 0
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            frags = fragments(open(path, "rb").read())
        except ValueError as e:
            print(f"{path}: {e}", file=sys.stderr)
            continue
        d = os.path.join(out_dir, name)
        os.makedirs(d, exist_ok=True)
        for stage, k, blob in frags:
            if not blob:
                continue
            with open(os.path.join(d, f"{stage}{k}.{stage[0]}so"), "wb") as f:
                f.write(blob)
            total += 1
    print(f"{total} shaders from {len(files)} effects in {out_dir}")


if __name__ == "__main__":
    main()
