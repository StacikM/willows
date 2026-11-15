#include "kernel.h"
/* Virtual memory for Windows programs: VirtualAlloc/VirtualFree/VirtualProtect/VirtualQuery (and the Nt* versions) with real
   reserve / commit / decommit / release semantics. Reservations live in [VM_LO, VM_HI), a range of the single address space that
   nothing else uses; committed pages are 4 KB pages taken from a page pool (freed pages go back to it), mapped with 4 KB page tables.
   Allocators such as mozjemalloc rely on this: reserve a big range, release it, reserve again at an aligned address inside it,
   commit pieces on demand. A page that is reserved but not committed is not mapped (touching it faults, as on Windows).
   32-bit programs keep the old behaviour (plain heap memory below 4 GB). Everything a process reserved is released when it exits.
   The range is the whole user half of the address space above 64 GB: programs (JavaScript engines, hardened allocators) reserve at
   randomised addresses anywhere up to 128 TB. pml4[0] (the first 512 GB) is shared with the kernel's identity map and image
   mappings; the other top-level entries get their page tables on demand. */
typedef u32 DWORD; typedef i32 BOOL;
extern u64 pdpt[], pml4[];
#define VM_LO 0x1000000000ull                   /* 64 GB (below: identity map, 2 MB image mappings) */
#define VM_HI 0x800000000000ull                 /* 128 TB: the top of the user address space */
#define GRAN 0x10000ull                          /* allocation granularity */
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
#define MEM_FREE 0x10000
#define MEM_PRIVATE 0x20000
#define MEM_RESET 0x80000
#define MEM_IMAGE 0x1000000
#define MEM_RESET_UNDO 0x1000000

struct vreg { u64 base, size; u32 prot; int owner; u8 lazy; };      /* lazy: a stack, committed on demand (vmem_grow_stack) */
#define MAXVREG 8192
static struct vreg regs[MAXVREG]; static int nregs;            /* sorted by base, never overlapping */
static u64 committed_pages;

/* ---- physical pages ---- */
static u64 *freepg; static u64 nfreepg;
static u64 pg_alloc(void){ return (u64)kmalloc(4096,4096); }       /* page tables: low memory (identity-mapped, zeroed) */
static u64 dpg_alloc(void){                                       /* programs' pages: freed ones, then RAM above 4 GB, then the low heap */
  if(freepg){ u64 *p=freepg; freepg=(u64*)*p; nfreepg--; memset(p,0,4096); return (u64)p; }
  u64 h=mm_high_page(); if(h) return h;
  return (u64)kmalloc(4096,4096);                              /* zeroed */
}
static void pg_free(u64 p){ *(u64*)p=(u64)freepg; freepg=(u64*)p; nfreepg++; }
/* the same pool for program images (pe.c): a zeroed page (its kernel-visible address; mm_v2p gives the physical one), and back */
u64 vmem_page_alloc(void){ return dpg_alloc(); }
void vmem_page_free_phys(u64 phys){ pg_free(mm_p2v(phys)); }
u64 vmem_free_pages(void){ return nfreepg; }

static u64 *pdpt_of(u64 va, int create){                        /* the page-directory-pointer table for va (512 GB per entry) */
  unsigned i4=(va>>39)&511; if(i4==0) return pdpt;
  if(!(pml4[i4]&1)){ if(!create) return 0; u64 t=pg_alloc(); if(!t) return 0; pml4[i4]=t|3; }
  return (u64*)(pml4[i4]&~0xFFFull);
}
static u64 *pte(u64 va, int create){
  unsigned i3=(va>>30)&511, i2=(va>>21)&511, i1=(va>>12)&511;
  u64 *pdp=pdpt_of(va,create); if(!pdp) return 0;
  if(!(pdp[i3]&1)){ if(!create) return 0; u64 pd=pg_alloc(); if(!pd) return 0; pdp[i3]=pd|3; }
  u64 *pd=(u64*)(pdp[i3]&~0xFFFull);
  if(pd[i2]&0x80) return 0;                                    /* a 2 MB page (not ours) */
  if(!(pd[i2]&1)){ if(!create) return 0; u64 pt=pg_alloc(); if(!pt) return 0; pd[i2]=pt|3; }
  return &((u64*)(pd[i2]&~0xFFFull))[i1];
}
static void invlpg(u64 va){ __asm__ volatile("invlpg (%0)"::"r"(va):"memory"); }
static int commit(u64 a, u64 e){                               /* page-aligned range inside a reservation */
  for(u64 v=a; v<e; v+=4096){
    u64 *p=pte(v,1); if(!p) return -1;
    if(!(*p&1)){ u64 ph=dpg_alloc(); if(!ph) return -1; *p=mm_v2p(ph)|3; invlpg(v); committed_pages++; }
  }
  return 0;
}
static void decommit(u64 a, u64 e){
  for(u64 v=a; v<e; ){
    unsigned i3=(v>>30)&511, i2=(v>>21)&511;
    u64 *pdp=pdpt_of(v,0); if(!pdp){ v=(v|0x7FFFFFFFFFull)+1; continue; }   /* skip unmapped 512 GB / 1 GB / 2 MB areas quickly */
    if(!(pdp[i3]&1)){ v=(v|0x3FFFFFFFull)+1; continue; }
    u64 *pd=(u64*)(pdp[i3]&~0xFFFull); if(!(pd[i2]&1)){ v=(v|0x1FFFFFull)+1; continue; }
    u64 *p=pte(v,0); if(p && (*p&1)){ pg_free(mm_p2v(*p&0x000FFFFFFFFFF000ull)); *p=0; invlpg(v); committed_pages--; }
    v+=4096;
  }
}
static int committed(u64 v){ u64 *p=pte(v,0); return p && (*p&1); }

/* ---- reservations ---- */
static int find_reg(u64 a){                                     /* index of the reservation containing a, or -1 */
  int lo=0, hi=nregs-1;
  while(lo<=hi){ int m=(lo+hi)/2; if(a<regs[m].base) hi=m-1; else if(a>=regs[m].base+regs[m].size) lo=m+1; else return m; }
  return -1;
}
static int range_free(u64 b, u64 n){
  if(b<VM_LO || b+n>VM_HI || b+n<b) return 0;
  for(int i=0;i<nregs;i++){ if(regs[i].base>=b+n) break; if(regs[i].base+regs[i].size>b) return 0; }
  return 1;
}
static int insert_reg(u64 b, u64 n, u32 prot){
  if(nregs>=MAXVREG) return -1;
  int i=0; while(i<nregs && regs[i].base<b) i++;
  memmove(&regs[i+1],&regs[i],(nregs-i)*sizeof *regs); nregs++;
  regs[i].base=b; regs[i].size=n; regs[i].prot=prot; regs[i].owner=cur? cur->pid : 0; regs[i].lazy=0; return i;
}
static u64 next_hint=VM_LO;
static u64 find_gap(u64 n, u64 align){                          /* first fit, starting after the last reservation (wraps once) */
  for(int pass=0;pass<2;pass++){
    u64 b=pass? VM_LO : next_hint; b=(b+align-1)&~(align-1);
    for(int i=0;i<nregs;i++){
      if(regs[i].base+regs[i].size<=b) continue;
      if(regs[i].base>=b+n) break;
      b=(regs[i].base+regs[i].size+align-1)&~(align-1);
    }
    if(b+n<=VM_HI){ next_hint=b+n; return b; }
  }
  return 0;
}
static void release_reg(int i){ decommit(regs[i].base,regs[i].base+regs[i].size); memmove(&regs[i],&regs[i+1],(nregs-i-1)*sizeof *regs); nregs--; }
void vmem_cleanup(int pid){ for(int i=nregs-1;i>=0;i--) if(regs[i].owner==pid) release_reg(i); }
int vmem_owns(u64 a){ return a>=VM_LO && a<VM_HI; }

/* plain allocation for kernel code that wants freeable, page-granular memory (thread stacks) */
void *vmem_alloc(u64 n){
  n=(n+4095)&~4095ull; u64 b=find_gap(n+GRAN,GRAN); if(!b) return 0;   /* a free 64 KB guard gap stays below each block */
  b+=GRAN; int r=insert_reg(b,n,4); if(r<0) return 0; regs[r].owner=0;   /* owned by the kernel: freed explicitly, never by a process cleanup (it may be the running stack) */
  if(commit(b,b+n)){ int i=find_reg(b); if(i>=0) release_reg(i); return 0; }
  return (void*)b;
}
void vmem_release(void *p){ int i=find_reg((u64)p); if(i>=0) release_reg(i); }
/* Stacks grow on demand, as on Windows: the whole size is reserved, only the top is committed, and a page fault below it (handled on
   its own stack, idt.c) commits more. Each fault commits one page beyond the one touched, so the CPU can always push an interrupt
   frame without faulting while it delivers the interrupt. */
#define STACK_TOP_COMMIT 0x10000
void *vmem_alloc_stack(u64 n){
  n=(n+4095)&~4095ull; u64 b=find_gap(n+GRAN,GRAN); if(!b) return 0;
  b+=GRAN; int r=insert_reg(b,n,4); if(r<0) return 0; regs[r].owner=0; regs[r].lazy=1;
  u64 top=n<STACK_TOP_COMMIT? n : STACK_TOP_COMMIT;
  if(commit(b+n-top,b+n)){ int i=find_reg(b); if(i>=0) release_reg(i); return 0; }
  return (void*)b;
}
int vmem_readable(u64 a){ if(a<VM_LO || a>=VM_HI) return 0; u64 *pdp=pdpt_of(a,0); if(!pdp) return 0; u64 *p=pte(a,0); return p && (*p&1); }   /* crash reports: may this be read? */
int vmem_grow_stack(u64 addr){                                       /* page fault at addr: 1 if it was a stack growing (now committed) */
  int i=find_reg(addr); if(i<0 || !regs[i].lazy) return 0;
  u64 pg=addr&~0xFFFull, lo= pg>regs[i].base? pg-4096 : pg;
  return commit(lo,pg+4096)==0;
}

#define ERR(e) do{ win32_set_last_error(e); return 0; }while(0)
static void *va_core(void *addr, u64 size, DWORD type, DWORD prot);
static void *WINAPI v_VirtualAlloc(void *addr, u64 size, DWORD type, DWORD prot){       /* failures are logged with the caller: they are rare and usually fatal */
  void *r=va_core(addr,size,type,prot);
  if(!r){ char at[64]; if(!dll_addr_name((u64)__builtin_return_address(0),at,sizeof at)) ksnprintf(at,sizeof at,"%p",__builtin_return_address(0));
    kdbg("  [vmem] VirtualAlloc(%p, %p, type %x, prot %x) failed (error %u) from %s\n",addr,(void*)size,type,prot,win32_get_last_error(),at); }
  return r;
}
static void *va_core(void *addr, u64 size, DWORD type, DWORD prot){
  if(!size) ERR(87);
  if(cur && cur->img.is32){                                     /* 32-bit programs need memory below 4 GB: old behaviour */
    if(addr) ERR(487);
    void *p=kmalloc((size+0xFFF)&~0xFFFull,0x1000); if(!p) ERR(8); return p;
  }
  u64 a=(u64)addr;
  if(type&MEM_RESET || type==MEM_RESET_UNDO) return addr;       /* contents may be discarded: keep them */
  if(!(type&(MEM_COMMIT|MEM_RESERVE))) ERR(87);
  if(!a || (type&MEM_RESERVE)){
    u64 b, e;
    if(a){ b=a&~(GRAN-1); e=(a+size+4095)&~4095ull; if(!range_free(b,e-b)) ERR(487); }
    else { u64 n=(size+4095)&~4095ull; b=find_gap(n,GRAN); if(!b) ERR(8); e=b+n; }
    if(insert_reg(b,e-b,prot)<0) ERR(8);
    if((type&MEM_COMMIT) && commit(b,e)){ int i=find_reg(b); if(i>=0) release_reg(i); ERR(8); }
    return (void*)(a? a&~4095ull : b);
  }
  int i=find_reg(a); if(i<0) ERR(487);                         /* commit inside an existing reservation */
  u64 s=a&~4095ull, e=(a+size+4095)&~4095ull; if(e>regs[i].base+regs[i].size) ERR(487);
  if(commit(s,e)) ERR(8);
  return (void*)s;
}
static BOOL WINAPI v_VirtualFree(void *addr, u64 size, DWORD type){
  u64 a=(u64)addr; int i=find_reg(a);
  if(i<0) return vmem_owns(a)? (win32_set_last_error(487),0) : 1;    /* memory from the old allocator (32-bit programs): nothing to do */
  if(type&MEM_RELEASE){ if(size || a!=regs[i].base) ERR(87); release_reg(i); return 1; }
  if(type&MEM_DECOMMIT){ u64 e= size? (a+size+4095)&~4095ull : regs[i].base+regs[i].size; if(e>regs[i].base+regs[i].size) e=regs[i].base+regs[i].size; decommit(a&~4095ull,e); return 1; }
  ERR(87);
}
static BOOL WINAPI v_VirtualProtect(void *addr, u64 size, DWORD prot, DWORD *old){
  (void)size; int i=find_reg((u64)addr);
  if(i>=0){ if(old) *old=regs[i].prot; regs[i].prot=prot; return 1; }   /* protection is recorded, not enforced (everything is read/write/execute) */
  if(old) *old=0x40; return 1;
}
int pe_find_mapping(u64 a, u64 *base, u64 *span);
static u64 WINAPI v_VirtualQuery(void *addr, u8 *mbi, u64 len){
  if(len<48) ERR(24);
  u64 a=(u64)addr, pg=a&~4095ull; memset(mbi,0,48);
  u64 *base=(u64*)mbi, *abase=(u64*)(mbi+8), *rsize=(u64*)(mbi+0x18); u32 *aprot=(u32*)(mbi+0x10), *state=(u32*)(mbi+0x20), *protect=(u32*)(mbi+0x24), *type=(u32*)(mbi+0x28);
  *base=pg;
  if(vmem_owns(a)){
    int i=find_reg(a);
    if(i<0){ u64 end=VM_HI; for(int k=0;k<nregs;k++) if(regs[k].base>a){ end=regs[k].base; break; } *rsize=end-pg; *state=MEM_FREE; *protect=1; return 48; }
    u64 e=regs[i].base+regs[i].size; int c=committed(pg); u64 v=pg; while(v<e && committed(v)==c) v+=4096;
    *abase=regs[i].base; *aprot=regs[i].prot; *rsize=v-pg; *state=c? MEM_COMMIT : MEM_RESERVE; *protect=c? regs[i].prot : 0; *type=MEM_PRIVATE; return 48;
  }
  u64 ib, isp;
  if(pe_find_mapping(a,&ib,&isp)){ *abase=ib; *aprot=0x80; *rsize=ib+isp-pg; *state=MEM_COMMIT; *protect=0x40; *type=MEM_IMAGE; return 48; }
  if(cur && cur->teb){ u64 top=((u64*)win32_cur_teb())[1], lo=((u64*)win32_cur_teb())[2];
    if(a>=lo && a<top){ *abase=lo; *aprot=4; *rsize=top-pg; *state=MEM_COMMIT; *protect=4; *type=MEM_PRIVATE; return 48; } }
  *abase=a&~(GRAN-1); *aprot=4; *rsize=4096; *state=MEM_COMMIT; *protect=4; *type=MEM_PRIVATE; return 48;
}
/* the *Ex versions: only the calling process (or the pseudo handle -1) */
static void *WINAPI v_VirtualAllocEx(u64 h, void *a, u64 n, DWORD t, DWORD p){ (void)h; return v_VirtualAlloc(a,n,t,p); }
static BOOL WINAPI v_VirtualFreeEx(u64 h, void *a, u64 n, DWORD t){ (void)h; return v_VirtualFree(a,n,t); }
static BOOL WINAPI v_VirtualProtectEx(u64 h, void *a, u64 n, DWORD p, DWORD *o){ (void)h; return v_VirtualProtect(a,n,p,o); }
static u64 WINAPI v_VirtualQueryEx(u64 h, void *a, u8 *m, u64 n){ (void)h; return v_VirtualQuery(a,m,n); }
static void *WINAPI v_VirtualAlloc2(u64 h, void *a, u64 n, DWORD t, DWORD p, void *ep, DWORD np){ (void)h;(void)ep;(void)np; return v_VirtualAlloc(a,n,t&~0x4000u,p); }
static BOOL WINAPI v_VirtualLock(void *a, u64 n){ (void)a;(void)n; return 1; }
static BOOL WINAPI v_PrefetchVirtualMemory(u64 h, u64 n, void *e, DWORD f){ (void)h;(void)n;(void)e;(void)f; return 1; }
static BOOL WINAPI v_FlushInstructionCache(u64 h, void *a, u64 n){ (void)h;(void)a;(void)n; return 1; }
static DWORD WINAPI v_DiscardVirtualMemory(void *a, u64 n){ (void)a;(void)n; return 0; }
static BOOL WINAPI v_OfferVirtualMemory(void *a, u64 n, DWORD p){ (void)a;(void)n;(void)p; return 0; }
static BOOL WINAPI v_GetProcessWorkingSetSize(u64 h, u64 *mn, u64 *mx){ (void)h; if(mn) *mn=0x100000; if(mx) *mx=0x10000000; return 1; }
static BOOL WINAPI v_SetProcessWorkingSetSize(u64 h, u64 mn, u64 mx){ (void)h;(void)mn;(void)mx; return 1; }
/* native API: NTSTATUS results, in/out base address and size */
static u32 st_of_err(void){ u32 e=win32_get_last_error(); return e==8? 0xC0000017 : e==87? 0xC000000D : 0xC0000018; }
static u32 WINAPI v_NtAllocateVirtualMemory(u64 h, void **base, u64 zb, u64 *size, DWORD type, DWORD prot){ (void)h;(void)zb; void *r=v_VirtualAlloc(*base,*size,type,prot); if(!r) return st_of_err(); u64 e=((u64)(*base? *base : r)+*size+4095)&~4095ull; *base=r; *size=e-(u64)r; return 0; }
static u32 WINAPI v_NtFreeVirtualMemory(u64 h, void **base, u64 *size, DWORD type){ (void)h; if(!v_VirtualFree(*base,(type&MEM_RELEASE)? 0 : *size,type)) return 0xC000009F; return 0; }
static u32 WINAPI v_NtProtectVirtualMemory(u64 h, void **base, u64 *size, DWORD prot, DWORD *old){ (void)h; v_VirtualProtect(*base,*size,prot,old); return 0; }
static u32 WINAPI v_NtQueryVirtualMemory(u64 h, void *a, u32 cls, void *out, u64 n, u64 *ret){ (void)h; if(cls!=0) return 0xC0000003; u64 r=v_VirtualQuery(a,out,n); if(ret) *ret=r; return r? 0 : 0xC0000004; }

#define V(n) {"kernel32",#n,v_##n}
#define VN(n) {"ntdll",#n,v_##n}
const struct export vmem_exports[] = {
  V(VirtualAlloc), V(VirtualFree), V(VirtualProtect), V(VirtualQuery), V(VirtualAllocEx), V(VirtualFreeEx), V(VirtualProtectEx), V(VirtualQueryEx),
  V(VirtualLock), {"kernel32","VirtualUnlock",v_VirtualLock}, V(PrefetchVirtualMemory), V(FlushInstructionCache), V(DiscardVirtualMemory), V(OfferVirtualMemory),
  {"kernel32","ReclaimVirtualMemory",v_DiscardVirtualMemory}, V(GetProcessWorkingSetSize), V(SetProcessWorkingSetSize),
  {"kernel32","GetProcessWorkingSetSizeEx",v_GetProcessWorkingSetSize}, {"kernel32","SetProcessWorkingSetSizeEx",v_SetProcessWorkingSetSize},
  {"kernelbase","VirtualAlloc2",v_VirtualAlloc2}, {"api-ms-win-core-memory-l1-1-6","VirtualAlloc2",v_VirtualAlloc2},
  VN(NtAllocateVirtualMemory), VN(NtFreeVirtualMemory), VN(NtProtectVirtualMemory), VN(NtQueryVirtualMemory),
  {"ntdll","ZwAllocateVirtualMemory",v_NtAllocateVirtualMemory}, {"ntdll","ZwFreeVirtualMemory",v_NtFreeVirtualMemory},
  {"ntdll","ZwProtectVirtualMemory",v_NtProtectVirtualMemory}, {"ntdll","ZwQueryVirtualMemory",v_NtQueryVirtualMemory},
};
const unsigned vmem_nexports = sizeof vmem_exports / sizeof *vmem_exports;
u64 vmem_committed(void){ return committed_pages*4096; }
u64 vmem_reserved(void){ u64 t=0; for(int i=0;i<nregs;i++) t+=regs[i].size; return t; }
