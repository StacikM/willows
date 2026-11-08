#!/bin/sh
# builddll.sh lib.c out.dll -- build a small x64 DLL (no libc). Functions named EXPORT_<name> are exported as <name>; DllMain is the entry point.
set -e
SRC=$1; OUT=$2; D=$(cd "$(dirname "$0")/.." && pwd); T=$(mktemp -d)
F="-m64 -mabi=ms -ffreestanding -fPIE -fvisibility=hidden -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -O1 -nostdinc -isystem $(gcc -print-file-name=include)"
gcc $F -c "$SRC" -o $T/lib.o
python3 -I "$D/tools/mkpe.py" $T/lib.o "$OUT"
rm -rf $T
