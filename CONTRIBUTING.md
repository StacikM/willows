# Contributing to Willows

Thanks for your interest. Willows is a hobby project: an operating system that runs Windows programs on its own kernel and Win32
layer. Bug reports, fixes, new Windows APIs and tests are all welcome.

## Human and AI work
Willows started as a **100% AI-generated** project: everything in it so far was written by AI (Claude, through Claude Code),
directed and tested by the maintainer. We keep track of who, or what, wrote each change:

- **Written by hand:** say so in your pull request ("written by hand"), and do not add an AI trailer to your commits. Your work
  is credited as human work.
- **Made with AI help** (code, tests or text generated or substantially rewritten by an AI tool): say so in the pull request,
  name the tool, and end each such commit with a trailer, for example
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` or `Assisted-by: <tool name>`.
- Either way, you are responsible for the change: you have read it, you have tested it, and you can explain it.

The README's notice will be updated once human-written code lands, so that it stays accurate.

## Before you start
- For anything larger than a small fix, open an issue first and describe what you want to change, so that work is not done twice.
- Read [docs/INTERNALS.md](docs/INTERNALS.md) for how the parts fit together, and [tools/dev/README.md](tools/dev/README.md) for the
  test and debugging tools. [AGENTS.md](AGENTS.md) has the full conventions; they apply to people too.

## Building and testing
You need Linux with `gcc`, `binutils`, `python3`, the GRUB 2 tools, `sfdisk`, `xorriso` and `qemu-system-x86_64`.

    make -s all                           # build the kernel and the apps
    make img && make run-img MEM=6144     # build a disk image and boot it in QEMU

Before sending a change, run the API test suites on the private test disk (it never touches your own `willows.img`):

    tools/dev/mktest.sh "sleep 8" "apitest > /Documents/t1.txt" "apitest2 > /Documents/t2.txt" \
        "apitest3 > /Documents/t3.txt" "nettest > /Documents/t4.txt" "cxxtest > /Documents/t5.txt"
    tools/dev/drive.py --reuse wait60
    python3 tools/dev/datafs.py build/dev/test.img extract build/dev/out
    tail -1 build/dev/out/Documents/t*.txt

All checks must pass. If you change the desktop or an app, include a screenshot; if you fix a program (PuTTY, LibreWolf, ...),
describe how you tested it.

## Pull requests
- Keep each pull request about one thing, with a clear description: what was broken or missing, what you changed, and how you
  tested it.
- Follow the existing code style (see [AGENTS.md](AGENTS.md#code-conventions)): imitate Windows behaviour exactly where programs
  rely on it, set error codes on failure, and comment *why* a piece of code exists.
- Add a check to `apps/apitest*.c` when you implement new Windows APIs, if they can be tested without a window.
- Do not commit third-party binaries (Windows DLLs, browser builds, programs from `user/`), disk images or build output.

## Reporting bugs
Open an issue with:
- what you ran and what happened (a screenshot helps);
- the crash report or error lines from the serial log (`make run-img` prints it in the terminal; `tools/dev/drive.py` saves it to
  `build/dev/serial.txt`);
- for a Windows program: its name and version, and where to get it.

For a security problem (for example in the TLS code or the certificate checks), please do not post exploit details publicly;
open an issue asking the maintainer for a private contact.

## License
Willows is licensed under the [GNU General Public License, version 3](LICENSE). By contributing, you agree that your
contribution is licensed under the same terms, and you confirm that you have the right to contribute it.
