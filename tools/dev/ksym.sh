#!/bin/sh
# Name the k:0x... kernel addresses in a Ctrl+F12 thread dump:   tools/dev/ksym.sh [build/dev/serial.txt]
cd "$(dirname "$0")/../.."
F=${1:-build/dev/serial.txt}
grep -o "k:0x[0-9a-f]*" "$F" | sort -u | sed 's/k://' | while read a; do
  printf '%s %s\n' "$a" "$(addr2line -f -e kernel64.elf "$a" | head -1)"; done
