#!/usr/bin/env python3
"""mkpe.py app.o out.exe -- link a freestanding x86-64 object into a PE32+ .exe.
Imports are undefined symbols named __imp_<dll>__<func>; they become a real import table."""
import subprocess, struct, sys, tempfile
obj, out = sys.argv[1], sys.argv[2]
IS_DLL = out.lower().endswith(".dll")             # DLL mode: symbols named EXPORT_<name> are exported, DllMain is the entry point
import os
def pick_base(name):
    """Every program gets its own 8 MB slot above 0x140000000 (the images have no relocations, so two programs cannot share an address).
    Slots are remembered in tools/appbases.txt so a program keeps the same address between builds."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "appbases.txt")
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
    return 0x140000000 + table[key] * 0x800000
BASE, TEXT_RVA = pick_base(out), 0x1000
FA, SA = 0x200, 0x1000
align = lambda v, a: (v + a - 1) & ~(a - 1)

syms = [l.split()[-1] for l in subprocess.check_output(["nm", "-u", obj], text=True).splitlines()]
imports = {}                                   # dll -> [func]
for s in syms:
    if s == "_GLOBAL_OFFSET_TABLE_": continue            # only referenced by relaxable GOT relocations (resolved at link time)
    if not s.startswith("__imp_"): sys.exit(f"undefined non-import symbol: {s}")
    dll, fn = s[6:].split("__", 1)
    imports.setdefault(dll, []).append(fn)

def build_idata(IDATA_RVA):
    off = (len(imports) + 1) * 20
    slots, ilt, iat = {}, {}, {}
    for dll, fns in imports.items():
        ilt[dll] = off; off += (len(fns) + 1) * 8
        iat[dll] = off; off += (len(fns) + 1) * 8
        for i, fn in enumerate(fns): slots[(dll, fn)] = IDATA_RVA + iat[dll] + 8 * i
    idata = bytearray(off)
    def put(s):
        o = len(idata)
        if o & 1: idata.extend(b"\0"); o += 1
        idata.extend(s); return o
    dll_name_rva, hint_rva = {}, {}
    for dll, fns in imports.items():
        dll_name_rva[dll] = IDATA_RVA + put(dll.upper().encode() + b".DLL\0")
        for fn in fns: hint_rva[(dll, fn)] = IDATA_RVA + put(b"\0\0" + fn.encode() + b"\0")
    for n, (dll, fns) in enumerate(imports.items()):
        struct.pack_into("<5I", idata, n * 20, IDATA_RVA + ilt[dll], 0, 0, dll_name_rva[dll], IDATA_RVA + iat[dll])
        for i, fn in enumerate(fns):
            struct.pack_into("<Q", idata, ilt[dll] + 8 * i, hint_rva[(dll, fn)])
            struct.pack_into("<Q", idata, iat[dll] + 8 * i, hint_rva[(dll, fn)])
    return idata, slots

d = tempfile.mkdtemp()
open(d + "/l.ld", "w").write(f"""SECTIONS {{ . = {BASE+TEXT_RVA:#x};
 .all : {{ *(.text*) *(.rodata*) *(.data*) *(.got) *(.got.plt) *(.igot*) }}
 .bss : {{ . = ALIGN(16); __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(16); __bss_end = .; }}
 /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }} }}""")
def link(slots):
    cmd = ["ld", "-m", "elf_x86_64", "-z", "max-page-size=4096", "-T", d + "/l.ld", "-e", "DllMain" if IS_DLL else "mainCRTStartup", "-o", d + "/a.elf", obj]
    cmd.append("--defsym=_GLOBAL_OFFSET_TABLE_=0")
    for (dll, fn), rva in slots.items(): cmd.append(f"--defsym=__imp_{dll}__{fn}={BASE+rva:#x}")
    subprocess.check_call(cmd)
    syms = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.check_output(["nm", d + "/a.elf"], text=True).splitlines() if len(l.split()) == 3}
    return syms
# pass 1: link against placeholder import addresses to learn how big code+data+bss is
_, tmp_slots = build_idata(0x4000000)
syms = link(tmp_slots)
end_rva = (syms.get("__bss_end", BASE + TEXT_RVA) - BASE)
IDATA_RVA = align(end_rva, SA) + SA
idata, slots = build_idata(IDATA_RVA)
syms = link(slots)                                  # pass 2: real import table addresses
if os.environ.get("MKPE_SYM"):                              # debugging aid: RVA -> symbol list
    open(os.environ["MKPE_SYM"], "w").write("".join(f"{v-BASE:x} {k}\n" for k, v in sorted(syms.items(), key=lambda kv: kv[1])))
subprocess.check_call(["objcopy", "-O", "binary", "-j", ".all", d + "/a.elf", d + "/a.bin"])
text = open(d + "/a.bin", "rb").read()
entry = (syms.get("DllMain", BASE) - BASE) if IS_DLL else syms["mainCRTStartup"] - BASE
bss_end = syms["__bss_end"] - BASE
virt_size = bss_end - TEXT_RVA                      # code + data + zero-initialised memory

EDATA_RVA = IDATA_RVA + align(len(idata), SA); edata = b""
if IS_DLL:
    exps = sorted((n[7:], a - BASE) for n, a in syms.items() if n.startswith("EXPORT_"))
    ne = len(exps); dll_name = os.path.basename(out).encode() + b"\0"
    hdr_sz = 40; aof = hdr_sz; aon = aof + 4 * ne; aoo = aon + 4 * ne; strs = aoo + 2 * ne
    blob = bytearray(dll_name); name_off = []
    for n, _ in exps: name_off.append(strs + len(blob)); blob += n.encode() + b"\0"
    ed = bytearray(strs) + blob
    struct.pack_into("<2I2H5I", ed, 0, 0, 0, 0, 0, EDATA_RVA + strs, 1, ne, ne, EDATA_RVA + aof)
    struct.pack_into("<2I", ed, 32, EDATA_RVA + aon, EDATA_RVA + aoo)
    for i, (n, rva) in enumerate(exps):
        struct.pack_into("<I", ed, aof + 4 * i, rva); struct.pack_into("<I", ed, aon + 4 * i, EDATA_RVA + name_off[i]); struct.pack_into("<H", ed, aoo + 2 * i, i)
    edata = bytes(ed)
text_raw, id_raw = align(len(text), FA), align(len(idata), FA); ed_raw = align(len(edata), FA)
hdr = bytearray(FA)
hdr[0:2] = b"MZ"; struct.pack_into("<I", hdr, 0x3C, 0x40)
p = 0x40
hdr[p:p+4] = b"PE\0\0"; p += 4
struct.pack_into("<2H3I2H", hdr, p, 0x8664, 3 if IS_DLL else 2, 0, 0, 0, 240, 0x2022 if IS_DLL else 0x0022); p += 20     # x64, EXE | LARGE_ADDRESS_AWARE
opt = struct.pack("<H2B5I", 0x20B, 1, 0, text_raw, id_raw, virt_size - text_raw if virt_size > text_raw else 0, entry, TEXT_RVA)
opt += struct.pack("<Q2I6H4I2H4Q2I", BASE, SA, FA, 4, 0, 0, 0, 5, 2, 0,
                   IDATA_RVA + align(len(idata), SA) + align(len(edata), SA), FA, 0, 3, 0, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
dirs = bytearray(16 * 8)
struct.pack_into("<2I", dirs, 8, IDATA_RVA, len(idata))
if IS_DLL: struct.pack_into("<2I", dirs, 0, EDATA_RVA, len(edata))
assert len(opt) + len(dirs) == 240, len(opt)
hdr[p:p+240] = opt + dirs; p += 240
def sect(name, vsize, va, raw, rawptr, ch):
    global p
    hdr[p:p+40] = struct.pack("<8s6I2HI", name, vsize, va, raw, rawptr, 0, 0, 0, 0, ch); p += 40
sect(b".text", virt_size, TEXT_RVA, text_raw, FA, 0xE0000060)
sect(b".idata", len(idata), IDATA_RVA, id_raw, FA + text_raw, 0xC0000040)
if IS_DLL: sect(b".edata", len(edata), EDATA_RVA, ed_raw, FA + text_raw + id_raw, 0x40000040)
open(out, "wb").write(bytes(hdr) + text.ljust(text_raw, b"\0") + bytes(idata).ljust(id_raw, b"\0") + edata.ljust(ed_raw, b"\0"))
print(f"{out}: {len(text)} bytes code, imports: " + ", ".join(f"{d}({len(f)})" for d, f in imports.items()))
