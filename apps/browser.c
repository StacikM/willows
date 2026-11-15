/* Willows Browser - a graphical web browser.
   HTTP/HTTPS through WinINet, an HTML parser, a CSS engine (selectors, cascade, media queries, variables), block / inline / float /
   table / flex layout, PNG / JPEG / GIF images, form controls and GDI painting. No JavaScript. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "winui.h"
#define IMG_MAXPIX (6 * 1000 * 1000)
#include "web/layout.h"
static void js_page_run(void); static void js_after(void); static int js_dispatch(int node, const char *type, int x, int y, int key);

#define TOOLBAR 44
#define STATUS 24
#define SCROLLW 14
#define MAXDOC (256 << 20)                                  /* largest download; pages and images are capped lower inside http_request */
#define MAXLINKS 4000

/* ---------- fonts (hooks for the layout engine) ---------- */
struct fnt { int mono, bold, italic, px; HANDLE h; int asc, height; };
static struct fnt fonts[128]; static int nfonts;
static HANDLE mdc;
static int font_get(int mono, int bold, int italic, int px){
  if(px < 6) px = 6; if(px > 96) px = 96;
  for(int i = 0; i < nfonts; i++) if(fonts[i].mono == mono && fonts[i].bold == bold && fonts[i].italic == italic && fonts[i].px == px) return i;
  if(nfonts >= 128) return 0;
  struct fnt *f = &fonts[nfonts]; f->mono = mono; f->bold = bold; f->italic = italic; f->px = px;
  f->h = API(gdi32, CreateFontA)(-px, 0, 0, 0, bold ? 700 : 400, italic != 0, 0, 0, 0, 0, 0, 0, 0, mono ? "Courier New" : "Open Sans");
  API(gdi32, SelectObject)(mdc, f->h);
  TEXTMETRICA tm; API(gdi32, GetTextMetricsA)(mdc, &tm); f->asc = tm.tmAscent; f->height = tm.tmHeight;
  return nfonts++;
}
static int font_w(int fi, const char *s, int n){ SIZE sz; API(gdi32, SelectObject)(mdc, fonts[fi].h); API(gdi32, GetTextExtentPoint32A)(mdc, s, n, &sz); return sz.cx; }
static int font_asc(int fi){ return fonts[fi].asc; }
static int font_h(int fi){ return fonts[fi].height; }

/* ---------- state ---------- */
static char cur_url[700];
static char *links[MAXLINKS]; static int nlinks;
static char page_title[160];
static char history[64][700]; static int hist_n, hist_i = -1;
static char url_text[700]; static int url_len, url_focus = 1, url_sel = 1;
static char status_text[400] = "Ready";
static int scroll, view_w = 900, view_h = 560, hover_link = -1;
static HANDLE hwnd, hnet;
static int g_gen;                                    /* bumped on every navigation: running loaders stop when it changes */
static int g_focus = -1;                             /* focused form control */
static int g_busy, g_quit;
static int g_doc_loaded;

static void set_status(const char *s){ snprintf(status_text, sizeof status_text, "%s", s); }
static void repaint(void){ API(user32, InvalidateRect)(hwnd, 0, 0); }
static void repaint_now(void){ API(user32, InvalidateRect)(hwnd, 0, 0); API(user32, UpdateWindow)(hwnd); }
static void clamp_scroll(void){ int mx = page_h - view_h; if(mx < 0) mx = 0; if(scroll > mx) scroll = mx; if(scroll < 0) scroll = 0; }
static void pump(void){
  MSG m;
  while(API(user32, PeekMessageA)(&m, 0, 0, 0, 1)){
    if(m.message == 0x12){ g_quit = 1; API(user32, PostQuitMessage)(0); return; }
    API(user32, TranslateMessage)(&m); API(user32, DispatchMessageA)(&m);
  }
}

/* ---------- URLs ---------- */
static void url_resolve(const char *base, const char *ref, char *out, int max){
  while(*ref == ' ') ref++;
  if(!strncmp(ref, "http://", 7) || !strncmp(ref, "https://", 8) || !strncmp(ref, "data:", 5) || !strncmp(ref, "about:", 6) || !strncmp(ref, "run:", 4) || !strncmp(ref, "file:", 5)){ snprintf(out, max, "%s", ref); return; }
  const char *p = strstr(base, "://"); if(!p){ snprintf(out, max, "%s", ref); return; }
  int scheme_len = (int)(p - base); p += 3; const char *auth_end = p; while(*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#') auth_end++;
  char origin[300]; int on = (int)(auth_end - base); if(on > 290) on = 290; memcpy(origin, base, on); origin[on] = 0;
  if(ref[0] == '/' && ref[1] == '/'){ char sch[12]; if(scheme_len > 10) scheme_len = 10; memcpy(sch, base, scheme_len); sch[scheme_len] = 0; snprintf(out, max, "%s:%s", sch, ref); return; }
  char path[700]; const char *pe = auth_end; int pl = 0; while(*pe && *pe != '?' && *pe != '#' && pl < 690) path[pl++] = *pe++; path[pl] = 0;
  const char *query = *pe == '?' ? pe : 0; char qbuf[400]; qbuf[0] = 0; if(query){ int q = 0; while(query[q] && query[q] != '#' && q < 390){ qbuf[q] = query[q]; q++; } qbuf[q] = 0; }
  if(!*ref){ snprintf(out, max, "%s%s%s", origin, path, qbuf); return; }
  if(ref[0] == '#'){ snprintf(out, max, "%s%s%s%s", origin, path, qbuf, ref); return; }
  if(ref[0] == '?'){ snprintf(out, max, "%s%s%s", origin, path, ref); return; }
  char full[1400];
  if(ref[0] == '/') snprintf(full, sizeof full, "%s", ref);
  else { char dir[700]; snprintf(dir, sizeof dir, "%s", path); char *sl = strrchr(dir, '/'); if(sl) sl[1] = 0; else strcpy(dir, "/"); snprintf(full, sizeof full, "%s%s", dir, ref); }
  /* normalise "." and ".." segments in the path part (up to ? or #) */
  char *tail = full; while(*tail && *tail != '?' && *tail != '#') tail++;
  char rest[700]; snprintf(rest, sizeof rest, "%s", tail); *tail = 0;
  char norm[1400]; int nn = 0; const char *s = full;
  while(*s){
    const char *e = s + 1; while(*e && *e != '/') e++;
    int sl = (int)(e - s);                                    /* segment including its leading '/' */
    if(sl == 2 && s[1] == '.'){ }
    else if(sl == 3 && s[1] == '.' && s[2] == '.'){ while(nn > 0 && norm[nn - 1] != '/') nn--; if(nn > 0) nn--; }
    else { if(nn + sl < (int)sizeof norm - 1){ memcpy(norm + nn, s, sl); nn += sl; } }
    s = e;
  }
  if(nn == 0){ norm[nn++] = '/'; }
  norm[nn] = 0;
  snprintf(out, max, "%s%s%s", origin, norm, rest);
}
static int b64v(int c){ if(c >= 'A' && c <= 'Z') return c - 'A'; if(c >= 'a' && c <= 'z') return c - 'a' + 26; if(c >= '0' && c <= '9') return c - '0' + 52; if(c == '+' || c == '-') return 62; if(c == '/' || c == '_') return 63; return -1; }
static unsigned char *data_uri(const char *url, int *len){
  const char *comma = strchr(url, ','); if(!comma) return 0;
  int b64 = 0; for(const char *p = url; p < comma; p++) if(p[0] == ';' && !strncmp(p, ";base64", 7)) b64 = 1;
  const char *d = comma + 1; int dl = (int)strlen(d); unsigned char *out = malloc(dl + 4); if(!out) return 0; int o = 0;
  if(b64){ unsigned acc = 0; int bits = 0; for(int i = 0; i < dl; i++){ int v = b64v((unsigned char)d[i]); if(v < 0) continue; acc = (acc << 6) | v; bits += 6; if(bits >= 8){ bits -= 8; out[o++] = (unsigned char)(acc >> bits); } } }
  else { for(int i = 0; i < dl; i++){ if(d[i] == '%' && i + 2 < dl && isalnum((unsigned char)d[i+1])){ int h = d[i+1], l = d[i+2]; h = isdigit(h) ? h - '0' : (tolower(h) - 'a' + 10); l = isdigit(l) ? l - '0' : (tolower(l) - 'a' + 10); out[o++] = (unsigned char)(h * 16 + l); i += 2; } else out[o++] = (unsigned char)d[i]; } }
  *len = o; return out;
}
static void urlenc(char *out, int max, const char *s){
  int k = 0; for(; *s && k < max - 4; s++){ unsigned char c = (unsigned char)*s; if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[k++] = (char)c; else if(c == ' ') out[k++] = '+'; else k += snprintf(out + k, 4, "%%%02X", c); }
  out[k] = 0;
}

/* ---------- networking ---------- */
/* Fetches url into a malloc'd buffer (NUL terminated). Returns 0 on failure. */
/* Open a request. Willows' wininet has one call for any method/headers/body; Windows and Wine do not, so look it up at run time
   and fall back to the standard InternetConnect/HttpOpenRequest/HttpSendRequest calls. *conn receives a connection handle to close afterwards. */
static HANDLE open_request(const char *url, const char *method, const char *hdrs, const void *body, int blen, HANDLE *conn){
  static HANDLE (WINAPI *willows)(HANDLE, const char *, const char *, const char *, const void *, DWORD); static int looked;
  *conn = 0;
  if(!looked){ looked = 1; HANDLE m = API(kernel32, LoadLibraryA)("wininet.dll"); willows = m ? API(kernel32, GetProcAddress)(m, "WillowsHttpRequest") : 0; }
  if(willows) return willows(hnet, url, method, hdrs, body, (DWORD)blen);
  HANDLE m = API(kernel32, LoadLibraryA)("wininet.dll"); if(!m) return 0;
  HANDLE (WINAPI *connect_)(HANDLE, const char *, unsigned short, const char *, const char *, DWORD, DWORD, unsigned long long) = API(kernel32, GetProcAddress)(m, "InternetConnectA");
  HANDLE (WINAPI *openreq)(HANDLE, const char *, const char *, const char *, const char *, const char **, DWORD, unsigned long long) = API(kernel32, GetProcAddress)(m, "HttpOpenRequestA");
  BOOL (WINAPI *sendreq)(HANDLE, const char *, DWORD, void *, DWORD) = API(kernel32, GetProcAddress)(m, "HttpSendRequestA");
  if(!connect_ || !openreq || !sendreq) return API(wininet, InternetOpenUrlA)(hnet, url, 0, 0, 0, 0);
  int https = !strncmp(url, "https://", 8); const char *p = url + (https ? 8 : 7); char host[260]; int k = 0; unsigned port = https ? 443 : 80;
  while(*p && *p != '/' && *p != ':' && k < 255) host[k++] = *p++; host[k] = 0;
  if(*p == ':'){ p++; port = 0; while(*p >= '0' && *p <= '9') port = port * 10 + (unsigned)(*p++ - '0'); }
  const char *path = *p ? p : "/";
  HANDLE c = connect_(hnet, host, (unsigned short)port, 0, 0, 3, 0, 0); if(!c) return 0;
  HANDLE r = openreq(c, method, path, 0, 0, 0, (https ? 0x00800000u : 0) | 0x80000000u | 0x04000000u, 0);   /* secure, reload, no cache write */
  if(!r || !sendreq(r, hdrs && hdrs[0] ? hdrs : 0, hdrs ? (DWORD)strlen(hdrs) : 0, (void *)body, (DWORD)blen)){ if(r) API(wininet, InternetCloseHandle)(r); API(wininet, InternetCloseHandle)(c); return 0; }
  *conn = c; return r;
}
static char g_cdisp[200];                                  /* Content-Disposition of the last response (a download's real file name) */
static unsigned char *http_request(const char *url, const char *method, const char *hdrs, const void *body, int blen, int maxbytes, int *outlen, char *ctype, int ctlen, char *final_url, int fulen, DWORD *status, int show_progress){
  g_cdisp[0] = 0;
  if(!strncmp(url, "file://", 7)){
    HANDLE f = API(kernel32, CreateFileA)(url + 7, 0x80000000u, 1, 0, 3, 0x80, 0); if(f == (HANDLE)(long long)-1 || !f) return 0;
    DWORD sz = API(kernel32, GetFileSize)(f, 0); if(sz > (DWORD)maxbytes) sz = (DWORD)maxbytes;
    unsigned char *b = malloc(sz + 1); DWORD got = 0; if(b) API(kernel32, ReadFile)(f, b, sz, &got, 0); API(kernel32, CloseHandle)(f);
    if(!b) return 0; b[got] = 0; *outlen = (int)got; if(status) *status = 200; if(final_url) snprintf(final_url, fulen, "%s", url);
    if(ctype){ int ul = (int)strlen(url); snprintf(ctype, ctlen, "%s", ul > 4 && (!strcasecmp_(url + ul - 4, ".png")) ? "image/png" : ul > 4 && (!strcasecmp_(url + ul - 4, ".jpg") || !strcasecmp_(url + ul - 5, ".jpeg")) ? "image/jpeg" : ul > 4 && !strcasecmp_(url + ul - 4, ".gif") ? "image/gif" : ul > 4 && !strcasecmp_(url + ul - 4, ".css") ? "text/css" : ul > 4 && !strcasecmp_(url + ul - 4, ".txt") ? "text/plain" : "text/html"); }
    return b;
  }
  HANDLE conn; HANDLE h = open_request(url, method, hdrs, body, blen, &conn);
  if(!h) return 0;
  if(final_url){ DWORD fl = (DWORD)fulen; if(!API(wininet, InternetQueryOptionA)(h, 34, final_url, &fl)) snprintf(final_url, fulen, "%s", url); }
  DWORD st = 0, sl = 4; API(wininet, HttpQueryInfoA)(h, 19 | 0x20000000, &st, &sl, 0); if(status) *status = st;
  if(ctype){ ctype[0] = 0; DWORD cl = (DWORD)ctlen; API(wininet, HttpQueryInfoA)(h, 1, ctype, &cl, 0); }
  { DWORD cl = sizeof g_cdisp; if(!API(wininet, HttpQueryInfoA)(h, 47, g_cdisp, &cl, 0)) g_cdisp[0] = 0; }
  if(ctype && (!strncmp(ctype, "text/", 5) || !strncmp(ctype, "image/", 6) || strstr(ctype, "json") || strstr(ctype, "xml")) && maxbytes > (6 << 20)) maxbytes = 6 << 20;   /* pages and images stay small; downloads may be big */
  int cap = 1 << 16, n = 0;
  { DWORD clen = 0, cll = 4; if(API(wininet, HttpQueryInfoA)(h, 5 | 0x20000000, &clen, &cll, 0) && clen > (DWORD)cap && clen <= (DWORD)maxbytes) cap = (int)clen + 4096; }   /* known size: one allocation instead of repeated doubling */
  unsigned char *buf = malloc(cap); if(!buf){ API(wininet, InternetCloseHandle)(h); if(conn) API(wininet, InternetCloseHandle)(conn); return 0; } DWORD got;
  while(API(wininet, InternetReadFile)(h, buf + n, cap - n - 1, &got) && got){
    n += got;
    if(n + 4096 > cap){ if(cap >= maxbytes) break; cap *= 2; unsigned char *nb = realloc(buf, cap); if(!nb) break; buf = nb; }
    if(show_progress && (n >> 14) != ((n - (int)got) >> 14)){ char pm[120]; snprintf(pm, sizeof pm, "Loading %s ... %d KB", url, n / 1024); set_status(pm); repaint_now(); }
    if(n >= maxbytes) break;
  }
  buf[n] = 0; API(wininet, InternetCloseHandle)(h); if(conn) API(wininet, InternetCloseHandle)(conn); *outlen = n; return buf;
}
static unsigned char *http_get(const char *url, int maxbytes, int *outlen, char *ctype, int ctlen, char *final_url, int fulen, DWORD *status, int show_progress){
  return http_request(url, "GET", 0, 0, 0, maxbytes, outlen, ctype, ctlen, final_url, fulen, status, show_progress);
}
static char *post_hold, *post_keep; static int post_len; static char post_ctype[100];       /* body of the POST that navigate() is about to send */
/* convert Latin-1 / Windows-1252 text to UTF-8 when the page says so */
static char *to_utf8(const char *s, int n, int *outn){
  static const unsigned w1252[32] = {0x20AC,0x81,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,0x2C6,0x2030,0x160,0x2039,0x152,0x8D,0x17D,0x8F,0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0x9D,0x17E,0x178};
  char *o = malloc((size_t)n * 3 + 4); if(!o) return 0; int k = 0;
  for(int i = 0; i < n; i++){ unsigned c = (unsigned char)s[i]; if(c < 0x80) o[k++] = (char)c; else { if(c < 0xA0) c = w1252[c - 0x80]; k += utf8_put(o + k, c); } }
  o[k] = 0; *outn = k; return o;
}
static int is_latin1(const char *html, int len, const char *ctype){
  if(strstr(ctype, "charset=")){ const char *c = strstr(ctype, "charset="); c += 8; if(*c == '"') c++; return !strncasecmp_(c, "iso-8859", 8) || !strncasecmp_(c, "windows-125", 11) || !strncasecmp_(c, "latin", 5); }
  int lim = len < 3000 ? len : 3000;
  for(int i = 0; i + 8 < lim; i++) if(!strncasecmp_(html + i, "charset=", 8)){ const char *c = html + i + 8; if(*c == '"' || *c == '\'') c++; return !strncasecmp_(c, "iso-8859", 8) || !strncasecmp_(c, "windows-125", 11); }
  return 0;
}

/* ---------- page loading ---------- */
static char *src_doc; static int src_len;
static void free_styles(void){ for(int i = 0; i < nnodes; i++){ if(N(i).type == 0 && N(i).st) free(N(i).st); N(i).st = 0; if(N(i).value){ free(N(i).value); N(i).value = 0; } } }
static void free_resources(void){ for(int i = 0; i < nres; i++){ img_free(res[i].im); free(res[i].url); } nres = 0; memset(res, 0, sizeof res); }
static void relayout_only(void){ if(!g_doc_loaded) return; layout_page(view_w); clamp_scroll(); }
static void restyle_layout(void){
  if(!g_doc_loaded) return;
  g_view_w = view_w; g_view_h = view_h;
  for(int i = 0; i < nnodes; i++){ if(N(i).type == 0 && N(i).st){ free(N(i).st); } N(i).st = 0; }
  compute_all(g_root); layout_page(view_w); clamp_scroll();
}
static const char *res_pick_src(int n){
  const char *ds = attr_of(n, "data-src"); if(!ds) ds = attr_of(n, "data-original"); if(!ds) ds = attr_of(n, "data-lazy-src");
  const char *s = attr_of(n, "src");
  if(ds && *ds && (!s || !*s || !strncmp(s, "data:", 5))) return ds;
  if(s && *s) return s;
  const char *ss = attr_of(n, "srcset"); if(!ss) ss = attr_of(n, "data-srcset");
  return ss && *ss ? ss : 0;
}
static int add_resource(const char *url){
  for(int i = 0; i < nres; i++) if(!strcmp(res[i].url, url)) return i;
  if(nres >= MAXRES) return -1;
  res[nres].url = strdup(url); res[nres].state = 0; res[nres].im = 0; return nres++;
}
static void collect_resources_x(int inc){                                     /* inc: only images that have no resource yet (added by scripts) */
  if(!inc) nres = 0;
  for(int i = 0; i < nnodes; i++){
    if(N(i).type != 0 || N(i).tag != TAG_img || !N(i).st || N(i).st->display == D_NONE || (inc && N(i).res)) continue;
    const char *src = res_pick_src(i); if(!src) continue;
    char first[600]; int k = 0; while(*src == ' ') src++; while(src[k] && src[k] != ' ' && src[k] != ',' && k < 590){ first[k] = src[k]; k++; } if(k > 1 && first[k-1] == ',') k--; first[k] = 0;
    if(!strncmp(first, "data:", 5)){ /* keep data URIs unresolved */ }
    char full[1400]; if(!strncmp(first, "data:", 5)) snprintf(full, sizeof full, "%s", src); else url_resolve(cur_url, first, full, sizeof full);
    int r = add_resource(full); if(r >= 0) N(i).res = r + 1;
  }
}
static void collect_resources(void){ collect_resources_x(0); }
static wimg *img_shrink(wimg *im){
  long long px = (long long)im->w * im->h; int f = 1; while(px / ((long long)f * f) > 1500000) f++;
  if(f == 1) return im;
  int nw = im->w / f, nh = im->h / f; if(nw < 1) nw = 1; if(nh < 1) nh = 1;
  wimg *o = img_new(nw, nh); if(!o) return im;
  for(int y = 0; y < nh; y++) for(int x = 0; x < nw; x++){
    unsigned r = 0, g = 0, b = 0, a = 0; int cnt = 0;
    for(int dy = 0; dy < f; dy++) for(int dx = 0; dx < f; dx++){ unsigned p = im->px[(size_t)(y * f + dy) * im->w + x * f + dx]; a += p >> 24; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; cnt++; }
    o->px[(size_t)y * nw + x] = ((a / cnt) << 24) | ((r / cnt) << 16) | ((g / cnt) << 8) | (b / cnt);
  }
  img_free(im); return o;
}
static void load_images(int gen){
  int total = 0; for(int i = 0; i < nres; i++) if(res[i].state == 0) total++;
  int done = 0;
  for(int i = 0; i < nres && gen == g_gen && !g_quit; i++){
    if(res[i].state != 0) continue;
    char msg[500]; snprintf(msg, sizeof msg, "Loading images (%d/%d) ...", done + 1, total); set_status(msg); repaint_now(); pump();
    if(gen != g_gen) return;
    unsigned char *data = 0; int len = 0;
    if(!strncmp(res[i].url, "data:", 5)) data = data_uri(res[i].url, &len);
    else { char ct[100]; DWORD st = 0; data = http_get(res[i].url, 3 << 20, &len, ct, sizeof ct, 0, 0, &st, 0); if(data && st >= 400){ free(data); data = 0; } }
    if(gen != g_gen){ free(data); return; }
    wimg *im = data ? img_decode(data, len) : 0; free(data);
    if(im) im = img_shrink(im);
    if(im){ res[i].im = im; res[i].state = 1; } else res[i].state = 2;
    done++;
    relayout_only(); repaint_now(); pump();
  }
  if(gen == g_gen){ set_status("Done"); repaint(); }
}
static void load_stylesheets(void){
  css_reset(); style_defaults(&default_style);
  parse_sheet(UA_CSS, UA_CSS + sizeof UA_CSS - 1, 0);
  if(g_quirks){ static const char q[] = "table{text-align:left}"; parse_sheet(q, q + sizeof q - 1, 0); }
  int fetched = 0;
  for(int i = 0; i < nnodes; i++){
    if(N(i).type != 0) continue;
    if(N(i).tag == TAG_style){
      const char *media = attr_of(i, "media"); if(media && !media_true(media, media + strlen(media))) continue;
      for(int c = N(i).first; c >= 0; c = N(c).next) if(N(c).type == 1) parse_sheet(N(c).text, N(c).text + strlen(N(c).text), 1);
    } else if(N(i).tag == TAG_link && fetched < 8){
      const char *rel = attr_of(i, "rel"), *href = attr_of(i, "href"); if(!rel || !href || !*href) continue;
      if(strncasecmp_(rel, "stylesheet", 10) != 0 || strcasecmp_(rel, "stylesheet") != 0) continue;
      const char *media = attr_of(i, "media"); if(media && *media && !media_true(media, media + strlen(media))) continue;
      char full[1400]; url_resolve(cur_url, href, full, sizeof full);
      char msg[500]; snprintf(msg, sizeof msg, "Loading style %s ...", full); set_status(msg); repaint_now();
      int len = 0; char ct[100]; DWORD st = 0;
      unsigned char *css = http_get(full, 600 << 10, &len, ct, sizeof ct, 0, 0, &st, 0); fetched++;
      if(css && st < 400){ char *copy = a_str((char *)css, len); parse_sheet(copy, copy + len, 1); }
      free(css);
    }
  }
}
static void build_links(void){
  nlinks = 0;
  for(int i = 0; i < nnodes; i++){
    N(i).link = -1;
    if(N(i).type != 0 || N(i).tag != TAG_a) continue;
    const char *href = attr_of(i, "href"); if(!href || !*href || nlinks >= MAXLINKS) continue;
    if(!strncasecmp_(href, "javascript:", 11) || !strncasecmp_(href, "mailto:", 7) || !strncasecmp_(href, "tel:", 4)) continue;
    char full[1400]; url_resolve(cur_url, href, full, sizeof full);
    links[nlinks] = a_str(full, (int)strlen(full)); N(i).link = nlinks++;
  }
}
static void find_title(void){
  page_title[0] = 0;
  for(int i = 0; i < nnodes; i++) if(N(i).type == 0 && N(i).tag == TAG_title){
    int k = 0; for(int c = N(i).first; c >= 0 && k < 150; c = N(c).next) if(N(c).type == 1){ for(const char *p = N(c).text; *p && k < 150; p++){ char ch = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p; if(ch == ' ' && (k == 0 || page_title[k - 1] == ' ')) continue; page_title[k++] = ch; } }
    while(k > 0 && page_title[k - 1] == ' ') k--; page_title[k] = 0; break;
  }
}
static void show_html(const char *html, int len, const char *url, int gen){
  g_focus = -1; g_doc_loaded = 0;
  free_styles(); free_resources();
  snprintf(cur_url, sizeof cur_url, "%s", url);
  html_parse(html, len);
  g_doc_loaded = 1;
  find_title(); build_links();
  g_view_w = view_w; g_view_h = view_h;
  load_stylesheets();
  compute_all(g_root); collect_resources(); layout_page(view_w);
  scroll = 0; clamp_scroll();
  char t[240]; snprintf(t, sizeof t, "%s - Willows Browser", page_title[0] ? page_title : url); API(user32, SetWindowTextA)(hwnd, t);
  repaint_now();
  js_page_run(); js_after();                                         /* <script> tags, then DOMContentLoaded/load */
  load_images(gen);
}

static const char *HOME_PAGE =
  "<html><head><title>Willows Browser</title><style>"
  "body{margin:0;background:#f4f6fb;font-family:sans-serif;color:#1f2937}"
  ".hero{background:#1d4ed8;color:#fff;padding:36px 40px}.hero h1{margin:0 0 6px 0;font-size:32px}.hero p{margin:0;font-size:16px;color:#dbe4ff}"
  ".wrap{padding:24px 40px;max-width:760px}h2{font-size:19px;margin:22px 0 10px 0}"
  ".card{display:block;background:#fff;border:1px solid #d9dee8;padding:12px 16px;margin:0 0 10px 0}.card b{color:#1d4ed8}.card span{color:#6b7280}"
  ".k{background:#e5e7eb;padding:1px 6px;font-family:monospace}"
  "</style></head><body><div class=hero><h1>Willows Browser</h1><p>HTML, CSS, images and forms, running on your own operating system.</p></div><div class=wrap>"
  "<h2>Try these</h2>"
  "<a class=card href=\"https://example.com/\"><b>example.com</b> <span>- the classic test page (HTTPS)</span></a>"
  "<a class=card href=\"https://en.wikipedia.org/wiki/Operating_system\"><b>Wikipedia</b> <span>- Operating system</span></a>"
  "<a class=card href=\"https://news.ycombinator.com/\"><b>Hacker News</b> <span>- tables and lists</span></a>"
  "<a class=card href=\"https://text.npr.org/\"><b>text.npr.org</b> <span>- news as light pages</span></a>"
  "<a class=card href=\"http://info.cern.ch/\"><b>info.cern.ch</b> <span>- the first website ever</span></a>"
  "<a class=card href=\"https://lite.duckduckgo.com/lite/\"><b>DuckDuckGo Lite</b> <span>- search</span></a>"
  "<h2>Using it</h2><div class=card>Type an address or a search in the bar and press <span class=k>Enter</span>. "
  "<span class=k>Ctrl+L</span> selects the bar, scroll with the wheel, <span class=k>PgUp/PgDn</span>, <span class=k>Home/End</span> or the arrow keys, "
  "<span class=k>Backspace</span> goes back.</div>"
  "<div class=card><span>Not supported: JavaScript, SVG, video. Pages that need scripts may look incomplete.</span></div>"
  "</div></body></html>";

static void error_page(const char *url, const char *why, int gen){
  char b[1500]; int n = snprintf(b, sizeof b, "<html><body style=\"margin:40px;font-family:sans-serif\"><h2>Can't open this page</h2><p>%s</p><p><b>%s</b></p><p><a href=\"about:home\">Home</a></p></body></html>", url, why);
  show_html(b, n, url, gen);
}
static void push_history(const char *u){
  if(hist_i < 62){ hist_i++; hist_n = hist_i + 1; snprintf(history[hist_i], 700, "%s", u); }
  else { memmove(history[0], history[1], 62 * 700); snprintf(history[62], 700, "%s", u); hist_i = 62; hist_n = 63; }
}
/* A response that is not a page, text or image is a download: save it under Downloads and offer to run it. */
/* the file name from a Content-Disposition header: filename*=UTF-8''my%20file.exe or filename="my file.exe" */
static int disp_name(const char *cd, char *out, int max){
  if(!cd || !*cd) return 0; const char *v = strstr(cd, "filename*="); int ext = v != 0; if(v) v += 10; else { v = strstr(cd, "filename="); if(!v) return 0; v += 9; }
  if(ext){ const char *q = strstr(v, "''"); if(q) v = q + 2; }
  int quoted = *v == '"'; if(quoted) v++; int k = 0;
  for(; *v && k < max - 1 && (quoted ? *v != '"' : (*v != ';' && *v != ' ')); v++){
    char c = *v; if(c == '%' && v[1] && v[2]){ int hi = isdigit((unsigned char)v[1]) ? v[1] - '0' : (tolower((unsigned char)v[1]) - 'a' + 10), lo = isdigit((unsigned char)v[2]) ? v[2] - '0' : (tolower((unsigned char)v[2]) - 'a' + 10); c = (char)(hi * 16 + lo); v += 2; }
    if(c == '/' || c == '\\') { k = 0; continue; }                                  /* never a path */
    if(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || c == '(' || c == ')') out[k++] = c; else if(c == ' ') out[k++] = '_';
  }
  out[k] = 0; return k > 0 && out[0] != '.';
}
/* a file name with no extension: guess one from the first bytes (or the content type) so executables stay runnable */
static const char *sniff_ext(const unsigned char *d, int n, const char *ctype){
  if(n >= 2 && d[0] == 'M' && d[1] == 'Z') return ".exe";
  if(n >= 4 && d[0] == 'P' && d[1] == 'K' && d[2] == 3 && d[3] == 4) return ".zip";
  if(n >= 4 && !memcmp(d, "%PDF", 4)) return ".pdf";
  if(n >= 6 && !memcmp(d, "7z\xBC\xAF\x27\x1C", 6)) return ".7z";
  if(n >= 8 && d[0] == 0xD0 && d[1] == 0xCF && d[2] == 0x11 && d[3] == 0xE0) return ".msi";
  if(n >= 4 && !memcmp(d, "\x89PNG", 4)) return ".png";
  if(ctype && strstr(ctype, "zip")) return ".zip";
  return 0;
}
static void save_download(const unsigned char *data, int n, const char *final_url, const char *ctype, int gen){
  char name[96]; int k = 0; const char *p = strstr(final_url, "://"); p = p ? p + 3 : final_url; const char *last = p; for(const char *q = p; *q && *q != '?' && *q != '#'; q++) if(*q == '/') last = q + 1;
  for(const char *q = last; *q && *q != '?' && *q != '#' && k < 90; q++){ char c = *q; if(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') name[k++] = c; else if(c == '%' && q[1] == '2' && q[2] == '0'){ name[k++] = '_'; q += 2; } else if(c == ' ') name[k++] = '_'; }
  name[k] = 0; if(!k || name[0] == '.'){ strcpy(name, "download.bin"); k = (int)strlen(name); }
  { char dn[96]; if(disp_name(g_cdisp, dn, sizeof dn)){ strcpy(name, dn); k = (int)strlen(name); } }                  /* the server's own file name wins */
  if(!strchr(name, '.')){ const char *e = sniff_ext(data, n, ctype); if(e && k + 5 < (int)sizeof name){ strcpy(name + k, e); k += (int)strlen(e); } }
  API(kernel32, CreateDirectoryA)("Downloads", 0);
  char path[160]; snprintf(path, sizeof path, "Downloads\\%s", name);
  HANDLE f = API(kernel32, CreateFileA)(path, 0x40000000u, 0, 0, 2, 0x80, 0); int ok = f != (HANDLE)~0ull && f;
  if(ok && n > (1 << 20)){ API(kernel32, SetFilePointer)(f, n, 0, 0); ok = API(kernel32, SetEndOfFile)(f) != 0; API(kernel32, SetFilePointer)(f, 0, 0, 0); }   /* big download: reserve the whole file in one allocation */
  int written = 0;
  if(ok){ while(written < n){ DWORD chunk = (DWORD)(n - written > 65536 ? 65536 : n - written), w = 0; if(!API(kernel32, WriteFile)(f, data + written, chunk, &w, 0) || !w) break; written += (int)w; } API(kernel32, CloseHandle)(f); }
  char html[1800]; int is_exe = k > 4 && !strcasecmp_(name + k - 4, ".exe"); int hn;
  if(ok && written == n){
    char runlink[400]; runlink[0] = 0;
    if(is_exe) snprintf(runlink, sizeof runlink, "<p><a href=\"run:Downloads/%s\" style=\"display:inline-block;background:#1d4ed8;color:#fff;padding:8px 18px;text-decoration:none\">Run this program</a></p><p style=\"color:#6b7280\">Only run programs you trust. Willows runs 64-bit Windows programs.</p>", name);
    hn = snprintf(html, sizeof html, "<html><body style=\"margin:40px;font-family:sans-serif\"><h2>Download finished</h2><p><b>%s</b> (%d KB, %s)<br>saved to <b>C:\\Downloads\\%s</b></p>%s<p><a href=\"about:home\">Home</a></p></body></html>",
      name, (n + 1023) / 1024, ctype[0] ? ctype : "unknown type", name, is_exe ? runlink : "<p>Open the Files app to see it in the Downloads folder.</p>");
  } else hn = snprintf(html, sizeof html, "<html><body style=\"margin:40px;font-family:sans-serif\"><h2>Download failed</h2><p>Could not save %s (%d bytes written of %d).</p><p><a href=\"about:home\">Home</a></p></body></html>", name, written, n);
  show_html(html, hn, final_url, gen);
}
static void run_downloaded(const char *rel){
  char msg[200]; snprintf(msg, sizeof msg, "Starting %s ...", rel); set_status(msg); repaint_now();
  API(shell32, ShellExecuteA)(0, "open", rel, 0, 0, 1);
}
static void navigate(const char *input_, int push){
  char input[760]; snprintf(input, sizeof input, "%s", input_);
  char url[900]; int gen = ++g_gen; g_busy = 1;
  const char *pbody = post_hold; int plen = post_len; char phdr[160]; phdr[0] = 0; if(pbody) snprintf(phdr, sizeof phdr, "Content-Type: %s\r\n", post_ctype);
  const char *meth = pbody ? "POST" : "GET";
  post_hold = 0; free(post_keep); post_keep = (char *)pbody;          /* the body stays valid until the next navigation */
  if(!strcmp(input, "about:home")){
    snprintf(url_text, sizeof url_text, "about:home"); url_len = (int)strlen(url_text);
    show_html(HOME_PAGE, (int)strlen(HOME_PAGE), "about:home", gen);
    if(push && gen == g_gen) push_history("about:home");
    set_status("Ready"); g_busy = 0; repaint(); return;
  }
  int has_scheme = strstr(input, "://") != 0;
  if(has_scheme) snprintf(url, sizeof url, "%s", input);
  else if(strchr(input, ' ') || !strchr(input, '.')){
    char q[500]; urlenc(q, sizeof q, input); snprintf(url, sizeof url, "https://lite.duckduckgo.com/lite/?q=%s", q);
  } else { int plain = isdigit((unsigned char)input[0]) || !strncmp(input, "localhost", 9) || strchr(input, ':'); snprintf(url, sizeof url, "%s://%s", plain ? "http" : "https", input); }
  snprintf(url_text, sizeof url_text, "%s", url); url_len = (int)strlen(url_text);
  char msg[960]; snprintf(msg, sizeof msg, "Loading %s ...", url); set_status(msg); API(user32, SetWindowTextA)(hwnd, "Loading... - Willows Browser"); repaint_now();
  char final_url[900], ctype[100]; DWORD status = 0; int n = 0;
  unsigned char *buf = http_request(url, meth, phdr, pbody, plen, MAXDOC, &n, ctype, sizeof ctype, final_url, sizeof final_url, &status, 1);
  if(!buf && !has_scheme && !strncmp(url, "https://", 8)){ snprintf(url, sizeof url, "http://%s", input); buf = http_request(url, meth, phdr, pbody, plen, MAXDOC, &n, ctype, sizeof ctype, final_url, sizeof final_url, &status, 1); }
  if(gen != g_gen){ free(buf); return; }
  if(!buf){ error_page(url, "The site could not be reached, or its security certificate was rejected.", gen); set_status("Failed"); g_busy = 0; repaint(); return; }
  snprintf(url_text, sizeof url_text, "%s", final_url); url_len = (int)strlen(url_text);
  if(push) push_history(final_url);
  char sm[500]; snprintf(sm, sizeof sm, "Rendering (HTTP %u, %d bytes) ...", status, n); set_status(sm);
  if(strstr(ctype, "text/html") || strstr(ctype, "xhtml") || !ctype[0]){
    char *doc = (char *)buf; int dn = n;
    if(is_latin1(doc, n, ctype)){ char *u = to_utf8(doc, n, &dn); if(u){ free(buf); buf = (unsigned char *)u; doc = u; } }
    show_html(doc, dn, final_url, gen);
  } else if(!strncmp(ctype, "text/", 5) || strstr(ctype, "json") || strstr(ctype, "xml") || strstr(ctype, "javascript")){
    char *w = malloc(n + 40); int m = snprintf(w, 20, "<pre>");
    for(int i = 0; i < n; i++){ if(buf[i] == '<'){ memcpy(w + m, "&lt;", 4); m += 4; } else if(buf[i] == '&'){ memcpy(w + m, "&amp;", 5); m += 5; } else w[m++] = (char)buf[i]; if(m + 8 > n + 30){ w = realloc(w, (size_t)n * 5 + 40); } }
    memcpy(w + m, "</pre>", 7); show_html(w, m + 6, final_url, gen); free(w);
  } else if(!strncmp(ctype, "image/", 6)){
    wimg *im = img_decode(buf, n);
    char html[1400]; int hn;
    if(im){ img_free(im); hn = snprintf(html, sizeof html, "<html><body style=\"margin:0;background:#222\"><img src=\"%s\"></body></html>", final_url); show_html(html, hn, final_url, gen); }
    else { error_page(final_url, "This image format cannot be shown.", gen); }
  } else { char sm2[300]; snprintf(sm2, sizeof sm2, "Saving download (%d KB) ...", n / 1024); set_status(sm2); repaint_now(); save_download(buf, n, final_url, ctype, gen); }
  free(buf);
  if(gen == g_gen){ char d[500]; snprintf(d, sizeof d, "Done  (HTTP %u, %s)", status, ctype[0] ? ctype : "unknown type"); set_status(d); url_focus = 0; }
  g_busy = 0; repaint();
}
static void go_back(void){ if(hist_i > 0){ hist_i--; navigate(history[hist_i], 0); } }
static void go_forward(void){ if(hist_i + 1 < hist_n){ hist_i++; navigate(history[hist_i], 0); } }

/* ---------- forms ---------- */
static int ctl_type(int n){                                  /* 0 text 1 checkbox 2 radio 3 button 4 select 5 textarea 6 hidden */
  int t = N(n).tag;
  if(t == TAG_select) return 4; if(t == TAG_textarea) return 5;
  const char *ty = attr_of(n, "type"); if(!ty) return 0;
  if(!strcasecmp_(ty, "checkbox")) return 1; if(!strcasecmp_(ty, "radio")) return 2;
  if(!strcasecmp_(ty, "submit") || !strcasecmp_(ty, "button") || !strcasecmp_(ty, "reset") || !strcasecmp_(ty, "image")) return 3;
  if(!strcasecmp_(ty, "hidden")) return 6;
  return 0;
}
static const char *ctl_value(int n){
  if(N(n).value) return N(n).value;
  if(N(n).tag == TAG_textarea){ for(int c = N(n).first; c >= 0; c = N(c).next) if(N(c).type == 1) return N(c).text; return ""; }
  const char *v = attr_of(n, "value"); return v ? v : "";
}
static int ctl_checked(int n){ if(N(n).value) return N(n).value[0] == '1'; return attr_of(n, "checked") != 0; }
static void set_value(int n, const char *v){ free(N(n).value); N(n).value = strdup(v); }
static int select_index(int n){ if(N(n).value) return atoi(N(n).value); int idx = 0, i = 0; for(int c = N(n).first; c >= 0; c = N(c).next) if(N(c).tag == TAG_option){ if(attr_of(c, "selected")) idx = i; i++; } return idx; }
static int select_option(int n, int idx, const char **val){
  int i = 0, first = -1; for(int c = N(n).first; c >= 0; c = N(c).next){ int o = N(c).tag == TAG_option ? c : -1;
    if(N(c).tag == TAG_optgroup){ for(int d = N(c).first; d >= 0; d = N(d).next) if(N(d).tag == TAG_option){ if(i == idx) o = d; else i++; if(o >= 0) break; } if(o >= 0 && i == idx) { } }
    if(N(c).tag == TAG_option){ if(first < 0) first = c; if(i == idx){ o = c; } else { i++; continue; } }
    if(o >= 0 && N(o).tag == TAG_option && i == idx){ if(val){ const char *v = attr_of(o, "value"); static char tmp[300]; if(!v){ tmp[0] = 0; for(int t = N(o).first; t >= 0; t = N(t).next) if(N(t).type == 1){ snprintf(tmp, sizeof tmp, "%s", N(t).text); break; } v = tmp; } *val = v; } return o; } }
  return first;
}
static int select_count(int n){ int k = 0; for(int c = N(n).first; c >= 0; c = N(c).next){ if(N(c).tag == TAG_option) k++; else if(N(c).tag == TAG_optgroup) for(int d = N(c).first; d >= 0; d = N(d).next) if(N(d).tag == TAG_option) k++; } return k; }
static void submit_form(int form, int clicked){
  if(js_dispatch(form, "submit", 0, 0, 0)){ js_after(); return; }        /* a submit handler called preventDefault() */
  const char *action = attr_of(form, "action"); char full[1400];
  url_resolve(cur_url, action && *action ? action : cur_url, full, sizeof full);
  char *q = strchr(full, '?'); if(q) *q = 0; char *fr = strchr(full, '#'); if(fr) *fr = 0;
  const char *meth = attr_of(form, "method"); int is_post = meth && !strcasecmp_(meth, "post");
  char url[2400]; int k = snprintf(url, sizeof url, "%s", full); int first = 1;
  char *pb = 0; int pn = 0, pcap = 0;
  for(int i = 0; i < nnodes; i++){
    if(N(i).type != 0) continue;
    int tg = N(i).tag; if(tg != TAG_input && tg != TAG_select && tg != TAG_textarea && tg != TAG_button) continue;
    int p = N(i).parent; while(p >= 0 && p != form) p = N(p).parent; if(p != form) continue;
    const char *name = attr_of(i, "name"); if(!name || !*name) continue;
    int ct = tg == TAG_button ? 3 : ctl_type(i); const char *val = "";
    if(ct == 3){ if(i != clicked) continue; val = ctl_value(i); }
    else if(ct == 1 || ct == 2){ if(!ctl_checked(i)) continue; val = attr_of(i, "value"); if(!val) val = "on"; }
    else if(ct == 4){ select_option(i, select_index(i), &val); }
    else val = ctl_value(i);
    char en[400], ev[900]; urlenc(en, sizeof en, name); urlenc(ev, sizeof ev, val);
    if(is_post){ int need = (int)strlen(en) + (int)strlen(ev) + 3; if(pn + need > pcap){ pcap = (pn + need) * 2 + 256; pb = realloc(pb, pcap); } pn += snprintf(pb + pn, pcap - pn, "%s%s=%s", first ? "" : "&", en, ev); }
    else if(k + 8 + (int)strlen(en) + (int)strlen(ev) < (int)sizeof url) k += snprintf(url + k, sizeof url - k, "%c%s=%s", first ? '?' : '&', en, ev);
    first = 0;
  }
  if(is_post){ if(!pb){ pb = malloc(1); pb[0] = 0; } free(post_hold); post_hold = pb; post_len = pn; snprintf(post_ctype, sizeof post_ctype, "application/x-www-form-urlencoded"); }
  navigate(url, 1);
}
static int form_of(int n){ for(int p = N(n).parent; p >= 0; p = N(p).parent) if(N(p).tag == TAG_form) return p; return -1; }

/* ---------- painting ---------- */
static unsigned bgr(unsigned c){ return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }
static void fill(HANDLE dc, int x, int y, int w, int h, unsigned rgb){
  if(w <= 0 || h <= 0) return; RECT r = {x, y, x + w, y + h}; HANDLE b = API(gdi32, CreateSolidBrush)(bgr(rgb)); API(user32, FillRect)(dc, &r, b); API(gdi32, DeleteObject)(b);
}
static void text_at(HANDLE dc, int fi, int x, int y, const char *s, int n, unsigned rgb){
  API(gdi32, SelectObject)(dc, fonts[fi].h); API(gdi32, SetTextColor)(dc, bgr(rgb)); API(gdi32, TextOutA)(dc, x, y, s, n);
}
static void frame(HANDLE dc, int x, int y, int w, int h, unsigned c){ fill(dc, x, y, w, 1, c); fill(dc, x, y + h - 1, w, 1, c); fill(dc, x, y, 1, h, c); fill(dc, x + w - 1, y, 1, h, c); }
static int fit_chars(int fi, const char *s, int n, int maxw){ while(n > 0 && font_w(fi, s, n) > maxw){ n--; while(n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--; } return n; }
static void draw_image(HANDLE dc, struct op *o, int x, int y){
  int r = N(o->node).res - 1;
  if(r >= 0 && r < nres && res[r].state == 1 && res[r].im){
    wimg *im = res[r].im;
    struct { unsigned sz; int w, h; unsigned short planes, bpp; unsigned comp, szimg; int xp, yp; unsigned used, imp; } bmi;
    memset(&bmi, 0, sizeof bmi); bmi.sz = 40; bmi.w = im->w; bmi.h = -im->h; bmi.planes = 1; bmi.bpp = 32;
    API(gdi32, StretchDIBits)(dc, x, y, o->w, o->h, 0, 0, im->w, im->h, im->px, &bmi, 0, 0x00AA0029);
  } else if(o->w > 0 && o->h > 0){ fill(dc, x, y, o->w, o->h, 0xFFEEF0F4); frame(dc, x, y, o->w, o->h, 0xFFC9CED6); }
}
static void draw_control(HANDLE dc, struct op *o, int x, int y){
  int n = o->node; struct style *st = N(n).st; int fi = fi_of(st); int fh = font_h(fi); int ct = ctl_type(n); int focus = n == g_focus;
  unsigned tc = st->color;
  if(ct == 0 || ct == 5){
    fill(dc, x, y, o->w, o->h, 0xFFFFFFFF); frame(dc, x, y, o->w, o->h, focus ? 0xFF3B82F6 : 0xFF8A8F98);
    const char *v = ctl_value(n); int pw = 0; if(!*v){ v = attr_of(n, "placeholder"); if(v){ tc = 0xFF9CA3AF; } else v = ""; }
    const char *ty = attr_of(n, "type"); char tmp[300]; int vn = (int)strlen(v);
    if(ct == 0 && ty && !strcasecmp_(ty, "password") && tc != 0xFF9CA3AF){ vn = vn < 299 ? vn : 299; for(int i = 0; i < vn; i++) tmp[i] = '*'; tmp[vn] = 0; v = tmp; }
    if(ct == 5){ int ly = y + 4; const char *p = v; while(*p && ly + fh <= y + o->h){ const char *e = p; while(*e && *e != '\n') e++; int k = fit_chars(fi, p, (int)(e - p), o->w - 10); text_at(dc, fi, x + 5, ly, p, k, tc); ly += fh; p = *e ? e + 1 : e; } }
    else {
      int k = vn; const char *sp = v; int maxw = o->w - 12;
      if(focus){ while(k > 0 && font_w(fi, sp, k) > maxw){ sp++; k--; } } else k = fit_chars(fi, v, vn, maxw);
      text_at(dc, fi, x + 6, y + (o->h - fh) / 2, sp, k, tc);
      if(focus){ int cw = font_w(fi, sp, k); fill(dc, x + 6 + cw, y + 4, 1, o->h - 8, 0xFF111827); }
    }
    (void)pw; return;
  }
  if(ct == 1 || ct == 2){
    fill(dc, x, y, o->w, o->h, 0xFFFFFFFF); frame(dc, x, y, o->w, o->h, 0xFF6B7280);
    if(ctl_checked(n)){ if(ct == 1){ fill(dc, x + 3, y + 3, o->w - 6, o->h - 6, 0xFF2563EB); } else { fill(dc, x + 4, y + 4, o->w - 8, o->h - 8, 0xFF2563EB); } }
    return;
  }
  if(ct == 3){
    const char *v = ctl_value(n); const char *ty = attr_of(n, "type"); if(!*v) v = ty && !strcasecmp_(ty, "reset") ? "Reset" : "Submit";
    fill(dc, x, y, o->w, o->h, 0xFFE9ECF1); frame(dc, x, y, o->w, o->h, 0xFF8A8F98);
    int k = (int)strlen(v); int tw = font_w(fi, v, k); text_at(dc, fi, x + (o->w - tw) / 2, y + (o->h - fh) / 2, v, k, 0xFF111827); return;
  }
  if(ct == 4){
    fill(dc, x, y, o->w, o->h, 0xFFFFFFFF); frame(dc, x, y, o->w, o->h, 0xFF8A8F98);
    int idx = select_index(n); int opt = select_option(n, idx, 0);
    if(opt >= 0){ for(int t = N(opt).first; t >= 0; t = N(t).next) if(N(t).type == 1){ int k = fit_chars(fi, N(t).text, (int)strlen(N(t).text), o->w - 30); text_at(dc, fi, x + 6, y + (o->h - fh) / 2, N(t).text, k, tc); break; } }
    fill(dc, x + o->w - 18, y + o->h / 2 - 2, 9, 2, 0xFF4B5563); fill(dc, x + o->w - 16, y + o->h / 2, 5, 2, 0xFF4B5563); fill(dc, x + o->w - 14, y + o->h / 2 + 2, 1, 2, 0xFF4B5563);
  }
}
static int isqrt_(int v){ int r = 0; while((r + 1) * (r + 1) <= v) r++; return r; }
static void round_fill(HANDLE dc, int x, int y, int w, int h, int r, unsigned rgb){
  if(w <= 0 || h <= 0) return; if(r > w / 2) r = w / 2; if(r > h / 2) r = h / 2; if(r < 0) r = 0;
  for(int i = 0; i < r; i++){                                         /* corner rows: inset from the circle equation (2x scale for half-pixel centres) */
    int dy = 2 * (r - i) - 1; int inset = r - (isqrt_(4 * r * r - dy * dy) + 1) / 2;
    fill(dc, x + inset, y + i, w - 2 * inset, 1, rgb); fill(dc, x + inset, y + h - 1 - i, w - 2 * inset, 1, rgb);
  }
  fill(dc, x, y + r, w, h - 2 * r, rgb);
}
static void round_box(HANDLE dc, int x, int y, int w, int h, struct op *o){
  int r = o->rad;
  if(o->bwid > 0){ round_fill(dc, x, y, w, h, r, o->bcol); round_fill(dc, x + o->bwid, y + o->bwid, w - 2 * o->bwid, h - 2 * o->bwid, r - o->bwid > 0 ? r - o->bwid : 0, o->icol); }
  else round_fill(dc, x, y, w, h, r, o->color);
}
static void paint(HANDLE dc){
  int top = TOOLBAR, bottom = view_h + TOOLBAR;
  fill(dc, 0, top, view_w + SCROLLW, view_h, canvas_bg);
  API(gdi32, SetBkMode)(dc, 1);
  for(int i = 0; i < nops; i++){
    struct op *o = &ops[i]; int y = o->y - scroll + top;
    if(y + o->h < top || y > bottom) continue;
    int x = o->x;
    switch(o->kind){
      case OP_RECT: if(o->rad > 0) round_box(dc, x, y, o->w, o->h, o); else fill(dc, x, y, o->w, o->h, o->color); break;
      case OP_TEXT: text_at(dc, o->font, x, y, o->text, o->len, o->color); break;
      case OP_IMG: draw_image(dc, o, x, y); break;
      case OP_INPUT: draw_control(dc, o, x, y); break;
    }
  }
  fill(dc, view_w, top, SCROLLW, view_h, 0xFFF3F4F6);
  if(page_h > view_h){ int th = view_h * view_h / page_h; if(th < 24) th = 24; int ty = top + (view_h - th) * scroll / (page_h - view_h); fill(dc, view_w + 2, ty, SCROLLW - 4, th, 0xFFA7ADB6); }
  fill(dc, 0, 0, view_w + SCROLLW, TOOLBAR, 0xFFF3F4F6); fill(dc, 0, TOOLBAR - 1, view_w + SCROLLW, 1, 0xFFD1D5DB);
  int fs = font_get(0, 0, 0, 14), fb = font_get(0, 1, 0, 18);
  const char *bl[3] = {"<", ">", "R"}; int can[3] = {hist_i > 0, hist_i + 1 < hist_n, 1};
  for(int b = 0; b < 3; b++){ fill(dc, 8 + b * 38, 7, 32, 30, can[b] ? 0xFFE5E7EB : 0xFFF3F4F6); text_at(dc, fb, 8 + b * 38 + 10, 9, bl[b], 1, can[b] ? 0xFF1F2937 : 0xFFB0B5BD); }
  int ux = 8 + 3 * 38 + 6, uw = view_w + SCROLLW - ux - 10;
  fill(dc, ux, 7, uw, 30, url_focus ? 0xFF3B82F6 : 0xFFB8BEC8); fill(dc, ux + 1, 8, uw - 2, 28, 0xFFFFFFFF);
  int shown = url_len; while(shown > 0 && font_w(fs, url_text + (url_len - shown), shown) > uw - 24) shown--;
  if(url_focus && url_sel) fill(dc, ux + 8, 11, font_w(fs, url_text + (url_len - shown), shown) + 4, 22, 0xFFBFD7FF);
  text_at(dc, fs, ux + 10, 12, url_text + (url_len - shown), shown, 0xFF1F2937);
  if(url_focus && !url_sel){ int cw = font_w(fs, url_text + (url_len - shown), shown); fill(dc, ux + 10 + cw, 12, 1, 20, 0xFF1F2937); }
  fill(dc, 0, bottom, view_w + SCROLLW, STATUS, 0xFFF3F4F6); fill(dc, 0, bottom, view_w + SCROLLW, 1, 0xFFD1D5DB);
  const char *st = (hover_link >= 0 && hover_link < nlinks) ? links[hover_link] : status_text;
  int f11 = font_get(0, 0, 0, 12); int n = (int)strlen(st); while(n > 0 && font_w(f11, st, n) > view_w - 16) n--;
  text_at(dc, f11, 8, bottom + 4, st, n, 0xFF4B5563);
}

/* ---------- input ---------- */
static int op_at(int mx, int my){                              /* topmost interactive op under the pointer */
  if(mx >= view_w || my < TOOLBAR || my >= TOOLBAR + view_h) return -1;
  int y = my - TOOLBAR + scroll;
  for(int i = nops - 1; i >= 0; i--){
    struct op *o = &ops[i]; if(o->kind == OP_RECT) continue;
    int pad = (o->kind == OP_TEXT && o->link >= 0) ? 8 : 0;                      /* the space after a link word still belongs to the link */
    if(mx >= o->x && mx < o->x + o->w + pad && y >= o->y - (pad ? 2 : 0) && y < o->y + o->h + (pad ? 2 : 0) && (o->kind == OP_INPUT || o->link >= 0)) return i;
  }
  return -1;
}
static void scroll_to_fragment(const char *frag){
  for(int i = 0; i < nnodes; i++){
    if(N(i).type != 0) continue;
    const char *id = attr_of(i, "id"), *nm = attr_of(i, "name");
    if((id && !strcmp(id, frag)) || (nm && !strcmp(nm, frag))){
      for(int k = 0; k < nops; k++){ if(ops[k].node < 0) continue; int p = ops[k].node; while(p >= 0 && p != i) p = N(p).parent; if(p == i){ scroll = ops[k].y - 8; clamp_scroll(); repaint(); return; } }
    }
  }
}
static void click_link(const char *u){
  if(!strncmp(u, "run:", 4)){ char p[200]; snprintf(p, sizeof p, "%s", u + 4); for(char *c = p; *c; c++) if(*c == '/') *c = '\\'; run_downloaded(p); return; }
  const char *hash = strchr(u, '#');
  if(hash && cur_url[0]){ int bl = (int)(hash - u); char a[700], b[700]; snprintf(a, sizeof a, "%.*s", bl, u); snprintf(b, sizeof b, "%s", cur_url); char *bh = strchr(b, '#'); if(bh) *bh = 0;
    if(!strcmp(a, b)){ if(hash[1]) scroll_to_fragment(hash + 1); else { scroll = 0; repaint(); } return; } }
  char t[700]; snprintf(t, sizeof t, "%s", u); navigate(t, 1);
}
static void click_control(int n){
  int ct = ctl_type(n); g_focus = -1;
  if(ct == 0 || ct == 5){ g_focus = n; url_focus = 0; }
  else if(ct == 1){ set_value(n, ctl_checked(n) ? "0" : "1"); }
  else if(ct == 2){ const char *nm = attr_of(n, "name"); int f = form_of(n);
    for(int i = 0; i < nnodes; i++) if(N(i).type == 0 && N(i).tag == TAG_input && ctl_type(i) == 2 && form_of(i) == f){ const char *m = attr_of(i, "name"); if(nm && m && !strcmp(nm, m)) set_value(i, "0"); }
    set_value(n, "1"); }
  else if(ct == 4){ int cnt = select_count(n); if(cnt > 0){ char b[16]; snprintf(b, sizeof b, "%d", (select_index(n) + 1) % cnt); set_value(n, b); } }
  else if(ct == 3){ const char *ty = attr_of(n, "type"); int f = form_of(n); if(ty && !strcasecmp_(ty, "reset")){ } else if(f >= 0) submit_form(f, n); }
  repaint();
}
#include "web/js.h"
static int node_at(int mx, int my){                                  /* topmost text/image/control under the pointer (any element, not only links) */
  if(mx >= view_w || my < TOOLBAR || my >= TOOLBAR + view_h) return -1;
  int y = my - TOOLBAR + scroll;
  for(int i = nops - 1; i >= 0; i--){ struct op *o = &ops[i]; if(o->kind == OP_RECT || o->node < 0) continue; if(mx >= o->x && mx < o->x + o->w && y >= o->y && y < o->y + o->h) return o->node; }
  return -1;
}
static int js_timer_on;
static void js_after(void){                                           /* apply what scripts changed: DOM edits, title, navigation, timer */
  if(js_dirty){ js_dirty = 0; build_links(); restyle_layout(); if(js_img_dirty){ js_img_dirty = 0; collect_resources_x(1); relayout_only(); } repaint(); }
  if(js_title_dirty){ js_title_dirty = 0; char t[240]; snprintf(t, sizeof t, "%s - Willows Browser", page_title[0] ? page_title : cur_url); API(user32, SetWindowTextA)(hwnd, t); }
  int want = js_ctx && js_timers_active; if(want && !js_timer_on){ API(user32, SetTimer)(hwnd, 77, 30, 0); js_timer_on = 1; } else if(!want && js_timer_on){ API(user32, KillTimer)(hwnd, 77); js_timer_on = 0; }
  if(js_nav_pending){ js_nav_pending = 0; char u[900]; snprintf(u, sizeof u, "%s", js_nav_url); navigate(u, 1); }
}
static LRESULT WINAPI wndproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  switch(m){
    case 0x113: if(wp == 77 && js_ctx){ js_tick_call("__tick"); js_after(); } return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HANDLE dc = API(user32, BeginPaint)(h, &ps); if(dc) paint(dc); API(user32, EndPaint)(h, &ps); return 0; }
    case WM_SIZE: {
      int w = (int)(lp & 0xFFFF), hh = (int)((lp >> 16) & 0xFFFF); if(w < 200 || hh < 120) return 0;
      int nw = w - SCROLLW; int changed = nw != view_w; view_w = nw; view_h = hh - TOOLBAR - STATUS;
      if(g_doc_loaded && changed && !g_busy) restyle_layout(); else clamp_scroll();
      repaint(); return 0;
    }
    case WM_MOUSEWHEEL: { int d = (short)((wp >> 16) & 0xFFFF); scroll -= d / 120 * 60; clamp_scroll(); repaint(); return 0; }
    case WM_MOUSEMOVE: {
      int mx = (int)(lp & 0xFFFF), my = (int)((lp >> 16) & 0xFFFF); int oi = op_at(mx, my);
      int l = oi >= 0 && ops[oi].link >= 0 ? ops[oi].link : -1; int ctl = oi >= 0 && ops[oi].kind == OP_INPUT;
      API(user32, SetCursor)(API(user32, LoadCursorA)(0, (const char*)(unsigned long long)(l >= 0 || (ctl && ctl_type(ops[oi].node) >= 1) ? 32649 : ctl ? 32513 : 32512)));
      if(l != hover_link){ hover_link = l; repaint(); }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      int mx = (int)(lp & 0xFFFF), my = (int)((lp >> 16) & 0xFFFF);
      if(my < TOOLBAR){
        if(mx >= 8 && mx < 40) go_back(); else if(mx >= 46 && mx < 78) go_forward(); else if(mx >= 84 && mx < 116) navigate(cur_url[0] ? cur_url : "about:home", 0);
        else { url_focus = 1; url_sel = 1; g_focus = -1; repaint(); }
      } else {
        url_focus = 0; int oi = op_at(mx, my);
        int ctl_node = oi >= 0 && ops[oi].kind == OP_INPUT ? ops[oi].node : -1; char lu[700]; lu[0] = 0; if(oi >= 0 && ops[oi].link >= 0) snprintf(lu, sizeof lu, "%s", links[ops[oi].link]);
        int cancelled = 0, tn = node_at(mx, my);
        if(js_ctx && tn >= 0){ int el = N(tn).type == 1 ? N(tn).parent : tn; cancelled = js_dispatch(el, "click", mx, my - TOOLBAR + scroll, 0); js_after(); }
        if(cancelled) repaint();
        else if(ctl_node >= 0) click_control(ctl_node);
        else if(lu[0]){ g_focus = -1; click_link(lu); }
        else if(mx >= view_w && page_h > view_h){ scroll = (my - TOOLBAR) * (page_h - view_h) / view_h; clamp_scroll(); repaint(); }
        else { g_focus = -1; repaint(); }
      }
      return 0;
    }
    case WM_CHAR: {
      int c = (int)wp;
      if(js_ctx && !url_focus && c != 12 && (g_focus >= 0 || c >= 32 || c == 13)){ int tgt = g_focus >= 0 ? g_focus : g_body; if(js_dispatch(tgt, "keydown", 0, 0, c)){ js_after(); return 0; } js_after(); }
      if(c == 12){ url_focus = 1; url_sel = 1; g_focus = -1; repaint(); return 0; }
      if(url_focus){
        if(c == 13){ url_sel = 0; if(url_len) navigate(url_text, 1); }
        else if(c == 27){ url_focus = 0; repaint(); }
        else if(c == 8){ if(url_sel){ url_len = 0; url_text[0] = 0; url_sel = 0; repaint(); } else if(url_len){ url_text[--url_len] = 0; repaint(); } }
        else if((c >= 32 && c < 127) || (c >= 160 && c < 256)){ if(url_sel){ url_len = 0; url_sel = 0; } if(url_len < 598){ if(c >= 160){ url_text[url_len++] = (char)(0xC0 | (c >> 6)); c = 0x80 | (c & 0x3F); } url_text[url_len++] = (char)c; url_text[url_len] = 0; } repaint(); }
      } else if(g_focus >= 0){
        int n = g_focus; const char *cv = ctl_value(n); int l = (int)strlen(cv);
        if(c == 13){ if(ctl_type(n) == 5){ char *nv = malloc(l + 2); memcpy(nv, cv, l); nv[l] = '\n'; nv[l + 1] = 0; free(N(n).value); N(n).value = nv; } else { int f = form_of(n); if(f >= 0) submit_form(f, -1); } }
        else if(c == 27){ g_focus = -1; }
        else if(c == 8){ if(l > 0){ char *nv = strdup(cv); int k = l - 1; while(k > 0 && ((unsigned char)nv[k] & 0xC0) == 0x80) k--; nv[k] = 0; free(N(n).value); N(n).value = nv; } }
        else if((c >= 32 && c < 127) || (c >= 160 && c < 256)){ char *nv = malloc(l + 3); memcpy(nv, cv, l); if(c >= 160){ nv[l++] = (char)(0xC0 | (c >> 6)); c = 0x80 | (c & 0x3F); } nv[l] = (char)c; nv[l + 1] = 0; free(N(n).value); N(n).value = nv; }
        if(js_ctx && g_focus >= 0 && c != 13){ js_dispatch(g_focus, "input", 0, 0, 0); js_after(); }
        repaint();
      } else {
        if(c == 8) go_back(); else if(c == ' '){ scroll += view_h - 40; clamp_scroll(); repaint(); }
      }
      return 0;
    }
    case WM_KEYDOWN: {
      if(url_focus || g_focus >= 0) return 0;
      int vk = (int)wp, d = 0;
      if(vk == 0x28) d = 40; else if(vk == 0x26) d = -40; else if(vk == 0x22) d = view_h - 40; else if(vk == 0x21) d = -(view_h - 40);
      else if(vk == 0x24){ scroll = 0; clamp_scroll(); repaint(); return 0; } else if(vk == 0x23){ scroll = page_h; clamp_scroll(); repaint(); return 0; }
      if(d){ scroll += d; clamp_scroll(); repaint(); }
      return 0;
    }
    case WM_DESTROY: g_quit = 1; API(user32, PostQuitMessage)(0); return 0;
  }
  return API(user32, DefWindowProcA)(h, m, wp, lp);
}

int main(int argc, char **argv){
  HANDLE hi = API(kernel32, GetModuleHandleA)(0);
  WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = "WBROWSER"; wc.hCursor = API(user32, LoadCursorA)(0, (const char*)32512ull);
  API(user32, RegisterClassA)(&wc);
  hwnd = API(user32, CreateWindowExA)(0, "WBROWSER", "Willows Browser", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 900, 640, 0, 0, hi, 0);
  if(!hwnd) return 1;
  mdc = API(user32, BeginPaint)(hwnd, &(PAINTSTRUCT){0});
  hnet = API(wininet, InternetOpenA)("Mozilla/5.0 (compatible; WillowsBrowser/2.0)", 0, 0, 0, 0);
  char start[800]; snprintf(start, sizeof start, "about:home");
  if(argc > 1 && argv[1][0]){ if(strstr(argv[1], "://") || !strcmp(argv[1], "about:home")) snprintf(start, sizeof start, "%s", argv[1]); else if(argv[1][0] == '/') snprintf(start, sizeof start, "file://%s", argv[1]); else snprintf(start, sizeof start, "file:///%s", argv[1]); }
  navigate(start, 1);
  MSG msg;
  while(!g_quit && API(user32, GetMessageA)(&msg, 0, 0, 0)){ API(user32, TranslateMessage)(&msg); API(user32, DispatchMessageA)(&msg); }
  return 0;
}
