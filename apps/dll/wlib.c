/* wlib.dll: imports wdep.dll, prints from DllMain, exports Add and Hello */
#define WINAPI __attribute__((ms_abi))
extern void *(WINAPI *__imp_kernel32__GetStdHandle)(unsigned);
extern int (WINAPI *__imp_kernel32__WriteFile)(void *, const void *, unsigned, unsigned *, void *);
extern int (WINAPI *__imp_wdep__DepTriple)(int);
static int len(const char *s){ int n = 0; while(s[n]) n++; return n; }
static void say(const char *s){ unsigned w; __imp_kernel32__WriteFile(__imp_kernel32__GetStdHandle(-11), s, len(s), &w, 0); }
int WINAPI DllMain(void *h, unsigned reason, void *res){ (void)h; (void)res; say(reason == 1 ? "[wlib] DllMain attach\n" : ""); return 1; }
int WINAPI EXPORT_Add(int a, int b){ return a + b; }
int WINAPI EXPORT_AddThenTriple(int a, int b){ return __imp_wdep__DepTriple(a + b); }
void WINAPI EXPORT_Hello(void){ say("[wlib] Hello from a real DLL\n"); }
