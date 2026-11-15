# gdb script: what C++ objects are these? Breaks at an address and names objects by their MSVC RTTI (vtable[-1] -> complete object
# locator -> type descriptor -> mangled name such as ".?AVAppWindow@mozilla@@").
#   RTTI_BREAK=0x203140f30 RTTI_COND='$rax==0' RTTI_EXPR='$rcx;*(long*)($rcx+0xd8)' gdb -batch -x tools/dev/gdbrtti.py
#     RTTI_BREAK  runtime address to stop at (DLL base from the serial log + RVA)
#     RTTI_COND   optional gdb condition (only stop when it holds)
#     RTTI_EXPR   ';'-separated expressions whose values are object pointers to name
# QEMU must have been started with QEMU_EXTRA=-s.
import os
import gdb
gdb.execute("set pagination off"); gdb.execute("target remote :1234")
def rd(a, n=8):
    return int(gdb.parse_and_eval("*(unsigned %s*)%#x" % ("long long" if n == 8 else "int", a))) & ((1 << (8 * n)) - 1)
def rtti(obj):
    try:
        vt = rd(obj); col = rd(vt - 8); self_rva = rd(col + 20, 4); base = col - self_rva
        td = base + rd(col + 12, 4); name = gdb.execute("x/s %#x" % (td + 16), to_string=True).split(":", 1)[1].strip()
        return "%#x vtable %#x -> %s" % (obj, vt, name)
    except Exception as e:
        return "%#x (no RTTI: %s)" % (obj, e)
b = os.environ["RTTI_BREAK"]; cond = os.environ.get("RTTI_COND")
gdb.execute("hbreak *%s" % b)
if cond: gdb.execute("condition 1 %s" % cond)
gdb.execute("continue")
for e in os.environ["RTTI_EXPR"].split(";"):
    v = int(gdb.parse_and_eval(e)) & 0xFFFFFFFFFFFFFFFF
    print("%s = %s" % (e, rtti(v) if v else "0"))
gdb.execute("detach")
