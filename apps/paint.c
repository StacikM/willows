#include "win.h"
/* Mouse demo: hold left button to draw, click a swatch to change colour, right button clears, ESC quits. */
void WINAPI mainCRTStartup(void){
  HANDLE dc = GetDC(0);
  if(!dc){ printf("no graphics mode available\n"); ExitProcess(1); }
  int W = GetSystemMetrics(0), H = GetSystemMetrics(1);
  static const DWORD cols[8] = { RGB(0,0,0), RGB(230,50,50), RGB(240,150,30), RGB(240,230,40),
                                 RGB(50,190,80), RGB(50,120,230), RGB(150,70,200), RGB(255,255,255) };
  HANDLE br[8]; for(int i = 0; i < 8; i++) br[i] = CreateSolidBrush(cols[i]);
  HANDLE paper = CreateSolidBrush(RGB(255,255,255)), bar = CreateSolidBrush(RGB(212,208,200));
  RECT all = {0, 0, W, H}, top = {0, 0, W, 40};
  int cur = 0, clear = 1;
  while(!GetAsyncKeyState(0x1B)){
    if(clear){
      FillRect(dc, &all, paper); FillRect(dc, &top, bar);
      for(int i = 0; i < 8; i++){ RECT r = {i*40+4, 4, i*40+36, 36}; FillRect(dc, &r, br[i]); }
      SetBkMode(dc, 1); SetTextColor(dc, RGB(0,0,0));
      TextOutA(dc, 340, 12, "Left: draw / pick colour   Right: clear   ESC: quit", 51);
      clear = 0;
    }
    POINT p; GetCursorPos(&p);
    if(GetAsyncKeyState(2) & 0x8000){ clear = 1; }
    else if(GetAsyncKeyState(1) & 0x8000){
      if(p.y < 40){ int i = p.x / 40; if(i < 8) cur = i; }
      else { RECT r = {p.x - 3, p.y - 3, p.x + 4, p.y + 4}; FillRect(dc, &r, br[cur]); }
    }
    Sleep(2);
  }
  ExitProcess(0);
}
