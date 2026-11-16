#include "kernel.h"
/* Inter-process communication: anonymous and named pipes, file mappings (shared memory), and a few file APIs that go with them.
   All programs share one address space, so a mapping is just a heap block that several handles/views point at. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE;
void win32_set_last_error(u32 e);
static void *K32(const char *n){ return win32_resolve("kernel32",n); }
static void w2a(const u16 *w, char *a, int n){ int i=0; for(;w&&w[i]&&i<n-1;i++) a[i]= w[i]<0x80? (char)w[i] : '?'; a[i]=0; }
static int a2w(const char *a, u16 *w, int n){ int i=0; for(;a[i]&&i<n-1;i++) w[i]=(u8)a[i]; w[i]=0; return i; }
#define ERR_BROKEN_PIPE 109
#define ERR_NO_DATA 232
#define ERR_PIPE_CONNECTED 535
#define ERR_ALREADY_EXISTS 183
#define ERR_FILE_NOT_FOUND 2
#define ERR_INVALID_HANDLE 6
#define ERR_INVALID_PARAMETER 87

/* ================= pipes =================
   handle = PIPE_BASE + index*2 + side. Anonymous: side 0 reads ring 0, side 1 writes ring 0.
   Named (duplex): the server (side 0) reads ring 0 and writes ring 1, the client (side 1) the opposite.
   Each ring grows as needed, so writes never block. Reads with an OVERLAPPED (IPC in Chromium-based programs and Firefox)
   are asynchronous: if no data is there they return ERROR_IO_PENDING and complete later (OVERLAPPED status, event,
   completion port packet) when the other side writes or closes. */
#define MAXPIPE 96
#define PIPE_BASE 0x7B00
#define RING0 65536u
#define RINGMAX (256u<<20)
#define PNAME 128
struct pipe { u8 used, named, connected; int refs[2], pid[2]; u8 own[2][MAXPROC]; char name[PNAME];   /* own: each process's references to each end (released when it exits) */ u8 *buf[2]; u32 cap[2], r[2], w[2]; };
static struct pipe P[MAXPIPE];
int pipe_is_handle(u64 h){ if(h<PIPE_BASE || h>=PIPE_BASE+MAXPIPE*2) return 0; return P[(h-PIPE_BASE)>>1].used; }
static struct pipe *pp(u64 h){ return &P[(h-PIPE_BASE)>>1]; }
static int side(u64 h){ return (int)((h-PIPE_BASE)&1); }
static int rx_ring(struct pipe *p, int s){ return p->named? (s==0? 0 : 1) : (s==0? 0 : -1); }
static int tx_ring(struct pipe *p, int s){ return p->named? (s==0? 1 : 0) : (s==1? 0 : -1); }
static struct pipe *pipe_alloc(int named){
  for(int i=0;i<MAXPIPE;i++) if(!P[i].used){ struct pipe *p=&P[i]; memset(p,0,sizeof *p); p->buf[0]=heap_alloc(RING0); p->buf[1]=named? heap_alloc(RING0) : 0; if(!p->buf[0] || (named && !p->buf[1])){ heap_free(p->buf[0]); heap_free(p->buf[1]); return 0; } p->cap[0]=p->cap[1]=RING0; p->used=1; p->named=(u8)named; return p; }
  return 0;
}
static int ring_grow(struct pipe *p, int rg, u32 need){                       /* room for `need` more bytes */
  u32 have=p->w[rg]-p->r[rg]; if(p->cap[rg]-have>=need) return 1;
  u32 nc=p->cap[rg]; while(nc-have<need && nc<RINGMAX) nc*=2; if(nc-have<need) return 0;
  u8 *nb=heap_alloc(nc); if(!nb) return 0;
  for(u32 i=0;i<have;i++) nb[i]=p->buf[rg][(p->r[rg]+i)%p->cap[rg]];
  heap_free(p->buf[rg]); p->buf[rg]=nb; p->cap[rg]=nc; p->r[rg]=0; p->w[rg]=have; return 1;
}
static u32 ring_take(struct pipe *p, int rg, u8 *d, u32 n){
  u32 avail=p->w[rg]-p->r[rg]; if(n>avail) n=avail;
  for(u32 i=0;i<n;i++) d[i]=p->buf[rg][(p->r[rg]+i)%p->cap[rg]];
  p->r[rg]+=n; return n;
}

/* ---- asynchronous operations ---- */
#define STATUS_PENDING 0x103
#define STATUS_PIPE_BROKEN 0xC000014Bu
#define STATUS_CANCELLED 0xC0000120u
#define MAXPEND 192
static struct pend { u8 used, connect; int pid; u64 h; u8 *buf; u32 n; u8 *ov; } PD[MAXPEND];
static void pipe_release_all(int pid);
void pipe_proc_exit(int pid){                                                    /* a process ends: its pending reads (their buffers are gone), then its pipe ends */
  for(int i=0;i<MAXPEND;i++) if(PD[i].used && PD[i].pid==pid) PD[i].used=0;
  pipe_release_all(pid);
}
int iocp_complete(u64 h, u32 bytes, u64 ov);
static int pipe_trace(void){ static int t=-1; if(t<0){ struct ramfile *f=fs_find("/System/apitrace.cfg"); t= f && !f->isdir; } return t; }   /* log pipe traffic while tracing */
static void ov_finish(u64 h, u8 *ov, u32 status, u32 bytes, int post){         /* fill in the OVERLAPPED, set its event, queue the packet */
  *(u64*)ov=status; *(u64*)(ov+8)=bytes;
  u64 ev=*(u64*)(ov+24);
  if(ev&~1ull){ BOOL (WINAPI *se)(HANDLE)=K32("SetEvent"); if(se) se(ev&~1ull); }
  int queued= post && !(ev&1)? iocp_complete(h,bytes,(u64)ov) : 0;                /* hEvent with the low bit set: no packet */
  if(status && pipe_is_handle(h) && pipe_trace()){ extern u64 iocp_key_of(u64); kdbg("  [pipe!] %p: completion status %x, key %p, ov %p, %s (pid %d)\n",(void*)h,status,(void*)iocp_key_of(h),(void*)ov,queued? "packet queued" : "no packet",cur->pid); }
  if(status==STATUS_PIPE_BROKEN){ struct pipe *p=pp(h); kdbg("  [pipe!] %p (%s) side %d: broken (refs %d/%d, connected %d) in pid %d\n",(void*)h,p->named? p->name : "anon",side(h),p->refs[0],p->refs[1],p->connected,cur->pid); }
  if(pipe_trace()) kdbg("  [pipe] %p: completed (status %x, %u bytes, event %p, %s) in pid %d\n",(void*)h,status,bytes,(void*)ev,queued? "packet queued" : "no port",cur->pid);
}
static void pipe_kick(struct pipe *p){                                          /* finish what can be finished */
  for(int i=0;i<MAXPEND;i++){ struct pend *q=&PD[i]; if(!q->used || !pipe_is_handle(q->h) || pp(q->h)!=p) continue;
    int s=side(q->h);
    if(q->connect){ if(p->connected){ q->used=0; ov_finish(q->h,q->ov,0,0,1); } continue; }
    int rr=rx_ring(p,s);
    if(p->w[rr]!=p->r[rr]){ u32 got=ring_take(p,rr,q->buf,q->n); q->used=0; ov_finish(q->h,q->ov,0,got,1); }
    else if(p->refs[1-s]<=0 && (!p->named || p->connected || s==1)){ q->used=0; ov_finish(q->h,q->ov,STATUS_PIPE_BROKEN,0,1); }
  }
}
static int pend_add(u64 h, int connect, u8 *buf, u32 n, u8 *ov){
  for(int i=0;i<MAXPEND;i++) if(!PD[i].used){ PD[i]=(struct pend){1,(u8)connect,cur->pid,h,buf,n,ov}; *(u64*)ov=STATUS_PENDING; *(u64*)(ov+8)=0; return 1; }
  kdbg("  [pipe!] %p: no room for another pending operation (%d in use) in pid %d\n",(void*)h,MAXPEND,cur->pid);
  return 0;
}
static int pipe_cancel(u64 h, u8 *ov){                                          /* CancelIo(Ex), and closing the handle */
  int n=0; for(int i=0;i<MAXPEND;i++){ struct pend *q=&PD[i]; if(q->used && q->h==h && (!ov || q->ov==ov)){ q->used=0; ov_finish(h,q->ov,STATUS_CANCELLED,0,1); n++; } }
  return n;
}

static void ref_add(struct pipe *p, int s, int pid){ p->refs[s]++; if(pid>=0 && pid<MAXPROC && p->own[s][pid]<255) p->own[s][pid]++; }
void pipe_addref_for(u64 h, int pid){ if(pipe_is_handle(h)) ref_add(pp(h),side(h),pid); }   /* a reference held by another process (inheritance, DuplicateHandle) */
void pipe_addref(u64 h){ pipe_addref_for(h,cur->pid); }
void pipe_close_for(u64 h, int pid){
  if(!pipe_is_handle(h)) return; struct pipe *p=pp(h); int s=side(h);
  if(pid>=0 && pid<MAXPROC && p->own[s][pid]) p->own[s][pid]--;
  if(p->refs[s]>0) p->refs[s]--;
  if(pipe_trace()) kdbg("  [pipe] %p (%s) side %d: closed by pid %d (refs now %d/%d)\n",(void*)h,p->named? p->name : "anon",s,pid,p->refs[0],p->refs[1]);
  if(p->refs[s]<=0){ pipe_cancel(h,0); extern void iocp_forget(u64); iocp_forget(h); }   /* that end is gone: its pending I/O is cancelled (the packets still go to its port), then the association */
  pipe_kick(p);                                                                 /* readers on the other side see the end */
  if(p->refs[0]<=0 && p->refs[1]<=0){ heap_free(p->buf[0]); heap_free(p->buf[1]); p->used=0; }
}
void pipe_close(u64 h){ pipe_close_for(h,cur->pid); }
static void pipe_release_all(int pid){                                          /* a process ended: its pipe ends close, so the other side sees a broken pipe */
  for(int i=0;i<MAXPIPE;i++) for(int s=0;s<2;s++) while(P[i].used && P[i].own[s][pid]) pipe_close_for(PIPE_BASE+(u64)i*2+s,pid);
}
int pipe_read_ov(u64 h, void *buf, u32 n, u32 *got, u8 *ov){
  struct pipe *p=pp(h); int s=side(h), rr=rx_ring(p,s); if(got) *got=0;
  if(rr<0){ win32_set_last_error(5); return 0; }                                  /* the write end cannot be read */
  if(ov){
    if(p->r[rr]!=p->w[rr]){ u32 k=ring_take(p,rr,buf,n); if(got) *got=k; ov_finish(h,ov,0,k,1); return 1; }
    if(p->refs[1-s]<=0 && (!p->named || p->connected || s==1)){ kdbg("  [pipe!] %p (%s) side %d: read, other end closed (refs %d/%d) in pid %d\n",(void*)h,p->named? p->name : "anon",s,p->refs[0],p->refs[1],cur->pid); win32_set_last_error(ERR_BROKEN_PIPE); return 0; }
    if(!pend_add(h,0,buf,n,ov)){ win32_set_last_error(8); return 0; }
    if(pipe_trace()) kdbg("  [pipe] %p: read %u bytes pending (pid %d)\n",(void*)h,n,cur->pid);
    win32_set_last_error(997); return 0;                                          /* ERROR_IO_PENDING */
  }
  while(p->r[rr]==p->w[rr]){
    if(p->refs[1-s]<=0){ win32_set_last_error(ERR_BROKEN_PIPE); return 0; }       /* every writer is gone: end of file */
    thread_note_wait("ReadFile(pipe)",h,(u64)__builtin_return_address(0)); idle_wait();
  }
  thread_end_wait();
  u32 k=ring_take(p,rr,buf,n); if(got) *got=k; return 1;
}
int pipe_read(u64 h, void *buf, u32 n, u32 *got){ return pipe_read_ov(h,buf,n,got,0); }
int pipe_write_ov(u64 h, const void *buf, u32 n, u32 *wrote, u8 *ov){
  struct pipe *p=pp(h); int s=side(h), tr=tx_ring(p,s); if(wrote) *wrote=0;
  if(tr<0){ win32_set_last_error(5); return 0; }
  if(p->refs[1-s]<=0 && (!p->named || p->connected || s==1)){ kdbg("  [pipe!] %p (%s) side %d: write %u bytes, other end closed (refs %d/%d) in pid %d\n",(void*)h,p->named? p->name : "anon",s,n,p->refs[0],p->refs[1],cur->pid); win32_set_last_error(ERR_NO_DATA); return 0; }   /* nobody left to read */
  if(!ring_grow(p,tr,n)){ kdbg("  [pipe!] %p: write %u bytes, out of memory in pid %d\n",(void*)h,n,cur->pid); win32_set_last_error(8); return 0; }
  const u8 *d=buf; for(u32 i=0;i<n;i++) p->buf[tr][(p->w[tr]+i)%p->cap[tr]]=d[i];
  p->w[tr]+=n; if(wrote) *wrote=n;
  if(pipe_trace()) kdbg("  [pipe] %p: write %u bytes%s (pid %d)\n",(void*)h,n,ov? " overlapped" : "",cur->pid);
  if(ov) ov_finish(h,ov,0,n,1);
  pipe_kick(p); return 1;
}
int pipe_write(u64 h, const void *buf, u32 n, u32 *wrote){ return pipe_write_ov(h,buf,n,wrote,0); }
static BOOL WINAPI i_CreatePipe(u64 *rd, u64 *wr, void *sa, DWORD size){
  (void)sa;(void)size; struct pipe *p=pipe_alloc(0); if(!p){ win32_set_last_error(8); return 0; }
  ref_add(p,0,cur->pid); ref_add(p,1,cur->pid); u64 base=PIPE_BASE+(u64)(p-P)*2; if(rd) *rd=base; if(wr) *wr=base+1; return 1;
}
static BOOL WINAPI i_PeekNamedPipe(u64 h, u8 *buf, DWORD n, DWORD *rd, DWORD *avail, DWORD *left){
  if(!pipe_is_handle(h)){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  struct pipe *p=pp(h); int rr=rx_ring(p,side(h)); if(rr<0){ win32_set_last_error(5); return 0; }
  u32 have=p->w[rr]-p->r[rr]; if(have==0 && p->refs[1-side(h)]<=0){ win32_set_last_error(ERR_BROKEN_PIPE); return 0; }
  u32 k=have<n? have : n; if(buf) for(u32 i=0;i<k;i++) buf[i]=p->buf[rr][(p->r[rr]+i)%p->cap[rr]];
  if(rd) *rd=buf? k : 0; if(avail) *avail=have; if(left) *left=have>k? have-k : 0; return 1;
}
/* named pipes: \\.\pipe\name */
static int pipe_name(const char *path, char *out, int max){
  static const char pre[]="\\\\.\\pipe\\"; int i=0; for(;pre[i];i++) if(path[i]!=pre[i] && !(path[i]=='/' && pre[i]=='\\')) return 0;
  int n=0; for(const char *q=path+i; *q && n<max-1; q++) out[n++]=(char)(*q>='A'&&*q<='Z'? *q+32 : *q); out[n]=0; return n>0;
}
int pipe_is_path(const char *name){ char t[PNAME]; return name && pipe_name(name,t,sizeof t); }
u64 pipe_open_client(const char *name){
  char t[PNAME]; if(!pipe_name(name,t,sizeof t)) return 0;
  for(int i=0;i<MAXPIPE;i++){ struct pipe *p=&P[i]; if(p->used && p->named && !p->connected && p->refs[0]>0 && !stricmp(p->name,t)){
    p->connected=1; p->refs[1]=0; memset(p->own[1],0,MAXPROC); ref_add(p,1,cur->pid); p->pid[1]=cur->pid; pipe_kick(p); if(pipe_trace()) kdbg("  [pipe] %s: client connected (pid %d)\n",t,cur->pid); return PIPE_BASE+(u64)i*2+1; } }
  kdbg("  [pipe!] %s: no server to connect to (pid %d)\n",t,cur->pid);
  return 0;
}
static u64 WINAPI i_CreateNamedPipeA(const char *name, DWORD om, DWORD pm, DWORD max, DWORD out, DWORD in, DWORD to, void *sa){
  (void)pm;(void)max;(void)out;(void)in;(void)to;(void)sa; char t[PNAME]; if(!name || !pipe_name(name,t,sizeof t)){ win32_set_last_error(ERR_INVALID_PARAMETER); return (u64)-1; }
  if(om&0x80000){ for(int i=0;i<MAXPIPE;i++) if(P[i].used && P[i].named && !stricmp(P[i].name,t)){ win32_set_last_error(5); return (u64)-1; } }   /* FILE_FLAG_FIRST_PIPE_INSTANCE */
  struct pipe *p=pipe_alloc(1); if(!p){ kdbg("  [pipe!] CreateNamedPipe(%s): all %d pipes in use (pid %d)\n",t,MAXPIPE,cur->pid); win32_set_last_error(8); return (u64)-1; }
  memcpy(p->name,t,strlen(t)+1); ref_add(p,0,cur->pid); p->pid[0]=cur->pid;
  if(pipe_trace()) kdbg("  [pipe] CreateNamedPipe(%s, mode %x) = %p (pid %d)\n",t,om,(void*)(PIPE_BASE+(u64)(p-P)*2),cur->pid);
  win32_set_last_error(0); return PIPE_BASE+(u64)(p-P)*2;
}
static u64 WINAPI i_CreateNamedPipeW(const u16 *name, DWORD om, DWORD pm, DWORD max, DWORD out, DWORD in, DWORD to, void *sa){ char t[200]; w2a(name,t,200); return i_CreateNamedPipeA(t,om,pm,max,out,in,to,sa); }
static BOOL WINAPI i_ConnectNamedPipe(u64 h, u8 *ov){
  if(!pipe_is_handle(h) || !pp(h)->named || side(h)!=0){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  struct pipe *p=pp(h); if(p->connected){ win32_set_last_error(ERR_PIPE_CONNECTED); return 0; }
  if(ov){ if(!pend_add(h,1,0,0,ov)){ win32_set_last_error(8); return 0; } win32_set_last_error(997); return 0; }    /* ERROR_IO_PENDING */
  thread_note_wait("ConnectNamedPipe",h,(u64)__builtin_return_address(0));
  while(!p->connected) idle_wait();
  thread_end_wait(); return 1;
}
static BOOL WINAPI i_DisconnectNamedPipe(u64 h){
  if(!pipe_is_handle(h) || !pp(h)->named) return 0; struct pipe *p=pp(h); p->connected=0; p->refs[1]=0; memset(p->own[1],0,MAXPROC); for(int i=0;i<2;i++) p->r[i]=p->w[i]=0; return 1;
}
/* GetOverlappedResult: the result of an asynchronous operation (any handle: files complete at once, pipes may be pending) */
static BOOL WINAPI i_GetOverlappedResultEx(u64 h, u8 *ov, DWORD *n, DWORD ms, BOOL al){
  (void)h;(void)al; if(!ov){ win32_set_last_error(ERR_INVALID_PARAMETER); return 0; }
  if(*(u32*)ov==STATUS_PENDING){
    if(!ms){ win32_set_last_error(996); return 0; }                               /* ERROR_IO_INCOMPLETE */
    u32 t0=ticks; thread_note_wait("GetOverlappedResult",h,(u64)__builtin_return_address(0));
    while(*(volatile u32*)ov==STATUS_PENDING){ if(ms!=0xFFFFFFFF && (u32)(ticks-t0)>=ms){ thread_end_wait(); win32_set_last_error(258); return 0; } idle_wait(); }
    thread_end_wait();
  }
  if(n) *n=*(u32*)(ov+8);
  u32 st=*(u32*)ov; if(!st) return 1;
  win32_set_last_error(st==STATUS_PIPE_BROKEN? ERR_BROKEN_PIPE : st==STATUS_CANCELLED? 995 : st==0xC0000011u? 38 : 31); return 0;
}
static BOOL WINAPI i_GetOverlappedResult(u64 h, u8 *ov, DWORD *n, BOOL wait){ return i_GetOverlappedResultEx(h,ov,n,wait? 0xFFFFFFFF : 0,0); }
static BOOL WINAPI i_WaitNamedPipeA(const char *name, DWORD ms){
  char t[PNAME]; if(!name || !pipe_name(name,t,sizeof t)) return 0; u32 end=ticks+(ms==0? 5000 : ms);
  for(;;){ for(int i=0;i<MAXPIPE;i++){ struct pipe *p=&P[i]; if(p->used && p->named && !p->connected && p->refs[0]>0 && !stricmp(p->name,t)) return 1; } if(ms!=0xFFFFFFFF && (i32)(ticks-end)>=0){ win32_set_last_error(121); return 0; } idle_wait(); }
}
static BOOL WINAPI i_WaitNamedPipeW(const u16 *name, DWORD ms){ char t[200]; w2a(name,t,200); return i_WaitNamedPipeA(t,ms); }
static BOOL WINAPI i_SetNamedPipeHandleState(u64 h, DWORD *mode, DWORD *maxc, DWORD *to){ (void)h;(void)mode;(void)maxc;(void)to; return 1; }
static BOOL WINAPI i_GetNamedPipeInfo(u64 h, DWORD *fl, DWORD *out, DWORD *in, DWORD *max){ if(!pipe_is_handle(h)){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; } if(fl) *fl=side(h)==0 && pp(h)->named? 1 : 0; if(out) *out=RING0; if(in) *in=RING0; if(max) *max=1; return 1; }   /* PIPE_SERVER_END */
static BOOL WINAPI i_GetNamedPipeClientProcessId(u64 h, DWORD *pid){ if(!pipe_is_handle(h)){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; } if(pid) *pid=pp(h)->pid[1]; return 1; }
static BOOL WINAPI i_GetNamedPipeServerProcessId(u64 h, DWORD *pid){ if(!pipe_is_handle(h)){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; } if(pid) *pid=pp(h)->pid[0]; return 1; }
static BOOL WINAPI i_GetNamedPipeHandleStateA(u64 h, DWORD *st, DWORD *cur_, DWORD *maxc, DWORD *to, char *user, DWORD n){ (void)h;(void)maxc;(void)to;(void)user;(void)n; if(st) *st=0; if(cur_) *cur_=1; return 1; }

/* ================= file mappings ================= */
#define MAXMAP 224                                        /* handles 0x7A00..0x7ADF (pipes start at 0x7B00) */
#define MAXVIEW 1024
#define MAPBASE 0x7A00
struct fmap { u8 used, write, borrowed; int refs, views;                /* borrowed: base is the file's own data (read-only mapping) */ char name[64]; u8 *base; u64 size; struct ramfile *f; };
struct view { u8 *p; u64 len; int map; };
static struct fmap M[MAXMAP]; static struct view V[MAXVIEW];
int map_is_handle(u64 h){ return h>=MAPBASE && h<MAPBASE+MAXMAP && M[h-MAPBASE].used; }
/* fs.c asks before freeing a deleted file's data: a read-only mapping may be showing that very memory */
int fmap_borrows(const void *data){ if(!data) return 0; for(int i=0;i<MAXMAP;i++) if(M[i].used && M[i].borrowed && M[i].base==data) return 1; return 0; }
void map_addref(u64 h){ if(map_is_handle(h)) M[h-MAPBASE].refs++; }                  /* DuplicateHandle */
/* NtQuerySection(SectionBasicInformation): {BaseAddress, AllocationAttributes, MaximumSize} */
static u32 WINAPI i_NtQuerySection(u64 h, u32 cls, u8 *out, u64 len, u64 *ret){
  if(!map_is_handle(h)) return 0xC0000008; if(cls!=0) return 0xC0000003; if(len<24) return 0xC0000004;    /* INVALID_HANDLE, INVALID_INFO_CLASS, INFO_LENGTH_MISMATCH */
  memset(out,0,24); *(u32*)(out+8)= M[h-MAPBASE].f? 0x1000000 : 0x8000000; *(u64*)(out+16)=M[h-MAPBASE].size;  /* SEC_IMAGE-less: SEC_FILE or SEC_COMMIT */
  if(ret) *ret=24; return 0;
}
static int map_trace(void);
static void map_release(struct fmap *m){ if(m->refs<=0 && m->views<=0){ if(map_trace()) kdbg("  [map] %p freed (pid %d)\n",(void*)(MAPBASE+(u64)(m-M)),cur->pid); if(!m->borrowed) heap_free(m->base); m->used=0; } }
void map_close(u64 h){ if(!map_is_handle(h)) return; struct fmap *m=&M[h-MAPBASE]; m->refs--; if(map_trace()) kdbg("  [map] close %p -> refs %d views %d\n",(void*)h,m->refs,m->views); map_release(m); }
static u64 map_create(u64 hfile, DWORD prot, DWORD hi, DWORD lo, const char *name){
  u64 size=((u64)hi<<32)|lo; struct ramfile *f=0;
  if(hfile!=(u64)-1){ f=fs_handle_file(hfile); if(!f){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; } if(!size) size=f->size; }
  if(!size){ win32_set_last_error(ERR_INVALID_PARAMETER); return 0; }
  if(name && *name) for(int i=0;i<MAXMAP;i++) if(M[i].used && !stricmp(M[i].name,name)){ M[i].refs++; win32_set_last_error(ERR_ALREADY_EXISTS); return MAPBASE+(u64)i; }
  int idx=-1; for(int i=0;i<MAXMAP;i++) if(!M[i].used){ idx=i; break; } if(idx<0){ win32_set_last_error(8); return 0; }
  struct fmap *m=&M[idx]; memset(m,0,sizeof *m);
  int ro=(prot&0xFF)==0x02 || (prot&0xFF)==0x20;                  /* PAGE_READONLY, PAGE_EXECUTE_READ */
  if(f && f->data && ro && size<=f->size){ m->base=f->data; m->borrowed=1; }   /* read-only view of a file: its data is in memory already, every process shares it */
  else { u64 cap=(size+0xFFF)&~0xFFFull; m->base=heap_alloc(cap); if(!m->base){ win32_set_last_error(8); return 0; } }
  m->used=1; m->refs=1; m->size=size; m->f=f; m->write=(prot&(0x04|0x08|0x40|0x80))!=0;
  if(name) for(unsigned k=0;name[k]&&k<63;k++) m->name[k]=name[k];
  if(f && f->data && !m->borrowed){ u64 n=f->size<size? f->size : size; memcpy(m->base,f->data,n); }
  win32_set_last_error(0); return MAPBASE+(u64)idx;
}
static int map_trace(void){ struct ramfile *f=fs_find("/System/apitrace.cfg"); return f && !f->isdir; }    /* log mappings while tracing (debugging) */
static u64 WINAPI i_CreateFileMappingA(u64 hf, void *sa, DWORD prot, DWORD hi, DWORD lo, const char *name){ (void)sa; u64 r=map_create(hf,prot,hi,lo,name);
  if(map_trace()) kdbg("  [map] CreateFileMapping(file %p, prot %x, size %x:%x, %s) = %p\n",(void*)hf,prot,hi,lo,name? name : "-",(void*)r); return r; }
static u64 WINAPI i_CreateFileMappingW(u64 hf, void *sa, DWORD prot, DWORD hi, DWORD lo, const u16 *name){ char t[200]; if(name) w2a(name,t,200); return i_CreateFileMappingA(hf,sa,prot,hi,lo,name? t : 0); }
static u64 WINAPI i_OpenFileMappingA(DWORD acc, BOOL inh, const char *name){ (void)acc;(void)inh; if(name) for(int i=0;i<MAXMAP;i++) if(M[i].used && !stricmp(M[i].name,name)){ M[i].refs++; return MAPBASE+(u64)i; } win32_set_last_error(ERR_FILE_NOT_FOUND); return 0; }
static u64 WINAPI i_OpenFileMappingW(DWORD acc, BOOL inh, const u16 *name){ char t[200]; w2a(name,t,200); return i_OpenFileMappingA(acc,inh,t); }
static void *view_map(u64 h, DWORD hi, DWORD lo, u64 bytes){
  if(!map_is_handle(h)){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  struct fmap *m=&M[h-MAPBASE]; u64 off=((u64)hi<<32)|lo; if(off>=m->size){ win32_set_last_error(ERR_INVALID_PARAMETER); return 0; }
  int v=-1; for(int i=0;i<MAXVIEW;i++) if(!V[i].p){ v=i; break; } if(v<0){ win32_set_last_error(8); return 0; }
  V[v].p=m->base+off; V[v].len=bytes? bytes : m->size-off; V[v].map=(int)(m-M); m->views++; return V[v].p;
}
static void *WINAPI i_MapViewOfFile(u64 h, DWORD acc, DWORD hi, DWORD lo, u64 bytes){ void *r=view_map(h,hi,lo,bytes);
  if(map_trace()){ kdbg("  [map] MapViewOfFile(%p, access %x, off %x:%x, %p bytes) = %p pid %d:",(void*)h,acc,hi,lo,(void*)bytes,r,cur->pid);
    if(r) for(int i=0;i<40;i++){ u8 c=((u8*)r)[i]; kdbg("%c", c>=32&&c<127? c : '.'); } kdbg("\n"); }
  return r; }
static void *WINAPI i_MapViewOfFileEx(u64 h, DWORD acc, DWORD hi, DWORD lo, u64 bytes, void *base){ (void)base; return i_MapViewOfFile(h,acc,hi,lo,bytes); }
static void flush_map(struct fmap *m){ if(m->f && m->write) fs_file_write_at(m->f,0,m->base,m->size<m->f->size? m->f->size : m->size); }   /* file-backed: write the changes back */
static BOOL WINAPI i_FlushViewOfFile(void *p, u64 n){ (void)n; for(int i=0;i<MAXVIEW;i++) if(V[i].p==(u8*)p){ flush_map(&M[V[i].map]); return 1; } return 0; }
static BOOL WINAPI i_UnmapViewOfFile(void *p){
  if(map_trace()) kdbg("  [map] UnmapViewOfFile(%p)\n",p);
  for(int i=0;i<MAXVIEW;i++) if(V[i].p==(u8*)p){ struct fmap *m=&M[V[i].map]; V[i].p=0; flush_map(m); m->views--; map_release(m); return 1; }
  win32_set_last_error(ERR_INVALID_PARAMETER); return 0;
}

/* ================= other file APIs ================= */
static BOOL WINAPI i_LockFile(u64 h, DWORD a, DWORD b, DWORD c, DWORD d){ (void)h;(void)a;(void)b;(void)c;(void)d; return 1; }
static BOOL WINAPI i_LockFileEx(u64 h, DWORD fl, DWORD r, DWORD lo, DWORD hi, void *ov){ (void)h;(void)fl;(void)r;(void)lo;(void)hi;(void)ov; return 1; }
static BOOL WINAPI i_UnlockFileEx(u64 h, DWORD r, DWORD lo, DWORD hi, void *ov){ (void)h;(void)r;(void)lo;(void)hi;(void)ov; return 1; }
static BOOL WINAPI i_DeviceIoControl(u64 h, DWORD code, void *in, DWORD inl, void *out, DWORD outl, DWORD *ret, void *ov){ (void)h;(void)code;(void)in;(void)inl;(void)out;(void)outl;(void)ov; if(ret) *ret=0; win32_set_last_error(1); return 0; }
/* SetFileInformationByHandle / GetFileInformationByHandleEx on files. Firefox's downloads move the finished ".part" file into place by
   renaming through the handle, and Rust's std reads sizes and attributes this way; both used to be no-ops / failures. */
int fs_handle_rename(u64 h, const char *dst, int replace); int fs_handle_setsize(u64 h, u64 size); int fs_handle_delete(u64 h);
static int file_info_trace(void){ struct ramfile *f=fs_find("/System/apitrace.cfg"); return f && !f->isdir; }
static BOOL WINAPI i_SetFileInformationByHandle(u64 h, int cls, void *info, DWORD n){
  if(!fs_handle_file(h) || !info){ if(pipe_is_handle(h)) return 1; win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  int e=0; u8 *in=info;
  switch(cls){
    case 3: case 22: {                                                                   /* FileRenameInfo(Ex): flags/replace at 0, name length at 16, name at 20 */
      if(n<20){ e=87; break; }
      u32 len=*(u32 *)(in+16)/2; char t[260]; u32 i=0; const u16 *w=(const u16 *)(in+20);
      for(;i<len && i<259;i++) t[i]= w[i]<0x80? (char)w[i] : '_'; t[i]=0;
      const char *p=t; if(p[0]=='\\' && p[1]=='?' && p[2]=='?' && p[3]=='\\') p+=4; if(p[0]=='\\' && p[1]=='\\' && p[2]=='?' && p[3]=='\\') p+=4;   /* \??\C:\... */
      int replace= cls==3? in[0]!=0 : (*(u32 *)in&1)!=0;
      e=fs_handle_rename(h,p,replace);
      if(file_info_trace()) kdbg("  [file] rename by handle %p -> %s (replace %d) = error %d\n",(void*)h,p,replace,e);
      break; }
    case 4: if(n>=1 && in[0]) e=fs_handle_delete(h); break;                               /* FileDispositionInfo */
    case 21: if(n>=4 && (*(u32 *)in&1)) e=fs_handle_delete(h); break;                     /* FileDispositionInfoEx: FILE_DISPOSITION_FLAG_DELETE */
    case 6: if(n<8){ e=87; break; } e=fs_handle_setsize(h,*(u64 *)in); break;            /* FileEndOfFileInfo */
    default: break;                                                                      /* basic info (times), allocation size, priority hints: accepted */
  }
  if(e){ win32_set_last_error((u32)e); return 0; } return 1;
}
static BOOL WINAPI i_GetFileInformationByHandleEx(u64 h, int cls, void *info, DWORD n){
  struct ramfile *f=fs_handle_file(h); if(!f || !info){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  u8 *o=info; u64 t=fs_mtime(f); u32 attr= f->isdir? 0x10 : 0x80;
  switch(cls){
    case 0: if(n<40) break; memset(o,0,40); for(int i=0;i<4;i++) *(u64 *)(o+i*8)=t; *(u32 *)(o+32)=attr; return 1;            /* FileBasicInfo */
    case 1: if(n<24) break; memset(o,0,24); *(u64 *)o=(f->size+4095)&~4095ull; *(u64 *)(o+8)=f->size; *(u32 *)(o+16)=1; o[21]=(u8)f->isdir; return 1;   /* FileStandardInfo */
    case 2: { if(n<6) break; u32 k=0, max=(n-4)/2; u16 *w=(u16 *)(o+4);                 /* FileNameInfo: the path without the drive */
      if(k<max) w[k++]='\\'; for(const char *q=f->name; *q && k<max; q++) w[k++]= *q=='/'? '\\' : (u8)*q;
      *(u32 *)o=k*2; return 1; }
    case 9: if(n<8) break; *(u32 *)o=attr; *(u32 *)(o+4)=0; return 1;                    /* FileAttributeTagInfo */
    default: win32_set_last_error(87); return 0;
  }
  win32_set_last_error(24); return 0;                                                    /* ERROR_BAD_LENGTH */
}
static BOOL WINAPI i_CancelIo(u64 h){ pipe_cancel(h,0); return 1; }
static BOOL WINAPI i_CancelIoEx(u64 h, u8 *ov){ if(!pipe_cancel(h,ov)){ win32_set_last_error(1168); return 0; } return 1; }   /* ERROR_NOT_FOUND: nothing was pending */
static BOOL WINAPI i_CancelSynchronousIo(u64 h){ (void)h; win32_set_last_error(1168); return 0; }
static BOOL WINAPI i_CreateSymbolicLinkW(const u16 *l, const u16 *t, DWORD f){ (void)l;(void)t;(void)f; win32_set_last_error(1314); return 0; }
static BOOL WINAPI i_GetDiskFreeSpaceExW(const u16 *p, u64 *avail, u64 *total, u64 *free_){ char t[260]; if(p) w2a(p,t,260); BOOL (WINAPI *f)(const char*,u64*,u64*,u64*)=K32("GetDiskFreeSpaceExA"); return f? f(p? t : 0,avail,total,free_) : 0; }
static DWORD final_path(u64 h, char *out, DWORD n, int wide){
  struct ramfile *f=fs_handle_file(h); if(!f){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  char t[200]; const char *pre="\\\\?\\C:\\"; int k=0; for(const char *q=pre;*q;) t[k++]=*q++; for(const char *q=f->name;*q&&k<198;q++) t[k++]= *q=='/'? '\\' : *q; t[k]=0;
  if(wide){ if(n<=(DWORD)k) return (DWORD)k+1; a2w(t,(u16*)out,(int)n); } else { if(n<=(DWORD)k) return (DWORD)k+1; memcpy(out,t,k+1); } return (DWORD)k;
}
static DWORD WINAPI i_GetFinalPathNameByHandleA(u64 h, char *out, DWORD n, DWORD fl){ (void)fl; return final_path(h,out,n,0); }
static DWORD WINAPI i_GetFinalPathNameByHandleW(u64 h, u16 *out, DWORD n, DWORD fl){ (void)fl; return final_path(h,(char*)out,n,1); }
static BOOL WINAPI i_ReadFileEx(u64 h, void *b, DWORD n, void *ov, void *cb){ (void)h;(void)b;(void)n;(void)ov;(void)cb; win32_set_last_error(ERR_INVALID_PARAMETER); return 0; }

#define I(n) {"kernel32", #n, i_##n}
const struct export ipc_exports[] = {
  I(CreatePipe), I(PeekNamedPipe), I(CreateNamedPipeA), I(CreateNamedPipeW), I(ConnectNamedPipe), I(DisconnectNamedPipe), I(WaitNamedPipeA), I(WaitNamedPipeW), I(SetNamedPipeHandleState), I(GetNamedPipeInfo), I(GetNamedPipeClientProcessId), I(GetNamedPipeServerProcessId), I(GetNamedPipeHandleStateA),
  I(CreateFileMappingA), I(CreateFileMappingW), I(OpenFileMappingA), I(OpenFileMappingW), I(MapViewOfFile), I(MapViewOfFileEx), I(FlushViewOfFile), I(UnmapViewOfFile),
  I(LockFile), {"kernel32","UnlockFile",i_LockFile}, I(LockFileEx), I(UnlockFileEx), I(DeviceIoControl), I(SetFileInformationByHandle), I(GetFileInformationByHandleEx), I(CancelIo), I(CancelIoEx), I(CancelSynchronousIo), I(GetOverlappedResult), I(GetOverlappedResultEx), I(CreateSymbolicLinkW),
  I(GetDiskFreeSpaceExW), I(GetFinalPathNameByHandleA), I(GetFinalPathNameByHandleW), I(ReadFileEx),
  {"ntdll","NtQuerySection",i_NtQuerySection}, {"ntdll","ZwQuerySection",i_NtQuerySection},
};
const unsigned ipc_nexports = sizeof ipc_exports / sizeof *ipc_exports;
