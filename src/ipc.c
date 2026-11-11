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

/* ================= pipes ================= */
#define MAXPIPE 48
#define PIPE_BASE 0x7B00
#define RING 65536u
struct pipe { u8 used, named, connected; int refs[2]; char name[64]; u8 *buf[2]; u32 r[2], w[2]; };
static struct pipe P[MAXPIPE];
/* handle = PIPE_BASE + index*2 + side. Anonymous: side 0 reads ring 0, side 1 writes ring 0.
   Named (duplex): the server (side 0) reads ring 0 and writes ring 1, the client (side 1) the opposite. */
int pipe_is_handle(u64 h){ if(h<PIPE_BASE || h>=PIPE_BASE+MAXPIPE*2) return 0; return P[(h-PIPE_BASE)>>1].used; }
static struct pipe *pp(u64 h){ return &P[(h-PIPE_BASE)>>1]; }
static int side(u64 h){ return (int)((h-PIPE_BASE)&1); }
static int rx_ring(struct pipe *p, int s){ return p->named? (s==0? 0 : 1) : (s==0? 0 : -1); }
static int tx_ring(struct pipe *p, int s){ return p->named? (s==0? 1 : 0) : (s==1? 0 : -1); }
static struct pipe *pipe_alloc(int named){
  for(int i=0;i<MAXPIPE;i++) if(!P[i].used){ struct pipe *p=&P[i]; memset(p,0,sizeof *p); p->buf[0]=heap_alloc(RING); p->buf[1]=named? heap_alloc(RING) : 0; if(!p->buf[0] || (named && !p->buf[1])){ heap_free(p->buf[0]); heap_free(p->buf[1]); return 0; } p->used=1; p->named=(u8)named; return p; }
  return 0;
}
void pipe_addref(u64 h){ if(pipe_is_handle(h)) pp(h)->refs[side(h)]++; }
void pipe_close(u64 h){
  if(!pipe_is_handle(h)) return; struct pipe *p=pp(h); int s=side(h); if(p->refs[s]>0) p->refs[s]--;
  if(p->refs[0]<=0 && p->refs[1]<=0){ heap_free(p->buf[0]); heap_free(p->buf[1]); p->used=0; }
}
int pipe_read(u64 h, void *buf, u32 n, u32 *got){
  struct pipe *p=pp(h); int s=side(h), rr=rx_ring(p,s); if(got) *got=0;
  if(rr<0){ win32_set_last_error(5); return 0; }                                  /* the write end cannot be read */
  while(p->r[rr]==p->w[rr]){
    if(p->refs[1-s]<=0){ win32_set_last_error(ERR_BROKEN_PIPE); return 0; }       /* every writer is gone: end of file */
    idle_wait();
  }
  u32 avail=p->w[rr]-p->r[rr]; if(n>avail) n=avail; u8 *d=buf;
  for(u32 i=0;i<n;i++) d[i]=p->buf[rr][(p->r[rr]+i)%RING];
  p->r[rr]+=n; if(got) *got=n; return 1;
}
int pipe_write(u64 h, const void *buf, u32 n, u32 *wrote){
  struct pipe *p=pp(h); int s=side(h), tr=tx_ring(p,s); if(wrote) *wrote=0;
  if(tr<0){ win32_set_last_error(5); return 0; }
  const u8 *d=buf; u32 done=0;
  while(done<n){
    if(p->refs[1-s]<=0){ win32_set_last_error(ERR_NO_DATA); return 0; }          /* nobody left to read */
    u32 space=RING-(p->w[tr]-p->r[tr]); if(!space){ idle_wait(); continue; }
    u32 k=n-done; if(k>space) k=space;
    for(u32 i=0;i<k;i++) p->buf[tr][(p->w[tr]+i)%RING]=d[done+i];
    p->w[tr]+=k; done+=k;
  }
  if(wrote) *wrote=done; return 1;
}
static BOOL WINAPI i_CreatePipe(u64 *rd, u64 *wr, void *sa, DWORD size){
  (void)sa;(void)size; struct pipe *p=pipe_alloc(0); if(!p){ win32_set_last_error(8); return 0; }
  p->refs[0]=p->refs[1]=1; u64 base=PIPE_BASE+(u64)(p-P)*2; if(rd) *rd=base; if(wr) *wr=base+1; return 1;
}
static BOOL WINAPI i_PeekNamedPipe(u64 h, u8 *buf, DWORD n, DWORD *rd, DWORD *avail, DWORD *left){
  if(!pipe_is_handle(h)){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  struct pipe *p=pp(h); int rr=rx_ring(p,side(h)); if(rr<0){ win32_set_last_error(5); return 0; }
  u32 have=p->w[rr]-p->r[rr]; if(have==0 && p->refs[1-side(h)]<=0){ win32_set_last_error(ERR_BROKEN_PIPE); return 0; }
  u32 k=have<n? have : n; if(buf) for(u32 i=0;i<k;i++) buf[i]=p->buf[rr][(p->r[rr]+i)%RING];
  if(rd) *rd=buf? k : 0; if(avail) *avail=have; if(left) *left=have>k? have-k : 0; return 1;
}
/* named pipes: \\.\pipe\name */
static int pipe_name(const char *path, char *out, int max){
  static const char pre[]="\\\\.\\pipe\\"; int i=0; for(;pre[i];i++) if(path[i]!=pre[i] && !(path[i]=='/' && pre[i]=='\\')) return 0;
  int n=0; for(const char *q=path+i; *q && n<max-1; q++) out[n++]=(char)(*q>='A'&&*q<='Z'? *q+32 : *q); out[n]=0; return n>0;
}
int pipe_is_path(const char *name){ char t[64]; return name && pipe_name(name,t,sizeof t); }
u64 pipe_open_client(const char *name){
  char t[64]; if(!pipe_name(name,t,sizeof t)) return 0;
  for(int i=0;i<MAXPIPE;i++){ struct pipe *p=&P[i]; if(p->used && p->named && !p->connected && p->refs[0]>0 && !stricmp(p->name,t)){ p->connected=1; p->refs[1]=1; return PIPE_BASE+(u64)i*2+1; } }
  return 0;
}
static u64 WINAPI i_CreateNamedPipeA(const char *name, DWORD om, DWORD pm, DWORD max, DWORD out, DWORD in, DWORD to, void *sa){
  (void)om;(void)pm;(void)max;(void)out;(void)in;(void)to;(void)sa; char t[64]; if(!name || !pipe_name(name,t,sizeof t)){ win32_set_last_error(ERR_INVALID_PARAMETER); return (u64)-1; }
  struct pipe *p=pipe_alloc(1); if(!p){ win32_set_last_error(8); return (u64)-1; }
  memcpy(p->name,t,strlen(t)+1); p->refs[0]=1; return PIPE_BASE+(u64)(p-P)*2;
}
static u64 WINAPI i_CreateNamedPipeW(const u16 *name, DWORD om, DWORD pm, DWORD max, DWORD out, DWORD in, DWORD to, void *sa){ char t[200]; w2a(name,t,200); return i_CreateNamedPipeA(t,om,pm,max,out,in,to,sa); }
static BOOL WINAPI i_ConnectNamedPipe(u64 h, void *ov){
  (void)ov; if(!pipe_is_handle(h) || !pp(h)->named || side(h)!=0){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  struct pipe *p=pp(h); if(p->connected){ win32_set_last_error(ERR_PIPE_CONNECTED); return 0; }
  while(!p->connected) idle_wait(); return 1;
}
static BOOL WINAPI i_DisconnectNamedPipe(u64 h){
  if(!pipe_is_handle(h) || !pp(h)->named) return 0; struct pipe *p=pp(h); p->connected=0; p->refs[1]=0; for(int i=0;i<2;i++) p->r[i]=p->w[i]=0; return 1;
}
static BOOL WINAPI i_WaitNamedPipeA(const char *name, DWORD ms){
  char t[64]; if(!name || !pipe_name(name,t,sizeof t)) return 0; u32 end=ticks+(ms==0? 5000 : ms);
  for(;;){ for(int i=0;i<MAXPIPE;i++){ struct pipe *p=&P[i]; if(p->used && p->named && !p->connected && p->refs[0]>0 && !stricmp(p->name,t)) return 1; } if(ms!=0xFFFFFFFF && (i32)(ticks-end)>=0){ win32_set_last_error(121); return 0; } idle_wait(); }
}
static BOOL WINAPI i_WaitNamedPipeW(const u16 *name, DWORD ms){ char t[200]; w2a(name,t,200); return i_WaitNamedPipeA(t,ms); }
static BOOL WINAPI i_SetNamedPipeHandleState(u64 h, DWORD *mode, DWORD *maxc, DWORD *to){ (void)h;(void)mode;(void)maxc;(void)to; return 1; }
static BOOL WINAPI i_GetNamedPipeInfo(u64 h, DWORD *fl, DWORD *out, DWORD *in, DWORD *max){ if(!pipe_is_handle(h)) return 0; if(fl) *fl=pp(h)->named? 0 : 0; if(out) *out=RING; if(in) *in=RING; if(max) *max=255; return 1; }
static BOOL WINAPI i_GetNamedPipeHandleStateA(u64 h, DWORD *st, DWORD *cur_, DWORD *maxc, DWORD *to, char *user, DWORD n){ (void)h;(void)maxc;(void)to;(void)user;(void)n; if(st) *st=0; if(cur_) *cur_=1; return 1; }

/* ================= file mappings ================= */
#define MAXMAP 48
#define MAPBASE 0x7A00
struct fmap { u8 used, write; int refs, views; char name[64]; u8 *base; u64 size; struct ramfile *f; };
struct view { u8 *p; u64 len; int map; };
static struct fmap M[MAXMAP]; static struct view V[128];
int map_is_handle(u64 h){ return h>=MAPBASE && h<MAPBASE+MAXMAP && M[h-MAPBASE].used; }
static void map_release(struct fmap *m){ if(m->refs<=0 && m->views<=0){ heap_free(m->base); m->used=0; } }
void map_close(u64 h){ if(!map_is_handle(h)) return; struct fmap *m=&M[h-MAPBASE]; m->refs--; map_release(m); }
static u64 map_create(u64 hfile, DWORD prot, DWORD hi, DWORD lo, const char *name){
  u64 size=((u64)hi<<32)|lo; struct ramfile *f=0;
  if(hfile!=(u64)-1){ f=fs_handle_file(hfile); if(!f){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; } if(!size) size=f->size; }
  if(!size){ win32_set_last_error(ERR_INVALID_PARAMETER); return 0; }
  if(name && *name) for(int i=0;i<MAXMAP;i++) if(M[i].used && !stricmp(M[i].name,name)){ M[i].refs++; win32_set_last_error(ERR_ALREADY_EXISTS); return MAPBASE+(u64)i; }
  int idx=-1; for(int i=0;i<MAXMAP;i++) if(!M[i].used){ idx=i; break; } if(idx<0){ win32_set_last_error(8); return 0; }
  struct fmap *m=&M[idx]; memset(m,0,sizeof *m); u64 cap=(size+0xFFF)&~0xFFFull; m->base=heap_alloc(cap); if(!m->base){ win32_set_last_error(8); return 0; }
  m->used=1; m->refs=1; m->size=size; m->f=f; m->write=(prot&(0x04|0x08|0x40|0x80))!=0;
  if(name) for(unsigned k=0;name[k]&&k<63;k++) m->name[k]=name[k];
  if(f && f->data){ u64 n=f->size<size? f->size : size; memcpy(m->base,f->data,n); }
  win32_set_last_error(0); return MAPBASE+(u64)idx;
}
static u64 WINAPI i_CreateFileMappingA(u64 hf, void *sa, DWORD prot, DWORD hi, DWORD lo, const char *name){ (void)sa; return map_create(hf,prot,hi,lo,name); }
static u64 WINAPI i_CreateFileMappingW(u64 hf, void *sa, DWORD prot, DWORD hi, DWORD lo, const u16 *name){ (void)sa; char t[200]; if(name) w2a(name,t,200); return map_create(hf,prot,hi,lo,name? t : 0); }
static u64 WINAPI i_OpenFileMappingA(DWORD acc, BOOL inh, const char *name){ (void)acc;(void)inh; if(name) for(int i=0;i<MAXMAP;i++) if(M[i].used && !stricmp(M[i].name,name)){ M[i].refs++; return MAPBASE+(u64)i; } win32_set_last_error(ERR_FILE_NOT_FOUND); return 0; }
static u64 WINAPI i_OpenFileMappingW(DWORD acc, BOOL inh, const u16 *name){ char t[200]; w2a(name,t,200); return i_OpenFileMappingA(acc,inh,t); }
static void *view_map(u64 h, DWORD hi, DWORD lo, u64 bytes){
  if(!map_is_handle(h)){ win32_set_last_error(ERR_INVALID_HANDLE); return 0; }
  struct fmap *m=&M[h-MAPBASE]; u64 off=((u64)hi<<32)|lo; if(off>=m->size){ win32_set_last_error(ERR_INVALID_PARAMETER); return 0; }
  int v=-1; for(int i=0;i<128;i++) if(!V[i].p){ v=i; break; } if(v<0){ win32_set_last_error(8); return 0; }
  V[v].p=m->base+off; V[v].len=bytes? bytes : m->size-off; V[v].map=(int)(m-M); m->views++; return V[v].p;
}
static void *WINAPI i_MapViewOfFile(u64 h, DWORD acc, DWORD hi, DWORD lo, u64 bytes){ (void)acc; return view_map(h,hi,lo,bytes); }
static void *WINAPI i_MapViewOfFileEx(u64 h, DWORD acc, DWORD hi, DWORD lo, u64 bytes, void *base){ (void)acc;(void)base; return view_map(h,hi,lo,bytes); }
static void flush_map(struct fmap *m){ if(m->f && m->write) fs_file_write_at(m->f,0,m->base,m->size<m->f->size? m->f->size : m->size); }   /* file-backed: write the changes back */
static BOOL WINAPI i_FlushViewOfFile(void *p, u64 n){ (void)n; for(int i=0;i<128;i++) if(V[i].p==(u8*)p){ flush_map(&M[V[i].map]); return 1; } return 0; }
static BOOL WINAPI i_UnmapViewOfFile(void *p){
  for(int i=0;i<128;i++) if(V[i].p==(u8*)p){ struct fmap *m=&M[V[i].map]; V[i].p=0; flush_map(m); m->views--; map_release(m); return 1; }
  win32_set_last_error(ERR_INVALID_PARAMETER); return 0;
}

/* ================= other file APIs ================= */
static BOOL WINAPI i_LockFile(u64 h, DWORD a, DWORD b, DWORD c, DWORD d){ (void)h;(void)a;(void)b;(void)c;(void)d; return 1; }
static BOOL WINAPI i_LockFileEx(u64 h, DWORD fl, DWORD r, DWORD lo, DWORD hi, void *ov){ (void)h;(void)fl;(void)r;(void)lo;(void)hi;(void)ov; return 1; }
static BOOL WINAPI i_UnlockFileEx(u64 h, DWORD r, DWORD lo, DWORD hi, void *ov){ (void)h;(void)r;(void)lo;(void)hi;(void)ov; return 1; }
static BOOL WINAPI i_DeviceIoControl(u64 h, DWORD code, void *in, DWORD inl, void *out, DWORD outl, DWORD *ret, void *ov){ (void)h;(void)code;(void)in;(void)inl;(void)out;(void)outl;(void)ov; if(ret) *ret=0; win32_set_last_error(1); return 0; }
static BOOL WINAPI i_SetFileInformationByHandle(u64 h, int cls, void *info, DWORD n){ (void)h;(void)cls;(void)info;(void)n; return 1; }
static BOOL WINAPI i_GetFileInformationByHandleEx(u64 h, int cls, void *info, DWORD n){ (void)h;(void)cls;(void)info;(void)n; win32_set_last_error(87); return 0; }
static BOOL WINAPI i_CancelIo(u64 h){ (void)h; return 1; }
static BOOL WINAPI i_CancelIoEx(u64 h, void *ov){ (void)h;(void)ov; return 1; }
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
  I(CreatePipe), I(PeekNamedPipe), I(CreateNamedPipeA), I(CreateNamedPipeW), I(ConnectNamedPipe), I(DisconnectNamedPipe), I(WaitNamedPipeA), I(WaitNamedPipeW), I(SetNamedPipeHandleState), I(GetNamedPipeInfo), I(GetNamedPipeHandleStateA),
  I(CreateFileMappingA), I(CreateFileMappingW), I(OpenFileMappingA), I(OpenFileMappingW), I(MapViewOfFile), I(MapViewOfFileEx), I(FlushViewOfFile), I(UnmapViewOfFile),
  I(LockFile), {"kernel32","UnlockFile",i_LockFile}, I(LockFileEx), I(UnlockFileEx), I(DeviceIoControl), I(SetFileInformationByHandle), I(GetFileInformationByHandleEx), I(CancelIo), I(CancelIoEx), I(CreateSymbolicLinkW),
  I(GetDiskFreeSpaceExW), I(GetFinalPathNameByHandleA), I(GetFinalPathNameByHandleW), I(ReadFileEx),
};
const unsigned ipc_nexports = sizeof ipc_exports / sizeof *ipc_exports;
