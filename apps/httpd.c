#include "win.h"
/* A tiny web server on port 8080; serves 10 requests then exits. */
void WINAPI mainCRTStartup(void){
  char wsa[408]; WSAStartup(0x0202, wsa);
  SOCKET l = socket(2, 1, 6);
  struct sockaddr_in a = { 2, htons(8080), 0, {0} };
  bind(l, &a, sizeof a); listen(l, 4);
  printf("httpd: listening on port 8080\n");
  for(int n = 1; n <= 10; n++){
    struct sockaddr_in peer; int pl = sizeof peer;
    SOCKET c = accept(l, &peer, &pl);
    char req[512]; int got = recv(c, req, sizeof req - 1, 0);
    char body[160], resp[320];
    int bl = sprintf(body, "<h1>Hello from Willows!</h1><p>request #%d from %s, %d bytes</p>\n", n, inet_ntoa(peer.addr), got);
    int rl = sprintf(resp, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s", bl, body);
    send(c, resp, rl, 0);
    closesocket(c);
    printf("httpd: served request #%d to %s\n", n, inet_ntoa(peer.addr));
  }
  ExitProcess(0);
}
