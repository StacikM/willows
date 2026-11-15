#!/usr/bin/env python3
"""Which function contains an address? Uses the PE exception table (.pdata: begin/end of every x64 function).

  tools/dev/pefunc.py file.dll 0x1801563b9 [more addresses ...]     (static addresses, i.e. ImageBase + RVA)
Prints the function's start and end, so breakpoints can be put on its entry and its returns."""
import bisect, struct, sys
d = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", d, 0x3C)[0]; nsec = struct.unpack_from("<H", d, pe + 6)[0]; optsz = struct.unpack_from("<H", d, pe + 20)[0]
opt = pe + 24; base = struct.unpack_from("<Q", d, opt + 24)[0]; dd = opt + 112
prva, psize = struct.unpack_from("<II", d, dd + 3 * 8)
secs = [struct.unpack_from("<IIII", d, opt + optsz + 40 * i + 8) for i in range(nsec)]
def off(rva):
    for vs, va, rs, rp in secs:
        if va <= rva < va + max(vs, rs): return rva - va + rp
o = off(prva); fns = sorted(struct.unpack_from("<II", d, o + 12 * i) for i in range(psize // 12))
starts = [b for b, e in fns]
for a in sys.argv[2:]:
    rva = int(a, 16) - base; i = bisect.bisect_right(starts, rva) - 1
    if i >= 0 and fns[i][0] <= rva < fns[i][1]: print(f"{a}: function {base + fns[i][0]:#x} .. {base + fns[i][1]:#x} (rva {fns[i][0]:#x})")
    else: print(f"{a}: not inside a listed function")
