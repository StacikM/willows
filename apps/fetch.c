#include "win.h"
/* Downloads web pages with the WinINet API: plain HTTP, HTTPS (certificate verified), and a redirect. */
static void show(const char *url, int max){
  printf("\n=== %s\n", url);
  HANDLE net = InternetOpenA("Willows", 0, 0, 0, 0);
  HANDLE h = InternetOpenUrlA(net, url, 0, 0, 0, 0);
  if(!h){ printf("FAILED, error %u\n", GetLastError()); return; }
  DWORD status = 0, len = 4;
  HttpQueryInfoA(h, 19 | 0x20000000, &status, &len, 0);
  printf("HTTP status %u\n", status);
  char buf[512]; DWORD got, total = 0, shown = 0;
  while(InternetReadFile(h, buf, sizeof buf - 1, &got) && got){
    buf[got] = 0; total += got;
    if(shown < (DWORD)max){ printf("%s", buf); shown += got; }
  }
  printf("\n-- %u bytes --\n", total);
  InternetCloseHandle(h);
}
void WINAPI mainCRTStartup(void){
  show("https://example.com/", 200);
  printf("\n##### certificates that MUST be rejected #####\n");
  show("https://expired.badssl.com/", 80);
  show("https://self-signed.badssl.com/", 80);
  show("https://wrong.host.badssl.com/", 80);
  printf("\n##### a bigger site that redirects #####\n");
  show("https://www.wikipedia.org/", 150);
  DWORD hr = URLDownloadToFileA(0, "https://example.com/", "example.html", 0, 0);
  printf("\nURLDownloadToFile -> %s (saved as C:\\example.html; open it in the notepad)\n", hr == 0 ? "OK" : "failed");
  ExitProcess(0);
}
