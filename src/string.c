#include "kernel.h"
void *memcpy(void *d, const void *s, unsigned long n){ void *r=d; __asm__ volatile("rep movsb":"+D"(d),"+S"(s),"+c"(n)::"memory"); return r; }   /* returns the destination (rep movsb moves d to its end) */
void *memmove(void *d, const void *s, unsigned long n){ u8 *a=d; const u8 *b=s;
  if(a<b) while(n--) *a++=*b++; else { a+=n; b+=n; while(n--) *--a=*--b; } return d; }
void *memset(void *d, int c, unsigned long n){ void *r=d; __asm__ volatile("rep stosb":"+D"(d),"+c"(n):"a"(c):"memory"); return r; }
unsigned long strlen(const char *s){ unsigned long n=0; while(s[n]) n++; return n; }
int strcmp(const char *a, const char *b){ while(*a && *a==*b){a++;b++;} return (u8)*a-(u8)*b; }
static char lc(char c){ return (c>='A'&&c<='Z')? c+32 : c; }
int stricmp(const char *a, const char *b){ while(*a && lc(*a)==lc(*b)){a++;b++;} return lc(*a)-lc(*b); }
int memcmp(const void *a, const void *b, unsigned long n){ const u8 *x=a,*y=b; for(;n;n--,x++,y++) if(*x!=*y) return *x-*y; return 0; }
