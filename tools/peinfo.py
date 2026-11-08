#!/usr/bin/env python3
"""peinfo.py prog.exe -- show what a Windows .exe is and which of its imports Willows implements."""
import re, struct, sys, os
data = open(sys.argv[1], "rb").read()
if data[:2] != b"MZ": sys.exit("not a PE file")
pe = struct.unpack_from("<I", data, 0x3C)[0]
if data[pe:pe+4] != b"PE\0\0": sys.exit("bad PE signature")
machine, nsec, _, _, _, optsz, chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
opt = pe + 24; magic = struct.unpack_from("<H", data, opt)[0]
p64 = magic == 0x20B
base = struct.unpack_from("<Q" if p64 else "<I", data, opt + (24 if p64 else 28))[0]
subsys = struct.unpack_from("<H", data, opt + 68)[0]
dd = opt + (112 if p64 else 96)
imp_rva, _ = struct.unpack_from("<II", data, dd + 8)
tls_rva, _ = struct.unpack_from("<II", data, dd + 9 * 8)
reloc_rva, _ = struct.unpack_from("<II", data, dd + 5 * 8)
secs = []
so = opt + optsz
for i in range(nsec):
    n, vs, va, rs, rp = struct.unpack_from("<8sIIII", data, so + 40 * i); secs.append((va, max(vs, rs), rp))
def off(rva):
    for va, sz, rp in secs:
        if va <= rva < va + sz: return rva - va + rp
    raise ValueError(hex(rva))
def cstr(o):
    e = data.index(b"\0", o); return data[o:e].decode("latin1")
arch = {0x8664: "x64", 0x14C: "x86 (32-bit)", 0xAA64: "ARM64"}.get(machine, hex(machine))
print(f"{sys.argv[1]}: PE{'32+' if p64 else '32'}  machine={arch}  subsystem={ {2:'GUI',3:'console'}.get(subsys, subsys) }  imagebase={base:#x}  relocs={'yes' if reloc_rva else 'no'}  tls={'yes' if tls_rva else 'no'}")
if machine != 0x8664: print("!! Willows runs x64 (PE32+) programs only")
here = os.path.dirname(os.path.abspath(__file__))
import glob
src = "".join(open(f).read() for f in glob.glob(os.path.join(here, "../src/*.c")) + glob.glob(os.path.join(here, "../src/*.inc")))
have = set(re.findall(r'\b(?:[KUGCWX]|WI|XD)\((?:"\w+",)?(\w+)\)', src)) | set(re.findall(r'\{"\w+","(\w+)"', src)) | set(re.findall(r'CX\("(\w+)"', src))
total = missing = 0
o = off(imp_rva) if imp_rva else None
while o:
    oft, _, _, name, ft = struct.unpack_from("<5I", data, o)
    if not name: break
    dll = cstr(off(name)); t = off(oft or ft); step = 8 if p64 else 4; fns = []
    while True:
        v = struct.unpack_from("<Q" if p64 else "<I", data, t)[0]
        if not v: break
        fns.append(f"#{v & 0xFFFF}" if v >> (63 if p64 else 31) else cstr(off(v & 0x7FFFFFFF) + 2)); t += step
    miss = [f for f in fns if f not in have]
    total += len(fns); missing += len(miss)
    print(f"  {dll}: {len(fns)} imports, {len(miss)} missing")
    for f in miss: print(f"      - {f}")
    o += 20
print(f"\n{total - missing}/{total} imports implemented" + ("  -> should get past loading" if not missing else "  -> missing ones are stubbed and the program stops when it calls one"))
