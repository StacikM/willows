/* hello32: a 32-bit (PE32) Windows program. Willows runs it in compatibility mode; its API calls go through thunks into the 64-bit Win32 layer. */
typedef unsigned int u32;
#define STD __attribute__((stdcall))
extern void *(STD *__imp_kernel32__GetStdHandle)(u32);
extern int (STD *__imp_kernel32__WriteFile)(void *, const void *, u32, u32 *, void *);
extern u32 (STD *__imp_kernel32__GetTickCount)(void);
extern u32 (STD *__imp_kernel32__GetCurrentProcessId)(void);
extern void *(STD *__imp_kernel32__HeapAlloc)(void *, u32, u32);
extern void *(STD *__imp_kernel32__GetProcessHeap)(void);
extern void (STD *__imp_kernel32__ExitProcess)(u32);

static void out(void *h, const char *s){ u32 n = 0, w; while(s[n]) n++; __imp_kernel32__WriteFile(h, s, n, &w, 0); }
static void num(void *h, u32 v){ char b[12]; int i = 11; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while(v); out(h, b + i); }

void STD mainCRTStartup(void){
  void *h = __imp_kernel32__GetStdHandle((u32)-11);
  out(h, "Hello from a 32-bit Windows program!\n");
  out(h, "pid "); num(h, __imp_kernel32__GetCurrentProcessId()); out(h, ", ticks "); num(h, __imp_kernel32__GetTickCount() > 0); out(h, "\n");
  char *m = __imp_kernel32__HeapAlloc(__imp_kernel32__GetProcessHeap(), 8, 64);                      /* a pointer returned by a 64-bit function must be usable here */
  m[0] = 'o'; m[1] = 'k'; m[2] = '\n'; m[3] = 0; out(h, "heap block: "); out(h, m);
  u32 fs0; __asm__ volatile("mov %%fs:0x18, %0" : "=r"(fs0)); u32 self; __asm__ volatile("mov %%fs:0x18, %0" : "=r"(self));
  out(h, fs0 == self && self ? "fs:[0x18] points at the TEB\n" : "FS is wrong\n");
  __imp_kernel32__ExitProcess(32);
}
