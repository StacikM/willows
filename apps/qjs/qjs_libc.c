/* The few libc functions QuickJS needs that the Willows shim (apps/shim) does not provide. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#define WINAPI __attribute__((ms_abi))
extern void (WINAPI *__imp_kernel32__GetSystemTimeAsFileTime)(unsigned long long *);
extern void *(WINAPI *__imp_kernel32__GetProcessHeap)(void);
extern unsigned long long (WINAPI *__imp_kernel32__HeapSize)(void *, unsigned, const void *);
extern unsigned long long (WINAPI *__imp_kernel32__GetTickCount64)(void);
int errno;

size_t malloc_usable_size(void *p){ unsigned long long n = __imp_kernel32__HeapSize(__imp_kernel32__GetProcessHeap(), 0, p); return n == (unsigned long long)-1 ? 0 : (size_t)n; }
ssize_t readlink(const char *p, char *b, size_t n){ (void)p; (void)b; (void)n; return -1; }
long labs(long x){ return x < 0 ? -x : x; }
int vfprintf(FILE *f, const char *fmt, va_list ap){ (void)f; return vprintf(fmt, ap); }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f){ (void)f; const unsigned char *c = p; for(size_t i = 0; i < sz * n; i++) putchar(c[i]); return n; }

/* ---- time: Windows FILETIME is 100 ns ticks since 1601 ---- */
static unsigned long long now_100ns(void){ unsigned long long ft; __imp_kernel32__GetSystemTimeAsFileTime(&ft); return ft; }
int gettimeofday(struct timeval *tv, struct timezone *tz){
  unsigned long long t = now_100ns() - 116444736000000000ull; if(tv){ tv->tv_sec = (long long)(t / 10000000ull); tv->tv_usec = (long long)((t % 10000000ull) / 10); }
  if(tz){ tz->tz_minuteswest = 0; tz->tz_dsttime = 0; } return 0;
}
time_t time(time_t *out){ struct timeval tv; gettimeofday(&tv, 0); if(out) *out = tv.tv_sec; return tv.tv_sec; }
int clock_gettime(int clk, struct timespec *ts){
  if(clk == CLOCK_MONOTONIC){ unsigned long long ms = __imp_kernel32__GetTickCount64(); ts->tv_sec = (time_t)(ms / 1000); ts->tv_nsec = (long)(ms % 1000) * 1000000; return 0; }
  struct timeval tv; gettimeofday(&tv, 0); ts->tv_sec = tv.tv_sec; ts->tv_nsec = (long)tv.tv_usec * 1000; return 0;
}
struct tm *gmtime_r(const time_t *tp, struct tm *tm){                              /* proleptic Gregorian, UTC */
  long long t = *tp, days = t / 86400, rem = t % 86400; if(rem < 0){ rem += 86400; days--; }
  tm->tm_hour = (int)(rem / 3600); tm->tm_min = (int)(rem % 3600 / 60); tm->tm_sec = (int)(rem % 60);
  tm->tm_wday = (int)((days + 4) % 7); if(tm->tm_wday < 0) tm->tm_wday += 7;
  long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
  long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9); if(m <= 2) y++;
  tm->tm_year = (int)(y - 1900); tm->tm_mon = m - 1; tm->tm_mday = d;
  static const int cum[] = {0,31,59,90,120,151,181,212,243,273,304,334}; int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  tm->tm_yday = cum[m - 1] + d - 1 + (leap && m > 2); tm->tm_isdst = 0; tm->tm_gmtoff = 0; tm->tm_zone = "UTC"; return tm;
}
struct tm *localtime_r(const time_t *tp, struct tm *tm){ return gmtime_r(tp, tm); }       /* Willows keeps UTC */

/* ---- strtod: decimal and exponent parsing (QuickJS uses its own js_atod for JavaScript numbers) ---- */
double strtod(const char *s, char **end){
  const char *p = s; while(*p == ' ' || (*p >= 9 && *p <= 13)) p++;
  int neg = 0; if(*p == '-'){ neg = 1; p++; } else if(*p == '+') p++;
  if((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'f'){ if(end) *end = (char *)p + 3; return neg ? -INFINITY : INFINITY; }
  if((p[0] | 32) == 'n' && (p[1] | 32) == 'a' && (p[2] | 32) == 'n'){ if(end) *end = (char *)p + 3; return NAN; }
  double v = 0; int any = 0, e10 = 0;
  while(*p >= '0' && *p <= '9'){ v = v * 10 + (*p - '0'); p++; any = 1; }
  if(*p == '.'){ p++; while(*p >= '0' && *p <= '9'){ v = v * 10 + (*p - '0'); e10--; p++; any = 1; } }
  if(!any){ if(end) *end = (char *)s; return 0; }
  if((*p | 32) == 'e'){ const char *q = p + 1; int en = 0, ex = 0; if(*q == '-'){ en = 1; q++; } else if(*q == '+') q++; if(*q >= '0' && *q <= '9'){ while(*q >= '0' && *q <= '9'){ if(ex < 10000) ex = ex * 10 + (*q - '0'); q++; } e10 += en ? -ex : ex; p = q; } }
  if(e10) v *= pow(10.0, e10);
  if(end) *end = (char *)p; return neg ? -v : v;
}
long strtol(const char *s, char **end, int base){ long v = 0; int neg = 0; while(*s == ' ') s++; if(*s == '-'){ neg = 1; s++; } else if(*s == '+') s++;
  if((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x'){ s += 2; base = 16; } else if(base == 0) base = (s[0] == '0') ? 8 : 10;
  for(;; s++){ int d = *s >= '0' && *s <= '9' ? *s - '0' : (*s | 32) >= 'a' && (*s | 32) <= 'z' ? (*s | 32) - 'a' + 10 : 99; if(d >= base) break; v = v * base + d; }
  if(end) *end = (char *)s; return neg ? -v : v; }
