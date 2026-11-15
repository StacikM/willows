#include "kernel.h"
struct idt_ent { u16 lo, sel; u8 ist, flags; u16 mid; u32 hi, zero; } __attribute__((packed));
static struct idt_ent idt[256];
extern u64 isr_table[32]; extern void irq0_stub(void), irq1_stub(void), irq12_stub(void);
volatile u32 ticks;

static void set_gate(int n, u64 h){ idt[n]=(struct idt_ent){h&0xFFFF,0x08,0,0x8E,(h>>16)&0xFFFF,(u32)(h>>32),0}; }
/* TSS with an interrupt stack (IST1) for page faults and double faults: programs' stacks grow on demand (vmem.c), and the page fault
   that grows one happens on that very stack - the CPU could not push the exception frame there. */
struct tss64 { u32 r0; u64 rsp[3]; u64 r1; u64 ist[7]; u64 r2; u16 r3, iomap; } __attribute__((packed));
static struct tss64 tss __attribute__((aligned(16)));
static u8 ist_stack[32768] __attribute__((aligned(16)));
static void tss_init(void){
  extern u64 gdt64_tss[2];
  memset(&tss,0,sizeof tss); tss.ist[0]=(u64)(ist_stack+sizeof ist_stack); tss.iomap=sizeof tss;
  u64 b=(u64)&tss, lim=sizeof tss-1;
  gdt64_tss[0]=(lim&0xFFFF)|((b&0xFFFFFF)<<16)|(0x89ull<<40)|(((lim>>16)&0xF)<<48)|(((b>>24)&0xFF)<<56);   /* present, 64-bit TSS (available) */
  gdt64_tss[1]=b>>32;
  __asm__ volatile("ltr %w0"::"r"((u16)0x28));
  idt[14].ist=1; idt[8].ist=1;                                          /* page fault, double fault */
  u64 cr0; __asm__ volatile("mov %%cr0,%0":"=r"(cr0)); cr0|=1ull<<16; __asm__ volatile("mov %0,%%cr0"::"r"(cr0));   /* WP: read-only pages hold for the kernel too (shared image pages, pe.c) */
}
void idt_init(void){
  for(int i=0;i<32;i++) set_gate(i,isr_table[i]);
  tss_init();
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
static void sput(const char *s){ for(;*s;s++){ while(!(inb(0x3FD)&0x20)); outb(0x3F8,*s); } }
void timer_tick(void){
  ticks++;
  if(ticks%3000==0 && cur && cur!=&procs[0] && cur->img.is32){                  /* debugging aid: a 32-bit program that is still running -> its latest Windows calls go to the serial log */
    extern u32 wow_ring[32], wow_pos; extern const char *wow_name(u32);
    sput("[wow] last calls, oldest first:"); for(int i=0;i<32;i++){ sput(" "); sput(wow_name(wow_ring[(wow_pos+i)&31])); } sput("\n");
  }
}

static const char *names[]={"divide error","debug","NMI","breakpoint","overflow","bound range","invalid opcode",
  "device n/a","double fault","","invalid TSS","segment not present","stack fault","general protection","page fault"};
/* exception reports go to the program's console (or the terminal that captures it) AND always to the serial log */
static void WINAPI xprintf(const char *f, ...){
  char b[512]; margs ap; __builtin_ms_va_start(ap,f); kvsnprintf(b,sizeof b,f,ap); __builtin_ms_va_end(ap);
  kprintf("%s",b); if(cur && cur->cap_on) kdbg("%s",b);
}
/* crash reports: may this address be read without faulting again? */
static int readable(u64 a){
  extern int vmem_readable(u64 a); extern int mm_in_window(u64 a);
  if(a<0x1000) return 0; if(a<0xC0000000ull) return 1;                   /* the identity-mapped low memory (page 0 is unmapped) */
  if(mm_in_window(a)) return 1;
  if(a<0x8000000000ull) return (vm_page_entry(a)&1)!=0;                   /* program and DLL images */
  return vmem_readable(a);
}
/* the C++ class of an object, from MSVC's RTTI: vtable[-1] -> complete object locator -> type descriptor (".?AVname@ns@@") */
static int rtti_name(u64 obj, char *out, int n){
  if(!readable(obj) || !readable(obj+7)) return 0; u64 vt=*(u64*)obj; u64 base, span;
  if(!pe_find_mapping(vt,&base,&span) || !readable(vt-8)) return 0;
  u64 col=*(u64*)(vt-8);
  if(!pe_find_mapping(col,&base,&span) || !readable(col+24)){             /* built without RTTI (Firefox): name the vtable itself */
    char m[64]; if(!dll_addr_name(vt,m,sizeof m)) return 0; int k=0; const char *pre="vtable "; while(*pre && k<n-1) out[k++]=*pre++;
    for(int i=0;m[i] && k<n-1;i++) out[k++]=m[i]; out[k]=0; return 1; }
  u32 sig=*(u32*)col, self=*(u32*)(col+20); if(sig!=1 || col-self!=base) return 0;           /* x64 locators point back at themselves */
  u64 td=base+*(u32*)(col+12); if(!readable(td+16)) return 0; const char *nm=(const char*)(td+16);
  int k=0; for(;k<n-1 && readable((u64)nm+k) && nm[k]; k++) out[k]=nm[k]; out[k]=0; return k>0;
}
static void dump_words(const char *what, u64 a, int n){                   /* words of memory; those pointing into a DLL by name */
  if(!readable(a) || !readable(a+(u64)n*8-1)) return; xprintf("    %s %p:",what,(void*)a); char nm[64];
  for(int i=0;i<n;i++){ u64 v=((u64*)a)[i]; if(dll_addr_name(v,nm,sizeof nm)) xprintf(" +%x=%s",i*8,nm); else if(v) xprintf(" +%x=%p",i*8,(void*)v); }
  xprintf("\n");
}
static void report_objects(struct regs *r){                                /* registers that point at C++ objects, by class */
  static const char *const rn[]={"rcx","rdx","rbx","rsi","rdi","r8","r12","r13","r14","r15"};
  u64 rv[]={r->rcx,r->rdx,r->rbx,r->rsi,r->rdi,r->r8,r->r12,r->r13,r->r14,r->r15}; char nm[96]; int any=0;
  for(unsigned i=0;i<sizeof rv/sizeof *rv;i++) if(rtti_name(rv[i],nm,sizeof nm)){ if(!any) xprintf("    objects:"); any=1; xprintf(" %s=%s",rn[i],nm); }
  if(any) xprintf("\n");
  if(rtti_name(r->rcx,nm,sizeof nm)){                                     /* and the objects in the fields of the one rcx points at (usually 'this') */
    int f=0; for(u64 off=8; off<0x200; off+=8){ if(!readable(r->rcx+off)) break; u64 v=*(u64*)(r->rcx+off); char fn[96];
      if(rtti_name(v,fn,sizeof fn)){ if(!f) xprintf("    fields of rcx:"); f=1; xprintf(" +%x=%s",(u32)off,fn); } }
    if(f) xprintf("\n");
  }
}
void exception_handler(struct regs *r){
  u64 cr2; __asm__ volatile("mov %%cr2,%0":"=r"(cr2));
  if(r->vec==14 && vmem_grow_stack(cr2)) return;                         /* a stack growing into its reservation: the page is there now, retry */
  if(r->vec==14 && (r->err&3)==3 && pe_cow_fault(cr2)) return;           /* a write to a shared image page: now a private copy, retry */
  xprintf("\n*** CPU exception %d (%s) err=%x rip=%p", (int)r->vec, r->vec<15?names[r->vec]:"?", (u32)r->err, r->rip);
  if(r->vec==14) xprintf(" cr2=%p", cr2);
  xprintf("\n    rax=%p rbx=%p rcx=%p rdx=%p rsp=%p\n", r->rax,r->rbx,r->rcx,r->rdx,r->rsp);
  if(cur!=&procs[0] && cur->img.is32){                                     /* 32-bit program: its last two Windows calls, and stack words that point into its image */
    extern u32 wow_ring[32], wow_pos; extern const char *wow_name(u32);
    xprintf("    last calls (newest first):"); for(int i=1;i<=4;i++) xprintf(" %s", wow_name(wow_ring[(wow_pos+32-i)&31]));
    xprintf("\n    base=%p stack:", cur->imagebase);
    u32 *sp=(u32*)(r->rsp&0xFFFFFFFFull); u64 base=cur->imagebase; int shown=0;
    for(int i=0;i<400 && shown<16;i++){ u32 v=sp[i]; if(v>base+0x1000 && v<base+0x600000){ xprintf(" +%x",v-(u32)base); shown++; } }
    xprintf("\n");
  } else
  if(cur!=&procs[0] && cur->imagebase){                                   /* poor man's backtrace: stack words that point into the program or one of its DLLs */
    char nm[64]; if(dll_addr_name(r->rip,nm,sizeof nm)) xprintf("    at %s%s\n",nm,cur_thr? " (in a thread)" : "");
    u64 top= cur_teb? cur_teb[1] : cur->img.stack_top+64; u64 *sp=(u64*)r->rsp; int shown=0; xprintf("    stack:");
    for(int i=0;i<2000 && shown<16 && (u64)(sp+i)<top;i++){ u64 v=sp[i]; if(dll_addr_name(v,nm,sizeof nm)){ xprintf(" %s",nm); shown++; } else if(v>=0x100000 && v<0x400000){ xprintf(" k%x",(u32)v); shown++; } }
    xprintf("\n");
    report_objects(r);
    if(r->vec==14 && cr2<0x1000){ dump_words("rcx ->",r->rcx,32); if(readable(r->rcx+0xd8)) dump_words("[rcx+d8] ->",*(u64*)(r->rcx+0xd8),8); }   /* a NULL-based fault: what 'this' holds */
    extern u64 api_ring[];
    if(api_ring[0]){                                                         /* apitrace.cfg: this process's newest calls (entry = pid<<56 | name) */
      u64 n=api_ring[0], pick[40]; int k=0; for(u64 i=n; i>0 && i+8192>n && k<40; i--){ u64 e=api_ring[1+((i-1)&8191)]; if((int)(e>>56)==cur->pid) pick[k++]=e&0x00FFFFFFFFFFFFFFull; }
      xprintf("    last API calls (newest last):"); while(k) xprintf(" %s",(const char*)pick[--k]); xprintf("\n");
    }
    if(r->vec==3){                                                          /* a deliberate crash: Firefox/Chromium-style programs leave the reason in a global */
      const char **why=dll_find_export(cur->pid,"gMozCrashReason");
      if(why && *why) xprintf("    crash reason: %s\n",*why);
    }
  }
  if(cur!=&procs[0]){ xprintf("    Application terminated.\n"); proc_terminate(r->vec==3? 0x80000003 : 0xC0000005); }
  kputs("    Kernel fault, halting.\n"); for(;;) __asm__ volatile("cli;hlt");
}
