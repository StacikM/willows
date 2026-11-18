# CLAUDE.md

The project rules for every AI agent are in AGENTS.md, imported here:

@AGENTS.md

## Claude Code specifics
- Edit source files with the Edit and Write tools, not with `sed` or scripts run through the shell.
- QEMU runs (`tools/dev/drive.py`, gdb loops) take minutes: run long ones in the background and keep the maintainer informed of
  what is being tested and why, in a sentence or two, instead of going quiet.
- When a test shows something different from what you expected, report it as it is (with the log lines or screenshot), and
  say so when a result is only partly verified.
- Settings saved in `willows.img` (keyboard layout, theme) are the maintainer's: never change them. Test disks use the US layout
  so that scripted typing works.
