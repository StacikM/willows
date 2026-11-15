#include "kernel.h"
/* Minimal ATA PIO driver (primary channel, master, LBA28). Works with QEMU and legacy/IDE-mode disks;
   it cannot talk to AHCI-only SATA, NVMe or USB sticks. */
static int ata_ok; static u32 ata_total, mult;          /* mult: sectors per block in multiple mode (0 = plain single-sector PIO) */
static void delay400(void){ for(int i=0;i<4;i++) inb(0x3F6); }
static int wait_bsy(void){ for(int i=0;i<2000000;i++) if(!(inb(0x1F7)&0x80)) return 0; return -1; }
static int wait_drq(void){ for(int i=0;i<2000000;i++){ u8 s=inb(0x1F7); if(s&0x01||s&0x20) return -1; if(!(s&0x80) && (s&0x08)) return 0; } return -1; }
static void insw(u16 *b){ u16 *d=b; u64 c=256; __asm__ volatile("rep insw":"+D"(d),"+c"(c):"d"((u16)0x1F0):"memory"); }
static void outsw(const u16 *b){ const u16 *s=b; u64 c=256; __asm__ volatile("rep outsw":"+S"(s),"+c"(c):"d"((u16)0x1F0)); }

static void find_bm(void);
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
  ata_total=id[60]|((u32)id[61]<<16); ata_ok=ata_total>0;
  if(ata_ok) find_bm();
  u32 m=id[47]&0xFF;                                  /* READ/WRITE MULTIPLE: the drive moves several sectors per request, which is much faster in emulators and on real disks */
  if(ata_ok && m>=2){
    outb(0x1F6,0xE0); delay400(); outb(0x1F2,(u8)m); outb(0x1F7,0xC6);                     /* SET MULTIPLE MODE */
    if(!wait_bsy() && !(inb(0x1F7)&0x01)) mult=m;
  }
  return ata_ok;
}
u32 ata_sectors(void){ return ata_ok? ata_total : 0; }

/* Bus-master DMA (PCI IDE controller): the controller moves the data itself, so a transfer costs a few port accesses instead of one trapped access per word,
   which in an emulator is a hundred times faster than PIO. Falls back to PIO when there is no bus master or the buffer lies above 4 GB. */
static u16 bm; static u32 prd[8] __attribute__((aligned(64)));
static u32 pci_rd(u8 b,u8 d,u8 f,u8 o){ outl(0xCF8,0x80000000u|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(o&0xFC)); return inl(0xCFC); }
static void pci_wr(u8 b,u8 d,u8 f,u8 o,u32 v){ outl(0xCF8,0x80000000u|((u32)b<<16)|((u32)d<<11)|((u32)f<<8)|(o&0xFC)); outl(0xCFC,v); }
static void find_bm(void){
  for(int d=0;d<32;d++) for(int f=0;f<8;f++){
    u32 id=pci_rd(0,(u8)d,(u8)f,0); if((id&0xFFFF)==0xFFFF) continue;
    u32 cl=pci_rd(0,(u8)d,(u8)f,8); if((cl>>16)!=0x0101) continue;                      /* mass storage, IDE */
    u32 bar=pci_rd(0,(u8)d,(u8)f,0x20); if(!(bar&1)) continue;
    pci_wr(0,(u8)d,(u8)f,4,pci_rd(0,(u8)d,(u8)f,4)|0x5);                                  /* I/O space + bus master */
    bm=(u16)(bar&0xFFFC); return;
  }
}
static int dma_xfer(u32 lba, u32 c, void *buf, int write){                              /* c <= 128 sectors; 0 = done, -1 = failed, -2 = cannot use DMA for this buffer */
  u64 a=(u64)buf, len=(u64)c*512; if(!bm || (a&1) || a+len>=0x100000000ull) return -2;
  int n=0; while(len){ u32 ch=0x10000-(u32)(a&0xFFFF); if(ch>len) ch=(u32)len; prd[n*2]=(u32)a; prd[n*2+1]=(ch&0xFFFF)|(len==ch? 0x80000000u : 0); a+=ch; len-=ch; n++; }
  outb(bm,0); outb(bm+2,0x06); outl(bm+4,(u32)(u64)prd);
  outb(0x1F6,0xE0|((lba>>24)&0x0F)); outb(0x1F2,(u8)c);
  outb(0x1F3,lba&0xFF); outb(0x1F4,(lba>>8)&0xFF); outb(0x1F5,(lba>>16)&0xFF); outb(0x1F7,write? 0xCA : 0xC8);
  outb(bm,write? 1 : 9);
  int ok=0; for(u32 i=0;i<20000000;i++){ u8 s=inb(bm+2); if(!(s&1)){ ok=1; break; } }
  u8 s=inb(bm+2); outb(bm,0); outb(bm+2,0x06);
  if(!ok || (s&2) || wait_bsy()) return -1;
  return (inb(0x1F7)&0x21)? -1 : 0;
}
/* up to 128 sectors per command: the drive is programmed once and then only the data port is touched per sector (one port access per sector instead of about fifteen) */
static void setup_n(u32 lba, u32 cnt, int cmd){
  outb(0x1F6,0xE0|((lba>>24)&0x0F)); outb(0x1F2,(u8)cnt);
  outb(0x1F3,lba&0xFF); outb(0x1F4,(lba>>8)&0xFF); outb(0x1F5,(lba>>16)&0xFF); outb(0x1F7,cmd);
}
int ata_read(u32 lba, u32 n, void *buf){
  if(!ata_ok) return -1;
  for(u32 done=0;done<n;){
    u32 c=n-done; if(c>128) c=128;
    { int r=dma_xfer(lba+done,c,(u8*)buf+done*512ull,0); if(r==0){ done+=c; continue; } if(r==-1) bm=0; }          /* DMA failed: use PIO from now on */
    setup_n(lba+done,c,mult? 0xC4 : 0x20); delay400();
    u32 blk=mult? mult : 1;
    for(u32 i=0;i<c;){ if(wait_bsy()||wait_drq()) return -1; u32 b=c-i<blk? c-i : blk; for(u32 k=0;k<b;k++) insw((u16*)((u8*)buf+(done+i+k)*512ull)); i+=b; }
    done+=c;
  }
  return 0;
}
int ata_write(u32 lba, u32 n, const void *buf){
  if(!ata_ok) return -1;
  for(u32 done=0;done<n;){
    u32 c=n-done; if(c>128) c=128;
    { int r=dma_xfer(lba+done,c,(void*)((const u8*)buf+done*512ull),1); if(r==0){ done+=c; continue; } if(r==-1) bm=0; }
    setup_n(lba+done,c,mult? 0xC5 : 0x30); delay400();
    u32 blk=mult? mult : 1;
    for(u32 i=0;i<c;){ if(wait_bsy()||wait_drq()) return -1; u32 b=c-i<blk? c-i : blk; for(u32 k=0;k<b;k++) outsw((const u16*)((const u8*)buf+(done+i+k)*512ull)); i+=b; }
    if(wait_bsy()) return -1;
    done+=c;
  }
  if(!n) return 0;
  outb(0x1F7,0xE7); return wait_bsy();                  /* flush cache */
}
