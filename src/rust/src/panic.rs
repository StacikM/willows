//! The kernel's panic path: stop the computer with a report when the kernel cannot go on.
//!
//! Reached from C through `kernel_panic(fmt, ...)` (src/panic.c), from a CPU fault in kernel code (src/idt.c), from a fatal
//! C++ error and from Rust's own `panic!`. It disables interrupts, writes the report to the serial log, draws it on a panic screen
//! (or prints it on the text console when there is no framebuffer) and halts. It allocates nothing and uses a static buffer,
//! so it works with a broken heap; a panic during the panic halts at once.
use core::fmt::{self, Write};
use core::sync::atomic::{AtomicBool, Ordering};
use crate::kernel;

/// Registers at the point of the panic (`struct panic_regs` in src/kernel.h).
#[repr(C)]
pub struct PanicRegs {
    pub rip: u64, pub rsp: u64, pub rbp: u64,
    /// rax rcx rdx rbx rsp rbp rsi rdi r8 .. r15, valid when `has_gp` is set (a CPU fault)
    pub gp: [u64; 16],
    pub cr2: u64, pub err: u64, pub vec: u64,
    pub has_gp: u32, pub has_fault: u32,
}

static PANICKING: AtomicBool = AtomicBool::new(false);

fn halt() -> ! { loop { unsafe { core::arch::asm!("cli; hlt", options(nomem, nostack)) } } }

/// A line of text built with `write!`, at most 191 bytes (longer text is cut).
struct Line { buf: [u8; 192], n: usize }
impl Line {
    fn new() -> Self { Line { buf: [0; 192], n: 0 } }
    fn as_str(&self) -> &str { core::str::from_utf8(&self.buf[..self.n]).unwrap_or("?") }
}
impl Write for Line {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for c in s.chars() {                                               // whole characters, line breaks as spaces: one line
            let c = if c == '\n' { ' ' } else { c };
            let mut t = [0u8; 4]; let e = c.encode_utf8(&mut t).as_bytes();
            if self.n + e.len() >= self.buf.len() { break; }
            self.buf[self.n..self.n + e.len()].copy_from_slice(e); self.n += e.len();
        }
        Ok(())
    }
}

/// "k:0x12ab34 wm_main+0x1c" for kernel code (named from C:\System\kernel.sym), "xul.dll+1234" for a program, else the address.
fn place(a: u64, out: &mut Line) {
    if kernel::in_kernel_text(a) {
        let _ = write!(out, "k:{:#x}", a);
        if let Some(s) = crate::symbols::lookup(a) { let _ = write!(out, " {}+{:#x}", s.name, s.offset); }
        return;
    }
    let mut name = [0u8; 64];
    let n = unsafe { kernel::panic_addr_name(a, name.as_mut_ptr(), name.len() as i32) };
    if n != 0 {
        let len = name.iter().position(|&c| c == 0).unwrap_or(0);
        let _ = out.write_str(core::str::from_utf8(&name[..len]).unwrap_or("?"));
    } else { let _ = write!(out, "{:#x}", a); }
}

/// The panic screen: a column of text on a dark background, drawn with the kernel's fonts.
struct Screen { x: i32, y: i32, width: i32, bottom: i32 }
impl Screen {
    const BG: u32 = 0x14182A;
    fn start() -> Option<Screen> {
        unsafe {
            if kernel::gfx_ok == 0 { return None; }
            kernel::gfx_panic_mode();
            let (w, h) = (kernel::gfx_w as i32, kernel::gfx_h as i32);
            kernel::gfx_rect(0, 0, w, h, Self::BG);
            kernel::gfx_rect(0, 0, w, 6, 0xF87171);                       // a red band across the top
            let x = (w / 10).max(24);
            Some(Screen { x, y: h / 9, width: w - 2 * x, bottom: h - 24 })
        }
    }
    /// One paragraph, wrapped at word boundaries to the screen's width.
    fn text(&mut self, style: i32, px: i32, color: u32, s: &str) {
        let f = unsafe { kernel::font_find(style, px) };
        if f.is_null() { return; }
        let lh = unsafe { kernel::ui_lineh(f) };
        let b = s.as_bytes(); let mut start = 0;
        while start < b.len() && self.y + lh <= self.bottom {
            let mut end = b.len(); let mut last_space = None;
            for i in start..b.len() {
                if b[i] == b' ' { last_space = Some(i); }
                if unsafe { kernel::ui_width(f, b[start..].as_ptr(), (i + 1 - start) as i32) } > self.width {
                    end = match last_space { Some(sp) if sp > start => sp, _ => i.max(start + 1) }; break;
                }
            }
            unsafe { kernel::ui_text(f, self.x, self.y, b[start..].as_ptr(), (end - start) as i32, color) }
            self.y += lh; start = end; while start < b.len() && b[start] == b' ' { start += 1; }
        }
    }
    fn gap(&mut self, px: i32) { self.y += px; }
}

/// The report goes to the serial log, and to the screen or the text console.
struct Report { screen: Option<Screen> }
impl Report {
    fn heading(&mut self, s: &str) {
        crate::kdbg!("*** PANIC: {}", s);
        match &mut self.screen { Some(sc) => sc.text(kernel::FS_BOLD, 34, 0xFFFFFF, s), None => crate::kprintln!("\n*** {}", s) }
    }
    fn para(&mut self, s: &str, color: u32) {
        crate::kdbg!("    {}", s);
        match &mut self.screen { Some(sc) => { sc.text(kernel::FS_REGULAR, 17, color, s); sc.gap(6); } None => crate::kprintln!("    {}", s) }
    }
    fn detail(&mut self, s: &str) {
        crate::kdbg!("    {}", s);
        match &mut self.screen { Some(sc) => sc.text(kernel::FS_MONO, 14, 0xCBD5E1, s), None => crate::kprintln!("    {}", s) }
    }
    fn gap(&mut self, px: i32) { if let Some(sc) = &mut self.screen { sc.gap(px) } }
}

const FAULT_NAMES: [&str; 20] = ["divide error", "debug", "NMI", "breakpoint", "overflow", "bound range", "invalid opcode",
    "no FPU", "double fault", "coprocessor segment", "invalid TSS", "segment not present", "stack fault", "general protection",
    "page fault", "reserved", "x87 error", "alignment check", "machine check", "SIMD error"];

/// Stop the kernel. `reason` is one line of text.
pub fn panic_with(reason: &str, r: &PanicRegs) -> ! {
    unsafe { core::arch::asm!("cli", options(nomem, nostack)) }
    if PANICKING.swap(true, Ordering::SeqCst) {
        unsafe { kernel::kputs(b"\n*** kernel panic while reporting a panic; halted\n\0".as_ptr()) }
        halt();
    }
    let mut rep = Report { screen: Screen::start() };
    rep.heading("Willows stopped");
    rep.gap(10);
    rep.para(reason, 0xFDE68A);
    rep.para("The kernel ran into a problem it cannot recover from, so it halted the computer. Files saved before this point \
              are on the disk; anything not saved yet is lost. Restart the computer to continue.", 0x9CA3AF);
    rep.gap(14);

    if r.rip != 0 { let mut l = Line::new(); let _ = l.write_str("Where      "); place(r.rip, &mut l); rep.detail(l.as_str()); }
    let name = unsafe { kernel::panic_proc_name() };
    let mut l = Line::new(); let _ = l.write_str("Program    ");
    if name.is_null() { let _ = l.write_str("none (the kernel itself)"); }
    else { let s = unsafe { core::ffi::CStr::from_ptr(name as *const core::ffi::c_char) }; let _ = l.write_str(s.to_str().unwrap_or("?")); }
    rep.detail(l.as_str());
    if r.has_fault != 0 {
        let what = FAULT_NAMES.get(r.vec as usize).copied().unwrap_or("CPU exception");
        let mut l = Line::new(); let _ = write!(l, "Fault      {} {} ({}), error {:#x}", "vector", r.vec, what, r.err);
        if r.vec == 14 { let _ = write!(l, ", address {:#x}", r.cr2); }
        rep.detail(l.as_str());
    }
    if r.has_gp != 0 {
        const N: [&str; 16] = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"];
        for row in 0..4 {
            let mut l = Line::new(); let _ = l.write_str(if row == 0 { "Registers  " } else { "           " });
            for c in 0..4 { let i = row * 4 + c; let _ = write!(l, "{:<3} {:016x}  ", N[i], r.gp[i]); }
            rep.detail(l.as_str());
        }
    }
    // the stack: words that point into code, nearest first (return addresses, mostly)
    let mut l = Line::new(); let _ = l.write_str("Stack      "); let mut shown = 0;
    if r.rsp >= 0x100000 {
        for i in 0..160u64 {
            let v = unsafe { core::ptr::read_volatile((r.rsp + i * 8) as *const u64) };
            let code = kernel::in_kernel_text(v) || { let mut t = [0u8; 8]; unsafe { kernel::panic_addr_name(v, t.as_mut_ptr(), 8) != 0 } };
            if !code { continue; }
            if shown > 0 && shown % 2 == 0 { rep.detail(l.as_str()); l = Line::new(); let _ = l.write_str("           "); }
            let start = l.n; place(v, &mut l); let _ = l.write_str("  "); while l.n < start + 46 { let _ = l.write_str(" "); } shown += 1;   // two columns
            if shown == 12 { break; }
        }
    }
    if shown == 0 { let _ = l.write_str("(no code addresses found)"); }
    rep.detail(l.as_str());
    rep.gap(14);
    rep.para("The same report is in the serial log. Please include it when you report the problem.", 0x9CA3AF);
    halt();
}

/// The C entry point (src/panic.c, src/idt.c).
#[no_mangle]
pub extern "C" fn kernel_panic_regs(reason: *const core::ffi::c_char, regs: *const PanicRegs) -> ! {
    let reason = if reason.is_null() { "kernel panic" }
                 else { unsafe { core::ffi::CStr::from_ptr(reason) }.to_str().unwrap_or("kernel panic (the reason is not valid text)") };
    let empty = PanicRegs { rip: 0, rsp: 0, rbp: 0, gp: [0; 16], cr2: 0, err: 0, vec: 0, has_gp: 0, has_fault: 0 };
    let r = if regs.is_null() { &empty } else { unsafe { &*regs } };
    panic_with(reason, r)
}

/// Rust's own `panic!` ends here, with the message and source location as the reason.
#[panic_handler]
fn rust_panic(info: &core::panic::PanicInfo) -> ! {
    let mut reason = Line::new(); let _ = write!(reason, "Rust panic: {}", info);
    let mut r = PanicRegs { rip: 0, rsp: 0, rbp: 0, gp: [0; 16], cr2: 0, err: 0, vec: 0, has_gp: 0, has_fault: 0 };
    unsafe { core::arch::asm!("mov {}, rsp", out(reg) r.rsp, options(nomem, nostack)); core::arch::asm!("mov {}, rbp", out(reg) r.rbp, options(nomem, nostack)); }
    panic_with(reason.as_str(), &r)
}
