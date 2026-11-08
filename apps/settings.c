/* Settings - keyboard layout, wallpaper themes and system information. Changes apply immediately and are saved to System\settings.cfg. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winui.h"

#define SIDE 190
static HANDLE hwnd; static int cw = 820, ch = 560, page, layout, theme, hover = -1;
static char test[160]; static int test_len; static HANDLE f_ui, f_big, f_small;
static const char *pages[] = { "System", "Keyboard", "Appearance" };
static const char *layouts[] = { "English (US)", "French (AZERTY)" };
static const char *layout_hint[] = { "QWERTY - the default layout", "AZERTY with AltGr, accented letters (é è à ù ç) and ²" };
static const char *tnames[] = { "Willow", "Forest", "Sunset", "Midnight", "Ocean", "Graphite" };
static const unsigned tcol[][3] = { {0x5C2A1A,0xED3A7C,0x26100B}, {0x354A12,0x4AA316,0x07160F}, {0x2E1D4A,0x4E4EE1,0x1F0814}, {0x1C1305,0x555F47,0x05060A}, {0x5C3A0B,0xE9A50E,0x06192B}, {0x312B2B,0x80899C,0x1A1A1D} };   /* BGR */

static void fill(HANDLE dc, int x, int y, int w, int h, unsigned c){ if(w <= 0 || h <= 0) return; RECT r = {x, y, x + w, y + h}; HANDLE b = API(gdi32, CreateSolidBrush)(c); API(user32, FillRect)(dc, &r, b); API(gdi32, DeleteObject)(b); }
static void text(HANDLE dc, HANDLE f, int x, int y, const char *s, unsigned c){ API(gdi32, SelectObject)(dc, f); API(gdi32, SetTextColor)(dc, c); API(gdi32, TextOutA)(dc, x, y, s, (int)strlen(s)); }
static HANDLE font(int px, int bold){ return API(gdi32, CreateFontA)(-px, 0, 0, 0, bold ? 700 : 400, 0, 0, 0, 0, 0, 0, 0, 0, "Open Sans"); }
static void card(HANDLE dc, int x, int y, int w, int h, int sel){ fill(dc, x, y, w, h, sel ? 0xE0A050 : 0xE5E1DC); fill(dc, x + 2, y + 2, w - 4, h - 4, sel ? 0xFCF3E8 : 0xFFFFFF); }
static void load_settings(void){
  char v[40]; layout = (API(kernel32, WillowsGetSetting)("layout", v, sizeof v) && (v[0] == 'a' || v[0] == 'A' || v[0] == 'f')) ? 1 : 0;
  theme = API(kernel32, WillowsGetSetting)("theme", v, sizeof v) ? atoi(v) : 0; if(theme < 0 || theme > 5) theme = 0;
}
static void set_layout(int l){ layout = l; API(kernel32, WillowsSetSetting)("layout", l ? "azerty" : "us"); test_len = 0; test[0] = 0; }
static void set_theme(int t){ theme = t; char v[8]; snprintf(v, sizeof v, "%d", t); API(kernel32, WillowsSetSetting)("theme", v); }
static void row(HANDLE dc, int x, int y, const char *k, const char *v){ text(dc, f_small, x, y, k, 0x6B7280); text(dc, f_ui, x + 150, y - 2, v, 0x1F2937); }

static void paint(HANDLE dc){
  API(gdi32, SetBkMode)(dc, 1);
  fill(dc, 0, 0, cw, ch, 0xF7F5F3); fill(dc, 0, 0, SIDE, ch, 0xF0EDEA); fill(dc, SIDE - 1, 0, 1, ch, 0xE0DCD8);
  text(dc, f_big, 20, 18, "Settings", 0x1F2937);
  for(int i = 0; i < 3; i++){ int y = 64 + i * 42; if(i == page) fill(dc, 10, y, SIDE - 20, 36, 0xF0DCC8); else if(i == hover) fill(dc, 10, y, SIDE - 20, 36, 0xE8E4E0); text(dc, f_ui, 26, y + 8, pages[i], i == page ? 0x8A4A12 : 0x374151); }
  int x = SIDE + 32; text(dc, f_big, x, 20, pages[page], 0x1F2937); int y = 76;
  if(page == 0){
    char b[96];
    text(dc, f_ui, x, y, "About this computer", 0x374151); y += 40;
    row(dc, x, y, "Operating system", "Willows 0.9"); y += 34;
    row(dc, x, y, "Architecture", "x86-64 (64-bit)"); y += 34;
    API(kernel32, WillowsGetGpuName)(b, sizeof b); row(dc, x, y, "Graphics", b); y += 34;
    int res = API(kernel32, WillowsGetResolution)(); snprintf(b, sizeof b, "%d x %d", res >> 16, res & 0xFFFF); row(dc, x, y, "Display", b); y += 34;
    unsigned long long tot = API(kernel32, WillowsGetMemory)(0), fr = API(kernel32, WillowsGetMemory)(1);
    snprintf(b, sizeof b, "%llu MB total, %llu MB free", tot >> 20, fr >> 20); row(dc, x, y, "Memory", b); y += 34;
    unsigned long long s = API(kernel32, GetTickCount64)() / 1000; snprintf(b, sizeof b, "%llu min %llu s", s / 60, s % 60); row(dc, x, y, "Uptime", b); y += 34;
    row(dc, x, y, "Keyboard", layouts[layout]); y += 34;
    row(dc, x, y, "Compatibility", "Runs Windows x64 programs (reports Windows 10)");
  } else if(page == 1){
    text(dc, f_ui, x, y, "Keyboard layout", 0x374151); y += 36;
    for(int i = 0; i < 2; i++){ card(dc, x, y, cw - x - 32, 64, layout == i); text(dc, f_ui, x + 18, y + 10, layouts[i], 0x1F2937); text(dc, f_small, x + 18, y + 36, layout_hint[i], 0x6B7280); if(layout == i) text(dc, f_ui, cw - 32 - 90, y + 20, "Active", 0x8A4A12); y += 76; }
    y += 14; text(dc, f_ui, x, y, "Try it here", 0x374151); y += 34; card(dc, x, y, cw - x - 32, 46, 0);
    char t[200]; snprintf(t, sizeof t, "%s|", test); text(dc, f_ui, x + 14, y + 10, t, 0x1F2937);
    y += 66; text(dc, f_small, x, y, "AZERTY: AltGr+é = ~   AltGr+\" = #   AltGr+' = {   AltGr+( = [   AltGr+- = |   AltGr+à = @", 0x6B7280);
  } else {
    text(dc, f_ui, x, y, "Wallpaper", 0x374151); y += 36; int cwid = 170, chh = 112;
    for(int i = 0; i < 6; i++){
      int cx = x + (i % 3) * (cwid + 16), cy = y + (i / 3) * (chh + 46);
      card(dc, cx - 4, cy - 4, cwid + 8, chh + 40, theme == i);
      fill(dc, cx, cy, cwid, chh / 2, tcol[i][2]); fill(dc, cx, cy + chh / 2, cwid, chh / 2, tcol[i][0]); fill(dc, cx + 18, cy + 22, 60, 60, tcol[i][1]); fill(dc, cx + 96, cy + 40, 56, 40, tcol[i][0]);
      text(dc, f_ui, cx + 4, cy + chh + 4, tnames[i], 0x1F2937);
    }
  }
}
static LRESULT WINAPI wndproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  switch(m){
    case WM_PAINT: { PAINTSTRUCT ps; HANDLE dc = API(user32, BeginPaint)(h, &ps); if(dc) paint(dc); API(user32, EndPaint)(h, &ps); return 0; }
    case WM_SIZE: cw = (int)(lp & 0xFFFF); ch = (int)((lp >> 16) & 0xFFFF); API(user32, InvalidateRect)(h, 0, 0); return 0;
    case WM_MOUSEMOVE: { int mx = (int)(lp & 0xFFFF), my = (int)((lp >> 16) & 0xFFFF), n = -1; if(mx < SIDE) for(int i = 0; i < 3; i++) if(my >= 64 + i * 42 && my < 100 + i * 42) n = i; if(n != hover){ hover = n; API(user32, InvalidateRect)(h, 0, 0); } return 0; }
    case WM_LBUTTONDOWN: {
      int mx = (int)(lp & 0xFFFF), my = (int)((lp >> 16) & 0xFFFF), x = SIDE + 32;
      if(mx < SIDE){ for(int i = 0; i < 3; i++) if(my >= 64 + i * 42 && my < 100 + i * 42) page = i; }
      else if(page == 1){ for(int i = 0; i < 2; i++){ int y = 112 + i * 76; if(mx >= x && mx < cw - 32 && my >= y && my < y + 64) set_layout(i); } }
      else if(page == 2){ for(int i = 0; i < 6; i++){ int cx = x + (i % 3) * 186, cy = 112 + (i / 3) * 158; if(mx >= cx - 4 && mx < cx + 174 && my >= cy - 4 && my < cy + 148) set_theme(i); } }
      API(user32, InvalidateRect)(h, 0, 0); return 0;
    }
    case WM_CHAR: {
      int c = (int)wp; if(page != 1) return 0;
      if(c == 8){ if(test_len){ do test_len--; while(test_len > 0 && (test[test_len] & 0xC0) == 0x80); test[test_len] = 0; } }
      else if(c >= 32 && c < 127 && test_len < 150) test[test_len++] = (char)c;
      else if(c >= 160 && c < 256 && test_len < 148){ test[test_len++] = (char)(0xC0 | (c >> 6)); test[test_len++] = (char)(0x80 | (c & 0x3F)); }
      test[test_len] = 0; API(user32, InvalidateRect)(h, 0, 0); return 0;
    }
    case WM_DESTROY: API(user32, PostQuitMessage)(0); return 0;
  }
  return API(user32, DefWindowProcA)(h, m, wp, lp);
}
int main(void){
  HANDLE hi = API(kernel32, GetModuleHandleA)(0); load_settings();
  f_ui = font(15, 0); f_big = font(24, 1); f_small = font(13, 0);
  WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = "WSETTINGS"; wc.hCursor = API(user32, LoadCursorA)(0, (const char*)32512ull);
  API(user32, RegisterClassA)(&wc);
  hwnd = API(user32, CreateWindowExA)(0, "WSETTINGS", "Settings", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 820, 560, 0, 0, hi, 0);
  MSG m; while(API(user32, GetMessageA)(&m, 0, 0, 0)){ API(user32, TranslateMessage)(&m); API(user32, DispatchMessageA)(&m); }
  return 0;
}
