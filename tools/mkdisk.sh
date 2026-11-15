#!/bin/sh
# Build a GRUB (BIOS) bootable disk image without root/xorriso: MBR + ext2 partition.
set -e
OUT=${1:-willows.img}; STAGE=$(mktemp -d); mkdir -p build; W=$(mktemp -d -p build)
mkdir -p "$STAGE/boot/grub" "$STAGE/apps"
./tools/gencfg.sh
cp iso/boot/grub/grub.cfg "$STAGE/boot/grub/"; cp kernel.elf "$STAGE/boot/"; python3 tools/patchres.py kernel.elf "$STAGE/boot"; cp apps/*.exe apps/*.dll "$STAGE/apps/"
cp user/*.exe user/*.dll "$STAGE/apps/" 2>/dev/null || true
mkdir -p "$STAGE/files"; cp -r files/. "$STAGE/files/" 2>/dev/null || true
# keep files the user saved in Willows (data area at LBA 122880, outside the ext2 partition) across rebuilds
mkdir -p build
KEEP=$(mktemp -p build 2>/dev/null || mktemp)
[ -f "$OUT" ] && dd if="$OUT" of="$KEEP" bs=512 skip=122880 conv=sparse status=none 2>/dev/null || : > "$KEEP"
rm -f "$OUT"; truncate -s 1084M "$OUT"
echo 'start=2048, size=118784, type=83, bootable' | sfdisk -q "$OUT"     # 58 MB partition, 1 GB data area behind it
mke2fs -q -F -t ext2 -d "$STAGE" -E offset=$((2048*512)) "$OUT" 59392k
[ -s "$KEEP" ] && dd if="$KEEP" of="$OUT" bs=512 seek=122880 conv=notrunc,sparse status=none; rm -f "$KEEP"
grub-mkimage -O i386-pc -o "$W/core.img" -p '(hd0,msdos1)/boot/grub' \
  biosdisk part_msdos ext2 multiboot normal configfile echo ls vbe video video_fb
# Equivalent of grub-bios-setup (which needs root): patch boot.img, embed core.img in the MBR gap.
python3 - "$OUT" "$W/core.img" <<'PY'
import struct, sys
img, core = sys.argv[1], open(sys.argv[2], 'rb').read()
boot = bytearray(open('/usr/lib/grub/i386-pc/boot.img', 'rb').read())
core = bytearray(core.ljust((len(core) + 511) // 512 * 512, b'\0'))
nsect = len(core) // 512
assert nsect < 2040, "core.img too big for MBR gap"
f = open(img, 'r+b')
mbr = f.read(512)
boot[0x1B8:0x200] = mbr[0x1B8:0x200]                    # keep disk signature + partition table
struct.pack_into('<Q', boot, 0x5C, 1)                   # core.img lives at LBA 1
struct.pack_into('<QHH', core, 512 - 12, 2, nsect - 1, 0x820)   # blocklist: rest of core after sector 0
f.seek(0); f.write(boot); f.seek(512); f.write(core); f.close()
print("core.img: %d sectors embedded" % nsect)
PY
rm -rf "$STAGE" "$W"; echo "built $OUT"
