#include "win.h"
/* Paints a gradient + text, then a square you move with the arrow keys. ESC quits. */
void WINAPI mainCRTStartup(void){
  HANDLE dc = GetDC(0);
  int W = GetSystemMetrics(0), H = GetSystemMetrics(1);
  if(!dc){ printf("no graphics mode available\n"); ExitProcess(1); }
  for(int y = 0; y < H; y++){
    HANDLE b = CreateSolidBrush(RGB(20, 20 + y * 100 / H, 80 + y * 120 / H));
    RECT r = {0, y, W, y + 1}; FillRect(dc, &r, b); DeleteObject(b);
  }
  HANDLE red = CreateSolidBrush(RGB(230, 60, 60)), grn = CreateSolidBrush(RGB(60, 200, 90));
  RECT box = {40, 60, 300, 200}; FillRect(dc, &box, red);
  SetBkMode(dc, 1);
  SetTextColor(dc, RGB(255, 255, 255));
  TextOutA(dc, 60, 20, "Willows - Win32 GDI demo (arrows move, ESC quits)", 54);
  SetTextColor(dc, RGB(0, 0, 0));
  TextOutA(dc, 60, 120, "FillRect + TextOutA", 19);
  for(int i = 0; i < 100; i++){ SetPixel(dc, 340 + i, 100, RGB(255, 255, 0)); SetPixel(dc, 340 + i, 101, RGB(255, 255, 0)); }

  int x = W / 2, y = H / 2;
  RECT s; HANDLE bg = CreateSolidBrush(RGB(20, 80, 140));
  while(!GetAsyncKeyState(0x1B)){
    s.left = x; s.top = y; s.right = x + 24; s.bottom = y + 24; FillRect(dc, &s, bg);
    if(GetAsyncKeyState(0x25)) x -= 4;
    if(GetAsyncKeyState(0x27)) x += 4;
    if(GetAsyncKeyState(0x26)) y -= 4;
    if(GetAsyncKeyState(0x28)) y += 4;
    s.left = x; s.top = y; s.right = x + 24; s.bottom = y + 24; FillRect(dc, &s, grn);
    Sleep(16);
  }
  ReleaseDC(0, dc);
  ExitProcess(0);
}
