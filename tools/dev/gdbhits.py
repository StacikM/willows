# gdb script: log every call of one function in a running Willows program - registers, a few fields, and the code addresses on the
# stack (the callers) - without stopping the program for long. Good for comparing a run that crashes with one that does not.
#   HITS_AT=0x203140f00[,0x...] HITS_FIELDS='rcx+0xd8; *(rcx+0xd8)+0x28' HITS_LO=0x202400000 HITS_HI=0x20e000000 gdb -batch -x tools/dev/gdbhits.py
#     HITS_AT      runtime address to break at (DLL base from the serial log + RVA, see pefunc.py)
#     HITS_FIELDS  ';'-separated addresses to read a 64-bit value from; registers by name, '*(x)' reads memory
#     HITS_LO/HI   the DLL's address range: stack words inside it are printed as +RVA (the callers)
#     HITS_MAX     stop after this many calls (default 60)
# QEMU must have been started with QEMU_EXTRA=-s.
import os, re
import gdb
ats = [int(a, 16) for a in os.environ["HITS_AT"].split(",")]   # several addresses: comma-separated (hardware breakpoints, at most 4)
lo = int(os.environ.get("HITS_LO", "0"), 16); hi = int(os.environ.get("HITS_HI", "0"), 16)
fields = [f.strip() for f in os.environ.get("HITS_FIELDS", "").split(";") if f.strip()]; limit = int(os.environ.get("HITS_MAX", "60"))
gdb.execute("set pagination off"); gdb.execute("target remote :1234")
def u64(expr):
    try: return int(gdb.parse_and_eval("*(unsigned long long *)(%s)" % re.sub(r"\b(r[a-z0-9]+)\b", r"$\1", expr))) & 0xFFFFFFFFFFFFFFFF
    except gdb.error: return None
for at in ats: gdb.execute("hbreak *%#x" % at)
for n in range(limit):
    try: gdb.execute("continue")
    except gdb.error: break
    vals = []
    for f in fields:
        v = u64(f); vals.append("[%s]=%s" % (f, "unreadable" if v is None else "%#x" % v))
    rsp = int(gdb.parse_and_eval("$rsp")) & 0xFFFFFFFFFFFFFFFF; callers = []
    for k in range(0, 0x400, 8):
        v = u64("%#x" % (rsp + k))
        if v is not None and lo <= v < hi: callers.append("+%x" % (v - lo))
        if len(callers) >= 10: break
    print("hit %d at %#x rcx=%#x %s callers %s" % (n + 1, int(gdb.parse_and_eval("$pc")) & 0xFFFFFFFFFFFFFFFF, int(gdb.parse_and_eval("$rcx")) & 0xFFFFFFFFFFFFFFFF, " ".join(vals), " ".join(callers)), flush=True)
gdb.execute("detach")
