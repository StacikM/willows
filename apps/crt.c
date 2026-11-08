#include "win.h"
/* Mimics what a real C runtime does: startup stub -> __getmainargs -> main -> exit.
   main() then pokes the TEB/PEB exactly like compiled Windows code does through fs:. */
static void at_exit(void){ printf("atexit handler ran\n"); }
static void pre_main(void){ printf("_initterm ran a pre-main initialiser\n"); }

int main(int argc, char **argv, char **envp){
  unsigned long long self, peb, seh; unsigned last;
  (void)envp;
  __asm__ volatile("mov %%gs:0x30,%0" : "=r"(self));
  __asm__ volatile("mov %%gs:0x60,%0" : "=r"(peb));
  __asm__ volatile("mov %%gs:0x00,%0" : "=r"(seh));
  printf("argc=%d argv[0]=%s\n", argc, argv[0]);
  printf("TEB=%p gs:[0x30]=%p SEH head=%p\n", self, self, seh);
  printf("PEB=%p ImageBase=%p GetModuleHandle(0)=%p\n", peb, *(unsigned long long*)(peb + 0x10), GetModuleHandleA(0));
  SetLastError(1234);
  __asm__ volatile("mov %%gs:0x68,%0" : "=r"(last));
  printf("SetLastError(1234) -> gs:[0x68] = %d\n", last);
  DWORD i = TlsAlloc(); TlsSetValue(i, (void*)0xCAFE);
  printf("TLS slot %u holds %x\n", i, (unsigned)(unsigned long long)TlsGetValue(i));
  DWORD (WINAPI *tick)(void) = (DWORD (WINAPI*)(void))GetProcAddress(LoadLibraryA("kernel32.dll"), "GetTickCount");
  printf("GetProcAddress(GetTickCount) = %p, ticks=%u\n", (void*)tick, tick ? tick() : 0);
  char buf[64]; sprintf(buf, "%s-%05d-%-4s|", "sprintf", 42, "ab");
  fprintf(stderr, "stderr says: %s\n", buf);
  atexit(at_exit);
  return argc + 40;
}

static void (*init_tbl[])(void) = { pre_main, 0 };
void WINAPI mainCRTStartup(void){
  int argc; char **argv, **env; int si = 0;
  __set_app_type(1);
  _initterm(init_tbl, init_tbl + 2);
  __getmainargs(&argc, &argv, &env, 0, &si);
  exit(main(argc, argv, env));
}
