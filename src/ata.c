#include "kernel.h"
/* Minimal ATA PIO driver (primary channel, master, LBA28). Works with QEMU and legacy/IDE-mode disks;
   it cannot talk to AHCI-only SATA, NVMe or USB sticks. */
static int ata_ok; static u32 ata_total;
static void delay400(void){ for(int i=0;i<4;i++) inb(0x3F6); }
static int wait_bsy(void){ for(int i=0;i<2000000;i++) if(!(inb(0x1F7)&0x80)) return 0; return -1; }
static int wait_drq(void){ for(int i=0;i<2000000;i++){ u8 s=inb(0x1F7); if(s&0x01||s&0x20) return -1; if(!(s&0x80) && (s&0x08)) return 0; } return -1; }
static void insw(u16 *b){ u16 *d=b; u64 c=256; __asm__ volatile("rep insw":"+D"(d),"+c"(c):"d"((u16)0x1F0):"memory"); }
static void outsw(const u16 *b){ const u16 *s=b; u64 c=256; __asm__ volatile("rep outsw":"+S"(s),"+c"(c):"d"((u16)0x1F0)); }

int ata_init(void){
  outb(0x3F6,0x02);                                    /* no interrupts, we poll */
  outb(0x1F6,0xA0); delay400();
  outb(0x1F2,0); outb(0x1F3,0); outb(0x1F4,0); outb(0x1F5,0);
  outb(0x1F7,0xEC);                                    /* IDENTIFY */
  if(inb(0x1F7)==0 || inb(0x1F7)==0xFF) return 0;      /* no drive */
  if(wait_bsy()) return 0;
  if(inb(0x1F4)||inb(0x1F5)) return 0;                 /* ATAPI/SATA signature: not a plain ATA disk */
  if(wait_drq()) return 0;
  u16 id[256]; insw(id);
  ata_total=id[60]|((u32)id[61]<<16); ata_ok=ata_total>0; return ata_ok;
}
u32 ata_sectors(void){ return ata_ok? ata_total : 0; }
static void setup(u32 lba, int cmd){
  outb(0x1F6,0xE0|((lba>>24)&0x0F)); outb(0x1F2,1);
  outb(0x1F3,lba&0xFF); outb(0x1F4,(lba>>8)&0xFF); outb(0x1F5,(lba>>16)&0xFF); outb(0x1F7,cmd);
}
int ata_read(u32 lba, u32 n, void *buf){
  if(!ata_ok) return -1;
  for(u32 i=0;i<n;i++){ setup(lba+i,0x20); delay400(); if(wait_bsy()||wait_drq()) return -1; insw((u16*)((u8*)buf+i*512)); }
  return 0;
}
int ata_write(u32 lba, u32 n, const void *buf){
  if(!ata_ok) return -1;
  for(u32 i=0;i<n;i++){ setup(lba+i,0x30); delay400(); if(wait_bsy()||wait_drq()) return -1; outsw((const u16*)((const u8*)buf+i*512)); if(wait_bsy()) return -1; }
  outb(0x1F7,0xE7); return wait_bsy();                  /* flush cache */
}
