#!/bin/sh
# Generate iso/boot/grub/grub.cfg. Every entry loads ALL programs (user/*.exe, apps/*.exe) and files (files/*) as modules;
# the programs show up in the Start menu. "autostart=" entries additionally launch one program at boot.
OUT=iso/boot/grub/grub.cfg
MODS=""
for f in user/*.dll; do [ -f "$f" ] && MODS="$MODS    module /apps/$(basename $f) $(basename $f)\n"; done
for f in user/*.exe; do [ -f "$f" ] && MODS="$MODS    module /apps/$(basename $f) $(basename $f)\n"; done
for f in apps/*.dll; do [ -f "$f" ] && MODS="$MODS    module /apps/$(basename $f) $(basename $f)\n"; done
for f in apps/*.exe; do [ -f "$f" ] || continue; b=$(basename $f); tag=" demo"; { [ "$b" = "browser.exe" ] || [ "$b" = "terminal.exe" ] || [ "$b" = "files.exe" ] || [ "$b" = "notes.exe" ] || [ "$b" = "settings.exe" ]; } && tag=""; MODS="$MODS    module /apps/$b $b$tag\n"; done
for f in $(cd files 2>/dev/null && find . -type f | sed "s|^\./||" | sort); do MODS="$MODS    module /files/$f $f\n"; done
{
echo "set timeout=5"; echo "set default=0"
for v in "1280x720:kernel.elf" "1920x1080:kernel-1080p.elf" "1600x900:kernel-900p.elf" "1024x768:kernel-768.elf"; do
  res=${v%%:*}; k=${v##*:}
  echo "menuentry \"Willows desktop ($res)\" {"; echo "    multiboot /boot/$k"; printf "$MODS"; echo "}"
done
for f in user/*.exe; do [ -f "$f" ] || continue; b=$(basename $f)
  echo "menuentry \"Willows desktop + start $b\" {"; echo "    multiboot /boot/kernel.elf autostart=$b"; printf "$MODS"; echo "}"
done
} > $OUT
