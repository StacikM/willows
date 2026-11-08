/* Notes - a small text editor. Opens the file given on the command line (a double-click on the desktop passes it). Ctrl+S saves. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "winui.h"

static HANDLE hwnd, hedit; static long long prev_edit_proc; static char path[260]; static int modified;

static void set_title(void){
  const char *leaf = path[0] ? path : "Untitled"; for(const char *p = path; *p; p++) if(*p == '/' || *p == '\\') leaf = p + 1;
  char t[300]; snprintf(t, sizeof t, "%s%s - Notes", modified ? "* " : "", leaf); API(user32, SetWindowTextA)(hwnd, t);
}
static int load(const char *p){
  HANDLE h = API(kernel32, CreateFileA)(p, 0x80000000u, 1, 0, 3, 0, 0); if(h == (HANDLE)~0ull) return 0;
  DWORD sz = API(kernel32, GetFileSize)(h, 0), got = 0; char *b = malloc(sz + 1); if(sz) API(kernel32, ReadFile)(h, b, sz, &got, 0);
  API(kernel32, CloseHandle)(h); b[got] = 0; API(user32, SetWindowTextA)(hedit, b); free(b); return 1;
}
static int save(void){
  if(!path[0]){                                                         /* Save As */
    char name[260] = "Untitled.txt"; unsigned char ofn[160]; memset(ofn, 0, sizeof ofn); *(DWORD*)ofn = 152; *(char**)(ofn + 48) = name; *(DWORD*)(ofn + 56) = sizeof name;
    if(!API(comdlg32, GetSaveFileNameA)(ofn)) return 0; snprintf(path, sizeof path, "%s", name);
  }
  int n = API(user32, GetWindowTextLengthA)(hedit); char *b = malloc(n + 2); API(user32, GetWindowTextA)(hedit, b, n + 1);
  HANDLE h = API(kernel32, CreateFileA)(path, 0x40000000u, 0, 0, 2, 0, 0); if(h == (HANDLE)~0ull){ free(b); API(user32, MessageBoxA)(hwnd, "Cannot save the file.", "Notes", 0); return 0; }
  DWORD w; API(kernel32, WriteFile)(h, b, n, &w, 0); API(kernel32, CloseHandle)(h); free(b); modified = 0; set_title(); return 1;
}
static LRESULT WINAPI edit_proc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  if(m == WM_CHAR){ if(wp == 19){ save(); return 0; } if(wp >= 32 || wp == 8 || wp == 13 || wp == 9){ if(!modified){ modified = 1; set_title(); } } }
  if(m == WM_KEYDOWN && wp == 0x2E && !modified){ modified = 1; set_title(); }
  return API(user32, CallWindowProcA)(prev_edit_proc, h, m, wp, lp);
}
static LRESULT WINAPI wndproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  switch(m){
    case WM_SIZE: { int w = (int)(lp & 0xFFFF), hh = (int)((lp >> 16) & 0xFFFF); if(hedit) API(user32, MoveWindow)(hedit, 0, 0, w, hh, 1); return 0; }
    case 7: if(hedit) API(user32, SetFocus)(hedit); return 0;           /* WM_SETFOCUS */
    case WM_CLOSE:
      if(modified){ int r = API(user32, MessageBoxA)(h, "Save changes before closing?", "Notes", 4); if(r == 6 && !save()) return 0; }
      return API(user32, DefWindowProcA)(h, m, wp, lp);
    case WM_DESTROY: API(user32, PostQuitMessage)(0); return 0;
  }
  return API(user32, DefWindowProcA)(h, m, wp, lp);
}
int main(int argc, char **argv){
  HANDLE hi = API(kernel32, GetModuleHandleA)(0);
  WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = "WNOTES"; wc.hCursor = API(user32, LoadCursorA)(0, (const char*)32512ull);
  API(user32, RegisterClassA)(&wc);
  hwnd = API(user32, CreateWindowExA)(0, "WNOTES", "Notes", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 760, 520, 0, 0, hi, 0);
  hedit = API(user32, CreateWindowExA)(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE, 0, 0, 758, 480, hwnd, (HANDLE)1, hi, 0);
  prev_edit_proc = API(user32, SetWindowLongPtrA)(hedit, -4, (long long)edit_proc);
  if(argc > 1){ snprintf(path, sizeof path, "%s", argv[1]); if(!load(path)){ /* new file */ } }
  set_title(); API(user32, SetFocus)(hedit);
  MSG m; while(API(user32, GetMessageA)(&m, 0, 0, 0)){ API(user32, TranslateMessage)(&m); API(user32, DispatchMessageA)(&m); }
  return 0;
}
