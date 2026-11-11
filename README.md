# Willows

A tiny x86-64 OS (GRUB + Multiboot) that loads and runs 64-bit Windows PE32+ `.exe` files on its own Win32/msvcrt layer,
with a small window manager (user32: windows, messages, EDIT/BUTTON/STATIC controls, open/save dialog) and a RAM filesystem.

## Build / boot
    make img        # willows.img  (dd it to a USB stick, or boot in QEMU)
    make iso        # willows.iso  (BIOS)
    make run-img    # boot willows.img in QEMU

## Test your own program
    make run-c   SRC=examples/primes.c     # plain C file (stdio/stdlib/string/ctype shim, no floats yet)
    make run-exe EXE=path/to/prog.exe      # an .exe you built yourself (e.g. i686-w64-mingw32-gcc)
Both put the exe in `user/`; everything in `user/` shows up in the GRUB menu of `willows.img`/`willows.iso`.
Unimplemented Windows APIs are reported by name when the program calls them (see src/win32.c to add more).

## Files
Willows has a real hierarchical filesystem rooted at `C:\` (RAM-backed). Everything in `files/` is loaded at boot
(`files/Documents/...`); `Desktop/` and `Documents/` are the user's folders. Programs can open, read, create, rename,
delete and list files and folders (`fopen`, `CreateFileA`, `FindFirstFileA`, `CreateDirectoryA`, `MoveFileA`, the
Open/Save dialogs). Changes are written to a 4 MB data area on the boot disk (ATA/IDE; LBA 122880), including
folders and deletions, so they survive reboots. `make img` keeps that area when rebuilding (delete `willows.img` for a
clean slate). ISO/CD boots, AHCI-only SATA, NVMe and USB sticks have no writable disk driver yet, so files stay in RAM there.

## Check a program before running it
    python3 tools/peinfo.py user/prog.exe    # architecture + which imports Willows implements

## Desktop
Boot "Willows desktop" in GRUB: taskbar (Start button, one button per open window, clock) and a Start menu listing every
program (`user/*.exe` and the built-in demos) with Restart and Shut down. Programs run as cooperative processes: they switch
when they wait (message loop, Sleep, keyboard input). Console programs and kernel messages appear in a "Console" window.
GRUB entries named "desktop + start X" launch one of your programs at boot (`autostart=X` on the kernel command line).

Every window has minimize / maximize (programs with a message loop) / close buttons; the taskbar button minimizes the
active window or restores/raises another. Closing a GDI program (`paint`, `gfx`) ends its process.

The desktop shows the contents of the `Desktop` folder as icons: click to select, double-click to open, right-click for
New folder / New text document / Open terminal / Rename / Delete (typing renames in place). Folders open in **Files**
(sidebar, path bar, new folder, rename, delete), text files in **Notes** (Ctrl+S saves), `.html` in the browser and
`.exe` runs. The Start button is the Willows willow mark; the taskbar is a floating dock.

## Dialogs, GDI and classic programs
Willows has a dialog manager (`src/dialog.inc`, `src/dialog2.inc`): resources are read from the program's `.rsrc`, dialog templates become real windows, and the standard
controls exist (buttons, check/radio, group boxes, labels, single-line edits, list boxes, combo boxes, a tree view) with Tab/Enter/Esc handling and modal dialogs.
`src/gdi.inc` is a per-DC GDI (memory DCs and bitmaps, pens/brushes/fonts, `ExtTextOut`, `BitBlt`, lines, rectangles) plus timers, clipboard, menus, keyboard translation
(`ToUnicodeEx`) and `WSAAsyncSelect` sockets. `src/win32x.c` adds ~200 more kernel32/advapi32/COM stubs and wide-string file APIs. With these, PuTTY's real configuration
dialog and terminal window run (Telnet/Raw to a reachable host; SSH is untested). Optional APIs that Willows lacks return 0 instead of stopping the program.

## Networking
Willows has an Intel e1000 driver (QEMU's default NIC; many real boards) and its own IPv4 stack: ARP, ICMP, UDP, DHCP, DNS and TCP
(client and server). Programs use it through Winsock 2 (`ws2_32`: `socket/connect/send/recv/bind/listen/accept/select/
gethostbyname/getaddrinfo` ...) and the ICMP API (`IcmpSendEcho`). The taskbar shows the IP address.
Demos: `ping.exe`, `nettest.exe` (HTTP GET from the host and from example.com), `httpd.exe` (web server on port 8080; with
`make run-img` it is reachable from the host at http://127.0.0.1:18080/). In QEMU the host is 10.0.2.2.
Not supported yet: HTTPS/TLS, IPv6, other NICs (Wi-Fi, Realtek, virtio), IP fragments, DHCP renewal.

## HTTPS
TLS 1.2 client via [BearSSL](https://bearssl.org) (third_party/bearssl, MIT licence), with Mozilla's 118 root certificates built in
(src/trust_anchors.h; `tools/mkcerts.sh` regenerates it). Certificates are really verified: chain to a trusted root, host name and
dates (from the CMOS clock) - expired, self-signed and wrong-host sites are refused. Programs get it through WinINet
(`InternetOpenUrlA/W`, `InternetReadFile`, `HttpQueryInfoA`; http and https, redirects, chunked) and URLMon
(`URLDownloadToFileA/W` saves into the RAM filesystem, and so onto the boot disk). Programs that bring their own TLS library can
use `BCryptGenRandom`, `CryptGenRandom` and `RtlGenRandom`, and `GetSystemTimeAsFileTime` now returns the real date.
Demo: `fetch.exe`. Limits: TLS 1.2 only (no 1.3), no client certificates or revocation checks, root list is frozen at build time,
random numbers come from RDRAND when the CPU has it (otherwise weaker timer jitter, with a warning).

## Look, fonts and the browser
The desktop runs at 1280x720 (16:9; change the numbers in the Multiboot header in src/boot.S). Windows have flat chrome with shadows,
hover effects and dot-style window buttons; the taskbar shows tasks, network and a clock with the date.

Text goes through one font API (`ui_text`, `ui_width`; src/font.c). src/fontdata.c (generated: Open Sans + Hack, anti-aliased) is included; without it the system falls back to the
built-in 8x16 bitmap font. To regenerate the glyph bitmaps (uses stb_truetype, public domain, in tools/host/):

    gcc -O2 -o /tmp/mkfont tools/host/mkfont.c -Itools/host -lm && /tmp/mkfont > src/fontdata.c && make img

`apps/browser.c` is a graphical web browser (a normal Windows program: WM_PAINT, GDI fonts, WinINet). The engine lives in `apps/web/`
and is plain C with no OS calls, so it can be tested on the host:
- `dom.h` HTML parser (implied end tags, entities, quirks detection), `css.h` CSS (selectors incl. attribute/nth/not, specificity cascade,
  `@media`, `var()`, calc of one kind, colours, shorthands), `layout.h` block / inline / float / table / flex layout with margin collapsing,
  `image.h` PNG (all types, Adam7), JPEG (baseline + progressive) and GIF decoders.
- http/https, external stylesheets, images loaded progressively, `data:` and `file:` URLs, forms (text, checkbox, radio, select, buttons; GET),
  history, in-page `#fragment` links, Latin-1 pages. No JavaScript, no SVG, no `<canvas>`/video; CSS grid falls back to normal flow.
- Windows use small coloured dot buttons (minimize / maximize / close) at the right end of the title bar.

## Terminal and screen resolution
`apps/terminal.c` is a terminal emulator with a small shell (Start menu, or Ctrl+Shift+T): ANSI colours, scrollback, history,
Tab completion, pipes (`| grep/head/tail/wc/sort`) and `>` / `>>` redirection. Commands: help ls cat cp mv rm touch echo ping curl wget
nslookup ipconfig ps kill run date uptime free calc neofetch history clear exit reboot shutdown. It is built only from Windows
APIs (FindFirstFile, CreateProcess, IcmpSendEcho, WinINet ...), which Willows now also provides to other programs.

The GRUB menu has one entry per resolution (1280x720, 1920x1080, 1600x900, 1024x768): `tools/patchres.py` writes kernel copies with
a different video request in the Multiboot header. Add resolutions there. Visuals: anti-aliased shapes, vector cursor, rounded
window corners, cached wallpaper, translucent taskbar and a sliding Start menu.

## Settings
The **settings** app (Start menu) has three pages: System (OS, GPU, display, memory, uptime), Keyboard (English US / French AZERTY with AltGr and accented letters, plus a test box) and Appearance (six wallpaper themes). Changes apply immediately and are saved to `System\settings.cfg`, which is loaded at boot. Programs can use the kernel32 exports `WillowsSetSetting`/`WillowsGetSetting` (keys: `layout` = `us`|`azerty`, `theme` = 0-5).

## Real DLL loading
Willows loads PE32+ DLLs from its file system ([src/dll.c](src/dll.c)): `LoadLibrary`/`GetProcAddress`/`FreeLibrary`, static and delay-load imports, export tables (by name, ordinal and forwarders), relocations, nested dependencies and `DllMain` (attach/detach). A DLL is found in the current folder, the root, Downloads, Documents, Desktop, `System`, `System32` or `Windows\System32`. Built-in DLLs (kernel32, user32, ucrt, ...) stay built in; a real file is used for names the built-ins don't provide, e.g. a bundled `msvcp140.dll`. Each process gets its own copy of a DLL. Test: run `dlltest` (uses `wlib.dll` -> `wdep.dll` and `wdyn.dll`, built with `tools/builddll.sh`; export functions as `EXPORT_<name>`).

## JavaScript (QuickJS)
The browser runs page scripts with **QuickJS-ng** (vendored in `apps/qjs`, with a small libc layer and musl's math functions). `apps/web/js.h` exposes node primitives to JavaScript and `apps/web/prelude.js` builds the browser environment on top: `window`, `document`, elements (attributes, classList, style, dataset, innerHTML, querySelector, ...), events with capture/bubble, timers and `requestAnimationFrame`, `fetch`/`XMLHttpRequest` (any method, headers and body), `URL`, storage, `console` (output goes to the program's console/terminal). Scripts run after the first layout; DOM edits restyle and re-layout the page. Click, input, keydown and submit events are delivered to page handlers (`preventDefault()` works). `jstest "1+2"` runs QuickJS directly in the terminal. Not supported yet: ES modules (`type=module`), multipart/file uploads, Web Workers, canvas, media, `<iframe>`, hit-testing clicks on empty boxes (only text, images and controls).

## Win32 coverage, batch 1
- **Registry** (`src/registry.c`): a real in-memory hive for the `Reg*` functions (open/create/query/set/get/enum/delete/info, A and W). It starts with the values programs commonly read (Windows version, computer name, CPU name, folders) and is not saved between boots yet.
- **Locks** (`src/win32y.c`): SRW locks, `InitOnce*`, `WaitOnAddress`, SLists. Willows schedules cooperatively, so these are correct but never block for long.
- **COM/OLE basics**: `CoInitializeEx`, `CoTaskMem*`, GUID strings, `CoCreateGuid`, BSTRs (`Sys*String*`), `VariantInit/Clear`. `CoCreateInstance` reports "class not registered".
- **Shell**: `SHGetKnownFolderPath`, `SHGetFolderPathA/W`, `SHGetSpecialFolderPath*`, `ShellExecuteW/ExA/ExW` (with arguments), `CommandLineToArgvW`, shlwapi path helpers.
- **Processes**: `CreateProcessW`, `GetProcessId`, psapi (`EnumProcesses`, `GetProcessMemoryInfo`, module names), job objects (accepted, not enforced).
- Run `apitest` (Start menu > Demos & tests; `apitest -v` lists every check) to verify: 43 checks.

## Win32 coverage, batch 2
- **Pipes** (`src/ipc.c`): anonymous pipes (`CreatePipe`, `PeekNamedPipe`) and duplex named pipes (`\\.\pipe\name` with `CreateNamedPipe`, `ConnectNamedPipe`, `CreateFile` as the client). Reads block until data or end-of-file. `CreateProcess` with `STARTF_USESTDHANDLES` redirects a child's stdin/stdout through pipes (so `subprocess`-style capture works); references are counted so the reader sees end-of-file when the child exits.
- **File mappings**: `CreateFileMapping`/`OpenFileMapping`/`MapViewOfFile`/`Unmap`/`Flush`. Named mappings are real shared memory between programs (all programs share one address space); file-backed mappings read the file and write changes back on unmap.
- **File helpers**: `LockFileEx`, `GetDiskFreeSpaceExW`, `GetFinalPathNameByHandle`, `SetFileInformationByHandle`, `DeviceIoControl` (fails), `CancelIo`.
- **Native registry** (`src/registry.c`): `NtOpenKey`, `NtCreateKey`, `NtQueryValueKey`, `NtEnumerateKey/ValueKey`, `NtQueryKey`, `NtSetValueKey`, `NtDeleteKey`, `NtClose`, `RtlOpenCurrentUser` on the same in-memory hive.
- **System queries** (`src/nt.c`): `NtQuerySystemInformation` (basic, processor, time of day, per-CPU performance), `NtQueryInformationProcess/Token`, `RtlExpandEnvironmentStrings`, `RtlGetNtVersionNumbers`, `BrandingFormatString`.
- Run `apitest2` to verify: 33 checks (it starts `echo.exe` as a child with redirected stdin/stdout).

## Win32 coverage, batch 3 (GDI)
- **Clipping and regions** (`src/gdi2.inc`): `IntersectClipRect`, `ExcludeClipRect` (only cuts that remove a whole side), `SelectClipRgn`, `GetClipBox`, `SaveDC`/`RestoreDC`. Regions are rectangles (bounding-box arithmetic for `CombineRgn`); the clip applies to fills, lines, text and blits.
- **Shapes**: `Ellipse`, `RoundRect`, `Polygon`/`PolyPolygon` (even-odd fill), `Arc`/`Pie`/`Chord`, `PolyBezier(To)`, `PolylineTo`, `FrameRect`, `InvertRect`, `FillRgn`/`FrameRgn`.
- **Text**: `DrawText(Ex)A/W` (alignment, word-break, single line, `DT_CALCRECT`, `&` prefixes, end ellipsis), `TextOutW`/`GetTextExtentPoint32W` now keep non-ASCII text (UTF-16 to UTF-8), `CreateFontW`/`CreateFontIndirectW`, `EnumFontFamilies(Ex)`, `GetTextFace`.
- **Bitmaps**: `CreateDIBSection` (32-bit top-down shares the pixel buffer; other formats and bottom-up DIBs are converted when blitted), `GetDIBits`, `SetDIBits`, `SetDIBitsToDevice`, `CreateDIBitmap`, `GetBitmapBits`/`SetBitmapBits`.
- **Blending**: `AlphaBlend` (constant and per-pixel alpha), `TransparentBlt`, `GradientFill` (horizontal, vertical, triangles).
- Not done: paths (`BeginPath`...), true complex regions, window/viewport origin translation, `MaskBlt`.
- Run `apitest3` (`-v` lists each check; 29 checks) or `apitest3 gdi` for a window that shows the shapes.
