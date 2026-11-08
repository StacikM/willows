#!/bin/sh
# buildjs.sh app.c out.exe  -- like buildc.sh, but also links the QuickJS engine (apps/qjs) and its small libc/math layer.
set -e
SRC=$1; OUT=$2; D=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
F="-m64 -mabi=ms -ffreestanding -fPIE -fvisibility=hidden -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -O2 -nostdinc -isystem $(gcc -print-file-name=include) -fno-math-errno -w"
Q="$D/apps/qjs"
# the engine and math library are compiled once and cached in build/
mkdir -p "$D/build"; LIB="$D/build/qjs.o"
if [ ! -f "$LIB" ] || [ -n "$(find "$Q" -newer "$LIB" -type f | head -1)" ]; then
  i=0; for f in "$Q/quickjs.c" "$Q/libregexp.c" "$Q/libunicode.c" "$Q/dtoa.c" "$Q/qjs_libc.c"; do
    gcc $F -DNDEBUG -I"$Q/inc" -I"$Q" -c "$f" -o "$T/e$i.o"; i=$((i+1)); done
  for f in "$Q"/math/*.c; do gcc $F -DNDEBUG -I"$Q/math" -I"$Q/inc" -c "$f" -o "$T/m_$(basename "$f" .c).o"; done
  ld -m elf_x86_64 -r "$T"/e*.o "$T"/m_*.o -o "$LIB"
fi
# apps/web/prelude.js becomes a C string (PRELUDE_JS) for programs that include web/js.h
python3 -I - "$D/apps/web/prelude.js" "$T/prelude_js.h" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
with open(sys.argv[2], "w") as f:
    f.write("static const char PRELUDE_JS[] = {\n" + ",".join(str(b) for b in d) + ",0};\n")
PY
gcc $F -I"$Q/inc" -I"$Q" -I"$D/apps" -I"$T" -c "$SRC" -o "$T/prog.o"
gcc $F -I"$D/apps/shim" -c "$D/apps/shim/shim.c" -o "$T/shim.o"
ld -m elf_x86_64 -r "$T/prog.o" "$T/shim.o" "$LIB" -o "$T/all.o"
mkdir -p "$(dirname "$OUT")"
python3 -I "$D/tools/mkpe.py" "$T/all.o" "$OUT"
rm -rf "$T"
