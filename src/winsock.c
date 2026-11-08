#include "kernel.h"
/* Winsock 2 (ws2_32) on top of net.c: blocking and non-blocking TCP/UDP sockets, name resolution, select, plus IcmpSendEcho. */
typedef u64 SOCKET; typedef i32 BOOL;
#define MAXWS 32
#define SHANDLE(i) (0x1000ull+(u64)(i)*4)
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (~0ull)
enum { WSAEWOULDBLOCK=10035, WSAENOTSOCK=10038, WSAEINVAL=10022, WSAENETDOWN=10050, WSAENETUNREACH=10051, WSAECONNRESET=10054,
       WSAENOTCONN=10057, WSAETIMEDOUT=10060, WSAECONNREFUSED=10061, WSANOTINITIALISED=10093, WSAHOST_NOT_FOUND=11001, WSAEAFNOSUPPORT=10047,
       WSAEOPNOTSUPP=10045, WSAEMFILE=10024 };
struct wsock { u8 used, type, nb, listening; int t, u; u16 lport, peer_port; u32 peer_ip; u64 ahwnd; u32 amsg, aevents; u8 rd_armed, close_sent, conn_sent; };
static struct wsock S[MAXWS]; static int ws_err, ws_started;
static inline u16 hs(u16 x){ return (u16)((x>>8)|(x<<8)); }
static inline u32 hl(u32 x){ return __builtin_bswap32(x); }
static struct wsock *get(SOCKET h){
  if(h<0x1000 || ((h-0x1000)&3)) return 0; u64 i=(h-0x1000)>>2; return (i<MAXWS && S[i].used)? &S[i] : 0;
}
static int fail(int e){ ws_err=e; return SOCKET_ERROR; }
static int tcp_err(int r){ return r==-2? WSAECONNRESET : r==-3? WSAETIMEDOUT : WSAENOTCONN; }

static int WINAPI w_WSAStartup(u16 ver, u8 *d){
  (void)ver; memset(d,0,408); *(u16*)d=0x0202; *(u16*)(d+2)=0x0202; *(u16*)(d+4)=32767; *(u16*)(d+6)=65467;
  const char *a="Willows Winsock 2.2", *b="Running"; memcpy(d+16,a,strlen(a)+1); memcpy(d+273,b,strlen(b)+1);
  ws_started=1; return 0;
}
static int WINAPI w_WSACleanup(void){ return 0; }
static int WINAPI w_WSAGetLastError(void){ return ws_err; }
static void WINAPI w_WSASetLastError(int e){ ws_err=e; }
static SOCKET WINAPI w_socket(int af, int type, int proto){
  (void)proto;
  if(!ws_started){ ws_err=WSANOTINITIALISED; return INVALID_SOCKET; }
  if(af!=2){ ws_err=WSAEAFNOSUPPORT; return INVALID_SOCKET; }
  if(type!=1 && type!=2){ ws_err=WSAEOPNOTSUPP; return INVALID_SOCKET; }
  for(int i=0;i<MAXWS;i++) if(!S[i].used){ memset(&S[i],0,sizeof S[i]); S[i].used=1; S[i].type=type; S[i].t=-1; S[i].u=-1; return SHANDLE(i); }
  ws_err=WSAEMFILE; return INVALID_SOCKET;
}
static int WINAPI w_closesocket(SOCKET h){
  struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK);
  if(s->type==1 && s->t>=0) tcp_close(s->t); if(s->type==2 && s->u>=0) udp_close(s->u);
  s->used=0; return 0;
}
static int WINAPI w_shutdown(SOCKET h, int how){ (void)how; return get(h)? 0 : fail(WSAENOTSOCK); }
static u32 sa_ip(const u8 *sa){ return hl(*(u32*)(sa+4)); }
static u16 sa_port(const u8 *sa){ return hs(*(u16*)(sa+2)); }
static void sa_fill(u8 *sa, u32 ip, u16 port){ memset(sa,0,16); *(u16*)sa=2; *(u16*)(sa+2)=hs(port); *(u32*)(sa+4)=hl(ip); }
static int WINAPI w_connect(SOCKET h, const u8 *sa, int len){
  struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK); if(len<16||*(const u16*)sa!=2) return fail(WSAEINVAL);
  if(!net_up()) return fail(WSAENETDOWN);
  u32 ip=sa_ip(sa); u16 port=sa_port(sa);
  if(s->type==2){ s->peer_ip=ip; s->peer_port=port; if(s->u<0) s->u=udp_open(s->lport); return s->u<0? fail(WSAEINVAL) : 0; }
  int t=tcp_connect(ip,port);
  if(t<0){ int e=t==-2? WSAECONNREFUSED : t==-3? WSAETIMEDOUT : WSAENETUNREACH; if(s->ahwnd && (s->aevents&0x10)){ wm_post_msg(s->ahwnd,s->amsg,h,0x10|((u64)e<<16)); s->conn_sent=1; ws_err=WSAEWOULDBLOCK; return SOCKET_ERROR; } return fail(e); }
  s->t=t; s->peer_ip=ip; s->peer_port=port;
  if(s->ahwnd){ if(s->aevents&0x10){ wm_post_msg(s->ahwnd,s->amsg,h,0x10); } if(s->aevents&2){ wm_post_msg(s->ahwnd,s->amsg,h,2); } s->conn_sent=1; s->rd_armed=1; }
  return 0;
}
static int WINAPI w_bind(SOCKET h, const u8 *sa, int len){
  struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK); if(len<16) return fail(WSAEINVAL);
  s->lport=sa_port(sa); if(s->type==2){ s->u=udp_open(s->lport); if(s->u<0) return fail(WSAEINVAL); } return 0;
}
static int WINAPI w_listen(SOCKET h, int backlog){
  (void)backlog; struct wsock *s=get(h); if(!s||s->type!=1) return fail(WSAENOTSOCK);
  s->t=tcp_listen(s->lport); if(s->t<0) return fail(WSAEINVAL); s->listening=1; return 0;
}
static SOCKET WINAPI w_accept(SOCKET h, u8 *sa, int *len){
  struct wsock *s=get(h); if(!s||!s->listening){ ws_err=WSAEINVAL; return INVALID_SOCKET; }
  u32 ip; u16 port; int c=tcp_accept(s->t,s->nb,&ip,&port);
  if(c==-2){ ws_err=WSAEWOULDBLOCK; return INVALID_SOCKET; } if(c<0){ ws_err=WSAEINVAL; return INVALID_SOCKET; }
  for(int i=0;i<MAXWS;i++) if(!S[i].used){
    memset(&S[i],0,sizeof S[i]); S[i].used=1; S[i].type=1; S[i].t=c; S[i].u=-1; S[i].peer_ip=ip; S[i].peer_port=port;
    if(sa&&len&&*len>=16){ sa_fill(sa,ip,port); *len=16; } return SHANDLE(i);
  }
  tcp_close(c); ws_err=WSAEMFILE; return INVALID_SOCKET;
}
static int WINAPI w_send(SOCKET h, const void *buf, int n, int flags){
  (void)flags; struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK);
  if(s->type==2){ if(!s->peer_port) return fail(WSAENOTCONN); if(s->u<0) s->u=udp_open(0); return udp_sendto(s->u,s->peer_ip,s->peer_port,buf,n)<0? fail(WSAENETUNREACH) : n; }
  if(s->t<0) return fail(WSAENOTCONN);
  int r=tcp_write(s->t,buf,n); return r<0? fail(tcp_err(r)) : r;
}
static int WINAPI w_recv(SOCKET h, void *buf, int n, int flags){
  (void)flags; struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK);
  if(s->type==2){ if(s->u<0) return fail(WSAENOTCONN); int r=udp_recvfrom(s->u,buf,n,NULL,NULL,s->nb); return r==-2? fail(WSAEWOULDBLOCK) : r; }
  if(s->t<0) return fail(WSAENOTCONN);
  int r=tcp_read(s->t,buf,n,s->nb); s->rd_armed=1; return r==-3? fail(WSAEWOULDBLOCK) : r<0? fail(tcp_err(r)) : r;
}
static int WINAPI w_sendto(SOCKET h, const void *buf, int n, int flags, const u8 *sa, int len){
  (void)flags; struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK);
  if(s->type==1) return w_send(h,buf,n,0);
  if(len<16) return fail(WSAEINVAL); if(s->u<0) s->u=udp_open(s->lport);
  return udp_sendto(s->u,sa_ip(sa),sa_port(sa),buf,n)<0? fail(WSAENETUNREACH) : n;
}
static int WINAPI w_recvfrom(SOCKET h, void *buf, int n, int flags, u8 *sa, int *len){
  (void)flags; struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK);
  if(s->type==1) return w_recv(h,buf,n,0);
  if(s->u<0) return fail(WSAENOTCONN);
  u32 ip=0; u16 port=0; int r=udp_recvfrom(s->u,buf,n,&ip,&port,s->nb);
  if(r==-2) return fail(WSAEWOULDBLOCK); if(r<0) return fail(WSAEINVAL);
  if(sa&&len&&*len>=16){ sa_fill(sa,ip,port); *len=16; } return r;
}
static int WINAPI w_WSAAsyncSelect(SOCKET h, u64 hwnd, u32 msg, i32 events){
  struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK);
  s->ahwnd=hwnd; s->amsg=msg; s->aevents=(u32)events; s->nb=hwnd? 1 : s->nb; s->rd_armed=1; s->close_sent=0;
  if(hwnd && s->type==1 && s->t>=0 && !s->conn_sent){ if(events&0x10) wm_post_msg(hwnd,msg,h,0x10); if(events&2) wm_post_msg(hwnd,msg,h,2); s->conn_sent=1; }
  return 0;
}
/* called whenever a program pumps messages: turn socket state into FD_READ / FD_WRITE / FD_ACCEPT / FD_CLOSE messages */
void ws_async_poll(void){
  for(int i=0;i<MAXWS;i++){ struct wsock *s=&S[i]; if(!s->used || !s->ahwnd) continue; SOCKET h=0x1000+(u64)i*4;
    if(s->type!=1) continue;
    if(s->listening){ if((s->aevents&8) && tcp_pending(s->t) && s->rd_armed){ s->rd_armed=0; wm_post_msg(s->ahwnd,s->amsg,h,8); } continue; }
    if(s->t<0) continue;
    if((s->aevents&1) && s->rd_armed && tcp_avail(s->t)>0){ s->rd_armed=0; wm_post_msg(s->ahwnd,s->amsg,h,1); }
    if((s->aevents&0x20) && !s->close_sent && tcp_avail(s->t)==0){ int st=tcp_state(s->t); if(tcp_rst(s->t)){ s->close_sent=1; wm_post_msg(s->ahwnd,s->amsg,h,0x20|((u64)WSAECONNRESET<<16)); } else if(st==0||st==7){ s->close_sent=1; wm_post_msg(s->ahwnd,s->amsg,h,0x20); } }
  }
}
static int WINAPI w_ioctlsocket(SOCKET h, i32 cmd, u32 *arg){
  struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK);
  if(cmd==(i32)0x8004667E){ s->nb=*arg!=0; return 0; }                       /* FIONBIO */
  if(cmd==0x4004667F){ *arg= s->type==1&&s->t>=0? (u32)tcp_avail(s->t) : 0; return 0; }   /* FIONREAD */
  return fail(WSAEINVAL);
}
static int WINAPI w_setsockopt(SOCKET h, int lvl, int opt, const void *v, int n){ (void)lvl;(void)opt;(void)v;(void)n; return get(h)? 0 : fail(WSAENOTSOCK); }
static int WINAPI w_getsockopt(SOCKET h, int lvl, int opt, void *v, int *n){ (void)lvl;(void)opt; if(!get(h)) return fail(WSAENOTSOCK); if(v&&n&&*n>=4){ *(u32*)v=0; *n=4; } return 0; }
static int WINAPI w_getsockname(SOCKET h, u8 *sa, int *len){ struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK); if(sa&&len&&*len>=16){ sa_fill(sa,net_ip(),s->lport); *len=16; } return 0; }
static int WINAPI w_getpeername(SOCKET h, u8 *sa, int *len){ struct wsock *s=get(h); if(!s) return fail(WSAENOTSOCK); if(!s->peer_port) return fail(WSAENOTCONN); if(sa&&len&&*len>=16){ sa_fill(sa,s->peer_ip,s->peer_port); *len=16; } return 0; }

/* fd_set on x64: u32 count, 4 pad, SOCKET array */
static int readable(struct wsock *s){
  if(s->type==2) return s->u>=0 && udp_avail(s->u);
  if(s->listening) return tcp_pending(s->t);
  if(s->t<0) return 0; int st=tcp_state(s->t);
  return tcp_avail(s->t)>0 || tcp_rst(s->t) || st==0 || st==7;               /* data, reset, closed, or peer closed (FIN) */
}
static int check_set(u8 *fds, int wr){
  if(!fds) return 0; u32 n=*(u32*)fds; u64 *a=(u64*)(fds+8); int out=0;
  for(u32 i=0;i<n;i++){ struct wsock *s=get(a[i]); if(s && (wr? (s->type==2 || s->t<0 || tcp_writable(s->t)) : readable(s))) a[out++]=a[i]; }
  *(u32*)fds=out; return out;
}
static int WINAPI w_select(int nfds, u8 *rd, u8 *wr, u8 *ex, const i32 *tv){
  (void)nfds; u32 end=tv? ticks+(u32)tv[0]*1000+(u32)tv[1]/1000 : 0xFFFFFFFF; int inf=!tv;
  u8 rcopy[8+8*64], wcopy[8+8*64]; if(rd) memcpy(rcopy,rd,8+8*64); if(wr) memcpy(wcopy,wr,8+8*64);
  for(;;){
    net_poll();
    int n=check_set(rd,0)+check_set(wr,1);
    if(n||(!inf && (i32)(ticks-end)>=0)){ if(ex) *(u32*)ex=0; return n; }
    if(rd) memcpy(rd,rcopy,8+8*64); if(wr) memcpy(wr,wcopy,8+8*64);
    idle_wait();
  }
}
static int WINAPI w___WSAFDIsSet(SOCKET h, const u8 *fds){ u32 n=*(const u32*)fds; const u64 *a=(const u64*)(fds+8); for(u32 i=0;i<n;i++) if(a[i]==h) return 1; return 0; }

static u16 WINAPI w_htons(u16 x){ return hs(x); }
static u16 WINAPI w_ntohs(u16 x){ return hs(x); }
static u32 WINAPI w_htonl(u32 x){ return hl(x); }
static u32 WINAPI w_ntohl(u32 x){ return hl(x); }
static u32 WINAPI w_inet_addr(const char *s){
  u32 v=0; int parts=0; for(;;){ if(*s<'0'||*s>'9') return 0xFFFFFFFF; u32 x=0; while(*s>='0'&&*s<='9') x=x*10+(*s++-'0'); if(x>255) return 0xFFFFFFFF; v=(v<<8)|x; parts++; if(*s=='.'){ s++; continue; } break; }
  return (parts==4 && !*s)? hl(v) : 0xFFFFFFFF;
}
static char *WINAPI w_inet_ntoa(u32 in){ static char b[16]; net_ip_str(hl(in),b); return b; }
static void *WINAPI w_gethostbyname(const char *name){
  static struct { u64 name, aliases; u16 type, len; u32 pad; u64 list; } he; static u32 addr; static u64 list[2]; static u64 aliases[1]; static char nm[64];
  u32 ip; if(!ws_started){ ws_err=WSANOTINITIALISED; return NULL; }
  if(!stricmp(name,"willows")) ip=net_ip(); else if(!stricmp(name,"localhost")) ip=0x7F000001;
  else if(dns_resolve(name,&ip)){ ws_err=WSAHOST_NOT_FOUND; return NULL; }
  addr=hl(ip); list[0]=(u64)&addr; list[1]=0; aliases[0]=0; unsigned i=0; for(;name[i]&&i<63;i++) nm[i]=name[i]; nm[i]=0;
  he.name=(u64)nm; he.aliases=(u64)aliases; he.type=2; he.len=4; he.list=(u64)list; return &he;
}
static int WINAPI w_gethostname(char *b, int n){ const char *h="willows"; if(n<8) return fail(WSAEINVAL); memcpy(b,h,8); return 0; }
static int WINAPI w_getaddrinfo(const char *node, const char *service, const u8 *hints, u8 **res){
  if(!ws_started) return WSANOTINITIALISED;
  int socktype=hints? *(const i32*)(hints+8) : 0, passive=hints? (*(const i32*)hints&1) : 0;
  u16 port=0; if(service){ if(!strcmp(service,"http")) port=80; else if(!strcmp(service,"https")) port=443; else if(!strcmp(service,"ftp")) port=21; else { const char *p=service; u32 x=0; while(*p>='0'&&*p<='9') x=x*10+(*p++-'0'); port=x; } }
  u32 ip; if(!node) ip=passive? 0 : 0x7F000001; else if(dns_resolve(node,&ip)) return WSAHOST_NOT_FOUND;
  u8 *ai=kmalloc(48,16), *sa=kmalloc(16,16); if(!ai||!sa) return WSAHOST_NOT_FOUND;
  sa_fill(sa,ip,port); *(i32*)(ai+4)=2; *(i32*)(ai+8)=socktype? socktype : 1; *(i32*)(ai+12)=(socktype==2)? 17 : 6;
  *(u64*)(ai+16)=16; *(u64*)(ai+32)=(u64)sa; *res=ai; return 0;
}
static void WINAPI w_freeaddrinfo(u8 *ai){ (void)ai; }

/* iphlpapi: the classic ping API */
static u64 WINAPI i_IcmpCreateFile(void){ return 0x77; }
static BOOL WINAPI i_IcmpCloseHandle(u64 h){ (void)h; return 1; }
static u32 WINAPI i_IcmpSendEcho(u64 h, u32 dest, void *data, u16 size, void *opts, u8 *reply, u32 rsize, u32 timeout){
  (void)h;(void)data;(void)size;(void)opts; int rtt=net_ping(hl(dest),timeout? timeout : 1000);
  if(reply && rsize>=40){ memset(reply,0,40); *(u32*)reply=dest; *(u32*)(reply+4)= rtt>=0? 0 : 11010; *(u32*)(reply+8)= rtt>=0? (u32)rtt : 0; }
  return rtt>=0;
}

#define W(n) {"ws2_32", #n, w_##n}
const struct export net_exports[]={
  W(WSAStartup), W(WSACleanup), W(WSAGetLastError), W(WSASetLastError), W(socket), W(closesocket), W(shutdown), W(connect), W(bind),
  W(listen), W(accept), W(send), W(recv), W(sendto), W(recvfrom), W(ioctlsocket), W(WSAAsyncSelect), W(setsockopt), W(getsockopt), W(getsockname),
  W(getpeername), W(select), W(__WSAFDIsSet), W(htons), W(ntohs), W(htonl), W(ntohl), W(inet_addr), W(inet_ntoa), W(gethostbyname),
  W(gethostname), W(getaddrinfo), W(freeaddrinfo),
  {"iphlpapi","IcmpCreateFile",i_IcmpCreateFile}, {"iphlpapi","IcmpCloseHandle",i_IcmpCloseHandle}, {"iphlpapi","IcmpSendEcho",i_IcmpSendEcho},
};
const unsigned net_nexports=sizeof net_exports/sizeof *net_exports;
