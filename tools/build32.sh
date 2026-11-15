#!/bin/sh
# build32.sh prog.c [out.exe] -- compile a freestanding C program into a 32-bit (PE32) Windows .exe for Willows' compatibility mode.
# Imports are declared as  extern T (__attribute__((stdcall)) *__imp_kernel32__Name)(args);  (see apps/hello32.c). No libc.
set -e
SRC=$1; OUT=${2:-user/$(basename "${SRC%.c}").exe}
[ -f "$SRC" ] || { echo "usage: $0 prog.c [out.exe]"; exit 1; }
D=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -O1 -nostdinc -isystem "$(gcc -print-file-name=include)" -c "$SRC" -o "$T/prog.o"
mkdir -p "$(dirname "$OUT")"
python3 -I "$D/tools/mkpe32.py" "$T/prog.o" "$OUT"
rm -rf "$T"
