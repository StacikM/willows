# gdb script: trace one function of a running Willows program, call by call (with the value each call returned).
#   gdb -batch -x tools/dev/gdbtrace.py      with environment variables:
#     TRACE_ENTRY=0x202556070   runtime address of the function (DLL base from the serial log + RVA, see pefunc.py)
#     TRACE_END=0x20255745d     end of the function (from pefunc.py), so calls are stepped over, not into
#     TRACE_MAX=20000           at most this many instructions
# QEMU must have been started with QEMU_EXTRA=-s (gdb server on :1234), e.g.
#   QEMU_EXTRA=-s tools/dev/drive.py --reuse wait200 &   then this script.
# Output: every call (address, instruction) and rax after it; the function's return value at the end.
import os
import gdb
entry = int(os.environ["TRACE_ENTRY"], 16); end = int(os.environ["TRACE_END"], 16); limit = int(os.environ.get("TRACE_MAX", "20000"))
gdb.execute("set pagination off"); gdb.execute("target remote :1234")
gdb.execute("hbreak *%#x" % entry); gdb.execute("continue")
print("entered %#x" % entry)
def pc(): return int(gdb.parse_and_eval("$pc")) & 0xFFFFFFFFFFFFFFFF
for n in range(limit):
    a = pc()
    if not (entry <= a < end): print("left the function at %#x, rax=%#x" % (a, int(gdb.parse_and_eval("$rax")) & 0xFFFFFFFFFFFFFFFF)); break
    ins = gdb.execute("x/i $pc", to_string=True).split(":", 1)[-1].strip()
    if ins.startswith("call"):
        gdb.execute("nexti", to_string=True)
        print("%#x (+%#x) %-50s -> rax=%#x" % (a, a - entry, ins[:50], int(gdb.parse_and_eval("$rax")) & 0xFFFFFFFFFFFFFFFF))
    else:
        gdb.execute("stepi", to_string=True)
gdb.execute("detach")
