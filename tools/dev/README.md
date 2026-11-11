# Dev / test tools

- `drive.py` boots `willows.img` in a headless QEMU and sends keys, clicks and screenshots (`build/dev/*.png`). Run it with no arguments for the full step list. Example (build first with `make iso img`):
  `tools/dev/drive.py wait14 ctrl-shift-t wait4 typebrowser ret wait8 ctrl-l typenews.ycombinator.com ret wait25 shot1`
- `post_server.py` is a small echo server for testing POST, `fetch` and `XMLHttpRequest` from the browser (guest URL `http://10.0.2.2:8002/`).
- Test pages live in `files/Documents/` (`jstest.html`, `posttest.html`, `elemtest.html`, `test.js`) and are on the Willows disk under `Documents`. Open one with `browser /documents/jstest.html`.
- `dlltest` (Start menu > Demos & tests) exercises the DLL loader; `jstest "1+2"` runs QuickJS in the terminal.
- Pass `--fresh` to `drive.py` to wipe saved files/settings (the keyboard layout and theme persist across boots and can garble typed text).
