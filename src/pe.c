#include "kernel.h"
/* PE32+ (x64) loader: maps sections (at ImageBase, even above 4GB), applies relocations, binds imports, sets up TLS info. */
#pragma pack(push,1)
struct dos_hdr { u16 magic; u8 pad[58]; u32 lfanew; };
struct coff { u16 machine, nsec; u32 ts, symtab, nsym; u16 optsz, chars; };
struct datadir { u32 rva, size; };
struct opt64 { u16 magic; u8 maj, min; u32 codesz, initsz, uninitsz, entry, codebase; u64 imagebase;
  u32 secalign, filealign; u16 osmaj, osmin, imaj, imin, ssmaj, ssmin; u32 win32ver, imagesz, hdrsz, csum;
  u16 subsys, dllchars; u64 stkres, stkcom, heapres, heapcom; u32 loaderflags, ndirs; struct datadir dir[16]; };
struct section { char name[8]; u32 vsize, va, rawsz, rawptr, reloc, line; u16 nreloc, nline; u32 chars; };
struct import_desc { u32 oft, ts, fwd, name, ft; };
struct tls_dir { u64 start, end, index, callbacks; u32 zero, chars; };
#pragma pack(pop)

/* Unresolved imports get a stub that reports the missing API:  mov rcx,name; mov rax,unimpl; jmp rax */
static void WINAPI unimpl(const char *what){
  kprintf("\n*** Unimplemented Windows API called: %s\n", what);
  proc_exit(0xC0000139);
}
/* Optional APIs found by GetProcAddress that Willows lacks: log the call once and return 0 (like a failing Windows call). */
static int has_prefix(const char *s, const char *p){ while(*p){ if(*s++ != *p++) return 0; } return 1; }
static u64 WINAPI soft_unimpl(const char *what){
  static const char *seen[48]; static int ns; int first = 1; for(int i = 0; i < ns; i++) if(seen[i] == what) first = 0;
  if(first && ns < 48){ seen[ns++] = what; kdbg("  [soft] unimplemented %s called, returning failure\n", what); }
  /* failure codes that callers recognise: NTSTATUS for the native API, HRESULT for COM/WinRT, CR_FAILURE for the config manager */
  if(has_prefix(what,"Nt")||has_prefix(what,"Zw")||has_prefix(what,"Ldr")||has_prefix(what,"Rtl")||has_prefix(what,"Wmi")||has_prefix(what,"Perf")||has_prefix(what,"D3DKMT")||has_prefix(what,"Lsa")) return 0xC00000BBull;
  if(has_prefix(what,"Ro")||has_prefix(what,"Windows")||has_prefix(what,"SHGet")||has_prefix(what,"Co")||has_prefix(what,"StrRet")||has_prefix(what,"GetDpiFor")) return 0x80004001ull;
  if(has_prefix(what,"CM_")) return 0x13;
  static const char *const win32err[] = {"ConvertInterfaceIndexToLuid","ConvertInterfaceLuidToGuid","GetIfEntry2","GetIpForwardTable2","GetIpInterfaceEntry","GetUnicastIpAddressTable","QueryDisplayConfig","DisplayConfigGetDeviceInfo","GetAddrInfoW","FreeMibTable"};
  for(unsigned i=0;i<sizeof win32err/sizeof *win32err;i++){ const char *a=what,*b=win32err[i]; while(*a && *a==*b){a++;b++;} if(!*a && !*b) return 50; }       /* ERROR_NOT_SUPPORTED */
  if(has_prefix(what,"WSAIoctl")||has_prefix(what,"WSARecv")||has_prefix(what,"WSASocket")) return (u64)-1;
  return 0;
}
void *pe_make_soft_stub(const char *name){
  u8 *p = kmalloc(32, 16); if(!p) return NULL; static char buf[16384]; static u32 used; u32 n = strlen(name) + 1; if(used + n > sizeof buf) used = 0;
  char *s = buf + used; used += n; memcpy(s, name, n);
  p[0] = 0x48; p[1] = 0xB9; *(u64*)(p + 2) = (u64)s; p[10] = 0x48; p[11] = 0xB8; *(u64*)(p + 12) = (u64)soft_unimpl; p[20] = 0xFF; p[21] = 0xE0; return p;
}
void *pe_make_stub(const char *dll, const char *name){
  u8 *p=kmalloc(32,16); if(!p) return NULL;
  static char buf[65536]; static u32 used;
  u32 n=strlen(dll)+strlen(name)+2;
  if(used+n>sizeof buf) used=0;
  char *s=buf+used; used+=n; u32 i=0; for(const char *d=dll;*d;) s[i++]=*d++; s[i++]='!'; for(const char *d=name;*d;) s[i++]=*d++; s[i]=0;
  p[0]=0x48; p[1]=0xB9; *(u64*)(p+2)=(u64)s;                 /* mov rcx, imm64 */
  p[10]=0x48; p[11]=0xB8; *(u64*)(p+12)=(u64)unimpl;         /* mov rax, imm64 */
  p[20]=0xFF; p[21]=0xE0;                                    /* jmp rax */
  return p;
}

extern u64 kernel_end_addr, mod_lo, mod_hi;
static struct { u64 base, span; } mapped[96]; static int nmapped; int pe_owner; static u64 next_va=0x200000000ull;
static int overlaps(u64 b, u64 n){ for(int i=0;i<nmapped;i++) if(b<mapped[i].base+mapped[i].span && b+n>mapped[i].base) return 1; return 0; }

void pe_unload(u64 base){                                          /* free the virtual range when a process exits */
  for(int i=0;i<nmapped;i++) if(mapped[i].base==base){ mapped[i]=mapped[--nmapped]; return; }
}

int pe_load(const void *file, u64 size, struct pe_image *out){ return pe_load_ex(file,size,out,0); }
int pe_load_ex(const void *file, u64 size, struct pe_image *out, int isdll){
  const u8 *f=file; const struct dos_hdr *dh=file;
  if(size<sizeof *dh || dh->magic!=0x5A4D){ kputs("pe: no MZ header\n"); return -1; }
  if(dh->lfanew+4+sizeof(struct coff)+sizeof(struct opt64)>size || *(u32*)(f+dh->lfanew)!=0x4550){ kputs("pe: bad PE signature\n"); return -1; }
  const struct coff *c=(void*)(f+dh->lfanew+4);
  const struct opt64 *o=(void*)(c+1);
  if(c->machine==0x14C){ kputs("pe: this is a 32-bit (x86) program; Willows is now a 64-bit OS and only runs PE32+ (x64)\n"); return -1; }
  if(c->machine!=0x8664 || o->magic!=0x20B){ kputs("pe: not an x64 PE32+ executable\n"); return -1; }
  const struct section *sec=(void*)((u8*)o+c->optsz);

  u64 want=o->imagebase, vbase; u8 *img;
  u64 span=(o->imagesz+0x1FFFFF)&~0x1FFFFFull;
  int can_reloc=o->dir[5].rva!=0;
  if(want>=0x100000000ull && !(want&0x1FFFFF)){                       /* e.g. 0x140000000: map fresh RAM there */
    u64 va=want;
    if(overlaps(want,span)){                                          /* another copy already lives there */
      if(!can_reloc){ kputs("pe: its base address is taken and it has no relocations, so a second copy cannot run\n"); return -1; }
      va=next_va; next_va+=span+0x200000;
    }
    img=kmalloc(span,0x200000);
    if(!img || vm_map(va,(u64)img,span)){ kputs("pe: cannot map image\n"); return -1; }
    vbase=va; if(nmapped<96){ mapped[nmapped].base=va; mapped[nmapped].span=span; nmapped++; }
  } else if(want>=0x400000 && want+o->imagesz<0x1000000 && !(want<mod_hi && want+o->imagesz>mod_lo) && want>=kernel_end_addr && !overlaps(want,o->imagesz)){
    img=(u8*)want; memset(img,0,o->imagesz); vbase=want;                 /* low base is identity-mapped */
    if(nmapped<96){ mapped[nmapped].base=want; mapped[nmapped].span=o->imagesz; nmapped++; }
  } else {
    img=kmalloc(o->imagesz,0x1000); vbase=(u64)img;                      /* relocate into the heap */
    if(!img){ kputs("pe: out of memory\n"); return -1; }
  }
  memcpy(img,f,o->hdrsz);
  for(int i=0;i<c->nsec;i++){
    u32 n=sec[i].rawsz<sec[i].vsize?sec[i].rawsz:sec[i].vsize;
    if(sec[i].rawptr+(u64)n>size||sec[i].va+(u64)sec[i].vsize>o->imagesz){ kputs("pe: bad section\n"); return -1; }
    memcpy(img+sec[i].va,f+sec[i].rawptr,n);
  }
  i64 delta=(i64)(vbase-want);
  if(delta){
    if(!o->dir[5].rva){ kputs("pe: needs relocation but has none\n"); return -1; }
    u8 *r=img+o->dir[5].rva, *end=r+o->dir[5].size;
    while(r<end){
      u32 page=*(u32*)r, blk=*(u32*)(r+4); if(!blk) break;
      for(u16 *e=(u16*)(r+8); (u8*)e<r+blk; e++){
        u8 *at=img+page+(*e&0xFFF);
        if((*e>>12)==10) *(u64*)at+=delta; else if((*e>>12)==3) *(u32*)at+=(u32)delta;
      }
      r+=blk;
    }
  }
  if(o->dir[1].rva){
    for(const struct import_desc *d=(void*)(img+o->dir[1].rva); d->name; d++){
      const char *dll=(char*)img+d->name;
      u64 *lk=(u64*)(img+(d->oft?d->oft:d->ft)), *iat=(u64*)(img+d->ft);
      for(;*lk;lk++,iat++){
        const char *fn; char ord[12];
        if(*lk>>63){ u32 v=*lk&0xFFFF; int n=0; ord[n++]='#'; char t[6]; int k=0; do{t[k++]='0'+v%10;v/=10;}while(v); while(k) ord[n++]=t[--k]; ord[n]=0; fn=ord; }
        else fn=(char*)img+(*lk&0x7FFFFFFF)+2;
        void *a=win32_resolve(dll,fn); if(!a) a=dll_import(dll,fn,pe_owner);
        if(!a){ static int nmiss; if(++nmiss<=6) kdbg("  [import] %s!%s  -> not available (returns failure)\n",dll,fn); a=pe_make_soft_stub(fn); }
        *iat=(u64)a;
      }
    }
  }
  if(o->dir[13].rva){                                                    /* delay-load imports: bound eagerly */
    struct delay_desc { u32 attrs, name, hmod, iat, int_, bound, unload, ts; };
    for(const struct delay_desc *d=(void*)(img+o->dir[13].rva); d->name; d++){
      const char *dll=(char*)img+d->name; u64 *lk=(u64*)(img+d->int_), *iat=(u64*)(img+d->iat);
      for(;*lk;lk++,iat++){
        const char *fn; char ord[12];
        if(*lk>>63){ u32 v=*lk&0xFFFF; int n=0; ord[n++]='#'; char t[6]; int k=0; do{t[k++]='0'+v%10;v/=10;}while(v); while(k) ord[n++]=t[--k]; ord[n]=0; fn=ord; }
        else fn=(char*)img+(*lk&0x7FFFFFFF)+2;
        void *a=win32_resolve(dll,fn); if(!a) a=dll_import(dll,fn,pe_owner); if(!a) a=pe_make_soft_stub(fn);
        *iat=(u64)a;
      }
    }
  }
  memset(out,0,sizeof *out);
  out->exp_rva=o->dir[0].rva; out->exp_size=o->dir[0].size;
  if(isdll){ out->base=vbase; out->entry=o->entry? vbase+o->entry : 0; return 0; }
  if(o->dir[9].rva){                                                     /* TLS directory */
    const struct tls_dir *t=(void*)(img+o->dir[9].rva);
    out->tls_start=t->start; out->tls_end=t->end; out->tls_index=t->index; out->tls_callbacks=t->callbacks; out->tls_zero=t->zero;
  }
  u64 stk=o->stkres? (o->stkres>0x100000?0x100000:o->stkres) : 0x100000;
  u8 *stack=kmalloc(stk,16); if(!stack){ kputs("pe: no stack memory\n"); return -1; }
  out->base=vbase; out->entry=vbase+o->entry; out->stack_top=(u64)stack+stk-64; out->stack_lo=(u64)stack;
  return 0;
}
