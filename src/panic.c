#include "kernel.h"
/* kernel_panic(): the C entry to the kernel's panic path, which is written in Rust (src/rust/src/panic.rs). C keeps the printf-style
   part (Rust cannot define C variadic functions); Rust halts the CPU, writes the report to the serial log and draws the panic screen. */

void WINAPI kernel_panic(const char *fmt, ...){
  static char reason[256];                                            /* static: the panic path must not depend on the stack being sane */
  margs ap; __builtin_ms_va_start(ap,fmt); kvsnprintf(reason,sizeof reason,fmt,ap); __builtin_ms_va_end(ap);
  struct panic_regs r; memset(&r,0,sizeof r);
  r.rip=(u64)__builtin_return_address(0); __asm__ volatile("mov %%rsp,%0":"=r"(r.rsp)); __asm__ volatile("mov %%rbp,%0":"=r"(r.rbp));
  kernel_panic_regs(reason,&r);
}
/* WillowsTestPanic(kind): stop the kernel on purpose, to test the panic path (Terminal: "panic c|rust|fault").
   0 = kernel_panic() from C, 1 = Rust's panic!, 2 = a NULL pointer access in the kernel's own context (the main loop does it). */
static volatile int fault_req;
extern void rust_test_panic(void);
static int WINAPI p_WillowsTestPanic(int kind){
  if(kind==0) kernel_panic("test panic requested by %s", (cur && cur!=&procs[0])? cur->name : "the kernel");
  if(kind==1) rust_test_panic();
  if(kind==2){ fault_req=1; return 1; }
  return 0;
}
void panic_service(void){ if(fault_req){ fault_req=0; volatile u64 *p=(volatile u64 *)0x8; *p=0; } }
const struct export panic_exports[]={ {"kernel32","WillowsTestPanic",(void *)p_WillowsTestPanic} };
const unsigned panic_nexports=sizeof panic_exports/sizeof *panic_exports;

/* for the panic report: the program that was running, if any, and the name of a program address ("xul.dll+1234") */
const char *panic_proc_name(void){ return (cur && cur!=&procs[0])? cur->name : 0; }
/* a file's contents (files live in RAM): the panic screen reads the kernel's symbol list, C:\System\kernel.sym */
const u8 *panic_file(const char *path, u64 *size){ struct ramfile *f=fs_find(path); if(!f || f->isdir || !f->data){ *size=0; return 0; } *size=f->size; return f->data; }
int panic_addr_name(u64 a, char *out, int max){ return cur? dll_addr_name(a,out,max) : 0; }
