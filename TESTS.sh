#!/bin/sh
# TESTS.sh - build Willows and run its tests headless in QEMU, on the private test disk build/dev/test.img (never willows.img).
#   ./TESTS.sh              build, boot checks, the API suites (apitest, apitest2, apitest3, cxxtest) and the network test
#   ./TESTS.sh --panic      also the three kernel panic paths (each one halts its own test machine, on purpose)
#   ./TESTS.sh --librewolf  also start LibreWolf on a web page (needs a LibreWolf portable folder in build/dev/testdata and
#                           6 GB of RAM for the VM)
#   ./TESTS.sh --all        everything
# Exit status: 0 when everything passed, 1 when something failed, 2 for a usage or setup problem.
# Results, logs and screenshots go to build/dev/tests/.
set -u
cd "$(dirname "$0")"

PANIC=0 LIBREWOLF=0
for a in "$@"; do
  case $a in
    --panic) PANIC=1 ;;
    --librewolf) LIBREWOLF=1 ;;
    --all) PANIC=1; LIBREWOLF=1 ;;
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "TESTS.sh: unknown option $a (try --help)"; exit 2 ;;
  esac
done
for t in make python3 qemu-system-x86_64; do command -v "$t" >/dev/null || { echo "TESTS.sh needs $t"; exit 2; }; done

OUT=build/dev/tests; rm -rf "$OUT"; mkdir -p "$OUT"
EMPTY=build/dev/testdata-tests; rm -rf "$EMPTY"; mkdir -p "$EMPTY"     # a C:\ with nothing in it: small and fast to boot
PASS=0 FAIL=0 SKIP=0; START=$(date +%s)
if [ -t 1 ]; then G='\033[32m' R='\033[31m' Y='\033[33m' N='\033[0m'; else G='' R='' Y='' N=''; fi   # colours on a terminal only
pass(){ PASS=$((PASS+1)); printf "  ${G}PASS${N}  %s\n" "$1"; }
fail(){ FAIL=$((FAIL+1)); printf "  ${R}FAIL${N}  %s\n" "$1"; [ $# -gt 1 ] && [ -n "$2" ] && printf '        %s\n' "$2"; }
skip(){ SKIP=$((SKIP+1)); printf "  ${Y}SKIP${N}  %s\n" "$1"; }
# boot the test disk headless: drive.py [steps...]; the serial log lands in build/dev/serial.txt
boot(){ timeout 600 python3 tools/dev/drive.py --reuse "$@" >>"$OUT/drive.log" 2>&1; }

echo "Building"
if make -s all >"$OUT/build.log" 2>&1; then pass "build (kernel and apps)"; else fail "build" "see $OUT/build.log"; exit 1; fi

# ---- one boot runs every suite; each writes its report to C:\Documents ----
echo "API suites and network"
set -- "sleep 8" "apitest > /Documents/t1.txt" "apitest2 > /Documents/t2.txt" "apitest3 > /Documents/t3.txt" "nettest > /Documents/t4.txt"
[ -f apps/cxxtest.exe ] && set -- "$@" "cxxtest > /Documents/t5.txt"
TESTDATA=$EMPTY tools/dev/mktest.sh "$@" >"$OUT/mktest.log" 2>&1 || { fail "making the test disk" "see $OUT/mktest.log"; exit 1; }
boot wait75 shot1
cp build/dev/serial.txt "$OUT/serial-suites.txt" 2>/dev/null; cp build/dev/1.png "$OUT/suites.png" 2>/dev/null
python3 tools/dev/datafs.py build/dev/test.img extract "$OUT/disk" >/dev/null 2>&1
suite(){                                                               # suite FILE NAME
  f="$OUT/disk/Documents/$1.txt"
  if [ ! -f "$f" ]; then fail "$2" "no report: it did not run or it crashed (see $OUT/serial-suites.txt)"; return; fi
  last=$(tail -n 1 "$f")
  case $last in
    *"ALL TESTS PASSED"*) pass "$2: ${last#*: }" ;;
    *) fail "$2: $last" "$(grep FAIL "$f" | head -5 | tr '\n' ';')" ;;
  esac
}
suite t1 apitest; suite t2 apitest2; suite t3 apitest3
if [ -f apps/cxxtest.exe ]; then suite t5 "cxxtest (C++ exceptions)"; else skip "cxxtest (MinGW-w64 is not installed)"; fi
n=$(sed -n 's/.*-- \([0-9]*\) bytes received --.*/\1/p' "$OUT/disk/Documents/t4.txt" 2>/dev/null | tail -n 1)
if [ -n "$n" ] && [ "$n" -gt 0 ]; then pass "nettest: $n bytes over HTTP"; else fail "nettest" "$(tail -n 1 "$OUT/disk/Documents/t4.txt" 2>/dev/null) (no network from the VM?)"; fi

# ---- the boot itself ----
echo "Boot"
S="$OUT/serial-suites.txt"
lang=$(grep -o 'Languages: .*' "$S" | head -n 1)
case $lang in
  "Languages: C++ ok, Rust ok") pass "kernel C++ and Rust self-tests" ;;
  *"C++ ok, Rust not built in"*) pass "kernel C++ self-test"; skip "kernel Rust (the x86_64-unknown-none Rust target is not installed)" ;;
  *) fail "kernel language self-tests" "${lang:-no Languages line in the serial log}" ;;
esac
if grep -q '\*\*\* PANIC' "$S"; then fail "no kernel panic" "$(grep -A1 '\*\*\* PANIC' "$S" | tail -n 1)"; else pass "no kernel panic"; fi
c=$(grep -c 'CPU exception' "$S"); if [ "$c" -eq 0 ]; then pass "no program crashed"; else fail "no program crashed" "$c crash report(s) in $S"; fi

# ---- the panic paths: each halts its machine, then the report is checked ----
if [ $PANIC = 1 ]; then
  echo "Kernel panics"
  for k in c rust fault; do
    TESTDATA=$EMPTY tools/dev/mktest.sh "panic $k" >>"$OUT/mktest.log" 2>&1
    boot wait25 shot1; cp build/dev/serial.txt "$OUT/serial-panic-$k.txt"; cp build/dev/1.png "$OUT/panic-$k.png" 2>/dev/null
    case $k in c) want='test panic requested' ;; rust) want='Rust panic: ' ;; fault) want='panic_service+' ;; esac
    if grep -q '\*\*\* PANIC: Willows stopped' "$OUT/serial-panic-$k.txt" && grep -q "$want" "$OUT/serial-panic-$k.txt"
    then pass "panic $k: report and screen ($OUT/panic-$k.png)"
    else fail "panic $k" "expected \"$want\" in $OUT/serial-panic-$k.txt"; fi
  done
fi

# ---- LibreWolf: starts, opens its content processes, nothing crashes for 90 s ----
if [ $LIBREWOLF = 1 ]; then
  echo "LibreWolf"
  lw=""
  for p in Programs/LibreWolf/LibreWolf/librewolf.exe Downloads/librewolf/LibreWolf/librewolf.exe; do [ -f "build/dev/testdata/$p" ] && { lw="/$p"; break; }; done
  if [ -z "$lw" ]; then skip "LibreWolf (put a LibreWolf portable folder in build/dev/testdata, e.g. run getlibrewolf in a test system)"
  else
    tools/dev/mktest.sh "$lw https://example.com/" >>"$OUT/mktest.log" 2>&1
    MEM=6144 boot wait90 shot1; cp build/dev/serial.txt "$OUT/serial-librewolf.txt"; cp build/dev/1.png "$OUT/librewolf.png" 2>/dev/null
    L="$OUT/serial-librewolf.txt"
    if grep -q -- '-contentproc' "$L"; then pass "LibreWolf started its content processes"; else fail "LibreWolf start" "no content process in $L"; fi
    c=$(grep -c 'CPU exception\|Unhandled exception\|\*\*\* PANIC' "$L"); ce=$(grep -c 'channel error' "$L")
    if [ "$c" -eq 0 ] && [ "$ce" -eq 0 ]; then pass "LibreWolf ran 90 s without a crash ($OUT/librewolf.png)"; else fail "LibreWolf stability" "$c crash(es), $ce channel error(s) in $L"; fi
  fi
fi

T=$(( $(date +%s) - START ))
echo
if [ $FAIL -eq 0 ]; then printf "${G}ALL TESTS PASSED${N}: %d passed, %d skipped (%d s). Logs: %s/\n" $PASS $SKIP $T "$OUT"; exit 0
else printf "${R}SOME TESTS FAILED${N}: %d failed, %d passed, %d skipped (%d s). Logs: %s/\n" $FAIL $PASS $SKIP $T "$OUT"; exit 1; fi
