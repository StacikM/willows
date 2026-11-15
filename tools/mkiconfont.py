#!/usr/bin/env python3
"""Build files/Windows/Fonts/WillowsIcons.ttf: a tiny TrueType font with the window caption icons programs take from Windows'
icon fonts (Firefox draws its own minimize / maximize / restore / close buttons with them). Willows' DirectWrite serves it under
the names "Segoe Fluent Icons" and "Segoe MDL2 Assets". The outlines are simple line art drawn here (no Microsoft font data).

  tools/mkiconfont.py [output.ttf]
Code points: U+E921 minimize, U+E922 maximize, U+E923 restore, U+E8BB close. Units: 1000 per em, icons inside 100..900."""
import os, struct, sys

def rect(x0, y0, x1, y1, hole=False):         # TrueType: outer contours clockwise (y up), holes counter-clockwise
    pts = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
    return pts[::-1] if hole else pts
def outline(x0, y0, x1, y1, t):
    return [rect(x0, y0, x1, y1), rect(x0 + t, y0 + t, x1 - t, y1 - t, hole=True)]
T = 80                                        # stroke width: 0.8 px at 10 px
d = 57                                        # half the diagonal stroke, along each axis (T / sqrt(2))
GLYPHS = {                                    # code point -> contours
    0xE921: [rect(100, 460, 900, 540)],                                                     # minimize
    0xE922: outline(100, 100, 900, 900, T),                                                 # maximize
    0xE923: outline(100, 100, 700, 700, T) + [rect(300, 820, 900, 900), rect(820, 300, 900, 820)],   # restore
    0xE8BB: [[(100, 100 + d), (900 - d, 900), (900, 900 - d), (100 + d, 100)],              # close: two crossing bars
             [(100, 900 - d), (100 + d, 900), (900, 100 + d), (900 - d, 100)]],
}
ORDER = sorted(GLYPHS)                        # glyph ids 1.. in code point order; glyph 0 is .notdef (empty)

def glyph_data(contours):
    xs = [x for c in contours for x, _ in c]; ys = [y for c in contours for _, y in c]
    out = struct.pack(">hhhhh", len(contours), min(xs), min(ys), max(xs), max(ys))
    end = -1
    for c in contours: end += len(c); out += struct.pack(">H", end)
    out += struct.pack(">H", 0)                                     # no instructions
    pts = [p for c in contours for p in c]
    out += bytes([1] * len(pts))                                    # every point on the curve, 16-bit deltas
    px = py = 0
    for x, _ in pts: out += struct.pack(">h", x - px); px = x
    for _, y in pts: out += struct.pack(">h", y - py); py = y
    return out + b"\0" * (-len(out) % 4)

def name_table(family):
    recs = [(1, family), (2, "Regular"), (3, family + " 1.0"), (4, family), (5, "Version 1.0"), (6, family.replace(" ", ""))]
    strings = b""; entries = b""
    for nid, s in recs:
        b = s.encode("utf-16-be"); entries += struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(b), len(strings)); strings += b
    return struct.pack(">HHH", 0, len(recs), 6 + 12 * len(recs)) + entries + strings

def cmap_table():
    segs = []                                                       # (start, end, glyph of start): runs of consecutive code points
    for i, cp in enumerate(ORDER):
        if segs and cp == segs[-1][1] + 1 and i + 1 == segs[-1][2] + (cp - segs[-1][0]): segs[-1] = (segs[-1][0], cp, segs[-1][2])
        else: segs.append((cp, cp, i + 1))
    segs.append((0xFFFF, 0xFFFF, 0))
    n = len(segs); sr = 2 * (1 << (n.bit_length() - 1)); es = (n.bit_length() - 1)
    sub = struct.pack(">HHHH", n * 2, sr, es, n * 2 - sr)
    sub += b"".join(struct.pack(">H", e) for _, e, _ in segs) + struct.pack(">H", 0)
    sub += b"".join(struct.pack(">H", s) for s, _, _ in segs)
    sub += b"".join(struct.pack(">H", ((g - s) if g else 1) & 0xFFFF) for s, _, g in segs)     # idDelta (modulo 65536)
    sub += b"".join(struct.pack(">H", 0) for _ in segs)
    sub = struct.pack(">HHH", 4, 6 + len(sub), 0) + sub
    return struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 1, 12) + sub

def checksum(b):
    b = b + b"\0" * (-len(b) % 4); return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF

def build(path, family="Willows Caption Icons"):
    glyphs = [b""] + [glyph_data(GLYPHS[cp]) for cp in ORDER]
    loca = [0]
    for g in glyphs: loca.append(loca[-1] + len(g))
    allpts = [p for cp in ORDER for c in GLYPHS[cp] for p in c]
    xmin = min(x for x, _ in allpts); ymin = min(y for _, y in allpts); xmax = max(x for x, _ in allpts); ymax = max(y for _, y in allpts)
    ng = len(glyphs); maxpts = max(sum(len(c) for c in GLYPHS[cp]) for cp in ORDER); maxcont = max(len(GLYPHS[cp]) for cp in ORDER)
    t = {}
    t[b"head"] = struct.pack(">IIIIHHqqhhhhHHhhh", 0x10000, 0x10000, 0, 0x5F0F3CF5, 0x000B, 1000, 0, 0, xmin, ymin, xmax, ymax, 0, 8, 2, 1, 0)
    t[b"hhea"] = struct.pack(">IhhhHhhhhhhhhhhhH", 0x10000, 1000, 0, 0, 1000, 0, 0, xmax, 1, 0, 0, 0, 0, 0, 0, 0, ng)
    t[b"hmtx"] = struct.pack(">Hh", 1000, 0) + b"".join(struct.pack(">Hh", 1000, min(x for c in GLYPHS[cp] for x, _ in c)) for cp in ORDER)
    t[b"maxp"] = struct.pack(">IHHHHHHHHHHHHHH", 0x10000, ng, maxpts, maxcont, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    t[b"glyf"] = b"".join(glyphs)
    t[b"loca"] = b"".join(struct.pack(">I", o) for o in loca)
    t[b"cmap"] = cmap_table()
    t[b"name"] = name_table(family)
    t[b"post"] = struct.pack(">IIhhIIIII", 0x30000, 0, -100, 50, 0, 0, 0, 0, 0)
    t[b"OS/2"] = struct.pack(">HhHHHhhhhhhhhhhh10sIIII4sHHHhhhHHIIhhHHH", 4, 1000, 400, 5, 0, 650, 700, 0, 140, 650, 700, 0, 480, 50, 250, 0,
                             bytes(10), 0, 0, 0, 0, b"WLLW", 0x40, ORDER[0], ORDER[-1], 1000, 0, 0, 1000, 0, 1, 0, 500, 700, 0, 32, 0)
    tags = sorted(t); n = len(tags); es = n.bit_length() - 1; sr = 16 * (1 << es)
    out = struct.pack(">IHHHH", 0x10000, n, sr, es, n * 16 - sr); off = 12 + 16 * n; body = b""
    for tag in tags:
        data = t[tag]; out += struct.pack(">4sIII", tag, checksum(data), off + len(body), len(data)); body += data + b"\0" * (-len(data) % 4)
    font = bytearray(out + body)
    hoff = 12 + 16 * n + sum(len(t[x]) + (-len(t[x]) % 4) for x in tags[:tags.index(b"head")])
    struct.pack_into(">I", font, hoff + 8, (0xB1B0AFBA - checksum(bytes(font))) & 0xFFFFFFFF)
    open(path, "wb").write(font)
    print("wrote", path, len(font), "bytes")

if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build(sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "files", "Windows", "Fonts", "WillowsIcons.ttf"))
