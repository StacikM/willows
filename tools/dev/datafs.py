#!/usr/bin/env python3
"""Read (and optionally extract) the Willows saved-data area of a disk image. Read-only on the image.

  tools/dev/datafs.py IMAGE                 list saved files (size, path)
  tools/dev/datafs.py IMAGE extract DIR     copy every saved file into DIR (to inspect the .exe/.dll files on the host)
  tools/dev/datafs.py IMAGE settings        print System/settings.cfg
  tools/dev/datafs.py IMAGE pack DIR        replace the data area of IMAGE with the files under DIR (DIR/Downloads/x -> C:\\Downloads\\x).
                                            Refuses willows.img: use it on build/dev/test.img (see tools/dev/mktest.sh).
"""
import os, struct, sys

DATA_LBA = 122880

def records(path):
    with open(path, "rb") as f:
        f.seek(DATA_LBA * 512); hdr = f.read(512)
        magic, ver, n, total = struct.unpack_from("<IIII", hdr)
        if magic != 0x53464C57: return
        for _ in range(n):
            h = f.read(512)
            if len(h) < 512: return
            name = h[:128].split(b"\0")[0].decode("latin-1"); flags = h[128]; size = struct.unpack_from("<Q", h, 136)[0]
            if flags & 3: yield name, flags, 0, None; continue
            data = f.read((size + 511) // 512 * 512)[:size]
            yield name, flags, size, data

def pack(img, top):
    if os.path.basename(os.path.abspath(img)) == "willows.img": sys.exit("refusing to write willows.img (the user's own image)")
    recs = []
    for d, dirs, files in os.walk(top):
        dirs.sort()
        rel = os.path.relpath(d, top).replace(os.sep, "/")
        if rel != ".": recs.append((rel, 1, b""))
        for f in sorted(files): recs.append(((f if rel == "." else rel + "/" + f), 0, open(os.path.join(d, f), "rb").read()))
    with open(img, "r+b") as f:
        f.seek((DATA_LBA + 1) * 512); total = 1
        for name, flags, data in recs:
            assert len(name.encode()) < 128, name
            h = bytearray(512); h[:len(name.encode())] = name.encode(); h[128] = flags; struct.pack_into("<Q", h, 136, len(data))
            f.write(h); f.write(data.ljust((len(data) + 511) // 512 * 512, b"\0")); total += 1 + (len(data) + 511) // 512
        assert total <= 2097152, "does not fit in the 1 GB data area"
        f.seek(DATA_LBA * 512); f.write(struct.pack("<IIII", 0x53464C57, 2, len(recs), total).ljust(512, b"\0"))
    print(f"packed {len(recs)} entries, {total * 512 / 1e6:.1f} MB into {img}")

if __name__ == "__main__":
    if len(sys.argv) < 2: print(__doc__); sys.exit(0)
    img, what = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else "list")
    if what == "pack": pack(img, sys.argv[3]); sys.exit(0)
    for name, flags, size, data in records(img):
        if what == "list": print(f"{'dir' if flags & 1 else 'del' if flags & 2 else size:>12}  {name}")
        elif what == "settings" and name.lower() == "system/settings.cfg": print(data.decode("latin-1"))
        elif what == "extract" and data is not None:
            out = os.path.join(sys.argv[3], name); os.makedirs(os.path.dirname(out), exist_ok=True); open(out, "wb").write(data)
