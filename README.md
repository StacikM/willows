# Willows

**A small x86-64 operating system that runs Windows programs: its own kernel, its own Win32 layer, and enough of both to run
a modern web browser (LibreWolf, a Firefox build) with YouTube, extensions and downloads.**

Willows boots with GRUB, loads 64-bit Windows `.exe` and `.dll` files directly, and gives them the Windows APIs they expect:
windows and messages, GDI, threads and synchronization, files, pipes, Winsock, TLS, COM basics and more. It has its own
desktop (taskbar, Start menu, file manager, terminal, text editor, settings) and its own small web browser.

> [!NOTE]
> **Willows is 100% AI-generated.** Every line of code, every tool and every document in this repository so far was written by
> AI (Anthropic's Claude, working through Claude Code), directed, tested and reviewed by StacikM.
> Contributions written by people are welcome, and are credited as human work; see [CONTRIBUTING.md](CONTRIBUTING.md#human-and-ai-work).

## What works
| Area | Status |
|---|---|
| **LibreWolf / Firefox** | Runs with its usual multi-process setup: HTTPS sites, YouTube pages, extensions (uBlock Origin), downloads with a save dialog, menus and its own title bar. Needs about 6 GB of RAM. Video playback does not work yet. |
| **Classic Win32 programs** | PuTTY's real configuration dialog and terminal; dialog-based and GDI programs; console programs. |
| **Desktop** | Taskbar with program icons, Start menu, desktop icons, file manager with search and list/grid views, terminal with pipes and redirection, Notes, Settings (US and French AZERTY keyboards, wallpaper themes). |
| **Files** | `C:\` file system saved to the boot disk (1 GB), with the Windows behaviour programs rely on. |
| **Network** | Intel e1000, IPv4 (DHCP, DNS, TCP, UDP, ICMP), Winsock 2, TLS 1.2 with verified certificates, WinINet. |
| **Exceptions** | Windows structured exception handling: C++ `throw`/`catch` (tested with MinGW-built programs), vectored handlers, unwinding; `__try`/`__except` and MSVC's C++ handler are implemented but not yet tested with real programs. |
| **Not yet** | Video playback in LibreWolf, 32-bit programs beyond simple ones, Wi-Fi and other network cards, USB/NVMe storage, sound, UEFI. |

How each part works is described in [docs/INTERNALS.md](docs/INTERNALS.md).

## Quick start
You need a Linux machine with `gcc`/`g++`, `binutils`, `python3`, the GRUB 2 tools (`grub-mkimage`, `grub-bios-setup`,
`grub-mkrescue`), `sfdisk`, `xorriso` and `qemu-system-x86_64`. Optional: Rust with the `x86_64-unknown-none` target (for the
kernel's Rust code; `rustup target add x86_64-unknown-none`) and MinGW-w64 (`x86_64-w64-mingw32-g++`, for the C++ exception test).

    make img                  # build willows.img (kernel, apps and a boot disk with a saved-data area)
    make run-img              # boot it in QEMU (3 GB of RAM by default)
    make run-img MEM=6144     # with enough RAM for LibreWolf

Other targets: `make iso` (a BIOS-bootable `willows.iso`), `make run-c SRC=examples/primes.c` (build and run a plain C file),
`make run-exe EXE=path/to/prog.exe` (boot with your own program; anything in `user/` appears in the Start menu).
`python3 tools/peinfo.py prog.exe` shows which of a program's imports Willows implements.

`make img` keeps the saved files of an existing `willows.img`; delete the image to start from scratch. Willows can also boot
from a USB stick or CD, but only ATA/IDE disks are writable so far, so files stay in RAM there.

## Getting LibreWolf
Open **Start → Get LibreWolf**, or type `getlibrewolf` in the Terminal. Willows downloads the newest official portable build
from [librewolf.net](https://librewolf.net), checks it against the published SHA-256, unpacks it to `C:\Programs\LibreWolf`
(about 510 MB) and adds it to the Start menu. Run it again to update. Willows does not include LibreWolf itself; LibreWolf is
distributed by its own project under its own license.

## Repository layout
| Path | What it is |
|---|---|
| `src/` | The kernel: boot, memory, scheduler, file system, drivers, network stack, window manager, and the Win32 layer (`win32*.c`, `thread.c`, `ipc.c`, `seh.c`, `ucrt.c`, `gdi*.inc`, ...). Mostly C, with C++ (`*.cpp`) and Rust (`src/rust`) support. |
| `apps/` | Built-in programs (all ordinary Windows programs): file manager, terminal, notes, settings, browser and its engine (`apps/web`), QuickJS (`apps/qjs`), tests. |
| `files/` | Files copied onto the disk at boot (`C:\Documents`, fonts in `C:\Windows\Fonts`). |
| `tools/` | Build scripts (disk image, PE writer, C/DLL builders) and `tools/dev/`, the test and debugging tools. |
| `third_party/` | BearSSL. |
| `docs/` | Documentation. |

## Development
- [docs/INTERNALS.md](docs/INTERNALS.md) explains the kernel and each part of the Win32 layer.
- [tools/dev/README.md](tools/dev/README.md) describes the headless test setup (a private test disk, scripted QEMU sessions,
  screenshots) and the debugging tools: crash reports, API traces, thread dumps, gdb scripts.
- [CONTRIBUTING.md](CONTRIBUTING.md) covers how to contribute and how human and AI work are labelled.
- [AGENTS.md](AGENTS.md) holds the rules for AI coding agents working on this repository.

## License
Willows is free software under the [GNU General Public License, version 3](LICENSE). You may use, study, change and share it;
anything you distribute that is built from it must be released under the same license, with its source.

Third-party parts keep their own licenses: BearSSL (`third_party/bearssl/LICENSE.txt`, MIT), QuickJS-ng (`apps/qjs/LICENSE`, MIT),
the Open Sans and Hack fonts (`files/Windows/Fonts/LICENSE-*.txt`), musl's math functions (`apps/qjs/math/COPYRIGHT`, MIT). LibreWolf, Firefox and
other Windows programs mentioned here belong to their own projects.
