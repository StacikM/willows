#include "../kernel.h"
/* Linked instead of the Rust library when this machine has no Rust toolchain for x86_64-unknown-none (see the Makefile):
   the kernel then has no Rust code, the boot log says so, and panics get this plain text report instead of the panic screen. */
int rust_selftest(void){ return -1; }
void kernel_panic_regs(const char *reason, const struct panic_regs *r){
  __asm__ volatile("cli");
  kprintf("\n*** Willows stopped: %s\n    rip %p rsp %p\n    (built without Rust: no panic screen)\n", reason? reason : "kernel panic", r? (void *)r->rip : 0, r? (void *)r->rsp : 0);
  for(;;) __asm__ volatile("cli; hlt");
}
void rust_test_panic(void){ kernel_panic_regs("test panic (no Rust in this build)",0); }
