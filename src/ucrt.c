#include "kernel.h"
/* The Universal C runtime (ucrtbase / api-ms-win-crt-*): stdio on Win32 file handles, printf/scanf with floating point and wide
   strings, wide start-up (argv/environment), locale and ctype tables, string and conversion helpers, low-level I/O (_open/_read...),
   time formatting, per-thread errno. Built WITH SSE (see the Makefile) so it can format and parse doubles.
   Its table is searched before the older, simpler versions in win32.c, crt2.c and fs.c. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE; typedef u16 wc; typedef short i16;
#define W_ WINAPI
static u64 strnlen(const char *s, u64 n){ u64 i=0; while(i<n && s[i]) i++; return i; }
static char *strchr(const char *s, int c){ for(;;s++){ if(*s==(char)c) return (char*)s; if(!*s) return 0; } }
static void *memchr(const void *p, int c, u64 n){ const u8 *b=p; for(u64 i=0;i<n;i++) if(b[i]==(u8)c) return (void*)(b+i); return 0; }
static int rdrand32(u32 *v){ u8 ok; __asm__ volatile("rdrand %0; setc %1":"=r"(*v),"=qm"(ok)); return ok; }
static void *K(const char *n){ return win32_resolve("kernel32",n); }
#define KCALL(type,name) ((type)K(#name))

/* ================= errno (per thread, in the TEB) ================= */
static u32 kern_errno[2];
static int *errno_p(void){ u64 *t=win32_cur_teb(); return t? (int*)((u8*)t+0x1B00) : (int*)&kern_errno[0]; }
static u32 *doserrno_p(void){ u64 *t=win32_cur_teb(); return t? (u32*)((u8*)t+0x1B04) : &kern_errno[1]; }
static void set_errno(int e){ *errno_p()=e; }
#define EINVAL 22
#define ERANGE 34
#define ENOENT 2
#define EBADF 9
#define ENOMEM 12
#define EEXIST 17
#define EACCES 13
static int *W_ u__errno(void){ return errno_p(); }
static u32 *W_ u___doserrno(void){ return doserrno_p(); }
static int W_ u__get_errno(int *e){ if(!e) return EINVAL; *e=*errno_p(); return 0; }
static int W_ u__set_errno(int e){ *errno_p()=e; return 0; }
static void W_ u__invalid_parameter_noinfo(void){}
static void W_ u__invalid_parameter_noinfo_noreturn(void){ kprintf("\n*** C runtime: invalid parameter (fatal)\n"); proc_exit(0xC0000417); }
static u64 inv_handler;
static u64 W_ u__get_invalid_parameter_handler(void){ return inv_handler; }
static u64 W_ u__set_invalid_parameter_handler(u64 h){ u64 o=inv_handler; inv_handler=h; return o; }
static u64 W_ u__set_thread_local_invalid_parameter_handler(u64 h){ return u__set_invalid_parameter_handler(h); }

/* ================= UTF-8 / UTF-16 ================= */
static int utf8_enc(u32 c, char *o){
  if(c<0x80){ o[0]=(char)c; return 1; }
  if(c<0x800){ o[0]=(char)(0xC0|c>>6); o[1]=(char)(0x80|(c&63)); return 2; }
  if(c<0x10000){ o[0]=(char)(0xE0|c>>12); o[1]=(char)(0x80|((c>>6)&63)); o[2]=(char)(0x80|(c&63)); return 3; }
  o[0]=(char)(0xF0|c>>18); o[1]=(char)(0x80|((c>>12)&63)); o[2]=(char)(0x80|((c>>6)&63)); o[3]=(char)(0x80|(c&63)); return 4;
}
static u32 utf8_dec(const u8 **pp){                                /* invalid bytes decode as Latin-1 */
  const u8 *p=*pp; u32 c=*p++;
  if(c>=0xC0 && c<0xF8){ int n= c>=0xF0? 3 : c>=0xE0? 2 : 1; u32 v=c&(0x3F>>n); int ok=1; for(int i=0;i<n;i++){ if((p[i]&0xC0)!=0x80){ ok=0; break; } v=(v<<6)|(p[i]&63); } if(ok){ p+=n; c=v; } }
  *pp=p; return c;
}
static wc *a2w_dup(const char *s){                                 /* heap copy, UTF-8 -> UTF-16 */
  if(!s) return 0; u64 n=0; for(const u8 *p=(const u8*)s;*p;){ u32 c=utf8_dec(&p); n+= c>=0x10000? 2 : 1; }
  wc *w=heap_alloc((n+1)*2); if(!w) return 0; u64 k=0;
  for(const u8 *p=(const u8*)s;*p;){ u32 c=utf8_dec(&p); if(c>=0x10000){ c-=0x10000; w[k++]=(wc)(0xD800|(c>>10)); w[k++]=(wc)(0xDC00|(c&0x3FF)); } else w[k++]=(wc)c; }
  w[k]=0; return w;
}
static u64 w2a_buf(const wc *w, char *a, u64 n){                  /* UTF-16 -> UTF-8 into a buffer of n bytes (always terminated) */
  u64 k=0; if(!n) return 0;
  for(;w && *w;w++){ u32 c=*w; if(c>=0xD800 && c<0xDC00 && w[1]>=0xDC00 && w[1]<0xE000){ c=0x10000+((c-0xD800)<<10)+(w[1]-0xDC00); w++; }
    char t[4]; int l=utf8_enc(c,t); if(k+l>=n) break; for(int i=0;i<l;i++) a[k++]=t[i]; }
  a[k]=0; return k;
}
static char *w2a_dup(const wc *w){ if(!w) return 0; u64 n=0; while(w[n]) n++; char *a=heap_alloc(n*3+1); if(a) w2a_buf(w,a,n*3+1); return a; }
#define WPATH(var,w) char var[300]; w2a_buf(w,var,sizeof var)
static u64 wlen(const wc *s){ u64 n=0; while(s[n]) n++; return n; }

/* ================= ctype ================= */
static u16 ctab[384]; static int ctab_ok;                          /* index -128..255 (signed chars and EOF work) */
static const u16 *ctype(void){
  if(!ctab_ok){ for(int c=0;c<256;c++){ u16 f=0;
      if(c>='A'&&c<='Z') f|=0x101; if(c>='a'&&c<='z') f|=0x102; if(c>='0'&&c<='9') f|=0x4;
      if(c==' '||(c>='\t'&&c<='\r')) f|=0x8; if(c==' '||c=='\t') f|=0x40; if(c<32||c==127) f|=0x20;
      if((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')) f|=0x80;
      if(c>32 && c<127 && !(f&0x107)) f|=0x10;
      ctab[128+c]=f; }
    ctab_ok=1; }
  return ctab+128;
}
static const u16 *W_ u___pctype_func(void){ return ctype(); }
static int W_ u__isctype(int c, int m){ return (c>=-128 && c<256)? ctype()[c]&m : 0; }
static int W_ u__isctype_l(int c, int m, void *l){ (void)l; return u__isctype(c,m); }
#define CT(name,mask) static int W_ u_##name(int c){ return (c>=-128 && c<256)? ctype()[c]&(mask) : 0; }
CT(isalnum,0x107) CT(islower,0x2) CT(isupper,0x1) CT(isprint,0x1D7|0x40) CT(ispunct,0x10) CT(iscntrl,0x20) CT(isgraph,0x197) CT(isalpha,0x103) CT(isdigit,0x4) CT(isspace,0x8)
static int wctype_of(u32 c){ return c<256? ctype()[c] : (c>=0xC0 && c!=0xD7 && c!=0xF7)? 0x103 : 0; }
#define WCT(name,mask) static int W_ u_##name(u32 c){ return wctype_of(c&0xFFFF)&(mask); }
WCT(iswdigit,0x4) WCT(iswspace,0x8) WCT(iswalpha,0x103) WCT(iswalnum,0x107) WCT(iswupper,0x1) WCT(iswlower,0x2) WCT(iswpunct,0x10) WCT(iswprint,0x1D7|0x40) WCT(iswxdigit,0x80) WCT(iswcntrl,0x20) WCT(iswgraph,0x197)
static u32 tolow(u32 c){ return (c>='A'&&c<='Z')||(c>=0xC0&&c<=0xDE&&c!=0xD7)? c+32 : c; }
static u32 toupp(u32 c){ return (c>='a'&&c<='z')||(c>=0xE0&&c<=0xFE&&c!=0xF7)? c-32 : c; }
static u32 W_ u_towupper(u32 c){ return toupp(c&0xFFFF); }
static u32 W_ u_towlower(u32 c){ return tolow(c&0xFFFF); }
static int W_ u__tolower(int c){ return c+32; }
static int W_ u__toupper(int c){ return c-32; }
static u32 W_ u__towlower_l(u32 c, void *l){ (void)l; return tolow(c&0xFFFF); }
static u32 W_ u__towupper_l(u32 c, void *l){ (void)l; return toupp(c&0xFFFF); }
static int W_ u__iswctype_l(u32 c, u16 m, void *l){ (void)l; return wctype_of(c&0xFFFF)&m; }
static int W_ u___iscsym(int c){ return u_isalnum(c)||c=='_'; }
static int W_ u___iscsymf(int c){ return u_isalpha(c)||c=='_'; }

/* ================= locale (always "C") ================= */
static void W_ u__lock_locales(void){}
static wc *lc_names[6];
static wc **W_ u____lc_locale_name_func(void){ return lc_names; }
static u32 W_ u____lc_collate_cp_func(void){ return 0; }
static wc lc_C[2]={'C',0};
static wc *W_ u__wsetlocale(int cat, const wc *loc){ (void)cat;(void)loc; return lc_C; }
struct locale_t_ { void *a, *b; };
static void *W_ u__create_locale(int cat, const char *l){ (void)cat;(void)l; return heap_alloc(16); }
static void *W_ u__wcreate_locale(int cat, const wc *l){ (void)cat;(void)l; return heap_alloc(16); }
static void W_ u__free_locale(void *l){ heap_free(l); }
static void *W_ u__get_current_locale(void){ return heap_alloc(16); }
static int W_ u___acrt_get_locale_data_prefix(void){ return 0; }

/* ================= strings ================= */
static int W_ u_strcpy_s(char *d, u64 n, const char *s){ if(!d||!n){ return EINVAL; } if(!s){ d[0]=0; return EINVAL; } u64 l=strlen(s); if(l>=n){ d[0]=0; return ERANGE; } memcpy(d,s,l+1); return 0; }
static int W_ u_strcat_s(char *d, u64 n, const char *s){ if(!d||!s) return EINVAL; u64 a=strnlen(d,n); if(a>=n) return EINVAL; return u_strcpy_s(d+a,n-a,s); }
#define TRUNC ((u64)-1)
static int W_ u_strncpy_s(char *d, u64 n, const char *s, u64 cnt){
  if(!d||!n) return EINVAL; if(!s){ d[0]=0; return cnt? EINVAL : 0; }
  u64 l=strnlen(s,cnt==TRUNC? n : cnt); if(l>=n){ if(cnt==TRUNC){ memcpy(d,s,n-1); d[n-1]=0; return 80; } d[0]=0; return ERANGE; }   /* STRUNCATE */
  memcpy(d,s,l); d[l]=0; return 0;
}
static int W_ u_strncat_s(char *d, u64 n, const char *s, u64 cnt){ if(!d||!s) return EINVAL; u64 a=strnlen(d,n); if(a>=n) return EINVAL; return u_strncpy_s(d+a,n-a,s,cnt); }
static char *W_ u_strncat(char *d, const char *s, u64 n){ char *e=d+strlen(d); while(n-- && *s) *e++=*s++; *e=0; return d; }
static u64 W_ u_strspn(const char *s, const char *a){ u64 n=0; while(s[n] && strchr(a,s[n])) n++; return n; }
static char *W_ u__strdup(const char *s){ if(!s) return 0; u64 l=strlen(s); char *d=heap_alloc_prog(l+1); if(d) memcpy(d,s,l+1); return d; }
static char *strtok_state;
static char *W_ u_strtok_s(char *s, const char *dl, char **ctx){
  if(!s) s=*ctx; if(!s) return 0; s+=u_strspn(s,dl); if(!*s){ *ctx=s; return 0; }
  char *e=s; while(*e && !strchr(dl,*e)) e++; if(*e) *e++=0; *ctx=e; return s;
}
static char *W_ u_strtok(char *s, const char *dl){ return u_strtok_s(s,dl,&strtok_state); }
static char *W_ u__strupr(char *s){ for(char *p=s;*p;p++) *p=(char)toupp((u8)*p); return s; }
static char *W_ u__strlwr(char *s){ for(char *p=s;*p;p++) *p=(char)tolow((u8)*p); return s; }
static int W_ u__strupr_s(char *s, u64 n){ (void)n; u__strupr(s); return 0; }
static int W_ u__strlwr_s(char *s, u64 n){ (void)n; u__strlwr(s); return 0; }
static char *W_ u__strrev(char *s){ u64 n=strlen(s); for(u64 i=0;i<n/2;i++){ char t=s[i]; s[i]=s[n-1-i]; s[n-1-i]=t; } return s; }
static int W_ u__memicmp(const void *a, const void *b, u64 n){ const u8 *x=a,*y=b; for(;n;n--,x++,y++){ int d=(int)tolow(*x)-(int)tolow(*y); if(d) return d; } return 0; }
static int W_ u_memcpy_s(void *d, u64 dn, const void *s, u64 n){ if(!n) return 0; if(!d||!s||dn<n){ if(d&&dn) memset(d,0,dn); return d? ERANGE : EINVAL; } memcpy(d,s,n); return 0; }
static int W_ u_memmove_s(void *d, u64 dn, const void *s, u64 n){ if(!n) return 0; if(!d||!s||dn<n) return ERANGE; memmove(d,s,n); return 0; }
static int W_ u_wmemcpy_s(wc *d, u64 dn, const wc *s, u64 n){ return u_memcpy_s(d,dn*2,s,n*2); }
static int W_ u_wmemmove_s(wc *d, u64 dn, const wc *s, u64 n){ return u_memmove_s(d,dn*2,s,n*2); }
static u64 W_ u___strncnt(const char *s, u64 n){ return strnlen(s,n); }
static int W_ u_strcoll(const char *a, const char *b){ return strcmp(a,b); }
static int W_ u__stricoll(const char *a, const char *b){ for(;;a++,b++){ int d=(int)tolow((u8)*a)-(int)tolow((u8)*b); if(d||!*a) return d; } }
static u64 W_ u_strxfrm(char *d, const char *s, u64 n){ u64 l=strlen(s); if(l<n) memcpy(d,s,l+1); return l; }
/* wide */
static wc *W_ u__wcsdup(const wc *s){ if(!s) return 0; u64 l=wlen(s); wc *d=heap_alloc_prog((l+1)*2); if(d) memcpy(d,s,(l+1)*2); return d; }
static wc *W_ u_wmemcpy(wc *d, const wc *s, u64 n){ memcpy(d,s,n*2); return d; }
static wc *W_ u_wmemmove(wc *d, const wc *s, u64 n){ memmove(d,s,n*2); return d; }
static wc *W_ u_wmemset(wc *d, wc c, u64 n){ for(u64 i=0;i<n;i++) d[i]=c; return d; }
static wc *W_ u_wmemchr(const wc *s, wc c, u64 n){ for(u64 i=0;i<n;i++) if(s[i]==c) return (wc*)s+i; return 0; }
static int W_ u_wmemcmp(const wc *a, const wc *b, u64 n){ for(u64 i=0;i<n;i++) if(a[i]!=b[i]) return a[i]<b[i]? -1 : 1; return 0; }
static wc *W_ u_wcsrchr(const wc *s, wc c){ const wc *r=0; for(;;s++){ if(*s==c) r=s; if(!*s) return (wc*)r; } }
static wc *W_ u_wcsstr(const wc *h, const wc *n){ if(!*n) return (wc*)h; for(;*h;h++){ u64 i=0; while(n[i] && h[i]==n[i]) i++; if(!n[i]) return (wc*)h; } return 0; }
static int inw(const wc *set, wc c){ for(;*set;set++) if(*set==c) return 1; return 0; }
static u64 W_ u_wcsspn(const wc *s, const wc *a){ u64 n=0; while(s[n] && inw(a,s[n])) n++; return n; }
static u64 W_ u_wcscspn(const wc *s, const wc *a){ u64 n=0; while(s[n] && !inw(a,s[n])) n++; return n; }
static wc *W_ u_wcspbrk(const wc *s, const wc *a){ for(;*s;s++) if(inw(a,*s)) return (wc*)s; return 0; }
static wc *W_ u_wcsncpy(wc *d, const wc *s, u64 n){ u64 i=0; for(;i<n && s[i];i++) d[i]=s[i]; for(;i<n;i++) d[i]=0; return d; }
static wc *W_ u_wcsncat(wc *d, const wc *s, u64 n){ wc *e=d+wlen(d); while(n-- && *s) *e++=*s++; *e=0; return d; }
static int W_ u_wcscpy_s(wc *d, u64 n, const wc *s){ if(!d||!n) return EINVAL; if(!s){ d[0]=0; return EINVAL; } u64 l=wlen(s); if(l>=n){ d[0]=0; return ERANGE; } memcpy(d,s,(l+1)*2); return 0; }
static int W_ u_wcscat_s(wc *d, u64 n, const wc *s){ if(!d||!s) return EINVAL; u64 a=0; while(a<n && d[a]) a++; if(a>=n) return EINVAL; return u_wcscpy_s(d+a,n-a,s); }
static int W_ u_wcsncpy_s(wc *d, u64 n, const wc *s, u64 cnt){
  if(!d||!n) return EINVAL; if(!s){ d[0]=0; return cnt? EINVAL : 0; }
  u64 l=0; u64 lim= cnt==TRUNC? n : cnt; while(l<lim && s[l]) l++;
  if(l>=n){ if(cnt==TRUNC){ memcpy(d,s,(n-1)*2); d[n-1]=0; return 80; } d[0]=0; return ERANGE; }
  memcpy(d,s,l*2); d[l]=0; return 0;
}
static int W_ u_wcsncat_s(wc *d, u64 n, const wc *s, u64 cnt){ if(!d||!s) return EINVAL; u64 a=0; while(a<n && d[a]) a++; if(a>=n) return EINVAL; return u_wcsncpy_s(d+a,n-a,s,cnt); }
static wc *W_ u_wcstok_s(wc *s, const wc *dl, wc **ctx){
  if(!s) s=*ctx; if(!s) return 0; s+=u_wcsspn(s,dl); if(!*s){ *ctx=s; return 0; }
  wc *e=s; while(*e && !inw(dl,*e)) e++; if(*e) *e++=0; *ctx=e; return s;
}
static wc *wcstok_state;
static wc *W_ u_wcstok(wc *s, const wc *dl, wc **ctx){ return u_wcstok_s(s,dl,ctx? ctx : &wcstok_state); }
static int W_ u__wcsicmp(const wc *a, const wc *b){ for(;;a++,b++){ int d=(int)tolow(*a)-(int)tolow(*b); if(d||!*a) return d; } }
static wc *W_ u__wcslwr(wc *s){ for(wc *p=s;*p;p++) *p=(wc)tolow(*p); return s; }
static wc *W_ u__wcsupr(wc *s){ for(wc *p=s;*p;p++) *p=(wc)toupp(*p); return s; }
static int W_ u__wcslwr_s(wc *s, u64 n){ (void)n; if(!s) return EINVAL; u__wcslwr(s); return 0; }
static int W_ u__wcsupr_s(wc *s, u64 n){ (void)n; if(!s) return EINVAL; u__wcsupr(s); return 0; }
static wc *W_ u__wcsrev(wc *s){ u64 n=wlen(s); for(u64 i=0;i<n/2;i++){ wc t=s[i]; s[i]=s[n-1-i]; s[n-1-i]=t; } return s; }
static wc *W_ u__wcsset(wc *s, wc c){ for(wc *p=s;*p;p++) *p=c; return s; }
static wc *W_ u__wcsnset(wc *s, wc c, u64 n){ for(wc *p=s;*p&&n;p++,n--) *p=c; return s; }
static int W_ u_wcscoll(const wc *a, const wc *b){ for(;*a && *a==*b;a++,b++); return (int)*a-(int)*b; }
static u64 W_ u_wcsxfrm(wc *d, const wc *s, u64 n){ u64 l=wlen(s); if(l<n) memcpy(d,s,(l+1)*2); return l; }
/* multibyte = UTF-8 */
static int W_ u_mbtowc(wc *w, const char *s, u64 n){ if(!s) return 0; if(!n) return -1; if(!*s){ if(w) *w=0; return 0; } const u8 *p=(const u8*)s; u32 c=utf8_dec(&p); if(w) *w=(wc)c; return (int)(p-(const u8*)s); }
static int W_ u_mblen(const char *s, u64 n){ return u_mbtowc(0,s,n); }
static int W_ u__mbclen(const u8 *s){ const u8 *p=s; utf8_dec(&p); return (int)(p-s); }
static int W_ u_wctomb(char *s, wc c){ if(!s) return 0; return utf8_enc(c,s); }
static u32 W_ u_btowc(int c){ return c<0? 0xFFFF : (u32)c; }
static int W_ u_wctob(u32 c){ return c<128? (int)c : -1; }
static u64 W_ u_mbstowcs(wc *d, const char *s, u64 n){
  u64 k=0; for(const u8 *p=(const u8*)s;*p;){ u32 c=utf8_dec(&p); if(d){ if(k>=n) return k; d[k]=(wc)c; } k++; }
  if(d && k<n) d[k]=0; return k;
}
static u64 W_ u_wcstombs(char *d, const wc *s, u64 n){
  u64 k=0; for(;*s;s++){ char t[4]; int l=utf8_enc(*s,t); if(d){ if(k+l>n) return k; memcpy(d+k,t,l); } k+=l; }
  if(d && k<n) d[k]=0; return k;
}
static int W_ u_mbstowcs_s(u64 *r, wc *d, u64 dn, const char *s, u64 cnt){ (void)cnt; u64 k=u_mbstowcs(d,s,d? dn-1 : 0); if(d) d[k<dn? k : dn-1]=0; if(r) *r=k+1; return 0; }
static int W_ u__mbstowcs_s_l(u64 *r, wc *d, u64 dn, const char *s, u64 cnt, void *l){ (void)l; return u_mbstowcs_s(r,d,dn,s,cnt); }
static int W_ u_wcstombs_s(u64 *r, char *d, u64 dn, const wc *s, u64 cnt){ (void)cnt; u64 k=u_wcstombs(d,s,d? dn-1 : 0); if(d) d[k<dn? k : dn-1]=0; if(r) *r=k+1; return 0; }
static u64 W_ u__wcstombs_l(char *d, const wc *s, u64 n, void *l){ (void)l; return u_wcstombs(d,s,n); }
static u64 W_ u_mbsrtowcs(wc *d, const char **src, u64 n, void *st){ (void)st; u64 k=u_mbstowcs(d,*src,n); if(d && k<n) *src=0; return k; }
static u64 W_ u_wcsrtombs(char *d, const wc **src, u64 n, void *st){ (void)st; u64 k=u_wcstombs(d,*src,n); if(d && k<n) *src=0; return k; }
static u8 *W_ u__mbsinc(const u8 *s){ const u8 *p=s; utf8_dec(&p); return (u8*)p; }
static u8 *W_ u__mbsdec(const u8 *start, const u8 *s){ if(s<=start) return 0; s--; while(s>start && (*s&0xC0)==0x80) s--; return (u8*)s; }
static u64 W_ u__mbslen(const u8 *s){ u64 n=0; while(*s){ utf8_dec(&s); n++; } return n; }
static u8 *W_ u__mbschr(const u8 *s, u32 c){ return (u8*)strchr((const char*)s,(int)c); }

/* ================= numbers ================= */
static long double pow10l_(int e){ long double r=1, b=10; int n=e<0? -e : e; while(n){ if(n&1) r*=b; b*=b; n>>=1; } return e<0? 1/r : r; }
static double parse_double(const u32 *s, u64 *used){                /* s: code points (narrow or wide input converted) */
  u64 i=0; while(s[i]==' '||(s[i]>='\t'&&s[i]<='\r')) i++;
  int neg=0; if(s[i]=='+'||s[i]=='-'){ neg=s[i]=='-'; i++; }
  u64 st=i;
  if((s[i]|32)=='i' && (s[i+1]|32)=='n' && (s[i+2]|32)=='f'){ i+=3; if((s[i]|32)=='i'&&(s[i+1]|32)=='n'&&(s[i+2]|32)=='i'&&(s[i+3]|32)=='t'&&(s[i+4]|32)=='y') i+=5; *used=i; return neg? -__builtin_inf() : __builtin_inf(); }
  if((s[i]|32)=='n' && (s[i+1]|32)=='a' && (s[i+2]|32)=='n'){ i+=3; if(s[i]=='('){ u64 j=i+1; while(s[j] && s[j]!=')') j++; if(s[j]==')') i=j+1; } *used=i; return neg? -__builtin_nan("") : __builtin_nan(""); }
  if(s[i]=='0' && (s[i+1]|32)=='x'){                                 /* hex float */
    u64 j=i+2; u64 m=0; int e=0, any=0, dot=0;
    for(;;j++){ u32 c=s[j]; int d= c>='0'&&c<='9'? (int)c-'0' : (c|32)>='a'&&(c|32)<='f'? (int)(c|32)-'a'+10 : -1;
      if(c=='.' && !dot){ dot=1; continue; } if(d<0) break; any=1;
      if(m<(1ull<<59)){ m=m*16+d; if(dot) e-=4; } else if(!dot) e+=4; }
    if(any){ if((s[j]|32)=='p'){ u64 k=j+1; int en=0, sg=1; if(s[k]=='+'||s[k]=='-'){ sg= s[k]=='-'? -1 : 1; k++; } if(s[k]>='0'&&s[k]<='9'){ while(s[k]>='0'&&s[k]<='9'){ if(en<100000) en=en*10+(int)(s[k]-'0'); k++; } e+=sg*en; j=k; } }
      long double v=(long double)m; while(e>0){ v*=2; e--; } while(e<0){ v/=2; e++; } *used=j; return (double)(neg? -v : v); }
    *used=i+1; return neg? -0.0 : 0.0;
  }
  u64 m=0; int dig=0, e=0, any=0, dot=0;
  for(;;i++){ u32 c=s[i]; if(c=='.' && !dot){ dot=1; continue; } if(c<'0'||c>'9') break; any=1;
    if(dig<19){ if(m||c!='0'){ m=m*10+(c-'0'); dig++; } if(dot) e--; } else if(!dot) e++; }
  if(!any){ *used=0; (void)st; return 0; }
  if((s[i]|32)=='e'){ u64 k=i+1; int en=0, sg=1; if(s[k]=='+'||s[k]=='-'){ sg= s[k]=='-'? -1 : 1; k++; } if(s[k]>='0'&&s[k]<='9'){ while(s[k]>='0'&&s[k]<='9'){ if(en<100000) en=en*10+(int)(s[k]-'0'); k++; } e+=sg*en; i=k; } }
  long double v=(long double)m;
  if(m){ if(e>4950) v=__builtin_infl(); else if(e<-4950) v=0; else if(e<-4900){ v*=pow10l_(-4900); v*=pow10l_(e+4900); } else v*=pow10l_(e); }
  double r=(double)v; if(m && (r==__builtin_inf() || r==0)) set_errno(ERANGE);
  *used=i; return neg? -r : r;
}
static double strtod_a(const char *s, char **end){ u32 b[400]; u64 n=0; for(;s[n] && n<399;n++) b[n]=(u8)s[n]; b[n]=0; u64 u; double r=parse_double(b,&u); if(end) *end=(char*)s+u; return r; }
static double strtod_w(const wc *s, wc **end){ u32 b[400]; u64 n=0; for(;s[n] && n<399;n++) b[n]=s[n]; b[n]=0; u64 u; double r=parse_double(b,&u); if(end) *end=(wc*)s+u; return r; }
static double W_ u_strtod(const char *s, char **e){ return strtod_a(s,e); }
static float W_ u_strtof(const char *s, char **e){ return (float)strtod_a(s,e); }
static double W_ u_wcstod(const wc *s, wc **e){ return strtod_w(s,e); }
static float W_ u_wcstof(const wc *s, wc **e){ return (float)strtod_w(s,e); }
static double W_ u_atof(const char *s){ return strtod_a(s,0); }
static double W_ u__wtof(const wc *s){ return strtod_w(s,0); }
static double W_ u__strtod_l(const char *s, char **e, void *l){ (void)l; return strtod_a(s,e); }
static double W_ u__wcstod_l(const wc *s, wc **e, void *l){ (void)l; return strtod_w(s,e); }
static u64 parse_int(const u32 *s, u64 *used, int base, int *neg, int *ovf){
  u64 i=0; *neg=0; *ovf=0; while(s[i]==' '||(s[i]>='\t'&&s[i]<='\r')) i++;
  if(s[i]=='+'||s[i]=='-'){ *neg=s[i]=='-'; i++; }
  if((base==0||base==16) && s[i]=='0' && (s[i+1]|32)=='x' && ((s[i+2]>='0'&&s[i+2]<='9')||((s[i+2]|32)>='a'&&(s[i+2]|32)<='f'))){ i+=2; base=16; }
  else if(base==0) base= s[i]=='0'? 8 : 10;
  u64 v=0, st=i;
  for(;;i++){ u32 c=s[i]; int d= c>='0'&&c<='9'? (int)c-'0' : (c|32)>='a'&&(c|32)<='z'? (int)(c|32)-'a'+10 : 99; if(d>=base) break;
    if(v>(~0ull-d)/(u64)base) *ovf=1; else v=v*base+d; }
  *used= i==st? 0 : i; return v;
}
static u64 conv_a(const char *s, char **end, int base, int *neg, int *ovf){ u32 b[100]; u64 n=0; for(;s[n]&&n<99;n++) b[n]=(u8)s[n]; b[n]=0; u64 u; u64 v=parse_int(b,&u,base,neg,ovf); if(end) *end=(char*)s+u; return v; }
static u64 conv_w(const wc *s, wc **end, int base, int *neg, int *ovf){ u32 b[100]; u64 n=0; for(;s[n]&&n<99;n++) b[n]=s[n]; b[n]=0; u64 u; u64 v=parse_int(b,&u,base,neg,ovf); if(end) *end=(wc*)s+u; return v; }
static i64 sclamp(u64 v, int neg, int ovf, int bits){ i64 mx=(i64)((1ull<<(bits-1))-1); if(ovf || (!neg && v>(u64)mx) || (neg && v>(u64)mx+1)){ set_errno(ERANGE); return neg? -mx-1 : mx; } return neg? -(i64)v : (i64)v; }
static u64 uclamp(u64 v, int neg, int ovf, int bits){ u64 mx= bits==64? ~0ull : (1ull<<bits)-1; if(ovf || v>mx){ set_errno(ERANGE); return mx; } return neg? (u64)(-(i64)v)&mx : v; }
static i64 W_ u__strtoi64(const char *s, char **e, int b){ int n,o; u64 v=conv_a(s,e,b,&n,&o); return sclamp(v,n,o,64); }
static u64 W_ u__strtoui64(const char *s, char **e, int b){ int n,o; u64 v=conv_a(s,e,b,&n,&o); return uclamp(v,n,o,64); }
static i64 W_ u__wcstoi64(const wc *s, wc **e, int b){ int n,o; u64 v=conv_w(s,e,b,&n,&o); return sclamp(v,n,o,64); }
static u64 W_ u__wcstoui64(const wc *s, wc **e, int b){ int n,o; u64 v=conv_w(s,e,b,&n,&o); return uclamp(v,n,o,64); }
static i32 W_ u_atol(const char *s){ int n,o; u64 v=conv_a(s,0,10,&n,&o); return (i32)sclamp(v,n,o,32); }
static i64 W_ u__atoi64(const char *s){ int n,o; u64 v=conv_a(s,0,10,&n,&o); return sclamp(v,n,o,64); }
static i32 W_ u__wtoi(const wc *s){ int n,o; u64 v=conv_w(s,0,10,&n,&o); return (i32)sclamp(v,n,o,32); }
static i64 W_ u__wtoi64(const wc *s){ int n,o; u64 v=conv_w(s,0,10,&n,&o); return sclamp(v,n,o,64); }
static i64 W_ u_atoll(const char *s){ return u__atoi64(s); }
static i64 W_ u__wtoll(const wc *s){ return u__wtoi64(s); }
static int utoa_(u64 v, int neg, char *b, u64 n, int base){            /* -> 0 or ERANGE */
  char t[70]; int k=0; do{ int d=(int)(v%base); t[k++]=(char)(d<10? '0'+d : 'a'+d-10); v/=base; }while(v);
  if(neg) t[k++]='-'; if((u64)k+1>n){ if(n) b[0]=0; return ERANGE; } for(int i=0;i<k;i++) b[i]=t[k-1-i]; b[k]=0; return 0;
}
static int wtoa_(u64 v, int neg, wc *b, u64 n, int base){ char t[70]; int r=utoa_(v,neg,t,sizeof t,base); u64 l=strlen(t); if(l+1>n){ if(n) b[0]=0; return ERANGE; } for(u64 i=0;i<=l;i++) b[i]=(u8)t[i]; return r; }
static int sneg(i64 v, int base){ return base==10 && v<0; }
static u64 sabs(i64 v, int base, int bits){ return base==10 && v<0? (u64)(-v) : bits==32? (u64)(u32)v : (u64)v; }
static int W_ u__itoa_s(i32 v, char *b, u64 n, int base){ return utoa_(sabs(v,base,32),sneg(v,base),b,n,base); }
static int W_ u__ltoa_s(i32 v, char *b, u64 n, int base){ return u__itoa_s(v,b,n,base); }
static int W_ u__i64toa_s(i64 v, char *b, u64 n, int base){ return utoa_(sabs(v,base,64),sneg(v,base),b,n,base); }
static int W_ u__ui64toa_s(u64 v, char *b, u64 n, int base){ return utoa_(v,0,b,n,base); }
static int W_ u__ultoa_s(u32 v, char *b, u64 n, int base){ return utoa_(v,0,b,n,base); }
static int W_ u__itow_s(i32 v, wc *b, u64 n, int base){ return wtoa_(sabs(v,base,32),sneg(v,base),b,n,base); }
static int W_ u__i64tow_s(i64 v, wc *b, u64 n, int base){ return wtoa_(sabs(v,base,64),sneg(v,base),b,n,base); }
static int W_ u__ui64tow_s(u64 v, wc *b, u64 n, int base){ return wtoa_(v,0,b,n,base); }
static char *W_ u__itoa(i32 v, char *b, int base){ u__itoa_s(v,b,34,base); return b; }
static char *W_ u__ltoa(i32 v, char *b, int base){ u__itoa_s(v,b,34,base); return b; }
static char *W_ u__i64toa(i64 v, char *b, int base){ u__i64toa_s(v,b,66,base); return b; }
static char *W_ u__ui64toa(u64 v, char *b, int base){ u__ui64toa_s(v,b,66,base); return b; }
static wc *W_ u__ltow(i32 v, wc *b, int base){ u__itow_s(v,b,34,base); return b; }
static wc *W_ u__i64tow(i64 v, wc *b, int base){ u__i64tow_s(v,b,66,base); return b; }
static wc *W_ u__ui64tow(u64 v, wc *b, int base){ u__ui64tow_s(v,b,66,base); return b; }
static i32 W_ u_labs(i32 v){ return v<0? -v : v; }
static i64 W_ u_llabs(i64 v){ return v<0? -v : v; }
static u64 W_ u_div(i32 a, i32 b){ i32 q=a/b, r=a%b; return (u32)q | (u64)(u32)r<<32; }          /* div_t {quot, rem} returned in rax */
struct lldiv_ { i64 q, r; };
static struct lldiv_ *W_ u_lldiv(struct lldiv_ *out, i64 a, i64 b){ out->q=a/b; out->r=a%b; return out; }   /* 16-byte struct: hidden return pointer */
static void *W_ u_bsearch(const void *key, const void *base, u64 n, u64 sz, int (*cmp)(const void*,const void*)){
  int (W_ *c)(const void*,const void*)=(void*)cmp; u64 lo=0, hi=n;
  while(lo<hi){ u64 m=(lo+hi)/2; const u8 *p=(const u8*)base+m*sz; int r=c(key,p); if(!r) return (void*)p; if(r<0) hi=m; else lo=m+1; }
  return 0;
}
static void *W_ u__lfind(const void *key, const void *base, u32 *n, u64 sz, int (*cmp)(const void*,const void*)){
  int (W_ *c)(const void*,const void*)=(void*)cmp; for(u32 i=0;i<*n;i++){ const u8 *p=(const u8*)base+i*sz; if(!c(key,p)) return (void*)p; } return 0;
}
static void W_ u_qsort_s(void *base, u64 n, u64 sz, int (*cmp)(void*,const void*,const void*), void *ctx){
  int (W_ *c)(void*,const void*,const void*)=(void*)cmp; u8 *b=base; u8 tmp[256]; if(sz>sizeof tmp) return;
  for(u64 i=1;i<n;i++) for(u64 j=i;j>0 && c(ctx,b+(j-1)*sz,b+j*sz)>0;j--){ memcpy(tmp,b+j*sz,sz); memcpy(b+j*sz,b+(j-1)*sz,sz); memcpy(b+(j-1)*sz,tmp,sz); }
}
static int W_ u_rand_s(u32 *r){ u32 v; if(rdrand32(&v)) *r=v; else *r=(u32)(ticks*2654435761u ^ (u32)(u64)r); return 0; }

/* ================= printf engine ================= */
struct sink { int wide; char *nb; wc *wb; u64 cap, len; void *file; };
struct ufile;
static void file_putc(struct ufile *f, int c);
static void sput(struct sink *s, u32 unit){                         /* one output unit: a byte (narrow) or a UTF-16 unit (wide) */
  if(s->file){ if(s->wide){ char t[4]; int l=utf8_enc(unit,t); for(int i=0;i<l;i++) file_putc(s->file,(u8)t[i]); } else file_putc(s->file,(int)unit); s->len++; return; }
  if(s->len+1<s->cap){ if(s->wide) s->wb[s->len]=(wc)unit; else s->nb[s->len]=(char)unit; }
  s->len++;
}
static void put_cp(struct sink *s, u32 c){                          /* a code point */
  if(s->wide){ if(c>=0x10000){ c-=0x10000; sput(s,0xD800|(c>>10)); sput(s,0xDC00|(c&0x3FF)); } else sput(s,c); }
  else { char t[4]; int l=utf8_enc(c,t); for(int i=0;i<l;i++) sput(s,(u8)t[i]); }
}
static void put_rep(struct sink *s, u32 c, int n){ while(n-->0) sput(s,c); }
#define F_LEFT 1
#define F_PLUS 2
#define F_SPACE 4
#define F_ALT 8
#define F_ZERO 16
static void put_str(struct sink *s, const void *str, int is_wide, int prec, int width, int fl){
  /* count the units it will produce (for padding), then emit */
  int n=0;
  if(!str){ str=is_wide? (const void*)L"(null)" : (const void*)"(null)"; }
  if(is_wide){ const wc *w=str; while((prec<0||n<prec) && w[n]) n++; }
  else { const char *a=str; while((prec<0||n<prec) && a[n]) n++; }
  if(!(fl&F_LEFT)) put_rep(s,' ',width-n);
  if(is_wide){ const wc *w=str; for(int i=0;i<n;i++){ if(s->wide) sput(s,w[i]); else { u32 c=w[i]; if(c>=0xD800&&c<0xDC00&&i+1<n&&w[i+1]>=0xDC00&&w[i+1]<0xE000){ c=0x10000+((c-0xD800)<<10)+(w[i+1]-0xDC00); i++; } put_cp(s,c); } } }
  else { const u8 *a=str; if(!s->wide) for(int i=0;i<n;i++) sput(s,a[i]); else { const u8 *p=a, *e=a+n; while(p<e) put_cp(s,utf8_dec(&p)); } }
  if(fl&F_LEFT) put_rep(s,' ',width-n);
}
static void put_num(struct sink *s, const char *digits, int nd, const char *prefix, int width, int fl, int zero_ok){
  int pl=(int)strlen(prefix), pad=width-nd-pl;
  if(!(fl&F_LEFT) && !((fl&F_ZERO)&&zero_ok)) put_rep(s,' ',pad);
  for(int i=0;i<pl;i++) sput(s,(u8)prefix[i]);
  if(!(fl&F_LEFT) && (fl&F_ZERO) && zero_ok) put_rep(s,'0',pad);
  for(int i=0;i<nd;i++) sput(s,(u8)digits[i]);
  if(fl&F_LEFT) put_rep(s,' ',pad);
}
/* decimal digits of v (> 0): n significant digits, rounded; *e10 = exponent of the first digit */
static void gen_digits(double v, int n, char *out, int *e10){
  long double x=v; int e=0;
  if(x>=10){ for(int b=4096;b;b>>=1){ long double p=pow10l_(b); if(x>=p){ x/=p; e+=b; } } }
  else if(x<1){ for(int b=4096;b;b>>=1){ long double p=pow10l_(b); if(x*p<10){ x*=p; e-=b; } } if(x<1){ x*=10; e--; } }
  if(x>=10){ x/=10; e++; }
  if(n>40) n=40; char d[48];
  for(int i=0;i<=n;i++){ int k=(int)x; if(k>9) k=9; if(k<0) k=0; d[i]=(char)('0'+k); x=(x-k)*10; if(i>=18) x=0; }
  if(d[n]>='5'){ int i=n-1; for(;i>=0;i--){ if(d[i]=='9') d[i]='0'; else { d[i]++; break; } } if(i<0){ for(int j=n-1;j>0;j--) d[j]=d[j-1]; d[0]='1'; e++; } }
  memcpy(out,d,n); *e10=e;
}
static void fmt_float(struct sink *s, double v, char conv, int prec, int width, int fl){
  char buf[400]; int k=0; char pre[3]={0}; int upper= conv>='A'&&conv<='Z'; conv=(char)(conv|32);
  union { double d; u64 u; } bits={v}; int neg=(int)(bits.u>>63); if(neg) v=-v;
  if(neg) pre[0]='-'; else if(fl&F_PLUS) pre[0]='+'; else if(fl&F_SPACE) pre[0]=' ';
  if(v!=v || v==__builtin_inf()){ const char *t= v!=v? (upper? "NAN" : "nan") : (upper? "INF" : "inf"); put_num(s,t,3,pre,width,fl&~F_ZERO,0); return; }
  if(prec<0) prec= conv=='a'? -1 : 6; if(prec>300) prec=300;
  if(conv=='a'){                                                   /* hex float */
    u64 m=bits.u&((1ull<<52)-1); int e=(int)((bits.u>>52)&0x7FF); int lead= e? 1 : 0; if(!e && m) e=1; e-=1023; if(!lead && !m) e=0;
    pre[neg||(fl&(F_PLUS|F_SPACE))? 1 : 0]='0'; pre[neg||(fl&(F_PLUS|F_SPACE))? 2 : 1]=upper? 'X' : 'x';
    int nd=13; if(prec>=0 && prec<13){ u64 r=1ull<<((13-prec)*4-1); m+=r; if(m>>52){ m&=(1ull<<52)-1; lead++; } nd=prec; } else while(nd>0 && prec<0 && !((m>>((13-nd)*4))&0xF)) nd--;
    buf[k++]=(char)('0'+lead); if(nd>0||(fl&F_ALT)) buf[k++]='.';
    for(int i=0;i<nd;i++){ int d=(int)((m>>((12-i)*4))&0xF); buf[k++]=(char)(d<10? '0'+d : (upper? 'A' : 'a')+d-10); }
    for(int i=nd;i<prec;i++) buf[k++]='0';
    k+=ksnprintf(buf+k,40,"%c%c%d",upper? 'P' : 'p',e<0? '-' : '+',e<0? -e : e);
    put_num(s,buf,k,pre,width,fl,1); return;
  }
  char d[48]; int e10=0; int P=prec;
  if(conv=='g'){ if(P==0) P=1; int x=0; if(v!=0){ gen_digits(v,P,d,&e10); x=e10; } if(P>x && x>=-4){ conv='f'; prec=P-1-x; } else { conv='e'; prec=P-1; } if(!(fl&F_ALT)) fl|=0x100; }   /* 0x100: strip zeros */
  if(conv=='e'){
    int nd=prec+1; if(nd>40) nd=40; if(v==0){ memset(d,'0',nd); e10=0; } else gen_digits(v,nd,d,&e10);
    buf[k++]=d[0]; int frac=prec; if(frac>0||(fl&F_ALT)) buf[k++]='.';
    for(int i=1;i<=frac;i++) buf[k++]= i<nd? d[i] : '0';
    if(fl&0x100){ while(k>1 && buf[k-1]=='0') k--; if(buf[k-1]=='.') k--; }
    k+=ksnprintf(buf+k,20,"%c%c%02d",upper? 'E' : 'e',e10<0? '-' : '+',e10<0? -e10 : e10);
  } else {                                                         /* f */
    int ndig=0; if(v!=0){ gen_digits(v,1,d,&e10); ndig=e10+1+prec; }
    if(v==0 || ndig<=0){                                            /* rounds to 0 or to one unit in the last place */
      int up= v!=0 && ndig==0 && (gen_digits(v,1,d,&e10), d[0]>='5');
      buf[k++]= (up && prec==0)? '1' : '0'; if(prec>0||(fl&F_ALT)) buf[k++]='.';
      for(int i=0;i<prec;i++) buf[k++]= (up && i==prec-1)? '1' : '0';
    } else {
      if(ndig>40){ gen_digits(v,40,d,&e10); } else gen_digits(v,ndig,d,&e10);
      int nd= ndig>40? 40 : ndig; int ip=e10+1;                  /* digits before the point (rounding may have added one) */
      if(ip<=0){ buf[k++]='0'; if(prec>0||(fl&F_ALT)) buf[k++]='.'; for(int i=0;i<prec;i++){ int idx=i+ip; buf[k++]= idx<0? '0' : idx<nd? d[idx] : '0'; } }
      else { for(int i=0;i<ip && k<350;i++) buf[k++]= i<nd? d[i] : '0'; if(prec>0||(fl&F_ALT)) buf[k++]='.'; for(int i=0;i<prec && k<390;i++) buf[k++]= ip+i<nd? d[ip+i] : '0'; }
    }
    if((fl&0x100) && memchr(buf,'.',k)){ while(k>1 && buf[k-1]=='0') k--; if(buf[k-1]=='.') k--; }
  }
  put_num(s,buf,k,pre,width,fl,1);
}
/* the formatter: fmt is narrow or wide; ap is the Microsoft va_list (8-byte slots) */
static int vformat(struct sink *s, const void *fmt, int wfmt, u8 *ap){
  const char *fa=fmt; const wc *fw=fmt; u64 i=0;
  #define FC(j) (wfmt? (u32)fw[j] : (u32)(u8)fa[j])
  #define ARG(T) (ap+=8, *(T*)(ap-8))
  for(;;){
    u32 c=FC(i); if(!c) break;
    if(c!='%'){ if(wfmt||!s->wide) sput(s,c); else { const u8 *p=(const u8*)fa+i; put_cp(s,utf8_dec(&p)); i=(u64)(p-(const u8*)fa)-1; } i++; continue; }
    i++; int fl=0, width=0, prec=-1;
    for(;;i++){ u32 f=FC(i); if(f=='-') fl|=F_LEFT; else if(f=='+') fl|=F_PLUS; else if(f==' ') fl|=F_SPACE; else if(f=='#') fl|=F_ALT; else if(f=='0') fl|=F_ZERO; else break; }
    if(FC(i)=='*'){ width=ARG(i32); if(width<0){ fl|=F_LEFT; width=-width; } i++; } else while(FC(i)>='0'&&FC(i)<='9') width=width*10+(int)(FC(i++)-'0');
    if(FC(i)=='.'){ i++; prec=0; if(FC(i)=='*'){ prec=ARG(i32); if(prec<0) prec=-1; i++; } else while(FC(i)>='0'&&FC(i)<='9') prec=prec*10+(int)(FC(i++)-'0'); }
    int size=0, chw=-1;                                              /* size: 1 hh, 2 h, 0 int, 8 64-bit; chw: 0 narrow, 1 wide string/char */
    for(;;){ u32 l=FC(i);
      if(l=='h'){ size= FC(i+1)=='h'? (i++,1) : 2; chw=0; i++; }
      else if(l=='l'){ if(FC(i+1)=='l'){ size=8; i+=2; } else { chw=1; i++; } }
      else if(l=='w'){ chw=1; i++; }
      else if(l=='L'||l=='q'){ size=8; i++; }
      else if(l=='j'||l=='z'||l=='t'){ size=8; i++; }
      else if(l=='I'){ if(FC(i+1)=='6'&&FC(i+2)=='4'){ size=8; i+=3; } else if(FC(i+1)=='3'&&FC(i+2)=='2'){ size=0; i+=3; } else { size=8; i++; } }
      else break; }
    u32 cv=FC(i++); if(!cv) break;
    switch(cv){
      case '%': sput(s,'%'); break;
      case 'c': case 'C': { int w= chw>=0? chw : (cv=='C')!=(wfmt!=0); u32 ch=(u32)ARG(u64)&(w? 0xFFFF : 0xFF);
        if(!(fl&F_LEFT)) put_rep(s,' ',width-1); if(w) { if(s->wide) sput(s,ch); else put_cp(s,ch); } else { if(s->wide) put_cp(s,ch); else sput(s,ch); } if(fl&F_LEFT) put_rep(s,' ',width-1); break; }
      case 's': case 'S': case 'Z': { const void *p=ARG(void*); int w= chw>=0? chw : (cv=='S')!=(wfmt!=0);
        if(cv=='Z'){ if(p){ u16 len=*(const u16*)p; const void *b=*(void*const*)((const u8*)p+8); int n= w? len/2 : len; put_str(s,b,w,prec<0||prec>n? n : prec,width,fl); } else put_str(s,0,w,prec,width,fl); break; }
        put_str(s,p,w,prec,width,fl); break; }
      case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': case 'b': {
        u64 v; int neg=0;
        if(cv=='p'){ v=ARG(u64); size=8; prec= prec<0? 16 : prec; }
        else if(cv=='d'||cv=='i'){ i64 x= size==8? ARG(i64) : size==1? (i64)(signed char)ARG(i32) : size==2? (i64)(short)ARG(i32) : (i64)ARG(i32); neg=x<0; v= neg? (u64)(-x) : (u64)x; }
        else { v= size==8? ARG(u64) : size==1? (u8)ARG(u32) : size==2? (u16)ARG(u32) : (u64)ARG(u32); }
        int base= (cv=='x'||cv=='X'||cv=='p')? 16 : cv=='o'? 8 : cv=='b'? 2 : 10; const char *dg= (cv=='X'||cv=='p')? "0123456789ABCDEF" : "0123456789abcdef";
        char t[70]; int n=0; u64 x=v; while(x){ t[n++]=dg[x%base]; x/=base; }
        int want= prec<0? 1 : prec; char out[140]; int k=0;
        if(cv=='o' && (fl&F_ALT) && (n>=want)) want=n+1;
        for(int z=n;z<want && k<128;z++) out[k++]='0'; for(int z=n-1;z>=0;z--) out[k++]=t[z];
        char pre[4]={0}; int pl=0;
        if(neg) pre[pl++]='-'; else if((cv=='d'||cv=='i') && (fl&F_PLUS)) pre[pl++]='+'; else if((cv=='d'||cv=='i') && (fl&F_SPACE)) pre[pl++]=' ';
        if((fl&F_ALT) && v && (cv=='x'||cv=='X')){ pre[pl++]='0'; pre[pl++]=(char)cv; }
        put_num(s,out,k,pre,width,fl,prec<0); break; }
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': fmt_float(s,ARG(double),(char)cv,prec,width,fl); break;
      case 'n': (void)ARG(void*); break;                                  /* disabled, as in the UCRT */
      default: sput(s,'%'); sput(s,cv); break;
    }
  }
  #undef FC
  #undef ARG
  return (int)s->len;
}
/* option bits of __stdio_common_v*printf */
#define OPT_LEGACY_VSPRINTF_NULL 1
#define OPT_STANDARD_SNPRINTF 2
static int sprint(int wide, void *buf, u64 n, const void *fmt, u8 *ap, u64 opt, int secure){
  struct sink s={0}; s.wide=wide; s.nb=buf; s.wb=buf; s.cap= buf? n : 0;
  int r=vformat(&s,fmt,wide,ap);
  if(!buf) return r;
  if((u64)r<n){ if(wide) ((wc*)buf)[r]=0; else ((char*)buf)[r]=0; return r; }
  if(secure){ if(n){ if(wide) ((wc*)buf)[0]=0; else ((char*)buf)[0]=0; } set_errno(ERANGE); return -1; }
  if(n){ if(opt&OPT_STANDARD_SNPRINTF){ if(wide) ((wc*)buf)[n-1]=0; else ((char*)buf)[n-1]=0; return r; }
    if((opt&OPT_LEGACY_VSPRINTF_NULL) && (u64)r==n) return r;          /* _vsnprintf: an exact fit is stored without a terminator and counts as success */
    if(!(opt&OPT_LEGACY_VSPRINTF_NULL)){ if(wide) ((wc*)buf)[n-1]=0; else ((char*)buf)[n-1]=0; } }
  return (opt&OPT_STANDARD_SNPRINTF)? r : -1;
}
static int W_ u___stdio_common_vsprintf(u64 opt, char *b, u64 n, const char *f, void *l, u8 *ap){ (void)l; return sprint(0,b,n,f,ap,opt,0); }
static int W_ u___stdio_common_vsprintf_s(u64 opt, char *b, u64 n, const char *f, void *l, u8 *ap){ (void)l; return sprint(0,b,n,f,ap,opt,1); }
static int W_ u___stdio_common_vsprintf_p(u64 opt, char *b, u64 n, const char *f, void *l, u8 *ap){ (void)l; return sprint(0,b,n,f,ap,opt,0); }
static int W_ u___stdio_common_vsnprintf_s(u64 opt, char *b, u64 n, u64 cnt, const char *f, void *l, u8 *ap){
  (void)l; if(cnt==TRUNC || cnt>=n){ int r=sprint(0,b,n,f,ap,opt|OPT_STANDARD_SNPRINTF,0); return (r>=0 && (u64)r>=n)? -1 : r; }
  return sprint(0,b,cnt+1,f,ap,opt|OPT_STANDARD_SNPRINTF,0);
}
static int W_ u___stdio_common_vswprintf(u64 opt, wc *b, u64 n, const wc *f, void *l, u8 *ap){ (void)l; return sprint(1,b,n,f,ap,opt,0); }
static int W_ u___stdio_common_vswprintf_s(u64 opt, wc *b, u64 n, const wc *f, void *l, u8 *ap){ (void)l; return sprint(1,b,n,f,ap,opt,1); }
static int W_ u___stdio_common_vswprintf_p(u64 opt, wc *b, u64 n, const wc *f, void *l, u8 *ap){ (void)l; return sprint(1,b,n,f,ap,opt,0); }
static int W_ u___stdio_common_vsnwprintf_s(u64 opt, wc *b, u64 n, u64 cnt, const wc *f, void *l, u8 *ap){
  (void)l; if(cnt==TRUNC || cnt>=n){ int r=sprint(1,b,n,f,ap,opt|OPT_STANDARD_SNPRINTF,0); return (r>=0 && (u64)r>=n)? -1 : r; }
  return sprint(1,b,cnt+1,f,ap,opt|OPT_STANDARD_SNPRINTF,0);
}
/* old msvcrt-style entry points (va_list = pointer to the slots) */
static int W_ u_vsnprintf(char *b, u64 n, const char *f, u8 *ap){ return sprint(0,b,n,f,ap,OPT_STANDARD_SNPRINTF,0); }
static int W_ u__vsnprintf(char *b, u64 n, const char *f, u8 *ap){ return sprint(0,b,n,f,ap,OPT_LEGACY_VSPRINTF_NULL,0); }
static int W_ u_vsprintf(char *b, const char *f, u8 *ap){ return sprint(0,b,~0ull>>1,f,ap,0,0); }
static int W_ u__vscprintf(const char *f, u8 *ap){ return sprint(0,0,0,f,ap,0,0); }
static int W_ u__vscwprintf(const wc *f, u8 *ap){ return sprint(1,0,0,f,ap,0,0); }
static int W_ u_vswprintf(wc *b, u64 n, const wc *f, u8 *ap){ return sprint(1,b,n,f,ap,OPT_STANDARD_SNPRINTF,0); }
static int W_ u__vsnwprintf(wc *b, u64 n, const wc *f, u8 *ap){ return sprint(1,b,n,f,ap,OPT_LEGACY_VSPRINTF_NULL,0); }
static int W_ u_sprintf(char *b, const char *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=sprint(0,b,~0ull>>1,f,(u8*)ap,0,0); __builtin_ms_va_end(ap); return r; }
static int W_ u_snprintf(char *b, u64 n, const char *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=sprint(0,b,n,f,(u8*)ap,OPT_STANDARD_SNPRINTF,0); __builtin_ms_va_end(ap); return r; }
static int W_ u__snprintf(char *b, u64 n, const char *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=sprint(0,b,n,f,(u8*)ap,OPT_LEGACY_VSPRINTF_NULL,0); __builtin_ms_va_end(ap); return r; }
static int W_ u_sprintf_s(char *b, u64 n, const char *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=sprint(0,b,n,f,(u8*)ap,0,1); __builtin_ms_va_end(ap); return r; }
static int W_ u_swprintf(wc *b, u64 n, const wc *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=sprint(1,b,n,f,(u8*)ap,OPT_STANDARD_SNPRINTF,0); __builtin_ms_va_end(ap); return r; }
static int W_ u__snwprintf(wc *b, u64 n, const wc *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=sprint(1,b,n,f,(u8*)ap,OPT_LEGACY_VSPRINTF_NULL,0); __builtin_ms_va_end(ap); return r; }
static int W_ u_swprintf_s(wc *b, u64 n, const wc *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=sprint(1,b,n,f,(u8*)ap,0,1); __builtin_ms_va_end(ap); return r; }

/* ================= low-level I/O: file descriptors over Win32 handles ================= */
#define MAXFD 64
struct fdent { u8 used, text, append, isdev; HANDLE h; };
static struct fdent fds[MAXPROC][MAXFD];
static struct fdent *fd_get(int fd){
  if(!cur || fd<0 || fd>=MAXFD) return 0; struct fdent *t=fds[cur->pid];
  if(fd<3 && !t[fd].used){ t[fd].used=1; t[fd].isdev=1; t[fd].text=1; t[fd].h= fd==0? 3 : fd==1? 7 : 11; }      /* the standard handles */
  return t[fd].used? &t[fd] : 0;
}
static int fd_new(HANDLE h, int text, int app, int dev){
  if(!cur) return -1; struct fdent *t=fds[cur->pid]; fd_get(0); fd_get(1); fd_get(2);
  for(int i=3;i<MAXFD;i++) if(!t[i].used){ t[i]=(struct fdent){1,(u8)text,(u8)app,(u8)dev,h}; return i; }
  set_errno(24); return -1;                                          /* EMFILE */
}
static void ucrt_proc_reset_startup(int pid);
void ucrt_proc_reset(int pid){ if(pid>=0 && pid<MAXPROC){ memset(fds[pid],0,sizeof fds[pid]); ucrt_proc_reset_startup(pid); } }   /* a new process in this slot */
static BOOL rd(HANDLE h, void *b, u32 n, u32 *got){ return KCALL(BOOL (W_*)(HANDLE,void*,u32,u32*,void*),ReadFile)(h,b,n,got,0); }
static BOOL wr(HANDLE h, const void *b, u32 n, u32 *put){ return KCALL(BOOL (W_*)(HANDLE,const void*,u32,u32*,void*),WriteFile)(h,b,n,put,0); }
static i64 seek_h(HANDLE h, i64 off, int wh){ i64 np=0; if(!KCALL(BOOL (W_*)(HANDLE,i64,i64*,DWORD),SetFilePointerEx)(h,off,&np,(DWORD)wh)) return -1; return np; }
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_APPEND 8
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define O_EXCL 0x400
#define O_TEXT 0x4000
#define O_BINARY 0x8000
static int open_a(const char *path, int fl){
  DWORD acc= (fl&3)==O_RDONLY? 0x80000000 : (fl&3)==O_WRONLY? 0x40000000 : 0xC0000000;
  DWORD disp= (fl&O_CREAT)? ((fl&O_EXCL)? 1 : (fl&O_TRUNC)? 2 : 4) : (fl&O_TRUNC)? 5 : 3;
  HANDLE h=KCALL(HANDLE (W_*)(const char*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE),CreateFileA)(path,acc,3,0,disp,0x80,0);
  if(h==(HANDLE)-1 || !h){ u32 e=win32_get_last_error(); *doserrno_p()=e; set_errno(e==2||e==3? ENOENT : e==80? EEXIST : EACCES); return -1; }
  int fd=fd_new(h,!(fl&O_BINARY),(fl&O_APPEND)!=0,0); if(fd<0) KCALL(BOOL (W_*)(HANDLE),CloseHandle)(h); return fd;
}
static int W_ u__open(const char *p, int fl, int mode){ (void)mode; return open_a(p,fl); }
static int W_ u__wopen(const wc *p, int fl, int mode){ (void)mode; WPATH(a,p); return open_a(a,fl); }
static int W_ u__sopen(const char *p, int fl, int sh, int mode){ (void)sh;(void)mode; return open_a(p,fl); }
static int W_ u__wsopen(const wc *p, int fl, int sh, int mode){ (void)sh;(void)mode; WPATH(a,p); return open_a(a,fl); }
static int W_ u__sopen_s(int *fd, const char *p, int fl, int sh, int mode){ (void)sh;(void)mode; *fd=open_a(p,fl); return *fd<0? *errno_p() : 0; }
static int W_ u__wsopen_s(int *fd, const wc *p, int fl, int sh, int mode){ (void)sh;(void)mode; WPATH(a,p); *fd=open_a(a,fl); return *fd<0? *errno_p() : 0; }
static int W_ u__close(int fd){ struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } if(!f->isdev) KCALL(BOOL (W_*)(HANDLE),CloseHandle)(f->h); f->used=0; return 0; }
static int W_ u__read(int fd, void *b, u32 n){
  struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } u32 got=0;
  if(!rd(f->h,b,n,&got)){ if(win32_get_last_error()==109) return 0; set_errno(EBADF); return -1; }      /* broken pipe = end of file */
  if(f->text && !f->isdev){ char *c=b; u32 o=0; for(u32 i=0;i<got;i++){ if(c[i]=='\r' && i+1<got && c[i+1]=='\n') continue; if(c[i]==26) break; c[o++]=c[i]; } got=o; }
  return (int)got;
}
static int W_ u__write(int fd, const void *b, u32 n){
  struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } if(f->append) seek_h(f->h,0,2); u32 put=0;
  if(f->text && !f->isdev){ const char *c=b; char t[512]; u32 k=0; for(u32 i=0;i<n;i++){ if(c[i]=='\n') t[k++]='\r'; t[k++]=c[i]; if(k>=510){ wr(f->h,t,k,&put); k=0; } } if(k) wr(f->h,t,k,&put); return (int)n; }
  if(!wr(f->h,b,n,&put)){ set_errno(EBADF); return -1; } return (int)put;
}
static i64 W_ u__lseeki64(int fd, i64 off, int wh){ struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } if(f->isdev) return 0; i64 r=seek_h(f->h,off,wh); if(r<0) set_errno(EINVAL); return r; }
static i32 W_ u__lseek(int fd, i32 off, int wh){ return (i32)u__lseeki64(fd,off,wh); }
static i64 W_ u__telli64(int fd){ return u__lseeki64(fd,0,1); }
static i32 W_ u__tell(int fd){ return (i32)u__lseeki64(fd,0,1); }
static i64 fd_size(struct fdent *f){ i64 sz=0; if(f->isdev || !KCALL(BOOL (W_*)(HANDLE,i64*),GetFileSizeEx)(f->h,&sz)) return -1; return sz; }
static i64 W_ u__filelengthi64(int fd){ struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } return fd_size(f); }
static i32 W_ u__filelength(int fd){ return (i32)u__filelengthi64(fd); }
static int W_ u__eof(int fd){ struct fdent *f=fd_get(fd); if(!f) return -1; i64 p=seek_h(f->h,0,1), s=fd_size(f); return p>=s; }
static int W_ u__commit(int fd){ return fd_get(fd)? 0 : -1; }
static int W_ u__chsize_s(int fd, i64 sz){ struct fdent *f=fd_get(fd); if(!f) return EBADF; i64 p=seek_h(f->h,0,1); seek_h(f->h,sz,0); KCALL(BOOL (W_*)(HANDLE),SetEndOfFile)(f->h); seek_h(f->h,p,0); return 0; }
static int W_ u__chsize(int fd, i32 sz){ return u__chsize_s(fd,sz)? -1 : 0; }
static int W_ u__setmode(int fd, int m){ struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } int old=f->text? O_TEXT : O_BINARY; f->text= !(m&O_BINARY); return old; }
static int W_ u__isatty(int fd){ struct fdent *f=fd_get(fd); return f && f->isdev && !cur->out_h; }
static HANDLE W_ u__get_osfhandle(int fd){ struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return (HANDLE)-1; } return f->h; }
static int W_ u__open_osfhandle(HANDLE h, int fl){ return fd_new(h,!(fl&O_BINARY),(fl&O_APPEND)!=0,h==3||h==7||h==11); }
static int W_ u__dup(int fd){ struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } HANDLE h=f->h; if(pipe_is_handle(h)) pipe_addref(h); return fd_new(h,f->text,f->append,1); }
static int W_ u__dup2(int a, int b){ struct fdent *f=fd_get(a); if(!f || b<0 || b>=MAXFD){ set_errno(EBADF); return -1; } fd_get(b); fds[cur->pid][b]=*f; fds[cur->pid][b].isdev=1; return 0; }
static int W_ u__locking(int fd, int m, i32 n){ (void)fd;(void)m;(void)n; return 0; }
static int W_ u__pipe(int *fds2, u32 sz, int fl){
  HANDLE r,w; if(!KCALL(BOOL (W_*)(HANDLE*,HANDLE*,void*,DWORD),CreatePipe)(&r,&w,0,sz)) return -1;
  fds2[0]=fd_new(r,!(fl&O_BINARY),0,0); fds2[1]=fd_new(w,!(fl&O_BINARY),0,0); return 0;
}
static int W_ u__getmaxstdio(void){ return 512; }
static int W_ u__setmaxstdio(int n){ return n; }

/* ================= FILE streams ================= */
struct ufile { char *ptr; char *base; int cnt; int flags; int fd; int bufsiz; char *tmp; int unget[4]; int nunget; u8 used, eof, err, owner_pid; };
#define MAXFILE 256
static struct ufile files_[MAXFILE];
static struct ufile *stdf(int i){ struct ufile *f=&files_[i]; if(!f->used){ f->used=1; f->fd=i; } return f; }
static int is_file(void *p){ return (u8*)p>=(u8*)files_ && (u8*)p<(u8*)(files_+MAXFILE) && ((struct ufile*)p)->used; }
static struct ufile *W_ u___acrt_iob_func(u32 i){ return stdf((int)(i%3)); }
static struct ufile *W_ u___iob_func(void){ stdf(0); stdf(1); stdf(2); return files_; }
static struct ufile *file_from_fd(int fd){ if(fd<0) return 0; for(int i=3;i<MAXFILE;i++) if(!files_[i].used){ struct ufile *f=&files_[i]; memset(f,0,sizeof *f); f->used=1; f->fd=fd; f->owner_pid=(u8)(cur? cur->pid : 0); return f; } set_errno(24); return 0; }
static int mode_flags(const char *m){
  int fl=0; char c=m[0]; int plus=0, bin=0; for(const char *p=m+1;*p;p++){ if(*p=='+') plus=1; else if(*p=='b') bin=1; else if(*p==',') break; }
  if(c=='r') fl= plus? O_RDWR : O_RDONLY; else if(c=='w') fl=(plus? O_RDWR : O_WRONLY)|O_CREAT|O_TRUNC; else if(c=='a') fl=(plus? O_RDWR : O_WRONLY)|O_CREAT|O_APPEND; else return -1;
  for(const char *p=m+1;*p;p++) if(*p=='x') fl|=O_EXCL;
  return fl|(bin? O_BINARY : 0);
}
static struct ufile *fopen_a(const char *path, const char *mode){
  int fl=mode_flags(mode); if(fl<0){ set_errno(EINVAL); return 0; }
  int fd=open_a(path,fl); if(fd<0) return 0; struct ufile *f=file_from_fd(fd); if(!f) u__close(fd); return f;
}
static struct ufile *W_ u_fopen(const char *p, const char *m){ return fopen_a(p,m); }
static int W_ u_fopen_s(struct ufile **f, const char *p, const char *m){ *f=fopen_a(p,m); return *f? 0 : *errno_p(); }
static struct ufile *W_ u__fsopen(const char *p, const char *m, int sh){ (void)sh; return fopen_a(p,m); }
static struct ufile *W_ u__wfopen(const wc *p, const wc *m){ WPATH(a,p); char mm[16]; w2a_buf(m,mm,16); return fopen_a(a,mm); }
static int W_ u__wfopen_s(struct ufile **f, const wc *p, const wc *m){ *f=u__wfopen(p,m); return *f? 0 : *errno_p(); }
static struct ufile *W_ u__wfsopen(const wc *p, const wc *m, int sh){ (void)sh; return u__wfopen(p,m); }
static struct ufile *W_ u__fdopen(int fd, const char *m){ (void)m; return fd_get(fd)? file_from_fd(fd) : 0; }
static struct ufile *W_ u__wfdopen(int fd, const wc *m){ (void)m; return fd_get(fd)? file_from_fd(fd) : 0; }
static int W_ u_fclose(struct ufile *f){ if(!is_file(f)) return -1; int r=u__close(f->fd); if(f-files_>=3) f->used=0; return r; }
static int W_ u__fcloseall(void){ int n=0; for(int i=3;i<MAXFILE;i++) if(files_[i].used && files_[i].owner_pid==cur->pid){ u_fclose(&files_[i]); n++; } return n; }
static struct ufile *W_ u_freopen(const char *p, const char *m, struct ufile *f){ if(!is_file(f)) return 0; u__close(f->fd); int fl=mode_flags(m); int fd=open_a(p,fl<0? 0 : fl); if(fd<0) return 0; f->fd=fd; f->eof=f->err=0; f->nunget=0; return f; }
static struct ufile *W_ u__wfreopen(const wc *p, const wc *m, struct ufile *f){ WPATH(a,p); char mm[16]; w2a_buf(m,mm,16); return u_freopen(a,mm,f); }
static int W_ u_freopen_s(struct ufile **o, const char *p, const char *m, struct ufile *f){ *o=u_freopen(p,m,f); return *o? 0 : EINVAL; }
static int W_ u__wfreopen_s(struct ufile **o, const wc *p, const wc *m, struct ufile *f){ *o=u__wfreopen(p,m,f); return *o? 0 : EINVAL; }
static int W_ u__fileno(struct ufile *f){ return is_file(f)? f->fd : -1; }
static u64 W_ u_fread(void *b, u64 sz, u64 n, struct ufile *f){
  if(!is_file(f) || !sz || !n) return 0; u64 want=sz*n, got=0; u8 *o=b;
  while(f->nunget && got<want){ o[got++]=(u8)f->unget[--f->nunget]; }
  while(got<want){ u64 chunk=want-got; if(chunk>0x40000000) chunk=0x40000000; int r=u__read(f->fd,o+got,(u32)chunk); if(r<0){ f->err=1; break; } if(r==0){ f->eof=1; break; } got+=r;
    struct fdent *e=fd_get(f->fd); if(e && e->isdev) break; }                     /* the console returns one line per read */
  return got/sz;
}
static u64 W_ u_fwrite(const void *b, u64 sz, u64 n, struct ufile *f){
  if(!sz || !n) return 0;
  if(!is_file(f)){ const char *c=b; for(u64 i=0;i<sz*n;i++) kputc(c[i]); return n; }
  f->nunget=0; int r=u__write(f->fd,b,(u32)(sz*n)); if(r<0){ f->err=1; return 0; } return (u64)r/sz;
}
static void file_putc(struct ufile *f, int c){ char ch=(char)c; u_fwrite(&ch,1,1,f); }
static int W_ u_fgetc(struct ufile *f){ if(!is_file(f)) return -1; if(f->nunget) return f->unget[--f->nunget]; u8 c; return u_fread(&c,1,1,f)==1? c : -1; }
static int W_ u_ungetc(int c, struct ufile *f){ if(c<0 || !is_file(f) || f->nunget>=4) return -1; f->unget[f->nunget++]=c&0xFF; f->eof=0; return c&0xFF; }
static int W_ u_fputc(int c, struct ufile *f){ char ch=(char)c; return u_fwrite(&ch,1,1,f)==1? (u8)c : -1; }
static int W_ u_putchar(int c){ return u_fputc(c,stdf(1)); }
static int W_ u_getchar(void){ return u_fgetc(stdf(0)); }
static int W_ u_fputs(const char *s, struct ufile *f){ u64 n=strlen(s); return u_fwrite(s,1,n,f)==n? 0 : -1; }
static int W_ u_puts(const char *s){ u_fputs(s,stdf(1)); return u_fputc('\n',stdf(1)); }
static char *W_ u_fgets(char *b, int n, struct ufile *f){
  if(n<=0 || !is_file(f)) return 0; int i=0;
  struct fdent *e=fd_get(f->fd);
  if(e && e->isdev && f->fd==0 && !f->nunget){                    /* console: read a whole line at once */
    if(n<3) return 0; int r=u__read(0,b,(u32)(n-1)); if(r<=0){ f->eof=1; return 0; } int o=0; for(int k=0;k<r;k++) if(b[k]!='\r') b[o++]=b[k]; b[o]=0; return b; }
  while(i<n-1){ int c=u_fgetc(f); if(c<0) break; b[i++]=(char)c; if(c=='\n') break; }
  if(!i) return 0; b[i]=0; return b;
}
static u32 W_ u_fgetwc(struct ufile *f){                              /* UTF-8 file -> one UTF-16 unit */
  int c=u_fgetc(f); if(c<0) return 0xFFFF; if(c<0x80) return (u32)c;
  u8 b[4]={(u8)c}; int n= c>=0xF0? 3 : c>=0xE0? 2 : c>=0xC0? 1 : 0; for(int i=1;i<=n;i++){ int d=u_fgetc(f); if(d<0) break; b[i]=(u8)d; }
  const u8 *p=b; u32 v=utf8_dec(&p); return v>0xFFFF? 0xFFFD : v;
}
static u32 W_ u_fputwc(u32 c, struct ufile *f){ char t[4]; int l=utf8_enc(c&0xFFFF,t); u_fwrite(t,1,l,f); return c&0xFFFF; }
static u32 W_ u_ungetwc(u32 c, struct ufile *f){ if(c==0xFFFF) return c; char t[4]; int l=utf8_enc(c,t); for(int i=l-1;i>=0;i--) u_ungetc((u8)t[i],f); return c; }
static wc *W_ u_fgetws(wc *b, int n, struct ufile *f){ int i=0; while(i<n-1){ u32 c=u_fgetwc(f); if(c==0xFFFF) break; b[i++]=(wc)c; if(c=='\n') break; } if(!i) return 0; b[i]=0; return b; }
static int W_ u_fputws(const wc *s, struct ufile *f){ for(;*s;s++) u_fputwc(*s,f); return 0; }
static int W_ u_fflush(struct ufile *f){ (void)f; return 0; }
static int W_ u__flushall(void){ return 0; }
static int W_ u__fseeki64(struct ufile *f, i64 off, int wh){
  if(!is_file(f)) return -1; if(wh==1) off-=f->nunget; f->nunget=0; f->eof=0;
  struct fdent *e=fd_get(f->fd); if(!e) return -1; if(e->isdev) return 0;
  return seek_h(e->h,off,wh)<0? -1 : 0;
}
static int W_ u_fseek(struct ufile *f, i32 off, int wh){ return u__fseeki64(f,off,wh); }
static i64 W_ u__ftelli64(struct ufile *f){ if(!is_file(f)) return -1; struct fdent *e=fd_get(f->fd); if(!e || e->isdev) return 0; return seek_h(e->h,0,1)-f->nunget; }
static i32 W_ u_ftell(struct ufile *f){ return (i32)u__ftelli64(f); }
static void W_ u_rewind(struct ufile *f){ u__fseeki64(f,0,0); if(is_file(f)) f->err=0; }
static int W_ u_fgetpos(struct ufile *f, i64 *pos){ *pos=u__ftelli64(f); return *pos<0? -1 : 0; }
static int W_ u_fsetpos(struct ufile *f, const i64 *pos){ return u__fseeki64(f,*pos,0); }
static int W_ u_feof(struct ufile *f){ return is_file(f)? f->eof : 0; }
static int W_ u_ferror(struct ufile *f){ return is_file(f)? f->err : 0; }
static void W_ u_clearerr(struct ufile *f){ if(is_file(f)) f->eof=f->err=0; }
static int W_ u_clearerr_s(struct ufile *f){ u_clearerr(f); return 0; }
static void W_ u_setbuf(struct ufile *f, char *b){ (void)f;(void)b; }
static int W_ u_setvbuf(struct ufile *f, char *b, int m, u64 n){ (void)f;(void)b;(void)m;(void)n; return 0; }
static void W_ u__lock_file(struct ufile *f){ (void)f; }
static int W_ u__get_stream_buffer_pointers(struct ufile *f, char ***base, char ***ptr, int **cnt){   /* the buffer is always empty: callers fall back to fgetc/fputc */
  if(!is_file(f)) return EINVAL; f->cnt=0; if(base) *base=&f->base; if(ptr) *ptr=&f->ptr; if(cnt) *cnt=&f->cnt; return 0;
}
static int W_ u__fputc_nolock(int c, struct ufile *f){ return u_fputc(c,f); }
static int W_ u__fgetc_nolock(int c, struct ufile *f){ (void)c; return u_fgetc(f); }
static struct ufile *W_ u_tmpfile(void){ static int n; char p[40]; ksnprintf(p,sizeof p,"C:\\Temp\\tmp%d_%d",cur->pid,n++); return fopen_a(p,"w+b"); }
static int W_ u_tmpfile_s(struct ufile **f){ *f=u_tmpfile(); return *f? 0 : EINVAL; }
/* formatted output to streams */
static int fprint(struct ufile *f, int wide, const void *fmt, u8 *ap){ struct sink s={0}; s.wide=wide; s.file= is_file(f)? f : stdf(1); return vformat(&s,fmt,wide,ap); }
static int W_ u___stdio_common_vfprintf(u64 opt, struct ufile *f, const char *fmt, void *l, u8 *ap){ (void)opt;(void)l; return fprint(f,0,fmt,ap); }
static int W_ u___stdio_common_vfprintf_s(u64 opt, struct ufile *f, const char *fmt, void *l, u8 *ap){ (void)opt;(void)l; return fprint(f,0,fmt,ap); }
static int W_ u___stdio_common_vfwprintf(u64 opt, struct ufile *f, const wc *fmt, void *l, u8 *ap){ (void)opt;(void)l; return fprint(f,1,fmt,ap); }
static int W_ u___stdio_common_vfwprintf_s(u64 opt, struct ufile *f, const wc *fmt, void *l, u8 *ap){ (void)opt;(void)l; return fprint(f,1,fmt,ap); }
static int W_ u_vfprintf(struct ufile *f, const char *fmt, u8 *ap){ return fprint(f,0,fmt,ap); }
static int W_ u_vprintf(const char *fmt, u8 *ap){ return fprint(stdf(1),0,fmt,ap); }
static int W_ u_vfwprintf(struct ufile *f, const wc *fmt, u8 *ap){ return fprint(f,1,fmt,ap); }
static int W_ u_printf(const char *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=fprint(stdf(1),0,f,(u8*)ap); __builtin_ms_va_end(ap); return r; }
static int W_ u_fprintf(struct ufile *fp, const char *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=fprint(fp,0,f,(u8*)ap); __builtin_ms_va_end(ap); return r; }
static int W_ u_wprintf(const wc *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=fprint(stdf(1),1,f,(u8*)ap); __builtin_ms_va_end(ap); return r; }
static int W_ u_fwprintf(struct ufile *fp, const wc *f, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,f); int r=fprint(fp,1,f,(u8*)ap); __builtin_ms_va_end(ap); return r; }
static void W_ u_perror(const char *s){ char b[200]; ksnprintf(b,sizeof b,"%s%serror %d\n",s? s : "",s&&*s? ": " : "",*errno_p()); u_fputs(b,stdf(2)); }

/* ================= scanf ================= */
struct src { const u8 *na; const wc *wa; struct ufile *f; u64 pos; int wide; };
static int sget(struct src *s){
  if(s->f){ int c= s->wide? (int)u_fgetwc(s->f) : u_fgetc(s->f); if(c==0xFFFF && s->wide) c=-1; if(c>=0) s->pos++; return c; }
  int c= s->wide? s->wa[s->pos] : s->na[s->pos]; if(!c) return -1; s->pos++; return c;
}
static void sunget(struct src *s, int c){ if(c<0) return; if(s->f){ if(s->wide) u_ungetwc((u32)c,s->f); else u_ungetc(c,s->f); } s->pos--; }
static int vscan(struct src *in, const void *fmt, int wfmt, u8 *ap){
  const char *fa=fmt; const wc *fw=fmt; u64 i=0; int n=0, any=0;
  #define FC(j) (wfmt? (u32)fw[j] : (u32)(u8)fa[j])
  for(;;){
    u32 c=FC(i); if(!c) break;
    if(c==' '||(c>='\t'&&c<='\r')){ int ch; while((ch=sget(in))>=0 && (ch==' '||(ch>='\t'&&ch<='\r'))); sunget(in,ch); i++; continue; }
    if(c!='%'){ int ch=sget(in); if(ch!=(int)c){ sunget(in,ch); break; } i++; continue; }
    i++; if(FC(i)=='%'){ int ch=sget(in); if(ch!='%') break; i++; continue; }
    int skip=0, width=0, size=0, chw=-1; if(FC(i)=='*'){ skip=1; i++; }
    while(FC(i)>='0'&&FC(i)<='9') width=width*10+(int)(FC(i++)-'0');
    for(;;){ u32 l=FC(i); if(l=='h'){ size= FC(i+1)=='h'? (i++,1) : 2; chw=0; i++; } else if(l=='l'){ if(FC(i+1)=='l'){ size=8; i+=2; } else { size=4; chw=1; i++; } }
      else if(l=='L'||l=='j'||l=='z'||l=='t'){ size=8; i++; } else if(l=='w'){ chw=1; i++; } else if(l=='I'){ if(FC(i+1)=='6'){ size=8; i+=3; } else if(FC(i+1)=='3'){ i+=3; } else { size=8; i++; } } else break; }
    u32 cv=FC(i++); if(!cv) break;
    if(cv=='n'){ if(!skip){ int *p=*(int**)ap; ap+=8; *p=(int)in->pos; } continue; }
    int ch;
    if(cv!='c' && cv!='C' && cv!='['){ while((ch=sget(in))>=0 && (ch==' '||(ch>='\t'&&ch<='\r'))); sunget(in,ch); }
    if(!width) width= (cv=='c'||cv=='C')? 1 : 0x7FFFFFFF;
    if(cv=='d'||cv=='i'||cv=='u'||cv=='x'||cv=='X'||cv=='o'||cv=='p'){
      u32 b[70]; int k=0; while(k<68 && k<width){ ch=sget(in); if(ch<0) break; int ok= (ch=='-'||ch=='+')? k==0 : ((ch|32)=='x')? k>0 : (ch>='0'&&ch<='9')||((ch|32)>='a'&&(ch|32)<='f'); if(!ok){ sunget(in,ch); break; } b[k++]=(u32)ch; } b[k]=0;
      if(!k) break; u64 used; int neg,ovf; u64 v=parse_int(b,&used,cv=='d'||cv=='u'? 10 : cv=='o'? 8 : cv=='i'? 0 : 16,&neg,&ovf);
      if(!used){ break; } for(u64 z=(u64)k;z>used;z--) sunget(in,(int)b[z-1]);
      if(neg) v=(u64)(-(i64)v);
      if(!skip){ void *p=*(void**)ap; ap+=8; if(cv=='p'||size==8) *(u64*)p=v; else if(size==2) *(u16*)p=(u16)v; else if(size==1) *(u8*)p=(u8)v; else *(u32*)p=(u32)v; n++; }
      any=1; continue;
    }
    if(cv=='e'||cv=='f'||cv=='g'||cv=='E'||cv=='G'||cv=='a'||cv=='A'||cv=='F'){
      u32 b[100]; int k=0; while(k<98 && k<width){ ch=sget(in); if(ch<0) break; if(!((ch>='0'&&ch<='9')||ch=='.'||ch=='-'||ch=='+'||(ch|32)=='e'||(ch|32)=='x'||((ch|32)>='a'&&(ch|32)<='f')||(ch|32)=='p'||(ch|32)=='i'||(ch|32)=='n'||(ch|32)=='t'||(ch|32)=='y')){ sunget(in,ch); break; } b[k++]=(u32)ch; } b[k]=0;
      u64 used; double v=parse_double(b,&used); if(!used) break; for(u64 z=(u64)k;z>used;z--) sunget(in,(int)b[z-1]);
      if(!skip){ void *p=*(void**)ap; ap+=8; if(size==4||size==8) *(double*)p=v; else *(float*)p=(float)v; n++; }
      any=1; continue;
    }
    if(cv=='s'||cv=='S'||cv=='c'||cv=='C'||cv=='['){
      int w= chw>=0? chw : ((cv=='S'||cv=='C')!=(wfmt!=0)); u8 set[256]; int inv=0;
      if(cv=='['){ memset(set,0,sizeof set); if(FC(i)=='^'){ inv=1; i++; } if(FC(i)==']'){ set[']']=1; i++; } while(FC(i) && FC(i)!=']'){ u32 a=FC(i); if(FC(i+1)=='-' && FC(i+2) && FC(i+2)!=']'){ for(u32 z=a;z<=FC(i+2)&&z<256;z++) set[z]=1; i+=3; } else { if(a<256) set[a]=1; i++; } } if(FC(i)==']') i++; }
      void *p= skip? 0 : *(void**)ap; if(!skip) ap+=8; u64 cap=0x7FFFFFFF; if(!skip && (wfmt? 1 : 1)){ /* secure versions pass a size after each buffer */ }
      int k=0;
      while(k<width){ ch=sget(in); if(ch<0) break;
        if(cv=='s'||cv=='S'){ if(ch==' '||(ch>='\t'&&ch<='\r')){ sunget(in,ch); break; } }
        else if(cv=='['){ int m= ch<256? set[ch] : 0; if(m==inv){ sunget(in,ch); break; } }
        if(p && (u64)k<cap){ if(w) ((wc*)p)[k]=(wc)ch; else ((char*)p)[k]=(char)ch; } k++; }
      if(!k) break;
      if(p && cv!='c' && cv!='C'){ if(w) ((wc*)p)[k]=0; else ((char*)p)[k]=0; }
      if(!skip) n++; any=1; continue;
    }
    break;
  }
  #undef FC
  if(!any && sget(in)<0) return -1;
  return n;
}
static int W_ u___stdio_common_vsscanf(u64 opt, const char *s, u64 n, const char *fmt, void *l, u8 *ap){ (void)opt;(void)n;(void)l; struct src in={(const u8*)s,0,0,0,0}; return vscan(&in,fmt,0,ap); }
static int W_ u___stdio_common_vswscanf(u64 opt, const wc *s, u64 n, const wc *fmt, void *l, u8 *ap){ (void)opt;(void)n;(void)l; struct src in={0,s,0,0,1}; return vscan(&in,fmt,1,ap); }
static int W_ u___stdio_common_vfscanf(u64 opt, struct ufile *f, const char *fmt, void *l, u8 *ap){ (void)opt;(void)l; struct src in={0,0,f,0,0}; return vscan(&in,fmt,0,ap); }
static int W_ u___stdio_common_vfwscanf(u64 opt, struct ufile *f, const wc *fmt, void *l, u8 *ap){ (void)opt;(void)l; struct src in={0,0,f,0,1}; return vscan(&in,fmt,1,ap); }
static int W_ u_sscanf(const char *s, const char *fmt, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,fmt); struct src in={(const u8*)s,0,0,0,0}; int r=vscan(&in,fmt,0,(u8*)ap); __builtin_ms_va_end(ap); return r; }
static int W_ u_vsscanf(const char *s, const char *fmt, u8 *ap){ struct src in={(const u8*)s,0,0,0,0}; return vscan(&in,fmt,0,ap); }
static int W_ u_swscanf(const wc *s, const wc *fmt, ...){ __builtin_ms_va_list ap; __builtin_ms_va_start(ap,fmt); struct src in={0,s,0,0,1}; int r=vscan(&in,fmt,1,(u8*)ap); __builtin_ms_va_end(ap); return r; }

/* ================= files and directories by name ================= */
struct stat64_ { u32 dev; u16 ino, mode; i16 nlink, uid, gid; u16 pad; u32 rdev; u32 pad2; i64 size, atime, mtime, ctime; };     /* struct _stat64 */
struct stat64i32_ { u32 dev; u16 ino, mode; i16 nlink, uid, gid; u16 pad; u32 rdev; i32 size; i64 atime, mtime, ctime; };           /* struct _stat64i32 */
static int stat_a(const char *p, i64 *size, int *dir){
  DWORD a=KCALL(DWORD (W_*)(const char*),GetFileAttributesA)(p); if(a==0xFFFFFFFF){ set_errno(ENOENT); return -1; }
  *dir=(a&0x10)!=0; *size=0;
  if(!*dir){ HANDLE h=KCALL(HANDLE (W_*)(const char*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE),CreateFileA)(p,0x80000000,3,0,3,0,0);
    if(h!=(HANDLE)-1){ KCALL(BOOL (W_*)(HANDLE,i64*),GetFileSizeEx)(h,size); KCALL(BOOL (W_*)(HANDLE),CloseHandle)(h); } }
  return 0;
}
static i64 now_unix(void){ u64 ft=0; KCALL(void (W_*)(u64*),GetSystemTimeAsFileTime)(&ft); return (i64)(ft/10000000ull)-11644473600ll; }
static void fill64(struct stat64_ *st, i64 size, int dir){ memset(st,0,sizeof *st); st->mode= dir? 0x41FF : 0x81B6; st->nlink=1; st->size=size; st->atime=st->mtime=st->ctime=now_unix(); st->dev=st->rdev=2; }
static int W_ u__stat64(const char *p, struct stat64_ *st){ i64 sz; int d; if(stat_a(p,&sz,&d)) return -1; fill64(st,sz,d); return 0; }
static int W_ u__wstat64(const wc *p, struct stat64_ *st){ WPATH(a,p); return u__stat64(a,st); }
static int W_ u__stat64i32(const char *p, struct stat64i32_ *st){ struct stat64_ s; if(u__stat64(p,&s)) return -1; memset(st,0,sizeof *st); st->mode=s.mode; st->nlink=1; st->size=(i32)s.size; st->atime=st->mtime=st->ctime=s.mtime; st->dev=st->rdev=2; return 0; }
static int W_ u__wstat64i32(const wc *p, struct stat64i32_ *st){ WPATH(a,p); return u__stat64i32(a,st); }
static int W_ u__fstat64(int fd, struct stat64_ *st){ struct fdent *f=fd_get(fd); if(!f){ set_errno(EBADF); return -1; } i64 s=fd_size(f); fill64(st,s<0? 0 : s,0); if(f->isdev) st->mode=0x2000; return 0; }
static int W_ u__fstat64i32(int fd, struct stat64i32_ *st){ struct stat64_ s; if(u__fstat64(fd,&s)) return -1; memset(st,0,sizeof *st); st->mode=s.mode; st->nlink=1; st->size=(i32)s.size; st->atime=st->mtime=st->ctime=s.mtime; return 0; }
static int W_ u__access(const char *p, int m){ (void)m; i64 s; int d; return stat_a(p,&s,&d); }
static int W_ u__waccess(const wc *p, int m){ WPATH(a,p); return u__access(a,m); }
static int W_ u__access_s(const char *p, int m){ return u__access(p,m)? *errno_p() : 0; }
static int W_ u__waccess_s(const wc *p, int m){ return u__waccess(p,m)? *errno_p() : 0; }
static int W_ u__wchmod(const wc *p, int m){ (void)m; return u__waccess(p,0); }
static int W_ u__chmod(const char *p, int m){ (void)m; return u__access(p,0); }
static int okfail(BOOL ok){ if(ok) return 0; u32 e=win32_get_last_error(); set_errno(e==2||e==3? ENOENT : e==183||e==80? EEXIST : EACCES); return -1; }
static int W_ u_remove(const char *p){ return okfail(KCALL(BOOL (W_*)(const char*),DeleteFileA)(p)); }
static int W_ u__wremove(const wc *p){ WPATH(a,p); return u_remove(a); }
static int W_ u__unlink(const char *p){ return u_remove(p); }
static int W_ u__wunlink(const wc *p){ return u__wremove(p); }
static int W_ u_rename(const char *a, const char *b){ return okfail(KCALL(BOOL (W_*)(const char*,const char*),MoveFileA)(a,b)); }
static int W_ u__wrename(const wc *a, const wc *b){ WPATH(x,a); WPATH(y,b); return u_rename(x,y); }
static int W_ u__mkdir(const char *p){ return okfail(KCALL(BOOL (W_*)(const char*,void*),CreateDirectoryA)(p,0)); }
static int W_ u__wmkdir(const wc *p){ WPATH(a,p); return u__mkdir(a); }
static int W_ u__rmdir(const char *p){ return okfail(KCALL(BOOL (W_*)(const char*),RemoveDirectoryA)(p)); }
static int W_ u__wrmdir(const wc *p){ WPATH(a,p); return u__rmdir(a); }
static int W_ u__chdir(const char *p){ return okfail(KCALL(BOOL (W_*)(const char*),SetCurrentDirectoryA)(p)); }
static int W_ u__wchdir(const wc *p){ WPATH(a,p); return u__chdir(a); }
static char *W_ u__getcwd(char *b, int n){
  char t[300]; KCALL(DWORD (W_*)(DWORD,char*),GetCurrentDirectoryA)(sizeof t,t); u64 l=strlen(t);
  if(!b){ u64 sz= (u64)n>l+1? (u64)n : l+1; b=heap_alloc(sz); if(!b) return 0; n=(int)sz; }
  if((u64)n<=l){ set_errno(ERANGE); return 0; } memcpy(b,t,l+1); return b;
}
static wc *W_ u__wgetcwd(wc *b, int n){
  char t[300]; KCALL(DWORD (W_*)(DWORD,char*),GetCurrentDirectoryA)(sizeof t,t); wc *w=a2w_dup(t); if(!w) return 0; u64 l=wlen(w);
  if(!b){ u64 sz= (u64)n>l+1? (u64)n : l+1; b=heap_alloc(sz*2); if(!b){ heap_free(w); return 0; } n=(int)sz; }
  if((u64)n<=l){ heap_free(w); set_errno(ERANGE); return 0; } memcpy(b,w,(l+1)*2); heap_free(w); return b;
}
static wc *W_ u__wgetdcwd(int d, wc *b, int n){ (void)d; return u__wgetcwd(b,n); }
static char *W_ u__getdcwd(int d, char *b, int n){ (void)d; return u__getcwd(b,n); }
static char *W_ u__fullpath(char *b, const char *p, u64 n){
  char t[300]; DWORD r=KCALL(DWORD (W_*)(const char*,DWORD,char*,char**),GetFullPathNameA)(p,sizeof t,t,0); if(!r||r>=sizeof t) return 0;
  if(!b){ b=heap_alloc(r+1); if(!b) return 0; n=r+1; } if(n<=r){ set_errno(ERANGE); return 0; } memcpy(b,t,r+1); return b;
}
static wc *W_ u__wfullpath(wc *b, const wc *p, u64 n){
  WPATH(a,p); char t[300]; if(!u__fullpath(t,a,sizeof t)) return 0; wc *w=a2w_dup(t); if(!w) return 0; u64 l=wlen(w);
  if(!b){ b=w; return b; } if(n<=l){ heap_free(w); set_errno(ERANGE); return 0; } memcpy(b,w,(l+1)*2); heap_free(w); return b;
}
static u32 W_ u__getdrives(void){ return 1<<2; }
static int mktemp_core(char *t){ u64 l=strlen(t); if(l<6) return EINVAL; char *x=t+l-6; for(int i=0;i<6;i++) if(x[i]!='X') return EINVAL;
  static u32 seq; for(int tries=0;tries<26;tries++){ u32 v=(u32)(cur? cur->pid : 0)*1000+seq++; for(int i=5;i>=1;i--){ x[i]=(char)('0'+v%10); v/=10; } x[0]=(char)('a'+tries); i64 s; int d; if(stat_a(t,&s,&d)) return 0; } return EEXIST; }
static int W_ u__mktemp_s(char *t, u64 n){ (void)n; return mktemp_core(t); }
static int W_ u__wmktemp_s(wc *t, u64 n){ char a[300]; w2a_buf(t,a,sizeof a); int r=mktemp_core(a); if(r) return r; u64 l=strlen(a); for(u64 i=0;i<=l && i<n;i++) t[i]=(u8)a[i]; return 0; }
static char *W_ u__mktemp(char *t){ return mktemp_core(t)? 0 : t; }

/* ================= environment ================= */
static char *W_ u_getenv(const char *k){
  static char buf[8][1024]; static int slot; char *b=buf[slot++&7];
  DWORD r=KCALL(DWORD (W_*)(const char*,char*,DWORD),GetEnvironmentVariableA)(k,b,1024); return (r && r<1024)? b : 0;
}
static wc *W_ u__wgetenv(const wc *k){
  static wc buf[8][1024]; static int slot; wc *w=buf[slot++&7]; WPATH(a,k); char *v=u_getenv(a); if(!v) return 0;
  const u8 *p=(const u8*)v; u64 n=0; while(*p && n<1022){ u32 c=utf8_dec(&p); w[n++]=(wc)(c>0xFFFF? 0xFFFD : c); } w[n]=0; return w;
}
static int W_ u_getenv_s(u64 *len, char *b, u64 n, const char *k){ char *v=u_getenv(k); if(!v){ if(len) *len=0; if(b&&n) b[0]=0; return 0; } u64 l=strlen(v); if(len) *len=l+1; if(!b) return 0; if(n<=l) return ERANGE; memcpy(b,v,l+1); return 0; }
static int W_ u__wgetenv_s(u64 *len, wc *b, u64 n, const wc *k){ wc *v=u__wgetenv(k); if(!v){ if(len) *len=0; if(b&&n) b[0]=0; return 0; } u64 l=wlen(v); if(len) *len=l+1; if(!b) return 0; if(n<=l) return ERANGE; memcpy(b,v,(l+1)*2); return 0; }
static int W_ u__dupenv_s(char **o, u64 *len, const char *k){ char *v=u_getenv(k); *o=v? u__strdup(v) : 0; if(len) *len=v? strlen(v)+1 : 0; return 0; }
static int W_ u__wdupenv_s(wc **o, u64 *len, const wc *k){ wc *v=u__wgetenv(k); *o=v? u__wcsdup(v) : 0; if(len) *len=v? wlen(v)+1 : 0; return 0; }
static int W_ u__putenv_s(const char *k, const char *v){ return KCALL(BOOL (W_*)(const char*,const char*),SetEnvironmentVariableA)(k,v&&*v? v : 0)? 0 : EINVAL; }
static int W_ u__putenv(const char *kv){ char k[128]; int i=0; while(kv[i] && kv[i]!='=' && i<127){ k[i]=kv[i]; i++; } k[i]=0; if(kv[i]!='=') return -1; return u__putenv_s(k,kv+i+1)? -1 : 0; }
static int W_ u__wputenv(const wc *kv){ char a[600]; w2a_buf(kv,a,sizeof a); return u__putenv(a); }
static int W_ u__wputenv_s(const wc *k, const wc *v){ char a[128], b[512]; w2a_buf(k,a,sizeof a); w2a_buf(v,b,sizeof b); return u__putenv_s(a,b); }

/* ================= start-up: wide argv, environment, command line ================= */
static wc **wargv_[MAXPROC]; static wc *wenv_[MAXPROC][1]; static wc **wenv_p[MAXPROC]; static int wargc_[MAXPROC]; static wc *wcmd_[MAXPROC];
static void build_wargv(void){
  int p=cur->pid; if(wargv_[p] && wargc_[p]==cur->argc) return;
  wc **v=heap_alloc((u64)(cur->argc+1)*8); if(!v) return; for(int i=0;i<cur->argc;i++) v[i]=a2w_dup(cur->argvp[i]); v[cur->argc]=0; wargv_[p]=v; wargc_[p]=cur->argc;
}
static int W_ u__configure_wide_argv(int m){ (void)m; build_wargv(); return 0; }
static int W_ u__initialize_wide_environment(void){ wenv_[cur->pid][0]=0; wenv_p[cur->pid]=wenv_[cur->pid]; return 0; }
static wc **W_ u__get_initial_wide_environment(void){ u__initialize_wide_environment(); return wenv_p[cur->pid]; }
static wc ***W_ u___p__wenviron(void){ u__initialize_wide_environment(); return &wenv_p[cur->pid]; }
static wc ***W_ u___p___wargv(void){ build_wargv(); return &wargv_[cur->pid]; }
static int *W_ u___p___wargc(void){ build_wargv(); return &wargc_[cur->pid]; }
static int W_ u___wgetmainargs(int *argc, wc ***argv, wc ***env, int wild, void *si){ (void)wild;(void)si; build_wargv(); *argc=wargc_[cur->pid]; *argv=wargv_[cur->pid]; if(env) *env=u__get_initial_wide_environment(); return 0; }
static const char *skip_prog(const char *c){                          /* the command line after the program name */
  if(*c=='"'){ c++; while(*c && *c!='"') c++; if(*c) c++; } else while(*c && *c!=' ' && *c!='\t') c++;
  while(*c==' '||*c=='\t') c++; return c;
}
static const char *W_ u__get_narrow_winmain_command_line(void){ return skip_prog(cur->cmdline); }
static wc *W_ u__get_wide_winmain_command_line(void){ if(!wcmd_[cur->pid]) wcmd_[cur->pid]=a2w_dup(skip_prog(cur->cmdline)); return wcmd_[cur->pid]; }
static char pgm[MAXPROC][300]; static wc *wpgm[MAXPROC];
static char *pgmptr(void){ char *b=pgm[cur->pid]; KCALL(DWORD (W_*)(HANDLE,char*,DWORD),GetModuleFileNameA)(0,b,300); return b; }
static int W_ u__get_pgmptr(char **o){ *o=pgmptr(); return 0; }
static int W_ u__get_wpgmptr(wc **o){ if(!wpgm[cur->pid]) wpgm[cur->pid]=a2w_dup(pgmptr()); *o=wpgm[cur->pid]; return 0; }
static char **W_ u___p__pgmptr(void){ static char *p; p=pgmptr(); return &p; }
static wc **W_ u___p__wpgmptr(void){ wc *w; u__get_wpgmptr(&w); static wc *p; p=w; return &p; }

/* ================= runtime ================= */
struct onexit_table { u64 *first, *last, *end; };
static int W_ u__initialize_onexit_table(struct onexit_table *t){ if(t){ t->first=t->last=t->end=0; } return 0; }
static int W_ u__register_onexit_function(struct onexit_table *t, u64 fn){
  if(!t) return -1;
  if(t->last==t->end){ u64 n= t->first? (u64)(t->end-t->first)*2 : 16; u64 used= t->first? (u64)(t->last-t->first) : 0; u64 *nb=heap_alloc(n*8); if(!nb) return -1; if(t->first){ memcpy(nb,t->first,used*8); heap_free(t->first); } t->first=nb; t->last=nb+used; t->end=nb+n; }
  *t->last++=fn; return 0;
}
static int W_ u__execute_onexit_table(struct onexit_table *t){
  if(!t || !t->first) return 0; u64 *f=t->first, *l=t->last; t->first=t->last=t->end=0;
  while(l>f){ u64 fn=*--l; if(fn) ((void (W_*)(void))fn)(); }
  heap_free(f); return 0;
}
static void W_ u__register_thread_local_exe_atexit_callback(u64 cb){ (void)cb; }
static int W_ u__seh_filter_dll(u32 code, void *ep){ (void)code;(void)ep; return 0; }       /* EXCEPTION_CONTINUE_SEARCH */
static u64 new_handler;
static u64 W_ u__set_new_handler(u64 h){ u64 o=new_handler; new_handler=h; return o; }
static u64 W_ u__query_new_handler(void){ return new_handler; }
static int W_ u__callnewh(u64 sz){ if(!new_handler) return 0; return ((int (W_*)(u64))new_handler)(sz); }
static void W_ u_terminate(void){ kprintf("\n*** C++ runtime: terminate() called\n"); proc_exit(3); }
static void W_ u_abort(void){ kprintf("\n*** abort() called\n"); proc_exit(3); }
static void W_ u__wassert(const wc *msg, const wc *file, u32 line){ char a[200], b[200]; w2a_buf(msg,a,sizeof a); w2a_buf(file,b,sizeof b); kprintf("\n*** Assertion failed: %s, file %s, line %u\n",a,b,line); proc_exit(3); }
static void W_ u__assert(const char *msg, const char *file, u32 line){ kprintf("\n*** Assertion failed: %s, file %s, line %u\n",msg,file,line); proc_exit(3); }
static u64 sig_handlers[MAXPROC][23];
static void ucrt_proc_reset_startup(int p){                            /* start-up data cached per process slot must not leak into the next program in that slot */
  if(wargv_[p]){ for(int i=0;i<wargc_[p];i++) heap_free(wargv_[p][i]); heap_free(wargv_[p]); } wargv_[p]=0; wargc_[p]=0;
  if(wcmd_[p]) heap_free(wcmd_[p]); wcmd_[p]=0; if(wpgm[p]) heap_free(wpgm[p]); wpgm[p]=0;
  memset(sig_handlers[p],0,sizeof sig_handlers[p]);
}
static u64 W_ u_signal(int s, u64 h){ if(s<0||s>=23) return (u64)-1; u64 o=sig_handlers[cur->pid][s]; sig_handlers[cur->pid][s]=h; return o; }
static int W_ u_raise(int s){ if(s<0||s>=23) return -1; u64 h=sig_handlers[cur->pid][s]; if(h==1) return 0; if(h>1){ ((void (W_*)(int))h)(s); return 0; } if(s==2||s==15||s==22||s==6){ kprintf("\n*** raise(%d)\n",s); proc_exit(3); } return 0; }
static const char *errtext(int e){
  switch(e){ case 0: return "No error"; case 1: return "Operation not permitted"; case ENOENT: return "No such file or directory"; case EBADF: return "Bad file descriptor";
    case ENOMEM: return "Not enough space"; case EACCES: return "Permission denied"; case EEXIST: return "File exists"; case EINVAL: return "Invalid argument";
    case 24: return "Too many open files"; case 28: return "No space left on device"; case ERANGE: return "Result too large"; default: return "Unknown error"; }
}
static int W_ u_strerror_s(char *b, u64 n, int e){ return u_strcpy_s(b,n,errtext(e)); }
static int W_ u__wcserror_s(wc *b, u64 n, int e){ const char *t=errtext(e); u64 i=0; for(;t[i] && i+1<n;i++) b[i]=(u8)t[i]; if(n) b[i]=0; return 0; }
static wc *W_ u__wcserror(int e){ static wc b[64]; u__wcserror_s(b,64,e); return b; }
static int W_ u_fegetround(void){ return 0; }
static int W_ u_fesetround(int r){ return r==0? 0 : 1; }
static int W_ u_feclearexcept(int e){ (void)e; return 0; }
static int W_ u_fetestexcept(int e){ (void)e; return 0; }
static int W_ u_fegetenv(u32 *env){ env[0]=0x9001F; env[1]=0; return 0; }
static int W_ u_fesetenv(const u32 *env){ (void)env; return 0; }
static int W_ u_feholdexcept(u32 *env){ return u_fegetenv(env); }
static u32 W_ u__control87(u32 nw, u32 mask){ (void)nw;(void)mask; return 0x9001F; }
static u32 W_ u__clearfp(void){ return 0; }
static u32 W_ u__statusfp(void){ return 0; }
static void *W_ u__aligned_malloc(u64 n, u64 al){ if(al<16) al=16; u8 *raw=heap_alloc_prog(n+al+8); if(!raw) return 0; u64 p=((u64)raw+8+al-1)&~(al-1); ((u64*)p)[-1]=(u64)raw; return (void*)p; }
static void W_ u__aligned_free(void *p){ if(p) heap_free((void*)((u64*)p)[-1]); }
static u64 W_ u__aligned_msize(void *p, u64 al, u64 off){ (void)al;(void)off; if(!p) return 0; u8 *raw=(u8*)((u64*)p)[-1]; return heap_size(raw)-(u64)((u8*)p-raw); }
static void *W_ u__aligned_realloc(void *p, u64 n, u64 al){
  if(!p) return u__aligned_malloc(n,al); if(!n){ u__aligned_free(p); return 0; }
  void *q=u__aligned_malloc(n,al); if(!q) return 0; u64 old=u__aligned_msize(p,al,0); memcpy(q,p,old<n? old : n); u__aligned_free(p); return q;
}
static void *W_ u__aligned_offset_malloc(u64 n, u64 al, u64 off){ (void)off; return u__aligned_malloc(n,al); }
static void *W_ u__recalloc(void *p, u64 c, u64 s){ u64 n=c*s, old= p? heap_size(p) : 0; void *q=heap_realloc(p,n); if(q && n>old) memset((u8*)q+old,0,n-old); return q; }
static void *W_ u__expand(void *p, u64 n){ (void)p;(void)n; return 0; }
static void *W_ u_malloc(u64 n){ void *p=heap_alloc_prog(n); if(!p) set_errno(ENOMEM); return p; }   /* freed when the process ends */
static void W_ u_free(void *p){ heap_free(p); }
static void *W_ u_calloc(u64 c, u64 s){ if(s && c>~0ull/s) return 0; return u_malloc(c*s); }
static void *W_ u_realloc(void *p, u64 n){ if(p && !n){ heap_free(p); return 0; } return p? heap_realloc(p,n) : u_malloc(n); }
static u64 W_ u__msize(void *p){ return heap_size(p); }

/* ================= time ================= */
static const char *const dnames[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char *const mnames[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static char *names_list(const char *const *n, int cnt){ char *b=heap_alloc(400); if(!b) return 0; u64 k=0; for(int i=0;i<cnt;i++){ b[k++]=':'; b[k++]=n[i][0]; b[k++]=n[i][1]; b[k++]=n[i][2]; b[k++]=':'; for(const char *p=n[i];*p;p++) b[k++]=*p; } b[k]=0; return b; }
static wc *wnames_list(const char *const *n, int cnt){ char *a=names_list(n,cnt); wc *w=a2w_dup(a); heap_free(a); return w; }
static char *W_ u__Getdays(void){ return names_list(dnames,7); }
static char *W_ u__Getmonths(void){ return names_list(mnames,12); }
static wc *W_ u__W_Getdays(void){ return wnames_list(dnames,7); }
static wc *W_ u__W_Getmonths(void){ return wnames_list(mnames,12); }
static void *W_ u__Gettnames(void){ return heap_alloc(0x300); }
static void *W_ u__W_Gettnames(void){ return heap_alloc(0x300); }
struct tm_ { int sec, min, hour, mday, mon, year, wday, yday, isdst; };
static u64 strftime_core(struct sink *s, const u32 *fmt, const struct tm_ *t){
  char b[64];
  for(;*fmt;fmt++){
    if(*fmt!='%'){ put_cp(s,*fmt); continue; }
    fmt++; if(*fmt=='#') fmt++; if(!*fmt) break; b[0]=0;
    int h12= t->hour%12? t->hour%12 : 12;
    switch(*fmt){
      case 'a': ksnprintf(b,sizeof b,"%.3s",dnames[t->wday%7]); break; case 'A': ksnprintf(b,sizeof b,"%s",dnames[t->wday%7]); break;
      case 'b': case 'h': ksnprintf(b,sizeof b,"%.3s",mnames[t->mon%12]); break; case 'B': ksnprintf(b,sizeof b,"%s",mnames[t->mon%12]); break;
      case 'c': ksnprintf(b,sizeof b,"%02d/%02d/%02d %02d:%02d:%02d",t->mon+1,t->mday,t->year%100,t->hour,t->min,t->sec); break;
      case 'd': ksnprintf(b,sizeof b,"%02d",t->mday); break; case 'e': ksnprintf(b,sizeof b,"%2d",t->mday); break;
      case 'D': case 'x': ksnprintf(b,sizeof b,"%02d/%02d/%02d",t->mon+1,t->mday,t->year%100); break;
      case 'F': ksnprintf(b,sizeof b,"%d-%02d-%02d",t->year+1900,t->mon+1,t->mday); break;
      case 'H': ksnprintf(b,sizeof b,"%02d",t->hour); break; case 'I': ksnprintf(b,sizeof b,"%02d",h12); break;
      case 'j': ksnprintf(b,sizeof b,"%03d",t->yday+1); break; case 'm': ksnprintf(b,sizeof b,"%02d",t->mon+1); break;
      case 'M': ksnprintf(b,sizeof b,"%02d",t->min); break; case 'n': ksnprintf(b,sizeof b,"\n"); break; case 't': ksnprintf(b,sizeof b,"\t"); break;
      case 'p': ksnprintf(b,sizeof b,"%s",t->hour<12? "AM" : "PM"); break; case 'r': ksnprintf(b,sizeof b,"%02d:%02d:%02d %s",h12,t->min,t->sec,t->hour<12? "AM" : "PM"); break;
      case 'R': ksnprintf(b,sizeof b,"%02d:%02d",t->hour,t->min); break; case 'S': ksnprintf(b,sizeof b,"%02d",t->sec); break;
      case 'T': case 'X': ksnprintf(b,sizeof b,"%02d:%02d:%02d",t->hour,t->min,t->sec); break;
      case 'u': ksnprintf(b,sizeof b,"%d",t->wday? t->wday : 7); break; case 'w': ksnprintf(b,sizeof b,"%d",t->wday); break;
      case 'y': ksnprintf(b,sizeof b,"%02d",t->year%100); break; case 'Y': ksnprintf(b,sizeof b,"%d",t->year+1900); break; case 'C': ksnprintf(b,sizeof b,"%02d",(t->year+1900)/100); break;
      case 'z': ksnprintf(b,sizeof b,"+0000"); break; case 'Z': ksnprintf(b,sizeof b,"UTC"); break; case '%': ksnprintf(b,sizeof b,"%%"); break;
      case 'U': ksnprintf(b,sizeof b,"%02d",(t->yday+7-t->wday)/7); break; case 'W': ksnprintf(b,sizeof b,"%02d",(t->yday+7-(t->wday+6)%7)/7); break;
      case 'V': case 'G': case 'g': ksnprintf(b,sizeof b,"%02d",(t->yday+7-(t->wday+6)%7)/7); break;
      default: b[0]='%'; b[1]=(char)*fmt; b[2]=0;
    }
    for(char *p=b;*p;p++) sput(s,(u8)*p);
  }
  return s->len;
}
static u64 strftime_any(void *buf, u64 n, const void *fmt, int wide, const struct tm_ *t){
  u32 f[512]; u64 k=0; if(wide){ const wc *w=fmt; while(w[k] && k<511){ f[k]=w[k]; k++; } } else { const u8 *p=fmt; while(*p && k<511) f[k++]=utf8_dec(&p); } f[k]=0;
  struct sink s={0}; s.wide=wide; s.nb=buf; s.wb=buf; s.cap=n; u64 r=strftime_core(&s,f,t);
  if(r>=n){ if(n){ if(wide) ((wc*)buf)[0]=0; else ((char*)buf)[0]=0; } return 0; }
  if(wide) ((wc*)buf)[r]=0; else ((char*)buf)[r]=0; return r;
}
static u64 W_ u__Strftime(char *b, u64 n, const char *f, const struct tm_ *t, void *names){ (void)names; return strftime_any(b,n,f,0,t); }
static u64 W_ u__Wcsftime(wc *b, u64 n, const wc *f, const struct tm_ *t, void *names){ (void)names; return strftime_any(b,n,f,1,t); }
static u64 W_ u_wcsftime(wc *b, u64 n, const wc *f, const struct tm_ *t){ return strftime_any(b,n,f,1,t); }
static u64 W_ u_strftime(char *b, u64 n, const char *f, const struct tm_ *t){ return strftime_any(b,n,f,0,t); }
static i32 tz_seconds=0; static int daylight_=0; static char tzn0[8]="UTC", tzn1[8]="UTC"; static char *tznames[2]={tzn0,tzn1};
static i32 *W_ u___timezone(void){ return &tz_seconds; }
static int *W_ u___daylight(void){ return &daylight_; }
static i32 dstbias=0;
static i32 *W_ u___dstbias(void){ return &dstbias; }
static char **W_ u___tzname(void){ return tznames; }
static void W_ u__tzset(void){}
static int W_ u__get_timezone(i32 *t){ *t=0; return 0; }
static int W_ u__get_daylight(int *d){ *d=0; return 0; }
static int W_ u__get_dstbias(i32 *d){ *d=0; return 0; }
static int W_ u__get_tzname(u64 *ret, char *b, u64 n, int i){ (void)i; if(ret) *ret=4; if(b && n>=4) memcpy(b,"UTC",4); return 0; }
static void gm(i64 t, struct tm_ *o){                                  /* seconds since 1970 -> broken-down UTC */
  i64 days=t/86400, rem=t%86400; if(rem<0){ rem+=86400; days--; }
  o->hour=(int)(rem/3600); o->min=(int)(rem%3600/60); o->sec=(int)(rem%60); o->wday=(int)((days+4)%7); if(o->wday<0) o->wday+=7;
  i64 y=1970; for(;;){ int ly=(y%4==0&&y%100!=0)||y%400==0; int dy=365+ly; if(days>=dy){ days-=dy; y++; } else if(days<0){ y--; ly=(y%4==0&&y%100!=0)||y%400==0; days+=365+ly; } else break; }
  int ly=(y%4==0&&y%100!=0)||y%400==0; o->yday=(int)days; static const int ml[12]={31,28,31,30,31,30,31,31,30,31,30,31};
  int m=0; while(m<12){ int d=ml[m]+(m==1&&ly); if(days<d) break; days-=d; m++; } o->mon=m; o->mday=(int)days+1; o->year=(int)(y-1900); o->isdst=0;
}
static struct tm_ gm_buf[MAXPROC];
static struct tm_ *W_ u__gmtime64(const i64 *t){ if(!t) return 0; gm(*t,&gm_buf[cur? cur->pid : 0]); return &gm_buf[cur? cur->pid : 0]; }
static int W_ u__gmtime64_s(struct tm_ *o, const i64 *t){ if(!o||!t) return EINVAL; gm(*t,o); return 0; }
static struct tm_ *W_ u__gmtime32(const i32 *t){ i64 v=*t; return u__gmtime64(&v); }
static int W_ u__gmtime32_s(struct tm_ *o, const i32 *t){ i64 v=*t; return u__gmtime64_s(o,&v); }
static double W_ u__difftime64(i64 a, i64 b){ return (double)(a-b); }
static double W_ u__difftime32(i32 a, i32 b){ return (double)(a-b); }
static i32 W_ u__time32(i32 *t){ i32 v=(i32)now_unix(); if(t) *t=v; return v; }
struct timeb_ { i64 time; u16 ms; i16 tz, dst; };
static void W_ u__ftime64(struct timeb_ *b){ u64 ft=0; KCALL(void (W_*)(u64*),GetSystemTimeAsFileTime)(&ft); b->time=(i64)(ft/10000000ull)-11644473600ll; b->ms=(u16)(ft/10000%1000); b->tz=0; b->dst=0; }
static int W_ u__ftime64_s(struct timeb_ *b){ u__ftime64(b); return 0; }
static i32 W_ u_clock(void){ return (i32)ticks; }
static char *W_ u_asctime(const struct tm_ *t){ static char b[32]; ksnprintf(b,sizeof b,"%.3s %.3s %2d %02d:%02d:%02d %d\n",dnames[t->wday%7],mnames[t->mon%12],t->mday,t->hour,t->min,t->sec,t->year+1900); return b; }
static int W_ u_asctime_s(char *b, u64 n, const struct tm_ *t){ return u_strcpy_s(b,n,u_asctime(t)); }
static wc *W_ u__wasctime(const struct tm_ *t){ static wc b[32]; char *a=u_asctime(t); for(int i=0;i<32;i++){ b[i]=(u8)a[i]; if(!a[i]) break; } return b; }
static char *W_ u__ctime64(const i64 *t){ struct tm_ m; gm(*t,&m); return u_asctime(&m); }
static wc *W_ u__wctime64(const i64 *t){ struct tm_ m; gm(*t,&m); return u__wasctime(&m); }
static char *W_ u__strdate(char *b){ struct tm_ m; gm(now_unix(),&m); ksnprintf(b,9,"%02d/%02d/%02d",m.mon+1,m.mday,m.year%100); return b; }
static char *W_ u__strtime(char *b){ struct tm_ m; gm(now_unix(),&m); ksnprintf(b,9,"%02d:%02d:%02d",m.hour,m.min,m.sec); return b; }

/* ================= exports ================= */
#define U(n) {"msvcrt",#n,u_##n}
#define UA(a,n) {"msvcrt",a,u_##n}
const struct export ucrt_exports[] = {
  U(_errno), U(__doserrno), U(_get_errno), U(_set_errno), U(_invalid_parameter_noinfo), U(_invalid_parameter_noinfo_noreturn), U(_get_invalid_parameter_handler),
  U(_set_invalid_parameter_handler), U(_set_thread_local_invalid_parameter_handler), UA("_get_thread_local_invalid_parameter_handler",_get_invalid_parameter_handler),
  U(__pctype_func), U(_isctype), U(_isctype_l), U(isalnum), U(islower), U(isupper), U(isprint), U(ispunct), U(iscntrl), U(isgraph), U(isalpha), U(isdigit), U(isspace),
  U(iswdigit), U(iswspace), U(iswalpha), U(iswalnum), U(iswupper), U(iswlower), U(iswpunct), U(iswprint), U(iswxdigit), U(iswcntrl), U(iswgraph), U(towupper), U(towlower),
  U(_tolower), U(_toupper), U(_towlower_l), U(_towupper_l), U(_iswctype_l), U(__iscsym), U(__iscsymf),
  U(_lock_locales), UA("_unlock_locales",_lock_locales), U(___lc_locale_name_func), U(___lc_collate_cp_func), U(_wsetlocale), U(_create_locale), U(_wcreate_locale),
  U(_free_locale), U(_get_current_locale), U(__acrt_get_locale_data_prefix),
  U(strcpy_s), U(strcat_s), U(strncpy_s), U(strncat_s), U(strncat), U(strspn), U(_strdup), UA("strdup",_strdup), U(strtok_s), U(strtok), U(_strupr), U(_strlwr), U(_strupr_s), U(_strlwr_s),
  U(_strrev), U(_memicmp), U(memcpy_s), U(memmove_s), U(wmemcpy_s), U(wmemmove_s), U(__strncnt), U(strcoll), U(_stricoll), U(strxfrm), UA("_strcoll_l",strcoll),
  U(_wcsdup), UA("wcsdup",_wcsdup), U(wmemcpy), U(wmemmove), U(wmemset), U(wmemchr), U(wmemcmp), U(wcsrchr), U(wcsstr), U(wcsspn), U(wcscspn), U(wcspbrk), U(wcsncpy), U(wcsncat),
  U(wcscpy_s), U(wcscat_s), U(wcsncpy_s), U(wcsncat_s), U(wcstok_s), U(wcstok), U(_wcsicmp), UA("_wcsicmp_l",_wcsicmp), UA("_wcsicoll",_wcsicmp), U(_wcslwr), U(_wcsupr), U(_wcslwr_s), U(_wcsupr_s),
  U(_wcsrev), U(_wcsset), U(_wcsnset), U(wcscoll), U(wcsxfrm),
  U(mbtowc), U(mblen), U(_mbclen), U(wctomb), U(btowc), U(wctob), U(mbstowcs), U(wcstombs), U(mbstowcs_s), U(_mbstowcs_s_l), U(wcstombs_s), U(_wcstombs_l), U(mbsrtowcs), U(wcsrtombs),
  U(_mbsinc), U(_mbsdec), U(_mbslen), U(_mbschr),
  U(strtod), U(strtof), U(wcstod), U(wcstof), U(atof), U(_wtof), U(_strtod_l), U(_wcstod_l), UA("strtold",strtod), UA("wcstold",wcstod),
  U(_strtoi64), U(_strtoui64), U(_wcstoi64), U(_wcstoui64), UA("strtoimax",_strtoi64), UA("strtoumax",_strtoui64), U(atol), U(_atoi64), U(_wtoi), UA("_wtol",_wtoi), U(_wtoi64), U(atoll), U(_wtoll),
  U(_itoa_s), U(_ltoa_s), U(_i64toa_s), U(_ui64toa_s), U(_ultoa_s), U(_itow_s), UA("_ltow_s",_itow_s), U(_i64tow_s), U(_ui64tow_s), U(_itoa), U(_ltoa), U(_i64toa), U(_ui64toa),
  U(_ltow), U(_i64tow), U(_ui64tow), U(labs), U(llabs), UA("_abs64",llabs), UA("imaxabs",llabs), U(div), UA("ldiv",div), U(lldiv), UA("imaxdiv",lldiv), U(bsearch), U(_lfind), U(qsort_s), U(rand_s),
  U(__stdio_common_vsprintf), U(__stdio_common_vsprintf_s), U(__stdio_common_vsprintf_p), U(__stdio_common_vsnprintf_s), U(__stdio_common_vswprintf), U(__stdio_common_vswprintf_s),
  U(__stdio_common_vswprintf_p), U(__stdio_common_vsnwprintf_s), U(vsnprintf), U(_vsnprintf), U(vsprintf), U(_vscprintf), U(_vscwprintf), U(vswprintf), U(_vsnwprintf),
  U(sprintf), U(snprintf), U(_snprintf), U(sprintf_s), U(swprintf), U(_snwprintf), U(swprintf_s),
  U(_open), U(_wopen), U(_sopen), U(_wsopen), U(_sopen_s), U(_wsopen_s), U(_close), U(_read), U(_write), U(_lseek), U(_lseeki64), U(_tell), U(_telli64), U(_filelength), U(_filelengthi64),
  U(_eof), U(_commit), U(_chsize), U(_chsize_s), U(_setmode), U(_isatty), U(_get_osfhandle), U(_open_osfhandle), U(_dup), U(_dup2), U(_locking), U(_pipe), U(_getmaxstdio), U(_setmaxstdio),
  U(__acrt_iob_func), U(__iob_func), U(fopen), U(fopen_s), U(_fsopen), U(_wfopen), U(_wfopen_s), U(_wfsopen), U(_fdopen), U(_wfdopen), U(fclose), U(_fcloseall), U(freopen), U(_wfreopen),
  U(freopen_s), U(_wfreopen_s), U(_fileno), U(fread), UA("_fread_nolock",fread), U(fwrite), UA("_fwrite_nolock",fwrite), U(fgetc), UA("getc",fgetc), UA("_getc_nolock",fgetc), UA("_fgetc_nolock",fgetc),
  U(ungetc), UA("_ungetc_nolock",ungetc), U(fputc), UA("putc",fputc), UA("_putc_nolock",fputc), UA("_fputc_nolock",fputc), U(putchar), U(getchar), U(fputs), U(puts), U(fgets),
  U(fgetwc), UA("getwc",fgetwc), UA("_fgetwc_nolock",fgetwc), U(fputwc), UA("putwc",fputwc), UA("_fputwc_nolock",fputwc), U(ungetwc), UA("_ungetwc_nolock",ungetwc), U(fgetws), U(fputws),
  U(fflush), UA("_fflush_nolock",fflush), U(_flushall), U(_fseeki64), UA("_fseeki64_nolock",_fseeki64), U(fseek), UA("_fseek_nolock",fseek), U(_ftelli64), UA("_ftelli64_nolock",_ftelli64), U(ftell), UA("_ftell_nolock",ftell),
  U(rewind), U(fgetpos), U(fsetpos), U(feof), U(ferror), U(clearerr), U(clearerr_s), U(setbuf), U(setvbuf), U(_lock_file), UA("_unlock_file",_lock_file), U(_get_stream_buffer_pointers),
  U(tmpfile), U(tmpfile_s),
  U(__stdio_common_vfprintf), U(__stdio_common_vfprintf_s), UA("__stdio_common_vfprintf_p",__stdio_common_vfprintf), U(__stdio_common_vfwprintf), U(__stdio_common_vfwprintf_s),
  UA("__stdio_common_vfwprintf_p",__stdio_common_vfwprintf), U(vfprintf), U(vprintf), U(vfwprintf), U(printf), U(fprintf), U(wprintf), U(fwprintf), U(perror),
  U(__stdio_common_vsscanf), U(__stdio_common_vswscanf), U(__stdio_common_vfscanf), U(__stdio_common_vfwscanf), U(sscanf), U(vsscanf), U(swscanf),
  U(_stat64), U(_wstat64), U(_stat64i32), U(_wstat64i32), UA("_stat",_stat64i32), UA("_wstat",_wstat64i32), U(_fstat64), U(_fstat64i32), UA("_fstat",_fstat64i32), U(_access), U(_waccess), U(_access_s), U(_waccess_s),
  U(_chmod), U(_wchmod), U(remove), U(_wremove), U(_unlink), U(_wunlink), U(rename), U(_wrename), U(_mkdir), U(_wmkdir), U(_rmdir), U(_wrmdir), U(_chdir), U(_wchdir),
  U(_getcwd), U(_wgetcwd), U(_getdcwd), U(_wgetdcwd), U(_fullpath), U(_wfullpath), U(_getdrives), U(_mktemp_s), U(_wmktemp_s), U(_mktemp),
  U(getenv), U(_wgetenv), U(getenv_s), U(_wgetenv_s), U(_dupenv_s), U(_wdupenv_s), U(_putenv), U(_putenv_s), U(_wputenv), U(_wputenv_s),
  U(_configure_wide_argv), U(_initialize_wide_environment), U(_get_initial_wide_environment), U(__p__wenviron), U(__p___wargv), U(__p___wargc), U(__wgetmainargs),
  U(_get_narrow_winmain_command_line), U(_get_wide_winmain_command_line), U(_get_pgmptr), U(_get_wpgmptr), U(__p__pgmptr), U(__p__wpgmptr),
  U(_initialize_onexit_table), U(_register_onexit_function), U(_execute_onexit_table), U(_register_thread_local_exe_atexit_callback), U(_seh_filter_dll),
  U(_set_new_handler), U(_query_new_handler), U(_callnewh), U(terminate), U(abort), U(_wassert), U(_assert), U(signal), U(raise), U(strerror_s), U(_wcserror_s), U(_wcserror),
  U(fegetround), U(fesetround), U(feclearexcept), U(fetestexcept), U(fegetenv), U(fesetenv), U(feholdexcept), U(_control87), U(_clearfp), U(_statusfp),
  U(_aligned_malloc), U(_aligned_free), U(_aligned_msize), U(_aligned_realloc), U(_aligned_offset_malloc), U(_recalloc), U(_expand),
  U(malloc), U(free), U(calloc), U(realloc), U(_msize), UA("_malloc_base",malloc), UA("_free_base",free), UA("_calloc_base",calloc), UA("_realloc_base",realloc), UA("_msize_base",_msize),
  U(_Getdays), U(_Getmonths), U(_W_Getdays), U(_W_Getmonths), U(_Gettnames), U(_W_Gettnames), U(_Strftime), U(_Wcsftime), U(wcsftime), U(strftime),
  U(__timezone), U(__daylight), U(__dstbias), U(__tzname), U(_tzset), U(_get_timezone), U(_get_daylight), U(_get_dstbias), U(_get_tzname),
  U(_gmtime64), U(_gmtime64_s), U(_gmtime32), U(_gmtime32_s), UA("gmtime",_gmtime64), U(_difftime64), U(_difftime32), U(_time32), U(_ftime64), U(_ftime64_s), UA("_ftime",_ftime64),
  U(clock), U(asctime), U(asctime_s), U(_wasctime), U(_ctime64), U(_wctime64), U(_strdate), U(_strtime),
};
const unsigned ucrt_nexports = sizeof ucrt_exports / sizeof *ucrt_exports;
