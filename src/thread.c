#include "kernel.h"
/* Threads and synchronisation. A process's main thread is its struct proc; CreateThread adds entries to threads[], each with its own
   stack, TEB (so TLS slots, FLS slots and the last error are per thread) and implicit-TLS blocks. Scheduling stays cooperative:
   a thread runs until it waits (Wait*, Sleep, a lock held by another thread, GetMessage...), so code between two waits is atomic
   and the lock implementations below only need to block correctly, not to be atomic against preemption.
   Kernel objects (events, mutexes, semaphores, threads) are reference-counted handles 0x20000 + 4*i; names are shared between
   processes. Process handles (0x8000 + pid) are waitable too. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE;
#define WINAPI_ __attribute__((ms_abi))
struct thread threads[MAXTHR]; struct thread *cur_thr; u64 *cur_teb;
extern void thread_start(void);

static u64 *teb_now(void){ return cur_teb; }

/* ---- debugging: what each thread is waiting for (slot = schedule() position: main threads 0..MAXPROC-1, then threads) ---- */
volatile int debug_dump_request;
static const char *w_what[MAXPROC+MAXTHR]; static u64 w_arg[MAXPROC+MAXTHR], w_ra[MAXPROC+MAXTHR];
static int my_slot(void){ return cur_thr? MAXPROC+(int)(cur_thr-threads) : (int)(cur-procs); }
#define WAITING(what,arg) do{ int s_=my_slot(); w_what[s_]=(what); w_arg[s_]=(u64)(arg); w_ra[s_]=(u64)__builtin_return_address(0); }while(0)
#define DONE_WAITING() (w_what[my_slot()]=0)
void thread_note_wait(const char *what, u64 arg, u64 ra){ int s=my_slot(); w_what[s]=what; w_arg[s]=arg; w_ra[s]=ra; }   /* for waits in other files */
void thread_end_wait(void){ DONE_WAITING(); }
static void dump_stack(u64 rsp, u64 top){
  char nm[64]; int shown=0, ks=0; kdbg("      stack:");
  extern char kernel_text_end[];
  for(u64 *p=(u64*)rsp; (u64)p<top && shown<48 && p<(u64*)rsp+8000; p++){
    if(dll_addr_name(*p,nm,sizeof nm)){ kdbg(" %s",nm); shown++; }
    else if(p<(u64*)rsp+40 && ks<6 && *p>=0x100000 && *p<(u64)kernel_text_end){ kdbg(" k:%p",(void*)*p); ks++; }   /* kernel frames before the program's: tools/dev/symstack.py names them */
  }
  kdbg("\n");
}
void debug_dump(void){
  struct proc *save=cur; struct thread *save_thr=cur_thr;
  kdbg("\n==== Willows: processes and threads (ticks %u) ====\n",ticks);
  for(int p=1;p<MAXPROC;p++){ if(!procs[p].used) continue; cur=&procs[p];
    kdbg("  pid %d %s%s\n",p,procs[p].name,procs[p].main_done? " (main thread finished)" : "");
    { extern u64 api_ring[]; u64 n=api_ring[0]; static u64 pick[300]; int k=0;   /* its last traced API calls (TRACE=1) */
      for(u64 i=n; i>0 && i+8192>n && k<300; i--){ u64 e=api_ring[1+((i-1)&8191)]; if((int)(e>>56)==p) pick[k++]=e&0x00FFFFFFFFFFFFFFull; }
      if(k){ kdbg("    last API calls:"); while(k) kdbg(" %s",(const char*)pick[--k]); kdbg("\n"); } }
    { void wm_dump_queue(int pid); wm_dump_queue(p); }
    if(!procs[p].main_done){ kdbg("    main thread %x: %s%s",8+p*4,w_what[p]? "waiting in " : "running/ready",w_what[p]? w_what[p] : "");
      if(w_what[p]){ char nm[64]; if(!dll_addr_name(w_ra[p],nm,sizeof nm)) ksnprintf(nm,sizeof nm,"%p",(void*)w_ra[p]); kdbg(" (%p) from %s",(void*)w_arg[p],nm); } kdbg("\n");
      dump_stack(procs[p].rsp, procs[p].img.stack_top+64); }
    for(int i=0;i<MAXTHR;i++){ struct thread *t=&threads[i]; if(!t->used || t->pid!=p || t->done) continue; int s=MAXPROC+i;
      kdbg("    thread %x \"%s\" (start %p)%s: %s%s",t->tid,t->name,(void*)t->fn,t->suspended? " suspended" : "",w_what[s]? "waiting in " : "running/ready",w_what[s]? w_what[s] : "");
      if(w_what[s]){ char nm[64]; if(!dll_addr_name(w_ra[s],nm,sizeof nm)) ksnprintf(nm,sizeof nm,"%p",(void*)w_ra[s]); kdbg(" (%p) from %s",(void*)w_arg[s],nm); } kdbg("\n");
      if(t==save_thr) kdbg("      (this thread is running the dump)\n");
      dump_stack(t==save_thr? (u64)__builtin_frame_address(0) : t->rsp,t->teb[1]); }
  }
  kdbg("  memory: %u MB committed by programs, %u MB reserved; RAM in use %u of %u MB\n",(u32)(vmem_committed()>>20),(u32)(vmem_reserved()>>20),
       (u32)((mm_total()-mm_free())>>20),(u32)(mm_total()>>20));
  { void kobj_report(void); kobj_report(); }
  { void mm_report(void); mm_report(); kdbg("  vmem: %u MB of freed program pages ready for reuse\n",(u32)(vmem_free_pages()>>8)); }
  kdbg("==== end ====\n");
  cur=save;
}
static void set_err(u32 e){ win32_set_last_error(e); }
u32 thread_cur_tid(void){ u64 *t=teb_now(); return t? *(u32*)((u8*)t+0x48) : 4; }

/* ================= kernel objects ================= */
#define KO_BASE 0x20000
#define MAXKO 16384
enum { KO_EVENT=1, KO_MUTEX, KO_SEMA, KO_THREAD, KO_IOCP, KO_TIMER };
struct packet { u64 key, ov; u32 bytes; };
struct kobj { u8 used, kind, manual, done; i32 refs, count, max; u32 owner, tid, code; int thr; char name[64]; struct packet *q; u32 qh, qt, qcap;   /* q: completion packets (KO_IOCP) */
  u8 armed; u32 due, period; u64 apc_fn, apc_arg; struct thread *apc_thr;                                                    /* KO_TIMER: due/period in ticks (ms) */
  u8 own[MAXPROC]; };                                                /* each process's references (released when it exits, as Windows closes a dead process's handles) */
static struct kobj ko[MAXKO];
static int my_pid(void){ return cur? cur->pid : 0; }
static void ko_free(struct kobj *o){ if(o->q) heap_free(o->q); o->q=0; o->used=0; }
static struct kobj *ko_get(HANDLE h){ if(h<KO_BASE || h>=KO_BASE+MAXKO*4 || (h&3)) return 0; struct kobj *o=&ko[(h-KO_BASE)/4]; return o->used? o : 0; }
static HANDLE ko_handle(struct kobj *o){ return KO_BASE+(HANDLE)(o-ko)*4; }
int kobj_is_handle(u64 h){ return ko_get(h)!=0; }
static struct kobj *ko_new(int kind){
  for(int i=0;i<MAXKO;i++) if(!ko[i].used){ memset(&ko[i],0,sizeof ko[i]); ko[i].used=1; ko[i].kind=(u8)kind; ko[i].refs=1; ko[i].thr=-1; ko[i].own[my_pid()]=1; return &ko[i]; }
  { static int warned; if(!warned++) kdbg("  [kobj] the kernel object table is full (%d objects)\n",MAXKO); }
  set_err(8); return 0;
}
static int name_eq(const char *a, const char *b){ for(;*a||*b;a++,b++) if((*a|32)!=(*b|32)) return 0; return 1; }
static void w2a_(const u16 *w, char *a, int n){ int i=0; for(;w&&w[i]&&i<n-1;i++) a[i]=w[i]<0x80? (char)w[i] : '?'; a[i]=0; }
static struct kobj *ko_named(const char *name, int kind, int create, int *existed){   /* names: "Local\x" and "Global\x" mean x */
  if(existed) *existed=0;
  if(!name || !*name) return create? ko_new(kind) : 0;
  const char *n=name; for(const char *p=name;*p;p++) if(*p=='\\') n=p+1;
  for(int i=0;i<MAXKO;i++) if(ko[i].used && ko[i].name[0] && name_eq(ko[i].name,n)){
    if(ko[i].kind!=kind){ kdbg("  [kobj] \"%s\" exists as another kind of object (%d, wanted %d)\n",n,ko[i].kind,kind); set_err(6); return 0; }
    ko[i].refs++; if(ko[i].own[my_pid()]<255) ko[i].own[my_pid()]++; if(existed) *existed=1; return &ko[i];
  }
  if(!create){ set_err(2); return 0; }
  struct kobj *o=ko_new(kind); if(!o) return 0; unsigned k=0; for(;n[k]&&k<63;k++) o->name[k]=n[k]; o->name[k]=0; return o;
}
static int thr_alive(struct kobj *o){                       /* a thread object: is the thread still running? */
  if(o->done) return 0;
  if(o->thr>=MAXTHR){ int pid=o->thr-MAXTHR; return procs[pid].used && !procs[pid].main_done; }
  if(o->thr>=0){ struct thread *t=&threads[o->thr]; return t->used && !t->done && t->tid==o->tid; }
  return 0;
}
/* NtQueryObject: the object type of any handle (programs check it before passing handles to other processes) */
static const char *handle_type(u64 h){
  struct kobj *o=ko_get(h);
  if(o){ static const char *const n[]={"","Event","Mutant","Semaphore","Thread","IoCompletion","Timer"}; return o->kind<=KO_TIMER? n[o->kind] : "Event"; }
  if(h==(u64)-1 || (h>=0x8000 && h<0x8000+MAXPROC)) return "Process";
  if(h==(u64)-2) return "Thread";
  if(fs_is_handle(h) || pipe_is_handle(h) || h==3 || h==7 || h==11) return "File";
  if(map_is_handle(h)) return "Section";
  if(h>=0x7D00 && h<0x7D10) return "Token";
  if(h>=0x7200 && h<0x7A00) return "Key";
  if(h>=0x7E10 && h<0x7E20) return "WindowStation";
  if(h>=0x7E20 && h<0x7E30) return "Desktop";
  return 0;
}
static u32 WINAPI_ t_NtQueryObject(u64 h, u32 cls, u8 *out, u32 len, u32 *ret){
  const char *t=handle_type(h); if(!t) return 0xC0000008;                     /* STATUS_INVALID_HANDLE */
  u32 need;
  switch(cls){
  case 0: need=56; if(ret) *ret=need; if(len<need) return 0xC0000004;          /* ObjectBasicInformation */
    memset(out,0,need); *(u32*)(out+4)=0x1F0FFF; *(u32*)(out+8)=1; *(u32*)(out+12)=2; return 0;   /* GrantedAccess, HandleCount, PointerCount */
  case 1: need=16+2; if(ret) *ret=need; if(len<need) return 0xC0000004;        /* ObjectNameInformation: unnamed */
    memset(out,0,need); *(u64*)(out+8)=(u64)(out+16); return 0;
  case 2: { u32 n=(u32)strlen(t); need=104+(n+1)*2; if(ret) *ret=need; if(len<need) return 0xC0000004;   /* ObjectTypeInformation */
    memset(out,0,104); u16 *w=(u16*)(out+104); for(u32 i=0;i<=n;i++) w[i]=(u8)t[i];
    *(u16*)out=(u16)(n*2); *(u16*)(out+2)=(u16)(n*2+2); *(u64*)(out+8)=(u64)w; return 0; }
  case 4: need=2; if(ret) *ret=need; if(len<need) return 0xC0000004;           /* ObjectHandleFlagInformation: Inherit, ProtectFromClose */
    out[0]=0; out[1]=0; return 0;
  }
  return 0xC0000003;                                                          /* STATUS_INVALID_INFO_CLASS */
}
static u32 WINAPI_ t_NtSetInformationObject(u64 h, u32 cls, void *in, u32 len){ (void)cls;(void)in;(void)len; return handle_type(h)? 0 : 0xC0000008; }
int kobj_addref_for(u64 h, int pid){ struct kobj *o=ko_get(h); if(!o) return 0; o->refs++; if(pid>=0 && pid<MAXPROC && o->own[pid]<255) o->own[pid]++; return 1; }
int kobj_addref(u64 h){ return kobj_addref_for(h,my_pid()); }
int kobj_close_for(u64 h, int pid){ struct kobj *o=ko_get(h); if(!o) return 0; if(pid>=0 && pid<MAXPROC && o->own[pid]) o->own[pid]--; if(--o->refs<=0) ko_free(o); return 1; }
int kobj_close(u64 h){ return kobj_close_for(h,my_pid()); }
void kobj_report(void){                                             /* Ctrl+F12: kernel objects in use, by kind */
  int n[8]={0}, tot=0; for(int i=0;i<MAXKO;i++) if(ko[i].used){ tot++; if(ko[i].kind<8) n[ko[i].kind]++; }
  kdbg("  kernel objects: %d of %d (events %d, mutexes %d, semaphores %d, threads %d, ports %d, timers %d)\n",tot,MAXKO,n[1],n[2],n[3],n[4],n[5],n[6]);
}
void kobj_proc_exit(int pid){                                       /* a process ended: the references it still held go */
  if(pid<=0 || pid>=MAXPROC) return;
  for(int i=0;i<MAXKO;i++){ struct kobj *o=&ko[i]; if(!o->used || !o->own[pid]) continue; o->refs-=o->own[pid]; o->own[pid]=0; if(o->refs<=0) ko_free(o); }
}
/* is h signaled? consume = take it (auto-reset event, semaphore count, mutex ownership). -1 = not a waitable handle */
static int sig(HANDLE h, int consume){
  if(h>=0x8000 && h<0x8000+MAXPROC) return !procs[h-0x8000].used;
  if(h==(HANDLE)-1 || h==(HANDLE)-2) return 0;
  struct kobj *o=ko_get(h); if(!o) return -1;
  switch(o->kind){
    case KO_EVENT: if(!o->count) return 0; if(consume && !o->manual) o->count=0; return 1;
    case KO_SEMA: if(o->count<=0) return 0; if(consume) o->count--; return 1;
    case KO_MUTEX: { u32 me=thread_cur_tid(); if(o->count && o->owner!=me) return 0; if(consume){ o->owner=me; o->count++; } return 1; }
    case KO_THREAD: return !thr_alive(o);
    case KO_IOCP: return o->qh!=o->qt;
    case KO_TIMER:
      if(o->armed && (i32)(ticks-o->due)>=0){ o->count=1; if(o->period) o->due+=o->period; else o->armed=0; }    /* expired: signaled */
      if(!o->count) return 0; if(consume && !o->manual) o->count=0; return 1;
  }
  return 0;
}
int kobj_signaled(u64 h){ return sig(h,1)==1; }

/* ================= APCs ================= */
static int run_apcs(void){
  struct thread *t=cur_thr; if(!t || !t->napc) return 0;
  while(t->napc){ u64 f=t->apc_fn[0], a=t->apc_arg[0]; t->napc--; for(int i=0;i<t->napc;i++){ t->apc_fn[i]=t->apc_fn[i+1]; t->apc_arg[i]=t->apc_arg[i+1]; } ((void (WINAPI_*)(u64))f)(a); }
  return 1;
}
static DWORD WINAPI_ t_QueueUserAPC(u64 fn, HANDLE h, u64 arg){
  struct kobj *o=ko_get(h); struct thread *t=0;
  if(h==(HANDLE)-2) t=cur_thr; else if(o && o->kind==KO_THREAD && o->thr>=0 && o->thr<MAXTHR) t=&threads[o->thr];
  if(!t || t->napc>=8){ set_err(6); return 0; }
  t->apc_fn[t->napc]=fn; t->apc_arg[t->napc]=arg; t->napc++; return 1;
}

/* ================= waiting ================= */
#define WAIT_TIMEOUT 0x102
#define WAIT_IO_COMPLETION 0xC0
static int expired(u32 t0, DWORD ms){ return ms!=0xFFFFFFFF && (u32)(ticks-t0)>=ms; }
static DWORD wait_many(DWORD n, const HANDLE *hs, BOOL all, DWORD ms, BOOL alertable){
  if(!n || n>64){ set_err(87); return 0xFFFFFFFF; }
  for(u32 t0=ticks;;){
    if(all){
      int ok=1; for(DWORD i=0;i<n;i++){ int s=sig(hs[i],0); if(s==0){ ok=0; break; } }
      if(ok){ for(DWORD i=0;i<n;i++) sig(hs[i],1); return 0; }
    } else for(DWORD i=0;i<n;i++){ int s=sig(hs[i],1); if(s!=0) return i; }     /* handles that are not waitable objects (files) count as signaled */
    if(alertable && run_apcs()) return WAIT_IO_COMPLETION;
    if(expired(t0,ms)) return WAIT_TIMEOUT;
    idle_wait();
  }
}
#define TRACKED(what,arg,expr) do{ WAITING(what,arg); __typeof__(expr) r_=(expr); DONE_WAITING(); return r_; }while(0)
static DWORD WINAPI_ t_WaitForSingleObject(HANDLE h, DWORD ms){ TRACKED("WaitForSingleObject",h,wait_many(1,&h,0,ms,0)); }
static DWORD WINAPI_ t_WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL al){ TRACKED("WaitForSingleObjectEx",h,wait_many(1,&h,0,ms,al)); }
static DWORD WINAPI_ t_WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms){ TRACKED("WaitForMultipleObjects",hs[0],wait_many(n,hs,all,ms,0)); }
static DWORD WINAPI_ t_WaitForMultipleObjectsEx(DWORD n, const HANDLE *hs, BOOL all, DWORD ms, BOOL al){ TRACKED("WaitForMultipleObjectsEx",hs[0],wait_many(n,hs,all,ms,al)); }
static BOOL WINAPI_ t_SetEvent(HANDLE h);
static BOOL WINAPI_ t_ReleaseMutex(HANDLE h);
static BOOL WINAPI_ t_ReleaseSemaphore(HANDLE h, i32 c, i32 *prev);
static DWORD WINAPI_ t_SignalObjectAndWait(HANDLE s, HANDLE w, DWORD ms, BOOL al){
  struct kobj *o=ko_get(s); if(!o){ set_err(6); return 0xFFFFFFFF; }
  if(o->kind==KO_EVENT) t_SetEvent(s); else if(o->kind==KO_MUTEX) t_ReleaseMutex(s); else if(o->kind==KO_SEMA) t_ReleaseSemaphore(s,1,0);
  return wait_many(1,&w,0,ms,al);
}
static void WINAPI_ t_Sleep(DWORD ms){ if(!ms){ schedule(); return; } WAITING("Sleep",ms); u32 t0=ticks; do idle_wait(); while(!expired(t0,ms)); DONE_WAITING(); }
static DWORD WINAPI_ t_SleepEx(DWORD ms, BOOL al){ u32 t0=ticks; if(al && run_apcs()) return WAIT_IO_COMPLETION; if(!ms){ schedule(); return 0; } do { idle_wait(); if(al && run_apcs()) return WAIT_IO_COMPLETION; } while(!expired(t0,ms)); return 0; }
static BOOL WINAPI_ t_SwitchToThread(void){ schedule(); return 1; }
/* native timeouts: NULL = forever, negative = relative in 100 ns units, positive = absolute (treated as "now") */
static DWORD nt_ms(const i64 *t){ if(!t) return 0xFFFFFFFF; if(*t>=0) return 0; u64 ms=(u64)(-*t)/10000; return ms>=0xFFFFFFFEull? 0xFFFFFFFE : (DWORD)ms; }
static u32 nt_status(DWORD r){ return r==WAIT_TIMEOUT? 0x102 : r==WAIT_IO_COMPLETION? 0xC0 : r==0xFFFFFFFF? 0xC0000008 : r; }
static u32 WINAPI_ t_NtWaitForSingleObject(HANDLE h, BOOL al, const i64 *t){ TRACKED("NtWaitForSingleObject",h,nt_status(wait_many(1,&h,0,nt_ms(t),al))); }
static u32 WINAPI_ t_NtWaitForMultipleObjects(DWORD n, const HANDLE *hs, DWORD type, BOOL al, const i64 *t){ TRACKED("NtWaitForMultipleObjects",hs[0],nt_status(wait_many(n,hs,type==0,nt_ms(t),al))); }
static u32 WINAPI_ t_NtDelayExecution(BOOL al, const i64 *t){ return t_SleepEx(nt_ms(t),al)==WAIT_IO_COMPLETION? 0xC0 : 0; }
static u32 WINAPI_ t_NtYieldExecution(void){ schedule(); return 0; }

/* ================= events, mutexes, semaphores ================= */
static HANDLE make_event(const char *name, BOOL manual, BOOL init){
  int ex; struct kobj *o=ko_named(name,KO_EVENT,1,&ex); if(!o) return 0;
  if(!ex){ o->manual=manual!=0; o->count=init!=0; set_err(0); } else set_err(183);    /* ERROR_ALREADY_EXISTS */
  return ko_handle(o);
}
static HANDLE WINAPI_ t_CreateEventA(void *sa, BOOL man, BOOL init, const char *n){ (void)sa; return make_event(n,man,init); }
static HANDLE WINAPI_ t_CreateEventW(void *sa, BOOL man, BOOL init, const u16 *n){ (void)sa; char a[128]; w2a_(n,a,128); return make_event(n? a : 0,man,init); }
static HANDLE WINAPI_ t_CreateEventExA(void *sa, const char *n, DWORD fl, DWORD acc){ (void)sa;(void)acc; return make_event(n,fl&1,fl&2); }
static HANDLE WINAPI_ t_CreateEventExW(void *sa, const u16 *n, DWORD fl, DWORD acc){ (void)sa;(void)acc; char a[128]; w2a_(n,a,128); return make_event(n? a : 0,fl&1,fl&2); }
static HANDLE open_named(const char *n, int kind){ struct kobj *o=ko_named(n,kind,0,0); return o? ko_handle(o) : 0; }
static HANDLE WINAPI_ t_OpenEventA(DWORD acc, BOOL inh, const char *n){ (void)acc;(void)inh; return open_named(n,KO_EVENT); }
static HANDLE WINAPI_ t_OpenEventW(DWORD acc, BOOL inh, const u16 *n){ (void)acc;(void)inh; char a[128]; w2a_(n,a,128); return open_named(a,KO_EVENT); }
static BOOL WINAPI_ t_SetEvent(HANDLE h){ struct kobj *o=ko_get(h); if(!o||o->kind!=KO_EVENT){ set_err(6); return 0; } o->count=1; return 1; }
static BOOL WINAPI_ t_ResetEvent(HANDLE h){ struct kobj *o=ko_get(h); if(!o||o->kind!=KO_EVENT){ set_err(6); return 0; } o->count=0; return 1; }
static BOOL WINAPI_ t_PulseEvent(HANDLE h){ struct kobj *o=ko_get(h); if(!o){ set_err(6); return 0; } o->count=1; schedule(); o->count=0; return 1; }
static HANDLE make_mutex(const char *name, BOOL own){
  int ex; struct kobj *o=ko_named(name,KO_MUTEX,1,&ex); if(!o) return 0;
  if(!ex){ if(own){ o->owner=thread_cur_tid(); o->count=1; } set_err(0); } else set_err(183);
  return ko_handle(o);
}
static HANDLE WINAPI_ t_CreateMutexA(void *sa, BOOL own, const char *n){ (void)sa; return make_mutex(n,own); }
static HANDLE WINAPI_ t_CreateMutexW(void *sa, BOOL own, const u16 *n){ (void)sa; char a[128]; w2a_(n,a,128); return make_mutex(n? a : 0,own); }
static HANDLE WINAPI_ t_CreateMutexExW(void *sa, const u16 *n, DWORD fl, DWORD acc){ (void)sa;(void)acc; char a[128]; w2a_(n,a,128); return make_mutex(n? a : 0,fl&1); }
static HANDLE WINAPI_ t_OpenMutexW(DWORD acc, BOOL inh, const u16 *n){ (void)acc;(void)inh; char a[128]; w2a_(n,a,128); return open_named(a,KO_MUTEX); }
static BOOL WINAPI_ t_ReleaseMutex(HANDLE h){
  struct kobj *o=ko_get(h); if(!o||o->kind!=KO_MUTEX){ set_err(6); return 0; }
  if(!o->count || o->owner!=thread_cur_tid()){ set_err(288); return 0; }                 /* ERROR_NOT_OWNER */
  if(--o->count==0) o->owner=0; return 1;
}
static HANDLE make_sema(const char *name, i32 init, i32 mx){
  int ex; struct kobj *o=ko_named(name,KO_SEMA,1,&ex); if(!o) return 0;
  if(!ex){ o->count=init; o->max=mx>0? mx : 0x7FFFFFFF; set_err(0); } else set_err(183);
  return ko_handle(o);
}
static HANDLE WINAPI_ t_CreateSemaphoreA(void *sa, i32 init, i32 mx, const char *n){ (void)sa; return make_sema(n,init,mx); }
static HANDLE WINAPI_ t_CreateSemaphoreW(void *sa, i32 init, i32 mx, const u16 *n){ (void)sa; char a[128]; w2a_(n,a,128); return make_sema(n? a : 0,init,mx); }
static HANDLE WINAPI_ t_CreateSemaphoreExW(void *sa, i32 init, i32 mx, const u16 *n, DWORD fl, DWORD acc){ (void)fl;(void)acc; return t_CreateSemaphoreW(sa,init,mx,n); }
static HANDLE WINAPI_ t_OpenSemaphoreW(DWORD acc, BOOL inh, const u16 *n){ (void)acc;(void)inh; char a[128]; w2a_(n,a,128); return open_named(a,KO_SEMA); }
static BOOL WINAPI_ t_ReleaseSemaphore(HANDLE h, i32 c, i32 *prev){
  struct kobj *o=ko_get(h); if(!o||o->kind!=KO_SEMA){ set_err(6); return 0; }
  if(c<=0 || o->count+c>o->max){ set_err(298); return 0; }                               /* ERROR_TOO_MANY_POSTS */
  if(prev) *prev=o->count; o->count+=c; return 1;
}
static u32 WINAPI_ t_NtSetEvent(HANDLE h, i32 *prev){ struct kobj *o=ko_get(h); if(!o) return 0xC0000008; if(prev) *prev=o->count; o->count=1; return 0; }
static u32 WINAPI_ t_NtResetEvent(HANDLE h, i32 *prev){ struct kobj *o=ko_get(h); if(!o) return 0xC0000008; if(prev) *prev=o->count; o->count=0; return 0; }
static u32 WINAPI_ t_NtCreateEvent(HANDLE *out, DWORD acc, void *oa, DWORD type, BOOL init){ (void)acc;(void)oa; HANDLE h=make_event(0,type==0,init); if(!h) return 0xC0000017; *out=h; return 0; }

/* ================= waitable timers ================= */
static HANDLE make_timer(const char *name, BOOL manual){
  int ex; struct kobj *o=ko_named(name,KO_TIMER,1,&ex); if(!o) return 0;
  if(!ex){ o->manual=manual!=0; set_err(0); } else set_err(183); return ko_handle(o);
}
static HANDLE WINAPI_ t_CreateWaitableTimerA(void *sa, BOOL man, const char *n){ (void)sa; return make_timer(n,man); }
static HANDLE WINAPI_ t_CreateWaitableTimerW(void *sa, BOOL man, const u16 *n){ (void)sa; char a[128]; w2a_(n,a,128); return make_timer(n? a : 0,man); }
static HANDLE WINAPI_ t_CreateWaitableTimerExW(void *sa, const u16 *n, DWORD fl, DWORD acc){ (void)sa;(void)acc; char a[128]; w2a_(n,a,128); return make_timer(n? a : 0,fl&1); }   /* CREATE_WAITABLE_TIMER_MANUAL_RESET */
static HANDLE WINAPI_ t_CreateWaitableTimerExA(void *sa, const char *n, DWORD fl, DWORD acc){ (void)sa;(void)acc; return make_timer(n,fl&1); }
static HANDLE WINAPI_ t_OpenWaitableTimerW(DWORD acc, BOOL inh, const u16 *n){ (void)acc;(void)inh; char a[128]; w2a_(n,a,128); return open_named(a,KO_TIMER); }
static BOOL WINAPI_ t_SetWaitableTimerEx(HANDLE h, const i64 *due, i32 period, u64 fn, u64 arg, void *ctx, DWORD delay){
  (void)ctx;(void)delay; struct kobj *o=ko_get(h); if(!o || o->kind!=KO_TIMER || !due){ set_err(6); return 0; }
  u32 ms;
  if(*due<0) ms=(u32)((u64)(-*due)/10000);
  else { u64 now=0; ((void (WINAPI_*)(u64*))win32_resolve("kernel32","GetSystemTimeAsFileTime"))(&now); ms= (u64)*due>now? (u32)(((u64)*due-now)/10000) : 0; }   /* absolute FILETIME */
  o->due=ticks+ms; o->period= period>0? (u32)period : 0; o->armed=1; o->count=0;
  o->apc_fn=fn; o->apc_arg=arg; o->apc_thr=cur_thr; return 1;
}
static BOOL WINAPI_ t_SetWaitableTimer(HANDLE h, const i64 *due, i32 period, u64 fn, u64 arg, BOOL resume){ (void)resume; return t_SetWaitableTimerEx(h,due,period,fn,arg,0,0); }
static BOOL WINAPI_ t_CancelWaitableTimer(HANDLE h){ struct kobj *o=ko_get(h); if(!o || o->kind!=KO_TIMER){ set_err(6); return 0; } o->armed=0; return 1; }

/* ================= I/O completion ports: a queue of (bytes, key, OVERLAPPED*) packets =================
   Handles associated with a port are remembered; Willows' file and pipe I/O completes synchronously, so packets come from
   PostQueuedCompletionStatus (and from future asynchronous I/O). */
#define MAXASSOC 256
static struct { HANDLE h, port; u64 key; } assoc[MAXASSOC];
static int iocp_post(struct kobj *o, u32 bytes, u64 key, u64 ov){
  if(o->qt-o->qh>=o->qcap){                                        /* grow */
    u32 n= o->qcap? o->qcap*2 : 64; struct packet *q=heap_alloc((u64)n*sizeof *q); if(!q) return 0;
    for(u32 i=0;i<o->qt-o->qh;i++) q[i]=o->q[(o->qh+i)%o->qcap];
    o->qt-=o->qh; o->qh=0; if(o->q) heap_free(o->q); o->q=q; o->qcap=n;
  }
  o->q[o->qt%o->qcap]=(struct packet){key,ov,bytes}; o->qt++; return 1;
}
static HANDLE WINAPI_ t_CreateIoCompletionPort(HANDLE file, HANDLE port, u64 key, DWORD threads){
  (void)threads; struct kobj *o= port? ko_get(port) : 0;
  if(port && (!o || o->kind!=KO_IOCP)){ set_err(87); return 0; }
  if(!o){ o=ko_new(KO_IOCP); if(!o) return 0; port=ko_handle(o); }
  if(file!=(HANDLE)-1 && file){
    int slot=-1; for(int i=0;i<MAXASSOC;i++) if(assoc[i].h==file){ slot=i; break; }                 /* already associated: update it */
    if(slot<0) for(int i=0;i<MAXASSOC;i++) if(!assoc[i].h){ slot=i; break; }
    if(slot<0){ kdbg("  [iocp] no room to associate %p (all %d in use)\n",(void*)file,MAXASSOC); set_err(8); return 0; }
    assoc[slot].h=file; assoc[slot].port=port; assoc[slot].key=key;
  }
  return port;
}
static BOOL WINAPI_ t_PostQueuedCompletionStatus(HANDLE port, DWORD bytes, u64 key, u64 ov){
  struct kobj *o=ko_get(port); if(!o || o->kind!=KO_IOCP){ set_err(6); return 0; } return iocp_post(o,bytes,key,ov);
}
static int iocp_wait(struct kobj *o, DWORD ms, BOOL al){
  for(u32 t0=ticks; o->qh==o->qt; ){ if(al && run_apcs()) return -2; if(expired(t0,ms)) return -1; idle_wait(); }
  return 0;
}
static BOOL WINAPI_ t_GetQueuedCompletionStatus(HANDLE port, DWORD *bytes, u64 *key, u64 *ov, DWORD ms){
  struct kobj *o=ko_get(port); if(!o || o->kind!=KO_IOCP){ set_err(6); return 0; }
  WAITING("GetQueuedCompletionStatus",port); int r=iocp_wait(o,ms,0); DONE_WAITING();
  if(r){ if(ov) *ov=0; set_err(258); return 0; }                   /* WAIT_TIMEOUT */
  struct packet p=o->q[o->qh%o->qcap]; o->qh++;
  { static int t=-1; if(t<0){ struct ramfile *f=fs_find("/System/apitrace.cfg"); t= f && !f->isdir; }
    if(t) kdbg("  [iocp] port %p: packet (key %p, %u bytes, ov %p) taken by tid %x pid %d\n",(void*)port,(void*)p.key,p.bytes,(void*)p.ov,thread_cur_tid(),cur->pid); }
  if(bytes) *bytes=p.bytes; if(key) *key=p.key; if(ov) *ov=p.ov; return 1;
}
struct ov_entry { u64 key, ov, internal; u32 bytes; };
static BOOL WINAPI_ t_GetQueuedCompletionStatusEx(HANDLE port, struct ov_entry *e, u32 n, u32 *got, DWORD ms, BOOL al){
  struct kobj *o=ko_get(port); if(!o || o->kind!=KO_IOCP || !n){ set_err(6); return 0; }
  WAITING("GetQueuedCompletionStatusEx",port); int r=iocp_wait(o,ms,al); DONE_WAITING();
  if(r==-2){ if(got) *got=0; set_err(0xC0); return 0; }            /* WAIT_IO_COMPLETION */
  if(r){ if(got) *got=0; set_err(258); return 0; }
  u32 k=0; while(k<n && o->qh!=o->qt){ struct packet p=o->q[o->qh%o->qcap]; o->qh++; e[k].key=p.key; e[k].ov=p.ov; e[k].internal=0; e[k].bytes=p.bytes; k++; }
  if(got) *got=k; return 1;
}
/* for asynchronous I/O completions: queue a packet if h is associated with a port (returns 1 if it was) */
int iocp_complete(u64 h, u32 bytes, u64 ov){
  for(int i=0;i<MAXASSOC;i++) if(assoc[i].h==h && h){ struct kobj *o=ko_get(assoc[i].port); if(o && o->kind==KO_IOCP){ iocp_post(o,bytes,assoc[i].key,ov); return 1; } }
  return 0;
}
/* A handle was closed for good: its completion port association goes with it, as on Windows. Handle numbers are reused (pipes,
   sockets), and a stale association sent the new handle's completions, with the old key, to the old owner. */
void iocp_forget(u64 h){
  for(int i=0;i<MAXASSOC;i++) if(assoc[i].h==h && h){ assoc[i].h=0; assoc[i].port=0; assoc[i].key=0; }
}
u64 iocp_key_of(u64 h){ for(int i=0;i<MAXASSOC;i++) if(assoc[i].h==h && h) return assoc[i].key; return 0; }

/* ================= critical sections (CRITICAL_SECTION: +0x0C RecursionCount, +0x10 OwningThread) ================= */
struct cs { void *dbg; i32 lock, rec; u64 owner, sem, spin; };
static void WINAPI_ t_InitializeCriticalSection(struct cs *c){ memset(c,0,sizeof *c); c->lock=-1; }
static BOOL WINAPI_ t_InitializeCriticalSectionEx(struct cs *c, DWORD s, DWORD f){ (void)f; t_InitializeCriticalSection(c); c->spin=s; return 1; }
static BOOL WINAPI_ t_InitializeCriticalSectionAndSpinCount(struct cs *c, DWORD s){ t_InitializeCriticalSection(c); c->spin=s; return 1; }
static void WINAPI_ t_EnterCriticalSection(struct cs *c){
  u64 me=thread_cur_tid();
  if(c->rec>0 && c->owner==me){ c->rec++; return; }
  if(c->rec>0){ WAITING("EnterCriticalSection",c); while(c->rec>0) idle_wait(); DONE_WAITING(); }
  c->owner=me; c->rec=1; c->lock=0;
}
static void WINAPI_ t_LeaveCriticalSection(struct cs *c){ if(c->rec>0 && --c->rec==0){ c->owner=0; c->lock=-1; } }
static BOOL WINAPI_ t_TryEnterCriticalSection(struct cs *c){ u64 me=thread_cur_tid(); if(c->rec>0 && c->owner!=me) return 0; if(c->rec>0) c->rec++; else { c->owner=me; c->rec=1; c->lock=0; } return 1; }
static void WINAPI_ t_DeleteCriticalSection(struct cs *c){ (void)c; }
static DWORD WINAPI_ t_SetCriticalSectionSpinCount(struct cs *c, DWORD s){ DWORD o=(DWORD)c->spin; c->spin=s; return o; }
static u32 WINAPI_ t_RtlEnterCriticalSection(struct cs *c){ t_EnterCriticalSection(c); return 0; }
static u32 WINAPI_ t_RtlLeaveCriticalSection(struct cs *c){ t_LeaveCriticalSection(c); return 0; }
static u32 WINAPI_ t_RtlInitializeCriticalSection(struct cs *c){ t_InitializeCriticalSection(c); return 0; }
static u32 WINAPI_ t_RtlInitializeCriticalSectionAndSpinCount(struct cs *c, DWORD s){ t_InitializeCriticalSectionAndSpinCount(c,s); return 0; }
static u32 WINAPI_ t_RtlInitializeCriticalSectionEx(struct cs *c, DWORD s, DWORD f){ t_InitializeCriticalSectionEx(c,s,f); return 0; }
static u32 WINAPI_ t_RtlDeleteCriticalSection(struct cs *c){ (void)c; return 0; }

/* ================= SRW locks: one word, 1 = held exclusively, 2*n = n shared holders ================= */
static void WINAPI_ t_InitializeSRWLock(volatile u64 *l){ *l=0; }
static void WINAPI_ t_AcquireSRWLockExclusive(volatile u64 *l){ if(*l){ WAITING("AcquireSRWLockExclusive",l); while(*l) idle_wait(); DONE_WAITING(); } *l=1; }
static void WINAPI_ t_ReleaseSRWLockExclusive(volatile u64 *l){ *l=0; }
static void WINAPI_ t_AcquireSRWLockShared(volatile u64 *l){ if(*l&1){ WAITING("AcquireSRWLockShared",l); while(*l&1) idle_wait(); DONE_WAITING(); } *l+=2; }
static void WINAPI_ t_ReleaseSRWLockShared(volatile u64 *l){ if(*l>=2) *l-=2; }
static u8 WINAPI_ t_TryAcquireSRWLockExclusive(volatile u64 *l){ if(*l) return 0; *l=1; return 1; }
static u8 WINAPI_ t_TryAcquireSRWLockShared(volatile u64 *l){ if(*l&1) return 0; *l+=2; return 1; }

/* ================= condition variables: the word counts wake-ups; a sleeper waits until it changes ================= */
static void WINAPI_ t_InitializeConditionVariable(volatile u64 *c){ *c=0; }
static void WINAPI_ t_WakeConditionVariable(volatile u64 *c){ *c+=2; }          /* waking every sleeper is allowed (spurious wake-ups) */
static int cv_wait(volatile u64 *c, u64 gen, DWORD ms){ for(u32 t0=ticks; *c==gen; ){ if(expired(t0,ms)) return 0; idle_wait(); } return 1; }
static BOOL WINAPI_ t_SleepConditionVariableCS(volatile u64 *c, struct cs *cs, DWORD ms){
  u64 gen=*c; i32 rec=cs->rec; cs->rec=0; cs->owner=0; cs->lock=-1;
  WAITING("SleepConditionVariableCS",c); int ok=cv_wait(c,gen,ms); DONE_WAITING();
  t_EnterCriticalSection(cs); cs->rec=rec;
  if(!ok){ set_err(1460); return 0; } return 1;
}
static BOOL WINAPI_ t_SleepConditionVariableSRW(volatile u64 *c, volatile u64 *l, DWORD ms, DWORD fl){
  u64 gen=*c; int shared=fl&1; if(shared) t_ReleaseSRWLockShared(l); else t_ReleaseSRWLockExclusive(l);
  WAITING("SleepConditionVariableSRW",c); int ok=cv_wait(c,gen,ms); DONE_WAITING();
  if(shared) t_AcquireSRWLockShared(l); else t_AcquireSRWLockExclusive(l);
  if(!ok){ set_err(1460); return 0; } return 1;
}
static u32 WINAPI_ t_RtlSleepConditionVariableCS(volatile u64 *c, struct cs *cs, const i64 *t){ return t_SleepConditionVariableCS(c,cs,nt_ms(t))? 0 : 0x102; }
static u32 WINAPI_ t_RtlSleepConditionVariableSRW(volatile u64 *c, volatile u64 *l, const i64 *t, DWORD fl){ return t_SleepConditionVariableSRW(c,l,nt_ms(t),fl)? 0 : 0x102; }

/* ================= INIT_ONCE: 0 = not started, 1 = running, (context | 2) = done ================= */
static void WINAPI_ t_InitOnceInitialize(volatile u64 *o){ *o=0; }
static BOOL WINAPI_ t_InitOnceBeginInitialize(volatile u64 *o, DWORD fl, BOOL *pending, void **ctx){
  if(fl&2){ if(*o&2){ *pending=0; if(ctx) *ctx=(void*)(*o&~3ull); } else *pending=1; return 1; }     /* INIT_ONCE_CHECK_ONLY */
  while(*o==1) idle_wait();
  if(*o&2){ *pending=0; if(ctx) *ctx=(void*)(*o&~3ull); return 1; }
  *o=1; *pending=1; return 1;
}
static BOOL WINAPI_ t_InitOnceComplete(volatile u64 *o, DWORD fl, void *ctx){ if(fl&4){ *o=0; return 1; } *o=((u64)ctx&~3ull)|2; return 1; }   /* INIT_ONCE_INIT_FAILED */
static BOOL WINAPI_ t_InitOnceExecuteOnce(volatile u64 *o, BOOL (WINAPI_ *fn)(volatile u64 *, void *, void **), void *param, void **ctx){
  BOOL pending; t_InitOnceBeginInitialize(o,0,&pending,ctx); if(!pending) return 1;
  void *c=0; if(!fn || !fn(o,param,&c)){ *o=0; return 0; }
  *o=((u64)c&~3ull)|2; if(ctx) *ctx=c; return 1;
}
static u32 WINAPI_ t_RtlRunOnceExecuteOnce(volatile u64 *o, BOOL (WINAPI_ *fn)(volatile u64 *, void *, void **), void *param, void **ctx){ return t_InitOnceExecuteOnce(o,fn,param,ctx)? 0 : 0xC0000001; }

/* ================= TLS and FLS (per thread, in the TEB) ================= */
#define FLS_OFF (0x1C00/8)                                       /* 128 FLS slots at TEB+0x1C00 (the TEB is 0x2000 bytes) */
#define tls_used (cur->tls_used)
static u64 *tls_slot(u64 *teb, DWORD i){                         /* 64 slots in the TEB, 1024 more in TlsExpansionSlots (TEB+0x1780) */
  if(i<64) return &teb[0x1480/8+i];
  if(i>=64+1024) return 0;
  u64 *ex=(u64*)teb[0x1780/8]; if(!ex){ ex=heap_alloc(1024*8); if(!ex) return 0; teb[0x1780/8]=(u64)ex; }
  return &ex[i-64];
}
static u8 tls_ex_used[MAXPROC][128];                              /* bitmap of the expansion slots per process */
static char main_name[MAXPROC][32];
void thread_proc_reset(int pid){ memset(tls_ex_used[pid],0,sizeof tls_ex_used[pid]); main_name[pid][0]=0; }   /* a new process in this slot */
static void each_teb(int pid, void (*f)(u64 *teb, u64 a, u64 b), u64 a, u64 b){
  if(procs[pid].teb) f(procs[pid].teb,a,b);
  for(int i=0;i<MAXTHR;i++) if(threads[i].used && !threads[i].zombie && threads[i].pid==pid) f(threads[i].teb,a,b);
}
static void zero_tls(u64 *teb, u64 i, u64 unused){ (void)unused; u64 *s=tls_slot(teb,(DWORD)i); if(s) *s=0; }
static DWORD WINAPI_ t_TlsAlloc(void){
  for(int i=0;i<64;i++) if(!tls_used[i]){ tls_used[i]=1; each_teb(cur->pid,zero_tls,i,0); return i; }
  for(int i=0;i<1024;i++) if(!(tls_ex_used[cur->pid][i/8]&(1<<(i%8)))){ tls_ex_used[cur->pid][i/8]|=(u8)(1<<(i%8)); each_teb(cur->pid,zero_tls,64+i,0); return 64+i; }
  set_err(8); return 0xFFFFFFFF;
}
static BOOL WINAPI_ t_TlsFree(DWORD i){ if(i<64) tls_used[i]=0; else if(i<64+1024) tls_ex_used[cur->pid][(i-64)/8]&=(u8)~(1<<((i-64)%8)); else { set_err(87); return 0; } return 1; }
static void *WINAPI_ t_TlsGetValue(DWORD i){ u64 *s=teb_now()? tls_slot(teb_now(),i) : 0; if(!s){ set_err(87); return 0; } set_err(0); return (void*)*s; }
static BOOL WINAPI_ t_TlsSetValue(DWORD i, void *v){ u64 *s=teb_now()? tls_slot(teb_now(),i) : 0; if(!s){ set_err(87); return 0; } *s=(u64)v; return 1; }
static void zero_fls(u64 *teb, u64 i, u64 unused){ (void)unused; teb[FLS_OFF+i]=0; }
static DWORD WINAPI_ t_FlsAlloc(u64 cb){ for(int i=1;i<128;i++) if(!cur->fls_used[i]){ cur->fls_used[i]=1; cur->fls_cb[i]=cb; each_teb(cur->pid,zero_fls,i,0); return i; } set_err(8); return 0xFFFFFFFF; }
static BOOL WINAPI_ t_FlsFree(DWORD i){ if(i>=128 || !cur->fls_used[i]){ set_err(87); return 0; } cur->fls_used[i]=0; cur->fls_cb[i]=0; return 1; }
static void *WINAPI_ t_FlsGetValue(DWORD i){ if(i>=128 || !teb_now()){ set_err(87); return 0; } set_err(0); return (void*)teb_now()[FLS_OFF+i]; }
static BOOL WINAPI_ t_FlsSetValue(DWORD i, void *v){ if(i>=128 || !teb_now()){ set_err(87); return 0; } teb_now()[FLS_OFF+i]=(u64)v; return 1; }
static BOOL WINAPI_ t_IsThreadAFiber(void){ return 0; }

/* ================= threads ================= */
static u32 next_tid=0x1000;
static HANDLE thread_handle(int thr, u32 tid){
  struct kobj *o=ko_new(KO_THREAD); if(!o) return 0; o->thr=thr; o->tid=tid; return ko_handle(o);
}
static void thread_done(struct thread *t, u32 code){             /* thread objects remember the exit code */
  for(int i=0;i<MAXKO;i++) if(ko[i].used && ko[i].kind==KO_THREAD && ko[i].thr==t-threads && ko[i].tid==t->tid && !ko[i].done){ ko[i].done=1; ko[i].code=code; }
  t->done=1; t->code=code; t->zombie=1;
}
void thread_free_zombies(void){                                   /* stacks and TEBs of finished threads (never the running one) */
  for(int i=0;i<MAXTHR;i++){ struct thread *t=&threads[i]; if(!t->used || !t->zombie || t==cur_thr) continue;
    u64 *arr=(u64*)t->teb[0x58/8]; (void)arr;
    if(t->teb[0x1780/8]) heap_free((void*)t->teb[0x1780/8]);
    vmem_release(t->stack); vmem_release(t->teb); t->used=0; }
}
void thread_kill_all(int pid){
  for(int i=0;i<MAXTHR;i++) if(threads[i].used && threads[i].pid==pid && !threads[i].done) thread_done(&threads[i],0xC000013A);
  for(int i=0;i<MAXKO;i++) if(ko[i].used && ko[i].kind==KO_THREAD && ko[i].thr==MAXTHR+pid && !ko[i].done){ ko[i].done=1; ko[i].code=proc_last_exit[pid]; }
}
int thread_runnable(int i){ struct thread *t=&threads[i]; return t->used && !t->done && !t->suspended && procs[t->pid].used; }
static void fls_callbacks(void){
  u64 *teb=teb_now(); if(!teb) return;
  for(int i=1;i<128;i++) if(cur->fls_used[i] && cur->fls_cb[i] && teb[FLS_OFF+i]){ u64 v=teb[FLS_OFF+i]; teb[FLS_OFF+i]=0; ((void (WINAPI_*)(u64))cur->fls_cb[i])(v); }
}
static void WINAPI_ t_ExitThread(DWORD code){
  struct thread *t=cur_thr;
  fls_callbacks();
  if(!t){                                                          /* the main thread: the process lives on while other threads run */
    int others=0; for(int i=0;i<MAXTHR;i++) if(threads[i].used && !threads[i].done && threads[i].pid==cur->pid) others++;
    if(!others) proc_exit((int)code);
    cur->main_done=1; for(int i=0;i<MAXKO;i++) if(ko[i].used && ko[i].kind==KO_THREAD && ko[i].thr==MAXTHR+cur->pid){ ko[i].done=1; ko[i].code=code; }
    for(;;) schedule();
  }
  dll_thread_notify(t->pid,3);                                     /* DLL_THREAD_DETACH */
  thread_done(t,code);
  int others=!cur->main_done; for(int i=0;i<MAXTHR;i++) if(threads[i].used && !threads[i].done && threads[i].pid==cur->pid) others++;
  if(!others) proc_exit((int)code);                                /* the last thread of a process whose main thread already left */
  for(;;) schedule();
}
void thread_main(void){                                            /* first activation of a new thread (thread_start in boot.S) */
  struct thread *t=cur_thr;
  dll_thread_notify(t->pid,2);                                     /* DLL_THREAD_ATTACH, TLS callbacks */
  u32 code=((DWORD (WINAPI_*)(u64))t->fn)(t->arg);
  t_ExitThread(code);
}
static void tls_init_thread(u64 *teb, int pid){                   /* implicit TLS blocks of the program and its DLLs */
  struct proc *p=&procs[pid]; u64 *arr=heap_alloc_for(pid,64*8); if(!arr) return; teb[0x58/8]=(u64)arr;   /* freed with the process */
  if(p->img.tls_end>p->img.tls_start || p->img.tls_index){
    u64 raw=p->img.tls_end-p->img.tls_start, total=raw+p->img.tls_zero; u8 *blk=heap_alloc_for64(pid,total+16);
    if(blk && raw) memcpy(blk,(void*)p->img.tls_start,raw); arr[0]=(u64)blk;
  }
  dll_tls_fill(pid,arr);
}
static HANDLE create_thread(u64 stk, u64 fn, u64 arg, DWORD fl, DWORD *tid){
  thread_free_zombies();
  int idx=-1; for(int i=0;i<MAXTHR;i++) if(!threads[i].used){ idx=i; break; }
  if(idx<0 || !cur || cur==&procs[0]){ kdbg("  [thread] cannot create a thread (table full)\n"); set_err(8); return 0; }
  if(cur->img.is32){ kdbg("  [thread] threads in 32-bit programs are not supported yet\n"); set_err(50); return 0; }
  u64 sz= stk? stk : 0x100000; if(sz<0x40000) sz=0x40000; if(sz>0x800000) sz=0x800000; sz=(sz+0xFFFF)&~0xFFFFull;
  u8 *stack=vmem_alloc_stack(sz); u64 *teb=vmem_alloc(0x2000);       /* the stack grows on demand */
  if(!stack || !teb){ kdbg("  [thread] cannot create a thread: out of memory (stack %u KB)\n",(u32)(sz>>10)); if(stack) vmem_release(stack); if(teb) vmem_release(teb); set_err(8); return 0; }
  struct thread *t=&threads[idx]; memset(t,0,sizeof *t);
  t->used=1; t->pid=cur->pid; t->stack=stack; t->teb=teb; t->tid=next_tid; next_tid+=4; t->fn=fn; t->arg=arg; t->suspended=(fl&4)!=0;
  teb[0]=~0ull; teb[1]=(u64)stack+sz; teb[2]=(u64)stack; teb[0x30/8]=(u64)teb; teb[0x40/8]=cur->pid; teb[0x48/8]=t->tid; teb[0x60/8]=(u64)cur->peb;
  tls_init_thread(teb,cur->pid);
  u64 *sp=(u64*)(((u64)stack+sz-64)&~15ull); sp-=8;               /* the same initial frame as a new process (see spawn_core) */
  for(int k=0;k<6;k++) sp[k]=0; sp[6]=(u64)thread_start;
  sp-=20; for(int k=0;k<20;k++) sp[k]=0;
  t->rsp=(u64)sp;
  if(tid) *tid=t->tid;
  HANDLE h=thread_handle(idx,t->tid); if(!h){ t->used=0; vmem_release(stack); vmem_release(teb); return 0; }
  kdbg("  [thread] %s: new thread %x (start %p)\n",cur->name,t->tid,(void*)fn);
  return h;
}
static HANDLE WINAPI_ t_CreateThread(void *sa, u64 stk, u64 fn, u64 arg, DWORD fl, DWORD *tid){ (void)sa; return create_thread((fl&0x10000)? stk : stk,fn,arg,fl,tid); }
static HANDLE WINAPI_ t_CreateRemoteThread(HANDLE p, void *sa, u64 stk, u64 fn, u64 arg, DWORD fl, DWORD *tid){
  if(p!=(HANDLE)-1 && p!=0x8000+(HANDLE)cur->pid){ set_err(5); return 0; } (void)sa; return create_thread(stk,fn,arg,fl,tid);
}
static HANDLE WINAPI_ t_CreateRemoteThreadEx(HANDLE p, void *sa, u64 stk, u64 fn, u64 arg, DWORD fl, void *attr, DWORD *tid){ (void)attr; return t_CreateRemoteThread(p,sa,stk,fn,arg,fl,tid); }
static struct thread *thr_of(HANDLE h){
  if(h==(HANDLE)-2) return cur_thr;
  struct kobj *o=ko_get(h); if(!o || o->kind!=KO_THREAD || o->thr<0 || o->thr>=MAXTHR) return 0;
  struct thread *t=&threads[o->thr]; return (t->used && t->tid==o->tid)? t : 0;
}
static DWORD WINAPI_ t_ResumeThread(HANDLE h){
  struct kobj *o=ko_get(h);
  if(o && o->kind==KO_THREAD && o->thr>=MAXTHR){ struct proc *p=&procs[o->thr-MAXTHR]; DWORD was=p->main_suspended; p->main_suspended=0; return was; }   /* a process started suspended */
  struct thread *t=thr_of(h); if(!t) return 0; DWORD was=t->suspended; t->suspended=0; return was;
}
u64 thread_main_handle(int pid){ return thread_handle(MAXTHR+pid,8+pid*4); }   /* a handle to a process's main thread (PROCESS_INFORMATION.hThread) */
static DWORD WINAPI_ t_SuspendThread(HANDLE h){ struct thread *t=thr_of(h); if(!t || t==cur_thr) return 0; DWORD was=t->suspended; t->suspended=1; return was; }
static BOOL WINAPI_ t_GetExitCodeThread(HANDLE h, DWORD *c){
  if(h==(HANDLE)-2){ if(c) *c=259; return 1; }
  struct kobj *o=ko_get(h); if(!o || o->kind!=KO_THREAD){ set_err(6); return 0; }
  if(c) *c= thr_alive(o)? 259 : o->code; return 1;
}
static BOOL WINAPI_ t_TerminateThread(HANDLE h, DWORD code){
  struct thread *t=thr_of(h);
  if(!t){ struct kobj *o=ko_get(h); if(o && o->kind==KO_THREAD && o->thr==MAXTHR+cur->pid) proc_exit((int)code); set_err(6); return 0; }
  if(t==cur_thr) t_ExitThread(code);
  thread_done(t,code); return 1;
}
static DWORD WINAPI_ t_GetCurrentThreadId(void){ return thread_cur_tid(); }
static DWORD WINAPI_ t_GetCurrentProcessId(void){ return cur? (DWORD)cur->pid : 0; }
static HANDLE WINAPI_ t_GetCurrentThread(void){ return (HANDLE)-2; }
static DWORD WINAPI_ t_GetThreadId(HANDLE h){
  if(h==(HANDLE)-2) return thread_cur_tid();
  struct kobj *o=ko_get(h); if(!o || o->kind!=KO_THREAD){ set_err(6); return 0; } return o->tid;
}
static HANDLE WINAPI_ t_OpenThread(DWORD acc, BOOL inh, DWORD tid){
  (void)acc;(void)inh;
  for(int i=0;i<MAXTHR;i++) if(threads[i].used && !threads[i].done && threads[i].tid==tid) return thread_handle(i,tid);
  for(int p=1;p<MAXPROC;p++) if(procs[p].used && procs[p].teb && *(u32*)((u8*)procs[p].teb+0x48)==tid) return thread_handle(MAXTHR+p,tid);
  set_err(87); return 0;
}
static DWORD WINAPI_ t_GetProcessIdOfThread(HANDLE h){ struct thread *t=thr_of(h); return t? (DWORD)t->pid : (DWORD)cur->pid; }
                                                                    /* main threads' descriptions: main_name (other threads keep theirs in struct thread) */
static char *thread_name_slot(HANDLE h){
  struct thread *t=thr_of(h); if(t) return t->name;
  if(h==(HANDLE)-2 || (ko_get(h) && ko_get(h)->kind==KO_THREAD && ko_get(h)->thr==MAXTHR+cur->pid)) return cur_thr? cur_thr->name : main_name[cur->pid];
  return 0;
}
static u32 WINAPI_ t_SetThreadDescription(HANDLE h, const u16 *d){
  char *n=thread_name_slot(h); if(!n) return 0xC0000008; int i=0; for(;d && d[i] && i<31;i++) n[i]= d[i]<0x80? (char)d[i] : '?'; n[i]=0; return 0;
}
static u32 WINAPI_ t_GetThreadDescription(HANDLE h, u16 **d){
  char *n=thread_name_slot(h); u16 *s=heap_alloc_prog(64); if(!s) return 0xC0000017; int i=0; for(;n && n[i];i++) s[i]=(u8)n[i]; s[i]=0; if(d) *d=s; return 0;
}
static BOOL WINAPI_ t_SetThreadPriority(HANDLE h, int p){ (void)h;(void)p; return 1; }
static int WINAPI_ t_GetThreadPriority(HANDLE h){ (void)h; return 0; }
static BOOL WINAPI_ t_SetThreadPriorityBoost(HANDLE h, BOOL b){ (void)h;(void)b; return 1; }
static DWORD WINAPI_ t_SetThreadIdealProcessor(HANDLE h, DWORD p){ (void)h;(void)p; return 0; }
static BOOL WINAPI_ t_SetThreadStackGuarantee(DWORD *sz){ if(sz) *sz=0; return 1; }
static void WINAPI_ t_GetCurrentThreadStackLimits(u64 *lo, u64 *hi){ u64 *teb=teb_now(); if(lo) *lo= teb? teb[2] : 0; if(hi) *hi= teb? teb[1] : 0; }
static BOOL WINAPI_ t_GetThreadIOPendingFlag(HANDLE h, BOOL *f){ (void)h; if(f) *f=0; return 1; }
static BOOL WINAPI_ t_DuplicateHandle(HANDLE sp, HANDLE sh, HANDLE tp, HANDLE *th, DWORD acc, BOOL inh, DWORD opt){
  (void)sp;(void)tp;(void)acc;(void)inh;
  if(sh==(HANDLE)-2){                                              /* a real handle for the calling thread */
    HANDLE h= cur_thr? thread_handle((int)(cur_thr-threads),cur_thr->tid) : thread_handle(MAXTHR+cur->pid,thread_cur_tid()); if(th) *th=h; return h!=0; }
  if(sh==(HANDLE)-1){ if(th) *th=0x8000+(HANDLE)cur->pid; return 1; }
  struct kobj *o=ko_get(sh);
  if(o){ int tpid= tp>=0x8000 && tp<0x8000+MAXPROC? (int)(tp-0x8000) : my_pid(), spid= sp>=0x8000 && sp<0x8000+MAXPROC? (int)(sp-0x8000) : my_pid();
    kobj_addref_for(sh,tpid); if(opt&1) kobj_close_for(sh,spid); if(th) *th=sh; return 1; }   /* the copy belongs to the target process */
  if(pipe_is_handle(sh)){                                          /* the copy belongs to the target process, the closed source to the source process */
    int tpid= tp>=0x8000 && tp<0x8000+MAXPROC? (int)(tp-0x8000) : cur->pid, spid= sp>=0x8000 && sp<0x8000+MAXPROC? (int)(sp-0x8000) : cur->pid;
    pipe_addref_for(sh,tpid); if(opt&1) pipe_close_for(sh,spid); }
  if(map_is_handle(sh)){ map_addref(sh); if(opt&1) map_close(sh); }              /* file mappings are reference-counted too */
  if(fs_is_handle(sh)){ extern void fs_haddref(u64); fs_haddref(sh); if(opt&1) fs_hclose(sh); }   /* and files */
  if(th) *th=sh; return 1;                                         /* other handles are global: a duplicate is the same value */
}
/* C runtime thread functions */
struct bt { u64 fn, arg; int ex; };
static DWORD WINAPI_ bt_trampoline(u64 a){
  struct bt b=*(struct bt*)a; heap_free((void*)a);
  if(b.ex) return ((u32 (WINAPI_*)(u64))b.fn)(b.arg);
  ((void (*)(u64))b.fn)(b.arg); return 0;                           /* _beginthread: cdecl, void */
}
static u64 WINAPI_ t__beginthreadex(void *sec, u32 stk, u64 fn, u64 arg, u32 fl, u32 *tid){
  (void)sec; struct bt *b=heap_alloc(sizeof *b); if(!b) return 0; b->fn=fn; b->arg=arg; b->ex=1;
  HANDLE h=create_thread(stk,(u64)bt_trampoline,(u64)b,fl,(DWORD*)tid); if(!h) heap_free(b); return h;
}
static u64 WINAPI_ t__beginthread(u64 fn, u32 stk, u64 arg){
  struct bt *b=heap_alloc(sizeof *b); if(!b) return (u64)-1; b->fn=fn; b->arg=arg; b->ex=0;
  HANDLE h=create_thread(stk,(u64)bt_trampoline,(u64)b,0,0); if(!h){ heap_free(b); return (u64)-1; } return h;
}
static void WINAPI_ t__endthreadex(u32 c){ t_ExitThread(c); }
static void WINAPI_ t__endthread(void){ t_ExitThread(0); }

#define T(n) {"kernel32",#n,t_##n}
#define TN(n) {"ntdll",#n,t_##n}
#define TC(n) {"msvcrt",#n,t_##n}
const struct export thread_exports[] = {
  T(CreateThread), T(CreateRemoteThread), T(CreateRemoteThreadEx), T(ExitThread), T(ResumeThread), T(SuspendThread), T(GetExitCodeThread), T(TerminateThread),
  T(GetCurrentThreadId), T(GetCurrentProcessId), T(GetCurrentThread), T(GetThreadId), T(OpenThread), T(GetProcessIdOfThread), T(SetThreadDescription), T(GetThreadDescription),
  T(SetThreadPriority), T(GetThreadPriority), T(SetThreadPriorityBoost), T(SetThreadIdealProcessor), T(SetThreadStackGuarantee), T(GetCurrentThreadStackLimits), T(GetThreadIOPendingFlag),
  T(QueueUserAPC), T(DuplicateHandle), T(CreateWaitableTimerA), T(CreateWaitableTimerW), T(CreateWaitableTimerExW), T(CreateWaitableTimerExA), T(OpenWaitableTimerW),
  T(SetWaitableTimer), T(SetWaitableTimerEx), T(CancelWaitableTimer),T(CreateIoCompletionPort), T(PostQueuedCompletionStatus), T(GetQueuedCompletionStatus), T(GetQueuedCompletionStatusEx),
  T(WaitForSingleObject), T(WaitForSingleObjectEx), T(WaitForMultipleObjects), T(WaitForMultipleObjectsEx), T(SignalObjectAndWait), T(Sleep), T(SleepEx), T(SwitchToThread),
  T(CreateEventA), T(CreateEventW), T(CreateEventExA), T(CreateEventExW), T(OpenEventA), T(OpenEventW), T(SetEvent), T(ResetEvent), T(PulseEvent),
  T(CreateMutexA), T(CreateMutexW), T(CreateMutexExW), {"kernel32","CreateMutexExA",t_CreateMutexExW}, T(OpenMutexW), T(ReleaseMutex),
  T(CreateSemaphoreA), T(CreateSemaphoreW), T(CreateSemaphoreExW), T(OpenSemaphoreW), T(ReleaseSemaphore),
  T(InitializeCriticalSection), T(InitializeCriticalSectionEx), T(InitializeCriticalSectionAndSpinCount), T(EnterCriticalSection), T(LeaveCriticalSection),
  T(TryEnterCriticalSection), T(DeleteCriticalSection), T(SetCriticalSectionSpinCount),
  T(InitializeSRWLock), T(AcquireSRWLockExclusive), T(ReleaseSRWLockExclusive), T(AcquireSRWLockShared), T(ReleaseSRWLockShared), T(TryAcquireSRWLockExclusive), T(TryAcquireSRWLockShared),
  T(InitializeConditionVariable), T(WakeConditionVariable), {"kernel32","WakeAllConditionVariable",t_WakeConditionVariable}, T(SleepConditionVariableCS), T(SleepConditionVariableSRW),
  T(InitOnceInitialize), T(InitOnceBeginInitialize), T(InitOnceComplete), T(InitOnceExecuteOnce),
  T(TlsAlloc), T(TlsFree), T(TlsGetValue), T(TlsSetValue), T(FlsAlloc), T(FlsFree), T(FlsGetValue), T(FlsSetValue), T(IsThreadAFiber),
  TN(NtWaitForSingleObject), TN(NtWaitForMultipleObjects), TN(NtDelayExecution), TN(NtYieldExecution), TN(NtSetEvent), TN(NtResetEvent), TN(NtCreateEvent),
  {"ntdll","ZwWaitForSingleObject",t_NtWaitForSingleObject}, {"ntdll","ZwDelayExecution",t_NtDelayExecution},
  TN(NtQueryObject), {"ntdll","ZwQueryObject",t_NtQueryObject}, TN(NtSetInformationObject), {"ntdll","ZwSetInformationObject",t_NtSetInformationObject},
  TN(RtlEnterCriticalSection), TN(RtlLeaveCriticalSection), TN(RtlInitializeCriticalSection), TN(RtlInitializeCriticalSectionAndSpinCount), TN(RtlInitializeCriticalSectionEx),
  TN(RtlDeleteCriticalSection), {"ntdll","RtlTryEnterCriticalSection",t_TryEnterCriticalSection},
  {"ntdll","RtlInitializeSRWLock",t_InitializeSRWLock}, {"ntdll","RtlAcquireSRWLockExclusive",t_AcquireSRWLockExclusive}, {"ntdll","RtlReleaseSRWLockExclusive",t_ReleaseSRWLockExclusive},
  {"ntdll","RtlAcquireSRWLockShared",t_AcquireSRWLockShared}, {"ntdll","RtlReleaseSRWLockShared",t_ReleaseSRWLockShared},
  {"ntdll","RtlTryAcquireSRWLockExclusive",t_TryAcquireSRWLockExclusive}, {"ntdll","RtlTryAcquireSRWLockShared",t_TryAcquireSRWLockShared},
  {"ntdll","RtlInitializeConditionVariable",t_InitializeConditionVariable}, {"ntdll","RtlWakeConditionVariable",t_WakeConditionVariable},
  {"ntdll","RtlWakeAllConditionVariable",t_WakeConditionVariable}, TN(RtlSleepConditionVariableCS), TN(RtlSleepConditionVariableSRW), TN(RtlRunOnceExecuteOnce),
  {"ntdll","RtlRunOnceInitialize",t_InitOnceInitialize},
  TC(_beginthreadex), TC(_beginthread), TC(_endthreadex), TC(_endthread),
};
const unsigned thread_nexports = sizeof thread_exports / sizeof *thread_exports;
