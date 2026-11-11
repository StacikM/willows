/* apitest3: Win32 batch 3 (GDI). `apitest3` checks pixels drawn into a DIB section and prints PASS/FAIL; `apitest3 gdi` opens a window showing the shapes. */
#include <stdio.h>
#include <string.h>
#include "winui.h"
typedef unsigned short WCHAR; typedef unsigned long long U64; typedef unsigned int U32;
#define W(s) ((const WCHAR *)u##s)
#define RGB(r, g, b) ((U32)((r) | ((g) << 8) | ((b) << 16)))
IMP(gdi32, HANDLE, CreateCompatibleDC, (HANDLE))
IMP(gdi32, HANDLE, CreateDIBSection, (HANDLE, const void*, UINT, void**, HANDLE, DWORD))
IMP(gdi32, BOOL, Ellipse, (HANDLE, int, int, int, int))
IMP(gdi32, BOOL, RoundRect, (HANDLE, int, int, int, int, int, int))
IMP(gdi32, BOOL, Polygon, (HANDLE, const POINT*, int))
IMP(gdi32, BOOL, Pie, (HANDLE, int, int, int, int, int, int, int, int))
IMP(gdi32, BOOL, Arc, (HANDLE, int, int, int, int, int, int, int, int))
IMP(gdi32, BOOL, PolyBezier, (HANDLE, const POINT*, DWORD))
IMP(gdi32, HANDLE, GetStockObject, (int))
IMP(gdi32, HANDLE, CreatePen, (int, int, DWORD))
IMP(gdi32, int, IntersectClipRect, (HANDLE, int, int, int, int))
IMP(gdi32, int, SelectClipRgn, (HANDLE, HANDLE))
IMP(gdi32, HANDLE, CreateRectRgn, (int, int, int, int))
IMP(gdi32, int, CombineRgn, (HANDLE, HANDLE, HANDLE, int))
IMP(gdi32, int, GetRgnBox, (HANDLE, RECT*))
IMP(gdi32, BOOL, PtInRegion, (HANDLE, int, int))
IMP(gdi32, int, SaveDC, (HANDLE))
IMP(gdi32, BOOL, RestoreDC, (HANDLE, int))
IMP(gdi32, BOOL, BitBlt, (HANDLE, int, int, int, int, HANDLE, int, int, DWORD))
IMP(gdi32, BOOL, AlphaBlend, (HANDLE, int, int, int, int, HANDLE, int, int, int, int, DWORD))
IMP(gdi32, BOOL, TransparentBlt, (HANDLE, int, int, int, int, HANDLE, int, int, int, int, DWORD))
IMP(gdi32, BOOL, GradientFill, (HANDLE, void*, DWORD, void*, DWORD, DWORD))
IMP(gdi32, int, GetDIBits, (HANDLE, HANDLE, DWORD, DWORD, void*, void*, DWORD))
IMP(gdi32, int, SetDIBitsToDevice, (HANDLE, int, int, DWORD, DWORD, int, int, DWORD, DWORD, const void*, const void*, DWORD))
IMP(gdi32, int, SetBkColor, (HANDLE, DWORD))
IMP(user32, int, DrawTextW, (HANDLE, const WCHAR*, int, RECT*, DWORD))
IMP(user32, HANDLE, GetDC, (HANDLE))
IMP(user32, int, ReleaseDC, (HANDLE, HANDLE))

static int fails, total, verbose;
static void check(const char *what, int ok){ total++; if(!ok || verbose) printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if(!ok) fails++; }
static unsigned char bmi32[40];
static void mkbmi(unsigned char *b, int w, int h, int bpp){ memset(b, 0, 40); *(DWORD*)b = 40; *(int*)(b + 4) = w; *(int*)(b + 8) = h; *(unsigned short*)(b + 12) = 1; *(unsigned short*)(b + 14) = (unsigned short)bpp; }
static void fill(HANDLE dc, int l, int t, int r, int b, U32 col){ RECT rc = {l, t, r, b}; HANDLE br = API(gdi32, CreateSolidBrush)(col); API(user32, FillRect)(dc, &rc, br); API(gdi32, DeleteObject)(br); }
#define PIX(bits, w, x, y) (((U32*)(bits))[(y) * (w) + (x)] & 0xFFFFFF)
#define BGR(c) ((((c) & 255) << 16) | ((c) & 0xFF00) | (((c) >> 16) & 255))      /* COLORREF -> 0xRRGGBB as stored in pixels */

static void run_checks(void){
  HANDLE dc = API(gdi32, CreateCompatibleDC)(0); void *bits = 0; mkbmi(bmi32, 100, -100, 32);
  HANDLE bmp = API(gdi32, CreateDIBSection)(dc, bmi32, 0, &bits, 0, 0); check("CreateDIBSection (32-bit top-down) gives a pixel pointer", bmp && bits);
  API(gdi32, SelectObject)(dc, bmp); fill(dc, 0, 0, 100, 100, RGB(255, 255, 255));
  check("GDI drawing lands in the DIB bits", PIX(bits, 100, 50, 50) == 0xFFFFFF);
  fill(dc, 10, 10, 30, 30, RGB(255, 0, 0)); check("FillRect red is visible through the bits pointer", PIX(bits, 100, 15, 15) == 0xFF0000);

  /* shapes */
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, SelectObject)(dc, API(gdi32, GetStockObject)(8)); API(gdi32, SelectObject)(dc, API(gdi32, CreateSolidBrush)(RGB(0, 0, 255)));
  API(gdi32, Ellipse)(dc, 10, 10, 60, 60); check("Ellipse: centre is filled", PIX(bits, 100, 35, 35) == 0x0000FF); check("Ellipse: the corner of its bounding box is not", PIX(bits, 100, 11, 11) == 0xFFFFFF); check("Ellipse: edge midpoint is filled", PIX(bits, 100, 11, 35) == 0x0000FF);
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, RoundRect)(dc, 10, 10, 70, 50, 20, 20);
  check("RoundRect: centre filled", PIX(bits, 100, 40, 30) == 0x0000FF); check("RoundRect: the rounded corner is cut off", PIX(bits, 100, 10, 10) == 0xFFFFFF); check("RoundRect: a straight edge is filled", PIX(bits, 100, 40, 10) == 0x0000FF);
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); POINT tri[3] = {{50, 10}, {90, 80}, {10, 80}}; API(gdi32, Polygon)(dc, tri, 3);
  check("Polygon: inside the triangle", PIX(bits, 100, 50, 60) == 0x0000FF); check("Polygon: outside the triangle", PIX(bits, 100, 15, 20) == 0xFFFFFF);
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, Pie)(dc, 10, 10, 90, 90, 90, 50, 50, 10);
  { int filled = 0; for(int y = 0; y < 100; y++) for(int x = 0; x < 100; x++) if(PIX(bits, 100, x, y) == 0x0000FF) filled++; check("Pie fills a part of the circle", filled > 500 && filled < 5000); }
  POINT bz[4] = {{10, 90}, {30, 10}, {70, 10}, {90, 90}}; fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, SelectObject)(dc, API(gdi32, CreatePen)(0, 2, RGB(0, 128, 0))); API(gdi32, PolyBezier)(dc, bz, 4);
  { int drawn = 0; for(int y = 0; y < 100; y++) for(int x = 0; x < 100; x++) if(PIX(bits, 100, x, y) != 0xFFFFFF) drawn++; check("PolyBezier draws a curve", drawn > 60); }

  /* clipping and regions */
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, SaveDC)(dc); API(gdi32, IntersectClipRect)(dc, 10, 10, 20, 20); fill(dc, 0, 0, 100, 100, RGB(0, 200, 0));
  check("IntersectClipRect: drawing inside the clip", PIX(bits, 100, 15, 15) == 0x00C800); check("IntersectClipRect: nothing outside the clip", PIX(bits, 100, 50, 50) == 0xFFFFFF);
  API(gdi32, RestoreDC)(dc, -1); fill(dc, 40, 40, 60, 60, RGB(0, 200, 0)); check("RestoreDC removes the clip again", PIX(bits, 100, 50, 50) == 0x00C800);
  HANDLE r1 = API(gdi32, CreateRectRgn)(0, 0, 50, 50), r2 = API(gdi32, CreateRectRgn)(30, 30, 80, 80), rd = API(gdi32, CreateRectRgn)(0, 0, 1, 1); RECT box; API(gdi32, CombineRgn)(rd, r1, r2, 1); API(gdi32, GetRgnBox)(rd, &box);
  check("CombineRgn(AND) gives the overlap", box.left == 30 && box.top == 30 && box.right == 50 && box.bottom == 50); check("PtInRegion", API(gdi32, PtInRegion)(rd, 40, 40) && !API(gdi32, PtInRegion)(rd, 10, 10));
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, SelectClipRgn)(dc, rd); fill(dc, 0, 0, 100, 100, RGB(200, 0, 200)); check("SelectClipRgn clips to the region", PIX(bits, 100, 40, 40) == 0xC800C8 && PIX(bits, 100, 10, 10) == 0xFFFFFF); API(gdi32, SelectClipRgn)(dc, 0);

  /* text */
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); RECT tr = {0, 0, 40, 0}; int th = API(user32, DrawTextW)(dc, W("Hi there friend"), -1, &tr, 0x10 | 0x400);
  check("DrawTextW(DT_CALCRECT | DT_WORDBREAK) wraps into several lines", th > 20 && tr.bottom == th && tr.right <= 40);
  RECT dr = {5, 5, 95, 45}; API(gdi32, SetBkMode)(dc, 1); API(user32, DrawTextW)(dc, W("Willows"), -1, &dr, 1 | 4 | 0x20);
  { int ink = 0; for(int y = 5; y < 45; y++) for(int x = 5; x < 95; x++) if(PIX(bits, 100, x, y) != 0xFFFFFF) ink++; check("DrawTextW draws text inside its rectangle", ink > 30); }
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, SaveDC)(dc); API(gdi32, IntersectClipRect)(dc, 0, 60, 100, 100); API(user32, DrawTextW)(dc, W("Willows"), -1, &dr, 1 | 4 | 0x20 | 0x100); API(gdi32, RestoreDC)(dc, -1);
  { int ink = 0; for(int y = 0; y < 100; y++) for(int x = 0; x < 100; x++) if(PIX(bits, 100, x, y) != 0xFFFFFF) ink++; check("text outside the clip rectangle is not drawn", ink == 0); }

  /* blits */
  void *sb = 0; HANDLE sdc = API(gdi32, CreateCompatibleDC)(0); HANDLE sbmp = API(gdi32, CreateDIBSection)(sdc, bmi32, 0, &sb, 0, 0); API(gdi32, SelectObject)(sdc, sbmp);
  for(int i = 0; i < 100 * 100; i++) ((U32*)sb)[i] = 0x80FF0000u;                                    /* red at 50% alpha */
  fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, AlphaBlend)(dc, 0, 0, 50, 50, sdc, 0, 0, 50, 50, (1u << 24) | (255u << 16));
  { U32 p = PIX(bits, 100, 10, 10); check("AlphaBlend with per-pixel alpha mixes red into white", ((p >> 16) & 255) == 255 && ((p >> 8) & 255) > 100 && ((p >> 8) & 255) < 160); }
  for(int i = 0; i < 100 * 100; i++) ((U32*)sb)[i] = 0xFFFF00FF; ((U32*)sb)[5 * 100 + 5] = 0xFF00FF00; fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, TransparentBlt)(dc, 0, 0, 10, 10, sdc, 0, 0, 10, 10, RGB(255, 0, 255));
  check("TransparentBlt skips the key colour", PIX(bits, 100, 2, 2) == 0xFFFFFF && PIX(bits, 100, 5, 5) == 0x00FF00);
  { unsigned char tv[32]; memset(tv, 0, sizeof tv); *(int*)(tv + 0) = 0; *(int*)(tv + 4) = 0; *(unsigned short*)(tv + 8) = 0xFF00; *(int*)(tv + 16) = 100; *(int*)(tv + 20) = 100; *(unsigned short*)(tv + 28) = 0xFF00; U32 mesh[2] = {0, 1};
    API(gdi32, GradientFill)(dc, tv, 2, mesh, 1, 0); U32 a = PIX(bits, 100, 1, 50), z = PIX(bits, 100, 98, 50), m = PIX(bits, 100, 50, 50);
    check("GradientFill: red on the left, blue on the right, a mix in the middle", ((a >> 16) & 255) > 240 && (z & 255) > 240 && ((m >> 16) & 255) > 100 && (m & 255) > 100); }
  /* a 24-bit bottom-up DIB section is converted when it is blitted */
  { void *b24 = 0; unsigned char bi24[40]; mkbmi(bi24, 8, 8, 24); HANDLE d24 = API(gdi32, CreateCompatibleDC)(0); HANDLE h24 = API(gdi32, CreateDIBSection)(d24, bi24, 0, &b24, 0, 0); API(gdi32, SelectObject)(d24, h24);
    unsigned char *p = b24; for(int y = 0; y < 8; y++) for(int x = 0; x < 8; x++){ unsigned char *q = p + y * 24 + x * 3; q[0] = 255; q[1] = 0; q[2] = 0; }           /* blue pixels (BGR), stored bottom-up */
    p[7 * 24 + 0] = 0; p[7 * 24 + 1] = 0; p[7 * 24 + 2] = 255;                                                                                                          /* the LAST row is the top row: red at its first pixel */
    fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); API(gdi32, BitBlt)(dc, 0, 0, 8, 8, d24, 0, 0, 0xCC0020); check("24-bit bottom-up DIB: top-left pixel is the last stored row", PIX(bits, 100, 0, 0) == 0xFF0000 && PIX(bits, 100, 1, 1) == 0x0000FF); }
  /* GetDIBits and SetDIBitsToDevice */
  { static unsigned char out[100 * 100 * 3]; unsigned char bi[40]; mkbmi(bi, 100, 100, 24); fill(dc, 0, 0, 100, 100, RGB(255, 255, 255)); fill(dc, 0, 0, 10, 10, RGB(255, 0, 0));
    int n = API(gdi32, GetDIBits)(dc, bmp, 0, 100, out, bi, 0); check("GetDIBits(24-bit) reads 100 rows", n == 100);
    check("GetDIBits is bottom-up: the first row is the bitmap's last, the red square is at the end", out[0] == 255 && out[1] == 255 && out[2] == 255 && out[(99 * 100) * 3 + 2] == 255 && out[(99 * 100) * 3 + 0] == 0);
    fill(dc, 0, 0, 100, 100, RGB(0, 0, 0)); API(gdi32, SetDIBitsToDevice)(dc, 0, 0, 100, 100, 0, 0, 0, 100, out, bi, 0); check("SetDIBitsToDevice keeps the top-left red square at the top", PIX(bits, 100, 2, 2) == 0xFF0000 && PIX(bits, 100, 50, 50) == 0xFFFFFF); }
}

/* ---- the showcase window ---- */
static HANDLE wnd;
static void showcase(HANDLE dc){
  RECT all = {0, 0, 640, 480}; HANDLE bg = API(gdi32, CreateSolidBrush)(RGB(245, 245, 250)); API(user32, FillRect)(dc, &all, bg); API(gdi32, DeleteObject)(bg);
  API(gdi32, SetBkMode)(dc, 1); API(gdi32, SelectObject)(dc, API(gdi32, CreatePen)(0, 2, RGB(40, 40, 120)));
  API(gdi32, SelectObject)(dc, API(gdi32, CreateSolidBrush)(RGB(120, 170, 255))); API(gdi32, Ellipse)(dc, 20, 20, 140, 120);
  API(gdi32, SelectObject)(dc, API(gdi32, CreateSolidBrush)(RGB(255, 190, 120))); API(gdi32, RoundRect)(dc, 160, 20, 300, 120, 30, 30);
  POINT star[10];
  { static const POINT pts[10] = {{360,20},{375,60},{415,60},{383,85},{395,125},{360,100},{325,125},{337,85},{305,60},{345,60}}; memcpy(star, pts, sizeof star); API(gdi32, SelectObject)(dc, API(gdi32, CreateSolidBrush)(RGB(255, 220, 90))); API(gdi32, Polygon)(dc, star, 10); }
  API(gdi32, SelectObject)(dc, API(gdi32, CreateSolidBrush)(RGB(150, 220, 150))); API(gdi32, Pie)(dc, 440, 20, 560, 120, 560, 70, 500, 20);
  POINT bz[7] = {{20, 200}, {60, 130}, {100, 270}, {140, 200}, {180, 130}, {220, 270}, {260, 200}}; API(gdi32, SelectObject)(dc, API(gdi32, CreatePen)(0, 3, RGB(200, 40, 80))); API(gdi32, PolyBezier)(dc, bz, 7);
  { unsigned char tv[32]; memset(tv, 0, sizeof tv); *(int*)(tv) = 300; *(int*)(tv + 4) = 150; *(unsigned short*)(tv + 8) = 0xFF00; *(int*)(tv + 16) = 560; *(int*)(tv + 20) = 200; *(unsigned short*)(tv + 28) = 0xFF00; U32 mesh[2] = {0, 1}; API(gdi32, GradientFill)(dc, tv, 2, mesh, 1, 0); }
  RECT tr = {20, 290, 300, 400}; API(gdi32, SetTextColor)(dc, RGB(30, 30, 60)); API(user32, DrawTextW)(dc, W("DrawTextW wraps this long sentence inside its rectangle, with word breaks and a second line.\nA new paragraph follows."), -1, &tr, 0x10);
  RECT cr = {320, 290, 620, 360}; API(gdi32, SelectObject)(dc, API(gdi32, CreateSolidBrush)(RGB(255, 255, 255))); API(user32, FillRect)(dc, &cr, API(gdi32, GetStockObject)(0)); API(gdi32, SaveDC)(dc); API(gdi32, IntersectClipRect)(dc, 340, 300, 480, 350);
  RECT big = {320, 295, 620, 360}; API(user32, DrawTextW)(dc, W("Clipped to a rectangle: only the middle of this long line is visible"), -1, &big, 0x20 | 0x100); API(gdi32, RestoreDC)(dc, -1);
  API(user32, DrawTextW)(dc, W("Centered"), -1, &(RECT){320, 365, 620, 420}, 1 | 4 | 0x20);
}
static LRESULT WINAPI wndproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  if(m == WM_PAINT){ PAINTSTRUCT ps; HANDLE dc = API(user32, BeginPaint)(h, &ps); if(dc) showcase(dc); API(user32, EndPaint)(h, &ps); return 0; }
  if(m == WM_DESTROY){ API(user32, PostQuitMessage)(0); return 0; }
  return API(user32, DefWindowProcA)(h, m, wp, lp);
}
int main(int argc, char **argv){
  if(argc > 1 && !strcmp(argv[1], "gdi")){
    HANDLE hi = API(kernel32, GetModuleHandleA)(0); WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = "WGDITEST"; wc.hCursor = API(user32, LoadCursorA)(0, (const char*)32512ull); API(user32, RegisterClassA)(&wc);
    wnd = API(user32, CreateWindowExA)(0, "WGDITEST", "GDI showcase", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 660, 470, 0, 0, hi, 0);
    MSG msg; while(API(user32, GetMessageA)(&msg, 0, 0, 0)){ API(user32, TranslateMessage)(&msg); API(user32, DispatchMessageA)(&msg); } return 0;
  }
  verbose = argc > 1; run_checks(); printf("\n%s: %d of %d checks passed\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", total - fails, total); return fails;
}
