#!/usr/bin/env python3
"""mkpe32.py app32.o out.exe -- link a freestanding 32-bit (i386) object into a PE32 .exe that Willows runs in compatibility mode.
Imports are undefined symbols named __imp_<dll>__<func> (stdcall functions); they become a real import table with 4-byte IAT slots.
The image has no relocations and is linked at a fixed base inside the low range Willows maps 32-bit programs at (0x400000..0xFFFFFF)."""
import os, struct, subprocess, sys, tempfile
obj, out = sys.argv[1], sys.argv[2]

def pick_base(name):
    """Each 32-bit program gets its own 1 MB slot in 0x800000..0xF00000 (the kernel image ends at about 0xA7F000 - check `nm kernel64.elf | grep kernel_end`; a program's slot must start above that), remembered in tools/appbases32.txt."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "appbases32.txt")
    table = {}
    if os.path.exists(path):
        for line in open(path):
            parts = line.split()
            if len(parts) == 2: table[parts[0]] = int(parts[1])
    key = os.path.basename(name).lower()
    if key not in table:
        table[key] = max(table.values(), default=-1) + 1
        with open(path, "w") as f:
            for k, v in sorted(table.items(), key=lambda kv: kv[1]): f.write(f"{k} {v}\n")
    if table[key] > 7: sys.exit("too many 32-bit programs for the low address range")
    return 0x800000 + table[key] * 0x100000

BASE, TEXT_RVA, FA, SA = pick_base(out), 0x1000, 0x200, 0x1000
align = lambda v, a: (v + a - 1) & ~(a - 1)
syms_undef = [l.split()[-1] for l in subprocess.check_output(["nm", "-u", obj], text=True).splitlines()]
imports = {}
for s in syms_undef:
    if not s.startswith("__imp_"): sys.exit(f"undefined non-import symbol: {s}")
    dll, fn = s[6:].split("__", 1); imports.setdefault(dll, []).append(fn)

def build_idata(rva):
    off = (len(imports) + 1) * 20; slots, ilt, iat = {}, {}, {}
    for dll, fns in imports.items():
        ilt[dll] = off; off += (len(fns) + 1) * 4
        iat[dll] = off; off += (len(fns) + 1) * 4
        for i, fn in enumerate(fns): slots[(dll, fn)] = rva + iat[dll] + 4 * i
    data = bytearray(off)
    def put(b):
        o = len(data)
        if o & 1: data.extend(b"\0"); o += 1
        data.extend(b); return o
    name_rva, hint_rva = {}, {}
    for dll, fns in imports.items():
        name_rva[dll] = rva + put(dll.upper().encode() + b".DLL\0")
        for fn in fns: hint_rva[(dll, fn)] = rva + put(b"\0\0" + fn.encode() + b"\0")
    for n, (dll, fns) in enumerate(imports.items()):
        struct.pack_into("<5I", data, n * 20, rva + ilt[dll], 0, 0, name_rva[dll], rva + iat[dll])
        for i, fn in enumerate(fns):
            struct.pack_into("<I", data, ilt[dll] + 4 * i, hint_rva[(dll, fn)])
            struct.pack_into("<I", data, iat[dll] + 4 * i, hint_rva[(dll, fn)])
    return data, slots

d = tempfile.mkdtemp()
open(d + "/l.ld", "w").write(f"""SECTIONS {{ . = {BASE + TEXT_RVA:#x};
 .all : {{ *(.text*) *(.rodata*) *(.data*) }}
 .bss : {{ . = ALIGN(16); __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(16); __bss_end = .; }}
 /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }} }}""")
def link(slots):
    cmd = ["ld", "-m", "elf_i386", "-z", "max-page-size=4096", "-T", d + "/l.ld", "-e", "mainCRTStartup", "-o", d + "/a.elf", obj]
    for (dll, fn), rva in slots.items(): cmd.append(f"--defsym=__imp_{dll}__{fn}={BASE + rva:#x}")
    subprocess.check_call(cmd)
    return {l.split()[2]: int(l.split()[0], 16) for l in subprocess.check_output(["nm", d + "/a.elf"], text=True).splitlines() if len(l.split()) == 3}
_, tmp = build_idata(0x4000000); syms = link(tmp)
IDATA_RVA = align(syms.get("__bss_end", BASE + TEXT_RVA) - BASE, SA) + SA
idata, slots = build_idata(IDATA_RVA); syms = link(slots)
subprocess.check_call(["objcopy", "-O", "binary", "-j", ".all", d + "/a.elf", d + "/a.bin"])
text = open(d + "/a.bin", "rb").read()
entry = syms["mainCRTStartup"] - BASE; virt = syms["__bss_end"] - BASE - TEXT_RVA
text_raw, id_raw = align(len(text), FA), align(len(idata), FA)
hdr = bytearray(FA); hdr[0:2] = b"MZ"; struct.pack_into("<I", hdr, 0x3C, 0x40); p = 0x40
hdr[p:p+4] = b"PE\0\0"; p += 4
struct.pack_into("<2H3I2H", hdr, p, 0x14C, 2, 0, 0, 0, 224, 0x0103); p += 20           # i386, EXE | 32BIT_MACHINE | RELOCS_STRIPPED
img_size = IDATA_RVA + align(len(idata), SA)
opt = struct.pack("<H2B6I", 0x10B, 1, 0, text_raw, id_raw, max(virt - text_raw, 0), entry, TEXT_RVA, IDATA_RVA)
opt += struct.pack("<I2I6H4I2H6I", BASE, SA, FA, 4, 0, 0, 0, 4, 0, 0, img_size, FA, 0, 3, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
dirs = bytearray(16 * 8); struct.pack_into("<2I", dirs, 8, IDATA_RVA, len(idata))
assert len(opt) + len(dirs) == 224, len(opt)
hdr[p:p+224] = opt + dirs; p += 224
def sect(name, vsize, va, raw, rawptr, ch):
    global p
    hdr[p:p+40] = struct.pack("<8s6I2HI", name, vsize, va, raw, rawptr, 0, 0, 0, 0, ch); p += 40
sect(b".text", virt, TEXT_RVA, text_raw, FA, 0xE0000060)
sect(b".idata", len(idata), IDATA_RVA, id_raw, FA + text_raw, 0xC0000040)
open(out, "wb").write(bytes(hdr) + text.ljust(text_raw, b"\0") + bytes(idata).ljust(id_raw, b"\0"))
print(f"{out}: 32-bit, {len(text)} bytes code at {BASE:#x}, imports: " + ", ".join(f"{k}({len(v)})" for k, v in imports.items()))
