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
u64 mm_free(void){ return hlimit_>hcur? hlimit_-hcur : 0; }
u64 mm_total(void){ return hlimit_; }

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
  if(cl>=0){ if(hfree_[cl]){ blk=hfree_[cl]; hfree_[cl]=*(void**)blk; } }
  else { void **pp=&hbig_; while(*pp){ u64 c=((u64*)*pp)[-2]; if(c>=cap && c<=cap*2){ blk=*pp; *pp=*(void**)blk; cap=c; break; } pp=(void**)*pp; } }
  if(!blk){ u64 *h=kmalloc(cap+16,16); if(!h) return NULL; h[0]=cap; blk=h+2; }
  ((u64*)blk)[-1]=n; memset(blk,0,n); return blk;
}
void heap_free(void *p){
  if(!p) return; u64 cap=((u64*)p)[-2];
  if(cap<=(256u<<10)){ int cl=hclass(cap); *(void**)p=hfree_[cl]; hfree_[cl]=p; }
  else { *(void**)p=hbig_; hbig_=p; }
}
u64 heap_size(void *p){ return p? ((u64*)p)[-1] : 0; }
void *heap_realloc(void *p, u64 n){
  if(!p) return heap_alloc(n);
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
