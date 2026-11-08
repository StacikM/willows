#include "kernel.h"
/* WinINet (InternetOpenUrl / InternetReadFile / HttpQueryInfo), URLMon (URLDownloadToFile) and the random-number APIs
   that programs with their own TLS library use. http:// and https:// are supported; redirects are followed. */
typedef u64 HANDLE; typedef i32 BOOL; typedef u32 DWORD;
void win32_set_last_error(u32 e);
#define MAXHC 8
struct hc { u8 used; int sock, tls, status, chunked, eof; i64 clen, chunk_left; u8 rb[2048]; u32 rbp, rbn; char loc[512], ctype[96], url[640]; };
static struct hc H[MAXHC];
static u32 ie_err;                                        /* ERROR_INTERNET_* */
static int WINAPI fmt(char *b, u32 n, const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(b,n,f,ap); __builtin_ms_va_end(ap); return r; }
#define HBASE 0x6100
struct url { int https; char host[128]; u16 port; char path[512]; };

static int startswith_i(const char *s, const char *p){ while(*p){ if((*s|32)!=(*p|32)) return 0; s++; p++; } return 1; }
static int url_parse(struct url *u, const char *s){
  memset(u,0,sizeof *u);
  if(startswith_i(s,"https://")){ u->https=1; s+=8; u->port=443; } else if(startswith_i(s,"http://")){ s+=7; u->port=80; } else return -1;
  int n=0; while(*s && *s!='/' && *s!=':' && n<127) u->host[n++]=*s++; u->host[n]=0; if(!n) return -1;
  if(*s==':'){ s++; u32 p=0; while(*s>='0'&&*s<='9') p=p*10+(*s++-'0'); if(!p||p>65535) return -1; u->port=p; }
  if(*s!='/'){ u->path[0]='/'; u->path[1]=0; if(*s) return -1; } else { int k=0; while(*s && k<511) u->path[k++]=*s++; u->path[k]=0; }
  return 0;
}
static void w2a(const u16 *w, char *out, int max){ int i=0; for(;w&&w[i]&&i<max-1;i++) out[i]=w[i]>127? '?' : (char)w[i]; out[i]=0; }

/* ---- raw I/O over plain TCP or TLS ---- */
static int net_read(struct hc *c, void *buf, u32 n){
  if(c->tls>=0) return tls_read(c->tls,buf,n);
  u32 end=ticks+20000;
  for(;;){ int r=tcp_read(c->sock,buf,n,1); if(r!=-3) return r<0? -1 : r; if((i32)(ticks-end)>=0) return -1; idle_wait(); }
}
static int net_write(struct hc *c, const void *d, u32 n){ return c->tls>=0? tls_write(c->tls,d,n) : tcp_write(c->sock,d,n); }
static int rb_fill(struct hc *c){ int r=net_read(c,c->rb,sizeof c->rb); if(r<=0) return r; c->rbp=0; c->rbn=r; return r; }
static int rb_getc(struct hc *c){ if(c->rbp>=c->rbn){ if(rb_fill(c)<=0) return -1; } return c->rb[c->rbp++]; }
static int rb_read(struct hc *c, u8 *buf, u32 n){
  if(c->rbp<c->rbn){ u32 k=c->rbn-c->rbp; if(k>n) k=n; memcpy(buf,c->rb+c->rbp,k); c->rbp+=k; return k; }
  return net_read(c,buf,n);
}
static int read_line(struct hc *c, char *out, int max){
  int n=0; for(;;){ int ch=rb_getc(c); if(ch<0) return -1; if(ch=='\n') break; if(ch!='\r' && n<max-1) out[n++]=ch; } out[n]=0; return n;
}
static void hc_close(struct hc *c){ if(!c->used) return; if(c->tls>=0) tls_close(c->tls); tcp_close(c->sock); c->used=0; }

/* ---- one HTTP request ---- */
static int parse_headers(struct hc *c){
  char line[600]; if(read_line(c,line,sizeof line)<0) return -1;
  const char *p=line; while(*p && *p!=' ') p++; while(*p==' ') p++;
  c->status=0; while(*p>='0'&&*p<='9') c->status=c->status*10+(*p++-'0');
  c->clen=-1; c->chunked=0; c->loc[0]=0; c->ctype[0]=0;
  for(;;){
    int n=read_line(c,line,sizeof line); if(n<0) return -1; if(n==0) break;
    if(startswith_i(line,"content-length:")){ const char *v=line+15; while(*v==' ') v++; i64 x=0; while(*v>='0'&&*v<='9') x=x*10+(*v++-'0'); c->clen=x; }
    else if(startswith_i(line,"transfer-encoding:")){ const char *v=line+18; for(;*v;v++) if(startswith_i(v,"chunked")){ c->chunked=1; break; } }
    else if(startswith_i(line,"content-type:")){ const char *v=line+13; while(*v==' ') v++; int k=0; while(*v && k<95) c->ctype[k++]=*v++; c->ctype[k]=0; }
    else if(startswith_i(line,"location:")){ const char *v=line+9; while(*v==' ') v++; int k=0; while(*v && k<511) c->loc[k++]=*v++; c->loc[k]=0; }
  }
  return 0;
}
static struct hc *open_url_ex(const char *url, int depth, const char *method, const char *hdrs, const u8 *body, u32 blen){
  struct url u; if(url_parse(&u,url)){ ie_err=12006; return NULL; }
  u32 ip; if(dns_resolve(u.host,&ip)){ ie_err=12007; return NULL; }
  int sock=tcp_connect(ip,u.port); if(sock<0){ ie_err=12029; return NULL; }
  struct hc *c=NULL; for(int i=0;i<MAXHC;i++) if(!H[i].used){ c=&H[i]; break; }
  if(!c){ tcp_close(sock); ie_err=12018; return NULL; }
  memset(c,0,sizeof *c); c->used=1; c->sock=sock; c->tls=-1; { int k=0; for(;url[k]&&k<639;k++) c->url[k]=url[k]; c->url[k]=0; }
  if(u.https){ c->tls=tls_open(u.host,sock); if(c->tls<0){ tcp_close(sock); c->used=0; ie_err=12157; return NULL; } }
  char hosthdr[160]; static char req[5120];
  if((u.https&&u.port==443)||(!u.https&&u.port==80)) fmt(hosthdr,sizeof hosthdr,"%s",u.host); else fmt(hosthdr,sizeof hosthdr,"%s:%u",u.host,(u32)u.port);
  int n=fmt(req,sizeof req,"%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Willows/0.3\r\nAccept: */*\r\n",method,u.path,hosthdr);
  if(hdrs){ u32 hl=strlen(hdrs); if(hl>4000) hl=4000; memcpy(req+n,hdrs,hl); n+=hl; if(hl<2||hdrs[hl-1]!='\n'){ req[n++]='\r'; req[n++]='\n'; } }
  if(body||blen) n+=fmt(req+n,sizeof req-n,"Content-Length: %u\r\n",blen);
  n+=fmt(req+n,sizeof req-n,"Connection: close\r\n\r\n");
  if(net_write(c,req,n)<0 || (blen && net_write(c,body,blen)<0)){
    con_quiet++; kprintf("https: %s\n",tls_last_error); con_quiet--; kprintf("HTTPS failed: %s\n",tls_last_error);
    hc_close(c); ie_err=u.https? 12157 : 12031; return NULL;
  }
  if(parse_headers(c)){ hc_close(c); ie_err=12152; return NULL; }
  if((c->status==301||c->status==302||c->status==303||c->status==307||c->status==308) && c->loc[0] && depth<6){
    char next[640];
    if(startswith_i(c->loc,"http")) { int k=0; for(;c->loc[k]&&k<639;k++) next[k]=c->loc[k]; next[k]=0; }
    else fmt(next,sizeof next,"%s://%s:%u%s%s",u.https? "https" : "http",u.host,(u32)u.port,c->loc[0]=='/'? "" : "/",c->loc);
    hc_close(c);
    int keep=(c->status==307||c->status==308);                                  /* 301/302/303 turn a POST into a GET */
    return keep? open_url_ex(next,depth+1,method,hdrs,body,blen) : open_url_ex(next,depth+1,"GET",0,0,0);
  }
  return c;
}
static struct hc *open_url(const char *url, int depth){ return open_url_ex(url,depth,"GET",0,0,0); }

/* ---- response body (identity or chunked) ---- */
static int hc_read(struct hc *c, u8 *buf, u32 n){
  if(c->eof||!n) return 0;
  if(c->chunked){
    if(c->chunk_left==0){
      char line[40]; int l=read_line(c,line,sizeof line); if(l<0){ c->eof=1; return 0; }
      if(l==0 && read_line(c,line,sizeof line)<0){ c->eof=1; return 0; }          /* CRLF that ended the previous chunk */
      i64 sz=0; for(const char *p=line; *p; p++){ int d= *p>='0'&&*p<='9'? *p-'0' : (*p|32)>='a'&&(*p|32)<='f'? (*p|32)-'a'+10 : -1; if(d<0) break; sz=sz*16+d; }
      if(sz==0){ c->eof=1; return 0; }
      c->chunk_left=sz;
    }
    u32 take=(i64)n>c->chunk_left? (u32)c->chunk_left : n; int r=rb_read(c,buf,take);
    if(r<=0){ c->eof=1; return 0; } c->chunk_left-=r; return r;
  }
  if(c->clen>=0){
    if(c->clen==0){ c->eof=1; return 0; }
    u32 take=(i64)n>c->clen? (u32)c->clen : n; int r=rb_read(c,buf,take);
    if(r<=0){ c->eof=1; return 0; } c->clen-=r; return r;
  }
  int r=rb_read(c,buf,n); if(r<=0){ c->eof=1; return 0; } return r;
}
static struct hc *hget(HANDLE h){ if(h<HBASE||((h-HBASE)&3)) return NULL; u64 i=(h-HBASE)>>2; return (i<MAXHC&&H[i].used)? &H[i] : NULL; }

/* ---- WinINet ---- */
static HANDLE WINAPI i_InternetOpenA(const char *a, DWORD t, const char *p, const char *b, DWORD f){ (void)a;(void)t;(void)p;(void)b;(void)f; return 0x6001; }
static HANDLE WINAPI i_InternetOpenW(const u16 *a, DWORD t, const u16 *p, const u16 *b, DWORD f){ (void)a;(void)t;(void)p;(void)b;(void)f; return 0x6001; }
static HANDLE WINAPI i_InternetOpenUrlA(HANDLE s, const char *url, const char *hdr, DWORD hl, DWORD flags, u64 ctx){
  (void)s;(void)hdr;(void)hl;(void)flags;(void)ctx;
  struct hc *c=open_url(url,0); if(!c){ win32_set_last_error(ie_err); return 0; } return HBASE+(c-H)*4;
}
/* Willows extension: any method with extra headers ("Name: value\r\n...") and a request body */
static HANDLE WINAPI i_WillowsHttpRequest(HANDLE s, const char *url, const char *method, const char *hdrs, const void *body, DWORD blen){
  (void)s; struct hc *c=open_url_ex(url,0,method&&*method? method : "GET",hdrs,body,blen); if(!c){ win32_set_last_error(ie_err); return 0; } return HBASE+(c-H)*4;
}
static HANDLE WINAPI i_InternetOpenUrlW(HANDLE s, const u16 *url, const u16 *hdr, DWORD hl, DWORD flags, u64 ctx){ char u[600]; w2a(url,u,sizeof u); return i_InternetOpenUrlA(s,u,0,hl,flags,ctx); (void)hdr; }
static BOOL WINAPI i_InternetReadFile(HANDLE h, void *buf, DWORD n, DWORD *got){
  struct hc *c=hget(h); if(!c){ win32_set_last_error(6); return 0; }
  int r=hc_read(c,buf,n); if(got) *got=r<0? 0 : r; return r>=0;
}
static BOOL WINAPI i_InternetQueryDataAvailable(HANDLE h, DWORD *avail, DWORD f, u64 ctx){ (void)f;(void)ctx; struct hc *c=hget(h); if(!c) return 0; if(avail) *avail= c->rbn>c->rbp? c->rbn-c->rbp : (c->eof? 0 : 1); return 1; }
static BOOL WINAPI i_InternetCloseHandle(HANDLE h){ struct hc *c=hget(h); if(c) hc_close(c); return 1; }
static BOOL WINAPI i_HttpQueryInfoA(HANDLE h, DWORD level, void *buf, DWORD *len, DWORD *idx){
  (void)idx; struct hc *c=hget(h); if(!c||!len) return 0;
  if((level&0xFFFF)==1){ u32 n=strlen(c->ctype); if(*len<=n){ *len=n+1; return 0; } memcpy(buf,c->ctype,n+1); *len=n; return 1; }   /* HTTP_QUERY_CONTENT_TYPE */
  i64 v= (level&0xFFFF)==19? c->status : (level&0xFFFF)==5? c->clen : -2; if(v==-2){ win32_set_last_error(12150); return 0; }
  if(level&0x20000000){ if(*len<4){ *len=4; return 0; } *(DWORD*)buf=(DWORD)v; *len=4; return 1; }      /* HTTP_QUERY_FLAG_NUMBER */
  char t[24]; int n=fmt(t,sizeof t,"%d",(int)v); if(*len<=(DWORD)n){ *len=n+1; return 0; } memcpy(buf,t,n+1); *len=n; return 1;
}
static BOOL WINAPI i_InternetQueryOptionA(HANDLE h, DWORD opt, void *buf, DWORD *len){   /* INTERNET_OPTION_URL (34): the final URL after redirects */
  struct hc *c=hget(h); if(!c||!len||opt!=34) return 0; u32 n=strlen(c->url); if(*len<=n){ *len=n+1; return 0; } memcpy(buf,c->url,n+1); *len=n; return 1;
}
static BOOL WINAPI i_InternetSetOptionA(HANDLE h, DWORD o, void *b, DWORD l){ (void)h;(void)o;(void)b;(void)l; return 1; }
static BOOL WINAPI i_InternetGetConnectedState(DWORD *flags, DWORD r){ (void)r; if(flags) *flags=0x12; return net_up(); }

/* ---- URLMon: download straight into the RAM filesystem (and so onto the boot disk) ---- */
static DWORD download(const char *url, const char *file){
  struct hc *c=open_url(url,0); if(!c) return 0x80004005;
  struct ramfile *f=fs_open_write(file); if(!f){ hc_close(c); return 0x80004005; }
  f->size=0; u8 buf[2048]; u64 total=0; int r;
  while((r=hc_read(c,buf,sizeof buf))>0){ if(total+r>3000000 || fs_append(f,buf,r)){ hc_close(c); return 0x80004005; } total+=r; }
  hc_close(c); fs_commit(f); return c->status>=200&&c->status<300? 0 : 0x80004005;
}
static DWORD WINAPI u_URLDownloadToFileA(void *caller, const char *url, const char *file, DWORD res, void *cb){ (void)caller;(void)res;(void)cb; return download(url,file); }
static DWORD WINAPI u_URLDownloadToFileW(void *caller, const u16 *url, const u16 *file, DWORD res, void *cb){ (void)caller;(void)res;(void)cb; char u[640], f[128]; w2a(url,u,sizeof u); w2a(file,f,sizeof f); return download(u,f); }

/* ---- random numbers (BCrypt, CryptoAPI, RtlGenRandom) for programs that bring their own TLS ---- */
static i32 WINAPI b_BCryptGenRandom(void *alg, u8 *buf, DWORD n, DWORD flags){ (void)alg;(void)flags; random_bytes(buf,n); return 0; }
static BOOL WINAPI a_CryptAcquireContext(u64 *prov, const void *c, const void *p, DWORD t, DWORD f){ (void)c;(void)p;(void)t;(void)f; if(prov) *prov=0x77; return 1; }
static BOOL WINAPI a_CryptGenRandom(u64 prov, DWORD n, u8 *buf){ (void)prov; random_bytes(buf,n); return 1; }
static BOOL WINAPI a_CryptReleaseContext(u64 prov, DWORD f){ (void)prov;(void)f; return 1; }
static u8 WINAPI a_SystemFunction036(void *buf, DWORD n){ random_bytes(buf,n); return 1; }

#define WI(n) {"wininet", #n, i_##n}
const struct export inet_exports[]={
  WI(InternetOpenA), WI(InternetOpenW), WI(InternetOpenUrlA), WI(InternetOpenUrlW), WI(WillowsHttpRequest), WI(InternetReadFile), WI(InternetQueryDataAvailable),
  WI(InternetCloseHandle), WI(HttpQueryInfoA), WI(InternetQueryOptionA), WI(InternetSetOptionA), WI(InternetGetConnectedState),
  {"urlmon","URLDownloadToFileA",u_URLDownloadToFileA}, {"urlmon","URLDownloadToFileW",u_URLDownloadToFileW},
  {"bcrypt","BCryptGenRandom",b_BCryptGenRandom},
  {"advapi32","CryptAcquireContextA",a_CryptAcquireContext}, {"advapi32","CryptAcquireContextW",a_CryptAcquireContext},
  {"advapi32","CryptGenRandom",a_CryptGenRandom}, {"advapi32","CryptReleaseContext",a_CryptReleaseContext},
  {"advapi32","SystemFunction036",a_SystemFunction036},
};
const unsigned inet_nexports=sizeof inet_exports/sizeof *inet_exports;
