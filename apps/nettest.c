#include "win.h"
/* A tiny HTTP client: GET / from the QEMU host (10.0.2.2:8000), then try a DNS lookup. */
static int fetch(const char *ip, int port, const char *path, const char *host){
  SOCKET s = socket(2, 1, 6);
  struct sockaddr_in a = { 2, htons(port), inet_addr(ip), {0} };
  printf("connecting to %s:%d ...\n", ip, port);
  if(connect(s, &a, sizeof a) != 0){ printf("connect failed, WSA error %d\n", WSAGetLastError()); closesocket(s); return 1; }
  char req[128]; sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
  int n = 0; while(req[n]) n++;
  send(s, req, n, 0);
  char buf[512]; int total = 0, got;
  while((got = recv(s, buf, sizeof buf - 1, 0)) > 0){
    buf[got] = 0; if(total < 700) printf("%s", buf); total += got;
  }
  printf("\n-- %d bytes received --\n", total);
  closesocket(s); return 0;
}
void WINAPI mainCRTStartup(void){
  char wsa[408]; WSAStartup(0x0202, wsa);
  int rc = fetch("10.0.2.2", 8000, "/hello.txt", "10.0.2.2");
  struct hostent *he = gethostbyname("example.com");
  if(he){
    const char *ip = inet_ntoa(*(unsigned int*)he->h_addr_list[0]);
    printf("example.com -> %s\n", ip);
    fetch(ip, 80, "/", "example.com");
  } else   printf("DNS lookup for example.com failed (WSA error %d) - no internet from here?\n", WSAGetLastError());
  ExitProcess(rc);
}
