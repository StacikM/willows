#include "kernel.h"
/* Restart and power-off. Power-off parses just enough ACPI (RSDP -> RSDT -> FADT -> DSDT "_S5_") to enter sleep state S5;
   emulator-specific ports are tried as fallbacks. */
static inline void outw(u16 p, u16 v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline u16 inw(u16 p){ u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static int sig(const void *a, const char *s, int n){ const char *p=a; for(int i=0;i<n;i++) if(p[i]!=s[i]) return 0; return 1; }
static int csum(const u8 *p, u32 n){ u8 s=0; while(n--) s+=*p++; return s==0; }

static const u8 *find_rsdp(void){
  extern u16 bda_ebda_seg; const u8 *e=(const u8*)(u64)(bda_ebda_seg<<4);           /* saved at boot: page 0 is unmapped (mm_guard_null) */
  for(const u8 *p=e; e && p<e+1024; p+=16) if(sig(p,"RSD PTR ",8) && csum(p,20)) return p;
  for(const u8 *p=(const u8*)0xE0000; p<(const u8*)0x100000; p+=16) if(sig(p,"RSD PTR ",8) && csum(p,20)) return p;
  return NULL;
}
static void acpi_off(void){
  const u8 *r=find_rsdp(); if(!r) return;
  const u8 *rsdt=(const u8*)(u64)*(u32*)(r+16); if(!sig(rsdt,"RSDT",4)) return;
  u32 n=(*(u32*)(rsdt+4)-36)/4;
  for(u32 i=0;i<n;i++){
    const u8 *t=(const u8*)(u64)((u32*)(rsdt+36))[i]; if(!sig(t,"FACP",4)) continue;
    u32 dsdt=*(u32*)(t+40), pm1a=*(u32*)(t+64), pm1b=*(u32*)(t+68), smi=*(u32*)(t+48); u8 en=t[52];
    if(!(inw(pm1a)&1) && smi && en){ outb(smi,en); for(int k=0;k<3000000 && !(inw(pm1a)&1);k++); }   /* enable ACPI mode */
    const u8 *d=(const u8*)(u64)dsdt; if(!sig(d,"DSDT",4)) return;
    u32 len=*(u32*)(d+4);
    for(const u8 *p=d+36; p<d+len-8; p++){
      if(!sig(p,"_S5_",4)) continue;
      if(!(p[-1]==0x08 || (p[-2]==0x08 && p[-1]=='\\')) || p[4]!=0x12) continue;
      const u8 *q=p+5; q+=((*q&0xC0)>>6)+2;
      if(*q==0x0A) q++; u16 a=*q<<10; q++;
      if(*q==0x0A) q++; u16 b=*q<<10;
      outw(pm1a,a|(1<<13)); if(pm1b) outw(pm1b,b|(1<<13));
      return;
    }
    return;
  }
}
void power_off(void){
  fs_flush();                                                         /* changes not saved yet (saving is deferred up to 2 s) */
  __asm__ volatile("cli");
  acpi_off();
  outw(0x604,0x2000); outw(0xB004,0x2000); outw(0x4004,0x3400);     /* QEMU / Bochs / VirtualBox */
  gfx_rect(0,0,gfx_w,gfx_h,0); gfx_text(gfx_w/2-136,gfx_h/2-8,"It is now safe to turn off your computer.",41,0xFF9900,0,0);
  kputs("\nSystem halted. It is now safe to turn off your computer.\n");
  for(;;) __asm__ volatile("hlt");
}
void power_restart(void){
  fs_flush();
  __asm__ volatile("cli");
  for(int i=0;i<100000 && (inb(0x64)&2);i++);
  outb(0x64,0xFE);                                                   /* pulse CPU reset via the keyboard controller */
  for(volatile int i=0;i<1000000;i++);
  struct { u16 l; u64 b; } __attribute__((packed)) z={0,0};          /* fallback: triple fault */
  __asm__ volatile("lidt %0; int3"::"m"(z));
  for(;;) __asm__ volatile("hlt");
}
