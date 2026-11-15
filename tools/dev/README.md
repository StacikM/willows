# Dev / test tools

- `drive.py` boots `willows.img` in a headless QEMU and sends keys, clicks and screenshots (`build/dev/*.png`). Run it with no arguments for the full step list. Example (build first with `make iso img`):
  `tools/dev/drive.py wait14 ctrl-shift-t wait4 typebrowser ret wait8 ctrl-l typenews.ycombinator.com ret wait25 shot1`
- `post_server.py` is a small echo server for testing POST, `fetch` and `XMLHttpRequest` from the browser (guest URL `http://10.0.2.2:8002/`).
- Test pages live in `files/Documents/` (`jstest.html`, `posttest.html`, `elemtest.html`, `test.js`) and are on the Willows disk under `Documents`. Open one with `browser /documents/jstest.html`.
- `dlltest` (Start menu > Demos & tests) exercises the DLL loader; `jstest "1+2"` runs QuickJS in the terminal.
- `drive.py` always boots a private copy of the image (`build/dev/test.img`), so it never touches your real `willows.img` or its saved files and settings (layout, theme, downloads). The copy starts with an empty data area (US layout); `--keep` carries your saved data into the copy.

## Running big Windows programs (test.img)
- `mktest.sh ["command" ...]` builds `build/dev/test.img` from the current kernel and apps plus the files in `build/dev/testdata/` (a folder that mirrors `C:\`). Each command runs in turn in a terminal at boot (`C:\System\autorun.cfg`); boot it with `drive.py --reuse wait60 shot1`. Output redirected to a file (`apitest > /Documents/t.txt`) can be read back with `datafs.py`.
  Example: `tools/dev/mktest.sh "apitest -v > /Documents/t1.txt" "/Downloads/librewolf/LibreWolf/librewolf.exe"`
- `datafs.py IMAGE [list | extract DIR | settings | pack DIR]` reads the saved-data area of an image (read-only on `willows.img`; `pack` refuses it). Seed `testdata/` with `datafs.py willows.img extract build/dev/testdata`.
- `missing.py app.exe [dll ...]` follows a program's whole DLL tree and lists the Windows functions Willows does not provide yet.
- Debugging in the running system (all output goes to `build/dev/serial.txt`):
  - crash reports name the module and offset (`at xul.dll+353b18`), scan the stack for return addresses, and print Firefox's `gMozCrashReason`;
  - `TRACE=1 mktest.sh ...` records every DLL's Windows calls (and functions found with GetProcAddress); crash reports list the program's last 40;
  - `Ctrl+F12` (drive.py step `ctrl-f12`) dumps every process and thread: what it waits for, where it was called from, its stack;
  - error exits, `RaiseException`, fail-fast and failed `VirtualAlloc` calls are logged with the caller; page 0 is unmapped, so NULL pointers fault.
  - `Ctrl+F12` dumps also list kernel frames as `k:0x...`; `ksym.sh` names them.
- `strnear.py file.dll ADDR...` names a function in a stripped DLL by the strings it uses (assertions, log texts, pref names).
- `gdbwatch.py`: hardware watchpoint on a running system ("who overwrites this?"); `WATCH_PTR` = a pointer variable whose target to watch, or `WATCH_ADDR`. Start QEMU with `QEMU_EXTRA=-s`.
- LibreWolf: `mktest.sh "set MOZ_DISABLE_SOCKET_PROCESS=1 MOZ_DISABLE_CONTENT_SANDBOX=1" "/Downloads/librewolf/LibreWolf/librewolf.exe"`, then
  `MEM=6144 drive.py --reuse "until:CPU exception:200" wait30 at500,101 mb1 wait0.3 mb0 typefile:///C:/Documents/hello.html ret wait25 shot1`
  (it needs more than 3 GB of RAM; Willows uses the memory above 4 GB for programs' pages and images).
- `tools/mkiconfont.py` rebuilds `files/Windows/Fonts/WillowsIcons.ttf`, the caption-button icon font that DirectWrite serves as "Segoe Fluent Icons" / "Segoe MDL2 Assets".
- With `TRACE=1` the serial log also shows pipe traffic (`[pipe]`), completion-port packets (`[iocp]`), cross-thread posted messages (`[msg]`) and top-level window activity (`[wm]`).
