# Willows internals

How Willows is put together, subsystem by subsystem. For building and running it, see the [README](../README.md); for the test and
debugging tools, see [tools/dev/README.md](../tools/dev/README.md).

## Kernel model
- One address space, ring 0 for everything. Programs and the kernel share the page tables; RAM above 4 GB is mapped at 48 GB and used
  for program pages and DLL images.
- Processes and threads are scheduled cooperatively inside the kernel (they switch when they wait: message loops, `Sleep`, waits on
  objects, I/O). Program code is also preempted by the timer every 10 ms (`timer_preempt` in `src/proc.c`), so a program that spins
  cannot freeze the system. Kernel code is never preempted, so the kernel needs no locks.
- Faults go through an IDT with a TSS; page faults and double faults use their own stack (IST1). CR0.WP is on.
- Stacks are reserved and committed lazily (`vmem_alloc_stack` / `vmem_grow_stack`). Read-only pages of a DLL are shared between the
  processes that load it, with copy-on-write (`pe_cow_fault`).
- Per-process resources (heap blocks, pipes, kernel objects, sockets, GDI objects, environment, window classes, exception handlers)
  are owned by their process and released when it exits.

## Files
- A hierarchical file system rooted at `C:\`, held in RAM. Everything in `files/` is loaded at boot; `Desktop`, `Documents` and
  `Downloads` are the user's folders.
- Changes are saved to a 1 GB data area on the boot disk (ATA/IDE, LBA 122880), folders and deletions included, so they survive
  reboots. `C:\Temp` is never saved, and files over 256 MB stay in RAM only. `make img` keeps the data area when it rebuilds the image.
  ISO/CD boots, AHCI-only SATA, NVMe and USB sticks have no writable disk driver yet, so files stay in RAM there.
- Windows semantics that programs rely on: directory listings return `.` and `..`; a file deleted while open stays readable through
  its open handles; `DuplicateHandle` of a file is a counted reference; names with an alternate data stream (`file:stream`) are
  refused, as on FAT32; `GetDiskFreeSpaceEx` reports the real saved-data space.
- File APIs: `fopen`, `CreateFile`, `FindFirstFile`, `MoveFile(Ex)`, `CopyFile(Ex)`, `SetFileInformationByHandle` (rename, delete,
  size), `GetFileInformationByHandleEx`, `NtReadFile`/`NtWriteFile` (Rust's standard library uses them), the Open/Save dialogs.

## Programs and DLLs
- 64-bit PE32+ programs and DLLs load from the file system (`src/pe.c`, `src/dll.c`): static and delay-load imports, exports by name,
  ordinal and forwarder, relocations, nested dependencies, `DllMain`, implicit TLS. A DLL is found in the program's folder first, then
  the current folder and the system folders. Built-in DLLs (`kernel32`, `user32`, `ucrt`, ...) have small real PE images with export
  tables; a real file is used for names the built-ins do not provide (e.g. a bundled `msvcp140.dll`).
- Unimplemented Windows APIs are reported by name when called; optional ones return a failure instead of stopping the program.
- 32-bit (PE32) programs run in the CPU's compatibility mode (`src/wow.c`): each import is a stub that switches to 64-bit mode, widens
  the stdcall arguments and calls the Win64 function. Stage 1: about 80 kernel32/user32 functions; no 32-bit callbacks, C runtime,
  DLLs or SEH yet. Build a test with `tools/build32.sh prog.c out.exe`.

## Exceptions
Windows x64 structured exception handling, in `src/seh.c` (entry points in `src/boot.S`):
- A CPU fault in a program's own code becomes an `EXCEPTION_RECORD` and `CONTEXT` on the thread's stack (`veh_redirect` in
  `src/idt.c`); `RaiseException` captures its caller's registers. Both go to the vectored handlers (`AddVectoredExceptionHandler`)
  first, then up the program's frames to each frame's language handler, then to the `SetUnhandledExceptionFilter` filter.
  "Continue execution" resumes from the possibly changed `CONTEXT` (`ctx_restore`, SSE state included); an unhandled fault ends
  the program with a crash report.
- `RtlLookupFunctionEntry` searches the module's `.pdata` and the tables added at run time (`RtlAddFunctionTable`,
  `RtlAddGrowableFunctionTable` for JITs); `RtlVirtualUnwind` runs the unwind codes (prolog, epilog, chained entries; the logic
  follows Wine). `RtlUnwindEx` calls the unwind handlers up to the target frame and resumes there; `RtlRestoreContext` handles
  `STATUS_UNWIND_CONSOLIDATE` (MSVC runs catch blocks this way) and `longjmp`. `__C_specific_handler` implements
  `__try`/`__except`/`__finally`.
- So C++ `throw`/`catch` works for MinGW-built programs (tested by `cxxtest`) and should for MSVC-built ones (their
  `vcruntime140.dll` brings `__CxxFrameHandler`; not tested with a real MSVC program yet).
- Handlers are program code called from Willows' C code, which has no unwind tables. Each thread keeps a list of boundary records
  (in `TEB->NtTib.ExceptionList`, unused on x64): a stack walk that reaches Willows code continues from the context its record
  names, as Windows does with its dispatcher frames. Limit: exceptions do not cross Windows API callbacks (a C++ exception thrown in
  a window procedure does not reach a `catch` around the message loop).

## Windows, GDI and dialogs
- `src/wm.c` is the window manager: message queues per thread, window classes and atoms, `SetWindowPos` with the
  `WM_WINDOWPOSCHANGING/CHANGED` sequence, `WM_NCCALCSIZE`/`WM_NCHITTEST` (programs that draw their own title bar, like Firefox, get
  no Willows frame), owned popups kept above their owner, right-clicks (`WM_RBUTTONDOWN/UP`, `WM_CONTEXTMENU`), maximize/minimize.
- Dialogs (`src/dialog.inc`, `src/dialog2.inc`): dialog templates from `.rsrc`, the standard controls (buttons, check/radio, group
  boxes, labels, edits, list and combo boxes, a tree view), Tab/Enter/Esc, modal dialogs.
- GDI (`src/gdi.inc`, `src/gdi2.inc`): per-DC state, memory DCs, DIB sections, pens/brushes/fonts, `ExtTextOut`, `DrawText`,
  `BitBlt`, `AlphaBlend`, `TransparentBlt`, `GradientFill`, shapes (`Ellipse`, `RoundRect`, `Polygon`, `Arc`/`Pie`, Béziers),
  rectangle regions and clipping. Not done: paths, complex regions, viewport transforms, `MaskBlt`.
- The common file dialogs exist both as `GetOpenFileName`/`GetSaveFileName` and as COM objects (`CoCreateInstance` of
  `FileOpenDialog`/`FileSaveDialog`, with `IShellItem`), all drawn by Willows' own dialog.
- Text goes through one font API (`ui_text`, `ui_width`; `src/font.c`); `src/fontdata.c` holds Open Sans and Hack, anti-aliased.
  `src/dwrite.c` serves DirectWrite to programs that use it. Regenerate the glyph bitmaps with
  `gcc -O2 -o /tmp/mkfont tools/host/mkfont.c -Itools/host -lm && /tmp/mkfont > src/fontdata.c`.

## Desktop and apps
- The desktop is 1280x720 by default; the GRUB menu has one entry per resolution (`tools/patchres.py` patches the Multiboot header).
- The taskbar is a floating dock: Start button, one button per window (with the program's icon), network and clock. The Start menu
  lists the built-in programs, installed programs from `C:\System\shortcuts.cfg` (`Name|C:\path\program.exe` per line), "Get
  LibreWolf" while it is not installed, and Restart / Shut down.
- The desktop shows the `Desktop` folder as icons, with a right-click menu (new folder, new text document, open terminal, rename,
  delete).
- Built-in apps (all ordinary Windows programs in `apps/`): **Files** (file manager: path bar, search, list/grid views, sorting,
  context menu), **Terminal** (shell with pipes, redirection, history, Tab completion; `help` lists the commands), **Notes**,
  **Settings** (system info, keyboard layout US/AZERTY, wallpaper themes; saved to `System\settings.cfg`), **Browser** (Willows'
  own web engine, below), plus demos and tests under "Demos & tests".
- `getlibrewolf` (Terminal, or Start → Get LibreWolf) downloads the newest official LibreWolf portable build, checks its published
  SHA-256, unpacks it to `C:\Programs\LibreWolf` and adds a shortcut.

## Networking and HTTPS
- An Intel e1000 driver (QEMU's default NIC) and an IPv4 stack: ARP, ICMP, UDP, DHCP, DNS, TCP (client and server, sliding window
  with retransmission, non-blocking connect, loopback connections).
- Winsock 2 (`ws2_32`, plus `wsock32` with its own ordinals), `select`, `getaddrinfo`, the ICMP API and the iphlpapi queries that
  browsers make. The taskbar shows the IP address. In QEMU the host is 10.0.2.2.
- TLS 1.2 through [BearSSL](https://bearssl.org) (`third_party/bearssl`) with Mozilla's root certificates built in
  (`src/trust_anchors.h`; `tools/mkcerts.sh` regenerates it). Certificates are verified: chain, host name and dates. Programs get it
  through WinINet (`InternetOpenUrl`, `InternetReadFile`, `HttpQueryInfo`; redirects, chunked) and URLMon. Programs that bring their
  own TLS (Firefox) use `BCryptGenRandom` and friends.
- Not supported: TLS 1.3 in the built-in client, IPv6, other network cards, IP fragments, DHCP renewal.

## Willows' own browser
`apps/browser.c` with its engine in `apps/web/` (plain C, no OS calls, so it can be tested on the host): an HTML parser, CSS (cascade,
`@media`, `var()`, `calc()`, grid, flex, floats, tables), PNG/JPEG/GIF decoders, forms, history, and JavaScript through QuickJS-ng
(`apps/qjs`) with a DOM, events, timers, `fetch`/`XMLHttpRequest`, `URL` and storage built in `apps/web/prelude.js`. Not supported:
ES modules, Web Workers, canvas, media, iframes.

## Win32 coverage, by area
- **Registry** (`src/registry.c`): an in-memory hive behind the `Reg*` and native `Nt*Key` functions, seeded with the values programs
  usually read. Not saved between boots.
- **Synchronization** (`src/thread.c`, `src/win32y.c`): threads with their own stack, TEB, TLS and FLS; events, mutexes, semaphores,
  waitable timers, I/O completion ports, critical sections, SRW locks, condition variables, `InitOnce`, `WaitOnAddress`, APCs,
  `RegisterWaitForSingleObject`.
- **Virtual memory** (`src/vmem.c`): `VirtualAlloc`/`Free`/`Query`/`Protect` and the `Nt*` versions, reserve/commit at any address.
- **IPC** (`src/ipc.c`): anonymous and named pipes with overlapped I/O and completion ports, `CreateProcess` with redirected
  handles, file mappings shared between processes.
- **C runtime** (`src/ucrt.c`, `src/crtfloat.c`): stdio on Win32 handles, printf/scanf with floating point and wide strings,
  environment, locale, per-thread `errno`; musl's math library.
- **COM/OLE and shell** (`src/win32y.c`): `CoInitializeEx`, `CoTaskMem*`, GUIDs, BSTRs, variants, the file dialog objects,
  `SHGetKnownFolderPath`, `ShellExecute(Ex)`, `CommandLineToArgvW`, shlwapi paths.
- **Security** (`src/security.c`): real SIDs, ACLs, security descriptors and token queries; no access checks are enforced.
- **System queries** (`src/nt.c`): `NtQuerySystemInformation`, `NtQueryInformationProcess/Token`, `NtQueryObject`, version queries.

## Test programs
`apitest`, `apitest2`, `apitest3` (Start → Demos & tests; `-v` lists every check) cover batches of the Win32 layer: 43, 33 and 35
checks. `cxxtest` (built with MinGW-w64 when it is installed) checks C++ exceptions: 8 checks. `nettest` checks HTTP, `dlltest`
the DLL loader, `jstest "1+2"` QuickJS.

## Languages in the kernel
The kernel is mostly C, and can also have C++ and Rust code:
- **C++**: `.cpp` files in `src/` that include `src/cxx.h`. Freestanding (no exceptions, RTTI or standard library); classes,
  templates, virtual functions, lambdas and `new`/`delete` on the kernel heap work, and global constructors run at boot
  (`src/cxxrt.cpp`, the `.init_array` section in `linker.ld`). `kern::vec` and `kern::unique` stand in for `std::vector` and
  `std::unique_ptr`.
- **Rust**: the `no_std` crate in `src/rust` is built for `x86_64-unknown-none` and linked in as a static library. `Box`, `Vec`
  and `String` use the kernel heap; a panic stops the kernel with its message. Export functions with
  `#[no_mangle] pub extern "C" fn`, declare them in `src/kernel.h`. Without the Rust toolchain, `src/rust/none.c` stands in.
- Both follow the kernel's rules: no floating point or SSE, never block except through the kernel's wait helpers. The boot log
  (serial) checks both with a self-test: `Languages: C++ ok, Rust ok`.

## Kernel panics
`kernel_panic(fmt, ...)` (printf-style, `src/panic.c`) stops the kernel. The panic path itself is Rust (`src/rust/src/panic.rs`):
it disables interrupts, writes a report to the serial log and draws a panic screen (or prints the report on the text console
when there is no framebuffer), then halts. The report names the reason, the program that was running, the fault and registers
for a CPU fault, and the code addresses found on the stack: program addresses as `xul.dll+1234`, kernel addresses with their
function name from `C:\System\kernel.sym` (written by the Makefile from the kernel's symbols, `src/rust/src/symbols.rs`).
It is reached from C, from a CPU fault in the kernel's own context (a fault in a program, even inside a Windows API call, ends
only that program), from a fatal C++ error (`cxx_fatal`) and from Rust's `panic!`. It uses no heap, and a panic while panicking
halts at once. Rust code prints with `kprint!`, `kprintln!` and `kdbg!` (`src/rust/src/log.rs`).
Test it with `panic c`, `panic rust` or `panic fault` in the Terminal (this really halts the machine). `tools/dev/mktest.sh` runs them headless (see
[AGENTS.md](../AGENTS.md#testing)).
