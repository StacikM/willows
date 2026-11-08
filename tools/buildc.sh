#!/bin/sh
# buildc.sh prog.c [out.exe]  -- compile an ordinary C program into a Willows-runnable PE .exe
# Uses a tiny libc shim (apps/shim): stdio/stdlib/string/ctype basics. No floats/%f, no files, no threads yet.
set -e
SRC=$1; OUT=${2:-user/$(basename "${SRC%.c}").exe}
[ -f "$SRC" ] || { echo "usage: $0 prog.c [out.exe]"; exit 1; }
D=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
F="-m64 -mabi=ms -ffreestanding -fPIE -fvisibility=hidden -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -O1 -nostdinc -isystem $(gcc -print-file-name=include) -I$D/apps/shim"
gcc $F -c "$SRC" -o $T/prog.o
gcc $F -c "$D/apps/shim/shim.c" -o $T/shim.o
ld -m elf_x86_64 -r $T/prog.o $T/shim.o -o $T/all.o
mkdir -p "$(dirname "$OUT")"
python3 -I "$D/tools/mkpe.py" $T/all.o "$OUT"
rm -rf $T
