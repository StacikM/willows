#include "win.h"
static unsigned slen(const char *s){ unsigned n=0; while(s[n]) n++; return n; }

void WINAPI mainCRTStartup(void){
  const char *msg = "Hello from a Windows PE executable!\n";
  DWORD wrote;
  WriteFile(GetStdHandle((DWORD)-11), msg, slen(msg), &wrote, 0);
  printf("msvcrt printf works: %d + %d = %d, hex %x, str %s\n", 40, 2, 42, 0xBEEF, "ok");

  char *p = HeapAlloc(GetProcessHeap(), 8, 64);
  p[0]='h'; p[1]='e'; p[2]='a'; p[3]='p'; p[4]='\n'; p[5]=0;
  WriteFile(GetStdHandle((DWORD)-11), p, 5, &wrote, 0);

  DWORD t0 = GetTickCount();
  Sleep(250);
  printf("slept %u ms\n", GetTickCount() - t0);

  MessageBoxA(0, "Win32 MessageBoxA says hi", "Willows", 0);
  CreateDirectoryA("C:\\x", 0);          /* not implemented yet -> reported, app is stopped cleanly */
  printf("not reached\n");
  ExitProcess(0);
}
