//! Kernel function names for addresses, from `C:\System\kernel.sym`.
//!
//! The Makefile writes that file from the kernel's ELF symbols ("0000000000123abc name" per line, sorted by address); the boot
//! loader brings it along like any file in `files/`. A lookup is a scan of plain text, so it needs no allocation and survives a
//! broken heap. Text that does not parse is skipped, so a damaged or stale file only costs names, never a crash.

/// The function that contains `addr`, and the offset into it.
pub struct Symbol<'a> { pub name: &'a str, pub offset: u64 }

fn table() -> &'static [u8] {
    let mut size = 0u64;
    let p = unsafe { crate::kernel::panic_file(b"/System/kernel.sym\0".as_ptr(), &mut size) };
    if p.is_null() || size == 0 { return &[]; }
    unsafe { core::slice::from_raw_parts(p, size as usize) }
}

fn hex(s: &[u8]) -> Option<u64> {
    if s.is_empty() || s.len() > 16 { return None; }
    let mut v = 0u64;
    for &c in s { v = v << 4 | match c { b'0'..=b'9' => c - b'0', b'a'..=b'f' => c - b'a' + 10, b'A'..=b'F' => c - b'A' + 10, _ => return None } as u64; }
    Some(v)
}

/// Name a kernel code address: the last symbol at or before it (the file is sorted by address).
pub fn lookup(addr: u64) -> Option<Symbol<'static>> {
    let mut best: Option<(u64, &'static [u8])> = None;
    for line in table().split(|&c| c == b'\n') {
        let Some(sp) = line.iter().position(|&c| c == b' ') else { continue };
        let (Some(a), name) = (hex(&line[..sp]), &line[sp + 1..]) else { continue };
        if a > addr { break; }
        if !name.is_empty() { best = Some((a, name)); }
    }
    let (a, name) = best?;
    let name = core::str::from_utf8(name).ok()?;
    if addr - a > 1 << 20 { return None; }                       // too far from any symbol: not a real match
    Some(Symbol { name, offset: addr - a })
}
