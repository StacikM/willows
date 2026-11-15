#!/usr/bin/env python3
"""Where does the CPU spend its time? Reads build/dev/prof.txt (drive.py step prof:N) and prints a histogram of the sampled
instruction pointers by kernel function and by program module (DLL names from the "[dll] loaded" lines in build/dev/serial.txt).

  tools/dev/prof.py [prof.txt]"""
import bisect, collections, os, re, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEV = os.path.join(ROOT, "build", "dev")
ks = []
for line in subprocess.run(["nm", "-n", os.path.join(ROOT, "kernel64.elf")], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in "tT": ks.append((int(p[0], 16), p[2]))
ka = [a for a, _ in ks]
mods = sorted((int(m.group(2), 16), os.path.basename(m.group(1))) for m in re.finditer(r"loaded (\S+) at 0x([0-9a-f]+)", open(os.path.join(DEV, "serial.txt"), errors="replace").read()))
def name(v):
    if 0x100000 <= v < 0x1000000:
        i = bisect.bisect_right(ka, v) - 1
        return "kernel:" + (ks[i][1] if i >= 0 else "?")
    best = None
    for b, n in mods:
        if b <= v < b + 0xC000000 and (best is None or b > best[0]): best = (b, n)
    if best: return best[1]
    if 0x140000000 <= v < 0x140200000: return "program .exe"
    return "other"
samples = [int(l, 16) for l in open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(DEV, "prof.txt")) if l.strip()]
c = collections.Counter(name(v) for v in samples)
print(f"{len(samples)} samples")
for n, k in c.most_common(25): print(f"{100.0 * k / len(samples):6.1f}%  {n}")
