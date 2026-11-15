# gdb script: who overwrites this memory? Hardware watchpoints on a running Willows system.
#   gdb -batch -x tools/dev/gdbwatch.py      with environment variables:
#     WATCH_PTR=0x20cd80150   a pointer variable (e.g. a DLL global: base from the serial log + RVA): once it holds an object
#                             address, the object's first 8 bytes (its vtable) are watched
#     WATCH_ADDR=0x...        or watch this address directly
#     WATCH_HITS=20           stop after this many writes
# QEMU must have been started with QEMU_EXTRA=-s (gdb server on :1234), e.g.
#   QEMU_EXTRA=-s MEM=6144 tools/dev/drive.py --reuse wait300 &   then this script.
# For every write: the instruction address, the new value, and the code addresses on the stack (name them with
# tools/dev/symstack.py-style lookups: "[dll] loaded" lines in build/dev/serial.txt, kernel addresses with addr2line).
import os
import gdb
gdb.execute("set pagination off"); gdb.execute("target remote :1234")
def rd(a): return int(gdb.parse_and_eval("*(unsigned long long*)%#x" % a)) & 0xFFFFFFFFFFFFFFFF
def reg(r): return int(gdb.parse_and_eval("$" + r)) & 0xFFFFFFFFFFFFFFFF
hits = int(os.environ.get("WATCH_HITS", "20"))
target = int(os.environ["WATCH_ADDR"], 16) if "WATCH_ADDR" in os.environ else None
if target is None:
    ptr = int(os.environ["WATCH_PTR"], 16)
    gdb.execute("watch *(unsigned long long*)%#x" % ptr)
    while True:
        gdb.execute("continue")
        v = rd(ptr)
        print("pointer %#x = %#x (written at %#x)" % (ptr, v, reg("pc")))
        if v: target = v; break
    gdb.execute("delete")
print("watching %#x (now %#x)" % (target, rd(target)))
gdb.execute("watch *(unsigned long long*)%#x" % target)
for n in range(hits):
    gdb.execute("continue")
    sp = reg("rsp")
    words = [rd(sp + 8 * i) for i in range(48)]
    code = [hex(w) for w in words if 0x100000 <= w < 0x1000000 or 0x140000000 <= w < 0x400000000]
    print("write #%d at pc=%#x: %#x  stack: %s" % (n + 1, reg("pc"), rd(target), " ".join(code[:16])))
gdb.execute("detach")
