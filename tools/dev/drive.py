#!/usr/bin/env python3
"""Headless QEMU driver for testing Willows: boots willows.img with no window, sends keys / mouse clicks, takes screenshots.

  tools/dev/drive.py [--fresh] STEP...        e.g.   tools/dev/drive.py wait14 ctrl-shift-t wait4 typebrowser ret wait10 shot1

Output goes to build/dev/ (screenshots N.png, serial.txt) which is git-ignored and survives reboots (unlike /tmp).
Steps:
  waitN         sleep N seconds (a cold boot needs ~14 s)
  until:RE:N    wait until the serial log (build/dev/serial.txt) matches regex RE, at most N seconds: ends a run as soon as
                something happens, e.g. until:'exited|CPU exception':300
  stack         RIP, RSP and 256 stack words -> build/dev/stack.txt (name them with tools/dev/symstack.py)
  shotNAME      screenshot -> build/dev/NAME.png
  typeTEXT      type characters (0.25 s each: faster drops keys). Handles space . / : - _ , ; = and uppercase via shift
  KEYNAME       any QEMU key name: ret, esc, ctrl-l, ctrl-shift-t, pgdn, alt_r-3 ...
  atX,Y         move the pointer to X,Y (screen pixels, 1280x720)
  mb1 / mb0     mouse button down / up   (click = mb1 wait0.3 mb0)
  wheelN        scroll wheel
The tool boots a private copy of willows.img (build/dev/test.img) and never modifies the real image or its saved settings.
The copy starts with an empty saved-data area (US keyboard layout); pass --keep to keep the saved data (your layout, downloads) in the copy.
Notes: the first click on the Start button is sometimes swallowed, so click twice with a wait between. Ctrl+Shift+T opens a terminal.
The pointer is PS/2 relative; `at` homes it to the corner first, so it is slow (~3 s)."""
import os, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "build", "dev"); os.makedirs(OUT, exist_ok=True)
IMG = os.path.join(ROOT, "willows.img")
SOCK = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "willows_mon.sock")
SHIFTED = {":": "shift-semicolon", "_": "shift-minus", "?": "shift-slash", "!": "shift-1", "@": "shift-2", "#": "shift-3", "(": "shift-9", ")": "shift-0", "+": "shift-equal", '"': "shift-apostrophe"}
PLAIN = {" ": "spc", ".": "dot", "/": "slash", "-": "minus", ",": "comma", ";": "semicolon", "=": "equal", "'": "apostrophe", "\\": "backslash", "[": "bracket_left", "]": "bracket_right"}

args = sys.argv[1:]
# The tool NEVER touches willows.img (the image you boot yourself, with your saved files and settings such as an AZERTY layout).
# It boots a private copy, build/dev/test.img. By default the copy starts with an empty saved-data area (US layout, no saved files),
# which is what scripted typing needs; --keep carries your saved data into the copy instead.
KEEP = "--keep" in args
REUSE = "--reuse" in args          # boot the previous run's test.img as it was left (to check that saved files survive a reboot)
args = [a for a in args if a not in ("--keep", "--fresh", "--reuse")]
if not args: print(__doc__); sys.exit(0)
TEST_IMG = os.path.join(OUT, "test.img")
if not REUSE: subprocess.run(["cp", "--sparse=always", IMG, TEST_IMG], check=True)
if not KEEP and not REUSE:
    subprocess.run(["dd", "if=/dev/zero", f"of={TEST_IMG}", "bs=512", "seek=122880", "count=8192", "conv=notrunc", "status=none"], check=True)
IMG = TEST_IMG

if os.path.exists(SOCK): os.remove(SOCK)
accel = ["-enable-kvm", "-cpu", "host"] if os.access("/dev/kvm", os.W_OK) else ["-cpu", "max"]
q = subprocess.Popen(["qemu-system-x86_64", *accel, "-m", os.environ.get("MEM", "1024"), "-drive", f"file={IMG},format=raw", "-nic", "user,model=e1000",
                      "-serial", f"file:{OUT}/serial.txt", "-monitor", f"unix:{SOCK},server,nowait", "-display", "none", *os.environ.get("QEMU_EXTRA", "").split()],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK): break
    time.sleep(0.1)
m = socket.socket(socket.AF_UNIX); m.connect(SOCK); m.settimeout(2)

def cmd(c, d=0.05):
    m.sendall((c + "\n").encode()); time.sleep(d)
    try: m.recv(65536)
    except Exception: pass

def shot(name):
    ppm = f"{OUT}/{name}.ppm"; cmd("screendump " + ppm, 0.6)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", ppm, f"{OUT}/{name}.png"]); os.remove(ppm); print("shot", f"{OUT}/{name}.png")

def key_for(ch):
    if ch in SHIFTED: return SHIFTED[ch]
    if ch in PLAIN: return PLAIN[ch]
    if ch.isupper(): return "shift-" + ch.lower()
    return ch

time.sleep(0.5)
try:
    for step in args:
        if step.startswith("until:"):                      # until:REGEX:SECONDS - wait until the serial log matches (or the time is up)
            import re as _re
            pat, _, secs = step[6:].rpartition(":"); limit = time.time() + float(secs or 120); rx = _re.compile(pat)
            while time.time() < limit:
                try: log = open(f"{OUT}/serial.txt", errors="replace").read()
                except OSError: log = ""
                if rx.search(log): print("until: matched", pat); break
                time.sleep(1)
            else: print("until: timed out waiting for", pat)
        elif step.startswith("wait"): time.sleep(float(step[4:]))
        elif step.startswith("shot"): shot(step[4:] or "shot")
        elif step == "regs" or step.startswith("mon:"):   # CPU registers (or any monitor command, e.g. mon:x/16wx 0x3667bdc) -> build/dev/regs.txt
            m.sendall((b"info registers" if step == "regs" else step[4:].encode()) + b"\n"); time.sleep(0.5); out = b""
            try:
                while True:
                    c = m.recv(65536)
                    if not c: break
                    out += c
            except Exception: pass
            open(f"{OUT}/regs.txt", "wb" if step == "regs" else "ab").write(out); print("regs ->", f"{OUT}/regs.txt")
        elif step.startswith("prof:"):                    # prof:N - sample the instruction pointer N times (~20 ms apart) -> build/dev/prof.txt (tools/dev/prof.py)
            import re as _re
            with open(f"{OUT}/prof.txt", "a") as pf:
                for _ in range(int(step[5:])):
                    m.sendall(b"info registers\n"); time.sleep(0.02); out = b""
                    try:
                        while True:
                            ch = m.recv(65536)
                            if not ch: break
                            out += ch
                            if b"RIP=" in out and b"\n" in out[out.index(b"RIP="):]: break
                    except Exception: pass
                    r = _re.search(rb"RIP=([0-9a-f]+)", out)
                    if r: pf.write(r.group(1).decode() + "\n")
            print("prof ->", f"{OUT}/prof.txt")
        elif step == "stack":                              # RIP, RSP and 256 stack words of the running CPU -> build/dev/stack.txt (for a hang)
            def mon(c):
                m.sendall(c.encode() + b"\n"); time.sleep(0.5); out = b""
                try:
                    while True:
                        ch = m.recv(65536)
                        if not ch: break
                        out += ch
                except Exception: pass
                return out.decode(errors="replace")
            r = mon("info registers"); import re as _re
            rip = _re.search(r"RIP=([0-9a-f]+)", r); rsp = _re.search(r"RSP=([0-9a-f]+)", r)
            txt = f"RIP={rip.group(1) if rip else '?'} RSP={rsp.group(1) if rsp else '?'}\n"
            if rsp: txt += mon(f"x/256gx 0x{rsp.group(1)}")
            open(f"{OUT}/stack.txt", "a").write(txt + "\n"); print("stack ->", f"{OUT}/stack.txt")
        elif step.startswith("type"):
            for ch in step[4:]: cmd("sendkey " + key_for(ch), 0.25)
        elif step.startswith("at") and "," in step:
            tx, ty = map(int, step[2:].split(","))
            for _ in range(16): cmd("mouse_move -100 -100", 0.15)
            x = y = 0
            while x < tx or y < ty:
                dx, dy = min(50, tx - x), min(50, ty - y); cmd(f"mouse_move {dx} {dy}", 0.12); x += dx; y += dy
            time.sleep(0.3)
        elif step.startswith("mb"): cmd("mouse_button " + step[2:], 0.15)
        elif step.startswith("wheel"): cmd("mouse_move 0 0 " + step[5:])
        else: cmd("sendkey " + step, 0.1)
finally:
    cmd("quit"); q.wait()
