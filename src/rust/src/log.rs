//! `kprint!` / `kprintln!`: formatted output from Rust code to the console and the serial log, like the C side's `kprintf`.
//! `kdbg!` writes to the serial log only. Both use a small stack buffer and never allocate, so they work in any context.
use core::fmt::{self, Write};

/// Output that goes out in NUL-terminated chunks through one of the kernel's C print functions.
pub struct Chunked { buf: [u8; 128], n: usize, serial_only: bool }
impl Chunked {
    pub const fn new(serial_only: bool) -> Self { Chunked { buf: [0; 128], n: 0, serial_only } }
    pub fn flush(&mut self) {
        if self.n == 0 { return; }
        self.buf[self.n] = 0;
        unsafe {
            if self.serial_only { crate::kernel::kdbg(b"%s\0".as_ptr(), self.buf.as_ptr()); }
            else { crate::kernel::kputs(self.buf.as_ptr()); }
        }
        self.n = 0;
    }
}
impl Write for Chunked {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for &b in s.as_bytes() {
            if b == 0 { continue; }
            if self.n == self.buf.len() - 1 { self.flush(); }
            self.buf[self.n] = b; self.n += 1;
        }
        Ok(())
    }
}
impl Drop for Chunked { fn drop(&mut self) { self.flush(); } }

#[doc(hidden)]
pub fn _print(args: fmt::Arguments, serial_only: bool) { let mut out = Chunked::new(serial_only); let _ = out.write_fmt(args); }

/// Print to the console and the serial log.
#[macro_export]
macro_rules! kprint { ($($a:tt)*) => { $crate::log::_print(format_args!($($a)*), false) }; }
/// Print a line to the console and the serial log.
#[macro_export]
macro_rules! kprintln { () => { $crate::kprint!("\n") }; ($($a:tt)*) => { $crate::log::_print(format_args!("{}\n", format_args!($($a)*)), false) }; }
/// Print a line to the serial log only.
#[macro_export]
macro_rules! kdbg { ($($a:tt)*) => { $crate::log::_print(format_args!("{}\n", format_args!($($a)*)), true) }; }
