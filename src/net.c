#include "kernel.h"
/* A small IPv4 stack: Ethernet, ARP, ICMP echo, UDP, DHCP client, DNS resolver, TCP (client + server, stop-and-wait sender).
   Polled from idle_wait(); everything is cooperative so no locking. IP addresses are host-order numbers in the API. */
#define ETH_ARP 0x0806
#define ETH_IP  0x0800
#define IP4(a,b,c,d) (((u32)(a)<<24)|((u32)(b)<<16)|((u32)(c)<<8)|(u32)(d))
static inline u16 hs(u16 x){ return (u16)((x>>8)|(x<<8)); }
static inline u32 hl(u32 x){ return __builtin_bswap32(x); }
struct eth  { u8 dst[6], src[6]; u16 type; } __attribute__((packed));
struct arp  { u16 htype, ptype; u8 hlen, plen; u16 op; u8 sha[6]; u32 spa; u8 tha[6]; u32 tpa; } __attribute__((packed));
struct iph  { u8 vihl, tos; u16 len, id, frag; u8 ttl, proto; u16 csum; u32 src, dst; } __attribute__((packed));
struct udph { u16 sport, dport, len, csum; } __attribute__((packed));
struct tcph { u16 sport, dport; u32 seq, ack; u8 off, flags; u16 win, csum, urg; } __attribute__((packed));
struct icmph{ u8 type, code; u16 csum, id, seq; } __attribute__((packed));

static u8 mymac[6]; static u32 myip, netmask, gateway, dnsip; static int nic_ok, have_ip, dhcp_static;
static const u8 bcast[6]={255,255,255,255,255,255};
int net_up(void){ return have_ip; }
u32 net_ip(void){ return myip; }
void net_ip_str(u32 ip, char *o){ int n=0; for(int i=3;i>=0;i--){ u32 v=(ip>>(i*8))&255; char t[4]; int k=0; do{ t[k++]='0'+v%10; v/=10; }while(v); while(k) o[n++]=t[--k]; if(i) o[n++]='.'; } o[n]=0; }

/* ---- checksums ---- */
static u32 csum_add(u32 s, const void *d, u32 n){ const u8 *p=d; while(n>1){ s+=(p[0]<<8)|p[1]; p+=2; n-=2; } if(n) s+=p[0]<<8; return s; }
static u16 csum_fin(u32 s){ while(s>>16) s=(s&0xFFFF)+(s>>16); return hs((u16)~s); }
static u16 l4_csum(u32 src, u32 dst, u8 proto, const void *seg, u32 len){
  u32 s=0; u8 ph[12]; *(u32*)ph=src; *(u32*)(ph+4)=dst; ph[8]=0; ph[9]=proto; *(u16*)(ph+10)=hs(len);
  s=csum_add(s,ph,12); s=csum_add(s,seg,len); return csum_fin(s);
}

/* ---- ARP ---- */
static struct { u32 ip; u8 mac[6]; int ok; } arpc[8]; static int arp_next;
static void arp_learn(u32 ip, const u8 *mac){
  for(int i=0;i<8;i++) if(arpc[i].ok && arpc[i].ip==ip){ memcpy(arpc[i].mac,mac,6); return; }
  arpc[arp_next].ip=ip; memcpy(arpc[arp_next].mac,mac,6); arpc[arp_next].ok=1; arp_next=(arp_next+1)&7;
}
static int arp_lookup(u32 ip, u8 *mac){ for(int i=0;i<8;i++) if(arpc[i].ok && arpc[i].ip==ip){ memcpy(mac,arpc[i].mac,6); return 1; } return 0; }
static void arp_send(u16 op, const u8 *dmac, u32 tip){
  u8 f[60]; memset(f,0,sizeof f); struct eth *e=(void*)f; struct arp *a=(void*)(f+14);
  memcpy(e->dst, op==1? bcast : dmac, 6); memcpy(e->src,mymac,6); e->type=hs(ETH_ARP);
  a->htype=hs(1); a->ptype=hs(ETH_IP); a->hlen=6; a->plen=4; a->op=hs(op);
  memcpy(a->sha,mymac,6); a->spa=hl(myip); if(op==2) memcpy(a->tha,dmac,6); a->tpa=hl(tip);
  nic_send(f,sizeof f);
}
static int arp_resolve(u32 ip, u8 *mac){
  if(arp_lookup(ip,mac)) return 1;
  for(int t=0;t<5;t++){
    arp_send(1,bcast,ip);
    u32 end=ticks+300; while((i32)(ticks-end)<0){ net_poll(); if(arp_lookup(ip,mac)) return 1; idle_wait(); }
  }
  return 0;
}

/* ---- IPv4 transmit ---- */
static u8 txf[1600]; static u16 ip_id;
static int ip_out(u32 src, u32 dst, u8 proto, const void *payload, u32 len, const u8 *dmac_override){
  if(len>1480) return -1;
  u8 dmac[6];
  if(dmac_override) memcpy(dmac,dmac_override,6);
  else {
    u32 hop=((dst&netmask)==(myip&netmask))? dst : gateway;
    if(!arp_resolve(hop,dmac)) return -1;
  }
  struct eth *e=(void*)txf; struct iph *ip=(void*)(txf+14);
  memcpy(e->dst,dmac,6); memcpy(e->src,mymac,6); e->type=hs(ETH_IP);
  ip->vihl=0x45; ip->tos=0; ip->len=hs(20+len); ip->id=hs(ip_id++); ip->frag=0; ip->ttl=64; ip->proto=proto; ip->csum=0;
  ip->src=hl(src); ip->dst=hl(dst); ip->csum=csum_fin(csum_add(0,ip,20));
  memcpy(txf+34,payload,len);
  u32 total=34+len; if(total<60){ memset(txf+total,0,60-total); total=60; }
  return nic_send(txf,total);
}

/* ---- UDP ---- */
#define MAXUDP 8
struct usock { u8 used; u16 port; struct { u16 len; u32 ip; u16 port; u8 data[1472]; } q[4]; u8 qh, qt; };
static struct usock U[MAXUDP];
int udp_open(u16 port){
  static u16 eph=40000;
  if(!port) port=eph++;
  for(int i=0;i<MAXUDP;i++) if(U[i].used && U[i].port==port) return -1;
  for(int i=0;i<MAXUDP;i++) if(!U[i].used){ memset(&U[i],0,sizeof U[i]); U[i].used=1; U[i].port=port; return i; }
  return -1;
}
void udp_close(int s){ if(s>=0&&s<MAXUDP) U[s].used=0; }
int udp_avail(int s){ return s>=0&&s<MAXUDP&&U[s].used&&U[s].qh!=U[s].qt; }
static int udp_tx(u32 src, u16 sp, u32 dst, u16 dp, const void *d, u32 n, const u8 *dmac){
  if(n>1472) return -1;
  u8 seg[1480]; struct udph *u=(void*)seg; u->sport=hs(sp); u->dport=hs(dp); u->len=hs(8+n); u->csum=0;
  memcpy(seg+8,d,n); u->csum=l4_csum(hl(src),hl(dst),17,seg,8+n); if(!u->csum) u->csum=0xFFFF;
  return ip_out(src,dst,17,seg,8+n,dmac);
}
int udp_sendto(int s, u32 ip, u16 port, const void *d, u32 n){
  if(s<0||s>=MAXUDP||!U[s].used||!have_ip) return -1;
  return udp_tx(myip,U[s].port,ip,port,d,n,NULL);
}
int udp_recvfrom(int s, void *d, u32 n, u32 *ip, u16 *port, int nb){
  if(s<0||s>=MAXUDP||!U[s].used) return -1;
  while(U[s].qh==U[s].qt){ if(nb) return -2; idle_wait(); net_poll(); }
  u8 i=U[s].qt; U[s].qt=(U[s].qt+1)&3; u32 c=U[s].q[i].len<n? U[s].q[i].len : n;
  memcpy(d,U[s].q[i].data,c); if(ip) *ip=U[s].q[i].ip; if(port) *port=U[s].q[i].port; return (int)c;
}

static void ip_ready(const char *how){
  char a[20], g[20]; net_ip_str(myip,a); net_ip_str(gateway,g);
  con_quiet++; kprintf("Network: %s: ip %s, gateway %s\n",how,a,g); wm_console_dirty(); con_quiet--;
}
/* ---- DHCP ---- */
static u32 dhcp_xid=0x57494C4C, dhcp_offer, dhcp_server, dhcp_t0; static int dhcp_state, dhcp_sock=-1;
static void dhcp_send(int type){
  u8 b[300]; memset(b,0,sizeof b); b[0]=1; b[1]=1; b[2]=6; *(u32*)(b+4)=hl(dhcp_xid); *(u16*)(b+10)=hs(0x8000);
  memcpy(b+28,mymac,6); b[236]=0x63; b[237]=0x82; b[238]=0x53; b[239]=0x63;
  u8 *o=b+240; *o++=53; *o++=1; *o++=type;
  if(type==3){ *o++=50; *o++=4; *(u32*)o=hl(dhcp_offer); o+=4; *o++=54; *o++=4; *(u32*)o=hl(dhcp_server); o+=4; }
  *o++=55; *o++=3; *o++=1; *o++=3; *o++=6; *o++=255;
  udp_tx(0,68,0xFFFFFFFF,67,b,(u32)(o-b)<300?300:(u32)(o-b),bcast);
}
static void dhcp_rx(const u8 *b, u32 n){
  if(n<241||b[0]!=2||*(u32*)(b+4)!=hl(dhcp_xid)) return;
  u32 yi=hl(*(u32*)(b+16)); int type=0; u32 mask=0, rt=0, dn=0, sid=0;
  for(const u8 *o=b+240; o<b+n && *o!=255;){
    if(*o==0){ o++; continue; } u8 c=o[0], l=o[1]; if(o+2+l>b+n) break;
    if(c==53) type=o[2]; else if(c==1&&l>=4) mask=hl(*(u32*)(o+2)); else if(c==3&&l>=4) rt=hl(*(u32*)(o+2));
    else if(c==6&&l>=4) dn=hl(*(u32*)(o+2)); else if(c==54&&l>=4) sid=hl(*(u32*)(o+2));
    o+=2+l;
  }
  if(type==2 && dhcp_state==1){ dhcp_offer=yi; dhcp_server=sid; dhcp_state=2; dhcp_send(3); }
  else if(type==5 && dhcp_state==2){ myip=yi; netmask=mask? mask : IP4(255,255,255,0); gateway=rt; dnsip=dn? dn : rt; have_ip=1; dhcp_state=3; ip_ready("DHCP"); }
}

/* ---- ICMP ---- */
static u16 ping_id=0x5757, ping_seq; static volatile int ping_got; static u32 ping_t0, ping_rtt;
static void icmp_rx(u32 src, const u8 *p, u32 n){
  if(n<8) return; const struct icmph *h=(const void*)p;
  if(h->type==8){                                                    /* echo request -> reply */
    u8 buf[1480]; if(n>sizeof buf) return; memcpy(buf,p,n); struct icmph *r=(void*)buf; r->type=0; r->csum=0; r->csum=csum_fin(csum_add(0,buf,n));
    ip_out(myip,src,1,buf,n,NULL);
  } else if(h->type==0 && h->id==hs(ping_id) && h->seq==hs(ping_seq)){ ping_rtt=ticks-ping_t0; ping_got=1; }
}
int net_ping(u32 ip, u32 timeout){
  if(!have_ip) return -1;
  u8 buf[8+32]; struct icmph *h=(void*)buf; memset(buf,0,sizeof buf); for(int i=0;i<32;i++) buf[8+i]='a'+i%23;
  h->type=8; h->id=hs(ping_id); h->seq=hs(++ping_seq); h->csum=csum_fin(csum_add(0,buf,sizeof buf));
  ping_got=0; ping_t0=ticks;
  if(ip_out(myip,ip,1,buf,sizeof buf,NULL)) return -1;
  u32 end=ticks+timeout; while((i32)(ticks-end)<0){ net_poll(); if(ping_got) return (int)ping_rtt; idle_wait(); }
  return -1;
}

/* ---- TCP ---- */
#define MAXTCP 16
#define RXSZ 16384
enum { T_CLOSED, T_LISTEN, T_SYN_SENT, T_SYN_RCVD, T_ESTAB, T_FIN1, T_FIN2, T_CLOSE_WAIT, T_LAST_ACK };
struct tsock {
  u8 used, state, eof, rst, has_un, utries, uflags; u32 rip; u16 lport, rport, mss, ulen; u32 snd_una, snd_nxt, rcv_nxt, peer_win, useq, uts;
  u32 rxh, rxn; u8 ub[1460]; int aq[4]; u8 aqn; int parent;
};
static struct tsock T[MAXTCP]; static u8 trx[MAXTCP][RXSZ];
static int seq_le(u32 a, u32 b){ return (i32)(b-a)>=0; }
static void tcp_xmit(struct tsock *t, u8 flags, const void *d, u32 n, u32 seq){
  u8 seg[1480]; struct tcph *h=(void*)seg; u32 hl_=20;
  h->sport=hs(t->lport); h->dport=hs(t->rport); h->seq=hl(seq); h->ack=(flags&0x10)? hl(t->rcv_nxt) : 0;
  h->flags=flags; h->win=hs(RXSZ-t->rxn>65535? 65535 : RXSZ-t->rxn); h->csum=0; h->urg=0;
  if(flags&0x02){ seg[20]=2; seg[21]=4; *(u16*)(seg+22)=hs(1460); hl_=24; }
  h->off=(hl_/4)<<4;
  if(n) memcpy(seg+hl_,d,n);
  h->csum=l4_csum(hl(myip),hl(t->rip),6,seg,hl_+n);
  ip_out(myip,t->rip,6,seg,hl_+n,NULL);
}
static void tcp_send_ctl(struct tsock *t, u8 flags){                      /* SYN/FIN: tracked for retransmission */
  t->uflags=flags; t->ulen=0; t->useq=t->snd_nxt; t->uts=ticks; t->utries=0; t->has_un=1;
  tcp_xmit(t,flags,NULL,0,t->snd_nxt); t->snd_nxt++;
}
static void tcp_reset(struct tsock *t){ t->rst=1; t->state=T_CLOSED; t->has_un=0; }
static struct tsock *tfind(u32 rip, u16 rport, u16 lport){
  for(int i=0;i<MAXTCP;i++){ struct tsock *t=&T[i]; if(t->used && t->state!=T_LISTEN && t->lport==lport && t->rport==rport && t->rip==rip) return t; }
  return NULL;
}
static struct tsock *tlisten(u16 lport){ for(int i=0;i<MAXTCP;i++) if(T[i].used && T[i].state==T_LISTEN && T[i].lport==lport) return &T[i]; return NULL; }
static int talloc(void){ for(int i=0;i<MAXTCP;i++) if(!T[i].used){ memset(&T[i],0,sizeof T[i]); T[i].used=1; T[i].parent=-1; return i; } return -1; }
static void rst_reply(u32 src, const struct tcph *h, u32 datalen){
  struct tsock t; memset(&t,0,sizeof t); t.rip=src; t.lport=hs(h->dport); t.rport=hs(h->sport);
  if(h->flags&0x10) tcp_xmit(&t,0x04,NULL,0,hl(h->ack)); else { t.rcv_nxt=hl(h->seq)+datalen+((h->flags&3)?1:0); tcp_xmit(&t,0x14,NULL,0,0); }
}
static void tcp_rx(u32 src, const u8 *p, u32 n){
  if(n<20) return; const struct tcph *h=(const void*)p; u32 off=(h->off>>4)*4; if(off<20||off>n) return;
  u8 f=h->flags; u32 seq=hl(h->seq), ack=hl(h->ack), dl=n-off; const u8 *data=p+off; u16 lport=hs(h->dport), rport=hs(h->sport);
  struct tsock *t=tfind(src,rport,lport);
  if(!t){
    struct tsock *l=tlisten(lport);
    if((f&0x02) && !(f&0x10) && l){                                  /* new connection to a listening socket */
      int ci=talloc(); if(ci<0) return; struct tsock *c=&T[ci];
      c->rip=src; c->rport=rport; c->lport=lport; c->rcv_nxt=seq+1; c->snd_una=c->snd_nxt=ticks*7919u+12345; c->mss=1460; c->peer_win=hs(h->win);
      c->rxh=c->rxn=0; c->parent=l-T; c->state=T_SYN_RCVD; tcp_send_ctl(c,0x12); return;
    }
    if(!(f&0x04)) rst_reply(src,h,dl);
    return;
  }
  if(f&0x04){ if(t->state!=T_LISTEN) tcp_reset(t); return; }
  t->peer_win=hs(h->win);
  if(t->state==T_SYN_SENT){
    if((f&0x12)==0x12 && ack==t->snd_nxt){
      t->rcv_nxt=seq+1; t->snd_una=ack; t->has_un=0; t->state=T_ESTAB;
      for(u32 o=20;o+1<off;){ if(p[o]==2&&o+3<off){ u16 m=(p[o+2]<<8)|p[o+3]; if(m<t->mss) t->mss=m; } if(p[o]==0) break; o+= p[o]==1? 1 : (p[o+1]? p[o+1] : 1); }
      tcp_xmit(t,0x10,NULL,0,t->snd_nxt);
    }
    return;
  }
  if(f&0x10){                                                         /* ACK processing */
    if(seq_le(t->snd_una,ack) && seq_le(ack,t->snd_nxt)){ t->snd_una=ack; if(ack==t->snd_nxt) t->has_un=0; }
    if(t->state==T_SYN_RCVD && ack==t->snd_nxt){
      t->state=T_ESTAB; struct tsock *l=(t->parent>=0)? &T[t->parent] : NULL;
      if(l && l->aqn<4) l->aq[l->aqn++]=t-T;
    } else if(t->state==T_FIN1 && !t->has_un) t->state=T_FIN2;
    else if(t->state==T_LAST_ACK && !t->has_un){ t->state=T_CLOSED; t->used=0; return; }
  }
  if(t->state==T_ESTAB||t->state==T_FIN1||t->state==T_FIN2||t->state==T_CLOSE_WAIT){
    if(dl){
      if(seq==t->rcv_nxt && RXSZ-t->rxn>=dl){
        for(u32 i=0;i<dl;i++) trx[t-T][(t->rxh+t->rxn+i)%RXSZ]=data[i];
        t->rxn+=dl; t->rcv_nxt+=dl; tcp_xmit(t,0x10,NULL,0,t->snd_nxt);
      } else tcp_xmit(t,0x10,NULL,0,t->snd_nxt);                       /* duplicate/out of order: re-ACK, peer retransmits */
    }
    if((f&0x01) && seq+dl==t->rcv_nxt){                               /* FIN */
      t->rcv_nxt++; t->eof=1; tcp_xmit(t,0x10,NULL,0,t->snd_nxt);
      if(t->state==T_ESTAB) t->state=T_CLOSE_WAIT; else if(t->state==T_FIN1||t->state==T_FIN2){ t->state=T_CLOSED; }
    }
  }
}
static void tcp_timers(void){
  for(int i=0;i<MAXTCP;i++){ struct tsock *t=&T[i]; if(!t->used||!t->has_un) continue;
    u32 rto=400u<<(t->utries>4?4:t->utries);
    if((u32)(ticks-t->uts)>=rto){
      if(t->utries>=7){ tcp_reset(t); continue; }
      t->utries++; t->uts=ticks;
      if(t->ulen) tcp_xmit(t,t->uflags,t->ub,t->ulen,t->useq); else tcp_xmit(t,t->uflags,NULL,0,t->useq);
    }
  }
}
int tcp_connect(u32 ip, u16 port){
  if(!have_ip) return -1;
  static u16 eph=49152; int i=talloc(); if(i<0) return -1; struct tsock *t=&T[i];
  t->rip=ip; t->rport=port; t->lport=eph++; if(eph<49152) eph=49152; t->mss=1460; t->snd_una=t->snd_nxt=ticks*104729u+1;
  t->state=T_SYN_SENT; tcp_send_ctl(t,0x02);
  u32 end=ticks+9000; while(t->state==T_SYN_SENT && !t->rst && (i32)(ticks-end)<0){ net_poll(); idle_wait(); }
  if(t->state!=T_ESTAB){ int r=t->rst? -2 : -3; t->used=0; return r; }
  return i;
}
int tcp_listen(u16 port){
  if(tlisten(port)) return -1; int i=talloc(); if(i<0) return -1; T[i].state=T_LISTEN; T[i].lport=port; return i;
}
int tcp_pending(int s){ return s>=0&&s<MAXTCP&&T[s].used&&T[s].aqn>0; }
int tcp_accept(int lis, int nb, u32 *ip, u16 *port){
  if(lis<0||lis>=MAXTCP||!T[lis].used||T[lis].state!=T_LISTEN) return -1;
  while(!T[lis].aqn){ if(nb) return -2; net_poll(); idle_wait(); }
  int c=T[lis].aq[0]; for(int i=1;i<T[lis].aqn;i++) T[lis].aq[i-1]=T[lis].aq[i]; T[lis].aqn--;
  if(ip) *ip=T[c].rip; if(port) *port=T[c].rport; return c;
}
int tcp_state(int s){ return (s>=0&&s<MAXTCP&&T[s].used)? T[s].state : T_CLOSED; }
int tcp_rst(int s){ return s>=0&&s<MAXTCP&&T[s].rst; }
int tcp_avail(int s){ return (s>=0&&s<MAXTCP&&T[s].used)? (int)T[s].rxn : 0; }
int tcp_writable(int s){ return s>=0&&s<MAXTCP&&T[s].used&&(T[s].state==T_ESTAB||T[s].state==T_CLOSE_WAIT)&&!T[s].has_un; }
int tcp_read(int s, void *d, u32 n, int nb){
  if(s<0||s>=MAXTCP||!T[s].used) return -1; struct tsock *t=&T[s];
  while(!t->rxn){
    if(t->rst) return -2; if(t->eof||t->state==T_CLOSED) return 0;
    if(nb) return -3; net_poll(); idle_wait();
  }
  u32 c=t->rxn<n? t->rxn : n; u8 *o=d;
  for(u32 i=0;i<c;i++) o[i]=trx[s][(t->rxh+i)%RXSZ];
  t->rxh=(t->rxh+c)%RXSZ; t->rxn-=c;
  if(c) tcp_xmit(t,0x10,NULL,0,t->snd_nxt);                          /* window update */
  return (int)c;
}
int tcp_write(int s, const void *d, u32 n){
  if(s<0||s>=MAXTCP||!T[s].used) return -1; struct tsock *t=&T[s]; const u8 *p=d; u32 sent=0;
  while(sent<n){
    u32 end=ticks+15000;
    while(t->has_un){ if(t->rst||t->state==T_CLOSED) return -2; if((i32)(ticks-end)>=0){ tcp_reset(t); return -2; } net_poll(); idle_wait(); }
    if(t->rst||(t->state!=T_ESTAB&&t->state!=T_CLOSE_WAIT)) return -2;
    u32 c=n-sent; if(c>t->mss) c=t->mss; if(t->peer_win && c>t->peer_win) c=t->peer_win; if(!c) c=1;
    memcpy(t->ub,p+sent,c); t->ulen=c; t->uflags=0x18; t->useq=t->snd_nxt; t->uts=ticks; t->utries=0; t->has_un=1;
    tcp_xmit(t,0x18,t->ub,c,t->snd_nxt); t->snd_nxt+=c; sent+=c;
  }
  return (int)sent;
}
void tcp_close(int s){
  if(s<0||s>=MAXTCP||!T[s].used) return; struct tsock *t=&T[s];
  if(t->state==T_LISTEN){ t->used=0; return; }
  u32 end=ticks+3000; while(t->has_un && (i32)(ticks-end)<0 && !t->rst){ net_poll(); idle_wait(); }
  if(t->state==T_ESTAB){ t->state=T_FIN1; tcp_send_ctl(t,0x11); }
  else if(t->state==T_CLOSE_WAIT){ t->state=T_LAST_ACK; tcp_send_ctl(t,0x11); }
  end=ticks+1500; while(t->used && t->state!=T_CLOSED && (t->has_un||t->state==T_FIN1) && (i32)(ticks-end)<0){ net_poll(); idle_wait(); }
  t->used=0;
}

/* ---- DNS ---- */
static struct { char name[48]; u32 ip; } dnsc[8]; static int dns_next;
int dns_resolve(const char *name, u32 *ip){
  u32 v=0; int parts=0, ok=1; const char *p=name;                    /* dotted quad? */
  for(;;){ if(*p<'0'||*p>'9'){ ok=0; break; } u32 x=0; while(*p>='0'&&*p<='9') x=x*10+(*p++-'0'); if(x>255){ ok=0; break; } v=(v<<8)|x; parts++; if(*p=='.'){ p++; continue; } break; }
  if(ok && parts==4 && !*p){ *ip=v; return 0; }
  for(int i=0;i<8;i++) if(dnsc[i].ip && !stricmp(dnsc[i].name,name)){ *ip=dnsc[i].ip; return 0; }
  if(!have_ip||!dnsip) return -1;
  int s=udp_open(0); if(s<0) return -1;
  u8 q[300]; u16 id=(u16)(ticks*31+7); memset(q,0,12); *(u16*)q=hs(id); *(u16*)(q+2)=hs(0x0100); *(u16*)(q+4)=hs(1);
  u8 *o=q+12; const char *l=name;
  while(*l){ const char *e=l; while(*e && *e!='.') e++; u32 n=e-l; if(n==0||n>63){ udp_close(s); return -1; } *o++=n; memcpy(o,l,n); o+=n; l=*e? e+1 : e; }
  *o++=0; *(u16*)o=hs(1); o+=2; *(u16*)o=hs(1); o+=2; u32 qlen=o-q;
  for(int attempt=0;attempt<3;attempt++){
    udp_sendto(s,dnsip,53,q,qlen);
    u32 end=ticks+2500;
    while((i32)(ticks-end)<0){
      net_poll();
      if(udp_avail(s)){
        u8 r[600]; int n=udp_recvfrom(s,r,sizeof r,NULL,NULL,1); if(n<12||*(u16*)r!=hs(id)) continue;
        u16 an=(r[6]<<8)|r[7]; if((r[3]&15)!=0||!an){ udp_close(s); return -1; }
        u8 *c=r+12; while(c<r+n && *c){ if((*c&0xC0)==0xC0){ c++; break; } c+=*c+1; } c+= (c<r+n&&*c==0)? 1 : 1; c+=4;   /* skip question */
        for(int k=0;k<an && c+12<=r+n;k++){
          if((*c&0xC0)==0xC0) c+=2; else { while(c<r+n && *c) c+=*c+1; c++; }
          u16 type=(c[0]<<8)|c[1], rdl=(c[8]<<8)|c[9]; c+=10;
          if(type==1 && rdl==4 && c+4<=r+n){ u32 a=((u32)c[0]<<24)|((u32)c[1]<<16)|((u32)c[2]<<8)|c[3];
            *ip=a; unsigned i=0; for(;name[i]&&i<47;i++) dnsc[dns_next].name[i]=name[i]; dnsc[dns_next].name[i]=0; dnsc[dns_next].ip=a; dns_next=(dns_next+1)&7;
            udp_close(s); return 0; }
          c+=rdl;
        }
        udp_close(s); return -1;
      }
      idle_wait();
    }
  }
  udp_close(s); return -1;
}

/* ---- receive path ---- */
static void ip_rx(const u8 *f, u32 n){
  if(n<34) return; const struct iph *ip=(const void*)(f+14);
  if((ip->vihl>>4)!=4) return; u32 hlen=(ip->vihl&15)*4; u32 tot=hs(ip->len); if(hlen<20||tot<hlen||14+tot>n) return;
  if(hs(ip->frag)&0x3FFF) return;                                    /* fragments unsupported */
  u32 dst=hl(ip->dst), src=hl(ip->src);
  if(have_ip && dst!=myip && dst!=0xFFFFFFFF && dst!=(myip|~netmask)) return;
  const u8 *p=f+14+hlen; u32 pl=tot-hlen;
  if(ip->proto==1) icmp_rx(src,p,pl);
  else if(ip->proto==6) tcp_rx(src,p,pl);
  else if(ip->proto==17 && pl>=8){
    const struct udph *u=(const void*)p; u16 dp=hs(u->dport); u32 ul=hs(u->len); if(ul<8||ul>pl) return; ul-=8;
    if(dp==68 && dhcp_state){ dhcp_rx(p+8,ul); return; }
    for(int i=0;i<MAXUDP;i++) if(U[i].used && U[i].port==dp){
      u8 nx=(U[i].qh+1)&3; if(nx==U[i].qt) break; if(ul>1472) break;
      U[i].q[U[i].qh].len=ul; U[i].q[U[i].qh].ip=src; U[i].q[U[i].qh].port=hs(u->sport); memcpy(U[i].q[U[i].qh].data,p+8,ul); U[i].qh=nx; break;
    }
  }
}
void net_poll(void){
  if(!nic_ok) return;
  static int busy; if(busy) return; busy=1;
  u32 len; const u8 *f;
  for(int guard=0; guard<32 && (f=nic_rx(&len)); guard++){
    if(len>=14){
      const struct eth *e=(const void*)f; u16 type=hs(e->type);
      int mine=!memcmp(e->dst,mymac,6)||!memcmp(e->dst,bcast,6);
      if(mine){
        if(type==ETH_ARP && len>=42){
          const struct arp *a=(const void*)(f+14);
          if(hs(a->op)==1 || hs(a->op)==2) arp_learn(hl(a->spa),a->sha);
          if(hs(a->op)==1 && have_ip && hl(a->tpa)==myip) arp_send(2,a->sha,hl(a->spa));
        } else if(type==ETH_IP) ip_rx(f,len);
      }
    }
    nic_rx_done();
  }
  tcp_timers();
  if(!have_ip){                                                       /* DHCP retransmit / static fallback */
    if(dhcp_state==1 && (u32)(ticks-dhcp_t0)>=1500){ dhcp_t0=ticks; dhcp_send(1); }
    if((u32)(ticks-dhcp_t0)>6000 && dhcp_state!=3){ myip=IP4(10,0,2,15); netmask=IP4(255,255,255,0); gateway=IP4(10,0,2,2); dnsip=IP4(10,0,2,3); have_ip=1; dhcp_static=1; ip_ready("no DHCP answer, using QEMU defaults"); }
  }
  busy=0;
}
int net_init(void){
  if(!nic_init(mymac)) return 0;
  nic_ok=1; dhcp_state=1; dhcp_t0=ticks; dhcp_sock=-1;
  dhcp_send(1);
  return 1;
}

