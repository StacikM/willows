#include "kernel.h"
/* PS/2 mouse on the aux port (IRQ12), standard 3-byte packets. */
volatile int mouse_x, mouse_y, mouse_buttons, mouse_wheel;
static u8 pk[4]; static int idx, present, pktlen=3; int mouse_has_wheel(void){ return pktlen==4; }

static void wait_in(void){ for(int i=0;i<100000 && (inb(0x64)&2);i++); }
static int  wait_out(void){ for(int i=0;i<100000;i++) if(inb(0x64)&1) return 1; return 0; }
static void mwrite(u8 v){ wait_in(); outb(0x64,0xD4); wait_in(); outb(0x60,v); }
static int mread(void){ return wait_out()? inb(0x60) : -1; }

static int mouse_setup(void);
int mouse_init(void){      /* interrupts off: controller replies would otherwise be eaten by the keyboard IRQ */
  u64 fl; __asm__ volatile("pushf; pop %0; cli":"=r"(fl));
  int r=mouse_setup();
  while(inb(0x64)&1) inb(0x60);
  if(fl&0x200) __asm__ volatile("sti");
  return r;
}
static int mouse_setup(void){
  wait_in(); outb(0x64,0xA8);                         /* enable aux port */
  wait_in(); outb(0x64,0x20); if(!wait_out()) return 0;
  u8 cfg=inb(0x60); cfg|=2; cfg&=~0x20;               /* IRQ12 on, aux clock on */
  wait_in(); outb(0x64,0x60); wait_in(); outb(0x60,cfg);
  mwrite(0xF6); mread();                              /* defaults */
  mwrite(0xF3); mread(); mwrite(200); mread();        /* magic sample-rate sequence 200,100,80 turns on the scroll wheel */
  mwrite(0xF3); mread(); mwrite(100); mread();
  mwrite(0xF3); mread(); mwrite(80);  mread();
  mwrite(0xF2); mread(); if(mread()==3) pktlen=4;     /* device id 3 = wheel mouse: 4-byte packets */
  mwrite(0xF4); if(mread()!=0xFA) return 0;          /* enable streaming */
  mouse_x=(gfx_ok?gfx_w:1024)/2; mouse_y=(gfx_ok?gfx_h:768)/2;
  outb(0x21, inb(0x21)&~4);                           /* cascade */
  outb(0xA1, inb(0xA1)&~0x10);                        /* IRQ12 */
  present=1; return 1;
}
void mouse_irq(void){
  u8 b=inb(0x60);
  if(idx==0 && (!(b&8) || (b&0xC0))) return;          /* out of sync: wait for a flags byte (bit 3 set, no overflow bits; a stray 0xFA ack looks like "right button held") */
  pk[idx++]=b; if(idx<pktlen) return; idx=0;
  if(pktlen==4){ int z=pk[3]&0x0F; if(z&8) z-=16; mouse_wheel+=z; }
  int dx=pk[1]-((pk[0]<<4)&0x100), dy=pk[2]-((pk[0]<<3)&0x100);
  int w=gfx_ok?(int)gfx_w:1024, h=gfx_ok?(int)gfx_h:768;
  int x=mouse_x+dx, y=mouse_y-dy;
  if(x<0)x=0; if(x>=w)x=w-1; if(y<0)y=0; if(y>=h)y=h-1;
  mouse_x=x; mouse_y=y; mouse_buttons=pk[0]&7;
  if(present && gfx_ok) gfx_cursor_move(x,y);
}
