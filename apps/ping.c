#include "win.h"
/* ping 10.0.2.2 (QEMU's gateway) four times using the Windows ICMP API */
void WINAPI mainCRTStartup(void){
  char wsa[408]; WSAStartup(0x0202, wsa);
  unsigned int target = inet_addr("10.0.2.2");
  HANDLE h = IcmpCreateFile();
  printf("Pinging %s with 32 bytes of data:\n", inet_ntoa(target));
  int ok = 0;
  for(int i = 0; i < 4; i++){
    unsigned char reply[64];
    if(IcmpSendEcho(h, target, "abcdefghijklmnopqrstuvwabcdefghi", 32, 0, reply, sizeof reply, 1000)){
      printf("Reply from %s: bytes=32 time=%dms\n", inet_ntoa(target), *(unsigned int*)(reply + 8)); ok++;
    } else printf("Request timed out.\n");
    Sleep(700);
  }
  printf("Packets: sent 4, received %d\n", ok);
  ExitProcess(ok == 4 ? 0 : 1);
}
