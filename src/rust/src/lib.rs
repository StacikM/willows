//! Kernel Rust for Willows.
//!
//! A `no_std` static library linked into the kernel. It runs in ring 0 on the kernel's own stack, like the C code, and
//! follows the same rules: no floating point or SSE (the `x86_64-unknown-none` target guarantees that), never block except
//! through the kernel's wait helpers. `Box`, `Vec`, `String` and friends from `alloc` work on the kernel heap.
//!
//! - [`kernel`]: the C functions and data Rust calls.
//! - [`log`]: `kprint!`, `kprintln!` and `kdbg!`.
//! - [`panic`]: the kernel's panic path (`kernel_panic` from C, CPU faults in the kernel, Rust's `panic!`).
//! - [`symbols`]: kernel function names for addresses (`C:\System\kernel.sym`).
//!
//! Add a function: write it in a module as `#[no_mangle] pub extern "C" fn name(...)`, declare it in `src/kernel.h`, and call it
//! from C. Call C functions through the declarations in [`kernel`].
#![no_std]

extern crate alloc;

pub mod kernel;
#[macro_use]
pub mod log;
pub mod panic;
pub mod symbols;

use alloc::{boxed::Box, vec::Vec};
use core::alloc::{GlobalAlloc, Layout};

/// `Box`, `Vec` ... allocate on the kernel heap, whose blocks are 16-byte aligned.
struct KernelHeap;
unsafe impl GlobalAlloc for KernelHeap {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        if l.align() > 16 { return core::ptr::null_mut(); }
        kernel::heap_alloc(l.size().max(1) as u64)
    }
    unsafe fn dealloc(&self, p: *mut u8, _l: Layout) { kernel::heap_free(p) }
}
#[global_allocator]
static HEAP: KernelHeap = KernelHeap;

/// Run once at boot (src/main.c): 1 when Rust code, the heap and dynamic dispatch work.
#[no_mangle]
pub extern "C" fn rust_selftest() -> i32 {
    trait Shape { fn area(&self) -> u32; }
    struct Rect(u32, u32);
    impl Shape for Rect { fn area(&self) -> u32 { self.0 * self.1 } }
    let shapes: Vec<Box<dyn Shape>> = alloc::vec![Box::new(Rect(3, 4)), Box::new(Rect(5, 5))];
    let total: u32 = shapes.iter().map(|s| s.area()).sum();
    (total == 37) as i32
}

/// Deliberately stop the kernel through Rust's `panic!` (the `panic rust` test, see src/panic.c).
#[no_mangle]
pub extern "C" fn rust_test_panic() -> ! { panic!("test panic requested from the terminal") }
