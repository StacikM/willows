#include "kernel.h"
#include "font.h"
/* Built-in Win64 subset. Everything runs in ring 0, single process, flat memory.
   All exported functions use the Windows x64 calling convention (WINAPI = ms_abi). */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE; typedef void *LPVOID;
typedef void (WINAPI *msfn)(void);

#define STD_IN  ((DWORD)-10)
#define STD_OUT ((DWORD)-11)
#define STD_ERR ((DWORD)-12)
#define H_STDIN 3
#define H_STDOUT 7
#define H_STDERR 11
#define HEAP_ZERO 8
#define PROCESS_HEAP 0x00B00000

/* ---- TEB / PEB (x64 layout; gs:[0x30]=TEB, gs:[0x60]=PEB, gs:[0x68]=LastError). Per process; `cur` is the running one. ---- */
#define g_teb (cur->teb)
#define g_peb (cur->peb)
#define g_imagebase (cur->imagebase)
#define g_cmdline (cur->cmdline)
#define g_cmdline_w (cur->cmdline_w)
#define tls_used (cur->tls_used)
#define fls_used (cur->fls_used)
#define fls_slots (cur->fls_slots)
static u64 dummy_teb[0x2000/8];
#define last_error (*(u32*)((u8*)(g_teb? g_teb : dummy_teb)+0x68))

void win32_init_proc(struct proc *p, const struct pe_image *img, const char *name){
  u64 *teb=kmalloc(0x2000,0x1000), *peb=kmalloc(0x1000,16);
  p->teb=teb; p->peb=peb; p->imagebase=img->base;
  u32 n=0; while(name[n] && n<255){ p->cmdline[n]=name[n]; p->cmdline_w[n]=(u8)name[n]; n++; } p->cmdline[n]=0; p->cmdline_w[n]=0;
  { char *d=p->argbuf; p->argc=0; const char *c=p->cmdline;                      /* split the command line into argv */
    while(*c && p->argc<16){ while(*c==' ') c++; if(!*c) break; char q=(*c=='"')? *c++ : 0; p->argvp[p->argc++]=d; while(*c && (q? *c!=q : *c!=' ')) *d++=*c++; *d++=0; if(q && *c==q) c++; }
    p->argvp[p->argc]=0; }
  teb[0x00/8]=~0ull;                   /* exception list: end */
  teb[0x08/8]=img->stack_top;          /* StackBase  */
  teb[0x10/8]=img->stack_lo;           /* StackLimit */
  teb[0x30/8]=(u64)teb;                /* self       */
  teb[0x40/8]=4+p->pid; teb[0x48/8]=8+p->pid;
  teb[0x60/8]=(u64)peb;                /* -> PEB     */
  ((u8*)peb)[0x02]=0;                  /* BeingDebugged */
  peb[0x10/8]=img->base;               /* ImageBaseAddress */
  peb[0x30/8]=PROCESS_HEAP;            /* ProcessHeap */
  *(u32*)((u8*)peb+0x118)=10; *(u32*)((u8*)peb+0x11C)=0;                     /* Windows 10.0 */
  *(u16*)((u8*)peb+0x120)=19045; *(u32*)((u8*)peb+0x124)=2;
  {                                    /* RTL_USER_PROCESS_PARAMETERS: image path, command line, current directory, environment */
    u8 *pp=kmalloc(0x800,16); u16 *wbuf=(u16*)(pp+0x200);                    /* wide strings live in the same block */
    u32 nl=0; const char *nm=p->name; u16 *img_path=wbuf; const char *pre="C:\\Willows\\"; for(;*pre;pre++) img_path[nl++]=(u16)*pre; for(u32 i=0;nm[i]&&nl<120;i++) img_path[nl++]=(u8)nm[i]; img_path[nl]=0;
    u16 *cl=wbuf+128; u32 cn=0; for(;p->cmdline[cn]&&cn<120;cn++) cl[cn]=(u8)p->cmdline[cn]; cl[cn]=0;
    u16 *cwd=wbuf+256; const char *dir="C:\\"; u32 dn=0; for(;dir[dn];dn++) cwd[dn]=(u16)dir[dn]; cwd[dn]=0;
    u16 *env=wbuf+320; env[0]=0; env[1]=0;
    *(u32*)(pp+0x00)=0x800; *(u32*)(pp+0x04)=0x800;                          /* MaximumLength, Length */
    *(u16*)(pp+0x38)=(u16)(dn*2); *(u16*)(pp+0x3A)=(u16)(dn*2+2); *(u64*)(pp+0x40)=(u64)cwd;           /* CurrentDirectory.DosPath */
    *(u16*)(pp+0x60)=(u16)(nl*2); *(u16*)(pp+0x62)=(u16)(nl*2+2); *(u64*)(pp+0x68)=(u64)img_path;      /* ImagePathName */
    *(u16*)(pp+0x70)=(u16)(cn*2); *(u16*)(pp+0x72)=(u16)(cn*2+2); *(u64*)(pp+0x78)=(u64)cl;            /* CommandLine */
    *(u64*)(pp+0x80)=(u64)env;                                                                         /* Environment */
    peb[0x20/8]=(u64)pp;
  }
  if(img->tls_end>img->tls_start || img->tls_index){                          /* implicit TLS (__declspec(thread)) */
    u64 raw=img->tls_end-img->tls_start, total=raw+img->tls_zero;
    u8 *blk=kmalloc(total+16,16); if(blk && raw) memcpy(blk,(void*)img->tls_start,raw);
    u64 *arr=kmalloc(64*8,16); arr[0]=(u64)blk; teb[0x58/8]=(u64)arr;
    if(img->tls_index) *(u32*)img->tls_index=0;
  }
}
void win32_set_context(struct proc *p){            /* GS base = the process's TEB */
  u64 b=p->teb? (u64)p->teb : 0;
  __asm__ volatile("wrmsr"::"c"(0xC0000101u),"a"((u32)b),"d"((u32)(b>>32)));
}
void win32_run_tls_callbacks(struct proc *p){
  if(p->img.tls_callbacks) for(u64 *cb=(u64*)p->img.tls_callbacks; *cb; cb++) ((void (WINAPI*)(u64,u32,void*))*cb)(p->img.base,1,0);
}

/* ---- kernel32 ---- */
static void WINAPI k_ExitProcess(DWORD code){ proc_exit(code); }
static HANDLE WINAPI k_GetStdHandle(DWORD n){ return n==STD_IN?H_STDIN: n==STD_OUT?H_STDOUT: n==STD_ERR?H_STDERR: (HANDLE)-1; }
static BOOL WINAPI k_WriteFile(HANDLE h, const char *buf, DWORD n, DWORD *written, LPVOID ov){
  (void)ov;
  if(fs_is_handle(h)) return fs_hwrite(h,buf,n,written);
  if(pipe_is_handle(h)) return pipe_write(h,buf,n,written);
  if(h!=H_STDOUT && h!=H_STDERR){ last_error=6; return 0; }   /* ERROR_INVALID_HANDLE */
  for(DWORD i=0;i<n;i++) kputc(buf[i]);
  if(written) *written=n;
  return 1;
}
static BOOL WINAPI k_WriteConsoleA(HANDLE h, const char *b, DWORD n, DWORD *w, LPVOID r){ (void)r; return k_WriteFile(h,b,n,w,0); }
static BOOL WINAPI k_WriteConsoleW(HANDLE h, const u16 *b, DWORD n, DWORD *w, LPVOID r){
  (void)r; if(h!=H_STDOUT&&h!=H_STDERR){ last_error=6; return 0; }
  for(DWORD i=0;i<n;i++) kputc(b[i]>127?'?':(char)b[i]);
  if(w) *w=n;
  return 1;
}
static BOOL WINAPI k_ReadFile(HANDLE h, char *buf, DWORD n, DWORD *got, LPVOID ov){
  (void)ov;
  if(fs_is_handle(h)) return fs_hread(h,buf,n,got);
  if(pipe_is_handle(h)) return pipe_read(h,buf,n,got);
  if(h!=H_STDIN){ last_error=6; return 0; }
  if(cur->in_h) return pipe_read(cur->in_h,buf,n,got);                    /* stdin redirected from a pipe */
  DWORD r=n? kb_readline(buf,n):0; if(got) *got=r;
  return 1;
}
static BOOL WINAPI k_ReadConsoleA(HANDLE h, char *b, DWORD n, DWORD *g, LPVOID r){ (void)r; return k_ReadFile(h,b,n,g,0); }
static DWORD WINAPI k_GetLastError(void){ return last_error; }
static void WINAPI k_SetLastError(DWORD e){ last_error=e; }
static const char *WINAPI k_GetCommandLineA(void){ return g_cmdline; }
static const u16 *WINAPI k_GetCommandLineW(void){ return g_cmdline_w; }
static int dll_known(const char *name);
static BOOL WINAPI k_CloseHandle(HANDLE h);
static void *resolve_any(const char *name);
static HANDLE WINAPI k_GetModuleHandleA(const char *n){ if(!n) return g_imagebase; u64 r=dll_modhandle(n); if(r) return r; return dll_known(n)? 0x7FF800000000ull : 0; }
static HANDLE WINAPI k_GetModuleHandleW(const u16 *n){
  if(!n) return g_imagebase; char b[64]; int i=0; for(;n[i]&&i<63;i++) b[i]=n[i]>127?'?':(char)n[i]; b[i]=0; return k_GetModuleHandleA(b);
}
static HANDLE WINAPI k_GetProcessHeap(void){ return PROCESS_HEAP; }
static HANDLE WINAPI k_HeapCreate(DWORD f, u64 i, u64 m){ (void)f;(void)i;(void)m; return PROCESS_HEAP; }
static LPVOID WINAPI k_HeapAlloc(HANDLE h, DWORD fl, u64 sz){ (void)h; (void)fl; void *p=heap_alloc(sz); if(!p) last_error=8; return p; }
static BOOL WINAPI k_HeapFree(HANDLE h, DWORD fl, LPVOID p){ (void)h;(void)fl; heap_free(p); return 1; }
static u64 WINAPI k_HeapSize(HANDLE h, DWORD fl, LPVOID p){ (void)h;(void)fl; return p? heap_size(p) : (u64)-1; }
static LPVOID WINAPI k_HeapReAlloc(HANDLE h, DWORD fl, LPVOID p, u64 sz){ (void)h;(void)fl; void *n=heap_realloc(p,sz); if(!n) last_error=8; return n; }
static LPVOID WINAPI k_VirtualAlloc(LPVOID addr, u64 sz, DWORD type, DWORD prot){
  (void)addr;(void)type;(void)prot; LPVOID p=kmalloc((sz+0xFFF)&~0xFFFull,0x1000); if(!p) last_error=8; return p;
}
static BOOL WINAPI k_VirtualFree(LPVOID a, u64 s, DWORD t){ (void)a;(void)s;(void)t; return 1; }
static BOOL WINAPI k_VirtualProtect(LPVOID a, u64 s, DWORD p, DWORD *old){ (void)a;(void)s;(void)p; if(old) *old=0x40; return 1; }
static u64 WINAPI k_VirtualQuery(LPVOID a, LPVOID info, u64 len){ (void)a;(void)info;(void)len; return 0; }
static DWORD WINAPI k_GetTickCount(void){ return ticks; }
static u64 WINAPI k_GetTickCount64(void){ return ticks; }
static void WINAPI k_Sleep(DWORD ms){ u32 t=ticks+ms; do idle_wait(); while((i32)(ticks-t)<0); }
static BOOL WINAPI k_SwitchToThread(void){ schedule(); return 1; }
static DWORD WINAPI k_GetCurrentProcessId(void){ return 4; }
static DWORD WINAPI k_GetCurrentThreadId(void){ return 8; }
static u32 WINAPI k_lstrlenA(const char *s){ return strlen(s); }
static BOOL WINAPI k_SetConsoleTitleA(const char *s){ (void)s; return 1; }
static long WINAPI n_NtClose(HANDLE h){ k_CloseHandle(h); return 0; }
static HANDLE WINAPI k_LoadLibraryA(const char *n){ u64 r=dll_loadlib(n); if(r) return r; if(dll_known(n)) return 0x7FF800000000ull; last_error=126; return 0; }
static LPVOID WINAPI k_GetProcAddress(HANDLE m, const char *n){ int real; void *rf=dll_getproc(m,n,&real); if(real){ if(!rf) last_error=127; return rf; } if((u64)n<0x10000) return 0;  void *f=resolve_any(n); if(!f){ f=pe_make_soft_stub(n); } return f; }
static void WINAPI k_InitializeCriticalSection(LPVOID c){ (void)c; }
static BOOL WINAPI k_InitializeCriticalSectionEx(LPVOID c, DWORD s, DWORD f){ (void)c;(void)s;(void)f; return 1; }
static BOOL WINAPI k_InitializeCriticalSectionAndSpinCount(LPVOID c, DWORD s){ (void)c;(void)s; return 1; }
static void WINAPI k_EnterCriticalSection(LPVOID c){ (void)c; }
static void WINAPI k_LeaveCriticalSection(LPVOID c){ (void)c; }
static void WINAPI k_DeleteCriticalSection(LPVOID c){ (void)c; }
static DWORD WINAPI k_TlsAlloc(void){ for(int i=0;i<64;i++) if(!tls_used[i]){ tls_used[i]=1; g_teb[0x1480/8+i]=0; return i; } return (DWORD)-1; }
static BOOL WINAPI k_TlsFree(DWORD i){ if(i>=64) return 0; tls_used[i]=0; return 1; }
static LPVOID WINAPI k_TlsGetValue(DWORD i){ if(i>=64){ last_error=87; return 0; } last_error=0; return (LPVOID)g_teb[0x1480/8+i]; }
static BOOL WINAPI k_TlsSetValue(DWORD i, LPVOID v){ if(i>=64) return 0; g_teb[0x1480/8+i]=(u64)v; return 1; }
/* fiber-local storage (the MSVC CRT uses it instead of TLS) */
static DWORD WINAPI k_FlsAlloc(LPVOID cb){ (void)cb; for(int i=0;i<128;i++) if(!fls_used[i]){ fls_used[i]=1; fls_slots[i]=0; return i; } return (DWORD)-1; }
static BOOL WINAPI k_FlsFree(DWORD i){ if(i>=128) return 0; fls_used[i]=0; return 1; }
static LPVOID WINAPI k_FlsGetValue(DWORD i){ if(i>=128) return 0; last_error=0; return (LPVOID)fls_slots[i]; }
static BOOL WINAPI k_FlsSetValue(DWORD i, LPVOID v){ if(i>=128) return 0; fls_slots[i]=(u64)v; return 1; }
static HANDLE WINAPI k_GetCurrentProcess(void){ return (HANDLE)-1; }
static HANDLE WINAPI k_GetCurrentThread(void){ return (HANDLE)-2; }
static BOOL WINAPI k_CloseHandle(HANDLE h){ if(fs_is_handle(h)) fs_hclose(h); else if(pipe_is_handle(h)) pipe_close(h); else if(map_is_handle(h)) map_close(h); return 1; }
static BOOL WINAPI k_TerminateProcess(HANDLE h, DWORD c){
  if(h>=0x8000 && h<0x8000+MAXPROC){ int pid=(int)(h-0x8000); if(pid!=cur->pid){ proc_last_exit[pid]=c; proc_kill(pid); return 1; } }
  proc_exit(c);
}
static HANDLE WINAPI k_OpenProcess(DWORD acc, BOOL inh, DWORD pid){ (void)acc;(void)inh; return (pid>0&&pid<MAXPROC&&procs[pid].used)? 0x8000+pid : 0; }
static BOOL WINAPI k_CreateProcessA(const char *app, char *cmd, void *pa, void *ta, BOOL inh, DWORD fl, void *env, const char *dir, void *si, u32 *pi){
  (void)pa;(void)ta;(void)inh;(void)env;(void)si;
  char name[64]; const char *src=app? app : cmd; if(!src){ last_error=87; return 0; }
  int n=0; if(*src=='"'){ src++; while(*src && *src!='"' && n<63) name[n++]=*src++; if(*src=='"') src++; } else while(*src && *src!=' ' && n<63) name[n++]=*src++;
  name[n]=0; { const char *b=name; for(const char *q=name;*q;q++) if(*q=='\\'||*q=='/') b=q+1; if(b!=name){ int k=0; while(b[k]){ name[k]=b[k]; k++; } name[k]=0; } }
  char full[256]; int fn=0; if(cmd){ while(cmd[fn] && fn<255){ full[fn]=cmd[fn]; fn++; } } else { for(int i=0;i<=n;i++) full[i]=name[i]; fn=n; } full[fn]=0;
  int pid=proc_spawn_cwd(name,full,dir? dir : cur->cwd);
  if(pid<0){ struct ramfile *f=fs_find(name); if(f && !f->isdir) pid=proc_spawn_data(fs_leaf(f),f->data,f->size,full,cur->cwd); }       /* an .exe stored on disk */
  if(pid<0){ last_error=2; return 0; }
  if((fl&0x40000000u) && pid>0 && pid<MAXPROC){ struct proc *c=&procs[pid]; c->cap=heap_alloc(32768); c->cap_w=c->cap_r=0; c->cap_on=c->cap!=0; }      /* Willows extension: capture the child's console output */
  if(si && pid>0 && pid<MAXPROC && (*(u32*)((u8*)si+60)&0x100)){                                                                       /* STARTF_USESTDHANDLES: stdin/stdout may be pipes */
    struct proc *c=&procs[pid]; u64 hin=*(u64*)((u8*)si+80), hout=*(u64*)((u8*)si+88);
    if(pipe_is_handle(hout)){ c->out_h=hout; pipe_addref(hout); }
    if(pipe_is_handle(hin)){ c->in_h=hin; pipe_addref(hin); }
  }
  if(pi){ memset(pi,0,24); *(u64*)pi=0x8000+pid; *(u64*)((u8*)pi+8)=0x8000+pid; pi[4]=pid; pi[5]=pid; }
  return 1;
}
static DWORD WINAPI k_WaitForSingleObject(HANDLE h, DWORD ms){
  if(h>=0x8000 && h<0x8000+MAXPROC){ int pid=(int)(h-0x8000); u32 end=ticks+ms; while(procs[pid].used){ if(ms!=0xFFFFFFFF && (i32)(ticks-end)>=0) return 0x102; idle_wait(); } return 0; }
  return 0;
}
static BOOL WINAPI k_GetExitCodeProcess(HANDLE h, DWORD *code){ if(h<0x8000||h>=0x8000+MAXPROC||!code) return 0; int pid=(int)(h-0x8000); *code= procs[pid].used? 259 : (DWORD)proc_last_exit[pid]; return 1; }
static HANDLE WINAPI k_CreateToolhelp32Snapshot(DWORD fl, DWORD pid){ (void)fl;(void)pid; return 0x9000; }
static int ps_next_i;
static BOOL ps_fill(u8 *pe){
  while(ps_next_i<MAXPROC && !procs[ps_next_i].used) ps_next_i++;
  if(ps_next_i>=MAXPROC) return 0;
  struct proc *p=&procs[ps_next_i]; *(u32*)(pe+8)=p->pid; *(u32*)(pe+28)=1; *(u32*)(pe+32)=0;
  int n=0; while(p->name[n] && n<259){ pe[44+n]=p->name[n]; n++; } pe[44+n]=0; ps_next_i++; return 1;
}
static BOOL WINAPI k_Process32First(HANDLE h, u8 *pe){ (void)h; ps_next_i=0; return ps_fill(pe); }
static BOOL WINAPI k_Process32Next(HANDLE h, u8 *pe){ (void)h; return ps_fill(pe); }
static BOOL WINAPI k_GlobalMemoryStatusEx(u8 *ms){
  u64 total=mm_total(), freeb=mm_free(); memset(ms+4,0,60); *(u32*)ms=64; *(u32*)(ms+4)=total? (u32)((total-freeb)*100/total) : 0;
  *(u64*)(ms+8)=total; *(u64*)(ms+16)=freeb; return 1;
}
static BOOL WINAPI k_GetComputerNameA(char *b, DWORD *n){ const char *h="WILLOWS"; if(!b||!n||*n<8){ if(n) *n=8; return 0; } memcpy(b,h,8); *n=7; return 1; }
static BOOL WINAPI k_GetUserNameA(char *b, DWORD *n){ const char *h="user"; if(!b||!n||*n<5){ if(n) *n=5; return 0; } memcpy(b,h,5); *n=5; return 1; }
static u64 WINAPI sh_ShellExecuteA(u64 hwnd, const char *verb, const char *file, const char *params, const char *dir, i32 show){
  (void)hwnd;(void)verb;(void)show; if(!file) return 2;
  char cmd[300]; unsigned n=0; for(const char *p=file;*p&&n<250;) cmd[n++]=*p++;                    /* the command line is "file params" */
  if(params&&*params){ cmd[n++]=' '; for(const char *p=params;*p&&n<298;) cmd[n++]=*p++; } cmd[n]=0;
  if(!(params&&*params) && shell_open(file)>=0) return 33;
  int pid=proc_spawn_cwd(file,cmd,dir&&*dir? dir : cur->cwd); if(pid<0 && shell_open(file)>=0) return 33; return pid>=0? 33 : 2;
}
static BOOL WINAPI u_ExitWindowsEx(DWORD flags, DWORD reason){ (void)reason; if(flags&2) power_restart(); power_off(); }
static LPVOID WINAPI k_SetUnhandledExceptionFilter(LPVOID f){ (void)f; return 0; }
static i32 WINAPI k_UnhandledExceptionFilter(LPVOID e){ (void)e; return 0; }
static void WINAPI k_GetStartupInfoA(u32 *si){ memset(si,0,104); si[0]=104; }
static void WINAPI k_GetStartupInfoW(u32 *si){ memset(si,0,104); si[0]=104; }
static BOOL WINAPI k_QueryPerformanceCounter(u64 *v){ *v=(u64)ticks*1000; return 1; }   /* microseconds */
static BOOL WINAPI k_QueryPerformanceFrequency(u64 *v){ *v=1000000; return 1; }
void win32_set_last_error(u32 e){ last_error=e; }
static void WINAPI k_GetSystemTimeAsFileTime(u64 *ft){ *ft=(time_now()+11644473600ull)*10000000ull+(u64)(ticks%1000)*10000ull; }
static void WINAPI k_GetSystemTime(u16 *st){
  int y,mo,d,h,mi,s,wd; unix_to_civil(time_now(),&y,&mo,&d,&h,&mi,&s,&wd);
  st[0]=y; st[1]=mo; st[2]=wd; st[3]=d; st[4]=h; st[5]=mi; st[6]=s; st[7]=ticks%1000;
}
static i32 WINAPI k_InterlockedIncrement(volatile i32 *p){ return ++*p; }
static i32 WINAPI k_InterlockedDecrement(volatile i32 *p){ return --*p; }
static i32 WINAPI k_InterlockedExchange(volatile i32 *p, i32 v){ i32 o=*p; *p=v; return o; }
static i32 WINAPI k_InterlockedExchangeAdd(volatile i32 *p, i32 v){ i32 o=*p; *p+=v; return o; }
static i32 WINAPI k_InterlockedCompareExchange(volatile i32 *p, i32 x, i32 c){ i32 o=*p; if(o==c) *p=x; return o; }
static const char *WINAPI k_GetEnvironmentStringsA(void){ static const char e[4]={0,0,0,0}; return e; }
static const u16 *WINAPI k_GetEnvironmentStringsW(void){ static const u16 e[2]={0,0}; return e; }
static BOOL WINAPI k_FreeEnvironmentStringsA(LPVOID p){ (void)p; return 1; }
static void WINAPI k_InitializeSListHead(u64 *h){ h[0]=h[1]=0; }
static BOOL WINAPI k_IsProcessorFeaturePresent(DWORD f){ return f==10 /*SSE2*/ || f==6 /*SSE3?*/ ? 1 : 0; }
static BOOL WINAPI k_IsDebuggerPresent(void){ return 0; }
static DWORD WINAPI k_GetVersion(void){ return 0x4A610A00; }
static void WINAPI k_OutputDebugStringA(const char *s){ kputs(s); }
static DWORD WINAPI k_GetModuleFileNameA(HANDLE m, char *b, DWORD n){ const char *mp=dll_modpath(m); const char *p="C:\\"; u32 i=0; while(*p&&i+1<n) b[i++]=*p++; for(const char *q=mp? mp : g_cmdline;*q&&i+1<n;){ b[i++]=(*q=='/')?'\\':*q; q++; } if(n) b[i]=0; return i; }
static BOOL WINAPI k_SetConsoleCtrlHandler(LPVOID h, BOOL a){ (void)h;(void)a; return 1; }
static BOOL WINAPI k_GetConsoleMode(HANDLE h, DWORD *m){ (void)h; if(m) *m=7; return 1; }
static BOOL WINAPI k_SetConsoleMode(HANDLE h, DWORD m){ (void)h;(void)m; return 1; }
static DWORD WINAPI k_GetConsoleOutputCP(void){ return 437; }
static DWORD WINAPI k_GetACP(void){ return 1252; }
static DWORD WINAPI k_GetFileType(HANDLE h){ return (h==H_STDIN||h==H_STDOUT||h==H_STDERR)? 2 : 0; }
static void WINAPI k_GetSystemInfo(u32 *si){
  memset(si,0,48); si[0]=9; si[1]=4096; *(u64*)(si+2)=0x10000; *(u64*)(si+4)=0x7FFFFFFEFFFFull; *(u64*)(si+6)=1; si[8]=1; si[9]=8664; si[10]=65536; si[11]=6;
}
static LPVOID WINAPI k_EncodePointer(LPVOID p){ return p; }
static int WINAPI k_MultiByteToWideChar(u32 cp, DWORD fl, const char *s, int n, u16 *w, int wn){
  (void)fl; if(n<0) n=strlen(s)+1; int o=0;
  for(int i=0;i<n;){
    u32 c=(u8)s[i]; int len=1;
    if(cp==65001 && c>=0xC0){ int extra= c>=0xF0? 3 : c>=0xE0? 2 : 1; if(i+extra<n+0 && extra<=3){ u32 v= c & (0x3F>>extra); int ok=1; for(int k=1;k<=extra;k++){ u8 cc=(u8)s[i+k]; if((cc&0xC0)!=0x80){ ok=0; break; } v=(v<<6)|(cc&0x3F); } if(ok){ c=v; len=extra+1; } } }
    if(c>=0x10000){ c-=0x10000; if(w){ if(o+2>wn) return 0; w[o]=(u16)(0xD800|(c>>10)); w[o+1]=(u16)(0xDC00|(c&0x3FF)); } o+=2; }
    else { if(w){ if(o+1>wn) return 0; w[o]=(u16)c; } o++; }
    i+=len;
  }
  return o;
}
static int WINAPI k_WideCharToMultiByte(u32 cp, DWORD fl, const u16 *w, int n, char *s, int sn, const char *d, int *u){
  (void)fl;(void)d;(void)u; if(n<0){ n=0; while(w[n]) n++; n++; } int o=0;
  for(int i=0;i<n;i++){
    u32 c=w[i];
    if(c>=0xD800 && c<0xDC00 && i+1<n && w[i+1]>=0xDC00 && w[i+1]<0xE000){ c=0x10000+((c&0x3FF)<<10)+(w[i+1]&0x3FF); i++; }
    char t[4]; int l;
    if(cp==65001){ if(c<0x80){ t[0]=(char)c; l=1; } else if(c<0x800){ t[0]=(char)(0xC0|(c>>6)); t[1]=(char)(0x80|(c&63)); l=2; } else if(c<0x10000){ t[0]=(char)(0xE0|(c>>12)); t[1]=(char)(0x80|((c>>6)&63)); t[2]=(char)(0x80|(c&63)); l=3; } else { t[0]=(char)(0xF0|(c>>18)); t[1]=(char)(0x80|((c>>12)&63)); t[2]=(char)(0x80|((c>>6)&63)); t[3]=(char)(0x80|(c&63)); l=4; } }
    else { t[0]= c>255? '?' : (char)c; l=1; }
    if(s){ if(o+l>sn) return 0; for(int k=0;k<l;k++) s[o+k]=t[k]; } o+=l;
  }
  return o;
}
static u32 WINAPI k_lstrcmpA(const char *a, const char *b){ return strcmp(a,b); }
static char *WINAPI k_lstrcpyA(char *d, const char *s){ char *r=d; while((*d++=*s++)); return r; }
/* SEH-related Rtl* functions: no unwinding support yet, report "nothing found" */
static void WINAPI k_RtlCaptureContext(LPVOID ctx){ memset(ctx,0,1232); }
static LPVOID WINAPI k_RtlLookupFunctionEntry(u64 pc, u64 *base, LPVOID hist){ (void)pc;(void)hist; if(base) *base=0; return 0; }
static LPVOID WINAPI k_RtlVirtualUnwind(DWORD t, u64 b, u64 pc, LPVOID fe, LPVOID ctx, LPVOID h, u64 *f, LPVOID ctxp){ (void)t;(void)b;(void)pc;(void)fe;(void)ctx;(void)h;(void)f;(void)ctxp; return 0; }

/* ---- user32 / gdi32 (draws straight onto the framebuffer; no window manager yet) ---- */
typedef struct { i32 left, top, right, bottom; } RECT;
static u32 rgb(DWORD c){ return ((c&0xFF)<<16)|(c&0xFF00)|((c>>16)&0xFF); }   /* COLORREF 0x00BBGGRR -> 0xRRGGBB */
#define H_DC 0x1000
#define H_BRUSH 0x2000
#define H_FONT 0x3000
static struct { u8 used, style; i32 px; } gfonts[24]; static HANDLE sel_font;      /* sel_font: 0 = default UI font */
static u32 brushes[64]; static u8 brush_used[64];
static DWORD text_fg=0x000000, text_bg=0xFFFFFF; static int bk_opaque=1;

static HANDLE WINAPI u_GetDC(HANDLE w){ return gfx_ok? wm_get_dc(w) : 0; }
static int WINAPI u_ReleaseDC(HANDLE w, HANDLE dc){ (void)w;(void)dc; return 1; }
static int WINAPI u_GetSystemMetrics(int i){ return i==0? wm_screen_w() : i==1? wm_screen_h() : 0; }
static HANDLE WINAPI g_CreateSolidBrush(DWORD c){
  for(int i=0;i<64;i++) if(!brush_used[i]){ brush_used[i]=1; brushes[i]=rgb(c); return H_BRUSH+i; }
  last_error=8; return 0;
}
static BOOL WINAPI g_DeleteObject(HANDLE o){
  if(o>=H_FONT&&o<H_FONT+24){ gfonts[o-H_FONT].used=0; if(sel_font==o) sel_font=0; return 1; }
  if(o>=H_BRUSH&&o<H_BRUSH+56){ brush_used[o-H_BRUSH]=0; return 1; } return o>=H_BRUSH&&o<H_BRUSH+64; }
static int WINAPI u_FillRect(HANDLE dc, const RECT *r, HANDLE br){
  (void)dc; if(!gfx_ok||br<H_BRUSH||br>=H_BRUSH+64||!brush_used[br-H_BRUSH]) return 0;
  wm_gdi_rect(dc,r->left,r->top,r->right-r->left,r->bottom-r->top,brushes[br-H_BRUSH]); return 1;
}
static DWORD WINAPI g_SetPixel(HANDLE dc, int x, int y, DWORD c){ wm_gdi_pixel(dc,x,y,rgb(c)); return c; }
static DWORD WINAPI g_SetTextColor(HANDLE dc, DWORD c){ (void)dc; DWORD o=text_fg; text_fg=c; return o; }
static DWORD WINAPI g_SetBkColor(HANDLE dc, DWORD c){ (void)dc; DWORD o=text_bg; text_bg=c; return o; }
static int WINAPI g_SetBkMode(HANDLE dc, int m){ (void)dc; int o=bk_opaque?2:1; bk_opaque=(m==2); return o; }   /* TRANSPARENT=1 OPAQUE=2 */
/* ---- GDI fonts and stock objects ---- */
static HANDLE new_font(int style, int px){
  for(int i=0;i<24;i++) if(!gfonts[i].used){ gfonts[i].used=1; gfonts[i].style=style; gfonts[i].px=px; return H_FONT+i; }
  return 0;
}
static const struct font *cur_font(void){
  if(sel_font>=H_FONT && sel_font<H_FONT+24 && gfonts[sel_font-H_FONT].used) return font_find(gfonts[sel_font-H_FONT].style,gfonts[sel_font-H_FONT].px);
  return font_find(FS_REGULAR,13);
}
static int ci_has(const char *s, const char *needle){ if(!s) return 0; unsigned n=strlen(needle); for(;*s;s++){ unsigned i=0; while(i<n && s[i] && (s[i]|32)==(needle[i]|32)) i++; if(i==n) return 1; } return 0; }
static HANDLE WINAPI g_CreateFontA(int h, int w, int esc, int ori, int weight, DWORD italic, DWORD under, DWORD strike, DWORD cs, DWORD op, DWORD cp, DWORD q, DWORD pitch, const char *face){
  (void)w;(void)esc;(void)ori;(void)under;(void)strike;(void)cs;(void)op;(void)cp;(void)q;(void)pitch;
  int px= h<0? -h : (h? h*85/100 : 13); int mono=ci_has(face,"courier")||ci_has(face,"consolas")||ci_has(face,"mono")||ci_has(face,"console")||ci_has(face,"fixed");
  int bold=weight>=600; int style= mono? (bold? FS_MONOBOLD : FS_MONO) : bold? FS_BOLD : italic? FS_ITALIC : FS_REGULAR;
  return new_font(style,px);
}
static HANDLE WINAPI g_GetStockObject(int i){
  static HANDLE stock[24];
  if(i>=0&&i<=5){ static const u32 col[6]={0xFFFFFF,0xC0C0C0,0x808080,0x404040,0x000000,0xFFFFFF};
    for(int k=56;k<62;k++) if(!brush_used[k]){} brush_used[56+i]=1; brushes[56+i]=col[i]; return H_BRUSH+56+i; }
  if(i==11||i==16){ if(!stock[i]) stock[i]=new_font(FS_MONO,13); return stock[i]; }
  if(i==12||i==13||i==14||i==17){ if(!stock[i]) stock[i]=new_font(FS_REGULAR,13); return stock[i]; }
  return 0x2F00+i;                                                                 /* pens and the rest: opaque handles */
}
static HANDLE WINAPI g_SelectObject(HANDLE dc, HANDLE o){
  (void)dc;
  if(o>=H_FONT && o<H_FONT+24){ HANDLE old=sel_font; sel_font=o; return old? old : 0x3F00; }
  return 0x2F10;                                                                    /* brush/pen/bitmap selection is accepted and ignored */
}
static BOOL WINAPI g_GetTextExtentPoint32A(HANDLE dc, const char *s, int n, i32 *sz){
  (void)dc; if(n<0) n=strlen(s); const struct font *f=cur_font(); sz[0]=ui_width(f,s,n); sz[1]=ui_lineh(f); return 1;
}
static BOOL WINAPI g_GetTextMetricsA(HANDLE dc, i32 *tm){
  (void)dc; const struct font *f=cur_font(); memset(tm,0,56);
  tm[0]=ui_lineh(f); tm[1]=ui_ascent(f); tm[2]=ui_lineh(f)-ui_ascent(f); tm[5]=ui_width(f,"x",1); tm[6]=ui_width(f,"W",1); return 1;
}
static BOOL WINAPI g_TextOutA(HANDLE dc, int x, int y, const char *s, int n){
  if(!gfx_ok) return 0; if(n<0) n=strlen(s); wm_gdi_text(dc,cur_font(),x,y,s,n,rgb(text_fg),rgb(text_bg),bk_opaque); return 1;
}
static int WINAPI u_GetAsyncKeyState(int vk){
  if(wm_desktop_on() && !wm_proc_focused()) return 0;
  int d = vk==1? mouse_buttons&1 : vk==2? (mouse_buttons>>1)&1 : vk==4? (mouse_buttons>>2)&1 : kb_vk(vk);
  return d? (int)0x8000 : 0;
}
static int WINAPI u_GetKeyState(int vk){ return u_GetAsyncKeyState(vk); }
static BOOL WINAPI u_GetCursorPos(i32 *pt){ if(!pt) return 0; wm_cursor_pos(&pt[0],&pt[1]); return 1; }
static int WINAPI u_ShowCursor(BOOL on){ static int cnt=0; cnt+= on?1:-1; gfx_cursor_show(cnt>=0); return cnt; }

static int WINAPI u_MessageBoxA(HANDLE hwnd, const char *text, const char *caption, DWORD type){
  (void)hwnd; if(!text) text=""; if(!caption) caption="";
  DWORD b=type&0xF; int yesno=(b==4||b==3);
  if(!gfx_ok){
    kprintf("\n+--[ %s ]--\n| %s\n+--", caption, text);
    kprintf(yesno ? " [Yes] [No] ->Yes\n" : b==1 ? " [OK] [Cancel] ->OK\n" : " [OK]\n");
    return yesno? 6 : 1;
  }
  const char *ln[10]; int len[10], nl=0, maxw=(int)strlen(caption);
  for(const char *p=text; nl<10; ){
    int n=0; while(p[n] && p[n]!='\n') n++;
    ln[nl]=p; len[nl]=n; if(n>maxw) maxw=n; nl++;
    if(!p[n]) break; p+=n+1;
  }
  if(maxw>60) maxw=60;
  int w=maxw*8+48, h=24+nl*16+80, x=((int)gfx_w-w)/2, y=((int)gfx_h-h)/2;
  static u32 save[420*260]; if(w>420||h>260) return 0;
  for(int j=0;j<h;j++) for(int i=0;i<w;i++) save[j*w+i]=gfx_get(x+i,y+j);
  gfx_rect(x-2,y-2,w+4,h+4,0x000000); gfx_rect(x,y,w,h,0xD4D0C8);
  gfx_rect(x,y,w,22,0x0A246A); gfx_text(x+8,y+3,caption,60,0xFFFFFF,0,0);
  for(int i=0;i<nl;i++) gfx_text(x+24,y+36+i*16,ln[i],len[i]>60?60:len[i],0x000000,0,0);
  int by=y+h-40;
  const char *lab1=yesno?"Yes":"OK", *lab2=yesno?"No":(b==1?"Cancel":0);
  int bx=x+(w-(lab2?170:80))/2;
  gfx_rect(bx,by,80,26,0x404040); gfx_rect(bx+1,by+1,78,24,0xE8E8E8);
  gfx_text(bx+40-(int)strlen(lab1)*4,by+5,lab1,10,0,0,0);
  gfx_rect(bx-2,by-2,84,2,0x000000); gfx_rect(bx-2,by+26,84,2,0x000000);
  if(lab2){ gfx_rect(bx+90,by,80,26,0x404040); gfx_rect(bx+91,by+1,78,24,0xE8E8E8);
            gfx_text(bx+130-(int)strlen(lab2)*4,by+5,lab2,10,0,0,0); }
  int res=yesno?6:1, prev=mouse_buttons&1; kb_flush();
  for(;;){
    { int vk,c; if(kb_event(&vk,&c)){ if(c==13||c==' ') break; if(vk==0x1B){ res=yesno?7:2; break; } } }
    int down=mouse_buttons&1, mxp=mouse_x, myp=mouse_y;
    if(prev && !down && myp>=by && myp<by+26){
      if(mxp>=bx && mxp<bx+80) break;
      if(lab2 && mxp>=bx+90 && mxp<bx+170){ res=yesno?7:2; break; }
    }
    prev=down; __asm__ volatile("hlt");
  }
  for(int j=0;j<h;j++) for(int i=0;i<w;i++) gfx_pixel(x+i,y+j,save[j*w+i]);
  return res;
}

int win32_messagebox(const char *text, const char *caption, unsigned type){ return u_MessageBoxA(0,text,caption,type); }

/* ---- msvcrt / ucrt (all ms_abi) ---- */
static char iob[3*64];                                  /* FILE objects: stdin, stdout, stderr */
static int fmode_, commode_, argc_=1, errno_; static char *argv_[2], *env_[1]; static char **argv_p, **env_p;
static msfn atexit_fns[32]; static int natexit;
static void *WINAPI c___iob_func(void){ return iob; }
static void *WINAPI c___acrt_iob_func(u32 i){ return iob+(i%3)*64; }
static int WINAPI c___getmainargs(int *argc, char ***argv, char ***env, int wild, void *si){
  (void)wild;(void)si; env_[0]=0; env_p=env_;
  *argc=cur->argc; *argv=cur->argvp; *env=env_; return 0;
}
static void WINAPI c___set_app_type(int t){ (void)t; }
static int WINAPI c__configure_narrow_argv(int m){ (void)m; return 0; }
static char **WINAPI c__get_initial_narrow_environment(void){ env_p=env_; return env_p; }
static int *WINAPI c___p__fmode(void){ return &fmode_; }
static int *WINAPI c___p__commode(void){ return &commode_; }
static int *WINAPI c___p___argc(void){ argc_=cur->argc; return &argc_; }
static char ***WINAPI c___p___argv(void){ argv_p=cur->argvp; return &argv_p; }
static char ***WINAPI c___p__environ(void){ env_p=env_; return &env_p; }
static int *WINAPI c__errno(void){ return &errno_; }
static u32 WINAPI c__controlfp(u32 n, u32 m){ (void)n;(void)m; return 0x9001F; }
static int WINAPI c__controlfp_s(u32 *cur, u32 n, u32 m){ (void)n;(void)m; if(cur) *cur=0x9001F; return 0; }
static void WINAPI c__fpreset(void){}
static void WINAPI c__initterm(msfn *b, msfn *e){ for(;b<e;b++) if(*b) (*b)(); }
typedef int (WINAPI *msfn_i)(void);
static int WINAPI c__initterm_e(msfn_i *b, msfn_i *e){ for(;b<e;b++) if(*b){ int r=(*b)(); if(r) return r; } return 0; }
static void WINAPI c__amsg_exit(int c){ kprintf("\nruntime error R%04d\n",c); k_ExitProcess(255); }
static int  WINAPI c_atexit(msfn f){ if(natexit>=32) return -1; atexit_fns[natexit++]=f; return 0; }
static int  WINAPI c__crt_atexit(msfn f){ return c_atexit(f); }
static LPVOID WINAPI c__onexit(LPVOID f){ c_atexit((msfn)f); return f; }
static int  WINAPI c__initialize_onexit_table(LPVOID t){ (void)t; return 0; }
static int  WINAPI c__register_onexit_function(LPVOID t, msfn f){ (void)t; return c_atexit(f); }
static void WINAPI c__cexit(void){ while(natexit) atexit_fns[--natexit](); }
static void WINAPI c__exit(int c){ k_ExitProcess(c); }
static void WINAPI c_exit(int c){ c__cexit(); k_ExitProcess(c); }
static void WINAPI c_abort(void){ kputs("\nabnormal program termination\n"); k_ExitProcess(3); }
static LPVOID WINAPI c_signal(int s, LPVOID f){ (void)s;(void)f; return 0; }
static void WINAPI c_nop(void){}
static int  WINAPI c_zero(void){ return 0; }
static int  WINAPI c_one(void){ return 1; }
static int WINAPI c_printf(const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); kvprintf(f,ap); __builtin_ms_va_end(ap); return 0; }
static int WINAPI c_sprintf(char *b, const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(b,0x7FFFFFFF,f,ap); __builtin_ms_va_end(ap); return r; }
static int WINAPI c_snprintf(char *b, u64 n, const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(b,(u32)n,f,ap); __builtin_ms_va_end(ap); return r; }
static int WINAPI c_vprintf(const char *f, margs ap){ kvprintf(f,ap); return 0; }
static int WINAPI c_vsnprintf(char *b, u64 n, const char *f, margs ap){ return kvsnprintf(b,(u32)n,f,ap); }
/* what MSVC's printf/sprintf expand to in the UCRT */
static int WINAPI c___stdio_common_vfprintf(u64 opt, void *fp, const char *f, LPVOID loc, margs ap){ (void)opt;(void)fp;(void)loc; kvprintf(f,ap); return 0; }
static int WINAPI c___stdio_common_vsprintf(u64 opt, char *b, u64 n, const char *f, LPVOID loc, margs ap){ (void)opt;(void)loc; return kvsnprintf(b,(u32)(n>0x7FFFFFFF?0x7FFFFFFF:n),f,ap); }
static int WINAPI c_puts(const char *s){ kputs(s); kputc('\n'); return 1; }
static int WINAPI c_putchar(int c){ kputc(c); return c; }
static int WINAPI c_getch(void){ return kb_getc(); }
static int WINAPI c_kbhit(void){ return kb_poll(); }
static int WINAPI c_setvbuf(void *fp, char *b, int m, u64 s){ (void)fp;(void)b;(void)m;(void)s; return 0; }
static void *WINAPI c_malloc(u64 n){ return k_HeapAlloc(0,0,n); }
static void WINAPI c_free(void *p){ heap_free(p); }
static void *WINAPI c_calloc(u64 n, u64 s){ return k_HeapAlloc(0,HEAP_ZERO,n*s); }
static void *WINAPI c_realloc(void *p, u64 n){ return heap_realloc(p,n); }
static u64 WINAPI c_strlen(const char *s){ return strlen(s); }
static int WINAPI c_strcmp(const char *a,const char *b){ return strcmp(a,b); }
static void *WINAPI c_memcpy(void *d,const void *s,u64 n){ return memcpy(d,s,n); }
static void *WINAPI c_memmove(void *d,const void *s,u64 n){ return memmove(d,s,n); }
static void *WINAPI c_memset(void *d,int c,u64 n){ return memset(d,c,n); }
static char *WINAPI c_strcpy(char *d, const char *s){ char *r=d; while((*d++=*s++)); return r; }
static char *WINAPI c_strncpy(char *d, const char *s, u64 n){ u64 i=0; for(;i<n&&s[i];i++) d[i]=s[i]; for(;i<n;i++) d[i]=0; return d; }
static char *WINAPI c_strcat(char *d, const char *s){ c_strcpy(d+strlen(d),s); return d; }
static char *WINAPI c_strchr(const char *s, int c){ for(;;s++){ if(*s==(char)c) return (char*)s; if(!*s) return 0; } }
static char *WINAPI c_strstr(const char *h, const char *n){ u64 l=strlen(n); for(;*h;h++){ u64 i=0; while(i<l&&h[i]==n[i]) i++; if(i==l) return (char*)h; } return l? 0:(char*)h; }
static int WINAPI c_strncmp(const char *a, const char *b, u64 n){ for(;n;n--,a++,b++){ if(*a!=*b) return (u8)*a-(u8)*b; if(!*a) return 0; } return 0; }
static int WINAPI c_memcmp(const void *a, const void *b, u64 n){ const u8 *x=a,*y=b; for(;n;n--,x++,y++) if(*x!=*y) return *x-*y; return 0; }
static int WINAPI c_atoi(const char *s){ int n=0, neg=0; while(*s==' ') s++; if(*s=='-'){ neg=1; s++; } while(*s>='0'&&*s<='9') n=n*10+(*s++-'0'); return neg?-n:n; }
static int WINAPI c_abs(int x){ return x<0?-x:x; }
static u32 rng=1; static void WINAPI c_srand(u32 s){ rng=s; } static int WINAPI c_rand(void){ rng=rng*1103515245+12345; return (rng>>16)&0x7FFF; }
static int WINAPI c_toupper(int c){ return (c>='a'&&c<='z')? c-32:c; }
static int WINAPI c_tolower(int c){ return (c>='A'&&c<='Z')? c+32:c; }
static int WINAPI c_isdigit(int c){ return c>='0'&&c<='9'; }
static int WINAPI c_isalpha(int c){ return (c|32)>='a'&&(c|32)<='z'; }
static int WINAPI c_isspace(int c){ return c==' '||(c>=9&&c<=13); }
static int WINAPI c___C_specific_handler(LPVOID a, LPVOID b, LPVOID c, LPVOID d){ (void)a;(void)b;(void)c;(void)d; return 1; }

#define K(n) {"kernel32", #n, k_##n}
#define U(n) {"user32", #n, u_##n}
#define G(n) {"gdi32", #n, g_##n}
#define C(n) {"msvcrt", #n, c_##n}
#define CX(name, fn) {"msvcrt", name, c_##fn}
static const struct export exports[] = {
  K(ExitProcess), K(GetStdHandle), K(WriteFile), K(WriteConsoleA), K(WriteConsoleW), K(ReadFile), K(ReadConsoleA),
  K(GetLastError), K(SetLastError), K(GetCommandLineA), K(GetCommandLineW), K(GetModuleHandleA), K(GetModuleHandleW),
  K(GetProcessHeap), K(HeapCreate), K(HeapAlloc), K(HeapFree), K(HeapSize), K(HeapReAlloc),
  K(VirtualAlloc), K(VirtualFree), K(VirtualProtect), K(VirtualQuery),
  K(GetTickCount), K(GetTickCount64), K(Sleep), K(SwitchToThread), K(GetCurrentProcessId), K(GetCurrentThreadId),
  K(lstrlenA), K(SetConsoleTitleA), K(LoadLibraryA), K(GetProcAddress),
  K(InitializeCriticalSection), K(InitializeCriticalSectionEx), K(InitializeCriticalSectionAndSpinCount),
  K(EnterCriticalSection), K(LeaveCriticalSection), K(DeleteCriticalSection),
  K(TlsAlloc), K(TlsFree), K(TlsGetValue), K(TlsSetValue), K(FlsAlloc), K(FlsFree), K(FlsGetValue), K(FlsSetValue),
  K(GetCurrentProcess), K(GetCurrentThread), K(CloseHandle), K(TerminateProcess), K(OpenProcess), K(CreateProcessA), K(WaitForSingleObject), K(GetExitCodeProcess), K(CreateToolhelp32Snapshot),
  K(Process32First), K(Process32Next), K(GlobalMemoryStatusEx), K(GetComputerNameA),
  {"ntdll","NtClose",n_NtClose},
  {"advapi32","GetUserNameA",k_GetUserNameA}, {"shell32","ShellExecuteA",sh_ShellExecuteA}, {"user32","ExitWindowsEx",u_ExitWindowsEx},
  K(SetUnhandledExceptionFilter), K(UnhandledExceptionFilter), K(GetStartupInfoA), K(GetStartupInfoW),
  K(QueryPerformanceCounter), K(QueryPerformanceFrequency), K(GetSystemTimeAsFileTime), K(GetSystemTime),
  {"kernel32","GetLocalTime",k_GetSystemTime},
  K(InterlockedIncrement), K(InterlockedDecrement), K(InterlockedExchange), K(InterlockedExchangeAdd), K(InterlockedCompareExchange),
  K(GetEnvironmentStringsA), K(GetEnvironmentStringsW), K(FreeEnvironmentStringsA),
  K(InitializeSListHead), K(IsProcessorFeaturePresent), K(IsDebuggerPresent), K(GetVersion), K(OutputDebugStringA),
  K(GetModuleFileNameA), K(SetConsoleCtrlHandler), K(GetConsoleMode), K(SetConsoleMode), K(GetConsoleOutputCP), K(GetACP),
  K(GetFileType), K(GetSystemInfo), K(EncodePointer), K(MultiByteToWideChar), K(WideCharToMultiByte), K(lstrcmpA), K(lstrcpyA),
  K(RtlCaptureContext), K(RtlLookupFunctionEntry), K(RtlVirtualUnwind),
  {"kernel32","GetNativeSystemInfo",k_GetSystemInfo}, {"kernel32","DecodePointer",k_EncodePointer},
  {"kernel32","FreeEnvironmentStringsW",k_FreeEnvironmentStringsA},
  U(MessageBoxA), U(GetDC), U(ReleaseDC), U(GetSystemMetrics), U(FillRect), U(GetAsyncKeyState), U(GetKeyState),
  U(GetCursorPos), U(ShowCursor),
  G(CreateSolidBrush), G(DeleteObject), G(CreateFontA), G(GetStockObject), G(SelectObject), G(GetTextExtentPoint32A), G(GetTextMetricsA),
  {"gdi32","GetTextExtentPointA",g_GetTextExtentPoint32A}, G(SetPixel), G(SetTextColor), G(SetBkColor), G(SetBkMode), G(TextOutA),
  C(printf), C(sprintf), C(snprintf), C(vprintf), C(vsnprintf), C(puts), C(putchar),
  C(setvbuf), C(malloc), C(free), C(calloc), C(realloc), C(exit), C(abort), C(atexit), C(signal),
  C(strlen), C(strcmp), C(strcpy), C(strncpy), C(strcat), C(strchr), C(strstr), C(strncmp), C(memcpy), C(memmove), C(memset),
  C(memcmp), C(atoi), C(abs), C(srand), C(rand), C(toupper), C(tolower), C(isdigit), C(isalpha), C(isspace),
  CX("_getch",getch), CX("_kbhit",kbhit), CX("_snprintf",snprintf), CX("__iob_func",__iob_func), CX("__acrt_iob_func",__acrt_iob_func),
  CX("__getmainargs",__getmainargs), CX("__set_app_type",__set_app_type), CX("_set_app_type",__set_app_type),
  CX("_configure_narrow_argv",_configure_narrow_argv), CX("_get_initial_narrow_environment",_get_initial_narrow_environment),
  CX("_initialize_narrow_environment",zero), CX("__p__fmode",__p__fmode), CX("__p__commode",__p__commode),
  CX("__p___argc",__p___argc), CX("__p___argv",__p___argv), CX("__p__environ",__p__environ), CX("_errno",_errno),
  CX("_controlfp",_controlfp), CX("_controlfp_s",_controlfp_s), CX("_fpreset",_fpreset), CX("_initterm",_initterm),
  CX("_initterm_e",_initterm_e), CX("_amsg_exit",_amsg_exit), CX("_onexit",_onexit), CX("_crt_atexit",_crt_atexit),
  CX("_initialize_onexit_table",_initialize_onexit_table), CX("_register_onexit_function",_register_onexit_function),
  CX("_cexit",_cexit), CX("_c_exit",_cexit), CX("_exit",_exit), CX("__setusermatherr",nop), CX("_lock",nop), CX("_unlock",nop),
  CX("__lconv_init",zero), CX("_set_fmode",zero), CX("_set_new_mode",zero), CX("_seh_filter_exe",zero),
  CX("_set_invalid_parameter_handler",zero), CX("_configthreadlocale",zero), CX("_set_abort_behavior",zero),
  CX("__stdio_common_vfprintf",__stdio_common_vfprintf), CX("__stdio_common_vsprintf",__stdio_common_vsprintf),
  CX("__C_specific_handler",__C_specific_handler), CX("_isatty",one),
  {"msvcrt","_fmode",&fmode_}, {"msvcrt","_commode",&commode_},             /* data imports: the slot receives the variable's address */
};

static int dll_eq(const char *a, const char *b){   /* case-insensitive, ".dll" optional on a */
  while(*b && (*a|32)==(*b|32)){a++;b++;}
  return !*b && (!*a || stricmp(a,".dll")==0);
}
static const struct export *tab(int t, unsigned *n){
  switch(t){
    case 0: *n=wm_nexports; return wm_exports;                          /* the window manager comes first: its GDI/user32 versions win */
    case 1: *n=sizeof exports/sizeof *exports; return exports;
    case 2: *n=fs_nexports; return fs_exports;
    case 3: *n=net_nexports; return net_exports;
    case 5: *n=x_nexports; return x_exports;
    case 6: *n=crt2_nexports; return crt2_exports;
    case 7: *n=math_nexports; return math_exports;
    case 8: *n=nt2_nexports; return nt2_exports;
    case 9: *n=hw_nexports; return hw_exports;
    case 10: *n=settings_nexports; return settings_exports;
    case 11: *n=reg_nexports; return reg_exports;
    case 12: *n=y_nexports; return y_exports;
    case 13: *n=ipc_nexports; return ipc_exports;
    case 14: *n=nt_nexports; return nt_exports;
    default: *n=inet_nexports; return inet_exports;
  }
}
#define FOR_ALL(e) for(int _t=0;_t<15;_t++) for(unsigned _n, _i=(tab(_t,&_n),0); _i<_n; _i++) for(const struct export *e=tab(_t,&_n)+_i; e; e=0)
static void *resolve_exact(const char *dll, const char *name){
  FOR_ALL(e) if(dll_eq(dll,e->dll) && strcmp(name,e->name)==0) return e->fn;
  return NULL;
}
/* Many DLL names map onto one implementation: api-ms-win-crt-*, ucrtbase, vcruntime140 -> msvcrt; api-ms-win-core-*, kernelbase -> kernel32 */
void *win32_resolve(const char *dll, const char *name){
  void *f=resolve_exact(dll,name); if(f) return f;
  static const char *const alias[]={"api-ms-win-","ucrtbase","vcruntime","msvcp","kernelbase","ntdll","msvcr","wsock32"};
  for(unsigned a=0;a<sizeof alias/sizeof *alias;a++){
    unsigned n=strlen(alias[a]); char p[16]; unsigned k=0; for(;k<n&&dll[k];k++) p[k]=dll[k]|32;
    p[k]=0; if(k==n && strcmp(p,alias[a])==0){ f=resolve_any(name); if(f) return f; }
  }
  return NULL;
}
int win32_dll_is_builtin(const char *name){                       /* true for DLLs that Willows implements itself; real files are never loaded for these */
  for(const char *p=name;*p;p++) if(*p=='\\'||*p=='/') name=p+1;
  static const char *const alias[]={"api-ms-win-","ucrtbase","kernelbase","msvcr"};
  for(unsigned a=0;a<sizeof alias/sizeof *alias;a++){ unsigned n=strlen(alias[a]); unsigned k=0; while(k<n && name[k] && (name[k]|32)==alias[a][k]) k++; if(k==n) return 1; }
  FOR_ALL(e) if(dll_eq(name,e->dll)) return 1; return 0; }
static int dll_known(const char *name){
  for(const char *p=name;*p;p++) if(*p=='\\'||*p=='/') name=p+1;                 /* "C:\\Windows\\System32\\ws2_32.dll" -> "ws2_32.dll" */
  static const char *const alias[]={"api-ms-win-","ucrtbase","vcruntime","msvcp","kernelbase","msvcr"};
  for(unsigned a=0;a<sizeof alias/sizeof *alias;a++){ unsigned n=strlen(alias[a]); unsigned k=0; while(k<n && name[k] && (name[k]|32)==alias[a][k]) k++; if(k==n) return 1; }
  FOR_ALL(e) if(dll_eq(name,e->dll)) return 1; return 0; }
static void *resolve_any(const char *name){ FOR_ALL(e) if(strcmp(name,e->name)==0) return e->fn; return NULL; }
