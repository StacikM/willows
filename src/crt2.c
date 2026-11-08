#include "kernel.h"
/* More of the C runtime that Windows programs import from msvcrt / api-ms-win-crt-*: strings, wide strings, number parsing, qsort,
   locale, time conversion and a few stdio stubs. (Floating point lives in crtmath.c, which is built with SSE.) */
typedef unsigned short wc16;
#define C2(n) {"msvcrt", #n, c2_##n}
#define C2N(dll, n, f) {dll, n, f}

static int lower(int c){ return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* ---- number parsing ---- */
static u64 parse_u(const char *s, char **end, int base, int *neg, int *ovf, u64 limit){
  while(*s == ' ' || (*s >= 9 && *s <= 13)) s++;
  *neg = 0; if(*s == '-'){ *neg = 1; s++; } else if(*s == '+') s++;
  if((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')){ s += 2; base = 16; }
  else if(base == 0) base = s[0] == '0' ? 8 : 10;
  u64 v = 0; const char *start = s; *ovf = 0;
  for(;; s++){ int d = *s >= '0' && *s <= '9' ? *s - '0' : (lower(*s) >= 'a' && lower(*s) <= 'z') ? lower(*s) - 'a' + 10 : 99; if(d >= base) break; if(v > (limit - d) / base) *ovf = 1; else v = v * base + d; }
  if(end) *end = (char *)(s == start ? start : s); return v;
}
static i64 WINAPI c2_strtoll(const char *s, char **end, int base){ int ng, ov; u64 v = parse_u(s, end, base, &ng, &ov, 0x7FFFFFFFFFFFFFFFull); if(ov) return ng ? (i64)0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFll; return ng ? -(i64)v : (i64)v; }
static i32 WINAPI c2_strtol(const char *s, char **end, int base){ int ng, ov; u64 v = parse_u(s, end, base, &ng, &ov, 0x7FFFFFFFull); if(ov) return ng ? (i32)0x80000000 : 0x7FFFFFFF; return ng ? -(i32)v : (i32)v; }
static u64 WINAPI c2_strtoull(const char *s, char **end, int base){ int ng, ov; u64 v = parse_u(s, end, base, &ng, &ov, ~0ull); if(ov) return ~0ull; return ng ? (u64)-(i64)v : v; }
static u32 WINAPI c2_strtoul(const char *s, char **end, int base){ int ng, ov; u64 v = parse_u(s, end, base, &ng, &ov, 0xFFFFFFFFull); if(ov) return 0xFFFFFFFFu; return ng ? (u32)-(i32)v : (u32)v; }
static void narrow(const wc16 *w, char *a, int max){ int i = 0; for(; w[i] && i < max - 1; i++) a[i] = w[i] < 0x80 ? (char)w[i] : '?'; a[i] = 0; }
static i64 WINAPI c2_wcstoll(const wc16 *w, wc16 **end, int base){ char a[80]; narrow(w, a, 80); char *e; i64 v = c2_strtoll(a, &e, base); if(end) *end = (wc16 *)w + (e - a); return v; }
static i32 WINAPI c2_wcstol(const wc16 *w, wc16 **end, int base){ char a[80]; narrow(w, a, 80); char *e; i32 v = c2_strtol(a, &e, base); if(end) *end = (wc16 *)w + (e - a); return v; }
static u64 WINAPI c2_wcstoull(const wc16 *w, wc16 **end, int base){ char a[80]; narrow(w, a, 80); char *e; u64 v = c2_strtoull(a, &e, base); if(end) *end = (wc16 *)w + (e - a); return v; }
static u32 WINAPI c2_wcstoul(const wc16 *w, wc16 **end, int base){ char a[80]; narrow(w, a, 80); char *e; u32 v = c2_strtoul(a, &e, base); if(end) *end = (wc16 *)w + (e - a); return v; }
static char *utoa_(u64 v, char *buf, int radix){ char t[70]; int n = 0; do { int d = (int)(v % radix); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= radix; } while(v); int k = 0; while(n) buf[k++] = t[--n]; buf[k] = 0; return buf; }
static char *WINAPI c2__ultoa(u32 v, char *buf, int radix){ return utoa_(v, buf, radix); }
static wc16 *WINAPI c2__ultow(u32 v, wc16 *buf, int radix){ char t[40]; utoa_(v, t, radix); int i = 0; for(; t[i]; i++) buf[i] = (wc16)t[i]; buf[i] = 0; return buf; }
static wc16 *WINAPI c2__itow(int v, wc16 *buf, int radix){ char t[40]; int k = 0; u32 u = (u32)v; if(radix == 10 && v < 0){ t[k++] = '-'; u = (u32)-v; } utoa_(u, t + k, radix); int i = 0; for(; t[i]; i++) buf[i] = (wc16)t[i]; buf[i] = 0; return buf; }
/* ---- strings ---- */
static int WINAPI c2__stricmp(const char *a, const char *b){ for(;; a++, b++){ int x = lower((u8)*a), y = lower((u8)*b); if(x != y) return x - y; if(!x) return 0; } }
static int WINAPI c2__strnicmp(const char *a, const char *b, u64 n){ for(; n; n--, a++, b++){ int x = lower((u8)*a), y = lower((u8)*b); if(x != y) return x - y; if(!x) return 0; } return 0; }
static int WINAPI c2__wcsnicmp(const wc16 *a, const wc16 *b, u64 n){ for(; n; n--, a++, b++){ int x = lower(*a), y = lower(*b); if(x != y) return x - y; if(!x) return 0; } return 0; }
static int WINAPI c2_isblank(int c){ return c == ' ' || c == '\t'; }
static int WINAPI c2_isxdigit(int c){ return (c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'f'); }
static int WINAPI c2_towlower(int c){ return lower(c); }
static int WINAPI c2_iswctype(int c, int mask){ int r = 0; if(c >= 'A' && c <= 'Z') r |= 0x101; if(c >= 'a' && c <= 'z') r |= 0x102; if(c >= '0' && c <= '9') r |= 0x4; if(c == ' ' || (c >= 9 && c <= 13)) r |= 0x8; if(c > 32 && c < 127 && !((c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'z'))) r |= 0x10; if(c2_isxdigit(c)) r |= 0x80; if(c >= 0x80) r |= 0x100; return (r & mask) != 0; }
static u64 WINAPI c2_strcspn(const char *s, const char *rej){ u64 n = 0; for(; s[n]; n++) for(const char *r = rej; *r; r++) if(s[n] == *r) return n; return n; }
static u64 WINAPI c2_strnlen(const char *s, u64 max){ u64 n = 0; while(n < max && s[n]) n++; return n; }
static char *WINAPI c2_strpbrk(const char *s, const char *set){ for(; *s; s++) for(const char *r = set; *r; r++) if(*s == *r) return (char *)s; return 0; }
static char *WINAPI c2_strerror(int e){ static char b[32]; static const char *const m[] = {"No error", "Operation not permitted", "No such file or directory", "No such process", "Interrupted", "I/O error", "No such device", "Argument list too long", "Exec format error", "Bad file descriptor"}; if(e >= 0 && e < 10) return (char *)m[e]; b[0] = 'E'; b[1] = 'r'; b[2] = 'r'; b[3] = ' '; utoa_((u32)e, b + 4, 10); return b; }
static void *WINAPI c2_memchr(const void *s, int c, u64 n){ const u8 *p = s; for(; n; n--, p++) if(*p == (u8)c) return (void *)p; return 0; }
static char *WINAPI c2_strrchr(const char *s, int c){ const char *r = 0; for(;; s++){ if(*s == (char)c) r = s; if(!*s) return (char *)r; } }
static wc16 *WINAPI c2_wcschr(const wc16 *s, wc16 c){ for(;; s++){ if(*s == c) return (wc16 *)s; if(!*s) return 0; } }
static u64 WINAPI c2_wcslen(const wc16 *s){ u64 n = 0; while(s[n]) n++; return n; }
static u64 WINAPI c2_wcsnlen(const wc16 *s, u64 max){ u64 n = 0; while(n < max && s[n]) n++; return n; }
static wc16 *WINAPI c2_wcscpy(wc16 *d, const wc16 *s){ wc16 *r = d; while((*d++ = *s++)); return r; }
static wc16 *WINAPI c2_wcscat(wc16 *d, const wc16 *s){ c2_wcscpy(d + c2_wcslen(d), s); return d; }
static int WINAPI c2_wcscmp(const wc16 *a, const wc16 *b){ while(*a && *a == *b){ a++; b++; } return (int)*a - (int)*b; }
static int WINAPI c2_wcsncmp(const wc16 *a, const wc16 *b, u64 n){ for(; n; n--, a++, b++){ if(*a != *b) return (int)*a - (int)*b; if(!*a) return 0; } return 0; }
/* ---- multibyte (UTF-8) ---- */
static u64 WINAPI c2_mbrtowc(wc16 *pwc, const char *s, u64 n, void *st){
  (void)st; if(!s) return 0; if(!n) return (u64)-2; u8 c = (u8)s[0]; int len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; if((u64)len > n) return (u64)-2;
  u32 v = len == 1 ? c : (c & (0x3F >> (len - 1))); for(int i = 1; i < len; i++){ if(((u8)s[i] & 0xC0) != 0x80) return (u64)-1; v = (v << 6) | ((u8)s[i] & 0x3F); }
  if(pwc) *pwc = (wc16)(v > 0xFFFF ? 0xFFFD : v); return v ? (u64)len : 0;
}
static u64 WINAPI c2_mbrlen(const char *s, u64 n, void *st){ return c2_mbrtowc(0, s, n, st); }
static u64 WINAPI c2_wcrtomb(char *s, wc16 w, void *st){ (void)st; if(!s) return 1; if(w < 0x80){ s[0] = (char)w; return 1; } if(w < 0x800){ s[0] = (char)(0xC0 | (w >> 6)); s[1] = (char)(0x80 | (w & 63)); return 2; } s[0] = (char)(0xE0 | (w >> 12)); s[1] = (char)(0x80 | ((w >> 6) & 63)); s[2] = (char)(0x80 | (w & 63)); return 3; }
static int WINAPI c2__getpid(void){ return cur->pid; }
/* ---- sorting and division ---- */
struct ldivt { i32 quot, rem; };
static struct ldivt WINAPI c2_ldiv(i32 a, i32 b){ struct ldivt r = {0, 0}; if(b){ r.quot = a / b; r.rem = a % b; } return r; }
typedef int (WINAPI *cmpfn)(const void *, const void *);
static void WINAPI c2_qsort(void *base, u64 n, u64 size, cmpfn cmp){
  if(n < 2 || !size || size > 256) return; u8 *a = base; u8 tmp[256];
  for(u64 gap = n / 2; gap > 0; gap /= 2)                                  /* shell sort: no recursion, fine for the sizes programs sort */
    for(u64 i = gap; i < n; i++){ memcpy(tmp, a + i * size, size); u64 j = i; while(j >= gap && cmp(a + (j - gap) * size, tmp) > 0){ memcpy(a + j * size, a + (j - gap) * size, size); j -= gap; } memcpy(a + j * size, tmp, size); }
}
/* ---- environment, locale ---- */
static char *WINAPI c2_getenv(const char *name){
  static char buf[256]; u32 (WINAPI *f)(const char *, char *, u32) = win32_resolve("kernel32", "GetEnvironmentVariableA"); if(!f) return 0;
  u32 r = f(name, buf, sizeof buf); return (r && r < sizeof buf) ? buf : 0;
}
static char *WINAPI c2_setlocale(int cat, const char *loc){ (void)cat; (void)loc; return (char *)"C"; }
static void *WINAPI c2_localeconv(void){
  static u8 lc[160]; static char dot[] = ".", empty[] = ""; if(!*(char **)lc){ memset(lc, 0, sizeof lc); ((char **)lc)[0] = dot; for(int i = 1; i < 10; i++) ((char **)lc)[i] = empty; static wc16 wdot[2] = {'.', 0}, wempty[1] = {0}; ((wc16 **)lc)[14 + 0] = wdot; for(int i = 1; i < 6; i++) ((wc16 **)lc)[14 + i] = wempty; }
  return lc;
}
static u32 WINAPI c2____lc_codepage_func(void){ return 65001; }
static int WINAPI c2____mb_cur_max_func(void){ return 4; }
/* ---- time (local time is UTC) ---- */
struct tm_ { i32 sec, min, hour, mday, mon, year, wday, yday, isdst; };
static i64 days_from_civil(i64 y, unsigned m, unsigned d){ y -= m <= 2; i64 era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400); unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + (i64)doe - 719468; }
static void civil_from_days(i64 z, i64 *y, unsigned *m, unsigned *d){ z += 719468; i64 era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097); unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; i64 yy = (i64)yoe + era * 400; unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); unsigned mp = (5 * doy + 2) / 153; *d = doy - (153 * mp + 2) / 5 + 1; *m = mp < 10 ? mp + 3 : mp - 9; *y = yy + (*m <= 2); }
static void gmt(i64 t, struct tm_ *o){
  i64 days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400); i64 rem = t - days * 86400; i64 y; unsigned m, d; civil_from_days(days, &y, &m, &d);
  o->sec = (i32)(rem % 60); o->min = (i32)(rem / 60 % 60); o->hour = (i32)(rem / 3600); o->mday = (i32)d; o->mon = (i32)m - 1; o->year = (i32)(y - 1900);
  o->wday = (i32)(((days % 7) + 11) % 7); o->yday = (i32)(days - days_from_civil(y, 1, 1)); o->isdst = 0;
}
static struct tm_ *WINAPI c2__localtime64(const i64 *t){ static struct tm_ r; gmt(*t, &r); return &r; }
static int WINAPI c2__localtime64_s(struct tm_ *o, const i64 *t){ gmt(*t, o); return 0; }
static i64 WINAPI c2__mktime64(struct tm_ *t){
  i64 y = t->year + 1900 + t->mon / 12; int mo = t->mon % 12; if(mo < 0){ mo += 12; y--; }
  i64 days = days_from_civil(y, (unsigned)mo + 1, 1) + t->mday - 1; i64 r = days * 86400 + t->hour * 3600 + t->min * 60 + t->sec; gmt(r, t); return r;
}
static u64 WINAPI c2_strftime(char *buf, u64 max, const char *fmt, const struct tm_ *t){
  static const char *const wd[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *const mn[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  u64 o = 0; char tmp[32];
  #define PUT(s) do { for(const char *q = (s); *q && o + 1 < max; q++) buf[o++] = *q; } while(0)
  #define NUM(v, w) do { u64 vv = (u64)(v); char x[24]; int n = 0; do { x[n++] = (char)('0' + vv % 10); vv /= 10; } while(vv); int pad = (w) - n; while(pad-- > 0 && o + 1 < max) buf[o++] = '0'; while(n && o + 1 < max) buf[o++] = x[--n]; } while(0)
  for(; *fmt && o + 1 < max; fmt++){
    if(*fmt != '%'){ buf[o++] = *fmt; continue; }
    switch(*++fmt){
      case 'Y': NUM(t->year + 1900, 4); break; case 'y': NUM((t->year + 1900) % 100, 2); break; case 'm': NUM(t->mon + 1, 2); break; case 'd': NUM(t->mday, 2); break; case 'e': if(t->mday < 10) PUT(" "); NUM(t->mday, 1); break;
      case 'H': NUM(t->hour, 2); break; case 'M': NUM(t->min, 2); break; case 'S': NUM(t->sec, 2); break; case 'I': NUM(t->hour % 12 ? t->hour % 12 : 12, 2); break; case 'p': PUT(t->hour < 12 ? "AM" : "PM"); break;
      case 'j': NUM(t->yday + 1, 3); break; case 'a': { tmp[0] = wd[t->wday % 7][0]; tmp[1] = wd[t->wday % 7][1]; tmp[2] = wd[t->wday % 7][2]; tmp[3] = 0; PUT(tmp); break; } case 'A': PUT(wd[t->wday % 7]); break;
      case 'b': case 'h': { const char *m = mn[t->mon % 12]; tmp[0] = m[0]; tmp[1] = m[1]; tmp[2] = m[2]; tmp[3] = 0; PUT(tmp); break; } case 'B': PUT(mn[t->mon % 12]); break;
      case 'F': NUM(t->year + 1900, 4); PUT("-"); NUM(t->mon + 1, 2); PUT("-"); NUM(t->mday, 2); break; case 'T': NUM(t->hour, 2); PUT(":"); NUM(t->min, 2); PUT(":"); NUM(t->sec, 2); break;
      case 'D': NUM(t->mon + 1, 2); PUT("/"); NUM(t->mday, 2); PUT("/"); NUM((t->year + 1900) % 100, 2); break; case 'Z': PUT("UTC"); break; case 'z': PUT("+0000"); break; case '%': PUT("%"); break; default: break;
    }
  }
  buf[o] = 0; return o;
}
/* ---- stdio stubs ---- */
static int WINAPI c2_ungetc(int c, void *f){ (void)c; (void)f; return -1; }
static int WINAPI c2_ferror(void *f){ (void)f; return 0; }
static int WINAPI c2_fgetwc(void *f){ (void)f; return 0xFFFF; }
static int WINAPI c2_fputwc(wc16 c, void *f){ (void)f; return c; }
static int WINAPI c2__getc_nolock(void *f){ (void)f; return -1; }
static int WINAPI c2__dup(int fd){ return fd; }
static int WINAPI c2__dup2(int a, int b){ (void)a; (void)b; return 0; }
static i64 WINAPI c2__get_osfhandle(int fd){ return fd == 0 ? 3 : fd == 1 ? 7 : fd == 2 ? 11 : (i64)-1; }
static int WINAPI c2__open_osfhandle(i64 h, int fl){ (void)fl; return h == 3 ? 0 : h == 7 ? 1 : h == 11 ? 2 : -1; }
static void WINAPI c2__lock_file(void *f){ (void)f; }
static u64 WINAPI c2__msize(void *p){ return heap_size(p); }
static int WINAPI c2___stdio_common_vfwprintf(u64 o, void *f, const wc16 *fmt, void *l, void *ap){ (void)o; (void)f; (void)fmt; (void)l; (void)ap; return -1; }
static int WINAPI c2___stdio_common_vswprintf(u64 o, wc16 *b, u64 n, const wc16 *fmt, void *l, void *ap){ (void)o; (void)fmt; (void)l; (void)ap; if(b && n) b[0] = 0; return -1; }

const struct export crt2_exports[] = {
  C2(strtol), C2(strtoll), C2(strtoul), C2(strtoull), C2(wcstol), C2(wcstoll), C2(wcstoul), C2(wcstoull), C2(_ultoa), C2(_ultow), C2(_itow), C2(_stricmp), C2(_strnicmp), C2(_wcsnicmp),
  C2(isblank), C2(isxdigit), C2(towlower), C2(iswctype), C2(strcspn), C2(strnlen), C2(strpbrk), C2(strerror), C2(memchr), C2(strrchr), C2(wcschr), C2(wcslen), C2(wcsnlen), C2(wcscpy), C2(wcscat),
  C2(wcscmp), C2(wcsncmp), C2(mbrtowc), C2(mbrlen), C2(wcrtomb), C2(_getpid), C2(ldiv), C2(qsort), C2(getenv), C2(setlocale), C2(localeconv), C2(___lc_codepage_func), C2(___mb_cur_max_func),
  C2(_localtime64), C2(_localtime64_s), C2(_mktime64), C2(strftime), C2(ungetc), C2(ferror), C2(fgetwc), C2(fputwc), C2(_getc_nolock), C2(_dup), C2(_dup2), C2(_get_osfhandle), C2(_open_osfhandle),
  C2(_lock_file), {"msvcrt", "_unlock_file", c2__lock_file}, C2(_msize), C2(__stdio_common_vfwprintf), C2(__stdio_common_vswprintf),
};
const unsigned crt2_nexports = sizeof crt2_exports / sizeof *crt2_exports;

/* ---- ntdll odds and ends ---- */
static int WINAPI nt_RtlQueryPerformanceCounter(u64 *c){ *c = (u64)ticks * 1000; return 1; }
static int WINAPI nt_RtlQueryPerformanceFrequency(u64 *f){ *f = 1000000; return 1; }
static void WINAPI nt_peb_lock(void){}
static void WINAPI nt_RtlFreeUnicodeString(void *us){ (void)us; }
static void *WINAPI nt_RtlImageNtHeader(void *base){ const u8 *b = base; if(b[0] != 'M' || b[1] != 'Z') return 0; return (void *)(b + *(const u32 *)(b + 0x3C)); }
static i32 WINAPI nt_RtlUTF8ToUnicodeN(wc16 *dst, u32 dmax, u32 *dlen, const char *src, u32 slen){ u32 o = 0; for(u32 i = 0; i < slen; ){ wc16 w; u64 r = c2_mbrtowc(&w, src + i, slen - i, 0); if(r == (u64)-1 || r == (u64)-2 || r == 0){ w = 0xFFFD; r = 1; } if(dst && (o + 1) * 2 > dmax) break; if(dst) dst[o] = w; o++; i += (u32)r; } if(dlen) *dlen = o * 2; return 0; }
static i32 WINAPI nt_RtlUnicodeToUTF8N(char *dst, u32 dmax, u32 *dlen, const wc16 *src, u32 slen){ u32 o = 0; for(u32 i = 0; i < slen / 2; i++){ char t[4]; u32 n = (u32)c2_wcrtomb(t, src[i], 0); if(dst && o + n > dmax) break; if(dst) for(u32 k = 0; k < n; k++) dst[o + k] = t[k]; o += n; } if(dlen) *dlen = o; return 0; }
const struct export nt2_exports[] = {
  {"ntdll", "RtlQueryPerformanceCounter", nt_RtlQueryPerformanceCounter}, {"ntdll", "RtlQueryPerformanceFrequency", nt_RtlQueryPerformanceFrequency}, {"ntdll", "RtlAcquirePebLock", nt_peb_lock},
  {"ntdll", "RtlReleasePebLock", nt_peb_lock}, {"ntdll", "RtlFreeUnicodeString", nt_RtlFreeUnicodeString}, {"ntdll", "RtlImageNtHeader", nt_RtlImageNtHeader},
  {"ntdll", "RtlUTF8ToUnicodeN", nt_RtlUTF8ToUnicodeN}, {"ntdll", "RtlUnicodeToUTF8N", nt_RtlUnicodeToUTF8N},
};
const unsigned nt2_nexports = sizeof nt2_exports / sizeof *nt2_exports;
