#include "kernel.h"
/* Win32 batch 1: modern locks (SRW, InitOnce, WaitOnAddress, SLists), COM/OLE basics (CoTaskMem, GUID strings, BSTR),
   shell folders and launching (SHGetKnownFolderPath, ShellExecuteW, CommandLineToArgvW, shlwapi paths),
   process helpers (CreateProcessW, psapi, job objects). Willows schedules cooperatively, so locks only need to be correct, not fast. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE;
#define Y(n) {"kernel32", #n, y_##n}
#define YD(d,n) {d, #n, y_##n}
static u64 dummy_teb_y[0x2000/8];
#define last_error (*(u32*)((u8*)(cur_teb? cur_teb : dummy_teb_y)+0x68))     /* the running thread's last error */

static int wlen(const u16 *s){ int n=0; while(s&&s[n]) n++; return n; }
static void w2a(const u16 *w, char *a, int n){ int i=0; for(;w&&w[i]&&i<n-1;i++) a[i]= w[i]<0x80? (char)w[i] : '?'; a[i]=0; }
static int a2w(const char *a, u16 *w, int n){ int i=0; for(;a[i]&&i<n-1;i++) w[i]=(u8)a[i]; w[i]=0; return i; }
static void *K32(const char *n){ return win32_resolve("kernel32",n); }
static void cat_(char *d, const char *s){ while(*d) d++; while((*d++=*s++)); }

/* ================= locks and one-time initialisation ================= */
/* SRW locks, INIT_ONCE and critical sections: see thread.c. WaitOnAddress polls (cooperative scheduling). */
static BOOL WINAPI y_WaitOnAddress(volatile void *addr, void *cmp, u64 size, DWORD ms){
  u32 end=ticks+ms; thread_note_wait("WaitOnAddress",(u64)addr,(u64)__builtin_return_address(0));
  for(;;){
    if(memcmp((const void*)addr,cmp,size)){ thread_end_wait(); return 1; }
    if(ms!=0xFFFFFFFF && (i32)(ticks-end)>=0){ thread_end_wait(); last_error=1460; return 0; }
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
/* COM apartments, per thread (TEB+0x1B10: type 1 = STA, 2 = MTA; TEB+0x1B14: CoInitialize count). mta_users counts the threads of a
   process that are in the multi-threaded apartment: while there are any, uninitialised threads are implicitly in it too. */
static int mta_users[MAXPROC];
void com_proc_reset(int pid){ mta_users[pid]=0; }                   /* a new process in this slot */
static u32 *apt(void){ u64 *t=win32_cur_teb(); static u32 none[2]; return t? (u32*)((u8*)t+0x1B10) : none; }
static long WINAPI y_CoInitializeEx(void *r, DWORD m){
  (void)r; u32 *a=apt(); u32 want=(m&2)? 1 : 2;                                    /* COINIT_APARTMENTTHREADED */
  if(a[1] && a[0]!=want) return (long)0x80010106;                                   /* RPC_E_CHANGED_MODE */
  if(a[1]++){ return 1; }                                                           /* S_FALSE: already initialised */
  a[0]=want; if(want==2) mta_users[cur->pid]++; return 0;
}
static long WINAPI y_CoInitialize(void *r){ return y_CoInitializeEx(r,2); }
static void WINAPI y_CoUninitialize(void){ u32 *a=apt(); if(!a[1]) return; if(--a[1]==0){ if(a[0]==2 && mta_users[cur->pid]>0) mta_users[cur->pid]--; a[0]=0; } }
static long WINAPI y_CoGetApartmentType(i32 *type, i32 *qual){
  if(!type || !qual) return (long)0x80070057;
  u32 *a=apt(); *qual=0;
  if(a[1]){ *type= a[0]==2? 1 : (cur_thr? 0 : 3); return 0; }                   /* APTTYPE_MTA, APTTYPE_STA, APTTYPE_MAINSTA */
  if(mta_users[cur->pid]){ *type=1; *qual=1; return 0; }                            /* APTTYPEQUALIFIER_IMPLICIT_MTA */
  *type=-1; return (long)0x800401F0;                                                /* CO_E_NOTINITIALIZED */
}
static void *WINAPI y_CoTaskMemAlloc(u64 n){ return heap_alloc_prog(n? n : 1); }
static void WINAPI y_CoTaskMemFree(void *p){ heap_free(p); }
static void *WINAPI y_CoTaskMemRealloc(void *p, u64 n){ return heap_realloc(p,n? n : 1); }
static long WINAPI y_OleInitialize(void *r){ return y_CoInitializeEx(r,2); }
static void WINAPI y_OleUninitialize(void){ y_CoUninitialize(); }
static long WINAPI y_CoInitializeSecurity(void *a, i32 b, void *c, void *d, DWORD e, DWORD f, void *g, DWORD h, void *i){ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; return 0; }
static long WINAPI y_CoSetProxyBlanket(void *a, DWORD b, DWORD c, void *d, DWORD e, DWORD f, void *g, DWORD h){ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; return 0; }
static long WINAPI y_CoGetClassObject(const void *c, DWORD ctx, void *s, const void *iid, void **out){ (void)c;(void)ctx;(void)s;(void)iid; if(out) *out=0; return (long)0x80040154; }   /* REGDB_E_CLASSNOTREG */
static long WINAPI y_CoCreateInstanceEx(const u8 *c, void *o, DWORD ctx, void *s, DWORD n, u8 *q);   /* (below, with the built-in COM classes) */
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
  char a[260], d[200]; static char c[2048]; char *ap=0, *cp=0, *dp=0;
  if(app){ w2a(app,a,260); ap=a; } if(cmd){ w2a(cmd,c,2048); cp=c; } if(dir){ w2a(dir,d,200); dp=d; }
  return f(ap,cp,pa,ta,inh,fl,env,dp,si,pi);
}
static DWORD WINAPI y_GetProcessId(HANDLE h){ if(h==(HANDLE)-1) return (DWORD)cur->pid; if(h>=0x8000 && h<0x8000+MAXPROC) return (DWORD)(h-0x8000); return 0; }
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

/* ================= WinRT basics: HSTRING and activation ================= */
/* An HSTRING points to {flags, length, buffer}. WindowsCreateStringReference keeps it in the caller's HSTRING_HEADER (24 bytes);
   WindowsCreateString allocates it with a copy of the text. NULL is the empty string. No WinRT classes exist, so activation fails cleanly. */
struct hstr { u32 flags, len; const u16 *buf; };
static long WINAPI y_WindowsCreateString(const u16 *s, u32 n, struct hstr **out){
  if(!out) return (long)0x80070057; if(!n){ *out=0; return 0; } if(!s) return (long)0x80004003;
  struct hstr *h=heap_alloc(sizeof *h+(u64)(n+1)*2); if(!h) return (long)0x8007000E;
  u16 *b=(u16*)(h+1); memcpy(b,s,(u64)n*2); b[n]=0; h->flags=1; h->len=n; h->buf=b; *out=h; return 0;
}
static long WINAPI y_WindowsCreateStringReference(const u16 *s, u32 n, struct hstr *hdr, struct hstr **out){
  if(!out || !hdr) return (long)0x80070057; if(!n){ *out=0; return 0; }
  hdr->flags=0; hdr->len=n; hdr->buf=s; *out=hdr; return 0;
}
static long WINAPI y_WindowsDeleteString(struct hstr *h){ if(h && h->flags==1) heap_free(h); return 0; }
static const u16 *WINAPI y_WindowsGetStringRawBuffer(struct hstr *h, u32 *len){ static const u16 empty[1]; if(len) *len= h? h->len : 0; return h? h->buf : empty; }
static u32 WINAPI y_WindowsGetStringLen(struct hstr *h){ return h? h->len : 0; }
static i32 WINAPI y_WindowsIsStringEmpty(struct hstr *h){ return !h || !h->len; }
static long WINAPI y_WindowsDuplicateString(struct hstr *h, struct hstr **out){ return h? y_WindowsCreateString(h->buf,h->len,out) : (*out=0,0); }
static long WINAPI y_RoInitialize(i32 type){ long r=y_CoInitializeEx(0,type? 0 : 2); return r==1? 1 : r; }
static void WINAPI y_RoUninitialize(void){ y_CoUninitialize(); }
static long WINAPI y_RoGetActivationFactory(struct hstr *cls, void *iid, void **out){ (void)cls;(void)iid; if(out) *out=0; return (long)0x80040154; }   /* REGDB_E_CLASSNOTREG */
static long WINAPI y_RoActivateInstance(struct hstr *cls, void **out){ (void)cls; if(out) *out=0; return (long)0x80040154; }

/* ================= process creation extras ================= */
/* PROC_THREAD_ATTRIBUTE_LIST (opaque to programs): u32 count, u32 used, then {attribute, value pointer, size} entries from +16.
   CreateProcess uses the handle list (PROC_THREAD_ATTRIBUTE_HANDLE_LIST): the child gets its own reference to each of those
   handles, so they stay valid after the parent closes its copies. Mitigation policies and the parent process change nothing. */
static BOOL WINAPI y_InitializeProcThreadAttributeList(u8 *list, DWORD count, DWORD fl, u64 *size){
  (void)fl; u64 need=48+(u64)count*24; if(!list || *size<need){ *size=need; last_error=122; return 0; }   /* ERROR_INSUFFICIENT_BUFFER */
  memset(list,0,need); *(u32*)list=count; return 1;
}
static BOOL WINAPI y_UpdateProcThreadAttribute(u8 *list, DWORD fl, u64 attr, void *val, u64 size, void *prev, u64 *ret){
  (void)fl;(void)prev;(void)ret; if(!list){ last_error=87; return 0; }
  u32 n=*(u32*)list, used=*(u32*)(list+4); u64 *e=(u64*)(list+16);
  for(u32 i=0;i<used;i++) if(e[i*3]==attr){ e[i*3+1]=(u64)val; e[i*3+2]=size; return 1; }
  if(used>=n){ last_error=122; return 0; }
  e[used*3]=attr; e[used*3+1]=(u64)val; e[used*3+2]=size; *(u32*)(list+4)=used+1; return 1;
}
u64 *proc_attr_find(const u8 *list, u64 attr, u64 *size){                 /* for CreateProcess */
  if(!list) return 0; u32 used=*(const u32*)(list+4); const u64 *e=(const u64*)(list+16);
  for(u32 i=0;i<used && i<64;i++) if(e[i*3]==attr){ if(size) *size=e[i*3+2]; return (u64*)e[i*3+1]; }
  return 0;
}
static void WINAPI y_DeleteProcThreadAttributeList(u8 *list){ (void)list; }
static BOOL WINAPI y_CreateProcessW(const u16 *app, u16 *cmd, void *pa, void *ta, BOOL inh, DWORD fl, void *env, const u16 *dir, void *si, u32 *pi);
static BOOL WINAPI y_CreateProcessAsUserW(HANDLE tok, const u16 *app, u16 *cmd, void *pa, void *ta, BOOL inh, DWORD fl, void *env, const u16 *dir, void *si, u32 *pi){ (void)tok; return y_CreateProcessW(app,cmd,pa,ta,inh,fl,env,dir,si,pi); }   /* the token is not enforced */

/* ================= window stations and desktops: Willows has one of each; sandboxes ask for separate ones ================= */
#define WINSTA_H 0x7E10
#define DESKTOP_HANDLE 0x7E20
static HANDLE WINAPI y_CreateWindowStationW(const u16 *n, DWORD fl, DWORD acc, void *sa){ (void)n;(void)fl;(void)acc;(void)sa; return WINSTA_H+1; }
static HANDLE WINAPI y_OpenWindowStationW(const u16 *n, BOOL inh, DWORD acc){ (void)n;(void)inh;(void)acc; return WINSTA_H; }
static HANDLE WINAPI y_GetProcessWindowStation(void){ return WINSTA_H; }
static BOOL WINAPI y_SetProcessWindowStation(HANDLE h){ (void)h; return 1; }
static BOOL WINAPI y_CloseWindowStation(HANDLE h){ (void)h; return 1; }
static HANDLE WINAPI y_CreateDesktopW(const u16 *n, const u16 *dev, void *dm, DWORD fl, DWORD acc, void *sa){ (void)n;(void)dev;(void)dm;(void)fl;(void)acc;(void)sa; return DESKTOP_HANDLE+1; }
static HANDLE WINAPI y_OpenDesktopW(const u16 *n, DWORD fl, BOOL inh, DWORD acc){ (void)n;(void)fl;(void)inh;(void)acc; return DESKTOP_HANDLE; }
static HANDLE WINAPI y_OpenInputDesktop(DWORD fl, BOOL inh, DWORD acc){ (void)fl;(void)inh;(void)acc; return DESKTOP_HANDLE; }
static HANDLE WINAPI y_GetThreadDesktop(DWORD tid){ (void)tid; return DESKTOP_HANDLE; }
static BOOL WINAPI y_SetThreadDesktop(HANDLE h){ (void)h; return 1; }
static BOOL WINAPI y_CloseDesktop(HANDLE h){ (void)h; return 1; }
static BOOL WINAPI y_SwitchDesktop(HANDLE h){ (void)h; return 1; }

/* ================= time, memory comparison, process info ================= */
static void WINAPI y_GetSystemTimePreciseAsFileTime(u64 *ft){ void (WINAPI *f)(u64*)=K32("GetSystemTimeAsFileTime"); if(f) f(ft); }
static void WINAPI y_QueryInterruptTime(u64 *t){ *t=(u64)ticks*10000; }                          /* 100 ns units since boot */
static BOOL WINAPI y_QueryUnbiasedInterruptTime(u64 *t){ *t=(u64)ticks*10000; return 1; }
static u64 WINAPI y_RtlCompareMemory(const u8 *a, const u8 *b, u64 n){ u64 i=0; while(i<n && a[i]==b[i]) i++; return i; }   /* number of equal bytes */
static long WINAPI y_CoIncrementMTAUsage(u64 *cookie){ mta_users[cur->pid]++; if(cookie) *cookie=1; return 0; }
static long WINAPI y_CoDecrementMTAUsage(u64 cookie){ (void)cookie; if(mta_users[cur->pid]>0) mta_users[cur->pid]--; return 0; }
static BOOL WINAPI y_IsWow64Process2(HANDLE h, u16 *proc, u16 *native){ (void)h; if(proc) *proc=0; if(native) *native=0x8664; return 1; }   /* not WOW64; x64 machine */
static DWORD WINAPI y_GetDynamicTimeZoneInformation(u8 *tz){                                   /* UTC, no daylight saving */
  memset(tz,0,432); const char *n="Coordinated Universal Time"; for(int i=0;n[i];i++){ ((u16*)(tz+4))[i]=(u8)n[i]; ((u16*)(tz+88))[i]=(u8)n[i]; }
  const char *k="UTC"; for(int i=0;k[i];i++) ((u16*)(tz+172))[i]=(u8)k[i]; return 0;      /* TIME_ZONE_ID_UNKNOWN */
}
/* the system clock is UTC: every year has the same rules, and local time = UTC */
static BOOL WINAPI y_GetTimeZoneInformationForYear(u16 year, const void *dyn, u8 *tz){ (void)year;(void)dyn; u8 d[432]; y_GetDynamicTimeZoneInformation(d); memcpy(tz,d,172); return 1; }
static BOOL WINAPI y_SystemTimeToTzSpecificLocalTimeEx(const void *tz, const u8 *u, u8 *l){ (void)tz; memcpy(l,u,16); return 1; }
static BOOL WINAPI y_TzSpecificLocalTimeToSystemTime(const void *tz, const u8 *l, u8 *u){ (void)tz; memcpy(u,l,16); return 1; }
static DWORD WINAPI y_EnumDynamicTimeZoneInformation(DWORD i, u8 *tz){ if(i) return 259; y_GetDynamicTimeZoneInformation(tz); return 0; }   /* ERROR_NO_MORE_ITEMS after the one zone */

/* ================= setupapi: a machine with no enumerable devices ================= */
static HANDLE WINAPI y_SetupDiGetClassDevsW(const void *guid, const u16 *en, u64 parent, DWORD fl){ (void)guid;(void)en;(void)parent;(void)fl; return 0x7E00; }   /* an empty device set */
static HANDLE WINAPI y_SetupDiGetClassDevsA(const void *guid, const char *en, u64 parent, DWORD fl){ (void)guid;(void)en;(void)parent;(void)fl; return 0x7E00; }
static BOOL WINAPI y_SetupDiEnumDeviceInfo(HANDLE set, DWORD i, void *info){ (void)set;(void)i;(void)info; last_error=259; return 0; }       /* ERROR_NO_MORE_ITEMS */
static BOOL WINAPI y_SetupDiEnumDeviceInterfaces(HANDLE set, void *info, const void *g, DWORD i, void *out){ (void)set;(void)info;(void)g;(void)i;(void)out; last_error=259; return 0; }
static BOOL WINAPI y_SetupDiDestroyDeviceInfoList(HANDLE set){ (void)set; return 1; }

/* ================= winmm timing ================= */
static u32 WINAPI y_timeGetTime(void){ return ticks; }
static u32 WINAPI y_timeBeginPeriod(u32 ms){ (void)ms; return 0; }
static u32 WINAPI y_timeGetDevCaps(u32 *tc, u32 n){ if(!tc || n<8) return 97; tc[0]=1; tc[1]=1000000; return 0; }   /* TIMERR_NOCANDO otherwise */
static u32 WINAPI y_GetShortPathNameW(const u16 *src, u16 *dst, u32 n){ u32 l=0; while(src[l]) l++; if(!dst || n<=l) return l+1; memcpy(dst,src,((u64)l+1)*2); return l; }

#define YP(n) {"psapi", #n, y_##n}
/* ================= the common file dialogs as COM objects =================
   Firefox (and most modern programs) ask for a file through CoCreateInstance(CLSID_FileSaveDialog / FileOpenDialog), then
   IFileDialog::Show and GetResult -> IShellItem::GetDisplayName. These objects run Willows' own dialog (comdlg32 GetSave/OpenFileNameA).
   Without them a download that asks where to save failed ("The Windows file-dialog could not be opened"). */
#define S_OK_ 0
#define E_NOINTERFACE_ ((long)0x80004002)
#define E_NOTIMPL_ ((long)0x80004001)
#define E_UNEXPECTED_ ((long)0x8000FFFF)
#define E_POINTER_ ((long)0x80004003)
static int guid_is(const u8 *g, u32 d1, u16 d2, u16 d3, const char *d4){   /* d4: 16 hex digits */
  if(!g || *(const u32 *)g!=d1 || *(const u16 *)(g+4)!=d2 || *(const u16 *)(g+6)!=d3) return 0;
  for(int i=0;i<8;i++){ int hi=d4[2*i], lo=d4[2*i+1]; hi= hi<='9'? hi-'0' : (hi|32)-'a'+10; lo= lo<='9'? lo-'0' : (lo|32)-'a'+10; if(g[8+i]!=(u8)(hi*16+lo)) return 0; }
  return 1;
}
#define IS_IUNKNOWN(g)     guid_is(g,0x00000000,0x0000,0x0000,"c000000000000046")
#define IS_ISHELLITEM(g)   guid_is(g,0x43826d1e,0xe718,0x42ee,"bc55a1e261c37bfe")
#define IS_ISHELLITEMARR(g) guid_is(g,0xb63ea76d,0x1f85,0x456f,"a19c48159efa858b")
#define IS_IMODALWINDOW(g) guid_is(g,0xb4db1657,0x70d7,0x485e,"8e3e6fcb5a5c1802")
#define IS_IFILEDIALOG(g)  guid_is(g,0x42f85136,0xdb7e,0x439c,"85f1e4075d135fc8")
#define IS_IFILESAVE(g)    guid_is(g,0x84bccd23,0x5fde,0x4cdb,"aea4af64b83d78ab")
#define IS_IFILEOPEN(g)    guid_is(g,0xd57c7288,0xd4ad,0x4768,"be029d969532d960")
#define IS_CLSID_FILEOPEN(g) guid_is(g,0xdc1c5a9c,0xe88a,0x4dde,"a5a160f82a20aef7")
#define IS_CLSID_FILESAVE(g) guid_is(g,0xc0b4e2f3,0xba21,0x4773,"8dba335ec946eb8b")
static u16 *co_wdup(const u16 *s){ int n=wlen(s); u16 *d=y_CoTaskMemAlloc((u64)(n+1)*2); if(d){ memcpy(d,s,(u64)n*2); d[n]=0; } return d; }
static void wcpy(u16 *d, const u16 *s, int max){ int i=0; for(;s && s[i] && i<max-1;i++) d[i]=s[i]; d[i]=0; }

/* ---- IShellItem: a file or folder by its path ---- */
struct sitem { void **vt; long refs; u16 path[260]; };
static void *sitem_vt[8];
static struct sitem *sitem_new(const u16 *path);
static long WINAPI si_QueryInterface(struct sitem *s, const u8 *iid, void **out){
  if(!out) return E_POINTER_; if(IS_IUNKNOWN(iid) || IS_ISHELLITEM(iid)){ s->refs++; *out=s; return S_OK_; } *out=0; return E_NOINTERFACE_; }
static u32 WINAPI si_AddRef(struct sitem *s){ return (u32)++s->refs; }
static u32 WINAPI si_Release(struct sitem *s){ long r=--s->refs; if(r<=0){ heap_free(s); return 0; } return (u32)r; }
static long WINAPI si_BindToHandler(struct sitem *s, void *bc, const u8 *h, const u8 *iid, void **out){ (void)s;(void)bc;(void)h;(void)iid; if(out) *out=0; return E_NOTIMPL_; }
static long WINAPI si_GetParent(struct sitem *s, void **out){
  if(!out) return E_POINTER_; u16 p[260]; wcpy(p,s->path,260); int n=wlen(p); while(n>0 && p[n-1]!='\\' && p[n-1]!='/') n--;
  if(n<=3){ *out=0; return (long)0x80004005; } p[n-1]=0; *out=sitem_new(p); return *out? S_OK_ : (long)0x8007000E; }
static long WINAPI si_GetDisplayName(struct sitem *s, u32 sigdn, u16 **out){
  if(!out) return E_POINTER_;
  if(sigdn==0 || sigdn==0x80018001){ const u16 *b=s->path; for(const u16 *q=s->path;*q;q++) if(*q=='\\' || *q=='/') b=q+1; *out=co_wdup(*b? b : s->path); }   /* NORMALDISPLAY, PARENTRELATIVEPARSING: the leaf */
  else *out=co_wdup(s->path);                                                                   /* FILESYSPATH, DESKTOPABSOLUTEPARSING, ... */
  return *out? S_OK_ : (long)0x8007000E;
}
static long WINAPI si_GetAttributes(struct sitem *s, u32 mask, u32 *attr){
  char a[260]; w2a(s->path,a,260); u32 v=0x40000000u | (fs_isdir(a)? 0x20000000u : 0);      /* SFGAO_FILESYSTEM, SFGAO_FOLDER */
  if(attr) *attr=v&mask; return (v&mask)==mask? S_OK_ : 1; }
static long WINAPI si_Compare(struct sitem *s, struct sitem *o, u32 hint, int *order){
  (void)hint; char a[260], b[260]; w2a(s->path,a,260); w2a(o? o->path : (const u16 *)u"",b,260); int r=stricmp(a,b); if(order) *order=r; return r? 1 : S_OK_; }
static struct sitem *sitem_new(const u16 *path){
  if(!sitem_vt[0]){ void *v[8]={(void*)si_QueryInterface,(void*)si_AddRef,(void*)si_Release,(void*)si_BindToHandler,(void*)si_GetParent,(void*)si_GetDisplayName,(void*)si_GetAttributes,(void*)si_Compare}; memcpy(sitem_vt,v,sizeof v); }
  struct sitem *s=heap_alloc_prog(sizeof *s); if(!s) return 0; s->vt=sitem_vt; s->refs=1; wcpy(s->path,path,260); return s;
}
static long WINAPI y_SHCreateItemFromParsingName(const u16 *path, void *bc, const u8 *iid, void **out){
  (void)bc; if(!out) return E_POINTER_; *out=0; if(!path) return (long)0x80070057;
  char a[260]; w2a(path,a,260); if(!fs_find(a) && !fs_isdir(a)) return (long)0x80070002;     /* HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) */
  struct sitem *s=sitem_new(path); if(!s) return (long)0x8007000E;
  long r=si_QueryInterface(s,iid,out); si_Release(s); return r;
}

/* ---- IShellItemArray with one item (IFileOpenDialog::GetResults) ---- */
struct sarr { void **vt; long refs; struct sitem *item; };
static long WINAPI sa_QueryInterface(struct sarr *a, const u8 *iid, void **out){
  if(!out) return E_POINTER_; if(IS_IUNKNOWN(iid) || IS_ISHELLITEMARR(iid)){ a->refs++; *out=a; return S_OK_; } *out=0; return E_NOINTERFACE_; }
static u32 WINAPI sa_AddRef(struct sarr *a){ return (u32)++a->refs; }
static u32 WINAPI sa_Release(struct sarr *a){ long r=--a->refs; if(r<=0){ si_Release(a->item); heap_free(a); return 0; } return (u32)r; }
static long WINAPI sa_NotImpl(void){ return E_NOTIMPL_; }
static long WINAPI sa_GetCount(struct sarr *a, u32 *n){ (void)a; if(n) *n=1; return S_OK_; }
static long WINAPI sa_GetItemAt(struct sarr *a, u32 i, void **out){ if(!out) return E_POINTER_; if(i){ *out=0; return (long)0x80070057; } a->item->refs++; *out=a->item; return S_OK_; }
static void *sarr_vt[10];

/* ---- IFileOpenDialog / IFileSaveDialog ---- */
struct fdlg { void **vt; long refs; int save; u32 options, typeindex; u16 name[260], folder[260], title[128], defext[16], result[260]; int has_result; };
static void *fdlg_vt[32];
static long WINAPI fd_QueryInterface(struct fdlg *d, const u8 *iid, void **out){
  if(!out) return E_POINTER_;
  if(IS_IUNKNOWN(iid) || IS_IMODALWINDOW(iid) || IS_IFILEDIALOG(iid) || (d->save && IS_IFILESAVE(iid)) || (!d->save && IS_IFILEOPEN(iid))){ d->refs++; *out=d; return S_OK_; }
  *out=0; return E_NOINTERFACE_;                                                               /* IFileDialogCustomize etc.: optional, callers cope */
}
static u32 WINAPI fd_AddRef(struct fdlg *d){ return (u32)++d->refs; }
static u32 WINAPI fd_Release(struct fdlg *d){ long r=--d->refs; if(r<=0){ heap_free(d); return 0; } return (u32)r; }
static long WINAPI fd_Show(struct fdlg *d, HANDLE owner){
  char file[260]="", dir[260]="", title[128]="", ext[16]=""; w2a(d->name,file,260); w2a(d->folder,dir,260); w2a(d->title,title,128); w2a(d->defext,ext,16);
  if(!dir[0]){ u16 *p=0; static const u8 downloads[16]={0x90,0xe2,0x4d,0x37,0x3f,0x12,0x65,0x45,0x91,0x64,0x39,0xc4,0x92,0x5e,0x46,0x7b};   /* FOLDERID_Downloads */
    if(d->save && y_SHGetKnownFolderPath(downloads,0,0,&p)==0 && p){ w2a(p,dir,260); y_CoTaskMemFree(p); } }
  u8 ofn[152]; memset(ofn,0,sizeof ofn); *(u32 *)ofn=152; *(u64 *)(ofn+8)=owner;
  *(char **)(ofn+48)=file; *(u32 *)(ofn+56)=260; *(char **)(ofn+80)= dir[0]? dir : 0; *(char **)(ofn+88)= title[0]? title : 0; *(char **)(ofn+104)= ext[0]? ext : 0;
  i32 (WINAPI *f)(u8 *)=win32_resolve("comdlg32", d->save? "GetSaveFileNameA" : "GetOpenFileNameA");
  if(!f || !f(ofn)) return (long)0x800704C7;                                                    /* HRESULT_FROM_WIN32(ERROR_CANCELLED) */
  int dot=0; for(const char *q=file;*q;q++){ if(*q=='.') dot=1; if(*q=='\\' || *q=='/') dot=0; }
  if(d->save && ext[0] && !dot && strlen(file)+strlen(ext)+2<260){ cat_(file,"."); cat_(file,ext); }
  char full[260]; if(file[1]==':') memcpy(full,file,strlen(file)+1); else { BOOL (WINAPI *gf)(const char*,DWORD,char*,char**)=K32("GetFullPathNameA"); if(!gf || !gf(file,260,full,0)) memcpy(full,file,strlen(file)+1); }
  a2w(full,d->result,260); d->has_result=1; return S_OK_;
}
static long WINAPI fd_SetFileTypes(struct fdlg *d, u32 n, const void *specs){ (void)d;(void)n;(void)specs; return S_OK_; }
static long WINAPI fd_SetFileTypeIndex(struct fdlg *d, u32 i){ d->typeindex=i; return S_OK_; }
static long WINAPI fd_GetFileTypeIndex(struct fdlg *d, u32 *i){ if(!i) return E_POINTER_; *i= d->typeindex? d->typeindex : 1; return S_OK_; }
static long WINAPI fd_Advise(struct fdlg *d, void *sink, u32 *cookie){ (void)d;(void)sink; if(cookie) *cookie=1; return S_OK_; }
static long WINAPI fd_Unadvise(struct fdlg *d, u32 cookie){ (void)d;(void)cookie; return S_OK_; }
static long WINAPI fd_SetOptions(struct fdlg *d, u32 o){ d->options=o; return S_OK_; }
static long WINAPI fd_GetOptions(struct fdlg *d, u32 *o){ if(!o) return E_POINTER_; *o=d->options; return S_OK_; }
static long WINAPI fd_SetFolder(struct fdlg *d, struct sitem *si){                            /* SetDefaultFolder and SetFolder */
  if(!si) return E_POINTER_; u16 *p=0; long (WINAPI *gdn)(void*,u32,u16**)=(long (WINAPI *)(void*,u32,u16**))si->vt[5];
  if(gdn(si,0x80058000u,&p)==0 && p){ wcpy(d->folder,p,260); y_CoTaskMemFree(p); } return S_OK_; }
static long WINAPI fd_GetFolder(struct fdlg *d, void **out){
  if(!out) return E_POINTER_; *out=sitem_new(d->folder[0]? d->folder : (const u16 *)u"C:\\Documents"); return *out? S_OK_ : (long)0x8007000E; }
static long WINAPI fd_GetCurrentSelection(struct fdlg *d, void **out){ (void)d; if(out) *out=0; return E_NOTIMPL_; }
static long WINAPI fd_SetFileName(struct fdlg *d, const u16 *n){ wcpy(d->name,n,260); return S_OK_; }
static long WINAPI fd_GetFileName(struct fdlg *d, u16 **out){ if(!out) return E_POINTER_; *out=co_wdup(d->name); return *out? S_OK_ : (long)0x8007000E; }
static long WINAPI fd_SetTitle(struct fdlg *d, const u16 *t){ wcpy(d->title,t,128); return S_OK_; }
static long WINAPI fd_SetLabel(struct fdlg *d, const u16 *t){ (void)d;(void)t; return S_OK_; }   /* SetOkButtonLabel, SetFileNameLabel */
static long WINAPI fd_GetResult(struct fdlg *d, void **out){
  if(!out) return E_POINTER_; if(!d->has_result){ *out=0; return E_UNEXPECTED_; } *out=sitem_new(d->result); return *out? S_OK_ : (long)0x8007000E; }
static long WINAPI fd_AddPlace(struct fdlg *d, void *si, u32 where){ (void)d;(void)si;(void)where; return S_OK_; }
static long WINAPI fd_SetDefaultExtension(struct fdlg *d, const u16 *e){ wcpy(d->defext,e,16); return S_OK_; }
static long WINAPI fd_Close(struct fdlg *d, long hr){ (void)d;(void)hr; return S_OK_; }
static long WINAPI fd_SetClientGuid(struct fdlg *d, const u8 *g){ (void)d;(void)g; return S_OK_; }
static long WINAPI fd_ClearClientData(struct fdlg *d){ (void)d; return S_OK_; }
static long WINAPI fd_SetFilter(struct fdlg *d, void *f){ (void)d;(void)f; return S_OK_; }
static long WINAPI fd_Slot27(struct fdlg *d, void **out){                                     /* save: SetSaveAsItem(IShellItem*); open: GetResults(IShellItemArray**) */
  if(d->save){ struct sitem *si=(struct sitem *)out; if(si){ u16 *p=0; long (WINAPI *gdn)(void*,u32,u16**)=(long (WINAPI *)(void*,u32,u16**))si->vt[5];
      if(gdn(si,0x80058000u,&p)==0 && p){ const u16 *b=p; for(const u16 *q=p;*q;q++) if(*q=='\\') b=q+1; wcpy(d->name,b,260); y_CoTaskMemFree(p); } } return S_OK_; }
  if(!out) return E_POINTER_; if(!d->has_result){ *out=0; return E_UNEXPECTED_; }
  struct sarr *a=heap_alloc_prog(sizeof *a); if(!a) return (long)0x8007000E; a->vt=sarr_vt; a->refs=1; a->item=sitem_new(d->result); if(!a->item){ heap_free(a); return (long)0x8007000E; }
  *out=a; return S_OK_;
}
static long WINAPI fd_Slot28(struct fdlg *d, void *a){ (void)d; if(!d->save && a) *(void **)a=0; return d->save? S_OK_ : E_NOTIMPL_; }   /* SetProperties / GetSelectedItems */
static long WINAPI fd_Props(struct fdlg *d, void *a, BOOL b){ (void)d;(void)a;(void)b; return S_OK_; }
static long WINAPI fd_GetProperties(struct fdlg *d, void **out){ (void)d; if(out) *out=0; return E_NOTIMPL_; }
static long WINAPI fd_ApplyProperties(struct fdlg *d, void *a, void *b, HANDLE c, void *e){ (void)d;(void)a;(void)b;(void)c;(void)e; return S_OK_; }
/* CoCreateInstance for the classes Willows implements itself; REGDB_E_CLASSNOTREG for the rest */
long com_create_builtin(const u8 *clsid, const u8 *iid, void **out){
  if(!out) return E_POINTER_; *out=0;
  int save=IS_CLSID_FILESAVE(clsid); if(!save && !IS_CLSID_FILEOPEN(clsid)) return (long)0x80040154;
  if(!fdlg_vt[0]){
    void *v[32]={(void*)fd_QueryInterface,(void*)fd_AddRef,(void*)fd_Release,(void*)fd_Show,(void*)fd_SetFileTypes,(void*)fd_SetFileTypeIndex,(void*)fd_GetFileTypeIndex,
      (void*)fd_Advise,(void*)fd_Unadvise,(void*)fd_SetOptions,(void*)fd_GetOptions,(void*)fd_SetFolder,(void*)fd_SetFolder,(void*)fd_GetFolder,(void*)fd_GetCurrentSelection,
      (void*)fd_SetFileName,(void*)fd_GetFileName,(void*)fd_SetTitle,(void*)fd_SetLabel,(void*)fd_SetLabel,(void*)fd_GetResult,(void*)fd_AddPlace,(void*)fd_SetDefaultExtension,
      (void*)fd_Close,(void*)fd_SetClientGuid,(void*)fd_ClearClientData,(void*)fd_SetFilter,(void*)fd_Slot27,(void*)fd_Slot28,(void*)fd_Props,(void*)fd_GetProperties,(void*)fd_ApplyProperties};
    memcpy(fdlg_vt,v,sizeof v);
    void *a[10]={(void*)sa_QueryInterface,(void*)sa_AddRef,(void*)sa_Release,(void*)sa_NotImpl,(void*)sa_NotImpl,(void*)sa_NotImpl,(void*)sa_NotImpl,(void*)sa_GetCount,(void*)sa_GetItemAt,(void*)sa_NotImpl};
    memcpy(sarr_vt,a,sizeof a);
  }
  struct fdlg *d=heap_alloc_prog(sizeof *d); if(!d) return (long)0x8007000E; memset(d,0,sizeof *d); d->vt=fdlg_vt; d->refs=1; d->save=save;
  long r=fd_QueryInterface(d,iid,out); fd_Release(d); return r;
}
static long WINAPI y_CoCreateInstanceEx(const u8 *c, void *o, DWORD ctx, void *s, DWORD n, u8 *q){   /* MULTI_QI: {const IID*, IUnknown*, HRESULT} */
  (void)o;(void)ctx;(void)s; long any=(long)0x80040154;
  for(DWORD i=0;i<n && q;i++){ u8 *m=q+24*i; void *p=0; long r=com_create_builtin(c,*(const u8 **)m,&p); *(void **)(m+8)=p; *(long *)(m+16)=r; if(r==0) any=0; }
  return any;
}
const struct export y_exports[] = {
  YD("combase",WindowsCreateString), YD("combase",WindowsCreateStringReference), YD("combase",WindowsDeleteString), YD("combase",WindowsGetStringRawBuffer),
  YD("combase",WindowsGetStringLen), YD("combase",WindowsIsStringEmpty), YD("combase",WindowsDuplicateString), YD("combase",RoInitialize), YD("combase",RoUninitialize),
  YD("combase",RoGetActivationFactory), YD("combase",RoActivateInstance),
  YD("user32",CreateWindowStationW), YD("user32",OpenWindowStationW), YD("user32",GetProcessWindowStation), YD("user32",SetProcessWindowStation), YD("user32",CloseWindowStation),
  YD("user32",CreateDesktopW), YD("user32",OpenDesktopW), YD("user32",OpenInputDesktop), YD("user32",GetThreadDesktop), YD("user32",SetThreadDesktop), YD("user32",CloseDesktop), YD("user32",SwitchDesktop),
  Y(InitializeProcThreadAttributeList),Y(UpdateProcThreadAttribute), Y(DeleteProcThreadAttributeList), YD("advapi32",CreateProcessAsUserW), Y(CreateProcessAsUserW),
  Y(GetSystemTimePreciseAsFileTime),Y(QueryInterruptTime), {"kernel32","QueryInterruptTimePrecise",y_QueryInterruptTime}, Y(QueryUnbiasedInterruptTime),
  {"kernel32","QueryUnbiasedInterruptTimePrecise",y_QueryUnbiasedInterruptTime}, YD("ntdll",RtlCompareMemory), {"kernel32","RtlCompareMemory",y_RtlCompareMemory},
  YD("ole32",CoIncrementMTAUsage), YD("ole32",CoDecrementMTAUsage), Y(IsWow64Process2), Y(GetDynamicTimeZoneInformation), Y(GetTimeZoneInformationForYear), Y(SystemTimeToTzSpecificLocalTimeEx),
  Y(TzSpecificLocalTimeToSystemTime), {"kernel32","TzSpecificLocalTimeToSystemTimeEx",y_TzSpecificLocalTimeToSystemTime}, YD("advapi32",EnumDynamicTimeZoneInformation),
  YD("setupapi",SetupDiGetClassDevsW), YD("setupapi",SetupDiGetClassDevsA), YD("setupapi",SetupDiEnumDeviceInfo), YD("setupapi",SetupDiEnumDeviceInterfaces), YD("setupapi",SetupDiDestroyDeviceInfoList),
  YD("winmm",timeGetTime),YD("winmm",timeBeginPeriod), {"winmm","timeEndPeriod",y_timeBeginPeriod}, YD("winmm",timeGetDevCaps), Y(GetShortPathNameW),
      {"kernel32","WaitOnAddress",y_WaitOnAddress}, {"api-ms-win-core-synch-l1-2-0","WaitOnAddress",y_WaitOnAddress}, {"kernel32","WakeByAddressSingle",y_WakeByAddressSingle}, {"kernel32","WakeByAddressAll",y_WakeByAddressAll},
  Y(InitializeSListHead), Y(InterlockedPushEntrySList), Y(InterlockedPopEntrySList), Y(InterlockedFlushSList), Y(QueryDepthSList), Y(InterlockedPushListSListEx), Y(SetThreadDescription), Y(SetThreadStackGuarantee), Y(GetCurrentProcessorNumber),
  YD("ole32",CoInitializeEx), YD("ole32",CoInitialize), YD("ole32",CoUninitialize), YD("ole32",CoGetApartmentType), YD("combase",CoGetApartmentType), YD("combase",CoInitializeEx),YD("ole32",CoTaskMemAlloc), YD("ole32",CoTaskMemFree), YD("ole32",CoTaskMemRealloc), YD("ole32",OleInitialize), YD("ole32",OleUninitialize), YD("ole32",CoInitializeSecurity), YD("ole32",CoSetProxyBlanket),
  YD("ole32",CoGetClassObject), YD("ole32",CoCreateInstanceEx), YD("ole32",CoFreeUnusedLibraries), YD("ole32",CoCreateGuid), YD("ole32",StringFromGUID2), YD("ole32",CLSIDFromString), {"ole32","IIDFromString",y_CLSIDFromString}, YD("ole32",StringFromCLSID), {"ole32","StringFromIID",y_StringFromCLSID},
  YD("oleaut32",SysAllocString), YD("oleaut32",SysAllocStringLen), YD("oleaut32",SysFreeString), YD("oleaut32",SysStringLen), YD("oleaut32",SysStringByteLen), YD("oleaut32",SysReAllocString), YD("oleaut32",VariantInit), YD("oleaut32",VariantClear),
  YD("shell32",SHCreateItemFromParsingName), YD("shell32",SHGetKnownFolderPath), YD("shell32",SHGetFolderPathW), YD("shell32",SHGetFolderPathA), YD("shell32",SHGetSpecialFolderPathW), YD("shell32",SHGetSpecialFolderPathA), YD("shell32",SHCreateDirectoryExW), YD("shell32",SHCreateDirectoryExA),
  YD("shell32",ShellExecuteW), YD("shell32",ShellExecuteExA), YD("shell32",ShellExecuteExW), YD("shell32",CommandLineToArgvW),
  YD("shlwapi",PathFileExistsA), YD("shlwapi",PathFileExistsW), YD("shlwapi",PathIsDirectoryA), YD("shlwapi",PathIsDirectoryW), YD("shlwapi",PathFindFileNameA), YD("shlwapi",PathFindFileNameW), YD("shlwapi",PathFindExtensionA), YD("shlwapi",PathFindExtensionW),
  YD("shlwapi",PathRemoveFileSpecA), YD("shlwapi",PathRemoveFileSpecW), YD("shlwapi",PathIsRelativeA), YD("shlwapi",PathIsRelativeW), YD("shlwapi",PathAppendA), YD("shlwapi",PathAppendW), YD("shlwapi",PathCombineA), YD("shlwapi",PathCombineW),
  YD("shlwapi",PathStripPathA), YD("shlwapi",PathStripPathW), YD("shlwapi",PathRemoveExtensionW), YD("shlwapi",PathAddBackslashW), YD("shlwapi",StrStrIW), YD("shlwapi",StrStrW), YD("shlwapi",StrCmpIW), YD("shlwapi",StrStrIA), YD("shlwapi",StrStrA), Y(SetConsoleOutputCP),
  Y(CreateProcessW), Y(GetProcessId), Y(SetPriorityClass), Y(GetPriorityClass), Y(WaitForInputIdle), YD("user32",WaitForInputIdle),
  YP(EnumProcesses), YP(GetProcessMemoryInfo), YP(EnumProcessModules), YP(GetModuleBaseNameW), YP(GetModuleBaseNameA), YP(GetModuleFileNameExW), YP(GetModuleFileNameExA),
  {"kernel32","K32EnumProcesses",y_EnumProcesses}, {"kernel32","K32GetProcessMemoryInfo",y_GetProcessMemoryInfo}, {"kernel32","K32EnumProcessModules",y_EnumProcessModules}, {"kernel32","K32GetModuleBaseNameW",y_GetModuleBaseNameW},
  {"kernel32","K32GetModuleBaseNameA",y_GetModuleBaseNameA}, {"kernel32","K32GetModuleFileNameExW",y_GetModuleFileNameExW}, {"kernel32","K32GetModuleFileNameExA",y_GetModuleFileNameExA}, Y(QueryFullProcessImageNameW),
  Y(CreateJobObjectW), Y(CreateJobObjectA), Y(AssignProcessToJobObject), Y(SetInformationJobObject), Y(QueryInformationJobObject), Y(TerminateJobObject), Y(IsProcessInJob),
};
const unsigned y_nexports = sizeof y_exports / sizeof *y_exports;
