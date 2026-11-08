#include "kernel.h"
/* Intel 82540EM-family (e1000) driver, polled. QEMU's default NIC, also common on real boards. */
#define NRX 32
#define NTX 16
struct rxd { u64 addr; u16 len, csum; u8 status, errors; u16 special; } __attribute__((packed));
struct txd { u64 addr; u16 len; u8 cso, cmd, status, css; u16 special; } __attribute__((packed));
static volatile u8 *mmio; static struct rxd *rx; static struct txd *tx; static u8 *rxbuf, *txbuf; static u32 rx_cur, tx_cur; static int ok;

static u32 pci_r(u8 b,u8 d,u8 f,u8 o){ outl(0xCF8,0x80000000u|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(o&0xFC)); return inl(0xCFC); }
static void pci_w(u8 b,u8 d,u8 f,u8 o,u32 v){ outl(0xCF8,0x80000000u|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(o&0xFC)); outl(0xCFC,v); }
static u32 rd(u32 r){ return *(volatile u32*)(mmio+r); }
static void wr(u32 r,u32 v){ *(volatile u32*)(mmio+r)=v; }
static void wait_ms(u32 ms){ u32 t=ticks+ms; while((i32)(ticks-t)<0) __asm__ volatile("hlt"); }
static u16 eeprom(u8 a){ wr(0x14,((u32)a<<8)|1); for(int i=0;i<100000;i++){ u32 v=rd(0x14); if(v&0x10) return v>>16; } return 0; }

int nic_init(u8 mac[6]){
  int found=0; u8 B=0,D=0,F=0;
  for(int b=0;b<8 && !found;b++) for(int d=0;d<32 && !found;d++){
    u32 id=pci_r(b,d,0,0); if((id&0xFFFF)!=0x8086) continue;
    u16 dev=id>>16;
    if(dev==0x100E||dev==0x100F||dev==0x1004||dev==0x1015||dev==0x1019||dev==0x10D3||dev==0x153A||dev==0x1533||dev==0x10EA){ B=b;D=d;F=0;found=1; }
  }
  if(!found) return 0;
  u32 cmd=pci_r(B,D,F,4); pci_w(B,D,F,4,cmd|0x6);                    /* memory space + bus master */
  u32 bar=pci_r(B,D,F,0x10)&~0xFu; if(!bar || bar>=0x100000000ull) return 0;
  mmio=(volatile u8*)(u64)bar;
  wr(0xD8,0xFFFFFFFF); wr(0x100,0); wr(0x400,0);                      /* interrupts off, rx/tx off */
  wr(0x0,rd(0x0)|(1u<<26)); wait_ms(20);                              /* reset */
  wr(0xD8,0xFFFFFFFF);
  wr(0x0,(rd(0x0)|(1u<<6)|(1u<<5))&~(1u<<3));                        /* set link up, auto speed */
  u32 lo=rd(0x5400), hi=rd(0x5404);
  if(!(hi&0x80000000u)){ u16 w0=eeprom(0),w1=eeprom(1),w2=eeprom(2); lo=w0|((u32)w1<<16); hi=w2; }
  mac[0]=lo; mac[1]=lo>>8; mac[2]=lo>>16; mac[3]=lo>>24; mac[4]=hi; mac[5]=hi>>8;
  for(int i=0;i<128;i++) wr(0x5200+i*4,0);                           /* multicast table */
  rx=kmalloc(sizeof(struct rxd)*NRX,128); tx=kmalloc(sizeof(struct txd)*NTX,128);
  rxbuf=kmalloc(2048*NRX,16); txbuf=kmalloc(2048*NTX,16);
  if(!rx||!tx||!rxbuf||!txbuf) return 0;
  for(int i=0;i<NRX;i++){ rx[i].addr=(u64)(rxbuf+i*2048); rx[i].status=0; }
  for(int i=0;i<NTX;i++){ tx[i].addr=(u64)(txbuf+i*2048); tx[i].status=1; tx[i].cmd=0; }
  wr(0x2800,(u32)(u64)rx); wr(0x2804,0); wr(0x2808,NRX*16); wr(0x2810,0); wr(0x2818,NRX-1);
  wr(0x3800,(u32)(u64)tx); wr(0x3804,0); wr(0x3808,NTX*16); wr(0x3810,0); wr(0x3818,0);
  wr(0x100,(1<<1)|(1<<15)|(1<<26));                                  /* RX enable, accept broadcast, strip CRC, 2048B buffers */
  wr(0x400,(1<<1)|(1<<3)|(0x10<<4)|(0x40<<12)); wr(0x410,0x0060200A);/* TX enable, pad short packets */
  rx_cur=0; tx_cur=0; ok=1; return 1;
}
int nic_send(const void *frame, u32 len){
  if(!ok||len>1514) return -1;
  struct txd *d=&tx[tx_cur];
  for(int i=0;i<200000 && !(d->status&1);i++);                       /* wait until the descriptor is free */
  if(!(d->status&1)) return -1;
  memcpy(txbuf+tx_cur*2048,frame,len);
  d->len=len; d->cmd=0x0B; d->status=0;                              /* EOP | IFCS | RS */
  tx_cur=(tx_cur+1)%NTX; wr(0x3818,tx_cur);
  return 0;
}
const u8 *nic_rx(u32 *len){
  if(!ok) return NULL;
  struct rxd *d=&rx[rx_cur];
  if(!(d->status&1)) return NULL;
  *len=d->len; return rxbuf+rx_cur*2048;
}
void nic_rx_done(void){
  struct rxd *d=&rx[rx_cur]; d->status=0; wr(0x2818,rx_cur); rx_cur=(rx_cur+1)%NRX;
}
