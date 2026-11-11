#include "kernel.h"
/* Win32 batch 1: modern locks (SRW, InitOnce, WaitOnAddress, SLists), COM/OLE basics (CoTaskMem, GUID strings, BSTR),
   shell folders and launching (SHGetKnownFolderPath, ShellExecuteW, CommandLineToArgvW, shlwapi paths),
   process helpers (CreateProcessW, psapi, job objects). Willows schedules cooperatively, so locks only need to be correct, not fast. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE;
#define Y(n) {"kernel32", #n, y_##n}
#define YD(d,n) {d, #n, y_##n}
static u64 dummy_teb_y[0x2000/8];
#define last_error (*(u32*)((u8*)(cur->teb? cur->teb : dummy_teb_y)+0x68))

static int wlen(const u16 *s){ int n=0; while(s&&s[n]) n++; return n; }
static void w2a(const u16 *w, char *a, int n){ int i=0; for(;w&&w[i]&&i<n-1;i++) a[i]= w[i]<0x80? (char)w[i] : '?'; a[i]=0; }
static int a2w(const char *a, u16 *w, int n){ int i=0; for(;a[i]&&i<n-1;i++) w[i]=(u8)a[i]; w[i]=0; return i; }
static void *K32(const char *n){ return win32_resolve("kernel32",n); }
static void cat_(char *d, const char *s){ while(*d) d++; while((*d++=*s++)); }

/* ================= locks and one-time initialisation ================= */
/* SRWLOCK is one pointer-sized word: 1 = held exclusively, 2*n = n shared holders. If a lock is still busy after a number of yields
   (nothing else could release it in a cooperative system) it is taken anyway rather than hanging the program. */
static void spin_wait(volatile u64 *l, int exclusive){
  for(int i=0;i<200;i++){ if(exclusive? *l==0 : !(*l&1)) return; idle_wait(); }
}
static void WINAPI y_InitializeSRWLock(volatile u64 *l){ *l=0; }
static void WINAPI y_AcquireSRWLockExclusive(volatile u64 *l){ spin_wait(l,1); *l=1; }
static void WINAPI y_ReleaseSRWLockExclusive(volatile u64 *l){ *l=0; }
static void WINAPI y_AcquireSRWLockShared(volatile u64 *l){ spin_wait(l,0); *l = (*l&1)? 2 : *l+2; }
static void WINAPI y_ReleaseSRWLockShared(volatile u64 *l){ if(*l>=2) *l-=2; }
static u8 WINAPI y_TryAcquireSRWLockExclusive(volatile u64 *l){ if(*l) return 0; *l=1; return 1; }
static u8 WINAPI y_TryAcquireSRWLockShared(volatile u64 *l){ if(*l&1) return 0; *l+=2; return 1; }
static BOOL WINAPI y_TryEnterCriticalSection(void *cs){ (void)cs; return 1; }

/* INIT_ONCE: 0 = not started, 1 = running, otherwise (context | 2) when finished */
static void WINAPI y_InitOnceInitialize(u64 *o){ *o=0; }
static BOOL WINAPI y_InitOnceExecuteOnce(u64 *o, BOOL (WINAPI *fn)(void *, void *, void **), void *param, void **ctx){
  if(*o&2){ if(ctx) *ctx=(void*)(*o&~3ull); return 1; }
  void *c=0; if(!fn || !fn(o,param,&c)){ *o=0; return 0; }
  *o=((u64)c&~3ull)|2; if(ctx) *ctx=c; return 1;
}
static BOOL WINAPI y_InitOnceBeginInitialize(u64 *o, DWORD fl, BOOL *pending, void **ctx){
  (void)fl; if(*o&2){ *pending=0; if(ctx) *ctx=(void*)(*o&~3ull); return 1; } *pending=1; *o=1; return 1;
}
static BOOL WINAPI y_InitOnceComplete(u64 *o, DWORD fl, void *ctx){ if(fl&2){ *o=0; return 1; } *o=((u64)ctx&~3ull)|2; return 1; }

static BOOL WINAPI y_WaitOnAddress(volatile void *addr, void *cmp, u64 size, DWORD ms){
  u32 end=ticks+ms;
  for(;;){
    if(memcmp((const void*)addr,cmp,size)) return 1;
    if(ms!=0xFFFFFFFF && (i32)(ticks-end)>=0){ last_error=1460; return 0; }
    idle_wait();
  }
}
static void WINAPI y_WakeByAddressSingle(void *a){ (void)a; }
static void WINAPI y_WakeByAddressAll(void *a){ (void)a; }

/* SLIST_HEADER: {first entry, depth}; entries start with a Next pointer */
static void WINAPI y_InitializeSListHead(u64 *h){ h[0]=0; h[1]=0; }
static u64 *WINAPI y_InterlockedPushEntrySList(u64 *h, u64 *e){ u64 *old=(u64*)h[0]; e[0]=h[0]; h[0]=(u64)e; h[1]++; return old; }
static u64 *WINAPI y_InterlockedPopEntrySList(u64 *h){ u64 *e=(u64*)h[0]; if(e){ h[0]=e[0]; h[1]--; } return e; }
static u64 *WINAPI y_InterlockedFlushSList(u64 *h){ u64 *e=(u64*)h[0]; h[0]=0; h[1]=0; return e; }
static u16 WINAPI y_QueryDepthSList(u64 *h){ return (u16)h[1]; }
static u64 *WINAPI y_InterlockedPushListSListEx(u64 *h, u64 *first, u64 *last, DWORD n){ u64 *old=(u64*)h[0]; last[0]=h[0]; h[0]=(u64)first; h[1]+=n; return old; }
static long WINAPI y_SetThreadDescription(HANDLE t, const u16 *d){ (void)t;(void)d; return 0; }
static BOOL WINAPI y_SetThreadStackGuarantee(DWORD *s){ (void)s; return 1; }
static DWORD WINAPI y_GetCurrentProcessorNumber(void){ return 0; }

/* ================= COM / OLE basics ================= */
static int com_state[MAXPROC];
static long WINAPI y_CoInitializeEx(void *r, DWORD m){ (void)r;(void)m; int *s=&com_state[cur->pid&(MAXPROC-1)]; int first=!*s; (*s)++; return first? 0 : 1; }
static void *WINAPI y_CoTaskMemAlloc(u64 n){ return heap_alloc(n? n : 1); }
static void WINAPI y_CoTaskMemFree(void *p){ heap_free(p); }
static void *WINAPI y_CoTaskMemRealloc(void *p, u64 n){ return heap_realloc(p,n? n : 1); }
static long WINAPI y_OleInitialize(void *r){ (void)r; return 0; }
static void WINAPI y_OleUninitialize(void){}
static long WINAPI y_CoInitializeSecurity(void *a, i32 b, void *c, void *d, DWORD e, DWORD f, void *g, DWORD h, void *i){ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; return 0; }
static long WINAPI y_CoSetProxyBlanket(void *a, DWORD b, DWORD c, void *d, DWORD e, DWORD f, void *g, DWORD h){ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; return 0; }
static long WINAPI y_CoGetClassObject(const void *c, DWORD ctx, void *s, const void *iid, void **out){ (void)c;(void)ctx;(void)s;(void)iid; if(out) *out=0; return (long)0x80040154; }   /* REGDB_E_CLASSNOTREG */
static long WINAPI y_CoCreateInstanceEx(const void *c, void *o, DWORD ctx, void *s, DWORD n, void *q){ (void)c;(void)o;(void)ctx;(void)s;(void)n;(void)q; return (long)0x80040154; }
static void WINAPI y_CoFreeUnusedLibraries(void){}
static long WINAPI y_CoCreateGuid(u8 *g){ random_bytes(g,16); g[7]=(g[7]&0x0F)|0x40; g[8]=(g[8]&0x3F)|0x80; return 0; }
static const char HEXD[]="0123456789ABCDEF";
static int guid_fmt(const u8 *g, u16 *out){                                  /* {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}: 38 chars + NUL */
  static const int order[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15}; int n=0; out[n++]='{';
  for(int i=0;i<16;i++){ if(i==4||i==6||i==8||i==10) out[n++]='-'; u8 b=g[order[i]]; out[n++]=(u8)HEXD[b>>4]; out[n++]=(u8)HEXD[b&15]; }
  out[n++]='}'; out[n]=0; return n;
}
static int hexv(int c){ return c>='0'&&c<='9'? c-'0' : (c|32)>='a'&&(c|32)<='f'? (c|32)-'a'+10 : -1; }
static int guid_parse(const char *s, u8 *g){
  static const int order[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
  if(*s=='{') s++; for(int i=0;i<16;i++){ if(i==4||i==6||i==8||i==10){ if(*s!='-') return 0; s++; } int a=hexv(s[0]), b=hexv(s[1]); if(a<0||b<0) return 0; g[order[i]]=(u8)(a*16+b); s+=2; }
  return 1;
}
static int WINAPI y_StringFromGUID2(const u8 *g, u16 *out, int cch){ if(cch<39) return 0; guid_fmt(g,out); return 39; }
static long WINAPI y_CLSIDFromString(const u16 *s, u8 *g){ char a[64]; w2a(s,a,64); return guid_parse(a,g)? 0 : (long)0x800401F3; }
static long WINAPI y_StringFromCLSID(const u8 *g, u16 **out){ u16 *p=heap_alloc(80); if(!p) return (long)0x8007000E; guid_fmt(g,p); *out=p; return 0; }
/* BSTR: a UTF-16 string with a 4-byte byte-length prefix */
static u16 *WINAPI y_SysAllocStringLen(const u16 *s, u32 n){ u8 *p=heap_alloc(4+(u64)n*2+2); if(!p) return 0; *(u32*)p=n*2; u16 *b=(u16*)(p+4); if(s) memcpy(b,s,(u64)n*2); else memset(b,0,(u64)n*2); b[n]=0; return b; }
static u16 *WINAPI y_SysAllocString(const u16 *s){ return s? y_SysAllocStringLen(s,(u32)wlen(s)) : 0; }
static void WINAPI y_SysFreeString(u16 *b){ if(b) heap_free((u8*)b-4); }
static u32 WINAPI y_SysStringLen(const u16 *b){ return b? *(const u32*)((const u8*)b-4)/2 : 0; }
static u32 WINAPI y_SysStringByteLen(const u16 *b){ return b? *(const u32*)((const u8*)b-4) : 0; }
static BOOL WINAPI y_SysReAllocString(u16 **b, const u16 *s){ u16 *n=y_SysAllocString(s); if(!n) return 0; y_SysFreeString(*b); *b=n; return 1; }
static void WINAPI y_VariantInit(u16 *v){ memset(v,0,24); }
static long WINAPI y_VariantClear(u16 *v){ if(v[0]==8){ u16 *b=*(u16**)(v+4); y_SysFreeString(b); } memset(v,0,24); return 0; }

/* ================= shell folders and launching ================= */
static void mkdirs(const char *path){                                       /* "C:\\A\\B" -> create A then A\\B */
  char t[128]; int n=0; for(const char *p=path;*p&&n<126;p++){ t[n++]=*p; if((p[1]=='\\'||p[1]==0) && n>3){ t[n]=0; if(!fs_isdir(t)) fs_mkdir(t); } }
}
struct kf { const char *guid, *path; int csidl; };
static const struct kf KF[] = {
  {"B4BFCC3A-DB2C-424C-B029-7FE99A87C641","C:\\Desktop",0x10}, {"FDD39AD0-238F-46AF-ADB4-6C85480369C7","C:\\Documents",5}, {"374DE290-123F-4565-9164-39C4925E467B","C:\\Downloads",-1},
  {"3EB685DB-65F9-4CF6-A03A-E3EF65729F3D","C:\\AppData\\Roaming",0x1A}, {"F1B32785-6FBA-4FCF-9D55-7B8E7F157091","C:\\AppData\\Local",0x1C}, {"62AB5D82-FDC1-4DC3-A9DD-070D1D495D97","C:\\ProgramData",0x23},
  {"5E6C858F-0E22-4760-9AFE-EA3317B67173","C:\\",0x28}, {"F38BF404-1D43-42F2-9305-67DE0B28FC23","C:\\Windows",0x24}, {"1AC14E77-02E7-4E5D-B744-2EB1AE5198B7","C:\\Windows\\System32",0x25},
  {"905E63B6-C1BF-494E-B29C-65B732D3D21A","C:\\Program Files",0x26}, {"7C5A40EF-A0FB-4BFC-874A-C0F2E0B9FA8E","C:\\Program Files (x86)",0x2A}, {"DFDF76A2-C82A-4D63-906A-5644AC457385","C:\\Public",-1},
  {"4BD8D571-6D19-48D3-BE97-422220080E43","C:\\Documents\\Music",0x0D}, {"33E28130-4E1E-4676-835A-98395C3BC3BB","C:\\Documents\\Pictures",0x27}, {"18989B1D-99B5-455B-841C-AB7C74E4DDFC","C:\\Documents\\Videos",0x0E},
  {"ED4824AF-DCE4-45A8-81E2-FC7965083634","C:\\Public\\Documents",0x2E},
};
static const char *kf_by_guid(const u8 *g){ u8 b[16]; for(unsigned i=0;i<sizeof KF/sizeof *KF;i++) if(guid_parse(KF[i].guid,b) && !memcmp(b,g,16)) return KF[i].path; return 0; }
static const char *kf_by_csidl(int csidl){ csidl&=0xFF; if(csidl==0x00) csidl=0x10; if(csidl==0x1B||csidl==0x2B) return "C:\\Windows"; for(unsigned i=0;i<sizeof KF/sizeof *KF;i++) if(KF[i].csidl==csidl) return KF[i].path; return 0; }
static long WINAPI y_SHGetKnownFolderPath(const u8 *id, DWORD fl, HANDLE tok, u16 **out){
  (void)fl;(void)tok; const char *p=kf_by_guid(id); if(!p){ *out=0; return (long)0x80070002; }
  mkdirs(p); u16 *w=heap_alloc(((u64)strlen(p)+1)*2); if(!w) return (long)0x8007000E; a2w(p,w,260); *out=w; return 0;
}
static long WINAPI y_SHGetFolderPathW(HANDLE hwnd, int csidl, HANDLE tok, DWORD fl, u16 *out){ (void)hwnd;(void)tok;(void)fl; const char *p=kf_by_csidl(csidl); if(!p){ if(out) out[0]=0; return (long)0x80070002; } mkdirs(p); a2w(p,out,260); return 0; }
static long WINAPI y_SHGetFolderPathA(HANDLE hwnd, int csidl, HANDLE tok, DWORD fl, char *out){ (void)hwnd;(void)tok;(void)fl; const char *p=kf_by_csidl(csidl); if(!p){ if(out) out[0]=0; return (long)0x80070002; } mkdirs(p); memcpy(out,p,strlen(p)+1); return 0; }
static BOOL WINAPI y_SHGetSpecialFolderPathW(HANDLE hwnd, u16 *out, int csidl, BOOL create){ (void)hwnd;(void)create; return y_SHGetFolderPathW(0,csidl,0,0,out)==0; }
static BOOL WINAPI y_SHGetSpecialFolderPathA(HANDLE hwnd, char *out, int csidl, BOOL create){ (void)hwnd;(void)create; return y_SHGetFolderPathA(0,csidl,0,0,out)==0; }
static long WINAPI y_SHCreateDirectoryExW(HANDLE hwnd, const u16 *path, void *sa){ (void)hwnd;(void)sa; char t[260]; w2a(path,t,260); if(fs_isdir(t)) return 183; mkdirs(t); return fs_isdir(t)? 0 : 3; }
static long WINAPI y_SHCreateDirectoryExA(HANDLE hwnd, const char *path, void *sa){ (void)hwnd;(void)sa; if(fs_isdir(path)) return 183; mkdirs(path); return fs_isdir(path)? 0 : 3; }

static u64 sh_exec_a(const char *verb, const char *file, const char *params, const char *dir, int show){
  u64 (WINAPI *f)(u64,const char*,const char*,const char*,const char*,i32)=win32_resolve("shell32","ShellExecuteA"); if(!f) return 2;
  return f(0,verb,file,params,dir,show);
}
static u64 WINAPI y_ShellExecuteW(HANDLE hwnd, const u16 *verb, const u16 *file, const u16 *params, const u16 *dir, i32 show){
  (void)hwnd; char v[32]="open", fl[300], p[300]="", d[200]=""; if(verb) w2a(verb,v,32); if(!file) return 2; w2a(file,fl,300); if(params) w2a(params,p,300); if(dir) w2a(dir,d,200);
  return sh_exec_a(v,fl,p,d,show);
}
static BOOL WINAPI y_ShellExecuteExA(u8 *e){
  const char *verb=*(const char**)(e+16), *file=*(const char**)(e+24), *params=*(const char**)(e+32), *dir=*(const char**)(e+40); int show=*(i32*)(e+48);
  u64 r=sh_exec_a(verb? verb : "open",file? file : "",params? params : "",dir? dir : "",show); *(u64*)(e+56)=r; *(u64*)(e+104)=0; if(r<=32){ last_error=(u32)r; return 0; } return 1;
}
static BOOL WINAPI y_ShellExecuteExW(u8 *e){
  const u16 *verb=*(const u16**)(e+16), *file=*(const u16**)(e+24), *params=*(const u16**)(e+32), *dir=*(const u16**)(e+40); int show=*(i32*)(e+48);
  char v[32]="open", f[300]="", p[300]="", d[200]=""; if(verb) w2a(verb,v,32); if(file) w2a(file,f,300); if(params) w2a(params,p,300); if(dir) w2a(dir,d,200);
  u64 r=sh_exec_a(v,f,p,d,show); *(u64*)(e+56)=r; *(u64*)(e+104)=0; if(r<=32){ last_error=(u32)r; return 0; } return 1;
}
/* CommandLineToArgvW: the Microsoft rules for backslashes and quotes. The result is one LocalAlloc block (the caller LocalFree()s it). */
static u16 **WINAPI y_CommandLineToArgvW(const u16 *cl, int *argc){
  if(!cl){ if(argc) *argc=0; return 0; }
  int len=wlen(cl); u64 cap=(u64)(len+2)*2+16*8+64; u8 *blk=heap_alloc(cap+ (u64)(len+2)*8); if(!blk) return 0;
  u16 **argv=(u16**)blk; u16 *buf=(u16*)(blk+(u64)(len/2+3)*8); int n=0, nb=0; const u16 *p=cl;
  if(!*p){ argv[n++]=buf; buf[nb++]=0; }                                         /* empty command line: argv[0] is the empty program name */
  else {
    int first=1;
    while(*p){
      while(*p==' '||*p=='\t') p++; if(!*p) break;
      argv[n++]=buf+nb; int inq=0;
      while(*p && (inq || (*p!=' ' && *p!='\t'))){
        if(first && *p=='"'){ inq=!inq; p++; continue; }                         /* argv[0] has no escape rules */
        if(!first && *p=='\\'){ int bs=0; while(*p=='\\'){ bs++; p++; } if(*p=='"'){ for(int i=0;i<bs/2;i++) buf[nb++]='\\'; if(bs&1){ buf[nb++]='"'; p++; } } else for(int i=0;i<bs;i++) buf[nb++]='\\'; continue; }
        if(!first && *p=='"'){ if(inq && p[1]=='"'){ buf[nb++]='"'; p+=2; continue; } inq=!inq; p++; continue; }
        buf[nb++]=*p++;
      }
      buf[nb++]=0; first=0;
    }
  }
  argv[n]=0; if(argc) *argc=n; return argv;
}
/* shlwapi paths (backslash or slash) */
static int is_sep(int c){ return c=='\\'||c=='/'; }
static BOOL WINAPI y_PathFileExistsA(const char *p){ return p && (fs_find(p) || fs_isdir(p)); }
static BOOL WINAPI y_PathFileExistsW(const u16 *p){ char t[260]; w2a(p,t,260); return y_PathFileExistsA(t); }
static BOOL WINAPI y_PathIsDirectoryA(const char *p){ return p && fs_isdir(p); }
static BOOL WINAPI y_PathIsDirectoryW(const u16 *p){ char t[260]; w2a(p,t,260); return fs_isdir(t); }
static const char *WINAPI y_PathFindFileNameA(const char *p){ const char *r=p; for(;*p;p++) if(is_sep(*p)) r=p+1; return r; }
static const u16 *WINAPI y_PathFindFileNameW(const u16 *p){ const u16 *r=p; for(;*p;p++) if(is_sep(*p)) r=p+1; return r; }
static const char *WINAPI y_PathFindExtensionA(const char *p){ const char *f=y_PathFindFileNameA(p), *d=0; for(;*f;f++) if(*f=='.') d=f; return d? d : f; }
static const u16 *WINAPI y_PathFindExtensionW(const u16 *p){ const u16 *f=y_PathFindFileNameW(p), *d=0; for(;*f;f++) if(*f=='.') d=f; return d? d : f; }
static BOOL WINAPI y_PathRemoveFileSpecW(u16 *p){ u16 *f=(u16*)y_PathFindFileNameW(p); if(f==p) { if(*p){ *p=0; return 1; } return 0; } if(f>p+1 && is_sep(f[-1]) && !(f-p==3 && p[1]==':')) f--; *f=0; return 1; }
static BOOL WINAPI y_PathRemoveFileSpecA(char *p){ char *f=(char*)y_PathFindFileNameA(p); if(f==p){ if(*p){ *p=0; return 1; } return 0; } if(f>p+1 && is_sep(f[-1]) && !(f-p==3 && p[1]==':')) f--; *f=0; return 1; }
static BOOL WINAPI y_PathIsRelativeW(const u16 *p){ return !(is_sep(p[0]) || (p[0] && p[1]==':')); }
static BOOL WINAPI y_PathIsRelativeA(const char *p){ return !(is_sep(p[0]) || (p[0] && p[1]==':')); }
static BOOL WINAPI y_PathAppendW(u16 *p, const u16 *more){ int n=wlen(p); if(n && !is_sep(p[n-1]) && n<258) p[n++]='\\'; while(is_sep(*more)) more++; int i=0; while(more[i] && n<259) p[n++]=more[i++]; p[n]=0; return 1; }
static BOOL WINAPI y_PathAppendA(char *p, const char *more){ int n=(int)strlen(p); if(n && !is_sep(p[n-1]) && n<258) p[n++]='\\'; while(is_sep(*more)) more++; int i=0; while(more[i] && n<259) p[n++]=more[i++]; p[n]=0; return 1; }
static u16 *WINAPI y_PathCombineW(u16 *out, const u16 *a, const u16 *b){ int n=0; if(a && (!b || y_PathIsRelativeW(b))){ for(;a[n]&&n<259;n++) out[n]=a[n]; } out[n]=0; if(b) y_PathAppendW(out,b); return out; }
static char *WINAPI y_PathCombineA(char *out, const char *a, const char *b){ int n=0; if(a && (!b || y_PathIsRelativeA(b))){ for(;a[n]&&n<259;n++) out[n]=a[n]; } out[n]=0; if(b) y_PathAppendA(out,b); return out; }
static void WINAPI y_PathStripPathW(u16 *p){ const u16 *f=y_PathFindFileNameW(p); if(f!=p){ int i=0; while((p[i]=f[i])) i++; } }
static void WINAPI y_PathStripPathA(char *p){ const char *f=y_PathFindFileNameA(p); if(f!=p){ int i=0; while((p[i]=f[i])) i++; } }
static void WINAPI y_PathRemoveExtensionW(u16 *p){ u16 *e=(u16*)y_PathFindExtensionW(p); *e=0; }
static u16 *WINAPI y_PathAddBackslashW(u16 *p){ int n=wlen(p); if(n && !is_sep(p[n-1]) && n<259){ p[n++]='\\'; p[n]=0; } return p+n; }
static const u16 *WINAPI y_StrStrIW(const u16 *h, const u16 *n){ int nl=wlen(n); if(!nl) return h; for(;*h;h++){ int i=0; while(i<nl && h[i] && ((h[i]|32)==(n[i]|32))) i++; if(i==nl) return h; } return 0; }
static const u16 *WINAPI y_StrStrW(const u16 *h, const u16 *n){ int nl=wlen(n); if(!nl) return h; for(;*h;h++){ int i=0; while(i<nl && h[i]==n[i]) i++; if(i==nl) return h; } return 0; }
static const char *WINAPI y_StrStrIA(const char *h, const char *n){ int nl=(int)strlen(n); if(!nl) return h; for(;*h;h++){ int i=0; while(i<nl && h[i] && ((h[i]|32)==(n[i]|32))) i++; if(i==nl) return h; } return 0; }
static const char *WINAPI y_StrStrA(const char *h, const char *n){ int nl=(int)strlen(n); if(!nl) return h; for(;*h;h++){ int i=0; while(i<nl && h[i]==n[i]) i++; if(i==nl) return h; } return 0; }
static BOOL WINAPI y_SetConsoleOutputCP(DWORD cp){ (void)cp; return 1; }
static int WINAPI y_StrCmpIW(const u16 *a, const u16 *b){ for(;*a && (*a|32)==(*b|32);a++,b++); return (int)(*a|32)-(int)(*b|32); }

/* ================= processes ================= */
static BOOL WINAPI y_CreateProcessW(const u16 *app, u16 *cmd, void *pa, void *ta, BOOL inh, DWORD fl, void *env, const u16 *dir, void *si, u32 *pi){
  BOOL (WINAPI *f)(const char*,char*,void*,void*,BOOL,DWORD,void*,const char*,void*,u32*)=K32("CreateProcessA"); if(!f) return 0;
  char a[260], c[512], d[200]; char *ap=0, *cp=0, *dp=0;
  if(app){ w2a(app,a,260); ap=a; } if(cmd){ w2a(cmd,c,512); cp=c; } if(dir){ w2a(dir,d,200); dp=d; }
  return f(ap,cp,pa,ta,inh,fl,env,dp,si,pi);
}
static DWORD WINAPI y_GetProcessId(HANDLE h){ if(h==(HANDLE)-1) return (DWORD)cur->pid; if(h>=0x8000 && h<0x8000+MAXPROC) return (DWORD)(h-0x8000); return 0; }
static DWORD WINAPI y_GetThreadId(HANDLE h){ (void)h; return 0x1000; }
static BOOL WINAPI y_SetPriorityClass(HANDLE h, DWORD c){ (void)h;(void)c; return 1; }
static DWORD WINAPI y_GetPriorityClass(HANDLE h){ (void)h; return 0x20; }
static DWORD WINAPI y_WaitForInputIdle(HANDLE h, DWORD ms){ (void)h;(void)ms; return 0; }
static BOOL WINAPI y_EnumProcesses(DWORD *pids, DWORD cb, DWORD *needed){
  DWORD n=0; for(int i=0;i<MAXPROC;i++) if(procs[i].used && (n+1)*4<=cb) pids[n++]=(DWORD)procs[i].pid; if(needed) *needed=n*4; return 1;
}
static int pid_of(HANDLE h){ if(h==(HANDLE)-1) return cur->pid; if(h>=0x8000 && h<0x8000+MAXPROC && procs[h-0x8000].used) return (int)(h-0x8000); return -1; }
static BOOL WINAPI y_GetProcessMemoryInfo(HANDLE h, u8 *pmc, DWORD cb){
  if(pid_of(h)<0 || cb<72){ last_error=87; return 0; } memset(pmc,0,cb); *(u32*)pmc=72; *(u32*)(pmc+4)=1000;
  u64 ws=6u<<20; *(u64*)(pmc+8)=ws+(2u<<20); *(u64*)(pmc+16)=ws; *(u64*)(pmc+24)=64<<10; *(u64*)(pmc+32)=64<<10; *(u64*)(pmc+40)=32<<10; *(u64*)(pmc+48)=32<<10; *(u64*)(pmc+56)=ws; *(u64*)(pmc+64)=ws+(2u<<20); return 1;
}
static BOOL WINAPI y_EnumProcessModules(HANDLE h, u64 *mods, DWORD cb, DWORD *needed){ int p=pid_of(h); if(p<0){ last_error=6; return 0; } if(needed) *needed=8; if(cb>=8 && mods) mods[0]=procs[p].imagebase; return 1; }
static DWORD WINAPI y_GetModuleBaseNameW(HANDLE h, HANDLE m, u16 *out, DWORD n){ (void)m; int p=pid_of(h); if(p<0||!n) return 0; return (DWORD)a2w(procs[p].name,out,(int)n); }
static DWORD WINAPI y_GetModuleBaseNameA(HANDLE h, HANDLE m, char *out, DWORD n){ (void)m; int p=pid_of(h); if(p<0||!n) return 0; unsigned i=0; for(;procs[p].name[i]&&i+1<n;i++) out[i]=procs[p].name[i]; out[i]=0; return i; }
static DWORD WINAPI y_GetModuleFileNameExW(HANDLE h, HANDLE m, u16 *out, DWORD n){ (void)m; int p=pid_of(h); if(p<0||n<12) return 0; char t[80]="C:\\Willows\\"; cat_(t,procs[p].name); return (DWORD)a2w(t,out,(int)n); }
static DWORD WINAPI y_GetModuleFileNameExA(HANDLE h, HANDLE m, char *out, DWORD n){ (void)m; int p=pid_of(h); if(p<0||n<12) return 0; char t[80]="C:\\Willows\\"; cat_(t,procs[p].name); unsigned i=0; for(;t[i]&&i+1<n;i++) out[i]=t[i]; out[i]=0; return i; }
static DWORD WINAPI y_QueryFullProcessImageNameW(HANDLE h, DWORD f, u16 *out, DWORD *n){ (void)f; int p=pid_of(h); if(p<0||!n){ last_error=6; return 0; } char t[80]="C:\\Willows\\"; cat_(t,procs[p].name); DWORD l=(DWORD)strlen(t); if(*n<=l){ *n=l+1; last_error=122; return 0; } a2w(t,out,(int)*n); *n=l; return 1; }
static int job_seq;
static HANDLE WINAPI y_CreateJobObjectW(void *sa, const u16 *name){ (void)sa;(void)name; return 0x7F00+(HANDLE)(job_seq++&0xFF); }
static HANDLE WINAPI y_CreateJobObjectA(void *sa, const char *name){ (void)sa;(void)name; return 0x7F00+(HANDLE)(job_seq++&0xFF); }
static BOOL WINAPI y_AssignProcessToJobObject(HANDLE j, HANDLE p){ (void)j;(void)p; return 1; }
static BOOL WINAPI y_SetInformationJobObject(HANDLE j, i32 c, void *i, DWORD l){ (void)j;(void)c;(void)i;(void)l; return 1; }
static BOOL WINAPI y_QueryInformationJobObject(HANDLE j, i32 c, void *i, DWORD l, DWORD *r){ (void)j;(void)c; if(i&&l) memset(i,0,l); if(r) *r=l; return 1; }
static BOOL WINAPI y_TerminateJobObject(HANDLE j, DWORD c){ (void)j;(void)c; return 1; }
static BOOL WINAPI y_IsProcessInJob(HANDLE p, HANDLE j, BOOL *r){ (void)p;(void)j; if(r) *r=0; return 1; }

#define YP(n) {"psapi", #n, y_##n}
const struct export y_exports[] = {
  Y(InitializeSRWLock), Y(AcquireSRWLockExclusive), Y(ReleaseSRWLockExclusive), Y(AcquireSRWLockShared), Y(ReleaseSRWLockShared), Y(TryAcquireSRWLockExclusive), Y(TryAcquireSRWLockShared), Y(TryEnterCriticalSection),
  Y(InitOnceInitialize), Y(InitOnceExecuteOnce), Y(InitOnceBeginInitialize), Y(InitOnceComplete),
  {"kernel32","WaitOnAddress",y_WaitOnAddress}, {"api-ms-win-core-synch-l1-2-0","WaitOnAddress",y_WaitOnAddress}, {"kernel32","WakeByAddressSingle",y_WakeByAddressSingle}, {"kernel32","WakeByAddressAll",y_WakeByAddressAll},
  Y(InitializeSListHead), Y(InterlockedPushEntrySList), Y(InterlockedPopEntrySList), Y(InterlockedFlushSList), Y(QueryDepthSList), Y(InterlockedPushListSListEx), Y(SetThreadDescription), Y(SetThreadStackGuarantee), Y(GetCurrentProcessorNumber),
  YD("ole32",CoInitializeEx), YD("ole32",CoTaskMemAlloc), YD("ole32",CoTaskMemFree), YD("ole32",CoTaskMemRealloc), YD("ole32",OleInitialize), YD("ole32",OleUninitialize), YD("ole32",CoInitializeSecurity), YD("ole32",CoSetProxyBlanket),
  YD("ole32",CoGetClassObject), YD("ole32",CoCreateInstanceEx), YD("ole32",CoFreeUnusedLibraries), YD("ole32",CoCreateGuid), YD("ole32",StringFromGUID2), YD("ole32",CLSIDFromString), {"ole32","IIDFromString",y_CLSIDFromString}, YD("ole32",StringFromCLSID), {"ole32","StringFromIID",y_StringFromCLSID},
  YD("oleaut32",SysAllocString), YD("oleaut32",SysAllocStringLen), YD("oleaut32",SysFreeString), YD("oleaut32",SysStringLen), YD("oleaut32",SysStringByteLen), YD("oleaut32",SysReAllocString), YD("oleaut32",VariantInit), YD("oleaut32",VariantClear),
  YD("shell32",SHGetKnownFolderPath), YD("shell32",SHGetFolderPathW), YD("shell32",SHGetFolderPathA), YD("shell32",SHGetSpecialFolderPathW), YD("shell32",SHGetSpecialFolderPathA), YD("shell32",SHCreateDirectoryExW), YD("shell32",SHCreateDirectoryExA),
  YD("shell32",ShellExecuteW), YD("shell32",ShellExecuteExA), YD("shell32",ShellExecuteExW), YD("shell32",CommandLineToArgvW),
  YD("shlwapi",PathFileExistsA), YD("shlwapi",PathFileExistsW), YD("shlwapi",PathIsDirectoryA), YD("shlwapi",PathIsDirectoryW), YD("shlwapi",PathFindFileNameA), YD("shlwapi",PathFindFileNameW), YD("shlwapi",PathFindExtensionA), YD("shlwapi",PathFindExtensionW),
  YD("shlwapi",PathRemoveFileSpecA), YD("shlwapi",PathRemoveFileSpecW), YD("shlwapi",PathIsRelativeA), YD("shlwapi",PathIsRelativeW), YD("shlwapi",PathAppendA), YD("shlwapi",PathAppendW), YD("shlwapi",PathCombineA), YD("shlwapi",PathCombineW),
  YD("shlwapi",PathStripPathA), YD("shlwapi",PathStripPathW), YD("shlwapi",PathRemoveExtensionW), YD("shlwapi",PathAddBackslashW), YD("shlwapi",StrStrIW), YD("shlwapi",StrStrW), YD("shlwapi",StrCmpIW), YD("shlwapi",StrStrIA), YD("shlwapi",StrStrA), Y(SetConsoleOutputCP),
  Y(CreateProcessW), Y(GetProcessId), Y(GetThreadId), Y(SetPriorityClass), Y(GetPriorityClass), Y(WaitForInputIdle), YD("user32",WaitForInputIdle),
  YP(EnumProcesses), YP(GetProcessMemoryInfo), YP(EnumProcessModules), YP(GetModuleBaseNameW), YP(GetModuleBaseNameA), YP(GetModuleFileNameExW), YP(GetModuleFileNameExA),
  {"kernel32","K32EnumProcesses",y_EnumProcesses}, {"kernel32","K32GetProcessMemoryInfo",y_GetProcessMemoryInfo}, {"kernel32","K32EnumProcessModules",y_EnumProcessModules}, {"kernel32","K32GetModuleBaseNameW",y_GetModuleBaseNameW},
  {"kernel32","K32GetModuleBaseNameA",y_GetModuleBaseNameA}, {"kernel32","K32GetModuleFileNameExW",y_GetModuleFileNameExW}, {"kernel32","K32GetModuleFileNameExA",y_GetModuleFileNameExA}, Y(QueryFullProcessImageNameW),
  Y(CreateJobObjectW), Y(CreateJobObjectA), Y(AssignProcessToJobObject), Y(SetInformationJobObject), Y(QueryInformationJobObject), Y(TerminateJobObject), Y(IsProcessInJob),
};
const unsigned y_nexports = sizeof y_exports / sizeof *y_exports;
