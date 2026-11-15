#include "kernel.h"
extern u64 pdpt[];
static u64 hcur, limit;
#define hlimit_ limit
void mm_init(u64 s, u64 e){ hcur=s; limit=e; }
/* Bump allocator, zeroed. No free yet. */
void *kmalloc(u64 size, u64 align){
  if(align<16) align=16;
  u64 p=(hcur+align-1)&~(align-1);
  if(p+size>limit || p+size<p) return NULL;
  hcur=p+size; memset((void*)p,0,size); return (void*)p;
}
/* Map [virt,virt+len) -> [phys,..) with 2MB pages. The low 4GB stays identity-mapped (boot.S);
   this is for images that want a high ImageBase such as 0x140000000. */
int vm_map(u64 virt, u64 phys, u64 len){
  if((virt|phys)&0x1FFFFF || virt<0x100000000ull || (virt+len)>>39) return -1;
  for(u64 off=0; off<len; off+=0x200000){
    u64 v=virt+off; unsigned i3=(v>>30)&511, i2=(v>>21)&511;
    if(!(pdpt[i3]&1)){ u64 *pd=kmalloc(4096,4096); if(!pd) return -1; pdpt[i3]=(u64)pd|3; }
    ((u64*)(pdpt[i3]&~0xFFFull))[i2]=(phys+off)|0x83;
  }
  u64 cr3; __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3":"=r"(cr3)::"memory");   /* flush TLB */
  return 0;
}
/* RAM above 4 GB (multiboot memory map): mapped at a kernel window [HWIN, HWIN+len) with 2 MB pages. Programs' VirtualAlloc pages
   come from here first (vmem.c), so the low heap stays for the kernel, DLL images and DMA. Page tables hold physical addresses:
   mm_v2p / mm_p2v translate for pages in the window. */
#define HWIN 0xC00000000ull                                      /* 48 GB: above the 2 MB image mappings, below VirtualAlloc's 64 GB */
#define HWIN_MAX (16ull<<30)
static u64 hi_phys, hi_len, hi_cur;
void mm_high_init(u64 phys, u64 len){
  u64 b=(phys+0x1FFFFF)&~0x1FFFFFull, e=(phys+len)&~0x1FFFFFull; if(e<=b || hi_len) return;
  if(e-b>HWIN_MAX) e=b+HWIN_MAX;
  if(vm_map(HWIN,b,e-b)) return;
  hi_phys=b; hi_len=e-b; hi_cur=0;
}
static u64 hi_top;                                                 /* 2 MB chunks are taken from the top of the high RAM, 4 KB pages from the bottom */
int mm_in_window(u64 a){ return hi_len && a>=HWIN && a<HWIN+hi_len; }
u64 mm_high_page(void){ if(hi_cur+4096>hi_len-hi_top) return 0; u64 v=HWIN+hi_cur; hi_cur+=4096; memset((void*)v,0,4096); return v; }
/* 2 MB chunks for program images (physical addresses, mapped with vm_map): reused after a process exits */
static u64 chunk_free_[1024]; static int nchunk_free;
u64 mm_chunk_alloc(void){
  if(nchunk_free) return chunk_free_[--nchunk_free];
  if(hi_len && hi_len>=hi_top+0x200000+hi_cur){ hi_top+=0x200000; return hi_phys+hi_len-hi_top; }
  void *p=kmalloc(0x200000,0x200000); return (u64)p;              /* low memory: identity-mapped, physical = virtual */
}
void mm_chunk_free(u64 phys){ if(nchunk_free<1024) chunk_free_[nchunk_free++]=phys; }
u64 mm_v2p(u64 v){ return (v>=HWIN && v<HWIN+hi_len)? v-HWIN+hi_phys : v; }
u64 mm_p2v(u64 p){ return (p>=hi_phys && p<hi_phys+hi_len)? p-hi_phys+HWIN : p; }
u64 mm_high_total(void){ return hi_len; }
u64 mm_high_free(void){ return hi_len-hi_top-hi_cur+(u64)nchunk_free*0x200000; }
/* 4 KB mappings in the image area (4 GB .. 512 GB): programs' and DLLs' images, page by page, so that they take their memory
   from the same pool as everything else (vmem.c) and only as much as they need */
static u64 *pt_for(u64 virt, int create){
  unsigned i3=(virt>>30)&511, i2=(virt>>21)&511;
  if(!(pdpt[i3]&1)){ if(!create) return 0; u64 *pd=kmalloc(4096,4096); if(!pd) return 0; pdpt[i3]=(u64)pd|3; }
  u64 *pd=(u64*)(pdpt[i3]&~0xFFFull);
  if(!(pd[i2]&1) || (pd[i2]&0x80)){ if(!create) return 0; u64 *pt=kmalloc(4096,4096); if(!pt) return 0; pd[i2]=(u64)pt|3; }   /* (a stale 2 MB entry is replaced) */
  return (u64*)(pd[i2]&~0xFFFull);
}
int vm_map_page(u64 virt, u64 phys){
  if(virt<0x100000000ull || (virt>>39)) return -1;
  u64 *pt=pt_for(virt,1); if(!pt) return -1;
  pt[(virt>>12)&511]=phys|3; __asm__ volatile("invlpg (%0)"::"r"(virt):"memory"); return 0;
}
int vm_map_page_ro(u64 virt, u64 phys){                             /* read-only (shared image pages: a write faults and gets its own copy) */
  if(virt<0x100000000ull || (virt>>39)) return -1;
  u64 *pt=pt_for(virt,1); if(!pt) return -1;
  pt[(virt>>12)&511]=phys|1; __asm__ volatile("invlpg (%0)"::"r"(virt):"memory"); return 0;
}
u64 vm_page_entry(u64 virt){                                        /* the page-table entry for virt in the image area (0 if none) */
  if(virt<0x100000000ull || (virt>>39)) return 0;
  u64 *pt=pt_for(virt,0); return pt? pt[(virt>>12)&511] : 0;
}
u64 vm_unmap_page(u64 virt){                                        /* the physical page that was there (0 if none) */
  if(virt<0x100000000ull || (virt>>39)) return 0;
  u64 *pt=pt_for(virt,0); if(!pt) return 0; u64 e=pt[(virt>>12)&511]; if(!(e&1)) return 0;
  pt[(virt>>12)&511]=0; __asm__ volatile("invlpg (%0)"::"r"(virt):"memory"); return e&0x000FFFFFFFFFF000ull;
}
/* the physical 2 MB page behind a vm_map'ed virtual address (0 if none), and removing it */
u64 vm_unmap2m(u64 virt){
  unsigned i3=(virt>>30)&511, i2=(virt>>21)&511; if(virt<0x100000000ull || (virt>>39) || !(pdpt[i3]&1)) return 0;
  u64 *pd=(u64*)(pdpt[i3]&~0xFFFull); u64 e=pd[i2]; if(!(e&1) || !(e&0x80)) return 0;
  pd[i2]=0; __asm__ volatile("invlpg (%0)"::"r"(virt):"memory"); return e&0x000FFFFFFFE00000ull;
}
static u64 heap_free_bytes;                                         /* capacity sitting in the heap's free lists (kept by heap_alloc/heap_free) */
u64 mm_free(void){                                                  /* what can still be handed out: untouched RAM plus freed memory waiting for reuse */
  return (hlimit_>hcur? hlimit_-hcur : 0)+mm_high_free()+heap_free_bytes+vmem_free_pages()*4096;
}
u64 mm_total(void){ return hlimit_+hi_len; }

/* General heap with free(): power-of-two size classes up to 256 KB, 4 KB-granular blocks above that (reused when they fit within 2x).
   Header (16 bytes before the payload): [0]=capacity, [1]=requested size. Memory is always returned zeroed. */
#define HCLASSES 24
static void *hfree_[HCLASSES]; static void *hbig_;                 /* free lists: hbig_ holds big blocks linked through their first payload word */
static int hclass(u64 n){ int c=5; while(((u64)1<<c)<n) c++; return c; }
void *heap_alloc(u64 n){
  if(!n) n=1;
  u64 cap; int cl=-1;
  if(n<=(256u<<10)){ cl=hclass(n); cap=(u64)1<<cl; }
  else cap=(n+4095)&~4095ull;
  void *blk=NULL;
  if(cl>=0){ if(hfree_[cl]){ blk=hfree_[cl]; hfree_[cl]=*(void**)blk; heap_free_bytes-=cap; } }
  else { void **pp=&hbig_; while(*pp){ u64 c=((u64*)*pp)[-2]; if(c>=cap && c<=cap*2){ blk=*pp; *pp=*(void**)blk; cap=c; heap_free_bytes-=c; break; } pp=(void**)*pp; } }
  if(!blk){ u64 *h=kmalloc(cap+16,16);
    if(!h && hi_len){ u64 need=(cap+16+4095)&~4095ull; if(hi_cur+need<=hi_len-hi_top){ h=(u64*)(HWIN+hi_cur); hi_cur+=need; } }   /* low heap full: RAM above 4 GB */
    if(!h) return NULL; h[0]=cap; blk=h+2; }
  ((u64*)blk)[-1]=n; memset(blk,0,n); return blk;
}
/* Program allocations (HeapAlloc, malloc, LocalAlloc, CoTaskMemAlloc ...): the same heap, but each block is on its process's list
   and freed when the process ends - a program that exits (or crashes) without freeing must not keep the memory. Layout: an ordinary
   block holding [prev][next][PROG tag | pid][size], then the program's bytes; the tag sits where heap_free/heap_size/heap_realloc
   look for a block's capacity and size, so they recognise program blocks whoever frees them. */
#define PROG_TAG 0x50524F4700000000ull                               /* "PROG" in the high half: no real capacity is that big */
static u64 *plist[64];                                              /* per process (index = pid) */
static int is_prog(void *p){ return (((u64*)p)[-2]&0xFFFFFFFF00000000ull)==PROG_TAG; }
void *heap_alloc_prog(u64 n){ extern struct proc *cur; return heap_alloc_for(cur? cur->pid : 0,n); }
void *heap_alloc_for(int pid, u64 n);
void *heap_alloc_for64(int pid, u64 n){ u8 *p=heap_alloc_for(pid,n+64); return p? (void*)(((u64)p+63)&~63ull) : 0; }   /* 64-byte aligned (TLS blocks); freed only with the process */
void *heap_alloc_for(int pid, u64 n){                               /* owned by process pid (0: the kernel's, never freed automatically) */
  if(pid<=0 || pid>=64) return heap_alloc(n);
  u64 *b=heap_alloc(n+32); if(!b) return NULL;
  b[0]=0; b[1]=(u64)plist[pid]; if(plist[pid]) plist[pid][0]=(u64)b; plist[pid]=b;
  b[2]=PROG_TAG|(u64)pid; b[3]=n; return b+4;
}
static void prog_unlink(u64 *b){
  int pid=(int)(b[2]&0xFFFFFFFF); u64 *pv=(u64*)b[0], *nx=(u64*)b[1];
  if(pv) pv[1]=(u64)nx; else if(pid>0 && pid<64 && plist[pid]==b) plist[pid]=nx;
  if(nx) nx[0]=(u64)pv;
}
void heap_free_process(int pid){                                    /* a process ended: everything it allocated and did not free */
  if(pid<=0 || pid>=64) return;
  while(plist[pid]){ u64 *b=plist[pid]; plist[pid]=(u64*)b[1]; b[2]=0; heap_free(b); }
}
void heap_free(void *p){
  if(!p) return;
  if(is_prog(p)){ u64 *b=(u64*)p-4; prog_unlink(b); b[2]=0; heap_free(b); return; }
  u64 cap=((u64*)p)[-2]; heap_free_bytes+=cap;
  if(cap<=(256u<<10)){ int cl=hclass(cap); *(void**)p=hfree_[cl]; hfree_[cl]=p; }
  else { *(void**)p=hbig_; hbig_=p; }
}
u64 heap_size(void *p){ return p? ((u64*)p)[-1] : 0; }
void *heap_realloc(void *p, u64 n){
  if(!p) return heap_alloc(n);
  if(is_prog(p)){                                                    /* a program block stays one (same owner) */
    u64 *b=(u64*)p-4, old=b[3]; u64 cap=((u64*)b)[-2]-32;
    if(n<=cap){ if(n>old) memset((u8*)p+old,0,n-old); b[3]=n; return p; }
    int pid=(int)(b[2]&0xFFFFFFFF); extern struct proc *cur; struct proc *sv=cur; extern struct proc procs[]; cur=&procs[pid];
    void *q=heap_alloc_prog(n); cur=sv; if(!q) return NULL; memcpy(q,p,old<n?old:n); heap_free(p); return q;
  }
  u64 cap=((u64*)p)[-2], old=((u64*)p)[-1];
  if(n<=cap){ if(n>old) memset((u8*)p+old,0,n-old); ((u64*)p)[-1]=n; return p; }
  void *q=heap_alloc(n); if(!q) return NULL; memcpy(q,p,old<n?old:n); heap_free(p); return q;
}

/* Make [phys, phys+len) write-combining (video memory). The 3-4 GB range is uncached for device registers, which is far too slow for copying a
   frame once the CPU honours it (KVM, real hardware). PAT entry 1 becomes WC; only the 2 MB pages covering the framebuffer use it. */
void mm_set_wc(u64 phys, u64 len){
  u32 lo, hi; __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0x277));
  lo=(lo&~0xFF00u)|0x0100u;                                       /* entry 1 (PWT=1, PCD=0, PAT=0): write-combining */
  __asm__ volatile("wrmsr"::"a"(lo),"d"(hi),"c"(0x277));
  for(u64 a=phys&~0x1FFFFFull; a<phys+len; a+=0x200000){
    unsigned i3=(a>>30)&511, i2=(a>>21)&511; u64 pd=pdpt[i3]; if(!(pd&1)) continue;
    u64 *e=&((u64*)(pd&~0xFFFull))[i2]; if(!(*e&0x80)) continue; *e=(*e&~0x18ull)|0x08;     /* PWT only */
  }
  u64 cr3; __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3":"=r"(cr3)::"memory");
}

/* Unmap the first 4 KB so that NULL pointer reads, writes and calls fault (and are reported) instead of silently using the BIOS data
   there. The first 2 MB page becomes 512 small pages. The one value the kernel needs from that page (the EBDA segment, for ACPI) is kept. */
u16 bda_ebda_seg;
void mm_guard_null(void){
  bda_ebda_seg=*(volatile u16*)0x40E;
  u64 *pd=(u64*)(pdpt[0]&~0xFFFull); u64 *pt=kmalloc(4096,4096); if(!pt) return;
  for(int i=0;i<512;i++) pt[i]=((u64)i<<12)|3;
  pt[0]=0;
  pd[0]=(u64)pt|3;
  u64 cr3; __asm__ volatile("mov %%cr3,%0; mov %0,%%cr3":"=r"(cr3)::"memory");
}

/* Ctrl+F12: where the RAM is (MB): the low bump heap's high-water mark, what heap_free() holds in its free lists, high RAM */
void mm_report(void){
  u64 fl=0; for(int c=0;c<HCLASSES;c++) for(void *p=hfree_[c];p;p=*(void**)p) fl+=(u64)1<<c;
  u64 big=0; for(void *p=hbig_;p;p=*(void**)p) big+=((u64*)p)[-2];
  kdbg("  RAM: low heap used up to %u MB of %u (free lists hold %u MB + %u MB big blocks); above 4 GB: %u MB pages, %u MB image chunks in use, %u MB chunks free, %u MB untouched\n",
       (u32)(hcur>>20),(u32)(limit>>20),(u32)(fl>>20),(u32)(big>>20),(u32)(hi_cur>>20),(u32)(hi_top>>20)-(u32)((u64)nchunk_free*2),(u32)nchunk_free*2,(u32)((hi_len-hi_top-hi_cur)>>20));
}
