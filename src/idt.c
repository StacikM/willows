#include "kernel.h"
struct idt_ent { u16 lo, sel; u8 ist, flags; u16 mid; u32 hi, zero; } __attribute__((packed));
static struct idt_ent idt[256];
extern u64 isr_table[32]; extern void irq0_stub(void), irq1_stub(void), irq12_stub(void);
volatile u32 ticks;

static void set_gate(int n, u64 h){ idt[n]=(struct idt_ent){h&0xFFFF,0x08,0,0x8E,(h>>16)&0xFFFF,(u32)(h>>32),0}; }
void idt_init(void){
  for(int i=0;i<32;i++) set_gate(i,isr_table[i]);
  set_gate(0x20,(u64)irq0_stub); set_gate(0x21,(u64)irq1_stub); set_gate(0x2C,(u64)irq12_stub);
  struct { u16 lim; u64 base; } __attribute__((packed)) d={sizeof idt-1,(u64)idt};
  __asm__ volatile("lidt %0"::"m"(d));
}
void timer_init(void){
  outb(0x20,0x11); outb(0xA0,0x11); outb(0x21,0x20); outb(0xA1,0x28);   /* remap PICs */
  outb(0x21,4); outb(0xA1,2); outb(0x21,1); outb(0xA1,1);
  outb(0x21,0xFE); outb(0xA1,0xFF);                                      /* only IRQ0 */
  outb(0x43,0x34); outb(0x40,1193&0xFF); outb(0x40,1193>>8);             /* ~1000 Hz */
  __asm__ volatile("sti");
}
void timer_tick(void){ ticks++; }

static const char *names[]={"divide error","debug","NMI","breakpoint","overflow","bound range","invalid opcode",
  "device n/a","double fault","","invalid TSS","segment not present","stack fault","general protection","page fault"};
void exception_handler(struct regs *r){
  u64 cr2; __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
  kprintf("\n*** CPU exception %d (%s) err=%x rip=%p", (int)r->vec, r->vec<15?names[r->vec]:"?", (u32)r->err, r->rip);
  if(r->vec==14) kprintf(" cr2=%p", cr2);
  kprintf("\n    rax=%p rbx=%p rcx=%p rdx=%p rsp=%p\n", r->rax,r->rbx,r->rcx,r->rdx,r->rsp);
  if(cur!=&procs[0] && cur->imagebase){                                    /* poor man's backtrace: stack words that point into the program image */
    u64 base=cur->imagebase; u64 *sp=(u64*)r->rsp; int shown=0; kprintf("    stack:");
    for(int i=0;i<300 && shown<14 && (u64)(sp+i)<cur->img.stack_top+64;i++){ u64 v=sp[i]; if(v>base+0x1000 && v<base+0x400000){ kprintf(" +%x",(u32)(v-base)); shown++; } else if(v>=0x100000 && v<0x400000){ kprintf(" k%x",(u32)v); shown++; } }
    kprintf("\n");
  }
  if(cur!=&procs[0]){ kprintf("    Application terminated.\n"); proc_exit(0xC0000005); }
  kputs("    Kernel fault, halting.\n"); for(;;) __asm__ volatile("cli;hlt");
}
