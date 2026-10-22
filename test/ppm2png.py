#!/usr/bin/env python3
"""Convert every PPM in a directory to PNG (stdlib only)."""
import struct, sys, zlib, glob, os


def ppm_to_png(src, dst):
    with open(src, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        f.readline()
        data = f.read()
    raw = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(dst, "wb") as f:
        f.write(png)


for p in sorted(glob.glob(os.path.join(sys.argv[1] if len(sys.argv) > 1 else "out", "*.ppm"))):
    ppm_to_png(p, p[:-4] + ".png")
    os.remove(p)
    print("wrote", p[:-4] + ".png")
