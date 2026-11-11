#!/usr/bin/env python3
"""Headless QEMU driver for testing Willows: boots willows.img with no window, sends keys / mouse clicks, takes screenshots.

  tools/dev/drive.py [--fresh] STEP...        e.g.   tools/dev/drive.py wait14 ctrl-shift-t wait4 typebrowser ret wait10 shot1

Output goes to build/dev/ (screenshots N.png, serial.txt) which is git-ignored and survives reboots (unlike /tmp).
Steps:
  waitN         sleep N seconds (a cold boot needs ~14 s)
  shotNAME      screenshot -> build/dev/NAME.png
  typeTEXT      type characters (0.25 s each: faster drops keys). Handles space . / : - _ , ; = and uppercase via shift
  KEYNAME       any QEMU key name: ret, esc, ctrl-l, ctrl-shift-t, pgdn, alt_r-3 ...
  atX,Y         move the pointer to X,Y (screen pixels, 1280x720)
  mb1 / mb0     mouse button down / up   (click = mb1 wait0.3 mb0)
  wheelN        scroll wheel
--fresh zeroes the saved-data area of the image first (forgets saved files and settings such as the AZERTY layout).
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
if "--fresh" in args:
    args.remove("--fresh")
    subprocess.run(["dd", "if=/dev/zero", f"of={IMG}", "bs=512", "seek=122880", "count=8192", "conv=notrunc", "status=none"], check=True)
if not args: print(__doc__); sys.exit(0)

if os.path.exists(SOCK): os.remove(SOCK)
accel = ["-enable-kvm", "-cpu", "host"] if os.access("/dev/kvm", os.W_OK) else ["-cpu", "max"]
q = subprocess.Popen(["qemu-system-x86_64", *accel, "-m", "256", "-drive", f"file={IMG},format=raw", "-nic", "user,model=e1000",
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
        if step.startswith("wait"): time.sleep(float(step[4:]))
        elif step.startswith("shot"): shot(step[4:] or "shot")
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
