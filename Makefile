CC      = gcc
CFLAGS  = -m64 -ffreestanding -fno-pie -fno-pic -mcmodel=small -mno-red-zone -fno-stack-protector -mgeneral-regs-only -O2 -Wall -Wextra -nostdinc -fno-builtin
APPFLAGS= -m64 -mabi=ms -ffreestanding -fPIE -fvisibility=hidden -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -O1 -Wall
SRC     = $(wildcard src/*.c)
# Kernel C++ (src/*.cpp, include src/cxx.h): freestanding, no exceptions/RTTI/standard library, no SSE - see src/cxxrt.cpp
CXX     = g++
CXXSRC  = $(wildcard src/*.cpp)
CXXFLAGS= -m64 -ffreestanding -fno-pie -fno-pic -mcmodel=small -mno-red-zone -fno-stack-protector -mgeneral-regs-only -O2 -Wall -Wextra \
          -nostdinc -nostdinc++ -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables -std=gnu++20
BRSRC   = $(shell find third_party/bearssl/src -name '*.c')
BROBJ   = $(BRSRC:.c=.o)
BRFLAGS = -m64 -ffreestanding -fno-pie -fno-pic -mcmodel=small -mno-red-zone -fno-stack-protector -mgeneral-regs-only -O2 -nostdinc \
          -isystem $(shell gcc -print-file-name=include) -Ithird_party/shim -Ithird_party/bearssl/inc -Ithird_party/bearssl/src \
          -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_RDRAND=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0
# musl's math functions (apps/qjs/math) also serve as the C runtime's math library: built with the Microsoft ABI and SSE
KMSRC   = $(wildcard apps/qjs/math/*.c)
KMOBJ   = $(patsubst apps/qjs/math/%.c,build/kmath/%.o,$(KMSRC))
KMFLAGS = -m64 -mabi=ms -ffreestanding -fno-pie -fno-pic -mcmodel=small -mno-red-zone -fno-stack-protector -O2 -nostdinc \
          -isystem $(shell gcc -print-file-name=include) -fno-math-errno -msse2 -w
# Kernel Rust (src/rust, a no_std static library): built when cargo and the x86_64-unknown-none target are installed
# (rustup target add x86_64-unknown-none); otherwise src/rust/none.c stands in and the kernel has no Rust code.
RUST_OK := $(shell cargo --version >/dev/null 2>&1 && rustup target list --installed 2>/dev/null | grep -qx x86_64-unknown-none && echo 1)
RUSTSRC = $(wildcard src/rust/src/*.rs src/rust/src/*/*.rs) src/rust/Cargo.toml
ifeq ($(RUST_OK),1)
RUSTLIB = build/rust/x86_64-unknown-none/release/libwillows_rs.a
else
RUSTLIB = src/rust/none.o
endif
OBJ     = $(SRC:.c=.o) $(CXXSRC:.cpp=.o) src/boot.o $(BROBJ) $(KMOBJ) $(RUSTLIB)
# Guest RAM in MB for every run target (the kernel uses up to 3 GB):  make run-img MEM=2048
MEM     ?= 3072
# Use hardware virtualisation (KVM) when the host allows it: far faster than QEMU's software CPU emulation.
ACCEL   := $(shell test -w /dev/kvm && echo "-enable-kvm -cpu host" || echo "-cpu max")
DLLS    = apps/wdep.dll apps/wlib.dll apps/wdyn.dll
APPS    = apps/hello32.exe apps/apitest.exe apps/apitest2.exe apps/apitest3.exe apps/dlltest.exe $(DLLS) apps/dlgtest.exe apps/hello.exe apps/exit42.exe apps/echo.exe apps/gfx.exe apps/crt.exe apps/paint.exe apps/ping.exe apps/nettest.exe apps/httpd.exe apps/fetch.exe apps/browser.exe apps/terminal.exe apps/notes.exe apps/files.exe apps/settings.exe apps/jstest.exe
# cxxtest: C++ exceptions through Windows SEH, built with MinGW-w64 when it is installed (a real compiler-built Windows program)
ifneq ($(shell command -v x86_64-w64-mingw32-g++ 2>/dev/null),)
APPS   += apps/cxxtest.exe
endif

all: kernel.elf files/System/kernel.sym $(APPS)
kernel.elf: $(OBJ) linker.ld
	ld -m elf_x86_64 -z max-page-size=4096 -T linker.ld -o kernel64.elf $(OBJ)
	objcopy -O elf32-i386 kernel64.elf $@   # GRUB's Multiboot 1 loader wants a 32-bit ELF
# the kernel's function names, sorted by address ("address name" per line): C:\System\kernel.sym, read by the panic screen
files/System/kernel.sym: kernel.elf
	mkdir -p files/System && nm -n -C --defined-only kernel64.elf | awk '$$2 ~ /^[tT]$$/ { a=$$1; sub(/^[^ ]+ [^ ]+ /, ""); print a, $$0 }' > $@   # (-C: readable C++ and Rust names, which may contain spaces)
src/%.o: src/%.c src/kernel.h
	$(CC) $(CFLAGS) -c $< -o $@
src/%.o: src/%.cpp src/kernel.h src/cxx.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
build/rust/x86_64-unknown-none/release/libwillows_rs.a: $(RUSTSRC)
	cd src/rust && CARGO_TARGET_DIR=../../build/rust RUSTFLAGS="-C code-model=small -C relocation-model=static" cargo build -q --release --target x86_64-unknown-none
src/rust/none.o: src/rust/none.c
	$(CC) $(CFLAGS) -c $< -o $@
# BearSSL (third_party/): freestanding build without CPU-specific intrinsics
$(BROBJ): %.o: %.c
	$(CC) $(BRFLAGS) -c $< -o $@
# the TLS/HTTP glue talks to BearSSL, so it needs its headers
src/tls.o src/inet.o: src/%.o: src/%.c src/kernel.h
	$(CC) $(CFLAGS) -isystem $(shell gcc -print-file-name=include) -Ithird_party/shim -Ithird_party/bearssl/inc -c $< -o $@
src/boot.o: src/boot.S
	$(CC) -m64 -c $< -o $@
apps/%.o: apps/%.c apps/win.h
	$(CC) $(APPFLAGS) -c $< -o $@
apps/%.exe: apps/%.o tools/mkpe.py
	python3 tools/mkpe.py $< $@
# programs that use the libc shim (stdio/stdlib/string) instead of apps/win.h
apps/terminal.exe apps/notes.exe apps/files.exe apps/settings.exe apps/dlgtest.exe apps/dlltest.exe apps/apitest.exe apps/apitest2.exe apps/apitest3.exe: apps/%.exe: apps/%.c apps/winui.h $(wildcard apps/web/*.h) apps/shim/shim.c tools/buildc.sh tools/mkpe.py
	./tools/buildc.sh apps/$*.c $@

# programs that embed the QuickJS engine (the browser also embeds apps/web/prelude.js, its DOM layer)
apps/jstest.exe apps/browser.exe: apps/%.exe: apps/%.c apps/winui.h apps/web/prelude.js $(wildcard apps/web/*.h) $(wildcard apps/qjs/*.[ch] apps/qjs/*/*.[ch]) tools/buildjs.sh tools/mkpe.py
	./tools/buildjs.sh apps/$*.c $@

apps/cxxtest.exe: apps/cxxtest.cpp
	x86_64-w64-mingw32-g++ -O2 -static -s -o $@ $<

# 32-bit (PE32) programs, run by Willows in compatibility mode
apps/hello32.exe: apps/hello32.c tools/build32.sh tools/mkpe32.py
	./tools/build32.sh $< $@

# DLLs (functions named EXPORT_<name> are exported); wlib imports wdep, so wdep is built first
apps/%.dll: apps/dll/%.c tools/builddll.sh tools/mkpe.py
	./tools/builddll.sh $< $@
apps/wlib.dll: apps/wdep.dll
apps/dlltest.exe: $(DLLS)

# fast test without GRUB (QEMU's built-in multiboot loader)
run: all
	qemu-system-x86_64 $(ACCEL) -m $(MEM) -nic user,model=e1000,hostfwd=tcp::18080-:8080 -kernel kernel.elf -initrd "apps/exit42.exe,apps/hello.exe,apps/crt.exe" -serial stdio -display none -no-reboot
run-gui: all
	qemu-system-x86_64 $(ACCEL) -m $(MEM) -nic user,model=e1000,hostfwd=tcp::18080-:8080 -kernel kernel.elf -initrd "apps/exit42.exe,apps/hello.exe,apps/crt.exe" -serial stdio

img: all
	./tools/mkdisk.sh willows.img
run-img: img
	qemu-system-x86_64 $(ACCEL) -m $(MEM) -nic user,model=e1000,hostfwd=tcp::18080-:8080 -drive file=willows.img,format=raw -serial stdio

# BIOS-only ISO (UEFI needs mtools for grub-mkrescue)
iso: all
	./tools/gencfg.sh
	rm -rf iso/apps && mkdir -p iso/apps && cp kernel.elf iso/boot/ && python3 tools/patchres.py kernel.elf iso/boot && cp $(APPS) iso/apps/ && (cp user/*.exe user/*.dll iso/apps/ 2>/dev/null || true)
	rm -rf iso/files && mkdir -p iso/files && cp -r files/. iso/files/
	grub-mkrescue -d /usr/lib/grub/i386-pc -o willows.iso iso
run-iso: iso
	qemu-system-x86_64 $(ACCEL) -m $(MEM) -nic user,model=e1000,hostfwd=tcp::18080-:8080 -cdrom willows.iso -serial stdio
# Test your own program:  make run-exe EXE=path/to/prog.exe   (copies it into user/, builds, boots in QEMU)
run-exe: all
	@test -n "$(EXE)" || { echo "usage: make run-exe EXE=path/to/prog.exe"; exit 1; }
	cp "$(EXE)" user/
	$(MAKE) img
	qemu-system-x86_64 $(ACCEL) -m $(MEM) -nic user,model=e1000,hostfwd=tcp::18080-:8080 -drive file=willows.img,format=raw -serial stdio
# Test a plain C program:  make run-c SRC=mine.c   (compiled against apps/shim, a tiny libc)
run-c: all
	@test -n "$(SRC)" || { echo "usage: make run-c SRC=prog.c"; exit 1; }
	./tools/buildc.sh "$(SRC)"
	$(MAKE) img
	qemu-system-x86_64 $(ACCEL) -m $(MEM) -nic user,model=e1000,hostfwd=tcp::18080-:8080 -drive file=willows.img,format=raw -serial stdio
clean:
	rm -f kernel64.elf src/*.o $(BROBJ) apps/*.o apps/*.exe kernel.elf willows.iso willows.img; rm -rf build

# wm.c includes the dialog/GDI files
src/wm.o: $(wildcard src/*.inc)

build/kmath/%.o: apps/qjs/math/%.c
	@mkdir -p build/kmath
	$(CC) $(KMFLAGS) -DNDEBUG -Iapps/qjs/math -Iapps/qjs/inc -c $< -o $@
src/crtfloat.o: src/crtfloat.c
	$(CC) $(KMFLAGS) -Iapps/qjs/inc -c $< -o $@

# the C runtime formats and parses doubles, so it is built with SSE too
src/ucrt.o: src/ucrt.c src/kernel.h
	$(CC) $(filter-out -mgeneral-regs-only,$(CFLAGS)) -msse2 -c $< -o $@
# DirectWrite rasterises glyphs (stb_truetype) with floating point
src/dwrite.o: src/dwrite.c src/kernel.h tools/host/stb_truetype.h
	$(CC) $(filter-out -mgeneral-regs-only,$(CFLAGS)) -msse2 -c $< -o $@

# floating-point CRT: the only kernel file built with SSE
src/crtmath.o: src/crtmath.c src/kernel.h
	$(CC) $(filter-out -mgeneral-regs-only,$(CFLAGS)) -msse2 -c $< -o $@
