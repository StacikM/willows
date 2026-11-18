//! The kernel's C functions and data that Rust code uses (declared in src/kernel.h; graphics in src/gfx.c and src/font.c).
//! Everything here is `unsafe` to call: wrap it in safe functions in the module that needs it.
#![allow(non_upper_case_globals, dead_code)]

/// An opaque C `struct font`.
#[repr(C)]
pub struct Font { _private: [u8; 0] }

/// Font styles of `font_find` (src/font.h).
pub const FS_REGULAR: i32 = 0;
pub const FS_BOLD: i32 = 1;
pub const FS_MONO: i32 = 3;

extern "C" {
    pub fn heap_alloc(n: u64) -> *mut u8;
    pub fn heap_free(p: *mut u8);
    /// writes to the screen console and the serial log; `s` is NUL-terminated
    pub fn kputs(s: *const u8);

    pub static gfx_ok: i32;
    pub static gfx_w: u32;
    pub static gfx_h: u32;
    pub fn gfx_rect(x: i32, y: i32, w: i32, h: i32, rgb: u32);
    pub fn gfx_panic_mode();
    pub fn font_find(style: i32, px: i32) -> *const Font;
    pub fn ui_text(f: *const Font, x: i32, y: i32, s: *const u8, n: i32, fg: u32);
    pub fn ui_width(f: *const Font, s: *const u8, n: i32) -> i32;
    pub fn ui_lineh(f: *const Font) -> i32;

    /// the running program's name, or NULL in the kernel's own context (src/panic.c)
    pub fn panic_proc_name() -> *const u8;
    /// "xul.dll+1234" for an address inside a program or DLL; 0 if it is not in one
    pub fn panic_addr_name(a: u64, out: *mut u8, max: i32) -> i32;

    /// a file's contents (in RAM) and size, or NULL
    pub fn panic_file(path: *const u8, size: *mut u64) -> *const u8;

    /// the end of the kernel's code (linker.ld)
    pub static kernel_text_end: u8;
}
extern "win64" {
    /// printf-style output to the screen and the serial log (`%s` wants a NUL-terminated string)
    pub fn kprintf(fmt: *const u8, ...);
    /// printf-style output to the serial log only
    pub fn kdbg(fmt: *const u8, ...);
}

/// Whether an address lies in the kernel's code.
pub fn in_kernel_text(a: u64) -> bool { a >= 0x100000 && a < unsafe { &kernel_text_end as *const u8 as u64 } }
