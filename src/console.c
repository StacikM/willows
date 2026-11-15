#include "kernel.h"
static volatile u16 *const vga = (u16*)0xB8000;
static int cx, cy, cols=80, rows=25; static const u8 attr = 0x07;
#define FG 0xC0C0C0
#define BG 0x000000

char con_buf[CON_ROWS][CON_COLS]; int con_cx, con_cy, con_on, con_quiet;
void console_desktop(int on){ con_on=on; memset(con_buf,' ',sizeof con_buf); con_cx=con_cy=0; }
static void con_put(char c){
  static int esc, par[4], np, have;                     /* ANSI escapes: cursor moves and erase are honoured, colours and the rest are swallowed */
  static char priv, osc;
  if(esc==0 && c==0x1B){ esc=1; return; }
  if(esc==1){ esc = (c=='[') ? 2 : (c==']' || c=='(') ? 3 : 0; osc=(c==']'); np=0; have=0; priv=0; par[0]=par[1]=0; return; }
  if(esc==3){ if(osc? (c==7 || c=='\\') : 1) esc=0; return; }
  if(esc==2){
    if(c=='?'){ priv=1; return; }
    if(c>='0' && c<='9'){ par[np]=par[np]*10+(c-'0'); have=1; return; }
    if(c==';'){ if(np<3) np++; par[np]=0; return; }
    esc=0; if(priv) return;
    int n=par[0]? par[0] : 1;
    switch(c){
      case 'H': case 'f': con_cy=(par[0]? par[0]:1)-1; con_cx=(np>=1 && par[1]? par[1]:1)-1; if(con_cy>=CON_ROWS) con_cy=CON_ROWS-1; if(con_cx>=CON_COLS) con_cx=CON_COLS-1; break;
      case 'A': con_cy-=n; if(con_cy<0) con_cy=0; break;
      case 'B': con_cy+=n; if(con_cy>=CON_ROWS) con_cy=CON_ROWS-1; break;
      case 'C': con_cx+=n; if(con_cx>=CON_COLS) con_cx=CON_COLS-1; break;
      case 'D': con_cx-=n; if(con_cx<0) con_cx=0; break;
      case 'J': if(par[0]==2 || par[0]==3){ memset(con_buf,' ',sizeof con_buf); } else if(par[0]==0){ memset(con_buf[con_cy]+con_cx,' ',CON_COLS-con_cx); for(int r=con_cy+1;r<CON_ROWS;r++) memset(con_buf[r],' ',CON_COLS); } break;
      case 'K': if(par[0]==0) memset(con_buf[con_cy]+con_cx,' ',CON_COLS-con_cx); else if(par[0]==1) memset(con_buf[con_cy],' ',con_cx+1); else memset(con_buf[con_cy],' ',CON_COLS); break;
      default: break;
    }
    (void)have; wm_console_dirty(); return;
  }
  if(c=='\n'){ con_cx=0; con_cy++; } else if(c=='\r') con_cx=0; else if(c=='\b'){ if(con_cx) con_cx--; }
  else if(c=='\t'){ con_cx=(con_cx+8)&~7; } else if((u8)c>=32){ con_buf[con_cy][con_cx]=c; con_cx++; }
  if(con_cx>=CON_COLS){ con_cx=0; con_cy++; }
  if(con_cy>=CON_ROWS){ memmove(con_buf,con_buf[1],(CON_ROWS-1)*CON_COLS); memset(con_buf[CON_ROWS-1],' ',CON_COLS); con_cy=CON_ROWS-1; }
  wm_console_dirty();
}
static void scroll(void){
  if(gfx_ok){ gfx_scroll_up(16,BG); cy=rows-1; return; }
  for(int i=0;i<80*24;i++) vga[i]=vga[i+80];
  for(int i=80*24;i<80*25;i++) vga[i]=(attr<<8)|' ';
  cy=24;
}
void console_init(void){
  outb(0x3F9,0); outb(0x3FB,0x80); outb(0x3F8,1); outb(0x3F9,0); outb(0x3FB,3); outb(0x3FA,0xC7); outb(0x3FC,0x0B);
  if(gfx_ok){ cols=gfx_w/8; rows=gfx_h/16; gfx_rect(0,0,gfx_w,gfx_h,BG); }
  else for(int i=0;i<80*25;i++) vga[i]=(attr<<8)|' ';
  cx=cy=0;
}
#define CAPSZ 32768
void kputc(char c){
  if(cur && cur->out_h){ pipe_write(cur->out_h,&c,1,0); return; }                                                                         /* stdout redirected into a pipe by its parent */
  if(cur && cur->cap_on && cur->cap){ u32 w=cur->cap_w; if(w-cur->cap_r<CAPSZ){ cur->cap[w%CAPSZ]=c; cur->cap_w=w+1; } return; }          /* a program started from the terminal: its output goes there */
  while(!(inb(0x3FD)&0x20)); outb(0x3F8,c);          /* serial mirror */
  if(con_on){ con_put(c); return; }                    /* desktop mode: text goes to the Console window */
  if(c=='\n'){ cx=0; cy++; }
  else if(c=='\r') cx=0;
  else if(c=='\b'){ if(cx) cx--; }
  else if(c=='\t'){ cx=(cx+8)&~7; }
  else if(gfx_ok){ gfx_char(cx*8,cy*16,c,FG,BG,1); cx++; }
  else { vga[cy*80+cx]=(attr<<8)|(u8)c; cx++; }
  if(cx>=cols){ cx=0; cy++; }
  if(cy>=rows) scroll();
}
void kputs(const char *s){ while(*s) kputc(*s++); }

typedef void (*putfn)(char, void*);
static void pad_out(putfn put, void *cx, const char *s, int n, int width, int left, char pad){
  if(!left) while(width>n){ put(pad,cx); width--; }
  for(int i=0;i<n;i++) put(s[i],cx);
  if(left) while(width>n){ put(' ',cx); width--; }
}
/* Win64 varargs: every argument occupies one 8-byte slot, ap walks them. */
static void WINAPI vfmt(putfn put, void *cx, const char *f, margs ap0){
  char *ap=(char*)ap0;
  #define NEXT() (*(u64*)((ap+=8)-8))
  for(;*f;f++){
    if(*f!='%'){ put(*f,cx); continue; }
    f++; char pad=' '; int w=0, left=0, wide=0;
    if(*f=='-'){ left=1; f++; }
    if(*f=='0'){ pad='0'; f++; }
    while(*f>='0'&&*f<='9') w=w*10+(*f++-'0');
    for(;;){                                   /* length modifiers: ll, I64, z, j, t are 64-bit; l, h are 32-bit */
      if(*f=='l'&&f[1]=='l'){ wide=1; f+=2; }
      else if(*f=='l'||*f=='h'){ f++; }
      else if(*f=='I'&&f[1]=='6'&&f[2]=='4'){ wide=1; f+=3; }
      else if(*f=='z'||*f=='j'||*f=='t'){ wide=1; f++; }
      else break;
    }
    char tmp[24]; int n=0; u64 v; unsigned base=10; int neg=0, upper=0;
    switch(*f){
      case 'd': case 'i': { i64 x= wide? (i64)NEXT() : (i64)(i32)NEXT(); neg=x<0; v=neg?0-(u64)x:(u64)x; goto number; }
      case 'u': v= wide? NEXT() : (u32)NEXT(); goto number;
      case 'x': v= wide? NEXT() : (u32)NEXT(); base=16; goto number;
      case 'X': v= wide? NEXT() : (u32)NEXT(); base=16; upper=1; goto number;
      case 'p': put('0',cx); put('x',cx); v=NEXT(); base=16; goto number;
      number: {
        do { tmp[n++]=(upper? "0123456789ABCDEF" : "0123456789abcdef")[v%base]; v/=base; } while(v);
        if(neg){ if(pad=='0'){ put('-',cx); w--; } else tmp[n++]='-'; }
        char rev[24]; for(int k=0;k<n;k++) rev[k]=tmp[n-1-k];
        pad_out(put,cx,rev,n,w,left,pad); break; }
      case 'c': { char c=(char)NEXT(); pad_out(put,cx,&c,1,w,left,' '); break; }
      case 's': { const char *t=(const char*)NEXT(); if(!t) t="(null)"; pad_out(put,cx,t,strlen(t),w,left,' '); break; }
      case '%': put('%',cx); break;
      case 0: return;
      default: put('%',cx); put(*f,cx);
    }
  }
  #undef NEXT
}
static void put_con(char c, void *cx){ (void)cx; kputc(c); }
static void put_ser(char c, void *cx){ (void)cx; while(!(inb(0x3FD)&0x20)); outb(0x3F8,c); }
void WINAPI kdbg(const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); vfmt(put_ser,0,f,ap); __builtin_ms_va_end(ap); }
void WINAPI kvprintf(const char *f, margs ap){ vfmt(put_con,0,f,ap); }
void WINAPI kprintf(const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); vfmt(put_con,0,f,ap); __builtin_ms_va_end(ap); }

struct sbuf { char *p; u32 left; int total; };
static void put_buf(char c, void *cx){ struct sbuf *b=cx; b->total++; if(b->left>1){ *b->p++=c; b->left--; } }
int WINAPI kvsnprintf(char *buf, u32 n, const char *f, margs ap){
  struct sbuf b={buf,n,0}; vfmt(put_buf,&b,f,ap); if(n) *b.p=0; return b.total;
}
int WINAPI ksnprintf(char *b, u32 n, const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(b,n,f,ap); __builtin_ms_va_end(ap); return r; }
