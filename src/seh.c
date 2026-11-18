#include "kernel.h"
/* Windows x64 structured exception handling: what C++ try/catch (MSVC's __CxxFrameHandler, MinGW's _GCC_specific_handler),
   __try/__except/__finally (__C_specific_handler) and RaiseException need from the system.

   - RtlLookupFunctionEntry finds a function's RUNTIME_FUNCTION in its module's .pdata (or in tables added at run time by JITs);
     RtlVirtualUnwind runs its unwind codes backwards to get the caller's registers (the logic follows Wine's dlls/ntdll/unwind.c).
   - seh_dispatch: vectored handlers first, then each frame's language handler, walking up the program's stack; then the
     program's unhandled-exception filter.
   - RtlUnwindEx calls the frames' unwind handlers (destructors, __finally) up to the target frame and resumes there; with
     STATUS_UNWIND_CONSOLIDATE, RtlRestoreContext runs the C++ catch block (the callback in the exception record) first.

   Handlers are program code but are called from Willows' C code, which has no unwind tables. Windows solves the same problem
   with special dispatcher frames; here each thread keeps a list of "boundary" records (TEB->NtTib.ExceptionList, unused on x64):
   when a stack walk reaches Willows code it continues from the context the record names - the original exception context while a
   handler searches, the frame being unwound while an unwind handler runs. A walk also stops at Willows code it has no record for,
   so exceptions do not cross Windows API callbacks (out of a window procedure into DispatchMessage, for instance). */

typedef u32 DWORD;
extern char kernel_text_end[];
extern int veh_call(void *ptrs);
extern void ctx_restore(void *ctx) __attribute__((noreturn));
u64 dll_module_at(u64 a);

#define CTX_SIZE 0x4d0
#define C_RSP 0x98
#define C_RIP 0xf8
#define C_RAX 0x78
#define REG(ctx,i) (*(u64 *)((u8 *)(ctx) + 0x78 + 8 * (i)))       /* rax rcx rdx rbx rsp rbp rsi rdi r8 .. r15 */
#define XMM(ctx,i) ((u8 *)(ctx) + 0x1a0 + 16 * (i))
struct rec { u32 code, flags; u64 next, addr; u32 n, pad; u64 info[15]; };   /* EXCEPTION_RECORD */
struct rtf { u32 begin, end, unwind; };                                     /* RUNTIME_FUNCTION */
struct dctx { u64 pc, base; struct rtf *fe; u64 frame, target_ip; u8 *ctx; void *handler, *hdata, *history; u32 scope, fill; };   /* DISPATCHER_CONTEXT */
#define EH_NONCONTINUABLE 1
#define EH_UNWINDING 2
#define EH_EXIT_UNWIND 4
#define EH_TARGET_UNWIND 0x20
#define EH_COLLIDED_UNWIND 0x40
#define UNW_EHANDLER 1
#define UNW_UHANDLER 2
#define UNW_CHAININFO 4
#define STATUS_UNWIND_CONSOLIDATE 0x80000029u
#define STATUS_LONGJUMP 0x80000026u
#define STATUS_UNWIND 0xC0000027u
typedef DWORD (WINAPI *handler_fn)(struct rec *, u64 frame, u8 *ctx, struct dctx *);

static int in_kernel(u64 pc){ return pc>=0x100000 && pc<(u64)kernel_text_end; }

/* ---------------------------------------------------------------- function tables */
#define MAXDYN 64
static struct dyn { int used, pid; struct rtf *tab; u32 count, max; u64 base, lo, hi; struct rtf *(WINAPI *cb)(u64 pc, void *ctx); void *cbctx; } dyns[MAXDYN];
static struct rtf *search(struct rtf *t, u32 n, u32 rva){
  u32 lo=0, hi=n; while(lo<hi){ u32 m=(lo+hi)/2; if(rva<t[m].begin) hi=m; else if(rva>=t[m].end) lo=m+1; else return &t[m]; }
  return 0;
}
static int pdata_of(u64 base, struct rtf **t, u32 *n){                    /* the exception directory of a mapped PE image */
  if(!base || *(u16 *)base!=0x5A4D) return 0;
  u32 lfanew=*(u32 *)(base+0x3c); const u8 *opt=(const u8 *)(base+lfanew+24);
  if(*(u32 *)(base+lfanew)!=0x4550 || *(u16 *)opt!=0x20b || *(u32 *)(opt+108)<4) return 0;    /* PE32+ with at least 4 data directories */
  u32 rva=*(u32 *)(opt+112+3*8), size=*(u32 *)(opt+112+3*8+4); if(!rva || !size) return 0;
  *t=(struct rtf *)(base+rva); *n=size/12; return 1;
}
static struct rtf *WINAPI s_RtlLookupFunctionEntry(u64 pc, u64 *base, void *history){
  (void)history; if(base) *base=0;
  for(int i=0;i<MAXDYN;i++){ struct dyn *d=&dyns[i]; if(!d->used || d->pid!=cur->pid || pc<d->lo || pc>=d->hi) continue;
    struct rtf *f= d->cb? d->cb(pc,d->cbctx) : search(d->tab,d->count,(u32)(pc-d->base)); if(f){ if(base) *base=d->base; return f; } }
  if(in_kernel(pc)) return 0;
  u64 b=dll_module_at(pc); struct rtf *t; u32 n; if(!b || !pdata_of(b,&t,&n)) return 0;
  struct rtf *f=search(t,n,(u32)(pc-b)); if(f && base) *base=b;
  return f;
}
static void *WINAPI s_RtlPcToFileHeader(u64 pc, u64 *base){ u64 b= in_kernel(pc)? 0 : dll_module_at(pc); if(base) *base=b; return (void *)b; }
static void dyn_range(struct dyn *d){ d->lo=~0ull; d->hi=0; for(u32 i=0;i<d->count;i++){ u64 a=d->base+d->tab[i].begin, e=d->base+d->tab[i].end; if(a<d->lo) d->lo=a; if(e>d->hi) d->hi=e; } }
static struct dyn *dyn_new(void){ for(int i=0;i<MAXDYN;i++) if(!dyns[i].used){ memset(&dyns[i],0,sizeof dyns[i]); dyns[i].used=1; dyns[i].pid=cur->pid; return &dyns[i]; } return 0; }
static u8 WINAPI s_RtlAddFunctionTable(struct rtf *t, u32 n, u64 base){ struct dyn *d=dyn_new(); if(!d) return 0; d->tab=t; d->count=n; d->base=base; dyn_range(d); return 1; }
static u8 WINAPI s_RtlDeleteFunctionTable(struct rtf *t){ for(int i=0;i<MAXDYN;i++) if(dyns[i].used && dyns[i].tab==t && dyns[i].pid==cur->pid){ dyns[i].used=0; return 1; } return 0; }
static u8 WINAPI s_RtlInstallFunctionTableCallback(u64 id, u64 base, u32 len, void *cb, void *ctx, const u16 *dll){
  (void)dll; if((id&3)!=3) return 0; struct dyn *d=dyn_new(); if(!d) return 0; d->tab=(struct rtf *)id; d->base=base; d->lo=base; d->hi=base+len; d->cb=cb; d->cbctx=ctx; return 1; }
static u32 WINAPI s_RtlAddGrowableFunctionTable(void **h, struct rtf *t, u32 n, u32 max, u64 lo, u64 hi){   /* Firefox's JIT registers its code this way */
  struct dyn *d=dyn_new(); if(!d) return 0xC0000017u; d->tab=t; d->count=n; d->max=max; d->base=lo; d->lo=lo; d->hi=hi; if(h) *h=d; return 0; }
static void WINAPI s_RtlGrowFunctionTable(void *h, u32 n){ struct dyn *d=h; if(d>=dyns && d<dyns+MAXDYN && d->used && n<=d->max) d->count=n; }
static void WINAPI s_RtlDeleteGrowableFunctionTable(void *h){ struct dyn *d=h; if(d>=dyns && d<dyns+MAXDYN) d->used=0; }

/* ---------------------------------------------------------------- RtlVirtualUnwind */
enum { UWOP_PUSH_NONVOL, UWOP_ALLOC_LARGE, UWOP_ALLOC_SMALL, UWOP_SET_FPREG, UWOP_SAVE_NONVOL, UWOP_SAVE_NONVOL_FAR, UWOP_EPILOG, UWOP_SPARE, UWOP_SAVE_XMM128, UWOP_SAVE_XMM128_FAR, UWOP_PUSH_MACHFRAME };
struct uinfo { u8 ver_flags, prolog, count, frame; u16 codes[]; };      /* UNWIND_INFO; a code is offset (low byte), op:4 info:4 */
#define OP(c) (((c)>>8)&15)
#define OPINFO(c) ((c)>>12)
#define OPOFF(c) ((c)&255)
static u32 rd32(const void *p){ u32 v; memcpy(&v,p,4); return v; }           /* (32-bit operands sit in the 16-bit code array) */
static int op_size(u16 c){ switch(OP(c)){ case UWOP_ALLOC_LARGE: return 2+(OPINFO(c)!=0); case UWOP_SAVE_NONVOL: case UWOP_SAVE_XMM128: case UWOP_EPILOG: return 2; case UWOP_SAVE_NONVOL_FAR: case UWOP_SAVE_XMM128_FAR: return 3; default: return 1; } }
static int in_epilog(const u8 *pc, u64 base, const struct rtf *f){              /* is pc at "add/lea rsp; pop...; ret"? */
  if((pc[0]&0xf8)==0x48){
    switch(pc[1]){
      case 0x81: if(pc[0]==0x48 && pc[2]==0xc4){ pc+=7; break; } return 0;
      case 0x83: if(pc[0]==0x48 && pc[2]==0xc4){ pc+=4; break; } return 0;
      case 0x8d: if(pc[0]&6) return 0; if(((pc[2]>>3)&7)!=4) return 0; if((pc[2]&7)==4) return 0;
        if((pc[2]>>6)==1){ pc+=4; break; } if((pc[2]>>6)==2){ pc+=7; break; } return 0;
    }
  }
  for(int guard=0; guard<64; guard++){
    if((*pc&0xf0)==0x40) pc++;
    switch(*pc){
      case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: pc++; continue;
      case 0xc2: case 0xc3: return 1;
      case 0xe9: pc+=5+*(const i32 *)(pc+1); if((u64)(pc-(const u8 *)base)>=f->begin && (u64)(pc-(const u8 *)base)<f->end) continue; return 0;
      case 0xeb: pc+=2+(signed char)pc[1]; if((u64)(pc-(const u8 *)base)>=f->begin && (u64)(pc-(const u8 *)base)<f->end) continue; return 0;
      case 0xf3: return pc[1]==0xc3;
    }
    return 0;
  }
  return 0;
}
static void run_epilog(const u8 *pc, u8 *ctx){
  for(int guard=0; guard<64; guard++){
    u8 rex=0; if((*pc&0xf0)==0x40) rex=*pc++&0x0f;
    switch(*pc){
      case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
        REG(ctx,*pc-0x58+(rex&1)*8)=*(u64 *)REG(ctx,4); REG(ctx,4)+=8; pc++; continue;
      case 0x81: REG(ctx,4)+=*(const i32 *)(pc+2); pc+=6; continue;
      case 0x83: REG(ctx,4)+=(signed char)pc[2]; pc+=3; continue;
      case 0x8d:
        if((pc[1]>>6)==1){ REG(ctx,4)=REG(ctx,(pc[1]&7)+(rex&1)*8)+(signed char)pc[2]; pc+=3; }
        else { REG(ctx,4)=REG(ctx,(pc[1]&7)+(rex&1)*8)+*(const i32 *)(pc+2); pc+=6; }
        continue;
      case 0xc2: REG(ctx,16)=*(u64 *)REG(ctx,4); REG(ctx,4)+=8+*(const u16 *)(pc+1); return;     /* REG 16 = Rip (0xf8) */
      case 0xc3: case 0xf3: REG(ctx,16)=*(u64 *)REG(ctx,4); REG(ctx,4)+=8; return;
      case 0xe9: pc+=5+*(const i32 *)(pc+1); continue;
      case 0xeb: pc+=2+(signed char)pc[1]; continue;
    }
    return;
  }
}
static void *WINAPI s_RtlVirtualUnwind(DWORD type, u64 base, u64 pc, struct rtf *f, u8 *ctx, void **hdata, u64 *frame_out, void *ptrs){
  (void)ptrs; u64 frame=REG(ctx,4); *frame_out=frame; int mach=0; u32 prolog_off=~0u; const struct uinfo *ui=0;
  for(int chain=0; chain<32; chain++){
    ui=(const struct uinfo *)(base+f->unwind);
    u32 ver=ui->ver_flags&7, flags=ui->ver_flags>>3; if(ver!=1 && ver!=2) return 0;
    if(ui->frame&15) frame=REG(ctx,ui->frame&15)-(u64)(ui->frame>>4)*16;
    if(pc>=base+f->begin && pc<base+f->begin+ui->prolog) prolog_off=(u32)(pc-base-f->begin);
    else { prolog_off=~0u; if(ui->count && in_epilog((const u8 *)pc,base,f)){ run_epilog((const u8 *)pc,ctx); *frame_out=frame; return 0; } }
    for(u32 i=0;i<ui->count;i+=op_size(ui->codes[i])){
      u16 c=ui->codes[i]; if(prolog_off<OPOFF(c)) continue;                        /* that prolog instruction has not run yet */
      switch(OP(c)){
        case UWOP_PUSH_NONVOL: REG(ctx,OPINFO(c))=*(u64 *)REG(ctx,4); REG(ctx,4)+=8; break;
        case UWOP_ALLOC_LARGE: REG(ctx,4)+= OPINFO(c)? rd32(&ui->codes[i+1]) : (u64)ui->codes[i+1]*8; break;
        case UWOP_ALLOC_SMALL: REG(ctx,4)+=(OPINFO(c)+1)*8; break;
        case UWOP_SET_FPREG: REG(ctx,4)=*frame_out=frame; break;
        case UWOP_SAVE_NONVOL: REG(ctx,OPINFO(c))=*(u64 *)(frame+(u64)ui->codes[i+1]*8); break;
        case UWOP_SAVE_NONVOL_FAR: REG(ctx,OPINFO(c))=*(u64 *)(frame+rd32(&ui->codes[i+1])); break;
        case UWOP_SAVE_XMM128: memcpy(XMM(ctx,OPINFO(c)),(void *)(frame+(u64)ui->codes[i+1]*16),16); break;
        case UWOP_SAVE_XMM128_FAR: memcpy(XMM(ctx,OPINFO(c)),(void *)(frame+rd32(&ui->codes[i+1])),16); break;
        case UWOP_PUSH_MACHFRAME:
          if(OPINFO(c)) REG(ctx,4)+=8;
          REG(ctx,16)=*(u64 *)REG(ctx,4); REG(ctx,4)=*(u64 *)(REG(ctx,4)+24); mach=1; break;
        default: break;                                                            /* UWOP_EPILOG (version 2): nothing to do here */
      }
    }
    if(!(flags&UNW_CHAININFO)) break;
    f=(struct rtf *)&ui->codes[(ui->count+1)&~1];                                  /* continue with the chained entry */
  }
  if(!mach){ REG(ctx,16)=*(u64 *)REG(ctx,4); REG(ctx,4)+=8; }                     /* pop the return address */
  u32 flags=ui->ver_flags>>3;
  if(!(flags&type) || prolog_off!=~0u) return 0;
  const u32 *h=(const u32 *)&ui->codes[(ui->count+1)&~1];
  if(hdata) *hdata=(void *)(h+1);
  return (void *)(base+*h);
}

/* ---------------------------------------------------------------- the per-thread boundary records */
struct bound { struct bound *prev; u8 *resume; struct dctx *dc; int collided; };   /* lives in a C frame on the program's stack */
static struct bound **bound_head(void){ return cur_teb? (struct bound **)&cur_teb[0] : 0; }
static void bound_push(struct bound *b, u8 *resume, struct dctx *dc, int collided){
  struct bound **h=bound_head(); b->resume=resume; b->dc=dc; b->collided=collided; b->prev= h? *h : 0; if(h) *h=b; }
static void bound_pop(struct bound *b){ struct bound **h=bound_head(); if(h && *h==b) *h=b->prev; }
static void bound_drop_below(u64 sp){ struct bound **h=bound_head(); while(h && *h && (u64)*h<sp) *h=(*h)->prev; }   /* records in frames that are gone */
static u64 stack_top(void){ return cur_teb? cur_teb[1] : ~0ull; }

/* One frame up the stack. w: the walk's context, moved to the caller. fctx: the frame's own context (before the step), what its
   handler gets. Returns -1 when the walk has to stop, 0 for a leaf function (no unwind info: no handler, no establisher frame),
   1 for a frame with unwind info (*h = its handler of `type`, or 0). */
static int step(u8 *w, u8 *fctx, u32 type, struct bound **it, struct dctx *dc, handler_fn *h, int *collided){
  *h=0; *collided=0;
  for(int guard=0; guard<8; guard++){
    u64 pc=REG(w,16);
    if(!pc || REG(w,4)>=stack_top()) return -1;
    if(in_kernel(pc)){                                                             /* Willows code: continue where its boundary record says */
      if(!*it) return -1;
      memcpy(w,(*it)->resume,CTX_SIZE); *collided=(*it)->collided; if((*it)->dc) dc->scope=(*it)->dc->scope; *it=(*it)->prev; continue;
    }
    memcpy(fctx,w,CTX_SIZE);
    u64 base; struct rtf *f=s_RtlLookupFunctionEntry(pc,&base,0);
    dc->pc=pc; dc->base=base; dc->fe=f;
    if(!f){ dc->frame=REG(w,4); REG(w,16)=*(u64 *)REG(w,4); REG(w,4)+=8; return 0; }
    *h=(handler_fn)s_RtlVirtualUnwind(type,base,pc,f,w,&dc->hdata,&dc->frame,0);
    if(dc->frame>=stack_top()){ *h=0; return -1; }
    return 1;
  }
  return -1;
}

/* ---------------------------------------------------------------- dispatch */
#define MAXFILTER MAXPROC
static void *top_filter[MAXFILTER];
void seh_proc_exit(int pid){
  for(int i=0;i<MAXDYN;i++) if(dyns[i].used && dyns[i].pid==pid) dyns[i].used=0;
  if(pid>=0 && pid<MAXFILTER) top_filter[pid]=0;
}
static void *WINAPI s_SetUnhandledExceptionFilter(void *f){ int p=cur->pid; if(p<0 || p>=MAXFILTER) return 0; void *o=top_filter[p]; top_filter[p]=f; return o; }
static i32 WINAPI s_UnhandledExceptionFilter(void *ptrs){
  int p=cur->pid; i32 (WINAPI *f)(void *)= (p>=0 && p<MAXFILTER)? (i32 (WINAPI *)(void *))top_filter[p] : 0;
  return f? f(ptrs) : 0;                                                         /* EXCEPTION_CONTINUE_SEARCH: no filter */
}
/* 1 = handled: continue from ctx (which the handlers may have changed). 0 = nobody took it. */
int seh_dispatch(struct rec *r, u8 *ctx){
  u64 ptrs[2]={(u64)r,(u64)ctx};
  if(veh_call(ptrs)) return 1;
  u8 w[CTX_SIZE] __attribute__((aligned(16))); memcpy(w,ctx,CTX_SIZE);
  struct bound *it= bound_head()? *bound_head() : 0;
  u8 fctx[CTX_SIZE] __attribute__((aligned(16)));
  for(int depth=0; depth<1024; depth++){
    struct dctx dc; memset(&dc,0,sizeof dc); handler_fn h; int coll;
    if(step(w,fctx,UNW_EHANDLER,&it,&dc,&h,&coll)<0) break;
    if(!h) continue;
    dc.ctx=ctx; dc.handler=(void *)h;
    struct bound b; bound_push(&b,ctx,0,0);                                       /* a nested walk from inside the handler resumes at the exception */
    DWORD d=h(r,dc.frame,ctx,&dc);
    bound_pop(&b);
    if(d==0){ if(r->flags&EH_NONCONTINUABLE) return 0; return 1; }               /* ExceptionContinueExecution */
    /* ExceptionContinueSearch (1) and the nested cases: keep searching */
  }
  i32 v=s_UnhandledExceptionFilter(ptrs);
  return v==-1;                                                                  /* EXCEPTION_CONTINUE_EXECUTION */
}
static void unhandled(struct rec *r, u8 *ctx){
  char at[64]; if(!dll_addr_name(REG(ctx,16),at,sizeof at)) ksnprintf(at,sizeof at,"%p",(void *)REG(ctx,16));
  kprintf("\n*** Unhandled exception 0x%x (flags %x, %u args%s%p) at %s: the program ends\n",r->code,r->flags,r->n,r->n? ", first " : "",r->n? (void *)r->info[0] : 0,at);
  kdbg("  [exit] unhandled exception 0x%x at %s\n",r->code,at);
  proc_terminate((int)r->code);
  for(;;) __asm__ volatile("hlt");
}
/* RaiseException / RtlRaiseException: ctx is the caller's context (captured by the stubs in boot.S) */
void seh_raise_rec_c(struct rec *r, u8 *ctx){
  if(!r->addr) r->addr=REG(ctx,16);
  if(r->code==0x406D1388){ ctx_restore(ctx); }                                  /* MSVC "name this thread" for a debugger: no debugger, nothing to do */
  if(seh_dispatch(r,ctx)) ctx_restore(ctx);
  unhandled(r,ctx);
}
void seh_raise_c(u32 code, u32 flags, u32 n, const u64 *args, u8 *ctx){
  struct rec r; memset(&r,0,sizeof r); r.code=code; r.flags=flags&EH_NONCONTINUABLE; r.addr=REG(ctx,16); if(n>15) n=15; r.n=n;
  for(u32 i=0;i<n && args;i++) r.info[i]=args[i];
  seh_raise_rec_c(&r,ctx);
}
/* a CPU fault (idt.c veh_dispatch): ptrs = {record, context} */
int seh_dispatch_fault(u64 *ptrs){ return seh_dispatch((struct rec *)ptrs[0],(u8 *)ptrs[1]); }

/* ---------------------------------------------------------------- unwinding */
static void restore(u8 *ctx, struct rec *r);
/* ctx: the context to start from (RtlUnwindEx's caller). Ends by resuming the target frame at target_ip. */
static void unwind(u64 target_frame, u64 target_ip, struct rec *r, u64 retval, u8 *ctx){
  struct rec local; if(!r){ memset(&local,0,sizeof local); local.code=STATUS_UNWIND; local.addr=REG(ctx,16); r=&local; }
  u32 flags=EH_UNWINDING | (target_frame? 0 : EH_EXIT_UNWIND);
  u8 w[CTX_SIZE] __attribute__((aligned(16))); memcpy(w,ctx,CTX_SIZE);
  struct bound *it= bound_head()? *bound_head() : 0;
  u8 frame_ctx[CTX_SIZE] __attribute__((aligned(16)));                          /* the frame being unwound, as its handler sees it */
  for(int depth=0; depth<4096; depth++){
    struct dctx dc; memset(&dc,0,sizeof dc); handler_fn h; int coll;
    int k=step(w,frame_ctx,UNW_UHANDLER,&it,&dc,&h,&coll);
    if(k<0){
      if(!target_frame) break;                                                     /* exit unwind: ran off the stack */
      kprintf("\n*** RtlUnwindEx: target frame %p not found on the stack\n",(void *)target_frame); proc_terminate((int)0xC0000029); for(;;) __asm__ volatile("hlt");
    }
    if(k==0){ memcpy(ctx,w,CTX_SIZE); continue; }                                  /* a leaf: nothing to run */
    if(target_frame && dc.frame>target_frame){ kprintf("\n*** RtlUnwindEx: walked past the target frame\n"); proc_terminate((int)0xC0000029); for(;;) __asm__ volatile("hlt"); }
    if(h){
      r->flags=(r->flags&EH_NONCONTINUABLE) | flags | (dc.frame==target_frame? EH_TARGET_UNWIND : 0) | (coll? EH_COLLIDED_UNWIND : 0);
      dc.target_ip=target_ip; dc.ctx=frame_ctx; dc.handler=(void *)h;
      struct bound b; bound_push(&b,frame_ctx,&dc,1);                               /* an unwind started by this handler collides with ours */
      DWORD d=h(r,dc.frame,frame_ctx,&dc);
      bound_pop(&b);
      (void)d;                                                                     /* ExceptionContinueSearch; a collided unwind has already left */
    }
    if(dc.frame==target_frame){ memcpy(ctx,frame_ctx,CTX_SIZE); goto done; }
    memcpy(ctx,w,CTX_SIZE);
  }
done:
  if(r->code!=STATUS_UNWIND_CONSOLIDATE) REG(ctx,16)=target_ip;
  REG(ctx,0)=retval;
  restore(ctx,r);
}
void seh_unwind_c(u64 target_frame, u64 target_ip, struct rec *r, u64 retval, u8 *ctx){ unwind(target_frame,target_ip,r,retval,ctx); }

/* RtlRestoreContext: STATUS_UNWIND_CONSOLIDATE runs the callback in the record (MSVC: the catch block) and resumes where it says;
   STATUS_LONGJUMP takes the registers from a jmp_buf. */
static void restore(u8 *ctx, struct rec *r){
  if(r && r->code==STATUS_UNWIND_CONSOLIDATE && r->n>=1){
    u64 (WINAPI *cb)(struct rec *)=(u64 (WINAPI *)(struct rec *))r->info[0];
    struct bound b; bound_push(&b,ctx,0,0);                                        /* an exception inside the catch block walks up from the target frame */
    u64 ip=cb(r); bound_pop(&b); REG(ctx,16)=ip;
  } else if(r && r->code==STATUS_LONGJUMP && r->n>=1){
    const u64 *jb=(const u64 *)r->info[0];                                         /* _JUMP_BUFFER: Frame Rbx Rsp Rbp Rsi Rdi R12-R15 Rip MxCsr+FpCsr Spare Xmm6-15 */
    REG(ctx,3)=jb[1]; REG(ctx,4)=jb[2]; REG(ctx,5)=jb[3]; REG(ctx,6)=jb[4]; REG(ctx,7)=jb[5];
    for(int i=0;i<4;i++) REG(ctx,12+i)=jb[6+i]; REG(ctx,16)=jb[10];
    for(int i=0;i<10;i++) memcpy(XMM(ctx,6+i),(const u8 *)jb+0x60+16*i,16);
  }
  bound_drop_below(REG(ctx,4));
  ctx_restore(ctx);
}
static void WINAPI s_RtlRestoreContext(u8 *ctx, struct rec *r){ restore(ctx,r); }

/* ---------------------------------------------------------------- __C_specific_handler: __try / __except / __finally */
struct scope { u32 count; struct { u32 begin, end, handler, target; } r[]; };
static DWORD WINAPI s___C_specific_handler(struct rec *r, u64 frame, u8 *ctx, struct dctx *dc){
  const struct scope *t=(const struct scope *)dc->hdata;
  if(r->flags&(EH_UNWINDING|EH_EXIT_UNWIND)){
    for(u32 i=dc->scope;i<t->count;i++){
      u64 b=dc->base+t->r[i].begin, e=dc->base+t->r[i].end;
      if(dc->pc<b || dc->pc>=e || t->r[i].target) continue;
      if((r->flags&EH_TARGET_UNWIND) && dc->target_ip>=b && dc->target_ip<e) break;
      dc->scope=i+1;
      ((void (WINAPI *)(u8, u64))(dc->base+t->r[i].handler))(1,frame);            /* __finally, abnormal termination */
    }
    return 1;
  }
  for(u32 i=dc->scope;i<t->count;i++){
    u64 b=dc->base+t->r[i].begin, e=dc->base+t->r[i].end;
    if(dc->pc<b || dc->pc>=e || !t->r[i].target) continue;
    if(t->r[i].handler!=1){                                                        /* 1 = EXCEPTION_EXECUTE_HANDLER without a filter */
      u64 ptrs[2]={(u64)r,(u64)ctx};
      i32 v=((i32 (WINAPI *)(void *, u64))(dc->base+t->r[i].handler))(ptrs,frame);
      if(v<0) return 0;                                                            /* EXCEPTION_CONTINUE_EXECUTION */
      if(v==0) continue;                                                           /* EXCEPTION_CONTINUE_SEARCH */
    }
    /* run the __except block: unwind to this frame, resuming at its target with the exception code in rax. The walk starts at
       the boundary record of this dispatch (the exception's context), since this handler is Willows code. */
    struct bound *top= bound_head()? *bound_head() : 0; if(!top) return 1;
    u8 start[CTX_SIZE] __attribute__((aligned(16))); memcpy(start,top->resume,CTX_SIZE);
    *bound_head()=top->prev;
    unwind(frame,dc->base+t->r[i].target,r,r->code,start);
  }
  return 1;
}

/* ---------------------------------------------------------------- exports */
extern char seh_RtlCaptureContext[], seh_RaiseException[], seh_RtlRaiseException[], seh_RtlUnwindEx[], seh_RtlUnwind[];
#define S(d,n) {d, #n, (void *)s_##n}
#define A(d,n) {d, #n, (void *)seh_##n}
const struct export seh_exports[] = {
  A("kernel32",RtlCaptureContext), A("ntdll",RtlCaptureContext), A("kernel32",RaiseException), A("ntdll",RtlRaiseException), A("kernel32",RtlRaiseException),
  A("kernel32",RtlUnwindEx), A("ntdll",RtlUnwindEx), A("kernel32",RtlUnwind), A("ntdll",RtlUnwind),
  S("kernel32",RtlLookupFunctionEntry), S("ntdll",RtlLookupFunctionEntry), S("kernel32",RtlVirtualUnwind), S("ntdll",RtlVirtualUnwind),
  S("kernel32",RtlRestoreContext), S("ntdll",RtlRestoreContext), S("kernel32",RtlPcToFileHeader), S("ntdll",RtlPcToFileHeader),
  S("kernel32",RtlAddFunctionTable), S("ntdll",RtlAddFunctionTable), S("kernel32",RtlDeleteFunctionTable), S("ntdll",RtlDeleteFunctionTable),
  S("kernel32",RtlInstallFunctionTableCallback), S("ntdll",RtlInstallFunctionTableCallback),
  S("ntdll",RtlAddGrowableFunctionTable), S("ntdll",RtlGrowFunctionTable), S("ntdll",RtlDeleteGrowableFunctionTable),
  S("kernel32",SetUnhandledExceptionFilter), S("kernel32",UnhandledExceptionFilter),
  S("ntdll",__C_specific_handler), S("msvcrt",__C_specific_handler), S("kernel32",__C_specific_handler),
};
const unsigned seh_nexports = sizeof seh_exports / sizeof *seh_exports;
