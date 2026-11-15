/* Files - the file manager.
   Everything is drawn into an off-screen 32-bit buffer (anti-aliased shapes, icons drawn in code, GDI text)
   and copied to the window with one BitBlt, so it never flickers.
   Top bar: back / forward / up, a clickable path (click empty space to type one), search.
   Command bar: new folder, rename, delete, list / grid view.  Right-click for a context menu. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "winui.h"

typedef unsigned int u32; typedef unsigned long long u64;

/* ---- layout ---- */
#define TB 52            /* top bar */
#define CB 44            /* command bar */
#define SW 212           /* sidebar width */
#define STH 30           /* status bar */
#define HDR 34           /* column header */
#define ROW 32
#define TILE_W 112
#define TILE_H 118

/* ---- colours, 0xRRGGBB ---- */
#define C_BG     0xFFFFFF
#define C_SIDE   0xF5F6F8
#define C_LINE   0xE6E8ED
#define C_TEXT   0x1C2230
#define C_MUTED  0x6B7385
#define C_FAINT  0xA6ADBA
#define C_ACCENT 0x2F6FEB
#define C_SEL    0xDDE8FD
#define C_SELHOT 0xCFDFFC
#define C_HOT    0xEFF1F5
#define C_FIELD  0xF0F2F5

/* ---------------------------------------------------------------- drawing ---- */
static u32 *px; static int BW, BH; static HANDLE mdc, mbmp;
static int clx0, cly0, clx1, cly1;
static void noclip(void){ clx0 = 0; cly0 = 0; clx1 = BW; cly1 = BH; }
static void setclip(int x, int y, int w, int h){ clx0 = x < 0 ? 0 : x; cly0 = y < 0 ? 0 : y; clx1 = x + w > BW ? BW : x + w; cly1 = y + h > BH ? BH : y + h; }
static float fsqrt(float v){ float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(v)); return r; }
static float fabs_(float v){ return v < 0 ? -v : v; }
static float fmax_(float a, float b){ return a > b ? a : b; }
static float fmin_(float a, float b){ return a < b ? a : b; }
static u32 mix(u32 a, u32 b, int t){        /* t = 0..255 of b */
  int r = ((a >> 16 & 255) * (255 - t) + (b >> 16 & 255) * t) / 255, g = ((a >> 8 & 255) * (255 - t) + (b >> 8 & 255) * t) / 255, bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
  return (u32)(r << 16 | g << 8 | bl);
}
static void put(int x, int y, u32 c, int a){
  if(a <= 0 || x < clx0 || y < cly0 || x >= clx1 || y >= cly1) return;
  u32 *p = &px[y * BW + x]; *p = 0xFF000000u | (a >= 255 ? c : mix(*p & 0xFFFFFF, c, a));
}
static void rect(int x, int y, int w, int h, u32 c){
  int x0 = x < clx0 ? clx0 : x, y0 = y < cly0 ? cly0 : y, x1 = x + w > clx1 ? clx1 : x + w, y1 = y + h > cly1 ? cly1 : y + h;
  for(int j = y0; j < y1; j++){ u32 *p = &px[j * BW]; for(int i = x0; i < x1; i++) p[i] = 0xFF000000u | c; }
}
/* rounded box with a vertical gradient (c1 top, c2 bottom), anti-aliased on every edge */
static void rrg(float x, float y, float w, float h, float r, u32 c1, u32 c2, int alpha){
  if(w <= 0 || h <= 0) return; if(r > w / 2) r = w / 2; if(r > h / 2) r = h / 2;
  float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
  int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w + 1), y1 = (int)(y + h + 1);
  for(int j = y0; j < y1; j++){
    float qy = fabs_(j + 0.5f - cy) - hy; u32 c = c1 == c2 ? c1 : mix(c1, c2, (int)((j - y) * 255 / h) < 0 ? 0 : (int)((j - y) * 255 / h) > 255 ? 255 : (int)((j - y) * 255 / h));
    for(int i = x0; i < x1; i++){
      float qx = fabs_(i + 0.5f - cx) - hx, d;
      if(qx > 0 && qy > 0) d = fsqrt(qx * qx + qy * qy) - r; else d = fmax_(qx, qy) - r;
      float cov = 0.5f - d; if(cov <= 0) continue; if(cov > 1) cov = 1;
      put(i, j, c, (int)(cov * alpha));
    }
  }
}
static void rr(float x, float y, float w, float h, float r, u32 c){ rrg(x, y, w, h, r, c, c, 255); }
static void rra(float x, float y, float w, float h, float r, u32 c, int a){ rrg(x, y, w, h, r, c, c, a); }
/* filled box with a border */
static void rbox(float x, float y, float w, float h, float r, u32 fill, u32 border, float bw){ rr(x, y, w, h, r, border); rr(x + bw, y + bw, w - 2 * bw, h - 2 * bw, r - bw > 0 ? r - bw : 0, fill); }
static void disc(float cx, float cy, float rad, u32 c, int a){
  for(int j = (int)(cy - rad - 1); j <= (int)(cy + rad + 1); j++) for(int i = (int)(cx - rad - 1); i <= (int)(cx + rad + 1); i++){
    float dx = i + 0.5f - cx, dy = j + 0.5f - cy, cov = rad + 0.5f - fsqrt(dx * dx + dy * dy); if(cov <= 0) continue; if(cov > 1) cov = 1; put(i, j, c, (int)(cov * a)); }
}
static void ring(float cx, float cy, float rad, float t, u32 c){
  for(int j = (int)(cy - rad - t); j <= (int)(cy + rad + t); j++) for(int i = (int)(cx - rad - t); i <= (int)(cx + rad + t); i++){
    float dx = i + 0.5f - cx, dy = j + 0.5f - cy, cov = t / 2 + 0.5f - fabs_(fsqrt(dx * dx + dy * dy) - rad); if(cov <= 0) continue; if(cov > 1) cov = 1; put(i, j, c, (int)(cov * 255)); }
}
/* a line with round ends, thickness t */
static void seg(float ax, float ay, float bx, float by, float t, u32 c){
  float vx = bx - ax, vy = by - ay, ll = vx * vx + vy * vy;
  for(int j = (int)(fmin_(ay, by) - t - 1); j <= (int)(fmax_(ay, by) + t + 1); j++) for(int i = (int)(fmin_(ax, bx) - t - 1); i <= (int)(fmax_(ax, bx) + t + 1); i++){
    float px_ = i + 0.5f - ax, py_ = j + 0.5f - ay, k = ll > 0 ? (px_ * vx + py_ * vy) / ll : 0; if(k < 0) k = 0; if(k > 1) k = 1;
    float dx = px_ - k * vx, dy = py_ - k * vy, cov = t / 2 + 0.5f - fsqrt(dx * dx + dy * dy); if(cov <= 0) continue; if(cov > 1) cov = 1; put(i, j, c, (int)(cov * 255)); }
}
static void shadow(float x, float y, float w, float h, float r){ for(int i = 10; i >= 1; i--) rra(x - i, y - i + 4, w + 2 * i, h + 2 * i, r + i, 0x0B1220, 5); }

/* ---- text (GDI into the buffer's DC) ---- */
static HANDLE f_reg, f_semi, f_small, f_badge, f_big; static int h_reg, h_semi, h_small, h_badge, h_big;
static HANDLE mkfont(int px_, int weight){ return API(gdi32, CreateFontA)(-px_, 0, 0, 0, weight, 0, 0, 0, 0, 0, 0, 5, 0, "Open Sans"); }
static int fheight(HANDLE f){ TEXTMETRICA tm; API(gdi32, SelectObject)(mdc, f); API(gdi32, GetTextMetricsA)(mdc, &tm); return tm.tmHeight; }
static int tw(HANDLE f, const char *s, int n){ SIZE z; if(n < 0) n = (int)strlen(s); if(!n) return 0; API(gdi32, SelectObject)(mdc, f); API(gdi32, GetTextExtentPoint32A)(mdc, s, n, &z); return z.cx; }
static int fh(HANDLE f){ return f == f_reg ? h_reg : f == f_semi ? h_semi : f == f_small ? h_small : f == f_badge ? h_badge : h_big; }
static void txt(int x, int y, HANDLE f, const char *s, int n, u32 c){ if(n < 0) n = (int)strlen(s); if(!n) return; API(gdi32, SelectObject)(mdc, f); API(gdi32, SetTextColor)(mdc, RGB(c >> 16 & 255, c >> 8 & 255, c & 255)); API(gdi32, TextOutA)(mdc, x, y, s, n); }
/* text vertically centred on cy, cut with "..." to fit maxw; returns the width drawn */
static int txtc(int x, int cy, int maxw, HANDLE f, const char *s, u32 c){
  int n = (int)strlen(s), w = tw(f, s, n), y = cy - fh(f) / 2;
  if(maxw > 0 && w > maxw){ char b[300]; int dots = tw(f, "...", 3); while(n > 0 && tw(f, s, n) + dots > maxw) n--; if(n > 290) n = 290; memcpy(b, s, n); memcpy(b + n, "...", 4); txt(x, y, f, b, -1, c); return tw(f, b, -1); }
  txt(x, y, f, s, n, c); return w;
}
static void txt_mid(int cx, int cy, int maxw, HANDLE f, const char *s, u32 c){ int w = tw(f, s, -1); if(w > maxw) w = maxw; txtc(cx - w / 2, cy, maxw, f, s, c); }

/* ---------------------------------------------------------------- the folder ---- */
enum { K_FOLDER, K_FILE, K_APP, K_LIB, K_TEXT, K_IMAGE, K_ARCHIVE, K_WEB, K_CODE, K_FONT, K_PDF, K_AUDIO, K_VIDEO };
static const struct { const char *exts, *label; u32 color; } kinds[] = {
  {"", "File folder", 0xF2B33D}, {"", "File", 0x8A93A3}, {"exe com msi", "Application", 0x2F6FEB}, {"dll sys ocx drv", "Application extension", 0x7D8796},
  {"txt md log ini cfg conf json xml csv inf nfo", "Text document", 0x5B6B82}, {"png jpg jpeg bmp gif ico webp svg tga", "Image", 0x1F9D63},
  {"zip 7z gz tar xz rar cab bz2 zst", "Compressed archive", 0x8B5CF6}, {"htm html css js mjs", "Web page", 0xEE6A2C},
  {"c h cpp hpp py sh s asm rs go java lua bat ps1", "Source code", 0x0C95D6}, {"ttf otf woff woff2 fon", "Font", 0xD946A8},
  {"pdf", "PDF document", 0xE5484D}, {"mp3 wav ogg flac m4a mid", "Audio", 0xE0922F}, {"mp4 mkv avi webm mov", "Video", 0xC53D88},
};
struct entry { char name[128]; char ext[8]; int isdir, kind; u64 size, mtime; };
static struct entry ents[1024]; static int nents;
static int vis[1024], nvis, sel = -1, anchor_vis = -1;
static char cwd[260] = "/";
static char back_st[32][260], fwd_st[32][260]; static int nback, nfwd;
static int sort_col, sort_desc, view_grid;
static char q[64]; static int qlen, search_focus;
static int edit; static char ebuf[200]; static int elen; enum { E_NONE, E_RENAME, E_PATH };
static int scroll; static HANDLE hwnd; static int cw = 900, ch = 560;

static int ci_cmp(const char *a, const char *b){ for(;; a++, b++){ int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if(x != y) return x - y; if(!x) return 0; } }
static int ci_has(const char *hay, const char *nd){ int n = (int)strlen(nd); if(!n) return 1; for(; *hay; hay++){ int i = 0; while(i < n && tolower((unsigned char)hay[i]) == tolower((unsigned char)nd[i])) i++; if(i == n) return 1; } return 0; }
static int word_in(const char *list, const char *w){ int n = (int)strlen(w); if(!n) return 0; for(const char *s = list; *s;){ const char *e = s; while(*e && *e != ' ') e++; if(e - s == n && !strncmp(s, w, n)) return 1; s = *e ? e + 1 : e; } return 0; }
static void classify(struct entry *e){
  e->ext[0] = 0; e->kind = e->isdir ? K_FOLDER : K_FILE; if(e->isdir) return;
  const char *dot = strrchr(e->name, '.'); if(!dot || dot == e->name || strlen(dot + 1) > 6) return;
  int i = 0; for(const char *s = dot + 1; *s && i < 7; s++) e->ext[i++] = (char)tolower((unsigned char)*s); e->ext[i] = 0;
  for(int k = K_APP; k < (int)(sizeof kinds / sizeof *kinds); k++) if(word_in(kinds[k].exts, e->ext)){ e->kind = k; return; }
}
static void type_label(const struct entry *e, char *o, int n){
  if(e->kind != K_FILE){ snprintf(o, n, "%s", kinds[e->kind].label); return; }
  if(e->ext[0]){ char u[8]; int i = 0; for(; e->ext[i]; i++) u[i] = (char)toupper((unsigned char)e->ext[i]); u[i] = 0; snprintf(o, n, "%s file", u); } else snprintf(o, n, "File");
}
static void fmt_size(u64 s, char *o, int n){
  if(s < 1024) snprintf(o, n, "%u bytes", (unsigned)s);
  else if(s < 1024 * 1024) snprintf(o, n, "%u KB", (unsigned)((s + 1023) / 1024));
  else if(s < (u64)1 << 30) snprintf(o, n, "%u.%u MB", (unsigned)(s >> 20), (unsigned)((s & 0xFFFFF) * 10 >> 20));
  else snprintf(o, n, "%u.%u GB", (unsigned)(s >> 30), (unsigned)((s & 0x3FFFFFFF) * 10 >> 30));
}
static void fmt_date(u64 ft, char *o, int n){
  static const char *mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  o[0] = 0; if(ft < 116444736000000000ull) return; long long s = (long long)(ft / 10000000ull) - 11644473600ll;
  long long days = s / 86400; int sec = (int)(s % 86400);
  long long z = days + 719468, era = z / 146097; unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = (int)(yoe + era * 400); unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9; y += m <= 2;
  snprintf(o, n, "%u %s %d, %02d:%02d", d, mon[m - 1], y, sec / 3600, sec / 60 % 60);
}
static int ent_cmp(const struct entry *a, const struct entry *b){
  if(a->isdir != b->isdir) return b->isdir - a->isdir;
  int r = 0;
  if(sort_col == 1) r = a->mtime < b->mtime ? -1 : a->mtime > b->mtime;
  else if(sort_col == 2){ char x[48], y[48]; type_label(a, x, 48); type_label(b, y, 48); r = ci_cmp(x, y); }
  else if(sort_col == 3) r = a->size < b->size ? -1 : a->size > b->size;
  if(!r) r = ci_cmp(a->name, b->name); else if(sort_desc) r = -r;
  return sort_col == 0 && sort_desc ? -r : r;
}
static void sort_ents(void){ for(int i = 1; i < nents; i++){ struct entry k = ents[i]; int j = i - 1; while(j >= 0 && ent_cmp(&ents[j], &k) > 0){ ents[j + 1] = ents[j]; j--; } ents[j + 1] = k; } }
static void refilter(void){ nvis = 0; for(int i = 0; i < nents; i++) if(ci_has(ents[i].name, q)) vis[nvis++] = i; if(sel >= nvis) sel = nvis - 1; }
static const char *leaf_name(void){ return cwd[1] ? strrchr(cwd, '/') + 1 : "This PC"; }
static void invalidate(void){ API(user32, InvalidateRect)(hwnd, 0, 0); }
static void reload(const char *keep){
  nents = 0; WIN32_FIND_DATAA fd; char pat[300]; snprintf(pat, sizeof pat, "%s%s*", cwd, cwd[1] ? "/" : "");
  HANDLE h = API(kernel32, FindFirstFileA)(pat, &fd);
  if(h != (HANDLE)~0ull){ do { if(nents < 1024 && strcmp(fd.name, ".") && strcmp(fd.name, "..")){ struct entry *e = &ents[nents++]; snprintf(e->name, sizeof e->name, "%s", fd.name); e->isdir = (fd.attr & 0x10) != 0; e->size = ((u64)fd.sizeHigh << 32) | fd.sizeLow; e->mtime = ((u64)fd.wt[1] << 32) | fd.wt[0]; classify(e); } } while(API(kernel32, FindNextFileA)(h, &fd)); API(kernel32, FindClose)(h); }
  sort_ents(); refilter(); sel = -1;
  if(keep) for(int i = 0; i < nvis; i++) if(!strcmp(ents[vis[i]].name, keep)) sel = i;
  char t[300]; snprintf(t, sizeof t, "%s - Files", leaf_name()); API(user32, SetWindowTextA)(hwnd, t);
}
static void full_path(const char *name, char *out, int max){ snprintf(out, max, "%s%s%s", cwd, cwd[1] ? "/" : "", name); }
static void go(const char *p, int push_back){
  if(push_back){ if(!strcmp(p, cwd)) return; if(nback == 32){ memmove(back_st[0], back_st[1], 31 * 260); nback--; } snprintf(back_st[nback++], 260, "%s", cwd); nfwd = 0; }
  char prev[260]; snprintf(prev, sizeof prev, "%s", cwd);
  snprintf(cwd, sizeof cwd, "%s", p[0] ? p : "/"); int l = (int)strlen(cwd); if(l > 1 && cwd[l - 1] == '/') cwd[l - 1] = 0;
  qlen = 0; q[0] = 0; search_focus = 0; edit = E_NONE; scroll = 0;
  /* going up selects the folder we came from */
  const char *keep = 0; int pl = (int)strlen(cwd); if(!strncmp(prev, cwd, pl) && (prev[pl] == '/' || (pl == 1 && prev[1]))) keep = prev + (pl == 1 ? 1 : pl + 1);
  char kb[128] = ""; if(keep){ snprintf(kb, sizeof kb, "%s", keep); char *s = strchr(kb, '/'); if(s) *s = 0; }
  reload(kb[0] ? kb : 0); invalidate();
}
static void go_back(void){ if(!nback) return; snprintf(fwd_st[nfwd++ % 32], 260, "%s", cwd); char t[260]; snprintf(t, sizeof t, "%s", back_st[--nback]); go(t, 0); }
static void go_fwd(void){ if(!nfwd) return; if(nback < 32) snprintf(back_st[nback++], 260, "%s", cwd); char t[260]; snprintf(t, sizeof t, "%s", fwd_st[--nfwd]); go(t, 0); }
static void go_up(void){ if(!cwd[1]) return; char t[260]; snprintf(t, sizeof t, "%s", cwd); char *s = strrchr(t, '/'); if(s == t) t[1] = 0; else *s = 0; go(t, 1); }
static struct entry *cur_ent(void){ return sel >= 0 && sel < nvis ? &ents[vis[sel]] : 0; }

static void rm_tree(const char *dir, int depth){
  if(depth > 24) return;
  char pat[300]; snprintf(pat, sizeof pat, "%s/*", dir); WIN32_FIND_DATAA fd; char (*kid)[128] = malloc(256 * 128); DWORD ka[256]; int n = 0; if(!kid) return;
  HANDLE h = API(kernel32, FindFirstFileA)(pat, &fd);
  if(h != (HANDLE)~0ull){ do { if(n < 256 && strcmp(fd.name, ".") && strcmp(fd.name, "..")){ snprintf(kid[n], 128, "%s", fd.name); ka[n++] = fd.attr; } } while(API(kernel32, FindNextFileA)(h, &fd)); API(kernel32, FindClose)(h); }
  for(int i = 0; i < n; i++){ char p2[400]; snprintf(p2, sizeof p2, "%s/%s", dir, kid[i]); if(ka[i] & 0x10) rm_tree(p2, depth + 1); else API(kernel32, DeleteFileA)(p2); }
  free(kid); API(kernel32, RemoveDirectoryA)(dir);
}
static void open_sel(void){
  struct entry *e = cur_ent(); if(!e) return; char p[400]; full_path(e->name, p, sizeof p);
  if(e->isdir) go(p, 1); else API(shell32, ShellExecuteA)(hwnd, "open", p, 0, 0, 1);
}
static void start_rename(void){ struct entry *e = cur_ent(); if(!e) return; edit = E_RENAME; snprintf(ebuf, sizeof ebuf, "%s", e->name); elen = (int)strlen(ebuf); search_focus = 0; invalidate(); }
static void new_folder(void){
  char name[64] = "New folder", p[400];
  for(int k = 2; k < 100; k++){ full_path(name, p, sizeof p); if(API(kernel32, GetFileAttributesA)(p) == 0xFFFFFFFF) break; snprintf(name, sizeof name, "New folder (%d)", k); }
  full_path(name, p, sizeof p); API(kernel32, CreateDirectoryA)(p, 0);
  qlen = 0; q[0] = 0; reload(name); start_rename();
}
static void delete_sel(void){
  struct entry *e = cur_ent(); if(!e) return; char msg[200], p[400];
  snprintf(msg, sizeof msg, "Delete %s \"%s\"?\n\nThis can't be undone.", e->isdir ? "the folder" : "", e->name);
  if(API(user32, MessageBoxA)(hwnd, msg, "Delete", 0x34) != 6) return;
  int was = sel; full_path(e->name, p, sizeof p); if(e->isdir) rm_tree(p, 0); else API(kernel32, DeleteFileA)(p);
  reload(0); sel = was < nvis ? was : nvis - 1; invalidate();
}
static void commit_edit(void){
  int k = edit; edit = E_NONE;
  if(k == E_RENAME){ struct entry *e = cur_ent(); if(e && elen && strcmp(ebuf, e->name)){ char a[400], b[400]; full_path(e->name, a, sizeof a); full_path(ebuf, b, sizeof b);
      if(!API(kernel32, MoveFileA)(a, b)) API(user32, MessageBoxA)(hwnd, "Couldn't rename the item. The name may already be taken.", "Rename", 0x30); else { char keep[200]; snprintf(keep, sizeof keep, "%s", ebuf); reload(keep); } } }
  else if(k == E_PATH){
    char t[260]; int j = 0; const char *s = ebuf; if((s[0] == 'C' || s[0] == 'c') && s[1] == ':') s += 2;
    for(; *s && j < 258; s++) t[j++] = *s == '\\' ? '/' : *s; t[j] = 0; if(t[0] != '/'){ memmove(t + 1, t, strlen(t) + 1); t[0] = '/'; }
    if(!ci_cmp(ebuf, "This PC")) strcpy(t, "/");
    DWORD a = API(kernel32, GetFileAttributesA)(t);
    if(a != 0xFFFFFFFF && (a & 0x10)) go(t, 1); else if(a != 0xFFFFFFFF) API(shell32, ShellExecuteA)(hwnd, "open", t, 0, 0, 1);
    else { char m[320]; snprintf(m, sizeof m, "Can't find \"%s\".", ebuf); API(user32, MessageBoxA)(hwnd, m, "Files", 0x30); }
  }
  invalidate();
}

/* ---------------------------------------------------------------- hit areas ---- */
enum { H_NONE, H_BACK, H_FWD, H_UP, H_REFRESH, H_CRUMB, H_PATH, H_SEARCH, H_QCLEAR, H_NEW, H_RENAME, H_DELETE, H_VLIST, H_VGRID, H_PLACE, H_COL, H_THUMB, H_MENU, H_ITEM, H_CONTENT };
struct hit { int x, y, w, h, id, arg; }; static struct hit hits[128]; static int nhits;
static int hot_id, hot_arg = -1, press_id, press_arg;
static void addhit(int x, int y, int w, int h, int id, int arg){ if(nhits < 128) hits[nhits++] = (struct hit){x, y, w, h, id, arg}; }
static int ishot(int id, int arg){ return hot_id == id && hot_arg == arg; }

/* geometry of the content area */
static int cx0(void){ return SW; } static int cy0(void){ return TB + CB; } static int cy1(void){ return ch - STH; }
static int list_top(void){ return cy0() + (view_grid ? 8 : HDR + 4); }
static int grid_cols(void){ int n = (cw - SW - 24) / TILE_W; return n < 1 ? 1 : n; }
static int content_h(void){ return view_grid ? ((nvis + grid_cols() - 1) / grid_cols()) * TILE_H + 12 : nvis * ROW + 8; }
static int view_h(void){ return cy1() - list_top(); }
static void clamp_scroll(void){ int m = content_h() - view_h(); if(scroll > m) scroll = m; if(scroll < 0) scroll = 0; }
static void item_rect(int i, int *x, int *y, int *w, int *h){
  if(view_grid){ int c = grid_cols(), gx = SW + 12 + ((cw - SW - 24) - c * TILE_W) / 2; *x = gx + (i % c) * TILE_W + 3; *y = list_top() + (i / c) * TILE_H - scroll + 2; *w = TILE_W - 6; *h = TILE_H - 4; }
  else { *x = SW + 8; *y = list_top() + i * ROW - scroll; *w = cw - SW - 16; *h = ROW - 2; }
}
static int item_at(int mx, int my){
  if(mx < SW || my < list_top() || my >= cy1()) return -1;
  for(int i = 0; i < nvis; i++){ int x, y, w, h; item_rect(i, &x, &y, &w, &h); if(mx >= x && mx < x + w && my >= y && my < y + h) return i; if(y > cy1()) break; }
  return -1;
}
static void ensure_visible(int i){
  if(i < 0) return; int x, y, w, h; item_rect(i, &x, &y, &w, &h);
  if(y < list_top()) scroll -= list_top() - y; else if(y + h > cy1()) scroll += y + h - cy1() + 4; clamp_scroll();
}

/* ---------------------------------------------------------------- icons ---- */
static void ic_folder(float x, float y, float s){
  float r = s * 0.08f; if(r < 1.6f) r = 1.6f;
  rr(x + s * 0.06f, y + s * 0.16f, s * 0.4f, s * 0.2f, r, 0xDB9628);
  rr(x + s * 0.06f, y + s * 0.22f, s * 0.88f, s * 0.64f, r, 0xE3A233);
  rrg(x + s * 0.06f, y + s * 0.33f, s * 0.88f, s * 0.53f, r, 0xFFD86E, 0xF7BA3E, 255);
  if(s >= 40) rra(x + s * 0.06f + r, y + s * 0.33f + 1, s * 0.88f - 2 * r, 1.2f, 0.5f, 0xFFF1C4, 200);
}
static void ic_file(float x, float y, float s, const struct entry *e){
  u32 col = kinds[e->kind].color;
  if(e->kind == K_APP){        /* a little window */
    rrg(x + s * 0.08f, y + s * 0.14f, s * 0.84f, s * 0.72f, s * 0.12f, mix(col, 0xFFFFFF, 60), col, 255);
    rra(x + s * 0.16f, y + s * 0.34f, s * 0.68f, s * 0.44f, s * 0.05f, 0xFFFFFF, 235);
    float d = s * 0.045f; if(d < 0.9f) d = 0.9f; for(int k = 0; k < 3; k++) disc(x + s * 0.2f + k * s * 0.09f, y + s * 0.24f, d, 0xFFFFFF, 220);
    return;
  }
  float pw = s * 0.66f, ph = s * 0.86f, px_ = x + (s - pw) / 2, py_ = y + s * 0.07f, r = s * 0.07f; if(r < 1.5f) r = 1.5f;
  rbox(px_, py_, pw, ph, r, 0xFFFFFF, 0xC3C9D3, s >= 40 ? 1.2f : 1.0f);
  float fold = s * 0.2f; rr(px_ + pw - fold, py_, fold, fold, r * 0.6f, 0xE4E8EE);        /* folded corner */
  if(e->kind == K_IMAGE){ rr(px_ + pw * 0.16f, py_ + ph * 0.4f, pw * 0.68f, ph * 0.42f, r * 0.6f, mix(col, 0xFFFFFF, 170)); disc(px_ + pw * 0.38f, py_ + ph * 0.54f, s * 0.05f + 0.5f, col, 255); seg(px_ + pw * 0.24f, py_ + ph * 0.76f, px_ + pw * 0.56f, py_ + ph * 0.6f, s * 0.05f + 0.6f, col); seg(px_ + pw * 0.56f, py_ + ph * 0.6f, px_ + pw * 0.76f, py_ + ph * 0.74f, s * 0.05f + 0.6f, col); }
  else { int lines = s >= 40 ? 4 : 2; for(int k = 0; k < lines; k++){ float ly = py_ + ph * (0.34f + k * (s >= 40 ? 0.1f : 0.2f)), lw = pw * (k == lines - 1 ? 0.4f : 0.62f); rr(px_ + pw * 0.18f, ly, lw, s >= 40 ? 1.6f : 1.2f, 0.6f, s >= 40 ? 0xD5DAE2 : mix(col, 0xFFFFFF, 90)); } }
  if(s >= 40 && e->ext[0]){        /* coloured label with the extension */
    char u[8]; int i = 0; for(; e->ext[i] && i < 4; i++) u[i] = (char)toupper((unsigned char)e->ext[i]); u[i] = 0;
    int w = tw(f_badge, u, -1) + 10; float bx = x + s * 0.08f, by = py_ + ph * 0.6f;
    rr(bx, by, w, h_badge + 3, 3, col); txt((int)bx + 5, (int)by + 1, f_badge, u, -1, 0xFFFFFF);
  } else if(s < 40) rr(px_ + 1, py_ + ph - 4, pw - 2, 3, 1, col);
}
static void ic_entry(float x, float y, float s, const struct entry *e){ if(e->isdir) ic_folder(x, y, s); else ic_file(x, y, s, e); }
static void ic_pc(float x, float y, float s){ rr(x + s * 0.06f, y + s * 0.14f, s * 0.88f, s * 0.58f, s * 0.1f, 0x3B4A63); rrg(x + s * 0.12f, y + s * 0.2f, s * 0.76f, s * 0.46f, s * 0.05f, 0x60A5FA, 0x2563EB, 255); rr(x + s * 0.4f, y + s * 0.72f, s * 0.2f, s * 0.1f, 0, 0x3B4A63); rr(x + s * 0.26f, y + s * 0.8f, s * 0.48f, s * 0.08f, s * 0.04f, 0x3B4A63); }
static void ic_disk(float x, float y, float s){ rrg(x + s * 0.04f, y + s * 0.28f, s * 0.92f, s * 0.46f, s * 0.12f, 0x9AA4B5, 0x6E7889, 255); rr(x + s * 0.12f, y + s * 0.58f, s * 0.5f, s * 0.06f, 1, 0x4B5565); disc(x + s * 0.8f, y + s * 0.6f, s * 0.06f + 0.4f, 0x34D399, 255); }
static void ic_desktop(float x, float y, float s){ rrg(x + s * 0.06f, y + s * 0.12f, s * 0.88f, s * 0.62f, s * 0.1f, 0x4F8DF7, 0x2B63D9, 255); rr(x + s * 0.12f, y + s * 0.6f, s * 0.76f, s * 0.08f, 0.5f, 0x1E4FB8); rr(x + s * 0.3f, y + s * 0.8f, s * 0.4f, s * 0.08f, s * 0.04f, 0x6B7385); }
static void ic_docs(float x, float y, float s){ rrg(x + s * 0.16f, y + s * 0.06f, s * 0.68f, s * 0.88f, s * 0.1f, 0x7C93B4, 0x55698A, 255); for(int k = 0; k < 3; k++) rr(x + s * 0.3f, y + s * (0.32f + k * 0.17f), s * (k == 2 ? 0.26f : 0.4f), 1.4f, 0.6f, 0xFFFFFF); }
static void ic_down(float x, float y, float s){ disc(x + s / 2, y + s / 2, s * 0.45f, 0x18A058, 255); seg(x + s / 2, y + s * 0.27f, x + s / 2, y + s * 0.7f, 1.7f, 0xFFFFFF); seg(x + s * 0.32f, y + s * 0.52f, x + s / 2, y + s * 0.7f, 1.7f, 0xFFFFFF); seg(x + s * 0.68f, y + s * 0.52f, x + s / 2, y + s * 0.7f, 1.7f, 0xFFFFFF); }
static void ic_win(float x, float y, float s){ rrg(x + s * 0.08f, y + s * 0.08f, s * 0.84f, s * 0.84f, s * 0.16f, 0x7C5CF0, 0x5B3FD6, 255); float g = s * 0.06f; for(int k = 0; k < 4; k++) rr(x + s * 0.26f + (k & 1) * (s * 0.24f + g), y + s * 0.26f + (k >> 1) * (s * 0.24f + g), s * 0.21f, s * 0.21f, 1, 0xFFFFFF); }

/* ---------------------------------------------------------------- painting ---- */
static const char *places[] = {"Desktop", "Documents", "Downloads", "Windows", "Local Disk (C:)"};
static const char *place_path[] = {"/Desktop", "/Documents", "/Downloads", "/Windows", "/"};
#define NPLACES 5
static void nav_button(int x, int id, int enabled, int glyph){
  int y = 9, s = 34; if(enabled && ishot(id, 0)) rr(x, y, s, s, 7, press_id == id ? 0xE2E6EC : C_HOT); addhit(x, y, s, s, id, 0);
  u32 c = enabled ? C_TEXT : 0xC5CAD3; float cx = x + s / 2.0f, cy = y + s / 2.0f, t = 1.7f;
  if(glyph == 0){ seg(cx + 6, cy, cx - 6, cy, t, c); seg(cx - 6, cy, cx - 1, cy - 5, t, c); seg(cx - 6, cy, cx - 1, cy + 5, t, c); }
  else if(glyph == 1){ seg(cx - 6, cy, cx + 6, cy, t, c); seg(cx + 6, cy, cx + 1, cy - 5, t, c); seg(cx + 6, cy, cx + 1, cy + 5, t, c); }
  else if(glyph == 2){ seg(cx, cy + 6, cx, cy - 6, t, c); seg(cx, cy - 6, cx - 5, cy - 1, t, c); seg(cx, cy - 6, cx + 5, cy - 1, t, c); }
  else { u32 bg = ishot(id, 0) ? (press_id == id ? 0xE2E6EC : C_HOT) : C_BG; ring(cx, cy, 6, t, c); rect((int)cx + 1, (int)cy - 9, 9, 8, bg); seg(cx + 6, cy - 1, cx + 3, cy - 4.5f, t, c); seg(cx + 6, cy - 1, cx + 8.8f, cy - 4.5f, t, c); }
}
static void draw_topbar(void){
  rect(0, 0, cw, TB, C_BG);
  nav_button(8, H_BACK, nback > 0, 0); nav_button(44, H_FWD, nfwd > 0, 1); nav_button(80, H_UP, cwd[1] != 0, 2); nav_button(116, H_REFRESH, 1, 3);
  int sw_ = cw > 760 ? 260 : cw > 560 ? 180 : 0, bx = 158, bw = cw - bx - (sw_ ? sw_ + 22 : 12), by = 9, bh = 34;
  /* path */
  if(edit == E_PATH){
    rbox(bx, by, bw, bh, 7, C_BG, C_ACCENT, 1.6f); setclip(bx + 4, by, bw - 8, bh);
    int w = txtc(bx + 12, by + bh / 2, 0, f_reg, ebuf, C_TEXT); rect(bx + 13 + w, by + 9, 1, bh - 18, C_TEXT); noclip();
    addhit(bx, by, bw, bh, H_PATH, 0);
  } else {
    rr(bx, by, bw, bh, 7, ishot(H_PATH, 0) ? 0xE9ECF0 : C_FIELD); addhit(bx, by, bw, bh, H_PATH, 0);
    if(cwd[1]) ic_folder(bx + 8, by + 7, 20); else ic_pc(bx + 8, by + 7, 20);
    /* segments: "This PC", then each folder; drop leading ones that don't fit */
    const char *segs[32]; int lens[32], n = 0; segs[n] = "This PC"; lens[n++] = 7;
    for(const char *s = cwd + 1; *s && n < 32;){ const char *e = strchr(s, '/'); int l = e ? (int)(e - s) : (int)strlen(s); segs[n] = s; lens[n++] = l; s += l; if(*s) s++; }
    int avail = bw - 50, total = 0, first = n - 1;
    for(int i = n - 1; i >= 0; i--){ int w = tw(f_reg, segs[i], lens[i]) + 26; if(total + w > avail && i != n - 1) break; total += w; first = i; }
    int x = bx + 36; char tmp[130];
    if(first > 0){ txtc(x + 2, by + bh / 2, 0, f_reg, "...", C_MUTED); x += 22; }
    for(int i = first; i < n; i++){
      int l = lens[i] > 128 ? 128 : lens[i]; memcpy(tmp, segs[i], l); tmp[l] = 0; int w = tw(f_reg, tmp, l), last = i == n - 1;
      if(x + w + 12 > bx + bw) w = bx + bw - x - 12;
      if(ishot(H_CRUMB, i) && !last) rr(x - 1, by + 4, w + 12, bh - 8, 5, 0xDCE1E8);
      addhit(x - 1, by + 4, w + 12, bh - 8, H_CRUMB, i);
      txtc(x + 5, by + bh / 2, w, f_reg, tmp, last ? C_TEXT : C_MUTED); x += w + 12;
      if(!last){ float cx = x + 6, cy = by + bh / 2.0f; seg(cx - 2, cy - 4, cx + 2, cy, 1.3f, C_FAINT); seg(cx + 2, cy, cx - 2, cy + 4, 1.3f, C_FAINT); x += 14; }
    }
  }
  /* search */
  if(sw_){
    int sx = cw - sw_ - 12;
    if(search_focus) rbox(sx, by, sw_, bh, 7, C_BG, C_ACCENT, 1.6f); else rr(sx, by, sw_, bh, 7, ishot(H_SEARCH, 0) ? 0xE9ECF0 : C_FIELD);
    addhit(sx, by, sw_, bh, H_SEARCH, 0);
    ring(sx + 17, by + 15.5f, 5, 1.5f, C_MUTED); seg(sx + 20.6f, by + 19.2f, sx + 24, by + 22.6f, 1.6f, C_MUTED);
    char ph[160]; snprintf(ph, sizeof ph, "Search %s", leaf_name());
    setclip(sx + 30, by, sw_ - 58, bh);
    int w = txtc(sx + 34, by + bh / 2, sw_ - 64, f_reg, qlen ? q : ph, qlen ? C_TEXT : C_FAINT); noclip();
    if(search_focus){ int cxp = qlen ? sx + 35 + w : sx + 34; rect(cxp, by + 9, 1, bh - 18, C_TEXT); }
    if(qlen){ int qx = sx + sw_ - 28; if(ishot(H_QCLEAR, 0)) disc(qx + 10, by + bh / 2.0f, 10, 0xDDE1E7, 255); seg(qx + 6.5f, by + 13.5f, qx + 13.5f, by + 20.5f, 1.4f, C_MUTED); seg(qx + 13.5f, by + 13.5f, qx + 6.5f, by + 20.5f, 1.4f, C_MUTED); addhit(qx, by + 7, 20, 20, H_QCLEAR, 0); }
  }
}
static int cmd_button(int x, int id, const char *label, int enabled, int glyph){
  int w = tw(f_reg, label, -1) + 44, y = TB + 6, h = 32; if(enabled && ishot(id, 0)) rr(x, y, w, h, 7, press_id == id ? 0xE2E6EC : C_HOT); addhit(x, y, w, h, id, 0);
  u32 c = enabled ? C_TEXT : C_FAINT, ac = enabled ? C_ACCENT : C_FAINT; float gx = x + 19, gy = y + h / 2.0f;
  if(glyph == 0){ ic_folder(gx - 9, gy - 10, 19); disc(gx + 6, gy + 5, 5, enabled ? 0x18A058 : C_FAINT, 255); seg(gx + 6, gy + 2.6f, gx + 6, gy + 7.4f, 1.3f, 0xFFFFFF); seg(gx + 3.6f, gy + 5, gx + 8.4f, gy + 5, 1.3f, 0xFFFFFF); }
  else if(glyph == 1){ seg(gx - 5, gy + 5, gx + 4, gy - 4, 3.2f, ac); seg(gx - 6.2f, gy + 6.2f, gx - 5, gy + 5, 1.6f, c); rr(gx - 7, gy + 7, 14, 1.4f, 0.5f, c); }
  else { rr(gx - 5, gy - 3, 10, 11, 2, enabled ? 0xE5484D : C_FAINT); rr(gx - 7, gy - 6, 14, 2, 1, enabled ? 0xE5484D : C_FAINT); rr(gx - 2, gy - 8, 4, 2, 1, enabled ? 0xE5484D : C_FAINT); rr(gx - 2.4f, gy - 1, 1.2f, 7, 0.4f, 0xFFFFFF); rr(gx + 1.2f, gy - 1, 1.2f, 7, 0.4f, 0xFFFFFF); }
  txtc(x + 34, y + h / 2, 0, f_reg, label, c); return w;
}
static void draw_cmdbar(void){
  rect(0, TB, cw, CB, C_BG); rect(0, TB + CB - 1, cw, 1, C_LINE);
  int x = 10, has = cur_ent() != 0;
  x += cmd_button(x, H_NEW, "New folder", 1, 0) + 4; rect(x + 2, TB + 13, 1, 18, C_LINE); x += 9;
  x += cmd_button(x, H_RENAME, "Rename", has, 1) + 2; cmd_button(x, H_DELETE, "Delete", has, 2);
  /* view switch: a pill with two segments */
  int vx = cw - 86, vy = TB + 7; rr(vx, vy, 76, 30, 8, C_FIELD); addhit(vx, vy, 38, 30, H_VLIST, 0); addhit(vx + 38, vy, 38, 30, H_VGRID, 0);
  if(!view_grid) rbox(vx + 3, vy + 3, 35, 24, 6, C_BG, 0xDADFE6, 1); else rbox(vx + 38, vy + 3, 35, 24, 6, C_BG, 0xDADFE6, 1);
  { u32 c = !view_grid ? C_ACCENT : C_MUTED; for(int k = 0; k < 3; k++){ disc(vx + 13, vy + 10 + k * 5, 1.2f, c, 255); rr(vx + 16.5f, vy + 9.3f + k * 5, 10, 1.5f, 0.6f, c); } }
  { u32 c = view_grid ? C_ACCENT : C_MUTED; for(int k = 0; k < 4; k++) rr(vx + 49 + (k & 1) * 8, vy + 8 + (k >> 1) * 8, 6.5f, 6.5f, 1.5f, c); }
}
static void draw_sidebar(void){
  int y0 = TB + CB; rect(0, y0, SW, ch - y0 - STH, C_SIDE); rect(SW - 1, y0, 1, ch - y0 - STH, C_LINE);
  int y = y0 + 14;
  for(int i = 0; i < NPLACES; i++){
    if(i == 0){ txt(20, y, f_small, "Quick access", -1, C_MUTED); y += h_small + 8; }
    if(i == 4){ y += 10; rect(16, y, SW - 32, 1, C_LINE); y += 12; txt(20, y, f_small, "This PC", -1, C_MUTED); y += h_small + 8; }
    int act = !strcmp(cwd, place_path[i]), hot = ishot(H_PLACE, i);
    if(act) rr(10, y, SW - 20, 34, 7, hot ? C_SELHOT : C_SEL); else if(hot) rr(10, y, SW - 20, 34, 7, 0xE9ECF1);
    if(act) rr(10, y + 9, 3.5f, 16, 1.75f, C_ACCENT);
    float ix = 24, iy = y + 7;
    if(i == 0) ic_desktop(ix, iy, 20); else if(i == 1) ic_docs(ix, iy, 20); else if(i == 2) ic_down(ix, iy, 20); else if(i == 3) ic_win(ix, iy, 20); else ic_disk(ix, iy, 20);
    txtc(54, y + 17, SW - 66, act ? f_semi : f_reg, places[i], C_TEXT);
    addhit(10, y, SW - 20, 34, H_PLACE, i); y += 36;
  }
}
static void draw_status(void){
  int y = ch - STH; rect(0, y, cw, STH, C_BG); rect(0, y, cw, 1, C_LINE);
  char t[200]; int n = snprintf(t, sizeof t, "%d item%s", nvis, nvis == 1 ? "" : "s");
  if(qlen) n += snprintf(t + n, sizeof t - n, " matching \"%s\"", q);
  struct entry *e = cur_ent();
  if(e){ n += snprintf(t + n, sizeof t - n, "      1 item selected"); if(!e->isdir){ char s[32]; fmt_size(e->size, s, 32); snprintf(t + n, sizeof t - n, "   %s", s); } }
  txtc(16, y + STH / 2 + 1, cw - 32, f_small, t, C_MUTED);
}
static void draw_rename_field(int x, int cy, int w){
  rbox(x - 4, cy - 13, w, 26, 5, C_BG, C_ACCENT, 1.5f); setclip(x, cy - 13, w - 8, 26);
  int tw_ = txtc(x + 2, cy, 0, f_reg, ebuf, C_TEXT); rect(x + 3 + tw_, cy - 8, 1, 16, C_TEXT); noclip();
}
static void draw_list(void){
  int x0 = SW, y0 = cy0(), W = cw - SW;
  /* columns: name | date modified | type | size */
  int show_date = W > 620, show_type = W > 430;
  int c_size = cw - 96, c_type = c_size - (show_type ? 160 : 0), c_date = c_type - (show_date ? 170 : 0), name_w = c_date - (x0 + 52) - 12;
  setclip(x0, list_top() - 4, W, cy1() - list_top() + 4);
  for(int i = 0; i < nvis; i++){
    int x, y, w, h; item_rect(i, &x, &y, &w, &h); if(y + h < list_top() - 4) continue; if(y > cy1()) break;
    struct entry *e = &ents[vis[i]]; int hot = ishot(H_ITEM, i) && !edit;
    if(i == sel) rr(x, y, w, h, 6, hot ? C_SELHOT : C_SEL); else if(hot) rr(x, y, w, h, 6, C_HOT);
    ic_entry(x + 12, y + 5, 20, e); int cy = y + h / 2;
    if(i == sel && edit == E_RENAME) draw_rename_field(x0 + 52, cy, name_w + 8);
    else txtc(x0 + 52, cy, name_w, f_reg, e->name, C_TEXT);
    char b[48];
    if(show_date){ fmt_date(e->mtime, b, sizeof b); txtc(c_date, cy, 160, f_small, b, C_MUTED); }
    if(show_type){ type_label(e, b, sizeof b); txtc(c_type, cy, 150, f_small, b, C_MUTED); }
    if(!e->isdir){ fmt_size(e->size, b, sizeof b); int sw_ = tw(f_small, b, -1); txtc(cw - 24 - sw_, cy, 0, f_small, b, C_MUTED); }
  }
  noclip();
  /* header (drawn after the rows so scrolled rows slide under it) */
  rect(x0, y0, W, HDR, C_BG); rect(x0 + 12, y0 + HDR - 1, W - 24, 1, C_LINE);
  struct { const char *l; int x, w, col; } cols[4] = {{"Name", x0 + 8, c_date - x0 - 8, 0}, {"Date modified", c_date - 8, 170, 1}, {"Type", c_type - 8, 160, 2}, {"Size", c_size - 8, cw - c_size, 3}};
  for(int k = 0; k < 4; k++){
    if((k == 1 && !show_date) || (k == 2 && !show_type)) continue;
    int hx = cols[k].x, hw = cols[k].w; if(ishot(H_COL, k)) rr(hx, y0 + 4, hw - 4, HDR - 9, 5, C_HOT); addhit(hx, y0 + 4, hw - 4, HDR - 9, H_COL, k);
    int lx = k == 0 ? x0 + 52 : hx + 8; int w = txtc(lx, y0 + HDR / 2 - 1, 0, sort_col == k ? f_semi : f_small, cols[k].l, sort_col == k ? C_TEXT : C_MUTED);
    if(sort_col == k){ float ax = lx + w + 9, ay = y0 + HDR / 2.0f - 1; if(sort_desc){ seg(ax - 3.5f, ay - 1.8f, ax, ay + 1.8f, 1.3f, C_MUTED); seg(ax, ay + 1.8f, ax + 3.5f, ay - 1.8f, 1.3f, C_MUTED); } else { seg(ax - 3.5f, ay + 1.8f, ax, ay - 1.8f, 1.3f, C_MUTED); seg(ax, ay - 1.8f, ax + 3.5f, ay + 1.8f, 1.3f, C_MUTED); } }
  }
}
static void draw_grid(void){
  setclip(SW, cy0(), cw - SW, cy1() - cy0());
  for(int i = 0; i < nvis; i++){
    int x, y, w, h; item_rect(i, &x, &y, &w, &h); if(y + h < cy0()) continue; if(y > cy1()) break;
    struct entry *e = &ents[vis[i]]; int hot = ishot(H_ITEM, i) && !edit;
    if(i == sel) rbox(x, y, w, h, 9, hot ? C_SELHOT : C_SEL, 0xB9D0FA, 1); else if(hot) rr(x, y, w, h, 9, C_HOT);
    ic_entry(x + (w - 60) / 2.0f, y + 10, 60, e);
    if(i == sel && edit == E_RENAME) draw_rename_field(x + 6, y + 90, w - 4);
    else {
      /* up to two lines, centred */
      const char *s = e->name; int n = (int)strlen(s), maxw = w - 12, cut = n;
      if(tw(f_reg, s, n) > maxw){ cut = n; while(cut > 1 && tw(f_reg, s, cut) > maxw) cut--; int sp = cut; while(sp > 0 && s[sp] != ' ' && s[sp] != '.' && s[sp] != '-' && s[sp] != '_') sp--; if(sp > cut / 2) cut = sp; }
      char l1[130]; int c1 = cut > 128 ? 128 : cut; memcpy(l1, s, c1); l1[c1] = 0;
      if(cut >= n) txt_mid(x + w / 2, y + 88, maxw, f_reg, l1, C_TEXT);
      else { const char *r = s + cut; while(*r == ' ') r++; txt_mid(x + w / 2, y + 80, maxw, f_reg, l1, C_TEXT); txt_mid(x + w / 2, y + 80 + h_reg, maxw, f_reg, r, C_TEXT); }
    }
  }
  noclip();
}
static void draw_empty(void){
  int cx = SW + (cw - SW) / 2, cy = cy0() + (cy1() - cy0()) / 2 - 30;
  if(qlen){ ring(cx, cy - 4, 18, 3, 0xC9CED6); seg(cx + 13, cy + 9, cx + 22, cy + 18, 3.5f, 0xC9CED6); char t[120]; snprintf(t, sizeof t, "No results for \"%s\"", q); txt_mid(cx, cy + 40, cw - SW - 40, f_semi, t, C_TEXT); txt_mid(cx, cy + 62, cw - SW - 40, f_small, "Try a different name.", C_MUTED); }
  else { ic_folder(cx - 32, cy - 36, 64); txt_mid(cx, cy + 40, 400, f_semi, "This folder is empty", C_TEXT); txt_mid(cx, cy + 62, 400, f_small, "Right-click to create a new folder.", C_MUTED); }
}
static void draw_scrollbar(void){
  int top = list_top(), vh = view_h(), chh = content_h(); if(chh <= vh) return;
  int th = vh * vh / chh; if(th < 30) th = 30; int ty = top + (vh - th) * scroll / (chh - vh);
  int hot = ishot(H_THUMB, 0) || press_id == H_THUMB, w = hot ? 7 : 5;
  rr(cw - 4 - w, ty + 2, w, th - 4, w / 2.0f, hot ? 0x9CA3AF : 0xC9CED6); addhit(cw - 14, ty, 14, th, H_THUMB, 0);
}

/* ---- context menu ---- */
enum { M_OPEN = 1, M_RENAME, M_DELETE, M_NEW, M_REFRESH, M_LIST, M_GRID, M_PATH, M_UP };
static int menu_open, menu_x, menu_y, menu_n; static struct { const char *l; int cmd, on; } menu_it[10];
#define MROW 32
static void menu_add(const char *l, int cmd, int on){ if(menu_n < 10){ menu_it[menu_n].l = l; menu_it[menu_n].cmd = cmd; menu_it[menu_n].on = on; menu_n++; } }
static int menu_h(void){ int h = 8; for(int i = 0; i < menu_n; i++) h += menu_it[i].cmd ? MROW : 9; return h; }
static void open_menu(int mx, int my){
  menu_n = 0; struct entry *e = cur_ent();
  if(e){ menu_add(e->isdir ? "Open" : "Open", M_OPEN, 1); menu_add("-", 0, 0); menu_add("Rename", M_RENAME, 1); menu_add("Delete", M_DELETE, 1); menu_add("-", 0, 0); menu_add("Copy path to search", M_PATH, 1); }
  else { menu_add("New folder", M_NEW, 1); menu_add("Refresh", M_REFRESH, 1); menu_add("-", 0, 0); menu_add("View as list", M_LIST, 1); menu_add("View as grid", M_GRID, 1); menu_add("-", 0, 0); menu_add("Go up a level", M_UP, cwd[1] != 0); }
  int w = 210, h = menu_h(); menu_x = mx + w > cw - 4 ? cw - 4 - w : mx; menu_y = my + h > ch - 4 ? my - h : my; if(menu_y < 4) menu_y = 4; if(menu_x < 4) menu_x = 4;
  menu_open = 1; invalidate();
}
static void draw_menu(void){
  int w = 210, h = menu_h(), x = menu_x, y = menu_y; shadow(x, y, w, h, 9); rbox(x, y, w, h, 9, C_BG, 0xDCE0E6, 1);
  int iy = y + 4;
  for(int i = 0; i < menu_n; i++){
    if(!menu_it[i].cmd){ rect(x + 10, iy + 4, w - 20, 1, C_LINE); iy += 9; continue; }
    int on = menu_it[i].on; if(on && ishot(H_MENU, i)) rr(x + 4, iy, w - 8, MROW, 6, C_HOT);
    int chk = (menu_it[i].cmd == M_LIST && !view_grid) || (menu_it[i].cmd == M_GRID && view_grid);
    if(chk){ float cx = x + 20, cy = iy + MROW / 2.0f; seg(cx - 4, cy, cx - 1, cy + 3, 1.6f, C_ACCENT); seg(cx - 1, cy + 3, cx + 5, cy - 4, 1.6f, C_ACCENT); }
    u32 c = !on ? C_FAINT : menu_it[i].cmd == M_DELETE ? 0xD93A3F : C_TEXT;
    txtc(x + 36, iy + MROW / 2, w - 48, menu_it[i].cmd == M_OPEN ? f_semi : f_reg, menu_it[i].l, c);
    addhit(x + 4, iy, w - 8, MROW, H_MENU, i); iy += MROW;
  }
}
static void run_cmd(int cmd){
  menu_open = 0;
  switch(cmd){
    case M_OPEN: open_sel(); break; case M_RENAME: start_rename(); break; case M_DELETE: delete_sel(); break;
    case M_NEW: new_folder(); break; case M_REFRESH: { struct entry *e = cur_ent(); char k[128] = ""; if(e) snprintf(k, sizeof k, "%s", e->name); reload(k[0] ? k : 0); } break;
    case M_LIST: view_grid = 0; clamp_scroll(); break; case M_GRID: view_grid = 1; clamp_scroll(); break; case M_UP: go_up(); break;
    case M_PATH: { struct entry *e = cur_ent(); if(e){ edit = E_PATH; char p[400]; full_path(e->name, p, sizeof p); snprintf(ebuf, sizeof ebuf, "C:%s", p); for(char *s = ebuf; *s; s++) if(*s == '/') *s = '\\'; elen = (int)strlen(ebuf); } } break;
  }
  invalidate();
}

static void render(void){
  noclip(); nhits = 0; rect(0, 0, BW, BH, C_BG);
  if(nvis) { if(view_grid) draw_grid(); else draw_list(); } else { if(!view_grid) draw_list(); draw_empty(); }
  draw_scrollbar(); draw_sidebar(); draw_topbar(); draw_cmdbar(); draw_status();
  if(menu_open) draw_menu();
}
static void ensure_buf(int w, int h){
  if(w < 1) w = 1; if(h < 1) h = 1; if(px && w == BW && h == BH) return;
  struct { DWORD size; int w, h; unsigned short planes, bpp; DWORD comp, img; int xp, yp; DWORD used, imp; } bi = {40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0};
  void *bits = 0; HANDLE b = API(gdi32, CreateDIBSection)(mdc, &bi, 0, &bits, 0, 0); if(!b || !bits) return;
  API(gdi32, SelectObject)(mdc, b); if(mbmp) API(gdi32, DeleteObject)(mbmp); mbmp = b; px = bits; BW = w; BH = h;
}

/* ---------------------------------------------------------------- input ---- */
static int find_hit(int mx, int my, int *arg){
  for(int i = nhits - 1; i >= 0; i--){ struct hit *h = &hits[i]; if(mx >= h->x && mx < h->x + h->w && my >= h->y && my < h->y + h->h){ *arg = h->arg; return h->id; } }
  if(menu_open){ *arg = 0; return H_NONE; }
  int it = item_at(mx, my); if(it >= 0){ *arg = it; return H_ITEM; }
  *arg = 0; return mx >= SW && my >= cy0() && my < cy1() ? H_CONTENT : H_NONE;
}
static void set_sel(int i){ if(i < 0) i = 0; if(i >= nvis) i = nvis - 1; sel = i; ensure_visible(sel); invalidate(); }
static int drag_off; static unsigned last_click_t; static int last_click_i = -2;
static void click(int id, int arg, int mx, int my){
  if(edit && !(id == H_PATH && edit == E_PATH) && !(id == H_ITEM && arg == sel && edit == E_RENAME)) commit_edit();
  if(id != H_SEARCH && id != H_QCLEAR) search_focus = 0;
  switch(id){
    case H_BACK: go_back(); break; case H_FWD: go_fwd(); break; case H_UP: go_up(); break; case H_REFRESH: run_cmd(M_REFRESH); break;
    case H_CRUMB: { if(arg == 0){ go("/", 1); break; } char t[260]; const char *s = cwd; int k = 0; for(int i = 0; s[i]; i++){ if(s[i] == '/' && i > 0 && ++k == arg){ memcpy(t, s, i); t[i] = 0; go(t, 1); return; } } go(cwd, 1); break; }
    case H_PATH: if(edit != E_PATH){ edit = E_PATH; snprintf(ebuf, sizeof ebuf, "C:%s", cwd[1] ? cwd : "\\"); for(char *s = ebuf; *s; s++) if(*s == '/') *s = '\\'; elen = (int)strlen(ebuf); } break;
    case H_SEARCH: search_focus = 1; break;
    case H_QCLEAR: qlen = 0; q[0] = 0; refilter(); search_focus = 1; scroll = 0; break;
    case H_NEW: new_folder(); break; case H_RENAME: start_rename(); break; case H_DELETE: delete_sel(); break;
    case H_VLIST: view_grid = 0; clamp_scroll(); break; case H_VGRID: view_grid = 1; clamp_scroll(); break;
    case H_PLACE: go(place_path[arg], 1); break;
    case H_COL: if(sort_col == arg) sort_desc ^= 1; else { sort_col = arg; sort_desc = arg == 1 || arg == 3; } { struct entry *e = cur_ent(); char k[128] = ""; if(e) snprintf(k, sizeof k, "%s", e->name); sort_ents(); refilter(); sel = -1; for(int i = 0; i < nvis; i++) if(k[0] && !strcmp(ents[vis[i]].name, k)) sel = i; } break;
    case H_THUMB: { int vh = view_h(), chh = content_h(), th = vh * vh / chh; if(th < 30) th = 30; drag_off = my - (list_top() + (vh - th) * scroll / (chh - vh)); API(user32, SetCapture)(hwnd); break; }
    case H_ITEM: {
      if(edit == E_RENAME) break;
      unsigned now = (unsigned)API(kernel32, GetTickCount64)(); sel = arg;
      if(arg == last_click_i && now - last_click_t < 500){ last_click_i = -2; open_sel(); return; }
      last_click_i = arg; last_click_t = now; break;
    }
    case H_CONTENT: sel = -1; break;
  }
  (void)mx; invalidate();
}
static void type_char(char *buf, int *len, int max, int c){
  if(c == 8){ if(*len) buf[--*len] = 0; }
  else if(c >= 32 && c < 127 && *len < max - 1){ buf[(*len)++] = (char)c; buf[*len] = 0; }
}
static LRESULT WINAPI wndproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  int mx = (short)(lp & 0xFFFF), my = (short)((lp >> 16) & 0xFFFF);
  switch(m){
    case 0x14: return 1;      /* WM_ERASEBKGND: everything is painted from the buffer */
    case WM_PAINT: { PAINTSTRUCT ps; HANDLE dc = API(user32, BeginPaint)(h, &ps); if(dc && px){ render(); API(gdi32, BitBlt)(dc, 0, 0, cw, ch, mdc, 0, 0, 0x00CC0020); } API(user32, EndPaint)(h, &ps); return 0; }
    case WM_SIZE: cw = (int)(lp & 0xFFFF); ch = (int)((lp >> 16) & 0xFFFF); ensure_buf(cw, ch); clamp_scroll(); menu_open = 0; invalidate(); return 0;
    case WM_MOUSEWHEEL: { int d = (short)((wp >> 16) & 0xFFFF); menu_open = 0; scroll -= d * (view_grid ? TILE_H : ROW * 3) / 120; clamp_scroll(); invalidate(); return 0; }
    case WM_MOUSEMOVE: {
      if(press_id == H_THUMB){ int vh = view_h(), chh = content_h(), th = vh * vh / chh; if(th < 30) th = 30; if(vh > th){ scroll = (my - drag_off - list_top()) * (chh - vh) / (vh - th); clamp_scroll(); } invalidate(); return 0; }
      int a, id = find_hit(mx, my, &a); if(id != hot_id || a != hot_arg){ hot_id = id; hot_arg = a; invalidate(); } return 0;
    }
    case WM_LBUTTONDOWN: {
      int a, id = find_hit(mx, my, &a);
      if(menu_open){ if(id == H_MENU){ if(menu_it[a].on) run_cmd(menu_it[a].cmd); } else { menu_open = 0; invalidate(); } return 0; }
      press_id = id; press_arg = a; click(id, a, mx, my); return 0;
    }
    case WM_LBUTTONUP: if(press_id == H_THUMB) API(user32, ReleaseCapture)(); press_id = 0; invalidate(); return 0;
    case 0x204: {             /* WM_RBUTTONDOWN: select what's under the mouse */
      int a, id = find_hit(mx, my, &a); if(menu_open){ menu_open = 0; }
      if(edit) commit_edit();
      if(id == H_ITEM) sel = a; else if(id == H_CONTENT) sel = -1; invalidate(); return 0;
    }
    case 0x205: { int a, id = find_hit(mx, my, &a); if(id == H_ITEM || id == H_CONTENT) open_menu(mx, my); return 0; }   /* WM_RBUTTONUP */
    case WM_CHAR: {
      int c = (int)wp; if(API(user32, GetKeyState)(0x11) & 0x8000) return 0;
      if(menu_open){ if(c == 27){ menu_open = 0; invalidate(); } return 0; }
      if(edit){ if(c == 13) commit_edit(); else if(c == 27){ edit = E_NONE; invalidate(); } else { if(edit == E_RENAME && c >= 32 && strchr("/\\:*?\"<>|", c)) return 0; type_char(ebuf, &elen, edit == E_RENAME ? 120 : 200, c); invalidate(); } return 0; }
      if(search_focus){
        if(c == 27){ if(qlen){ qlen = 0; q[0] = 0; } else search_focus = 0; }
        else if(c == 13){ search_focus = 0; if(nvis) sel = 0; }
        else type_char(q, &qlen, sizeof q, c);
        refilter(); scroll = 0; if(qlen && nvis && sel < 0) sel = 0; invalidate(); return 0;
      }
      if(c == 13) open_sel(); else if(c == 8) go_up(); else if(c == 27){ sel = -1; invalidate(); }
      else if(c > 32 && c < 127){       /* type to jump to an item */
        int start = sel + 1; for(int k = 0; k < nvis; k++){ int i = (start + k) % nvis; if(tolower((unsigned char)ents[vis[i]].name[0]) == tolower(c)){ set_sel(i); break; } } }
      return 0;
    }
    case 0x104: if(wp == 0x25) go_back(); else if(wp == 0x27) go_fwd(); else if(wp == 0x26) go_up(); else break; return 0;   /* Alt+arrows */
    case WM_KEYDOWN: {
      int vk = (int)wp, ctrl = (API(user32, GetKeyState)(0x11) & 0x8000) != 0;
      if(ctrl){ if(vk == 'F' || vk == 'E'){ search_focus = 1; edit = E_NONE; } else if(vk == 'L'){ click(H_PATH, 0, 0, 0); } else if(vk == 'N'){ new_folder(); } else if(vk == 'R'){ run_cmd(M_REFRESH); } else return 0; invalidate(); return 0; }
      if(vk == 0x74){ run_cmd(M_REFRESH); return 0; }
      if(edit || search_focus || menu_open){ if(search_focus && vk == 0x28 && nvis){ search_focus = 0; set_sel(0); } return 0; }
      int cols = view_grid ? grid_cols() : 1, page = view_grid ? cols * (view_h() / TILE_H) : view_h() / ROW;
      if(vk == 0x28) set_sel(sel < 0 ? 0 : sel + cols); else if(vk == 0x26) set_sel(sel < 0 ? 0 : sel - cols);
      else if(vk == 0x27 && view_grid) set_sel(sel + 1); else if(vk == 0x25 && view_grid) set_sel(sel - 1);
      else if(vk == 0x24) set_sel(0); else if(vk == 0x23) set_sel(nvis - 1);
      else if(vk == 0x22) set_sel(sel + (page > 1 ? page : 1)); else if(vk == 0x21) set_sel(sel - (page > 1 ? page : 1));
      else if(vk == 0x2E) delete_sel(); else if(vk == 0x71) start_rename();
      return 0;
    }
    case WM_DESTROY: API(user32, PostQuitMessage)(0); return 0;
  }
  return API(user32, DefWindowProcA)(h, m, wp, lp);
}
int main(int argc, char **argv){
  HANDLE hi = API(kernel32, GetModuleHandleA)(0);
  mdc = API(gdi32, CreateCompatibleDC)(0); API(gdi32, SetBkMode)(mdc, 1);
  f_reg = mkfont(14, 400); f_semi = mkfont(14, 600); f_small = mkfont(13, 400); f_badge = mkfont(10, 700); f_big = mkfont(18, 600);
  h_reg = fheight(f_reg); h_semi = fheight(f_semi); h_small = fheight(f_small); h_badge = fheight(f_badge); h_big = fheight(f_big);
  ensure_buf(cw, ch);
  WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = "WFILES"; wc.hCursor = API(user32, LoadCursorA)(0, (const char*)32512ull);
  API(user32, RegisterClassA)(&wc);
  if(argc > 1){ snprintf(cwd, sizeof cwd, "%s", argv[1]); for(char *s = cwd; *s; s++) if(*s == '\\') *s = '/'; if(cwd[0] != '/'){ memmove(cwd + 1, cwd, strlen(cwd) + 1); cwd[0] = '/'; } int l = (int)strlen(cwd); if(l > 1 && cwd[l - 1] == '/') cwd[l - 1] = 0; }
  if(!(API(kernel32, GetFileAttributesA)(cwd) & 0x10) || API(kernel32, GetFileAttributesA)(cwd) == 0xFFFFFFFF) strcpy(cwd, "/");
  hwnd = API(user32, CreateWindowExA)(0, "WFILES", "Files", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 940, 580, 0, 0, hi, 0);
  reload(0); invalidate();
  MSG m; while(API(user32, GetMessageA)(&m, 0, 0, 0)){ API(user32, TranslateMessage)(&m); API(user32, DispatchMessageA)(&m); }
  return 0;
}
