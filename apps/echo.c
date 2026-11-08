#include "win.h"
static unsigned slen(const char *s){ unsigned n=0; while(s[n]) n++; return n; }
void WINAPI mainCRTStartup(void){
  char buf[128]; DWORD n, w; HANDLE in = GetStdHandle((DWORD)-10), out = GetStdHandle((DWORD)-11);
  const char *q = "What is your name? ";
  WriteFile(out, q, slen(q), &w, 0);
  ReadFile(in, buf, sizeof buf - 1, &n, 0);
  while(n && (buf[n-1]=='\r' || buf[n-1]=='\n')) n--;
  buf[n] = 0;
  printf("Hello, %s! (%u chars typed)\n", buf, n);
  ExitProcess(n);
}
