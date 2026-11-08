#include <bearssl.h>
#include "kernel.h"
#include "trust_anchors.h"       /* Mozilla root certificates (tools/mkcerts.sh regenerates this) */
/* TLS 1.2 client on top of our TCP sockets, using BearSSL. Blocking and cooperative: waiting yields to other processes. */

/* ---- entropy: RDRAND if the CPU has it, mixed with the cycle counter and timer jitter ---- */
static u64 rdtsc(void){ u32 lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); return ((u64)hi<<32)|lo; }
static int has_rdrand(void){ u32 a=1,b,c=0,d; __asm__ volatile("cpuid":"+a"(a),"=b"(b),"+c"(c),"=d"(d)); return (c>>30)&1; }
static int rdrand64(u64 *v){ u8 ok; for(int i=0;i<10;i++){ u64 x; __asm__ volatile("rdrand %0; setc %1":"=r"(x),"=qm"(ok)::"cc"); if(ok){ *v=x; return 1; } } return 0; }
static u64 mix(u64 h, u64 v){ h^=v+0x9E3779B97F4A7C15ull+(h<<6)+(h>>2); h*=0xFF51AFD7ED558CCDull; return h^(h>>33); }
static void entropy_fill(u8 *out, u32 n){
  static int warned; int hw=has_rdrand();
  if(!hw && !warned){ warned=1; con_quiet++; kputs("tls: this CPU has no RDRAND; using timer jitter for randomness (weaker)\n"); con_quiet--; }
  u64 h=mix(rtc_unix(),rdtsc());
  for(u32 i=0;i<n;i+=8){
    u64 v=0;
    if(hw) rdrand64(&v);
    if(!hw){ for(int k=0;k<64;k++){ u64 a=rdtsc(); __asm__ volatile("hlt"); h=mix(h,rdtsc()-a); } }   /* jitter between timer interrupts */
    h=mix(h,v^rdtsc()^ticks^((u64)mouse_x<<20)^((u64)mouse_y<<40));
    u64 w=hw? (v^h) : h;
    for(u32 j=0;j<8 && i+j<n;j++) out[i+j]=(u8)(w>>(8*j));
  }
}
static int seeder(const br_prng_class **ctx){ u8 b[48]; entropy_fill(b,sizeof b); (*ctx)->update(ctx,b,sizeof b); return 1; }
br_prng_seeder br_prng_seeder_system(const char **name){ if(name) *name="willows"; return seeder; }

static br_hmac_drbg_context rng; static int rng_ok;
void random_bytes(void *buf, u32 n){
  u8 s[48]; entropy_fill(s,sizeof s);
  if(!rng_ok){ br_hmac_drbg_init(&rng,&br_sha256_vtable,s,sizeof s); rng_ok=1; } else br_hmac_drbg_update(&rng,s,sizeof s);
  br_hmac_drbg_generate(&rng,buf,n);
}

/* ---- connections ---- */
#define MAXTLS 3
struct tconn { u8 used; int sock; br_ssl_client_context sc; br_x509_minimal_context xc; br_sslio_context ioc; unsigned char iobuf[BR_SSL_BUFSIZE_BIDI]; };
static struct tconn P[MAXTLS];
const char *tls_last_error="";

static int lowread(void *ctx, unsigned char *buf, size_t len){
  struct tconn *c=ctx; u32 end=ticks+20000;
  for(;;){
    int r=tcp_read(c->sock,buf,len,1);
    if(r>0) return r;
    if(r==0||r==-2) return -1;                            /* closed or reset */
    if((i32)(ticks-end)>=0) return -1;                    /* 20 s without data */
    idle_wait();
  }
}
static int lowwrite(void *ctx, const unsigned char *buf, size_t len){
  struct tconn *c=ctx; int r=tcp_write(c->sock,buf,len); return r<0? -1 : r;
}
static const char *errtxt(int e){
  static char b[40];
  switch(e){
    case BR_ERR_X509_NOT_TRUSTED: return "certificate not trusted (unknown or untrusted issuer)";
    case BR_ERR_X509_EXPIRED: return "certificate expired or not yet valid (is the clock right?)";
    case BR_ERR_X509_BAD_SERVER_NAME: return "certificate is for a different host name";
    case BR_ERR_X509_DN_MISMATCH: return "certificate chain issuer mismatch";
    case BR_ERR_X509_TIME_UNKNOWN: return "current time unknown";
    case BR_ERR_X509_BAD_SIGNATURE: return "bad certificate signature";
    case BR_ERR_IO: return "connection lost during TLS";
    case BR_ERR_NO_RANDOM: return "no random numbers";
    case BR_ERR_UNSUPPORTED_VERSION: return "server does not support TLS 1.2";
    case BR_ERR_BAD_CIPHER_SUITE: return "no common cipher suite";
  }
  const char *p="TLS error "; int n=0; while(*p) b[n++]=*p++; char t[8]; int k=0; int v=e; do{ t[k++]='0'+v%10; v/=10; }while(v); while(k) b[n++]=t[--k]; b[n]=0; return b;
}
int tls_open(const char *host, int sock){
  int i=0; for(;i<MAXTLS;i++) if(!P[i].used) break;
  if(i==MAXTLS){ tls_last_error="too many TLS connections"; return -1; }
  struct tconn *c=&P[i]; memset(c,0,sizeof *c); c->used=1; c->sock=sock;
  br_ssl_client_init_full(&c->sc,&c->xc,TAs,TAs_NUM);
  u64 t=time_now(); br_x509_minimal_set_time(&c->xc,(uint32_t)(t/86400+719528),(uint32_t)(t%86400));
  br_ssl_engine_set_buffer(&c->sc.eng,c->iobuf,sizeof c->iobuf,1);
  br_ssl_client_reset(&c->sc,host,0);                     /* sets SNI and the name the certificate must match */
  br_sslio_init(&c->ioc,&c->sc.eng,lowread,c,lowwrite,c);
  return i;
}
int tls_write(int h, const void *d, u32 n){
  if(h<0||h>=MAXTLS||!P[h].used) return -1; struct tconn *c=&P[h];
  if(br_sslio_write_all(&c->ioc,d,n)<0 || br_sslio_flush(&c->ioc)<0){ tls_last_error=errtxt(br_ssl_engine_last_error(&c->sc.eng)); return -1; }
  return (int)n;
}
int tls_read(int h, void *d, u32 n){
  if(h<0||h>=MAXTLS||!P[h].used) return -1; struct tconn *c=&P[h];
  int r=br_sslio_read(&c->ioc,d,n);
  if(r>0) return r;
  int e=br_ssl_engine_last_error(&c->sc.eng);
  if(e==BR_ERR_OK||e==BR_ERR_IO) return 0;                /* clean close, or the server just dropped the TCP connection */
  tls_last_error=errtxt(e); return -1;
}
void tls_close(int h){
  if(h<0||h>=MAXTLS||!P[h].used) return; struct tconn *c=&P[h];
  br_ssl_engine_close(&c->sc.eng); br_sslio_flush(&c->ioc); c->used=0;
}
