/* Mini libc for Willows: the C library functions are thin wrappers over msvcrt imports (or plain C). */
#include <stdarg.h>
#include <stddef.h>
#define IMP(ret, name, args) extern ret (*__imp_msvcrt__##name) args;
IMP(int,  vprintf, (const char*, va_list))
IMP(int,  vsnprintf, (char*, size_t, const char*, va_list))
IMP(int,  puts, (const char*))
IMP(int,  putchar, (int))
IMP(char*, fgets, (char*, int, void*))
IMP(void*, malloc, (size_t))
IMP(void*, calloc, (size_t, size_t))
IMP(void*, realloc, (void*, size_t))
IMP(void,  free, (void*))
IMP(void,  exit, (int))
IMP(void,  abort, (void))
IMP(int,   atexit, (void (*)(void)))
IMP(int,   __getmainargs, (int*, char***, char***, int, void*))
IMP(void,  __set_app_type, (int))
typedef struct _FILE FILE;
static char fobj[3]; FILE *stdin = (FILE*)&fobj[0], *stdout = (FILE*)&fobj[1], *stderr = (FILE*)&fobj[2];

int vprintf(const char *f, va_list a){ return __imp_msvcrt__vprintf(f, a); }
int vsnprintf(char *b, size_t n, const char *f, va_list a){ return __imp_msvcrt__vsnprintf(b, n, f, a); }
int printf(const char *f, ...){ va_list a; va_start(a, f); int r = vprintf(f, a); va_end(a); return r; }
int sprintf(char *b, const char *f, ...){ va_list a; va_start(a, f); int r = vsnprintf(b, 0x7FFFFFFF, f, a); va_end(a); return r; }
int snprintf(char *b, size_t n, const char *f, ...){ va_list a; va_start(a, f); int r = vsnprintf(b, n, f, a); va_end(a); return r; }
int fprintf(FILE *fp, const char *f, ...){ (void)fp; va_list a; va_start(a, f); int r = vprintf(f, a); va_end(a); return r; }
int puts(const char *s){ return __imp_msvcrt__puts(s); }
int putchar(int c){ return __imp_msvcrt__putchar(c); }
int fputs(const char *s, FILE *fp){ (void)fp; while(*s) putchar(*s++); return 1; }
int fputc(int c, FILE *fp){ (void)fp; return putchar(c); }
int fflush(FILE *fp){ (void)fp; return 0; }
char *fgets(char *b, int n, FILE *fp){ return __imp_msvcrt__fgets(b, n, fp); }

/* line-buffered getchar/scanf on top of fgets */
static char line[256]; static int lpos, llen;
int getchar(void){
  if(lpos >= llen){ if(!fgets(line, sizeof line, 0)) return -1; lpos = 0; llen = 0; while(line[llen]) llen++; }
  return (unsigned char)line[lpos++];
}
int scanf(const char *f, ...){
  va_list a; va_start(a, f); int n = 0, c;
  for(; *f; f++){
    if(*f == ' '){ continue; }
    if(*f != '%') continue;
    f++;
    do c = getchar(); while(c == ' ' || c == '\n' || c == '\t');
    if(c < 0) break;
    if(*f == 'c'){ *va_arg(a, char*) = c; n++; }
    else if(*f == 's'){ char *d = va_arg(a, char*); while(c > 0 && c != ' ' && c != '\n'){ *d++ = c; c = getchar(); } *d = 0; n++; }
    else if(*f == 'd' || *f == 'u'){
      int neg = 0, v = 0; if(c == '-'){ neg = 1; c = getchar(); }
      while(c >= '0' && c <= '9'){ v = v*10 + c - '0'; c = getchar(); }
      *va_arg(a, int*) = neg ? -v : v; n++;
    } else break;
  }
  va_end(a); return n;
}

void *malloc(size_t n){ return __imp_msvcrt__malloc(n); }
void *calloc(size_t n, size_t s){ return __imp_msvcrt__calloc(n, s); }
void *realloc(void *p, size_t n){ return __imp_msvcrt__realloc(p, n); }
void free(void *p){ __imp_msvcrt__free(p); }
void exit(int c){ __imp_msvcrt__exit(c); for(;;); }
void abort(void){ __imp_msvcrt__abort(); for(;;); }
int atexit(void (*f)(void)){ return __imp_msvcrt__atexit(f); }

size_t strlen(const char *s){ size_t n = 0; while(s[n]) n++; return n; }
int strcmp(const char *a, const char *b){ while(*a && *a == *b){ a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n){ for(; n; n--, a++, b++){ if(*a != *b) return (unsigned char)*a - (unsigned char)*b; if(!*a) return 0; } return 0; }
char *strcpy(char *d, const char *s){ char *r = d; while((*d++ = *s++)); return r; }
char *strncpy(char *d, const char *s, size_t n){ size_t i = 0; for(; i < n && s[i]; i++) d[i] = s[i]; for(; i < n; i++) d[i] = 0; return d; }
char *strcat(char *d, const char *s){ strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c){ for(;; s++){ if(*s == (char)c) return (char*)s; if(!*s) return 0; } }
void *memcpy(void *d, const void *s, size_t n);
size_t strlen(const char *s);
char *strdup(const char *s){ size_t n = strlen(s) + 1; char *d = malloc(n); if(d) memcpy(d, s, n); return d; }
char *strrchr(const char *s, int c){ const char *r = 0; for(;; s++){ if(*s == (char)c) r = s; if(!*s) return (char*)r; } }
char *strstr(const char *h, const char *n){ size_t l = strlen(n); for(; *h; h++) if(!strncmp(h, n, l)) return (char*)h; return l ? 0 : (char*)h; }
void *memcpy(void *d, const void *s, size_t n){ char *a = d; const char *b = s; while(n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n){ char *a = d; const char *b = s; if(a < b) while(n--) *a++ = *b++; else { a += n; b += n; while(n--) *--a = *--b; } return d; }
void *memset(void *d, int c, size_t n){ char *a = d; while(n--) *a++ = (char)c; return d; }
void *memchr(const void *s, int c, size_t n){ const unsigned char *p = s; for(; n; n--, p++) if(*p == (unsigned char)c) return (void*)p; return 0; }
int memcmp(const void *a, const void *b, size_t n){ const unsigned char *x = a, *y = b; for(; n; n--, x++, y++) if(*x != *y) return *x - *y; return 0; }
int atoi(const char *s){ int n = 0, neg = 0; while(*s == ' ') s++; if(*s == '-'){ neg = 1; s++; } while(*s >= '0' && *s <= '9') n = n*10 + *s++ - '0'; return neg ? -n : n; }
int abs(int x){ return x < 0 ? -x : x; }
static unsigned rng = 1;
void srand(unsigned s){ rng = s; }
int rand(void){ rng = rng * 1103515245 + 12345; return (rng >> 16) & 0x7FFF; }
int toupper(int c){ return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c){ return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int isdigit(int c){ return c >= '0' && c <= '9'; }
int isalpha(int c){ return (c|32) >= 'a' && (c|32) <= 'z'; }
int isalnum(int c){ return isdigit(c) || isalpha(c); }
int isspace(int c){ return c == ' ' || (c >= 9 && c <= 13); }

extern int main(int, char**, char**);
void __attribute__((stdcall)) mainCRTStartup(void){
  int argc; char **argv, **env; int si = 0;
  __imp_msvcrt____set_app_type(1);
  __imp_msvcrt____getmainargs(&argc, &argv, &env, 0, &si);
  exit(main(argc, argv, env));
}
