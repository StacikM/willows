#!/bin/sh
# Build build/dev/test.img: the current kernel and apps plus a saved-data area made from build/dev/testdata/ (never touches willows.img).
#   tools/dev/mktest.sh ["command line" ...]      then   tools/dev/drive.py --reuse wait20 shot1
# With command lines, Willows opens a terminal at boot that runs them in turn (C:\System\autorun.cfg). Redirect output
# to a file ("apitest > /Documents/t.txt") and read it back with: tools/dev/datafs.py build/dev/test.img list / extract. E.g.
#   tools/dev/mktest.sh "/Downloads/librewolf/LibreWolf/librewolf.exe"
# build/dev/testdata/ mirrors C:\ (e.g. testdata/Downloads/app.exe). Seed it from your image read-only with
#   tools/dev/datafs.py willows.img extract build/dev/testdata      (then delete what you don't need: it is all loaded into RAM at boot)
# A System/settings.cfg is written with layout=us so that drive.py's typing works.
# TRACE=1 also writes System/apitrace.cfg: programs' Windows API calls are recorded and crash reports list the last 24.
# Ctrl+F12 in the running system (drive.py step ctrl-f12) dumps every process and thread to the serial log (build/dev/serial.txt).
set -e
cd "$(dirname "$0")/../.."
make -s all
D=${TESTDATA:-build/dev/testdata}                     # TESTDATA=dir: another folder for C:\ (TESTS.sh uses an empty one)
mkdir -p "$D/System"
printf 'layout=us\n' > "$D/System/settings.cfg"
rm -f "$D/System/autorun.cfg"; for c in "$@"; do printf '%s\n' "$c" >> "$D/System/autorun.cfg"; done
if [ -n "$TRACE" ]; then printf 'on\n' > "$D/System/apitrace.cfg"; else rm -f "$D/System/apitrace.cfg"; fi
rm -f build/dev/test.img
./tools/mkdisk.sh build/dev/test.img >/dev/null
python3 tools/dev/datafs.py build/dev/test.img pack "$D"
