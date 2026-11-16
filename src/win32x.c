#include "kernel.h"
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE; typedef void *LPVOID;
/* More Win32: wide-char variants, locale/codepage, environment, events/mutexes, version info, registry (empty),
   memory (Local/Global), time conversion, misc. Everything here is a small, honest implementation or a safe default. */
static u64 dummy_teb_x[0x2000/8];
#define last_error (*(u32*)((u8*)(cur_teb? cur_teb : dummy_teb_x)+0x68))     /* the running thread's last error */
#define X(n) {"kernel32", #n, x_##n}
#define XD(d,n) {d, #n, x_##n}

static int wlen(const u16 *s){ int n=0; while(s&&s[n]) n++; return n; }
static void w2a(const u16 *w, char *a, int n){ int i=0; for(;w&&w[i]&&i<n-1;i++) a[i]= w[i]<0x80? (char)w[i] : '?'; a[i]=0; }
static int a2w(const char *a, u16 *w, int n){ int i=0; for(;a[i]&&i<n-1;i++) w[i]=(u8)a[i]; w[i]=0; return i; }
static void *K32(const char *n){ return win32_resolve("kernel32",n); }
static char *strcpy_(char *d, const char *s){ char *r=d; while((*d++=*s++)); return r; }
static int kvsnprintf_s(char *b, unsigned n, DWORD id){ char t[24]; int l=0; DWORD v=id; do { t[l++]=(char)(0x30+v%10); v/=10; } while(v); char o[48]; int k=0; const char *pre="Error "; while(*pre) o[k++]=*pre++; while(l) o[k++]=t[--l]; o[k++]='.'; o[k]=0; if((unsigned)k+1>n) k=(int)n-1; memcpy(b,o,k); b[k]=0; return k; }
static int up(int c){ return c>='a'&&c<='z'? c-32 : c; }
static int lo(int c){ return c>='A'&&c<='Z'? c+32 : c; }

/* ---- code pages / locale ---- */
static BOOL WINAPI x_IsValidCodePage(u32 cp){ return cp==1252||cp==437||cp==65001||cp==850||cp==28591||cp==20127; }
static u32 WINAPI x_GetOEMCP(void){ return 437; }
static u32 WINAPI x_GetConsoleCP(void){ return 437; }
static BOOL WINAPI x_SetConsoleCP(u32 cp){ (void)cp; return 1; }
static BOOL WINAPI x_GetCPInfo(u32 cp, u8 *info){ (void)cp; memset(info,0,20); *(u32*)info= cp==65001? 4 : 1; info[4]='?'; return 1; }
static u32 WINAPI x_GetUserDefaultLCID(void){ return 0x409; }
static u16 WINAPI x_GetUserDefaultLangID(void){ return 0x409; }
static u32 WINAPI x_GetThreadLocale(void){ return 0x409; }
static BOOL WINAPI x_IsValidLocale(u32 l, DWORD f){ (void)l;(void)f; return 1; }
static int WINAPI x_GetUserDefaultLocaleName(u16 *b, int n){ static const char s[]="en-US"; if(n<6) return 0; a2w(s,b,n); return 6; }
static int WINAPI x_GetLocaleInfoA(u32 l, u32 t, char *b, int n){
  (void)l; const char *s=""; switch(t&0xFFFF){ case 0x0F: s="."; break; case 0x0E: s=","; break; case 0x1: s="English"; break; case 0x59: s="en"; break; case 0x5A: s="US"; break; case 0x4: s="English (United States)"; break; default: s="0"; }
  int len=(int)strlen(s)+1; if(!n) return len; if(n<len) return 0; memcpy(b,s,len); return len;
}
static int WINAPI x_GetLocaleInfoW(u32 l, u32 t, u16 *b, int n){ char tmp[64]; int r=x_GetLocaleInfoA(l,t,tmp,n? (n<64?n:64) : 0); if(!n||!r) return r; a2w(tmp,b,n); return r; }
static BOOL WINAPI x_GetStringTypeW(DWORD t, const u16 *s, int n, u16 *out){
  (void)t; if(n<0) n=wlen(s)+1;
  for(int i=0;i<n;i++){ u16 c=s[i],f=0; if(c>='A'&&c<='Z') f=0x101; else if(c>='a'&&c<='z') f=0x102; else if(c>='0'&&c<='9') f=0x4|0x80; else if(c==' '||(c>=9&&c<=13)) f=0x8|0x40; else if(c>32&&c<127) f=0x10; else if(c<32) f=0x20; if(c>='a'&&c<='f'||c>='A'&&c<='F') f|=0x80; if(c>=0x80) f=0x100; out[i]=f; }
  return 1;
}
static BOOL WINAPI x_GetStringTypeA(u32 l, DWORD t, const char *s, int n, u16 *out){ (void)l; u16 w[256]; if(n<0) n=(int)strlen(s)+1; if(n>256) n=256; for(int i=0;i<n;i++) w[i]=(u8)s[i]; return x_GetStringTypeW(t,w,n,out); }
static int WINAPI x_LCMapStringW(u32 l, DWORD f, const u16 *s, int n, u16 *d, int dn){
  (void)l; if(n<0) n=wlen(s)+1; if(!dn) return n; if(dn<n) return 0;
  for(int i=0;i<n;i++){ u16 c=s[i]; if((f&0x200)&&c<128) c=(u16)up(c); else if((f&0x100)&&c<128) c=(u16)lo(c); d[i]=c; } return n;
}
static int WINAPI x_LCMapStringA(u32 l, DWORD f, const char *s, int n, char *d, int dn){
  (void)l; if(n<0) n=(int)strlen(s)+1; if(!dn) return n; if(dn<n) return 0;
  for(int i=0;i<n;i++){ int c=(u8)s[i]; if(f&0x200) c=up(c); else if(f&0x100) c=lo(c); d[i]=(char)c; } return n;
}
static int cmp_core(const u16 *a,int an,const u16 *b,int bn,int ic){
  if(an<0) an=wlen(a); if(bn<0) bn=wlen(b); int i=0;
  for(;i<an&&i<bn;i++){ int x=a[i],y=b[i]; if(ic){ x=up(x); y=up(y);} if(x!=y) return x<y?1:3; }
  return an==bn? 2 : an<bn? 1 : 3;
}
static int WINAPI x_CompareStringW(u32 l, DWORD f, const u16 *a, int an, const u16 *b, int bn){ (void)l; return cmp_core(a,an,b,bn,f&1); }
static int WINAPI x_CompareStringA(u32 l, DWORD f, const char *a, int an, const char *b, int bn){
  (void)l; u16 x[260],y[260]; if(an<0) an=(int)strlen(a); if(bn<0) bn=(int)strlen(b); if(an>259) an=259; if(bn>259) bn=259;
  for(int i=0;i<an;i++) x[i]=(u8)a[i]; for(int i=0;i<bn;i++) y[i]=(u8)b[i]; return cmp_core(x,an,y,bn,f&1);
}

/* ---- strings ---- */
static int WINAPI x_lstrlenW(const u16 *s){ return wlen(s); }
static u16 *WINAPI x_lstrcpyW(u16 *d, const u16 *s){ int i=0; do d[i]=s[i]; while(s[i++]); return d; }
static u16 *WINAPI x_lstrcpynW(u16 *d, const u16 *s, int n){ int i=0; for(;i<n-1&&s[i];i++) d[i]=s[i]; if(n>0) d[i]=0; return d; }
static char *WINAPI x_lstrcpynA(char *d, const char *s, int n){ int i=0; for(;i<n-1&&s[i];i++) d[i]=s[i]; if(n>0) d[i]=0; return d; }
static char *WINAPI x_lstrcatA(char *d, const char *s){ char *e=d+strlen(d); while((*e++=*s++)); return d; }
static u16 *WINAPI x_lstrcatW(u16 *d, const u16 *s){ x_lstrcpyW(d+wlen(d),s); return d; }
static int WINAPI x_lstrcmpiA(const char *a, const char *b){ return stricmp(a,b); }
static int WINAPI x_lstrcmpW(const u16 *a, const u16 *b){ return cmp_core(a,-1,b,-1,0)-2; }
static int WINAPI x_lstrcmpiW(const u16 *a, const u16 *b){ return cmp_core(a,-1,b,-1,1)-2; }
static char *WINAPI x_CharUpperA(char *s){ if((u64)s<0x10000) return (char*)(u64)up((int)(u64)s); for(char *p=s;*p;p++) *p=(char)up(*p); return s; }
static char *WINAPI x_CharLowerA(char *s){ if((u64)s<0x10000) return (char*)(u64)lo((int)(u64)s); for(char *p=s;*p;p++) *p=(char)lo(*p); return s; }
static u16 *WINAPI x_CharUpperW(u16 *s){ if((u64)s<0x10000) return (u16*)(u64)up((int)(u64)s); for(u16 *p=s;*p;p++) *p=(u16)up(*p); return s; }
static u16 *WINAPI x_CharLowerW(u16 *s){ if((u64)s<0x10000) return (u16*)(u64)lo((int)(u64)s); for(u16 *p=s;*p;p++) *p=(u16)lo(*p); return s; }
static char *WINAPI x_CharNextA(char *s){ return *s? s+1 : s; }
static u16 *WINAPI x_CharNextW(u16 *s){ return *s? s+1 : s; }
static BOOL WINAPI x_IsCharAlphaA(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
static int WINAPI x_wsprintfA(char *b, const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(b,1024,f,ap); __builtin_ms_va_end(ap); return r; }
static int WINAPI x_wvsprintfA(char *b, const char *f, margs ap){ return kvsnprintf(b,1024,f,ap); }
static int WINAPI x_wsprintfW(u16 *b, const u16 *f, ...){ char fa[256],t[1024]; w2a(f,fa,256); margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(t,1024,fa,ap); __builtin_ms_va_end(ap); a2w(t,b,1024); return r; }

/* ---- paths, files (wide forms forward to the ANSI implementations) ---- */
typedef DWORD (WINAPI *f_gmfn)(HANDLE,char*,DWORD);
static DWORD WINAPI x_GetModuleFileNameW(HANDLE m, u16 *b, DWORD n){ char t[260]; f_gmfn f=K32("GetModuleFileNameA"); DWORD r=f? f(m,t,260):0; if(!n) return r; a2w(t,b,(int)n); return (DWORD)strlen(t); }
typedef DWORD (WINAPI *f_gcd)(DWORD,char*);
static DWORD WINAPI x_GetCurrentDirectoryW(DWORD n, u16 *b){ char t[260]; f_gcd f=K32("GetCurrentDirectoryA"); if(!f) return 0; DWORD r=f(260,t); if(!r) return 0; if(r+1>n) return r+1; a2w(t,b,(int)n); return r; }
typedef BOOL (WINAPI *f_pa)(const char*);
static BOOL WINAPI x_SetCurrentDirectoryW(const u16 *p){ char t[260]; w2a(p,t,260); f_pa f=K32("SetCurrentDirectoryA"); return f? f(t):0; }
static BOOL WINAPI x_DeleteFileW(const u16 *p){ char t[260]; w2a(p,t,260); f_pa f=K32("DeleteFileA"); return f? f(t):0; }
static int file_trace(void){ struct ramfile *f=fs_find("/System/apitrace.cfg"); return f && !f->isdir; }      /* log file calls while tracing */
static DWORD WINAPI x_GetFileAttributesW(const u16 *p){ char t[260]; w2a(p,t,260); DWORD (WINAPI *f)(const char*)=K32("GetFileAttributesA"); DWORD r= f? f(t):(DWORD)-1;
  if(file_trace()) kdbg("  [file] GetFileAttributes(%s) = %x\n",t,r); return r; }
static BOOL WINAPI x_CreateDirectoryW(const u16 *p, void *sa){ char t[260]; w2a(p,t,260); BOOL (WINAPI *f)(const char*,void*)=K32("CreateDirectoryA"); BOOL r= f? f(t,sa):0;
  if(file_trace()) kdbg("  [file] CreateDirectory(%s) = %d (error %u)\n",t,r,last_error); return r; }
static HANDLE WINAPI x_CreateFileW(const u16 *p, DWORD a, DWORD sh, void *sa, DWORD cd, DWORD fl, HANDLE tm){
  char t[260]; w2a(p,t,260); HANDLE (WINAPI *f)(const char*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE)=K32("CreateFileA"); HANDLE r= f? f(t,a,sh,sa,cd,fl,tm) : (HANDLE)(i64)-1;
  if(file_trace()) kdbg("  [file] CreateFile(%s, access %x, share %x, disp %u, flags %x) = %p (error %u)\n",t,a,sh,cd,fl,(void*)r,last_error); return r;
}
static DWORD WINAPI x_GetFullPathNameW(const u16 *p, DWORD n, u16 *b, u16 **fp){
  char t[260],o[300]; w2a(p,t,260); DWORD (WINAPI *f)(const char*,DWORD,char*,char**)=K32("GetFullPathNameA"); if(!f) return 0; char *leaf=0; DWORD r=f(t,300,o,&leaf);
  if(!r||r+1>n) return r? r+1 : 0; a2w(o,b,(int)n); if(fp) *fp= leaf? b+(leaf-o) : 0; return r;
}
static void ensure_temp(void){ if(!fs_isdir("C:\\Temp")) fs_mkdir("C:\\Temp"); }
static DWORD WINAPI x_GetTempPathA(DWORD n, char *b){ const char *s="C:\\Temp\\"; if(n<10) return 10; ensure_temp(); strcpy_(b,s); return 8; }
static DWORD WINAPI x_GetTempPathW(DWORD n, u16 *b){ if(n<10) return 10; ensure_temp(); a2w("C:\\Temp\\",b,(int)n); return 8; }
static DWORD WINAPI x_GetWindowsDirectoryA(char *b, DWORD n){ if(n<11) return 11; strcpy_(b,"C:\\Willows"); return 10; }
static DWORD WINAPI x_GetWindowsDirectoryW(u16 *b, DWORD n){ if(n<11) return 11; a2w("C:\\Willows",b,(int)n); return 10; }
static DWORD WINAPI x_GetSystemDirectoryA(char *b, DWORD n){ if(n<18) return 18; strcpy_(b,"C:\\Willows\\System32"); return 18; }
static DWORD WINAPI x_GetSystemDirectoryW(u16 *b, DWORD n){ if(n<19) return 19; a2w("C:\\Willows\\System32",b,(int)n); return 18; }
static DWORD WINAPI x_GetLogicalDrives(void){ return 1<<2; }
static u32 WINAPI x_GetDriveTypeA(const char *p){ (void)p; return 3; }
static u32 WINAPI x_GetDriveTypeW(const u16 *p){ (void)p; return 3; }
static BOOL WINAPI x_GetDiskFreeSpaceExA(const char *p, u64 *a, u64 *t, u64 *f){ (void)p; if(a) *a=48ull<<20; if(t) *t=64ull<<20; if(f) *f=48ull<<20; return 1; }
static BOOL WINAPI x_GetVolumeInformationA(const char *r, char *vn, DWORD vl, DWORD *sn, DWORD *mc, DWORD *fl, char *fs, DWORD fl2){
  (void)r; if(vn&&vl>7) strcpy_(vn,"Willows"); if(sn) *sn=0x57494C4C; if(mc) *mc=255; if(fl) *fl=0x3; if(fs&&fl2>4) strcpy_(fs,"WFS"); return 1;
}
static BOOL WINAPI x_FlushFileBuffers(HANDLE h){ (void)h; return 1; }
static BOOL WINAPI x_SetEndOfFile(HANDLE h){ return fs_is_handle(h)? fs_set_eof(h) : 1; }

/* ---- environment: one per process (as on Windows), copied from the parent or taken from CreateProcess's environment block ---- */
static int strncmp_ci_(const char *a, const char *b, int n){ for(int i=0;i<n;i++){ char x=a[i], y=b[i]; if(x>='a'&&x<='z') x-=32; if(y>='a'&&y<='z') y-=32; if(x!=y) return 1; if(!x) return 0; } return 0; }
static const char *strchr_(const char *s, char c){ for(;*s;s++) if(*s==c) return s; return 0; }
#define NENV 64
static struct penv { char k[NENV][48]; char v[NENV][320]; } penv_[MAXPROC];
static struct penv *myenv(void){ return &penv_[cur? cur->pid : 0]; }
#define envk (myenv()->k)
#define envv (myenv()->v)
static int env_find(const char *k){ for(int i=0;i<NENV;i++) if(envk[i][0]&&!stricmp(envk[i],k)) return i; return -1; }
static void env_set_in(struct penv *e, const char *k, int kl, const char *v){
  int i=-1; for(int j=0;j<NENV;j++) if(e->k[j][0] && (int)strlen(e->k[j])==kl && !strncmp_ci_(e->k[j],k,kl)){ i=j; break; }
  if(i<0) for(int j=0;j<NENV;j++) if(!e->k[j][0]){ i=j; break; } if(i<0) return;
  int n=kl<47? kl : 47; memcpy(e->k[i],k,n); e->k[i][n]=0;
  unsigned j=0; for(;v[j]&&j<319;j++) e->v[i][j]=v[j]; e->v[i][j]=0;
}
void env_reset(int pid){ if(pid>0 && pid<MAXPROC) memset(&penv_[pid],0,sizeof penv_[pid]); }   /* a new process in this slot (CreateProcess then fills it) */
/* a new process: its environment is its parent's, or the block given to CreateProcess ("k=v\0...\0\0", narrow or UTF-16) */
void env_inherit(int child, int parent, const void *block, int wide){
  if(child<=0 || child>=MAXPROC) return; struct penv *e=&penv_[child];
  if(!block){ if(parent>=0 && parent<MAXPROC && parent!=child) *e=penv_[parent]; return; }
  memset(e,0,sizeof *e);
  if(wide){ const u16 *p=block; while(*p){ char t[400]; int n=0; const u16 *q=p; while(*q && n<399){ t[n++]= *q<0x80? (char)*q : '?'; q++; } t[n]=0; while(*q) q++;
      const char *eq= t[0]=='='? 0 : strchr_(t,'='); if(eq) env_set_in(e,t,(int)(eq-t),eq+1); p=q+1; } }
  else { const char *p=block; while(*p){ const char *eq= p[0]=='='? 0 : strchr_(p,'='); if(eq) env_set_in(e,p,(int)(eq-p),eq+1); p+=strlen(p)+1; } }
}
/* Firefox's process sandbox (Chromium's) cannot protect anything on Willows - every program runs in ring 0 in one address space - and
   its launcher patches the child's ntdll, which here is shared kernel code. These documented switches make Firefox start its child
   processes the plain way. A program can still set them itself.
   Networking stays in the main process (MOZ_DISABLE_SOCKET_PROCESS: one process less, and its poll loop busy-waits here). The GPU
   process keeps its sandbox, so its launch fails at once and Firefox composites in the main process: its own GPU process stalls
   during start-up on Willows (not solved yet), and in-process compositing also saves a whole process's memory. */
static const char *sys_default_env(const char *k){
  static const char *const off[]={"MOZ_DISABLE_CONTENT_SANDBOX","MOZ_DISABLE_RDD_SANDBOX","MOZ_DISABLE_UTILITY_SANDBOX",
    "MOZ_DISABLE_GMP_SANDBOX","MOZ_DISABLE_VR_SANDBOX","MOZ_DISABLE_SOCKET_PROCESS"};
  for(unsigned j=0;j<sizeof off/sizeof *off;j++) if(!stricmp(k,off[j])) return "1";
  return !stricmp(k,"TEMP")||!stricmp(k,"TMP")? "C:\\Temp" : !stricmp(k,"SystemRoot")||!stricmp(k,"windir")? "C:\\Willows" : !stricmp(k,"PATH")? "C:\\Willows\\System32" : 0;
}
static DWORD WINAPI x_GetEnvironmentVariableA(const char *k, char *b, DWORD n){
  int i=env_find(k); const char *v= i>=0? envv[i] : sys_default_env(k);
  if(!v){ last_error=203; return 0; } DWORD l=(DWORD)strlen(v); if(l+1>n) return l+1; memcpy(b,v,l+1); return l;
}
static DWORD WINAPI x_GetEnvironmentVariableW(const u16 *k, u16 *b, DWORD n){ char kk[64],t[400]; w2a(k,kk,64); DWORD r=x_GetEnvironmentVariableA(kk,t,400); if(!r||r+1>n) return r; a2w(t,b,(int)n); return r; }
static BOOL WINAPI x_SetEnvironmentVariableA(const char *k, const char *v){
  if(!k || !*k || strchr_(k,'=')){ last_error=87; return 0; }
  if(!v){ int i=env_find(k); if(i>=0) envk[i][0]=0; return 1; }
  struct penv *e=myenv(); int before=env_find(k), full=1; for(int j=0;j<NENV;j++) if(!e->k[j][0]) full=0;
  if(before<0 && full){ last_error=8; return 0; }
  env_set_in(e,k,(int)strlen(k),v); return 1;
}
static BOOL WINAPI x_SetEnvironmentVariableW(const u16 *k, const u16 *v){ char kk[64],vv[400]; w2a(k,kk,64); if(v) w2a(v,vv,400); return x_SetEnvironmentVariableA(kk,v? vv:0); }
/* the whole environment as one block "k=v\0...\0\0" (the program frees it with FreeEnvironmentStrings) */
static void *env_block(int wide){
  struct penv *e=myenv(); u64 n=2; for(int i=0;i<NENV;i++) if(e->k[i][0]) n+=strlen(e->k[i])+strlen(e->v[i])+2;
  u8 *b=heap_alloc_prog(n*(wide? 2 : 1)); if(!b) return 0; u64 o=0;
  for(int i=0;i<NENV;i++) if(e->k[i][0]){ const char *parts[2]={e->k[i],e->v[i]};
    for(int p=0;p<2;p++){ for(const char *c=parts[p];*c;c++){ if(wide) ((u16*)b)[o++]=(u8)*c; else b[o++]=*c; } if(!p){ if(wide) ((u16*)b)[o++]='='; else b[o++]='='; } }
    if(wide) ((u16*)b)[o++]=0; else b[o++]=0; }
  if(wide){ ((u16*)b)[o]=0; ((u16*)b)[o+1]=0; } else { b[o]=0; b[o+1]=0; }
  return b;
}
static void *WINAPI x_GetEnvironmentStringsA(void){ return env_block(0); }
static void *WINAPI x_GetEnvironmentStringsW(void){ return env_block(1); }
static BOOL WINAPI x_FreeEnvironmentStringsA(void *p){ heap_free(p); return 1; }
static DWORD WINAPI x_ExpandEnvironmentStringsA(const char *s, char *d, DWORD n){
  char out[512]; int o=0; for(const char *p=s;*p&&o<500;){
    if(*p=='%'){ const char *e=p+1; while(*e&&*e!='%') e++; if(*e=='%'&&e>p+1){ char k[40]; int kl=(int)(e-p-1); if(kl<40){ memcpy(k,p+1,kl); k[kl]=0; char v[160]; DWORD r=x_GetEnvironmentVariableA(k,v,160); if(r&&r<160){ for(DWORD j=0;j<r&&o<500;j++) out[o++]=v[j]; p=e+1; continue; } } } }
    out[o++]=*p++; } out[o]=0; if((DWORD)o+1>n) return (DWORD)o+1; memcpy(d,out,o+1); return (DWORD)o+1;
}
static DWORD WINAPI x_ExpandEnvironmentStringsW(const u16 *s, u16 *d, DWORD n){ char a[300],t[512]; w2a(s,a,300); DWORD r=x_ExpandEnvironmentStringsA(a,t,512); if(r>n) return r; a2w(t,d,(int)n); return r; }

/* ---- events, mutexes, waiting and threads: see thread.c ---- */
/* ---- memory ---- */
static HANDLE WINAPI x_LocalAlloc(u32 fl, u64 n){ (void)fl; void *p=heap_alloc_prog(n); if(!p){ last_error=8; return 0; } if(fl&0x40) memset(p,0,n); return (HANDLE)p; }
static HANDLE WINAPI x_LocalFree(HANDLE h){ heap_free((void*)h); return 0; }
static HANDLE WINAPI x_LocalReAlloc(HANDLE h, u64 n, u32 fl){ (void)fl; return (HANDLE)heap_realloc((void*)h,n); }
static void *WINAPI x_GlobalLock(HANDLE h){ return (void*)h; }
static BOOL WINAPI x_GlobalUnlock(HANDLE h){ (void)h; return 0; }
static BOOL WINAPI x_HeapSetInformation(HANDLE h, u32 c, void *i, u64 l){ (void)h;(void)c;(void)i;(void)l; return 1; }
static BOOL WINAPI x_HeapValidate(HANDLE h, DWORD f, void *p){ (void)h;(void)f;(void)p; return 1; }
static BOOL WINAPI x_IsBadReadPtr(const void *p, u64 n){ (void)n; return p==0; }

/* ---- modules / debugging / misc ---- */
static HANDLE WINAPI x_LoadLibraryW(const u16 *n){ char t[260]; w2a(n,t,260); HANDLE (WINAPI *f)(const char*)=K32("LoadLibraryA"); return f? f(t):0; }
static HANDLE WINAPI x_LoadLibraryExA(const char *n, HANDLE f, DWORD fl){ (void)f;(void)fl; HANDLE (WINAPI *g)(const char*)=K32("LoadLibraryA"); return g? g(n):0; }
static HANDLE WINAPI x_LoadLibraryExW(const u16 *n, HANDLE f, DWORD fl){ (void)f;(void)fl; return x_LoadLibraryW(n); }
static BOOL WINAPI x_FreeLibrary(HANDLE h){ return dll_freelib(h); }
static BOOL WINAPI x_GetModuleHandleExA(DWORD fl, const char *n, HANDLE *out){
  HANDLE h; if(fl&4) h=dll_module_at((u64)n);                                     /* GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS */
  else { HANDLE (WINAPI *f)(const char*)=K32("GetModuleHandleA"); h= f? f(n) : 0; }
  if(out) *out=h; if(!h) last_error=126; return h!=0;
}
static BOOL WINAPI x_GetModuleHandleExW(DWORD fl, const u16 *n, HANDLE *out){ if(fl&4 || !n) return x_GetModuleHandleExA(fl,(const char*)n,out); char t[260]; w2a(n,t,260); return x_GetModuleHandleExA(fl,t,out); }
static BOOL WINAPI x_DisableThreadLibraryCalls(HANDLE h){ (void)h; return 1; }
static void WINAPI x_OutputDebugStringW(const u16 *s){ (void)s; }
static void WINAPI x_DebugBreak(void){}
static u32 WINAPI x_SetErrorMode(u32 m){ (void)m; return 0; }
static BOOL WINAPI x_IsWow64Process(HANDLE h, BOOL *w){ (void)h; if(w) *w=0; return 1; }
static BOOL WINAPI x_GetSystemPowerStatus(u8 *s){ memset(s,0,12); s[1]=1; s[2]=100; s[3]=0x80; return 1; }
static void WINAPI x_RaiseFailFastException(const u32 *rec, void *ctx, DWORD fl){      /* immediate termination, as on Windows; the log says who asked */
  (void)ctx;(void)fl; char at[64]; if(!dll_addr_name((u64)__builtin_return_address(0),at,sizeof at)) ksnprintf(at,sizeof at,"%p",__builtin_return_address(0));
  u32 code= rec? rec[0] : 0xC0000409; kprintf("\n*** RaiseFailFastException 0x%x from %s: the program ends\n",code,at); kdbg("  [exit] RaiseFailFastException 0x%x from %s\n",code,at);
  proc_terminate((int)code);
}
/* ---- vectored exception handlers. CPU faults in program code are offered to them (idt.c veh_redirect), and so is RaiseException.
   Windows keeps one list: first=1 puts a handler at the head, first=0 at the tail. Firefox needs this for WebAssembly (it catches
   its own out-of-bounds faults and resumes at a trap stub). ---- */
#define MAXVEH 64
struct veh { int pid; u64 fn; u32 seq; u8 used, first, cont; };
static struct veh vehs[MAXVEH]; static u32 veh_seq;
static u64 veh_add(u32 first, u64 fn, int cont){
  if(!fn) return 0;
  for(int i=0;i<MAXVEH;i++) if(!vehs[i].used){ vehs[i]=(struct veh){cur->pid,fn,++veh_seq,1,(u8)(first!=0),(u8)cont}; return (u64)&vehs[i]; }
  return 0;
}
static u32 veh_remove(u64 h){ struct veh *v=(struct veh *)h; if(v<vehs || v>=vehs+MAXVEH || !v->used || v->pid!=cur->pid) return 0; v->used=0; return 1; }
static u64 WINAPI x_AddVectoredExceptionHandler(u32 first, u64 fn){ return veh_add(first,fn,0); }
static u32 WINAPI x_RemoveVectoredExceptionHandler(u64 h){ return veh_remove(h); }
static u64 WINAPI x_AddVectoredContinueHandler(u32 first, u64 fn){ return veh_add(first,fn,1); }
static u32 WINAPI x_RemoveVectoredContinueHandler(u64 h){ return veh_remove(h); }
void veh_proc_exit(int pid){ for(int i=0;i<MAXVEH;i++) if(vehs[i].used && vehs[i].pid==pid) vehs[i].used=0; }
int veh_count(int pid){ int n=0; for(int i=0;i<MAXVEH;i++) if(vehs[i].used && !vehs[i].cont && vehs[i].pid==pid) n++; return n; }
/* call this process's exception handlers in list order; 1 = one of them answered EXCEPTION_CONTINUE_EXECUTION */
int veh_call(void *ptrs){
  struct veh l[MAXVEH]; int n=0;
  for(int i=0;i<MAXVEH;i++) if(vehs[i].used && !vehs[i].cont && vehs[i].pid==cur->pid) l[n++]=vehs[i];
  for(int i=1;i<n;i++){ struct veh k=l[i]; int j=i-1;                     /* heads (newest first), then tails (oldest first) */
    while(j>=0 && ((k.first && (!l[j].first || l[j].seq<k.seq)) || (!k.first && !l[j].first && l[j].seq>k.seq))){ l[j+1]=l[j]; j--; } l[j+1]=k; }
  for(int i=0;i<n;i++){ i32 (WINAPI *h)(void *)=(i32 (WINAPI *)(void *))l[i].fn; if(h(ptrs)==-1) return 1; }
  return 0;
}
static void WINAPI x_RaiseException(DWORD code, DWORD fl, DWORD n, const u64 *a){
  if(veh_count(cur->pid)){                                               /* offer it to the vectored handlers; "continue" returns to the caller */
    u64 rec[19]; memset(rec,0,sizeof rec); u8 cb[0x4d0+16]; u8 *ctx=(u8 *)(((u64)cb+15)&~15ull); memset(ctx,0,0x4d0);
    if(n>15) n=15; ((u32 *)rec)[0]=code; ((u32 *)rec)[1]=fl&1; rec[2]=(u64)__builtin_return_address(0); ((u32 *)rec)[6]=n; for(u32 k=0;k<n && a;k++) rec[4+k]=a[k];
    *(u32 *)(ctx+0x30)=0x10000B; *(u64 *)(ctx+0xf8)=rec[2]; *(u64 *)(ctx+0x98)=(u64)__builtin_frame_address(0)+16; *(u16 *)(ctx+0x38)=0x08; *(u16 *)(ctx+0x42)=0x10;
    u64 ptrs[2]={(u64)rec,(u64)ctx}; if(veh_call(ptrs)) return;
  }
  if(code==0x406D1388) return;                                           /* MSVC "name this thread" for a debugger: harmless without one */
  char at[64]; if(!dll_addr_name((u64)__builtin_return_address(0),at,sizeof at)) ksnprintf(at,sizeof at,"%p",__builtin_return_address(0));
  kprintf("\n*** RaiseException 0x%x (flags %x, %u args%s%p) from %s: no handler support, the program ends\n",code,fl,n,n? ", first " : "",n? (void*)a[0] : 0,at);
  void (WINAPI *f)(DWORD)=K32("ExitProcess"); if(f) f(code); }
static BOOL WINAPI x_GetSystemTimes(u64 *i, u64 *k, u64 *u){ u64 t=(u64)ticks*10000; if(i) *i=t; if(k) *k=t/10; if(u) *u=t/10; return 1; }
static BOOL WINAPI x_GetProcessTimes(HANDLE h, u64 *c, u64 *e, u64 *k, u64 *u){ (void)h; if(c) *c=0; if(e) *e=0; if(k) *k=(u64)ticks*10000; if(u) *u=(u64)ticks*10000; return 1; }
static u64 WINAPI x_GetCurrentProcessToken(void){ return (u64)-4; }
static BOOL WINAPI x_OpenProcessToken(HANDLE p, DWORD a, HANDLE *t){ (void)p;(void)a; if(t) *t=0x7001; return 1; }
static BOOL WINAPI x_GetTokenInformation(HANDLE t, u32 c, void *i, DWORD n, DWORD *r){ (void)t;(void)c; if(r) *r=4; if(n>=4&&i){ *(u32*)i=1; return 1; } last_error=122; return 0; }
static DWORD WINAPI x_FormatMessageA(DWORD fl, const void *src, DWORD id, DWORD lang, char *b, DWORD n, void *args){
  (void)src;(void)lang;(void)args; char t[64]; int l=kvsnprintf_s(t,sizeof t,id);
  if(fl&0x100){ char *p=heap_alloc_prog((u64)l+1); if(!p) return 0; memcpy(p,t,l+1); *(char**)b=p; return (DWORD)l; }   /* the program LocalFree()s it */
  if((DWORD)l+1>n) return 0; memcpy(b,t,l+1); return (DWORD)l;
}
static DWORD WINAPI x_FormatMessageW(DWORD fl, const void *src, DWORD id, DWORD lang, u16 *b, DWORD n, void *args){
  (void)src;(void)lang;(void)args; char t[64]; int l=kvsnprintf_s(t,sizeof t,id);
  if(fl&0x100){ u16 *p=heap_alloc_prog(((u64)l+1)*2); if(!p) return 0; a2w(t,p,l+1); *(u16**)b=p; return (DWORD)l; }
  if((DWORD)l+1>n) return 0; a2w(t,b,(int)n); return (DWORD)l;
}

/* ---- time ---- */
struct systime { u16 y,mo,dow,d,h,mi,s,ms; };
static BOOL WINAPI x_FileTimeToSystemTime(const u64 *ft, struct systime *st){
  u64 t=*ft/10000; st->ms=(u16)(t%1000); t/=1000; st->s=(u16)(t%60); t/=60; st->mi=(u16)(t%60); t/=60; st->h=(u16)(t%24); t/=24; /* days since 1601-01-01 */
  st->dow=(u16)((t+1)%7); u64 days=t; int y=1601; for(;;){ int leap=(y%4==0&&y%100)||y%400==0; u64 yl=leap?366:365; if(days<yl) break; days-=yl; y++; }
  static const u8 dm[]={31,28,31,30,31,30,31,31,30,31,30,31}; int leap=(y%4==0&&y%100)||y%400==0; int m=0; for(;m<12;m++){ u64 l=dm[m]+(m==1&&leap); if(days<l) break; days-=l; }
  st->y=(u16)y; st->mo=(u16)(m+1); st->d=(u16)(days+1); return 1;
}
static BOOL WINAPI x_SystemTimeToFileTime(const struct systime *st, u64 *ft){
  u64 days=0; for(int y=1601;y<st->y;y++) days+=((y%4==0&&y%100)||y%400==0)?366:365;
  static const u8 dm[]={31,28,31,30,31,30,31,31,30,31,30,31}; int leap=(st->y%4==0&&st->y%100)||st->y%400==0; for(int m=0;m<st->mo-1&&m<12;m++) days+=dm[m]+(m==1&&leap);
  days+=st->d-1; *ft=(((days*24+st->h)*60+st->mi)*60+st->s)*1000ull*10000+(u64)st->ms*10000; return 1;
}
static i32 WINAPI x_CompareFileTime(const u64 *a, const u64 *b){ return *a<*b? -1 : *a>*b; }
static DWORD WINAPI x_GetTimeZoneInformation(u8 *tz){ memset(tz,0,172); return 0; }
static void WINAPI x_GetLocalTime(struct systime *st){ void (WINAPI *f)(struct systime*)=K32("GetSystemTime"); if(f) f(st); }

/* ---- version ---- */
struct osvi { DWORD sz, major, minor, build, plat; char csd[128]; };
static BOOL WINAPI x_GetVersionExA(struct osvi *v){ DWORD sz=v->sz; v->major=10; v->minor=0; v->build=19045; v->plat=2; v->csd[0]=0; (void)sz; return 1; }
static BOOL WINAPI x_GetVersionExW(struct osvi *v){ v->major=10; v->minor=0; v->build=19045; v->plat=2; ((u16*)v->csd)[0]=0; return 1; }
static long WINAPI x_RtlGetVersion(struct osvi *v){ x_GetVersionExA(v); return 0; }
static BOOL WINAPI x_VerifyVersionInfoW(void *v, DWORD m, u64 c){ (void)v;(void)m;(void)c; return 1; }
static u64 WINAPI x_VerSetConditionMask(u64 m, DWORD t, u8 c){ (void)t;(void)c; return m; }

/* ---- interlocked 64-bit and pointer forms ---- */
static i64 WINAPI x_InterlockedCompareExchange64(volatile i64 *d, i64 x, i64 c){ i64 o=*d; if(o==c) *d=x; return o; }
static i64 WINAPI x_InterlockedExchange64(volatile i64 *d, i64 v){ i64 o=*d; *d=v; return o; }
static i64 WINAPI x_InterlockedExchangeAdd64(volatile i64 *d, i64 v){ i64 o=*d; *d=o+v; return o; }
static i64 WINAPI x_InterlockedIncrement64(volatile i64 *d){ return ++*d; }
static i64 WINAPI x_InterlockedDecrement64(volatile i64 *d){ return --*d; }
static i32 WINAPI x_InterlockedOr(volatile i32 *d, i32 v){ i32 o=*d; *d=o|v; return o; }
static i32 WINAPI x_InterlockedAnd(volatile i32 *d, i32 v){ i32 o=*d; *d=o&v; return o; }
static void *WINAPI x_InterlockedExchangePointer(void *volatile *d, void *v){ void *o=*d; *d=v; return o; }
static void *WINAPI x_InterlockedCompareExchangePointer(void *volatile *d, void *x, void *c){ void *o=*d; if(o==c) *d=x; return o; }

/* (the registry now lives in registry.c) */
/* ---- ini files: always empty, defaults returned ---- */
static DWORD WINAPI x_GetPrivateProfileStringA(const char *s, const char *k, const char *d, char *b, DWORD n, const char *f){ (void)s;(void)k;(void)f; if(!n) return 0; const char *v=d? d : ""; DWORD l=(DWORD)strlen(v); if(l>=n) l=n-1; memcpy(b,v,l); b[l]=0; return l; }
static u32 WINAPI x_GetPrivateProfileIntA(const char *s, const char *k, i32 d, const char *f){ (void)s;(void)k;(void)f; return (u32)d; }
static BOOL WINAPI x_WritePrivateProfileStringA(const char *s, const char *k, const char *v, const char *f){ (void)s;(void)k;(void)v;(void)f; return 1; }
static BOOL WINAPI x_GetUserNameW(u16 *b, DWORD *n){ if(*n<5){ *n=5; return 0; } a2w("user",b,(int)*n); *n=5; return 1; }
/* LsaGetUserName(&user, &domain): two LSA_UNICODE_STRINGs ({u16 len, u16 max, pad, wchar *buf}) the caller releases with LsaFreeMemory */
static i32 WINAPI x_LsaGetUserName(u8 **user, u8 **domain){
  const char *u = "user", *d = "WILLOWS"; u8 *a = heap_alloc(64), *b = heap_alloc(64); if(!a || !b) return (i32)0xC0000017;
  for(int k = 0; k < 2; k++){ u8 *s = k ? b : a; const char *t = k ? d : u; u16 *w = (u16 *)(s + 16); int n = 0; for(; t[n]; n++) w[n] = (u8)t[n]; w[n] = 0; *(u16 *)s = (u16)(n * 2); *(u16 *)(s + 2) = (u16)(n * 2 + 2); *(u16 **)(s + 8) = w; }
  if(user) *user = a; if(domain) *domain = b; return 0;
}
static i32 WINAPI x_LsaFreeMemory(void *p){ heap_free(p); return 0; }
static BOOL WINAPI x_GetUserNameExW(u32 fmt, u16 *b, DWORD *n){ (void)fmt; if(*n < 5){ *n = 5; return 0; } a2w("user", b, (int)*n); *n = 4; return 1; }
static BOOL WINAPI x_GetComputerNameExW(u32 type, u16 *b, DWORD *n){ (void)type; if(*n < 8){ *n = 8; return 0; } a2w("willows", b, (int)*n); *n = 7; return 1; }
static BOOL WINAPI x_GetComputerNameW(u16 *b, DWORD *n){ if(*n<8){ *n=8; return 0; } a2w("WILLOWS",b,(int)*n); *n=7; return 1; }


/* ---- batch 2: locale ids, attributes, condition variables, console attach, misc ---- */
static u32 WINAPI x_GetSystemDefaultLCID(void){ return 0x409; }
static u16 WINAPI x_GetSystemDefaultLangID(void){ return 0x409; }
static u16 WINAPI x_GetUserDefaultUILanguage(void){ return 0x409; }
static u16 WINAPI x_SetThreadUILanguage(u16 l){ return l? l : 0x409; }
static BOOL WINAPI x_SetThreadLocale(u32 l){ (void)l; return 1; }
static BOOL WINAPI x_EnumSystemLocalesW(void *cb, DWORD f){ (void)cb;(void)f; return 1; }
static BOOL WINAPI x_EnumUILanguagesW(void *cb, DWORD f, i64 p){ (void)cb;(void)f;(void)p; return 1; }
static int WINAPI x_LCMapStringEx(const u16 *loc, DWORD f, const u16 *s, int n, u16 *d, int dn, void *v, void *r, i64 p){ (void)loc;(void)v;(void)r;(void)p; return x_LCMapStringW(0,f,s,n,d,dn); }
static int WINAPI x_LCIDToLocaleName(u32 l, u16 *b, int n, DWORD f){ (void)l;(void)f; return x_GetUserDefaultLocaleName(b,n); }
static int WINAPI x_MulDiv(int a, int b, int c){ if(!c) return -1; i64 r=(i64)a*b; r= r>=0? (r+c/2)/c : (r-c/2)/c; return (int)r; }
static BOOL WINAPI x_SetFileAttributesA(const char *p, DWORD a){ (void)p;(void)a; return 1; }
static BOOL WINAPI x_SetFileAttributesW(const u16 *p, DWORD a){ (void)p;(void)a; return 1; }
static BOOL WINAPI x_SetFileTime(HANDLE h, const u64 *c, const u64 *a, const u64 *w){ (void)c;(void)a; struct ramfile *f=fs_handle_file(h); if(f && w && *w) f->mtime=*w; return 1; }   /* the write time is kept */
static BOOL WINAPI x_GetFileTime(HANDLE h, u64 *c, u64 *a, u64 *w){ struct ramfile *f=fs_handle_file(h); if(!f){ last_error=6; return 0; } u64 t=fs_mtime(f); if(c) *c=t; if(a) *a=t; if(w) *w=t; return 1; }
static BOOL WINAPI x_MoveFileW(const u16 *a, const u16 *b){ char x[260],y[260]; w2a(a,x,260); w2a(b,y,260); BOOL (WINAPI *f)(const char*,const char*)=K32("MoveFileA"); return f? f(x,y):0; }
static BOOL WINAPI x_MoveFileExW(const u16 *a, const u16 *b, DWORD fl){ char x[260],y[260]; w2a(a,x,260); if(b) w2a(b,y,260); BOOL (WINAPI *f)(const char*,const char*,DWORD)=K32("MoveFileExA"); return f? f(x,b? y : 0,fl) : 0; }
static BOOL WINAPI x_ReplaceFileW(const u16 *dst, const u16 *src, const u16 *backup, DWORD fl, void *e, void *r){ (void)backup;(void)fl;(void)e;(void)r; return x_MoveFileExW(src,dst,1); }
/* NtReadFile / NtWriteFile: Rust's standard library does all synchronous file I/O through these (std::fs::File::read/write), so every
   Rust component in a program (Firefox's key-value stores, ...) failed to write a file without them. IO_STATUS_BLOCK = {status, bytes}.
   ByteOffset: NULL or -2 = the current position, -1 = append (FILE_WRITE_TO_END_OF_FILE), else seek there first. */
static u32 nt_status_of(DWORD e){ return e==38? 0xC0000011u : e==109? 0xC000014Bu : e==6? 0xC0000008u : e==5? 0xC0000022u : e==8? 0xC0000017u : e==87? 0xC000000Du : 0xC0000001u; }
static int nt_seek(HANDLE h, const i64 *off, int write){
  if(!off || *off==-2) return 1;
  BOOL (WINAPI *sp)(HANDLE,i64,i64*,DWORD)=K32("SetFilePointerEx"); if(!sp) return 1;
  return *off==-1? (write? sp(h,0,0,2) : 1) : sp(h,*off,0,0);
}
static u32 WINAPI x_NtReadFile(HANDLE h, HANDLE ev, void *apc, void *actx, u64 *iosb, void *buf, DWORD n, const i64 *off, DWORD *key){
  (void)apc;(void)actx;(void)key; BOOL (WINAPI *rf)(HANDLE,void*,DWORD,DWORD*,void*)=K32("ReadFile"); if(!rf) return 0xC0000002u;
  if(!nt_seek(h,off,0)){ u32 st=nt_status_of(last_error); if(iosb){ iosb[0]=st; iosb[1]=0; } return st; }
  DWORD got=0; BOOL ok=rf(h,buf,n,&got,0); u32 st= ok? (got==0 && n>0? 0xC0000011u : 0) : nt_status_of(last_error);   /* nothing read: STATUS_END_OF_FILE */
  if(iosb){ iosb[0]=st; iosb[1]=got; }
  if(ev){ BOOL (WINAPI *se)(HANDLE)=K32("SetEvent"); if(se) se(ev); }
  return st;
}
static u32 WINAPI x_NtWriteFile(HANDLE h, HANDLE ev, void *apc, void *actx, u64 *iosb, const void *buf, DWORD n, const i64 *off, DWORD *key){
  (void)apc;(void)actx;(void)key; BOOL (WINAPI *wf)(HANDLE,const void*,DWORD,DWORD*,void*)=K32("WriteFile"); if(!wf) return 0xC0000002u;
  if(!nt_seek(h,off,1)){ u32 st=nt_status_of(last_error); if(iosb){ iosb[0]=st; iosb[1]=0; } return st; }
  DWORD put=0; BOOL ok=wf(h,buf,n,&put,0); u32 st= ok? 0 : nt_status_of(last_error);
  if(iosb){ iosb[0]=st; iosb[1]=put; }
  if(ev){ BOOL (WINAPI *se)(HANDLE)=K32("SetEvent"); if(se) se(ev); }
  return st;
}
/* CopyFileEx / MoveFileWithProgress: the progress callback is not called (copies are one memcpy here). Firefox moves a finished download
   from C:\Temp into place with CopyFileExW. Flag 1 = COPY_FILE_FAIL_IF_EXISTS. */
static BOOL WINAPI x_CopyFileExA(const char *a, const char *b, void *prog, void *data, BOOL *cancel, DWORD fl){ (void)prog;(void)data;(void)cancel;
  BOOL (WINAPI *f)(const char*,const char*,BOOL)=K32("CopyFileA"); return f? f(a,b,(fl&1)!=0) : 0; }
static BOOL WINAPI x_CopyFileExW(const u16 *a, const u16 *b, void *prog, void *data, BOOL *cancel, DWORD fl){ char x[260],y[260]; w2a(a,x,260); w2a(b,y,260); return x_CopyFileExA(x,y,prog,data,cancel,fl); }
static BOOL WINAPI x_MoveFileWithProgressA(const char *a, const char *b, void *prog, void *data, DWORD fl){ (void)prog;(void)data;
  BOOL (WINAPI *f)(const char*,const char*,DWORD)=K32("MoveFileExA"); return f? f(a,b,fl) : 0; }
static BOOL WINAPI x_MoveFileWithProgressW(const u16 *a, const u16 *b, void *prog, void *data, DWORD fl){ char x[260],y[260]; w2a(a,x,260); if(b) w2a(b,y,260); return x_MoveFileWithProgressA(x,b? y : 0,prog,data,fl); }
/* GetLongPathName: there are no 8.3 short names, so the long name is the name itself - if the file exists */
static DWORD WINAPI x_GetLongPathNameA(const char *s, char *o, DWORD n){
  if(!s){ last_error=87; return 0; } if(!fs_find(s) && !fs_isdir(s)){ last_error=2; return 0; }
  DWORD l=(DWORD)strlen(s); if(!o || n<=l) return l+1; if(o!=s) memmove(o,s,l+1); return l;
}
static DWORD WINAPI x_GetLongPathNameW(const u16 *s, u16 *o, DWORD n){
  if(!s){ last_error=87; return 0; } char t[260]; w2a(s,t,260); if(!fs_find(t) && !fs_isdir(t)){ last_error=2; return 0; }
  DWORD l=0; while(s[l]) l++; if(!o || n<=l) return l+1; if(o!=s) memmove(o,s,(l+1)*2); return l;
}
/* RegisterWaitForSingleObject: a waiter thread calls back when the object is signaled (or the timeout passes), once with
   WT_EXECUTEONLYONCE (8), else again and again until UnregisterWait. */
struct rwait { HANDLE obj; void (WINAPI *cb)(void *, u8); void *ctx; DWORD ms, flags; volatile int cancelled, running; };
static DWORD WINAPI rwait_thread(void *p){
  struct rwait *w=p; DWORD (WINAPI *wait)(HANDLE,DWORD)=K32("WaitForSingleObject");
  while(!w->cancelled){
    DWORD r=wait? wait(w->obj,w->ms) : 1; if(w->cancelled) break;
    if(r==0xFFFFFFFF){ break; }                                                     /* WAIT_FAILED: the object is gone */
    w->running=1; w->cb(w->ctx,(u8)(r==258)); w->running=0;                        /* TimerOrWaitFired: TRUE on timeout */
    if(w->flags&8) break;
    if(r==258 && w->ms==0) break;
  }
  w->running=0; if(w->cancelled==2) heap_free(w); else w->cancelled=3;             /* 2: unregistered, the thread frees it; 3: finished first */
  return 0;
}
static BOOL WINAPI x_RegisterWaitForSingleObject(HANDLE *out, HANDLE obj, void *cb, void *ctx, DWORD ms, DWORD flags){
  if(!out || !cb){ last_error=87; return 0; }
  struct rwait *w=heap_alloc(sizeof *w); if(!w){ last_error=8; return 0; } memset(w,0,sizeof *w);
  w->obj=obj; w->cb=cb; w->ctx=ctx; w->ms=ms; w->flags=flags;
  HANDLE (WINAPI *ct)(void*,u64,void*,void*,DWORD,DWORD*)=K32("CreateThread"); HANDLE t= ct? ct(0,0,(void*)rwait_thread,w,0,0) : 0;
  if(!t){ heap_free(w); return 0; } BOOL (WINAPI *ch)(HANDLE)=K32("CloseHandle"); if(ch) ch(t);
  *out=(HANDLE)w; return 1;
}
static BOOL WINAPI x_UnregisterWaitEx(HANDLE h, HANDLE ev){
  struct rwait *w=(struct rwait *)h; if(!w){ last_error=87; return 0; }
  if(w->cancelled==3){ heap_free(w); } else w->cancelled=2;                       /* the waiter frees it when it wakes */
  if(ev && ev!=(HANDLE)-1){ BOOL (WINAPI *se)(HANDLE)=K32("SetEvent"); if(se) se(ev); }
  return 1;
}
static BOOL WINAPI x_UnregisterWait(HANDLE h){ return x_UnregisterWaitEx(h,0); }
static BOOL WINAPI x_CopyFileW(const u16 *a, const u16 *b, BOOL fail){ char x[260],y[260]; w2a(a,x,260); w2a(b,y,260); BOOL (WINAPI *f)(const char*,const char*,BOOL)=K32("CopyFileA"); return f? f(x,y,fail):0; }
/* file times: one stored time per file (its last change, see fs_mtime), reported as creation, access and write time */
static void fill_times(u8 *p, const struct ramfile *f){ u64 t=fs_mtime(f); memcpy(p,&t,8); memcpy(p+8,&t,8); memcpy(p+16,&t,8); }
/* WIN32_FILE_ATTRIBUTE_DATA: attributes, 3 FILETIMEs, size high, size low */
static BOOL WINAPI x_GetFileAttributesExA(const char *p, u32 lvl, u8 *out){
  (void)lvl; struct ramfile *f=fs_find(p); if(file_trace()) kdbg("  [file] GetFileAttributesEx(%s) = %s\n",p,!f? "missing" : f->isdir? "dir" : "file");
  if(!f){ last_error=2; return 0; }
  memset(out,0,36); *(DWORD*)out= f->isdir? 0x10 : 0x80; fill_times(out+4,f); if(!f->isdir){ *(DWORD*)(out+28)=(DWORD)(f->size>>32); *(DWORD*)(out+32)=(DWORD)f->size; } return 1;
}
static BOOL WINAPI x_GetFileAttributesExW(const u16 *p, u32 lvl, void *out){ char t[260]; w2a(p,t,260); return x_GetFileAttributesExA(t,lvl,out); }
/* BY_HANDLE_FILE_INFORMATION: attributes, 3 FILETIMEs, volume serial, size high/low, links, index high/low */
static BOOL WINAPI x_GetFileInformationByHandle(HANDLE h, u8 *i){
  memset(i,0,52); struct ramfile *f=fs_handle_file(h);
  if(!f){ if(pipe_is_handle(h)){ *(DWORD*)i=0x80; return 1; } last_error=6; return 0; }
  *(DWORD*)i= f->isdir? 0x10 : 0x80; fill_times(i+4,f); *(DWORD*)(i+28)=0x57494C4C;
  if(!f->isdir){ *(DWORD*)(i+32)=(DWORD)(f->size>>32); *(DWORD*)(i+36)=(DWORD)f->size; }
  *(DWORD*)(i+40)=1; u64 idx=(u64)f; *(DWORD*)(i+44)=(DWORD)(idx>>32); *(DWORD*)(i+48)=(DWORD)idx; return 1;   /* the entry's address is its file id */
}
static BOOL WINAPI x_GetVolumeInformationW(const u16 *r, u16 *vn, DWORD vl, DWORD *sn, DWORD *mc, DWORD *fl, u16 *fs, DWORD fl2){
  (void)r; if(vn&&vl>7) a2w("Willows",vn,(int)vl); if(sn) *sn=0x57494C4C; if(mc) *mc=255; if(fl) *fl=3; if(fs&&fl2>4) a2w("WFS",fs,(int)fl2); return 1;
}
static DWORD WINAPI x_GetLogicalDriveStringsA(DWORD n, char *b){ if(n<5) return 5; memcpy(b,"C:\\\0\0",5); return 4; }
static BOOL WINAPI x_GetVolumePathNameA(const char *p, char *b, DWORD n){ (void)p; if(n<4) return 0; strcpy_(b,"C:\\"); return 1; }
static BOOL WINAPI x_GetVolumePathNameW(const u16 *p, u16 *b, DWORD n){ (void)p; if(n<4) return 0; a2w("C:\\",b,(int)n); return 1; }
static BOOL WINAPI x_RemoveDirectoryW(const u16 *p){ char t[260]; w2a(p,t,260); BOOL (WINAPI *f)(const char*)=K32("RemoveDirectoryA"); return f? f(t) : 0; }
static DWORD WINAPI x_GetTempFileNameW(const u16 *dir, const u16 *pre, u32 u, u16 *out){ static u32 seq=1; char d[200],p[8],t[260]; w2a(dir,d,200); w2a(pre,p,8); int l=(int)strlen(d); if(l&&d[l-1]=='\\') d[--l]=0; char *q=t; for(const char *z=d;*z;) *q++=*z++; *q++='\\'; for(const char *z=p;*z;) *q++=*z++; DWORD v=u? u : seq++; const char *hx="0123456789ABCDEF"; for(int i=12;i>=0;i-=4) *q++=hx[(v>>i)&15]; *q++='.'; *q++='t'; *q++='m'; *q++='p'; *q=0; a2w(t,out,260); return v; }
/* condition variables: cooperative scheduling, so a sleep is a yield */
static BOOL WINAPI x_SetDefaultDllDirectories(DWORD f){ (void)f; return 1; }
static u64 WINAPI x_SetThreadAffinityMask(HANDLE h, u64 m){ (void)h; return m; }
static BOOL WINAPI x_GetProcessAffinityMask(HANDLE h, u64 *p, u64 *s){ (void)h; if(p) *p=1; if(s) *s=1; return 1; }
static BOOL WINAPI x_VerifyVersionInfoA(void *v, DWORD m, u64 c){ (void)v;(void)m;(void)c; return 1; }
static BOOL WINAPI x_GetProductInfo(DWORD a, DWORD b, DWORD c, DWORD d, DWORD *o){ (void)a;(void)b;(void)c;(void)d; if(o) *o=0x30; return 1; }
static BOOL WINAPI x_AttachConsole(DWORD p){ (void)p; return 0; }
static BOOL WINAPI x_FreeConsole(void){ return 1; }
static HANDLE WINAPI x_GetConsoleWindow(void){ return 0; }
static BOOL WINAPI x_SetStdHandle(DWORD n, HANDLE h){ (void)n;(void)h; return 1; }
static BOOL WINAPI x_SystemTimeToTzSpecificLocalTime(const void *tz, const struct systime *u, struct systime *l){ (void)tz; *l=*u; return 1; }
static BOOL WINAPI x_HeapQueryInformation(HANDLE h, u32 c, void *i, u64 l, u64 *r){ (void)h;(void)c; if(r) *r=4; if(l>=4&&i){ *(u32*)i=0; return 1; } return 0; }
static void WINAPI x_RtlInitUnicodeString(u16 *us, const u16 *s){ u16 l=(u16)(wlen(s)*2); us[0]=l; us[1]=(u16)(l+2); *(const u16 **)(us+4)=s; }
static DWORD WINAPI x_RtlNtStatusToDosError(i32 st){ return st==0? 0 : st==(i32)0xC0000022? 5 : st==(i32)0xC0000034||st==(i32)0xC000003A? 2 : 317; }
static HANDLE WINAPI x_GetDesktopWindow(void){ return 0x7FFF0000ull; }
static DWORD WINAPI x_GetSysColor(int i){ static const DWORD c[]={0xC8C8C8,0x6B4A1F,0xFFFFFF,0xFFFFFF,0xF0F0F0,0xFFFFFF,0x646464,0,0,0,0xFFFFFF,0xFFFFFF,0xABABAB,0xD77800,0xFFFFFF,0xF0F0F0,0xA0A0A0,0x6D6D6D,0,0xF0F0F0}; return (unsigned)i<sizeof c/sizeof *c? c[i] : 0xF0F0F0; }
static BOOL WINAPI x_MessageBeep(u32 t){ (void)t; return 1; }
static u32 WINAPI x_CharUpperBuffW(u16 *s, u32 n){ for(u32 i=0;i<n;i++) s[i]=(u16)up(s[i]); return n; }
static BOOL WINAPI x_IsWindowVisible(HANDLE h){ return h!=0; }
static BOOL WINAPI x_EnableWindow(HANDLE h, BOOL e){ (void)h;(void)e; return 0; }
static BOOL WINAPI x_IsWindowEnabled(HANDLE h){ (void)h; return 1; }
static BOOL WINAPI x_SetProcessDefaultLayout(DWORD l){ (void)l; return 1; }
static BOOL WINAPI x_ChangeWindowMessageFilterEx(HANDLE h, u32 m, DWORD a, void *c){ (void)h;(void)m;(void)a;(void)c; return 1; }
static BOOL WINAPI x_InflateRect(i32 *r, int dx, int dy){ r[0]-=dx; r[1]-=dy; r[2]+=dx; r[3]+=dy; return 1; }
static BOOL WINAPI x_OffsetRect(i32 *r, int dx, int dy){ r[0]+=dx; r[1]+=dy; r[2]+=dx; r[3]+=dy; return 1; }
static BOOL WINAPI x_CopyRect(i32 *d, const i32 *s){ for(int i=0;i<4;i++) d[i]=s[i]; return 1; }
static BOOL WINAPI x_IntersectRect(i32 *d, const i32 *a, const i32 *b){ i32 l=a[0]>b[0]?a[0]:b[0], t=a[1]>b[1]?a[1]:b[1], r=a[2]<b[2]?a[2]:b[2], bt=a[3]<b[3]?a[3]:b[3]; if(l>=r||t>=bt){ d[0]=d[1]=d[2]=d[3]=0; return 0; } d[0]=l; d[1]=t; d[2]=r; d[3]=bt; return 1; }


/* ---- batch 3: PuTTY-style startup needs (file APIs in wide form, COM/security/registry stubs, threads) ---- */
static BOOL WINAPI x_AreFileApisANSI(void){ return 1; }
/* Willows extensions for the terminal: read the console output of a program it started (CreateProcess flag 0x40000000). buf==0 && n==0 stops capturing. */
static i32 WINAPI x_WillowsReadOutput(u32 pid, char *buf, u32 n){
  if(pid == 0 || pid >= MAXPROC) return 0; struct proc *p = &procs[pid]; if(!p->cap) return 0;
  if(!buf && !n){ p->cap_on = 0; return 0; }
  u32 avail = p->cap_w - p->cap_r; if(n > avail) n = avail; for(u32 i = 0; i < n; i++) buf[i] = p->cap[(p->cap_r + i) % 32768]; p->cap_r += n; return (i32)n;
}
static i32 WINAPI x_WillowsProcessRunning(u32 pid){ if(!(pid > 0 && pid < MAXPROC && procs[pid].used)) return 0; return 1 | (procs[pid].con_in? 2 : 0); }   /* bit 1: it reads console input */
static BOOL WINAPI x_Beep(DWORD f, DWORD d){ (void)f;(void)d; return 1; }
static BOOL WINAPI x_SetHandleInformation(HANDLE h, DWORD m, DWORD f){ (void)h;(void)m;(void)f; return 1; }
static BOOL WINAPI x_IsDBCSLeadByteEx(u32 cp, u8 c){ (void)cp;(void)c; return 0; }
static BOOL WINAPI x_LocalFileTimeToFileTime(const u64 *l, u64 *f){ *f = *l; return 1; }
static void WINAPI x_GlobalMemoryStatus(u32 *m){ m[0] = 32; m[1] = 20; m[2] = 256u << 20; m[3] = 120u << 20; m[4] = 512u << 20; m[5] = 300u << 20; m[6] = 0x7FFF0000; m[7] = 0x7F000000; }
static BOOL WINAPI x_ReadConsoleW(HANDLE h, u16 *b, DWORD n, DWORD *got, void *c){ (void)h;(void)b;(void)n;(void)c; if(got) *got = 0; return 0; }
static void WINAPI x_RtlUnwind(void *a, void *b, void *c, void *d){ (void)a;(void)b;(void)c;(void)d; }
static void WINAPI x_RtlUnwindEx(void *a, void *b, void *c, void *d, void *e, void *f){ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; }
static void *WINAPI x_RtlPcToFileHeader(void *pc, void **base){ void *b = (void *)cur->imagebase; if(base) *base = b; (void)pc; return b; }
static BOOL WINAPI x_fail0(void){ last_error = 5; return 0; }
static HANDLE WINAPI x_failh(void){ last_error = 5; return 0; }
struct fdw { DWORD attr; u64 ct, at, wt; DWORD hi, lo, r0, r1; u16 name[260]; u16 alt[14]; };
static void fda_to_w(const u8 *a, struct fdw *w){ memset(w, 0, sizeof *w); w->attr = *(const DWORD *)a; w->ct = *(const u64 *)(a + 4); w->at = *(const u64 *)(a + 12); w->wt = *(const u64 *)(a + 20); w->hi = *(const DWORD *)(a + 28); w->lo = *(const DWORD *)(a + 32); a2w((const char *)(a + 44), w->name, 260); }
static HANDLE WINAPI x_FindFirstFileW(const u16 *p, struct fdw *out){ char t[260]; w2a(p, t, 260); u8 a[320]; HANDLE (WINAPI *f)(const char *, void *) = K32("FindFirstFileA"); if(!f) return (HANDLE)(i64)-1; HANDLE h = f(t, a); if(h != (HANDLE)(i64)-1 && h) fda_to_w(a, out); return h; }
static BOOL WINAPI x_FindNextFileW(HANDLE h, struct fdw *out){ u8 a[320]; BOOL (WINAPI *f)(HANDLE, void *) = K32("FindNextFileA"); if(!f) return 0; BOOL r = f(h, a); if(r) fda_to_w(a, out); return r; }
static HANDLE WINAPI x_FindFirstFileExW(const u16 *p, u32 lvl, struct fdw *out, u32 op, void *flt, DWORD fl){ (void)lvl;(void)op;(void)flt;(void)fl; return x_FindFirstFileW(p, out); }
static int WINAPI x_GetDateFormatW(u32 loc, DWORD fl, const struct systime *st, const u16 *fmt, u16 *out, int n){ (void)loc;(void)fl;(void)fmt; struct systime t; if(st) t = *st; else x_GetLocalTime(&t); char b[32]; int k = kvsnprintf_s(b, sizeof b, t.d); (void)k; char o[40]; int l = 0; { char x[12]; int m = t.d, q = 0; do x[q++] = (char)('0' + m % 10); while((m /= 10)); while(q) o[l++] = x[--q]; o[l++] = '/'; m = t.mo; q = 0; do x[q++] = (char)('0' + m % 10); while((m /= 10)); while(q) o[l++] = x[--q]; o[l++] = '/'; m = t.y; q = 0; do x[q++] = (char)('0' + m % 10); while((m /= 10)); while(q) o[l++] = x[--q]; } o[l] = 0; if(!n) return l + 1; a2w(o, out, n); return l + 1; }
static int WINAPI x_GetTimeFormatW(u32 loc, DWORD fl, const struct systime *st, const u16 *fmt, u16 *out, int n){ (void)loc;(void)fl;(void)fmt; struct systime t; if(st) t = *st; else x_GetLocalTime(&t); char o[16]; o[0] = (char)('0' + t.h / 10); o[1] = (char)('0' + t.h % 10); o[2] = ':'; o[3] = (char)('0' + t.mi / 10); o[4] = (char)('0' + t.mi % 10); o[5] = ':'; o[6] = (char)('0' + t.s / 10); o[7] = (char)('0' + t.s % 10); o[8] = 0; if(!n) return 9; a2w(o, out, n); return 9; }
long com_create_builtin(const u8 *clsid, const u8 *iid, void **out);   /* win32y.c: the file dialogs; REGDB_E_CLASSNOTREG for the rest */
static long WINAPI x_CoCreateInstance(void *a, void *b, DWORD c, void *d, void **e){ (void)b;(void)c; return com_create_builtin(a,d,e); }
static BOOL WINAPI x_AllocateAndInitializeSid(void *a, u8 n, DWORD s0, DWORD s1, DWORD s2, DWORD s3, DWORD s4, DWORD s5, DWORD s6, DWORD s7, void **sid){ (void)a;(void)n;(void)s0;(void)s1;(void)s2;(void)s3;(void)s4;(void)s5;(void)s6;(void)s7; static u8 fake[16]; if(sid) *sid = fake; return 1; }
static BOOL WINAPI x_EqualSid(void *a, void *b){ return a == b; }
static DWORD WINAPI x_GetLengthSid(void *s){ (void)s; return 12; }
static BOOL WINAPI x_CopySid(DWORD n, void *d, void *s){ (void)n;(void)d;(void)s; return 1; }
static BOOL WINAPI x_true4(void *a, DWORD b, void *c, DWORD d){ (void)a;(void)b;(void)c;(void)d; return 1; }
static BOOL WINAPI x_InitializeSecurityDescriptor(void *sd, DWORD rev){ (void)rev; memset(sd, 0, 40); return 1; }
static BOOL WINAPI x_SetSecurityDescriptorDacl(void *sd, BOOL p, void *acl, BOOL d){ (void)sd;(void)p;(void)acl;(void)d; return 1; }
static BOOL WINAPI x_SetSecurityDescriptorOwner(void *sd, void *o, BOOL d){ (void)sd;(void)o;(void)d; return 1; }
static BOOL WINAPI x_ChooseColorA(void *c){ (void)c; return 0; }
static BOOL WINAPI x_ChooseFontA(void *c){ (void)c; return 0; }
static BOOL WINAPI x_GetFileNameW(void *c){ (void)c; return 0; }
static HANDLE WINAPI x_imm0(void){ return 0; }

const struct export x_exports[] = {
  XD("secur32",GetUserNameExW), XD("secur32",LsaGetUserName), XD("advapi32",LsaGetUserName), XD("advapi32",LsaFreeMemory), XD("secur32",LsaFreeMemory), X(GetComputerNameExW), X(AreFileApisANSI), X(WillowsReadOutput), X(WillowsProcessRunning), X(Beep), X(SetHandleInformation), X(IsDBCSLeadByteEx), X(LocalFileTimeToFileTime), X(GlobalMemoryStatus), X(ReadConsoleW), X(RtlUnwind), X(RtlUnwindEx), X(RtlPcToFileHeader),
  X(FindFirstFileW), X(FindNextFileW), X(FindFirstFileExW), X(GetDateFormatW), X(GetTimeFormatW),
  {"kernel32","GetCommState",x_fail0}, {"kernel32","SetCommState",x_fail0}, {"kernel32","SetCommBreak",x_fail0}, {"kernel32","ClearCommBreak",x_fail0}, {"kernel32","SetCommTimeouts",x_fail0},
{"ole32","CoCreateInstance",x_CoCreateInstance},
  XD("advapi32",AllocateAndInitializeSid), XD("advapi32",EqualSid), XD("advapi32",GetLengthSid), XD("advapi32",CopySid), XD("advapi32",InitializeSecurityDescriptor), XD("advapi32",SetSecurityDescriptorDacl), XD("advapi32",SetSecurityDescriptorOwner),
  XD("comdlg32",ChooseColorA), XD("comdlg32",ChooseFontA), {"comdlg32","GetOpenFileNameW",x_GetFileNameW}, {"comdlg32","GetSaveFileNameW",x_GetFileNameW},
  {"imm32","ImmGetContext",x_imm0}, {"imm32","ImmReleaseContext",x_imm0}, {"imm32","ImmGetCompositionStringW",x_imm0}, {"imm32","ImmSetCompositionFontA",x_imm0}, {"imm32","ImmSetCompositionWindow",x_imm0},

  X(GetSystemDefaultLCID), X(GetSystemDefaultLangID), X(GetUserDefaultUILanguage), X(SetThreadUILanguage), {"kernel32","GetThreadUILanguage",x_GetUserDefaultUILanguage}, X(SetThreadLocale),
  X(EnumSystemLocalesW), X(EnumUILanguagesW), X(LCMapStringEx), X(LCIDToLocaleName), X(MulDiv), X(SetFileAttributesA), X(SetFileAttributesW), X(SetFileTime), X(GetFileTime),
  X(MoveFileW), X(MoveFileExW), X(ReplaceFileW), X(CopyFileW), X(CopyFileExA), X(CopyFileExW), X(MoveFileWithProgressA), X(MoveFileWithProgressW),
  X(GetLongPathNameA), X(GetLongPathNameW), X(RegisterWaitForSingleObject), X(UnregisterWait), X(UnregisterWaitEx),
  {"ntdll","NtReadFile",x_NtReadFile}, {"ntdll","NtWriteFile",x_NtWriteFile}, X(GetFileAttributesExW), X(GetFileAttributesExA), X(GetFileInformationByHandle), X(GetVolumeInformationW), X(GetLogicalDriveStringsA), X(GetVolumePathNameA), X(GetVolumePathNameW), X(RemoveDirectoryW),X(GetTempFileNameW),
    X(SetDefaultDllDirectories), X(SetThreadAffinityMask), X(GetProcessAffinityMask), X(VerifyVersionInfoA), X(GetProductInfo), X(AttachConsole), X(FreeConsole), X(GetConsoleWindow), X(SetStdHandle),
  X(SystemTimeToTzSpecificLocalTime), X(HeapQueryInformation), XD("ntdll",RtlInitUnicodeString), XD("ntdll",RtlNtStatusToDosError),
  XD("user32",GetDesktopWindow), XD("user32",GetSysColor), XD("user32",MessageBeep), XD("user32",CharUpperBuffW), XD("user32",IsWindowVisible), XD("user32",EnableWindow), XD("user32",IsWindowEnabled),
  XD("user32",SetProcessDefaultLayout), XD("user32",ChangeWindowMessageFilterEx), XD("user32",InflateRect), XD("user32",OffsetRect), XD("user32",CopyRect), XD("user32",IntersectRect),
  X(IsValidCodePage), X(GetOEMCP), X(GetConsoleCP), X(SetConsoleCP), X(GetCPInfo), X(GetUserDefaultLCID), X(GetUserDefaultLangID), X(GetThreadLocale), X(IsValidLocale),
  X(GetUserDefaultLocaleName), X(GetLocaleInfoA), X(GetLocaleInfoW), X(GetStringTypeW), X(GetStringTypeA), X(LCMapStringW), X(LCMapStringA), X(CompareStringW), X(CompareStringA),
  X(lstrlenW), X(lstrcpyW), X(lstrcpynW), X(lstrcpynA), X(lstrcatA), X(lstrcatW), X(lstrcmpiA), X(lstrcmpW), X(lstrcmpiW),
  XD("user32",CharUpperA), XD("user32",CharLowerA), XD("user32",CharUpperW), XD("user32",CharLowerW), XD("user32",CharNextA), XD("user32",CharNextW), XD("user32",IsCharAlphaA),
  XD("user32",wsprintfA), XD("user32",wvsprintfA), XD("user32",wsprintfW),
  X(GetModuleFileNameW), X(GetCurrentDirectoryW), X(SetCurrentDirectoryW), X(DeleteFileW), X(GetFileAttributesW), X(CreateDirectoryW), X(CreateFileW), X(GetFullPathNameW),
  X(GetTempPathA), X(GetTempPathW), X(GetWindowsDirectoryA), X(GetWindowsDirectoryW), X(GetSystemDirectoryA), X(GetSystemDirectoryW),
  X(GetLogicalDrives), X(GetDriveTypeA), X(GetDriveTypeW), X(GetDiskFreeSpaceExA), X(GetVolumeInformationA), X(FlushFileBuffers), X(SetEndOfFile),  X(GetEnvironmentVariableA), X(GetEnvironmentVariableW), X(GetEnvironmentStringsA), X(GetEnvironmentStringsW), {"kernel32","GetEnvironmentStrings",x_GetEnvironmentStringsA},
  X(FreeEnvironmentStringsA), {"kernel32","FreeEnvironmentStringsW",x_FreeEnvironmentStringsA}, X(SetEnvironmentVariableA), X(SetEnvironmentVariableW), X(ExpandEnvironmentStringsA), X(ExpandEnvironmentStringsW),
      X(LocalAlloc), X(LocalFree), X(LocalReAlloc), {"kernel32","GlobalAlloc",x_LocalAlloc}, {"kernel32","GlobalFree",x_LocalFree}, {"kernel32","GlobalReAlloc",x_LocalReAlloc},
  X(GlobalLock), X(GlobalUnlock), X(HeapSetInformation), X(HeapValidate), X(IsBadReadPtr), {"kernel32","IsBadWritePtr",x_IsBadReadPtr},
  X(LoadLibraryW), X(LoadLibraryExA), X(LoadLibraryExW), X(FreeLibrary), X(GetModuleHandleExW), X(GetModuleHandleExA), X(DisableThreadLibraryCalls),
  X(OutputDebugStringW), X(DebugBreak), X(SetErrorMode), X(IsWow64Process), X(GetSystemPowerStatus), X(RaiseException), X(RaiseFailFastException),
  X(AddVectoredExceptionHandler), X(RemoveVectoredExceptionHandler), X(AddVectoredContinueHandler), X(RemoveVectoredContinueHandler),
  {"ntdll","RtlAddVectoredExceptionHandler",x_AddVectoredExceptionHandler}, {"ntdll","RtlRemoveVectoredExceptionHandler",x_RemoveVectoredExceptionHandler},
  {"ntdll","RtlAddVectoredContinueHandler",x_AddVectoredContinueHandler}, {"ntdll","RtlRemoveVectoredContinueHandler",x_RemoveVectoredContinueHandler},
  X(GetSystemTimes), X(GetProcessTimes), {"kernel32","GetThreadTimes",x_GetProcessTimes},
  XD("advapi32",GetCurrentProcessToken), XD("advapi32",OpenProcessToken), XD("advapi32",GetTokenInformation),
  X(FormatMessageA), X(FormatMessageW),
  X(FileTimeToSystemTime), X(SystemTimeToFileTime), X(CompareFileTime), X(GetTimeZoneInformation), X(GetLocalTime),
  X(GetVersionExA), X(GetVersionExW), XD("ntdll",RtlGetVersion), XD("kernel32",VerifyVersionInfoW), XD("kernel32",VerSetConditionMask),
  X(InterlockedCompareExchange64), X(InterlockedExchange64), X(InterlockedExchangeAdd64), X(InterlockedIncrement64), X(InterlockedDecrement64),
  X(InterlockedOr), X(InterlockedAnd), X(InterlockedExchangePointer), X(InterlockedCompareExchangePointer),
  X(GetPrivateProfileStringA), X(GetPrivateProfileIntA), X(WritePrivateProfileStringA), XD("advapi32",GetUserNameW), X(GetComputerNameW),
};
const unsigned x_nexports=sizeof x_exports/sizeof *x_exports;
