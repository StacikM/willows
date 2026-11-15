#include "kernel.h"
/* PE32+ (x64) loader: maps sections (at ImageBase, even above 4GB), applies relocations, binds imports, sets up TLS info. */
#pragma pack(push,1)
struct dos_hdr { u16 magic; u8 pad[58]; u32 lfanew; };
struct coff { u16 machine, nsec; u32 ts, symtab, nsym; u16 optsz, chars; };
struct datadir { u32 rva, size; };
struct opt64 { u16 magic; u8 maj, min; u32 codesz, initsz, uninitsz, entry, codebase; u64 imagebase;
  u32 secalign, filealign; u16 osmaj, osmin, imaj, imin, ssmaj, ssmin; u32 win32ver, imagesz, hdrsz, csum;
  u16 subsys, dllchars; u64 stkres, stkcom, heapres, heapcom; u32 loaderflags, ndirs; struct datadir dir[16]; };
struct opt32 { u16 magic; u8 maj, min; u32 codesz, initsz, uninitsz, entry, codebase, database, imagebase, secalign, filealign; u16 osmaj, osmin, imaj, imin, ssmaj, ssmin; u32 win32ver, imagesz, hdrsz, csum;
  u16 subsys, dllchars; u32 stkres, stkcom, heapres, heapcom, loaderflags, ndirs; struct datadir dir[16]; };
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
  static const char *seen[1024]; static int ns; int first = 1; for(int i = 0; i < ns; i++) if(seen[i] == what) first = 0;
  if(first && ns < 1024){ seen[ns++] = what; char at[64]; if(!dll_addr_name((u64)__builtin_return_address(0), at, sizeof at)) ksnprintf(at, sizeof at, "%p", __builtin_return_address(0));
    kdbg("  [soft] unimplemented %s called from %s, returning failure\n", what, at); }
  /* failure codes that callers recognise: NTSTATUS for the native API, HRESULT for COM/WinRT, CR_FAILURE for the config manager */
  if(has_prefix(what,"Nt")||has_prefix(what,"Zw")||has_prefix(what,"Ldr")||has_prefix(what,"Rtl")||has_prefix(what,"Wmi")||has_prefix(what,"Perf")||has_prefix(what,"D3DKMT")||has_prefix(what,"Lsa")) return 0xC00000BBull;
  if(has_prefix(what,"Ro")||has_prefix(what,"Windows")||has_prefix(what,"SHGet")||has_prefix(what,"Co")||has_prefix(what,"StrRet")||has_prefix(what,"GetDpiFor")) return 0x80004001ull;
  if(has_prefix(what,"CM_")) return 0x13;
  /* graphics, media and shell COM entry points return HRESULTs: 0 would be S_OK with a NULL object (Firefox then retries forever) */
  if(has_prefix(what,"D3D")||has_prefix(what,"CreateDXGI")||has_prefix(what,"DXGI")||has_prefix(what,"D2D1")||has_prefix(what,"DComposition")||has_prefix(what,"Dwm")
     ||has_prefix(what,"MF")||has_prefix(what,"Direct3D")||has_prefix(what,"DXVA")||has_prefix(what,"Wer")||has_prefix(what,"Uia")||has_prefix(what,"CreateDirect")
     ||has_prefix(what,"Ole")||has_prefix(what,"GetScaleFactorFor")) return 0x887A0004ull;   /* DXGI_ERROR_UNSUPPORTED (a failure HRESULT) */
  static const char *const win32err[] = {"ConvertInterfaceIndexToLuid","ConvertInterfaceLuidToGuid","GetIfEntry2","GetIpForwardTable2","GetIpInterfaceEntry","GetUnicastIpAddressTable","QueryDisplayConfig","DisplayConfigGetDeviceInfo","GetDisplayConfigBufferSizes","GetAddrInfoW","FreeMibTable"};
  for(unsigned i=0;i<sizeof win32err/sizeof *win32err;i++){ const char *a=what,*b=win32err[i]; while(*a && *a==*b){a++;b++;} if(!*a && !*b) return 50; }       /* ERROR_NOT_SUPPORTED */
  if(has_prefix(what,"WSAIoctl")||has_prefix(what,"WSARecv")||has_prefix(what,"WSASocket")) return (u64)-1;
  /* functions whose failure value is INVALID_HANDLE_VALUE: 0 would look like a valid handle */
  if(has_prefix(what,"SetupDiGetClassDevs")||has_prefix(what,"SetupDiCreateDeviceInfoList")||has_prefix(what,"CreateFile")||has_prefix(what,"FindFirst")
     ||has_prefix(what,"CreateNamedPipe")||has_prefix(what,"CreateMailslot")||has_prefix(what,"CreateToolhelp32Snapshot")||has_prefix(what,"CreateConsoleScreenBuffer")) return (u64)-1;
  return 0;
}
void *pe_make_soft_stub(const char *name){                 /* one stub per name, kept for good (GetProcAddress may ask again and again) */
  static struct { const char *name; void *stub; } made[2048]; static int nmade;
  for(int i = 0; i < nmade; i++) if(!strcmp(made[i].name, name)) return made[i].stub;
  u32 n = strlen(name) + 1; u8 *p = kmalloc(32, 16); char *s = kmalloc(n, 1); if(!p || !s) return NULL; memcpy(s, name, n);
  p[0] = 0x48; p[1] = 0xB9; *(u64*)(p + 2) = (u64)s; p[10] = 0x48; p[11] = 0xB8; *(u64*)(p + 12) = (u64)soft_unimpl; p[20] = 0xFF; p[21] = 0xE0;
  if(nmade < 2048){ made[nmade].name = s; made[nmade].stub = p; nmade++; }
  return p;
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

/* Bind one import. The C++ runtime DLLs (msvcp*, vcruntime*, concrt*) come from the program's own copy when it ships one, so that all
   of their functions share one state; otherwise Willows' built-in versions serve them. */
static void *trace_stub(const char *dll, const char *fn, void *target);       /* API call trace, below */
static int lower_eq_prefix(const char *s, const char *p){ for(;*p;s++,p++) if((*s|32)!=*p) return 0; return 1; }
static void *bind_import(const char *dll, const char *fn){
  void *a=0;
  if(lower_eq_prefix(dll,"msvcp") || lower_eq_prefix(dll,"vcruntime") || lower_eq_prefix(dll,"concrt")) a=dll_import(dll,fn,pe_owner);
  if(!a) a=win32_resolve(dll,fn);
  if(!a) a=dll_import(dll,fn,pe_owner);
  return a;
}
/* ---- API call trace (debugging): with C:\System\apitrace.cfg present, imports are bound through a stub that records the function
   name in api_ring, then jumps to the function. Only rax, r10 and r11 are used (scratch registers that never carry arguments).
   Exception reports print the newest entries (idt.c). ---- */
u64 api_ring[1+8192];                                                /* [0] = number of calls so far, then 8192 entries (pid<<56 | name) */
static int trace_wanted(void){ struct ramfile *f=fs_find("/System/apitrace.cfg"); return f && !f->isdir; }
extern char kernel_text_end[];
static int is_code(u64 a){                                           /* only functions are traced: imports can also be variables */
  if(a>=0x100000 && a<(u64)kernel_text_end) return 1;               /* a built-in function */
  u64 base; if(!pe_find_mapping(a,&base,0)) return 0;
  const u8 *b=(const u8*)base; if(b[0]!='M'||b[1]!='Z') return 0;
  const u8 *pe=b+*(const u32*)(b+0x3C); u16 nsec=*(const u16*)(pe+6), optsz=*(const u16*)(pe+20); const u8 *s=pe+24+optsz;
  for(u16 i=0;i<nsec;i++,s+=40){ u32 va=*(const u32*)(s+12), vs=*(const u32*)(s+8); if(a-base>=va && a-base<va+vs) return (*(const u32*)(s+36)&0x20000000)!=0; }
  return 0;
}
static int noisy(const char *dll, const char *fn){                  /* very frequent calls that would push everything else out of the trace */
  static const char *const dlls[]={"api-ms-win-crt-string","api-ms-win-crt-heap","vcruntime","api-ms-win-crt-math","api-ms-win-crt-convert"};
  static const char *const fns[]={"malloc","free","moz_xmalloc","moz_xrealloc","moz_xcalloc","realloc","calloc","TlsGetValue","TlsSetValue","FlsGetValue",
    "EnterCriticalSection","LeaveCriticalSection","AcquireSRWLockExclusive","ReleaseSRWLockExclusive","AcquireSRWLockShared","ReleaseSRWLockShared",
    "PR_GetCurrentThread","QueryPerformanceCounter","GetLastError","SetLastError","GetCurrentThreadId","PR_Lock","PR_Unlock",
    "timeGetTime","PR_IntervalNow","PR_MillisecondsToInterval","MonitorFromWindow","GetTickCount","GetTickCount64","PR_GetThreadPrivate","PR_SetThreadPrivate"};
  for(unsigned i=0;i<sizeof dlls/sizeof *dlls;i++) if(lower_eq_prefix(dll,dlls[i])) return 1;
  for(unsigned i=0;i<sizeof fns/sizeof *fns;i++) if(!strcmp(fn,fns[i])) return 1;
  static const char *const parts[]={"_Query_perf","ToPrecision","ToShortest","moz_xstrdup","MutexImpl","ConditionVariableImpl","TimeStamp","RWLockImpl","ToSeconds","profiler_current_thread"};   /* C++ names: by substring */
  for(unsigned i=0;i<sizeof parts/sizeof *parts;i++) for(const char *p=fn;*p;p++){ const char *a=p, *b=parts[i]; while(*b && *a==*b){ a++; b++; } if(!*b) return 1; }
  return 0;
}
static void *trace_stub(const char *dll, const char *fn, void *target){
  if(!is_code((u64)target) || noisy(dll,fn)) return target;
  u8 *p=kmalloc(96,16); u32 dl=strlen(dll), fl=strlen(fn); char *name=kmalloc(dl+fl+2,1); if(!p || !name) return target;
  memcpy(name,dll,dl); name[dl]='!'; memcpy(name+dl+1,fn,fl+1);
  u32 k=0;
  p[k++]=0x49; p[k++]=0xBB; *(u64*)(p+k)=(u64)api_ring; k+=8;       /* movabs r11, api_ring */
  p[k++]=0x49; p[k++]=0x8B; p[k++]=0x03;                              /* mov rax, [r11] */
  p[k++]=0x48; p[k++]=0xFF; p[k++]=0xC0;                              /* inc rax */
  p[k++]=0x49; p[k++]=0x89; p[k++]=0x03;                              /* mov [r11], rax */
  p[k++]=0x25; *(u32*)(p+k)=8191; k+=4;                              /* and eax, 8191 */
  p[k++]=0x65; p[k++]=0x4C; p[k++]=0x8B; p[k++]=0x14; p[k++]=0x25; *(u32*)(p+k)=0x40; k+=4;   /* mov r10, gs:[0x40]  (process id, from the TEB) */
  p[k++]=0x49; p[k++]=0xC1; p[k++]=0xE2; p[k++]=56;                   /* shl r10, 56 */
  p[k++]=0x4D; p[k++]=0x89; p[k++]=0x54; p[k++]=0xC3; p[k++]=0x08;    /* mov [r11+rax*8+8], r10 */
  p[k++]=0x49; p[k++]=0xBA; *(u64*)(p+k)=(u64)name; k+=8;            /* movabs r10, name */
  p[k++]=0x4D; p[k++]=0x09; p[k++]=0x54; p[k++]=0xC3; p[k++]=0x08;    /* or [r11+rax*8+8], r10   -> entry = pid<<56 | name */
  p[k++]=0x49; p[k++]=0xBB; *(u64*)(p+k)=(u64)target; k+=8;          /* movabs r11, target */
  p[k++]=0x41; p[k++]=0xFF; p[k++]=0xE3;                              /* jmp r11 */
  return p;
}
void *pe_trace_wrap(const char *dll, const char *fn, void *target){ return (target && trace_wanted())? trace_stub(dll,fn,target) : target; }   /* GetProcAddress results */
static void *soft_stub_for(const char *dll, const char *fn){       /* imports by ordinal are named "dll!#n" so the log says which DLL */
  void *s;
  if(fn[0]!='#') s=pe_make_soft_stub(fn);
  else { char b[64]; ksnprintf(b,sizeof b,"%s!%s",dll,fn); s=pe_make_soft_stub(b); }
  return s;
}
extern u64 kernel_end_addr, mod_lo, mod_hi;
#define MAXMAPPED 512
struct share;
static struct { u64 base, span; struct share *sh; } mapped[MAXMAPPED]; static int nmapped; int pe_owner; static u64 next_va=0x200000000ull;
static void add_mapped(u64 base, u64 span, struct share *sh){ if(nmapped<MAXMAPPED){ mapped[nmapped].base=base; mapped[nmapped].span=span; mapped[nmapped].sh=sh; nmapped++; } }
/* ---- pages shared between copies of the same DLL or program ----
   Every process loads its own copy of each image (one address space: two copies cannot be at the same address). But most of an image
   - code, read-only data without relocations - comes out byte for byte the same wherever it is loaded. After a copy is loaded, each
   page of its read-only sections is compared with the shared page for that file: if equal, the copy maps the shared page instead
   (read-only; CR0.WP makes a write fault, and pe_cow_fault gives the writer a private copy). The first copy provides the shared pages.
   For Firefox this keeps one copy of xul.dll's 150 MB of code for all its processes instead of one each. */
#define MAXSHARE 96
static struct share { const void *file; u64 fsize; u32 npages; int users; u64 *ph; } shares[MAXSHARE];
static struct share *share_get(const void *file, u64 fsize, u32 imagesz){
  for(int i=0;i<MAXSHARE;i++) if(shares[i].users>0 && shares[i].file==file && shares[i].fsize==fsize) return &shares[i];
  for(int i=0;i<MAXSHARE;i++) if(shares[i].users<=0){ struct share *s=&shares[i]; u32 np=(imagesz+4095)/4096;
    u64 *ph=heap_alloc((u64)np*8); if(!ph) return 0; s->file=file; s->fsize=fsize; s->npages=np; s->users=0; s->ph=ph; return s; }
  return 0;
}
static void share_put(struct share *s){                              /* an image using it was unloaded */
  if(!s || --s->users>0) return;
  for(u32 i=0;i<s->npages;i++) if(s->ph[i]) vmem_page_free_phys(s->ph[i]);
  heap_free(s->ph); s->ph=0; s->users=0; s->file=0;
}
static void share_pages(struct share *s, u8 *img, const struct section *sec, int nsec, u32 imagesz){
  for(int i=0;i<nsec;i++){
    if(sec[i].chars&0x80000000u) continue;                           /* writable sections stay private */
    u32 a=sec[i].va, e=sec[i].va+sec[i].vsize; if(e>imagesz) e=imagesz;
    for(u32 p=a; p+4096<=e; p+=4096){                                 /* whole pages only (sections are page-aligned in practice) */
      u32 pg=p/4096; u64 va=(u64)img+p, ent=vm_page_entry(va); if(!(ent&1)) continue; u64 ph=ent&0x000FFFFFFFFFF000ull;
      if(!s->ph[pg]){ s->ph[pg]=ph; vm_map_page_ro(va,ph); continue; }            /* the first copy: its page becomes the shared one */
      if(s->ph[pg]==ph) continue;
      if(!memcmp((void*)va,(void*)mm_p2v(s->ph[pg]),4096)){ vm_map_page_ro(va,s->ph[pg]); vmem_page_free_phys(ph); }
    }
  }
}
static int overlaps(u64 b, u64 n){ for(int i=0;i<nmapped;i++) if(b<mapped[i].base+mapped[i].span && b+n>mapped[i].base) return 1; return 0; }

void pe_unload(u64 base){                                          /* free the virtual range when a process exits */
  for(int i=0;i<nmapped;i++) if(mapped[i].base==base){
    if(base>=0x100000000ull && !(base&0x1FFFFF)){                     /* an image mapped page by page: give its own pages back */
      struct share *s=mapped[i].sh;
      for(u64 off=0; off<mapped[i].span; off+=4096){ u64 ph=vm_unmap_page(base+off); if(!ph) continue;
        u32 pg=(u32)(off/4096); if(s && pg<s->npages && s->ph[pg]==ph) continue;   /* shared: freed with the last copy (share_put) */
        vmem_page_free_phys(ph); }
      share_put(s);
    }
    mapped[i]=mapped[--nmapped]; return; }
}
void pe_register_mapping(u64 base, u64 span){ add_mapped(base,span,0); }
/* page fault: a write to a shared (read-only) image page - the writer gets its own copy, as with Windows' copy-on-write images */
int pe_cow_fault(u64 addr){
  u64 ent=vm_page_entry(addr); if(!(ent&1) || (ent&2)) return 0;
  u64 ph=ent&0x000FFFFFFFFFF000ull, va=addr&~0xFFFull;
  for(int i=0;i<nmapped;i++) if(addr>=mapped[i].base && addr<mapped[i].base+mapped[i].span && mapped[i].sh){
    struct share *s=mapped[i].sh; u32 pg=(u32)((va-mapped[i].base)/4096); if(pg>=s->npages || s->ph[pg]!=ph) return 0;
    u64 np=vmem_page_alloc(); if(!np) return 0;
    memcpy((void*)np,(void*)mm_p2v(ph),4096); return vm_map_page(va,mm_v2p(np))==0;
  }
  return 0;
}
int pe_find_mapping(u64 a, u64 *base, u64 *span){                 /* which loaded image contains address a (VirtualQuery, backtraces) */
  for(int i=0;i<nmapped;i++) if(a>=mapped[i].base && a<mapped[i].base+mapped[i].span){ if(base) *base=mapped[i].base; if(span) *span=mapped[i].span; return 1; }
  return 0;
}

int pe_load(const void *file, u64 size, struct pe_image *out){ return pe_load_ex(file,size,out,0); }
int pe_load_ex(const void *file, u64 size, struct pe_image *out, int isdll){
  const u8 *f=file; const struct dos_hdr *dh=file;
  int tr=isdll && trace_wanted();          /* trace DLLs' imports only: programs such as Firefox check their own import table */
  if(size<sizeof *dh || dh->magic!=0x5A4D){ kputs("pe: no MZ header\n"); return -1; }
  if(dh->lfanew+4+sizeof(struct coff)+sizeof(struct opt64)>size || *(u32*)(f+dh->lfanew)!=0x4550){ kputs("pe: bad PE signature\n"); return -1; }
  const struct coff *c=(void*)(f+dh->lfanew+4);
  const struct opt64 *o=(void*)(c+1);
  if(c->machine==0x14C){ if(isdll){ kputs("pe: 32-bit DLLs are not supported\n"); return -1; } return pe32_load(file,size,out); }      /* a 32-bit program: wow.c / boot.S run it in compatibility mode */
  if(c->machine!=0x8664 || o->magic!=0x20B){ kputs("pe: not an x64 PE32+ executable\n"); return -1; }
  const struct section *sec=(void*)((u8*)o+c->optsz);

  u64 want=o->imagebase, vbase; u8 *img; int paged=0;
  u64 span=(o->imagesz+0x1FFFFF)&~0x1FFFFFull;
  int can_reloc=o->dir[5].rva!=0;
  if(want>=0x100000000ull && !(want&0x1FFFFF)){                       /* e.g. 0x140000000: map fresh RAM there */
    u64 va=want;
    if(overlaps(want,span)){                                          /* another copy already lives there */
      if(!can_reloc){ kputs("pe: its base address is taken and it has no relocations, so a second copy cannot run\n"); return -1; }
      va=next_va; next_va+=span+0x200000;
    }
    u64 need=(o->imagesz+0xFFF)&~0xFFFull;                            /* the 2 MB-aligned span only reserves addresses; pages are mapped as needed */
    for(u64 off=0; off<need; off+=4096){                              /* zeroed pages from the common pool (vmem.c), given back by pe_unload */
      u64 pg=vmem_page_alloc();
      if(!pg || vm_map_page(va+off,mm_v2p(pg))){ if(pg) vmem_page_free_phys(mm_v2p(pg)); for(u64 o2=0;o2<off;o2+=4096){ u64 q=vm_unmap_page(va+o2); if(q) vmem_page_free_phys(q); } kputs("pe: cannot map image (out of memory)\n"); return -1; }
    }
    img=(u8*)va;
    vbase=va; paged=1; add_mapped(va,span,0);                         /* its share is attached once the image is complete (below) */
  } else if(want>=0x400000 && want+o->imagesz<0x1000000 && !(want<mod_hi && want+o->imagesz>mod_lo) && want>=kernel_end_addr && !overlaps(want,o->imagesz)){
    img=(u8*)want; memset(img,0,o->imagesz); vbase=want;                 /* low base is identity-mapped */
    add_mapped(want,o->imagesz,0);
  } else {
    img=kmalloc(o->imagesz,0x1000); vbase=(u64)img;                      /* relocate into the heap */
    if(!img){ kputs("pe: out of memory\n"); return -1; }
    add_mapped(vbase,o->imagesz,0);
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
        void *a=bind_import(dll,fn);
        if(!a){ static int nmiss; if(++nmiss<=6) kdbg("  [import] %s!%s  -> not available (returns failure)\n",dll,fn); a=soft_stub_for(dll,fn); }
        if(tr && a) a=trace_stub(dll,fn,a);
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
        void *a=bind_import(dll,fn); if(!a) a=soft_stub_for(dll,fn); if(tr && a) a=trace_stub(dll,fn,a);
        *iat=(u64)a;
      }
    }
  }
  if(paged && o->secalign>=4096){                                        /* the image is complete: share its read-only pages with other copies */
    struct share *sh=share_get(file,size,o->imagesz);
    if(sh){ sh->users++; share_pages(sh,img,sec,c->nsec,o->imagesz);
      for(int i=nmapped-1;i>=0;i--) if(mapped[i].base==vbase){ mapped[i].sh=sh; break; } }
  }
  memset(out,0,sizeof *out);
  out->exp_rva=o->dir[0].rva; out->exp_size=o->dir[0].size; out->size=o->imagesz;
  if(o->dir[9].rva){                                                     /* TLS directory (programs and DLLs) */
    const struct tls_dir *t=(void*)(img+o->dir[9].rva);
    out->tls_start=t->start; out->tls_end=t->end; out->tls_index=t->index; out->tls_callbacks=t->callbacks; out->tls_zero=t->zero;
  }
  if(isdll){ out->base=vbase; out->entry=o->entry? vbase+o->entry : 0; return 0; }
  u64 stk=o->stkres? o->stkres : 0x100000; if(stk<0x100000) stk=0x100000; if(stk>0x1000000) stk=0x1000000;   /* what the program asks for, 1..16 MB */
  stk=(stk+0xFFFF)&~0xFFFFull;
  u8 *stack=vmem_alloc_stack(stk); if(!stack){ kputs("pe: no stack memory\n"); return -1; }       /* freed when the process slot is reused (proc.c) */
  out->base=vbase; out->entry=vbase+o->entry; out->stack_top=(u64)stack+stk-64; out->stack_lo=(u64)stack;
  return 0;
}

/* ---- 32-bit (PE32) programs: mapped below 4 GB, imports bound to stubs that switch to 64-bit mode (wow.c, boot.S) ---- */
u32 wow_import(const char *dll, const char *name);
int pe32_load(const void *file, u64 size, struct pe_image *out){
  const u8 *f=file; const struct dos_hdr *dh=file; const struct coff *c=(void*)(f+dh->lfanew+4); const struct opt32 *o=(void*)(c+1);
  if(c->optsz<sizeof(struct opt32)-16*8 || o->magic!=0x10B){ kputs("pe: not a valid 32-bit PE file\n"); return -1; }
  const struct section *sec=(void*)((u8*)o+c->optsz);
  u64 want=o->imagebase, vbase; u8 *img; u64 span=o->imagesz; int can_reloc=o->dir[5].rva!=0;
  if(want>=0x400000 && want+span<0x1000000 && !(want<mod_hi && want+span>mod_lo) && want>=kernel_end_addr && !overlaps(want,span)){
    img=(u8*)want; memset(img,0,span); vbase=want; add_mapped(want,span,0);
  } else {
    if(!can_reloc){ kputs("pe: its address range is not free and it has no relocations\n"); return -1; }
    img=kmalloc(span,0x1000); if(!img){ kputs("pe: out of memory\n"); return -1; } vbase=(u64)img;
    if(vbase+span>0xFFFFF000ull){ kputs("pe: no memory below 4 GB for a 32-bit program\n"); return -1; }
  }
  memcpy(img,f,o->hdrsz);
  for(int i=0;i<c->nsec;i++){
    u32 n=sec[i].rawsz<sec[i].vsize?sec[i].rawsz:sec[i].vsize;
    if(sec[i].rawptr+(u64)n>size||sec[i].va+(u64)sec[i].vsize>o->imagesz){ kputs("pe: bad section\n"); return -1; }
    memcpy(img+sec[i].va,f+sec[i].rawptr,n);
  }
  i64 delta=(i64)(vbase-want);
  if(delta){
    u8 *r=img+o->dir[5].rva, *end=r+o->dir[5].size;
    while(r<end){ u32 page=*(u32*)r, blk=*(u32*)(r+4); if(!blk) break;
      for(u16 *e=(u16*)(r+8); (u8*)e<r+blk; e++) if((*e>>12)==3) *(u32*)(img+page+(*e&0xFFF))+=(u32)delta;
      r+=blk; }
  }
  if(o->dir[1].rva){
    for(const struct import_desc *d=(void*)(img+o->dir[1].rva); d->name; d++){
      const char *dll=(char*)img+d->name; u32 *lk=(u32*)(img+(d->oft?d->oft:d->ft)), *iat=(u32*)(img+d->ft);
      for(;*lk;lk++,iat++){
        char ord[12]; const char *fn;
        if(*lk>>31){ u32 v=*lk&0xFFFF; int n=0; ord[n++]='#'; char t[6]; int k=0; do{t[k++]='0'+v%10;v/=10;}while(v); while(k) ord[n++]=t[--k]; ord[n]=0; fn=ord; }
        else fn=(char*)img+(*lk&0x7FFFFFFF)+2;
        *iat=wow_import(dll,fn);
      }
    }
  }
  u64 stk=o->stkres? (o->stkres>0x100000?0x100000:o->stkres) : 0x100000; u8 *stack=kmalloc(stk,16);
  if(!stack || (u64)stack+stk>0xFFFFF000ull){ kputs("pe: no stack memory below 4 GB\n"); return -1; }
  memset(out,0,sizeof *out); out->is32=1; out->size=o->imagesz; out->base=vbase; out->entry=vbase+o->entry; out->stack_top=(u64)stack+stk-64; out->stack_lo=(u64)stack;
  return 0;
}
