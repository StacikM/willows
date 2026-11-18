# AGENTS.md

Instructions for AI coding agents working on Willows. Read this before changing anything. People are welcome to read it too;
[CONTRIBUTING.md](CONTRIBUTING.md) is the guide written for them.

Willows is a hobby x86-64 operating system that runs Windows programs (up to LibreWolf/Firefox) on its own kernel and Win32
layer. Background: [README.md](README.md), [docs/INTERNALS.md](docs/INTERNALS.md), [tools/dev/README.md](tools/dev/README.md).

## Rules
1. **Never touch `willows.img`.** It holds the maintainer's saved files and settings (downloads, keyboard layout, browser
   profile). Do not write, rebuild, boot or delete it: no `make img`, `make run-img`, `make run-exe`, `make run-c`, and never
   `make clean` (it deletes the image). Test on the private test disk `build/dev/test.img` with the tools in `tools/dev/`.
   Reading it with `tools/dev/datafs.py willows.img list` is fine.
2. **Keep test and debugging tools in the repository**, in `tools/dev/`, documented in `tools/dev/README.md`. One-off throwaway
   scripts belong in your own scratch space, outside the repository.
3. **Do not commit or push unless asked.** Commits made with AI end with a `Co-Authored-By:` trailer naming the model (see
   [Commits](#commits)). Never rewrite published history.
4. **Never add third-party binaries** to the repository: no LibreWolf/Firefox files, no Microsoft DLLs, nothing from `user/` or
   `build/dev/testdata/`. Willows downloads LibreWolf from its own servers (`getlibrewolf`).
5. **Test before saying something works**, on the test disk, and say plainly what was and was not verified.

## Building
    make -s all          # kernel (kernel.elf / kernel64.elf) and every app in apps/; this does not touch willows.img

The kernel is C (`gcc -m64 -ffreestanding -mgeneral-regs-only -O2`): **no floating point or SSE in kernel code**. It can also
have C++ (`src/*.cpp`, include `src/cxx.h`: no exceptions, RTTI or standard library) and Rust (`src/rust`, `no_std`, target
`x86_64-unknown-none`); see [docs/INTERNALS.md](docs/INTERNALS.md#languages-in-the-kernel). Prefer C for small changes to C files;
use C++ or Rust for new self-contained parts where they make the code simpler or safer (Rust for parsing untrusted data).
Apps are built as Windows PE files by `tools/buildc.sh` (`-mabi=ms`, a small libc in `apps/shim`, imports declared in
`apps/winui.h`); `apps/cxxtest.exe` is built with MinGW-w64.

## Testing
`tools/dev/mktest.sh` builds `build/dev/test.img` from the current build plus `build/dev/testdata/` (a folder that mirrors `C:\`),
with commands that run in a terminal at boot. `tools/dev/drive.py --reuse` boots it headless in QEMU, sends keys and clicks, and
saves screenshots to `build/dev/*.png`; the serial log goes to `build/dev/serial.txt`.

The API test suites (run them before calling a change done):

    tools/dev/mktest.sh "sleep 8" "apitest > /Documents/t1.txt" "apitest2 > /Documents/t2.txt" \
        "apitest3 > /Documents/t3.txt" "nettest > /Documents/t4.txt" "cxxtest > /Documents/t5.txt"
    tools/dev/drive.py --reuse wait60
    python3 tools/dev/datafs.py build/dev/test.img extract build/dev/out
    tail -1 build/dev/out/Documents/t*.txt     # 43/43, 33/33, 35/35, 8/8 checks pass; nettest receives a page

The serial log's boot lines include `Languages: C++ ok, Rust ok` (the kernel's C++ and Rust self-tests). To check a behaviour
against real Windows semantics, build a small program with `x86_64-w64-mingw32-g++ -static` and run it both under Wine on the
host and in Willows.

(`sleep 8` gives DHCP time to finish before `nettest`.)

LibreWolf: put a LibreWolf portable folder in `build/dev/testdata/` (or run `getlibrewolf` inside the test system), then
`tools/dev/mktest.sh "/Programs/LibreWolf/LibreWolf/librewolf.exe https://example.com/"` and
`MEM=6144 tools/dev/drive.py --reuse wait90 shot1`. It needs `MEM=6144`; the default 1 GB fails with "Couldn't load XPCOM".
Each `drive.py` call boots a fresh VM from the image, so put all the steps of one scenario in one call.

## Debugging
- **Crash reports** (serial log) name the module and offset (`at xul.dll+d40f30`), scan the stack for return addresses, dump the
  object `rcx` points at, and print Firefox's crash reason. Kernel addresses appear as `k...`; name them with
  `addr2line -f -e kernel64.elf 0x...` or `tools/dev/ksym.sh`.
- `TRACE=1 tools/dev/mktest.sh ...` logs file, pipe, socket, message and completion-port activity and keeps the last API calls
  for crash reports. Tracing changes timing; a race may disappear under it.
- `ctrl-f12` (a `drive.py` step) dumps every process and thread with what it waits for.
- A **kernel panic** (`*** PANIC:` in the serial log) names kernel functions from `C:\System\kernel.sym`. In kernel code, stop on
  an impossible state with `kernel_panic("...", ...)` rather than halting by hand. `panic c|rust|fault` in the Terminal tests the
  panic path.
- Stripped DLLs: `tools/dev/pefunc.py` (which function contains an address), `tools/dev/strnear.py` (the strings a function
  uses, e.g. assertion texts, which usually name it), `tools/dev/missing.py` (Windows functions a program needs that Willows lacks).
- gdb (start QEMU with `QEMU_EXTRA=-s`): `tools/dev/gdbhits.py` (log every call of a function, cheap enough for races),
  `gdbtrace.py`, `gdbwatch.py` (hardware watchpoints), `gdbrtti.py` (name C++ objects by RTTI).
- Unimplemented APIs are logged as `[soft] unimplemented X called from y.dll+...`; a missing API that a program needs is usually
  the bug.

## Code conventions
- Match the surrounding style: dense C, short helpers, comments that explain *why* at the point where it matters (often the
  Windows behaviour being imitated and which program needs it).
- Windows API implementations are `WINAPI` (ms_abi) functions registered in an export table: `K(...)` in `src/win32.c`, `X(...)`
  in `src/win32x.c`, `YD(...)` in `src/win32y.c`, `T(...)` in `src/thread.c`, and so on. `ntdll` names are registered with an
  explicit `{"ntdll", "Name", fn}` entry.
- Imitate Windows exactly where programs depend on it: return values, `SetLastError` codes, NTSTATUS codes, structure layouts.
  A failure must set an error code (a stale one surfaces later as a confusing error, e.g. "Error 317").
- **Handle numbers are global**, not per process (file handles from `0x5000`, pipes from `0x7B00`, mappings `0x7A00`, kernel objects
  through `ko_get`). Anything a process holds must be owned and released when it exits, and duplicated handles must be counted.
- Kernel code is never preempted and runs in the calling program's thread; anything that waits must use the existing wait
  helpers (`idle_wait`, `thread_note_wait`).
- Log rare failures to the serial log with `kdbg` (prefix like `[pipe!]`); keep per-call logging behind the trace switch.
- Built-in apps use `apps/winui.h` for imports; add an `IMP(...)` line there when an app needs a new function.

## Commits
- Write a short summary line, then what changed and why, with the user-visible effect first.
- End AI-made commits with a trailer naming the model, for example:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
- Commits written entirely by a person carry no AI trailer; that is how human work is told apart (see
  [CONTRIBUTING.md](CONTRIBUTING.md#human-and-ai-work)).
