/* Files - the file manager. Sidebar of places, a folder view with icons, navigation, and the usual actions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "winui.h"

#define SIDE 170
#define TOP 52
#define ROW 30
struct entry { char name[64]; int isdir; DWORD size; };
static struct entry ents[512]; static int nents, sel = -1, scroll;
static char cwd[200] = "/"; static HANDLE hwnd; static int cw = 800, ch = 500;
static int ren_mode; static char ren_buf[64]; static int ren_len;
static char hist[32][200]; static int nhist;
static HANDLE f_ui, f_ui_b, f_small;

static HANDLE font(int px, int bold){ return API(gdi32, CreateFontA)(-px, 0, 0, 0, bold ? 700 : 400, 0, 0, 0, 0, 0, 0, 0, 0, "Open Sans"); }
static void fill(HANDLE dc, int x, int y, int w, int h, unsigned c){ if(w <= 0 || h <= 0) return; RECT r = {x, y, x + w, y + h}; HANDLE b = API(gdi32, CreateSolidBrush)(c); API(user32, FillRect)(dc, &r, b); API(gdi32, DeleteObject)(b); }
static void text(HANDLE dc, HANDLE f, int x, int y, const char *s, int n, unsigned c){ API(gdi32, SelectObject)(dc, f); API(gdi32, SetTextColor)(dc, c); API(gdi32, TextOutA)(dc, x, y, s, n); }
static int twidth(HANDLE dc, HANDLE f, const char *s, int n){ SIZE z; API(gdi32, SelectObject)(dc, f); API(gdi32, GetTextExtentPoint32A)(dc, s, n, &z); return z.cx; }

static int strcasecmp_(const char *a, const char *b){ for(;; a++, b++){ int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if(x != y) return x - y; if(!x) return 0; } }
static int cmp(const void *a, const void *b){ const struct entry *x = a, *y = b; if(x->isdir != y->isdir) return y->isdir - x->isdir; return strcasecmp_(x->name, y->name); }
static void sort_ents(void){ for(int i = 1; i < nents; i++){ struct entry k = ents[i]; int j = i - 1; while(j >= 0 && cmp(&ents[j], &k) > 0){ ents[j + 1] = ents[j]; j--; } ents[j + 1] = k; } }
static void refresh(void){
  nents = 0; sel = -1; WIN32_FIND_DATAA fd; char pat[260]; snprintf(pat, sizeof pat, "%s%s*", cwd, cwd[1] ? "/" : "");
  HANDLE h = API(kernel32, FindFirstFileA)(pat, &fd);
  if(h != (HANDLE)~0ull){ do { if(nents < 512){ snprintf(ents[nents].name, 64, "%s", fd.name); ents[nents].isdir = (fd.attr & 0x10) != 0; ents[nents].size = fd.sizeLow; nents++; } } while(API(kernel32, FindNextFileA)(h, &fd)); API(kernel32, FindClose)(h); }
  sort_ents(); scroll = 0;
  { const char *leaf = cwd[1] ? strrchr(cwd, '/') + 1 : "This PC"; char t[260]; snprintf(t, sizeof t, "%s - Files", leaf); API(user32, SetWindowTextA)(hwnd, t); }
}
static void go(const char *p, int push){
  if(push && nhist < 32){ snprintf(hist[nhist++], 200, "%s", cwd); }
  snprintf(cwd, sizeof cwd, "%s", p); if(!cwd[0]) strcpy(cwd, "/"); refresh(); API(user32, InvalidateRect)(hwnd, 0, 0);
}
static void go_up(void){ if(!cwd[1]) return; char t[200]; snprintf(t, sizeof t, "%s", cwd); char *s = strrchr(t, '/'); if(s == t) t[1] = 0; else *s = 0; go(t, 1); }
static void go_back(void){ if(nhist){ char t[200]; snprintf(t, sizeof t, "%s", hist[--nhist]); go(t, 0); } }
static void full_path(const char *name, char *out, int max){ snprintf(out, max, "%s%s%s", cwd, cwd[1] ? "/" : "", name); }
static void rm_tree(const char *dir){
  char pat[300]; snprintf(pat, sizeof pat, "%s/*", dir); WIN32_FIND_DATAA fd; static char kid[64][64]; static DWORD ka[64]; int n = 0;
  HANDLE h = API(kernel32, FindFirstFileA)(pat, &fd);
  if(h != (HANDLE)~0ull){ do { if(n < 64){ snprintf(kid[n], 64, "%s", fd.name); ka[n++] = fd.attr; } } while(API(kernel32, FindNextFileA)(h, &fd)); API(kernel32, FindClose)(h); }
  for(int i = 0; i < n; i++){ char p2[400]; snprintf(p2, sizeof p2, "%s/%s", dir, kid[i]); if(ka[i] & 0x10) rm_tree(p2); else API(kernel32, DeleteFileA)(p2); }
  API(kernel32, RemoveDirectoryA)(dir);
}
static void open_sel(void){
  if(sel < 0) return; char p[300]; full_path(ents[sel].name, p, sizeof p);
  if(ents[sel].isdir) go(p, 1); else API(shell32, ShellExecuteA)(0, "open", p, 0, 0, 1);
}
static void new_folder(void){
  char name[64] = "New folder", p[300]; for(int k = 2; k < 50; k++){ full_path(name, p, sizeof p); if(API(kernel32, GetFileAttributesA)(p) == 0xFFFFFFFF) break; snprintf(name, sizeof name, "New folder (%d)", k); }
  full_path(name, p, sizeof p); API(kernel32, CreateDirectoryA)(p, 0); refresh();
  for(int i = 0; i < nents; i++) if(!strcmp(ents[i].name, name)){ sel = i; ren_mode = 1; snprintf(ren_buf, sizeof ren_buf, "%s", name); ren_len = (int)strlen(ren_buf); }
  API(user32, InvalidateRect)(hwnd, 0, 0);
}
static void delete_sel(void){
  if(sel < 0) return; char msg[160], p[300]; snprintf(msg, sizeof msg, "Delete \"%s\"?\nThis cannot be undone.", ents[sel].name);
  if(API(user32, MessageBoxA)(hwnd, msg, "Delete", 4) != 6) return;
  full_path(ents[sel].name, p, sizeof p); if(ents[sel].isdir) rm_tree(p); else API(kernel32, DeleteFileA)(p); refresh(); API(user32, InvalidateRect)(hwnd, 0, 0);
}
static void commit_rename(void){
  ren_mode = 0; if(sel < 0 || !ren_len || !strcmp(ren_buf, ents[sel].name)){ API(user32, InvalidateRect)(hwnd, 0, 0); return; }
  char a[300], b[300]; full_path(ents[sel].name, a, sizeof a); full_path(ren_buf, b, sizeof b); API(kernel32, MoveFileA)(a, b); refresh(); API(user32, InvalidateRect)(hwnd, 0, 0);
}

static const char *places[4] = {"Desktop", "Documents", "Downloads", "This PC"}; static const char *place_path[4] = {"/Desktop", "/Documents", "/Downloads", "/"};
static void button(HANDLE dc, int x, int y, int w, const char *label, int enabled){
  fill(dc, x, y, w, 30, enabled ? 0xE5E7EB : 0xF3F4F6); int tw = twidth(dc, f_ui, label, (int)strlen(label)); text(dc, f_ui, x + (w - tw) / 2, y + 4, label, (int)strlen(label), enabled ? 0x1F2937 : 0xB0B5BD);
}
static void paint(HANDLE dc){
  API(gdi32, SetBkMode)(dc, 1);
  fill(dc, 0, 0, cw, ch, 0xFFFFFF);
  /* sidebar */
  fill(dc, 0, 0, SIDE, ch, 0xF3F4F6); fill(dc, SIDE - 1, 0, 1, ch, 0xE1E4E8);
  text(dc, f_ui_b, 16, 16, "PLACES", 6, 0x6B7280);
  for(int i = 0; i < 4; i++){
    int y = 44 + i * 38; int act = !strcmp(cwd, place_path[i]);
    if(act) fill(dc, 8, y, SIDE - 16, 32, 0xDDE8FB);
    fill(dc, 20, y + 8, 18, 16, i == 3 ? 0x9CA3AF : 0x3DB8F5 /* BGR amber */); text(dc, f_ui, 48, y + 5, places[i], (int)strlen(places[i]), act ? 0xB5560F : 0x1F2937);
  }
  /* toolbar */
  fill(dc, SIDE, 0, cw - SIDE, TOP, 0xFFFFFF); fill(dc, SIDE, TOP - 1, cw - SIDE, 1, 0xE1E4E8);
  button(dc, SIDE + 10, 11, 34, "<", nhist > 0); button(dc, SIDE + 48, 11, 34, "^", cwd[1] != 0);
  int px = SIDE + 90, pw = cw - px - 360; if(pw < 120) pw = 120;
  fill(dc, px, 11, pw, 30, 0xF3F4F6);
  if(ren_mode){ char t[100]; snprintf(t, sizeof t, "Rename to:  %s|", ren_buf); text(dc, f_ui, px + 10, 15, t, (int)strlen(t), 0xB5560F); }
  else { char t[240]; snprintf(t, sizeof t, "C:%s", cwd[1] ? cwd : "\\"); for(char *s = t; *s; s++) if(*s == '/') *s = '\\'; text(dc, f_ui, px + 10, 15, t, (int)strlen(t), 0x1F2937); }
  int bx = px + pw + 10; button(dc, bx, 11, 96, "New folder", 1); button(dc, bx + 100, 11, 76, "Rename", sel >= 0); button(dc, bx + 180, 11, 70, "Delete", sel >= 0);
  /* header */
  int lx = SIDE, y0 = TOP; fill(dc, lx, y0, cw - lx, 26, 0xFAFAFB);
  text(dc, f_small, lx + 52, y0 + 5, "Name", 4, 0x6B7280); text(dc, f_small, cw - 190, y0 + 5, "Size", 4, 0x6B7280); text(dc, f_small, cw - 110, y0 + 5, "Type", 4, 0x6B7280);
  /* rows */
  for(int i = 0; i < nents; i++){
    int y = y0 + 26 + (i - scroll) * ROW; if(y + ROW < y0 + 26 || y > ch) continue;
    struct entry *e = &ents[i];
    if(i == sel) fill(dc, lx + 6, y, cw - lx - 12, ROW - 2, 0xDDE8FB);
    if(e->isdir){ fill(dc, lx + 18, y + 8, 12, 4, 0x2A92D9); fill(dc, lx + 18, y + 11, 26, 16, 0x57C8FF); }
    else { fill(dc, lx + 22, y + 5, 18, 21, 0xD6CAC3); fill(dc, lx + 23, y + 6, 16, 19, 0xFBF9F8); fill(dc, lx + 26, y + 11, 10, 2, 0xD6CAC3); fill(dc, lx + 26, y + 16, 10, 2, 0xD6CAC3); }
    if(i == sel && ren_mode){ char t[80]; snprintf(t, sizeof t, "%s|", ren_buf); fill(dc, lx + 50, y + 3, 260, ROW - 8, 0xFFFFFF); text(dc, f_ui, lx + 56, y + 5, t, (int)strlen(t), 0x1F2937); }
    else text(dc, f_ui, lx + 52, y + 5, e->name, (int)strlen(e->name), 0x1F2937);
    if(!e->isdir){ char s[24]; if(e->size < 1024) snprintf(s, sizeof s, "%u B", e->size); else snprintf(s, sizeof s, "%u KB", e->size / 1024); text(dc, f_small, cw - 190, y + 7, s, (int)strlen(s), 0x6B7280); }
    const char *ty = e->isdir ? "Folder" : (strstr(e->name, ".exe") ? "Program" : "File"); text(dc, f_small, cw - 110, y + 7, ty, (int)strlen(ty), 0x6B7280);
  }
  if(!nents) text(dc, f_ui, lx + 20, y0 + 50, "This folder is empty.", 21, 0x9CA3AF);
}
static int row_at(int my){ int r = (my - TOP - 26) / ROW + scroll; return (my >= TOP + 26 && r >= 0 && r < nents) ? r : -1; }
static LRESULT WINAPI wndproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  static unsigned last_click_t; static int last_i = -2;
  switch(m){
    case WM_PAINT: { PAINTSTRUCT ps; HANDLE dc = API(user32, BeginPaint)(h, &ps); if(dc) paint(dc); API(user32, EndPaint)(h, &ps); return 0; }
    case WM_SIZE: cw = (int)(lp & 0xFFFF); ch = (int)((lp >> 16) & 0xFFFF); API(user32, InvalidateRect)(h, 0, 0); return 0;
    case WM_MOUSEWHEEL: { int d = (short)((wp >> 16) & 0xFFFF); scroll -= d / 120 * 2; int max = nents - (ch - TOP - 26) / ROW; if(scroll > max) scroll = max; if(scroll < 0) scroll = 0; API(user32, InvalidateRect)(h, 0, 0); return 0; }
    case WM_LBUTTONDOWN: {
      int mx = (int)(lp & 0xFFFF), my = (int)((lp >> 16) & 0xFFFF);
      if(ren_mode) commit_rename();
      if(mx < SIDE){ for(int i = 0; i < 4; i++){ int y = 44 + i * 38; if(my >= y && my < y + 32) go(place_path[i], 1); } return 0; }
      if(my < TOP){
        int px = SIDE + 90, pw = cw - px - 360; if(pw < 120) pw = 120, (void)pw; int bx = px + pw + 10;
        if(mx >= SIDE + 10 && mx < SIDE + 44) go_back(); else if(mx >= SIDE + 48 && mx < SIDE + 82) go_up();
        else if(mx >= bx && mx < bx + 96) new_folder();
        else if(mx >= bx + 100 && mx < bx + 176 && sel >= 0){ ren_mode = 1; snprintf(ren_buf, sizeof ren_buf, "%s", ents[sel].name); ren_len = (int)strlen(ren_buf); API(user32, InvalidateRect)(h, 0, 0); }
        else if(mx >= bx + 180 && mx < bx + 250) delete_sel();
        return 0;
      }
      int r = row_at(my); sel = r; unsigned now = (unsigned)API(kernel32, GetTickCount64)();
      if(r >= 0 && r == last_i && now - last_click_t < 450){ last_i = -2; open_sel(); } else { last_i = r; last_click_t = now; }
      API(user32, InvalidateRect)(h, 0, 0); return 0;
    }
    case WM_CHAR: {
      int c = (int)wp;
      if(ren_mode){ if(c == 13) commit_rename(); else if(c == 27){ ren_mode = 0; API(user32, InvalidateRect)(h, 0, 0); } else if(c == 8){ if(ren_len) ren_buf[--ren_len] = 0; API(user32, InvalidateRect)(h, 0, 0); }
        else if(c >= 32 && c < 127 && ren_len < 60 && !strchr("/\\:*?\"<>|", c)){ ren_buf[ren_len++] = (char)c; ren_buf[ren_len] = 0; API(user32, InvalidateRect)(h, 0, 0); } return 0; }
      if(c == 13) open_sel(); else if(c == 8) go_up();
      return 0;
    }
    case WM_KEYDOWN: {
      int vk = (int)wp; if(ren_mode) return 0;
      if(vk == 0x28 && sel + 1 < nents) sel++; else if(vk == 0x26 && sel > 0) sel--; else if(vk == 0x26 && sel < 0 && nents) sel = 0;
      else if(vk == 0x2E) delete_sel(); else return 0;
      API(user32, InvalidateRect)(h, 0, 0); return 0;
    }
    case WM_DESTROY: API(user32, PostQuitMessage)(0); return 0;
  }
  return API(user32, DefWindowProcA)(h, m, wp, lp);
}
int main(int argc, char **argv){
  HANDLE hi = API(kernel32, GetModuleHandleA)(0);
  WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = "WFILES"; wc.hCursor = API(user32, LoadCursorA)(0, (const char*)32512ull);
  API(user32, RegisterClassA)(&wc);
  hwnd = API(user32, CreateWindowExA)(0, "WFILES", "Files", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 860, 540, 0, 0, hi, 0);
  f_ui = font(14, 0); f_ui_b = font(12, 1); f_small = font(13, 0);
  if(argc > 1){ snprintf(cwd, sizeof cwd, "%s", argv[1]); for(char *s = cwd; *s; s++) if(*s == '\\') *s = '/'; if(cwd[0] != '/'){ memmove(cwd + 1, cwd, strlen(cwd) + 1); cwd[0] = '/'; } int l = (int)strlen(cwd); if(l > 1 && cwd[l - 1] == '/') cwd[l - 1] = 0; }
  if(API(kernel32, GetFileAttributesA)(cwd) != 0x10) strcpy(cwd, "/");
  refresh();
  MSG m; while(API(user32, GetMessageA)(&m, 0, 0, 0)){ API(user32, TranslateMessage)(&m); API(user32, DispatchMessageA)(&m); }
  return 0;
}
