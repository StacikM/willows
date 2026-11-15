#!/usr/bin/env python3
"""Name the addresses in build/dev/stack.txt (drive.py step `stack`, taken while the system hangs):
kernel addresses via kernel64.elf's symbol table, program/DLL addresses via the "[dll] loaded X at 0x..." lines of build/dev/serial.txt.

  tools/dev/symstack.py [stack.txt]
Only stack words that point into code are printed, newest (lowest address) first."""
import bisect, os, re, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEV = os.path.join(ROOT, "build", "dev")
ksyms = []
for line in subprocess.run(["nm", "-n", os.path.join(ROOT, "kernel64.elf")], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in "tT": ksyms.append((int(p[0], 16), p[2]))
kaddr = [a for a, _ in ksyms]
mods = []
for line in open(os.path.join(DEV, "serial.txt"), errors="replace"):
    m = re.search(r"loaded (\S+) at 0x([0-9a-f]+)", line)
    if m: mods.append((int(m.group(2), 16), os.path.basename(m.group(1))))
def name(v):
    if 0x100000 <= v < 0x1000000 and kaddr:
        i = bisect.bisect_right(kaddr, v) - 1
        if i >= 0: return f"{ksyms[i][1]}+{v - ksyms[i][0]:x}"
    best = None
    for base, n in mods:
        if base <= v < base + 0x10000000 and (best is None or base > best[0]): best = (base, n)
    if best and v - best[0] < 0xC000000: return f"{best[1]}+{v - best[0]:x}"
    if 0x140000000 <= v < 0x140200000: return f"exe+{v - 0x140000000:x}"
    return None
text = open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(DEV, "stack.txt")).read()
for block in text.split("RIP=")[1:]:
    rip = int(block.split()[0], 16); print(f"--- RIP {name(rip) or hex(rip)}")
    for v in re.findall(r"0x([0-9a-f]{16})", block):
        n = name(int(v, 16))
        if n: print("   ", n)
