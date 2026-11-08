#!/usr/bin/env python3
"""patchres.py kernel.elf outdir -- write kernel copies whose Multiboot header asks GRUB for other screen resolutions."""
import struct, sys, os
src, out = sys.argv[1], sys.argv[2]
data = bytearray(open(src, "rb").read())
off = next(i for i in range(0, 8192, 4) if struct.unpack_from("<I", data, i)[0] == 0x1BADB002)   # header lies in the first 8 KB
assert struct.unpack_from("<I", data, off + 4)[0] & 4, "kernel has no video request"
for name, (w, h) in {"1080p": (1920, 1080), "900p": (1600, 900), "768": (1024, 768)}.items():
    d = bytearray(data); struct.pack_into("<II", d, off + 36, w, h)
    open(os.path.join(out, f"kernel-{name}.elf"), "wb").write(d)
print("variants written")
