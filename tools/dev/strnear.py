#!/usr/bin/env python3
"""What is this function? Lists the strings (ASCII and UTF-16) that the function containing each address refers to.

  tools/dev/strnear.py file.dll 0x1814c0f60 [more static addresses ...]       (ImageBase + RVA; see pefunc.py)
Useful for naming functions in a stripped program: assertion texts, log messages and pref names give them away."""
import os, re, struct, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
L = sys.argv[1]; d = open(L, "rb").read()
pe = struct.unpack_from("<I", d, 0x3C)[0]; nsec = struct.unpack_from("<H", d, pe + 6)[0]; optsz = struct.unpack_from("<H", d, pe + 20)[0]
base = struct.unpack_from("<Q", d, pe + 24 + 24)[0]
secs = [struct.unpack_from("<IIII", d, pe + 24 + optsz + 40 * i + 8) for i in range(nsec)]
def off(va):
    rva = va - base
    for vs, sva, rs, rp in secs:
        if sva <= rva < sva + max(vs, rs): return rva - sva + rp
for a in sys.argv[2:]:
    ra = int(a, 16)
    fn = subprocess.run([sys.executable, os.path.join(here, "pefunc.py"), L, hex(ra)], capture_output=True, text=True).stdout.split()
    st, en = (int(fn[2], 16), int(fn[4], 16)) if len(fn) > 4 and fn[1] == "function" else (ra - 0x400, ra + 0x100)
    dis = subprocess.run(["objdump", "-d", f"--start-address={st:#x}", f"--stop-address={en:#x}", L], capture_output=True, text=True).stdout
    out = []
    for m in re.finditer(r"lea\s+0x[0-9a-f]+\(%rip\),%\w+\s+# 0x([0-9a-f]+)", dis):
        o = off(int(m.group(1), 16))
        if o is None: continue
        b = d[o:o + 100]; e = b.find(b"\0"); t = b[:e]
        if len(t) >= 5 and all(32 <= c < 127 for c in t): s = t.decode()
        else:
            try: s = d[o:o + 200].decode("utf-16-le").split("\0")[0]; s = "W:" + s if len(s) >= 5 and s.isprintable() else None
            except Exception: s = None
        if s and s not in out: out.append(s)
    print(f"{a}: function {st:#x}..{en:#x}: {out[:12]}")
