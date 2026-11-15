#include "kernel.h"
extern char kernel_end;
u64 kernel_end_addr, mod_lo=~0ull, mod_hi;

static int has_autostart(const char *cmd, const char *name){      /* kernel command line: autostart=a.exe,b.exe */
  if(!cmd) return 0; unsigned nl=strlen(name);
  for(const char *p=cmd; *p; p++){
    if(p[0]=='a'&&p[1]=='u'&&p[2]=='t'&&p[3]=='o'&&p[4]=='s'&&p[5]=='t'&&p[6]=='a'&&p[7]=='r'&&p[8]=='t'&&p[9]=='='){
      for(const char *q=p+10; *q && *q!=' '; ){
        const char *e=q; while(*e && *e!=',' && *e!=' ') e++;
        if((unsigned)(e-q)==nl){ unsigned i=0; while(i<nl && (q[i]|32)==(name[i]|32)) i++; if(i==nl) return 1; }
        q=*e==','? e+1 : e;
      }
    }
  }
  return 0;
}

static int has_autostart_any(const char *cmd){ for(const char *p=cmd;*p;p++) if(p[0]=='a'&&p[1]=='u'&&p[2]=='t'&&p[3]=='o'&&p[4]=='s') return 1; return 0; }

void kmain(u32 magic, u32 mbi_addr){
  struct mb_info *mbi=(struct mb_info*)(u64)mbi_addr;
  console_init();
  if(magic==0x2BADB002 && gfx_init(mbi)) console_init();   /* restart console on the framebuffer */
  kputs("Willows 0.3 (x86-64) - a tiny OS that wants to run Windows programs\n");
  if(magic!=0x2BADB002){ kprintf("Not booted by a Multiboot loader (magic=%x)\n",magic); return; }
  kernel_end_addr=(u64)&kernel_end;
  idt_init(); timer_init(); keyboard_init();
  int mouse=mouse_init(); if(mouse && gfx_ok) gfx_cursor_enable(mouse_x,mouse_y);

  struct mb_module *mods=(void*)(u64)mbi->mods_addr; u32 nmods=(mbi->flags&8)?mbi->mods_count:0;
  u64 top=kernel_end_addr;
  for(u32 i=0;i<nmods;i++){ if(mods[i].start<mod_lo) mod_lo=mods[i].start; if(mods[i].end>mod_hi) mod_hi=mods[i].end; if(mods[i].end>top) top=mods[i].end; }
  u64 memtop=0x100000+(u64)mbi->mem_upper*1024; if(memtop>0xC0000000ull) memtop=0xC0000000ull;
  u64 heap=(top+0xFFFFF)&~0xFFFFFull; if(heap<0x1000000) heap=0x1000000;
  mm_init(heap,memtop);
  /* GRUB drops the modules right behind the kernel, which blocks the usual low load addresses (0x400000 ...).
     Move them up into the heap so programs that cannot be relocated can load at their preferred base. */
  for(u32 i=0;i<nmods;i++){
    u64 n=mods[i].end-mods[i].start; void *c=kmalloc(n+16,16); if(!c) continue;
    memcpy(c,(void*)(u64)mods[i].start,n); mods[i].start=(u32)(u64)c; mods[i].end=(u32)((u64)c+n);
  }
  mod_lo=~0ull; mod_hi=0;
  mm_guard_null();                                                /* NULL pointer accesses fault from now on */
  if(mbi->flags&0x40){                                            /* memory map: usable RAM above 4 GB (QEMU puts everything beyond ~3 GB there) */
    u64 best=0, blen=0;
    for(u32 off=0; off+24<=mbi->mmap_len; ){ const u8 *e=(const u8*)(u64)(mbi->mmap_addr+off); u32 sz=*(const u32*)e;
      u64 base=*(const u64*)(e+4), len=*(const u64*)(e+12); u32 type=*(const u32*)(e+20);
      if(type==1 && base>=0x100000000ull && len>blen){ best=base; blen=len; }
      off+=sz+4; }
    if(blen) mm_high_init(best,blen);
  }
  if(gfx_ok) kprintf("Video: %ux%u framebuffer\n",gfx_w,gfx_h);
  kprintf("Mouse: %s\n",mouse? (mouse_has_wheel()? "PS/2 detected (with scroll wheel)" : "PS/2 detected") : "none");
  kprintf("Memory: %u KB upper, %u MB above 4 GB, heap at %p, %u module(s)\n",mbi->mem_upper,(u32)(mm_high_total()>>20),heap,nmods);

  fs_init();
  /* modules: .exe files become programs in the Start menu, everything else goes to the RAM filesystem */
  const char *cmd=(mbi->flags&4)? (const char*)(u64)mbi->cmdline : NULL;
  for(u32 i=0;i<nmods;i++){
    const char *cl=(const char*)(u64)mods[i].cmdline; if(!cl) continue;
    static char nm[8][64]; static int nn;                           /* first word = file name, optional "demo" tag after it */
    char *n=nm[nn++&7]; unsigned k=0; while(cl[k] && cl[k]!=' ' && k<63){ n[k]=cl[k]; k++; } n[k]=0;
    int demo=0; for(const char *t=cl+k; *t; t++) if(t[0]=='d'&&t[1]=='e'&&t[2]=='m'&&t[3]=='o'){ demo=1; break; }
    u64 l=k;
    if(l>4 && stricmp(n+l-4,".exe")==0) app_register(n,(void*)(u64)mods[i].start,mods[i].end-mods[i].start,demo);
    else fs_add(n,(void*)(u64)mods[i].start,mods[i].end-mods[i].start);
  }
  if(fs_load_disk()) kputs("Disk: saved files enabled (changes are written to the boot disk)\n");
  else kputs("Disk: no writable ATA disk, files are RAM-only\n");
  settings_load();
  kprintf("Programs: %d, files: %d\n",app_count(),fs_count());

  if(net_init()){ kputs("Network: Intel e1000 found, requesting an address (DHCP)\n"); } else kputs("Network: no supported NIC (needs Intel e1000)\n");

  procs[0].used=1; procs[0].pid=0; memcpy(procs[0].name,"System",7);
  if(gfx_ok) wm_desktop_start();

  /* programs named in autostart=... start immediately; without a window system, run them one after another */
  int seq=!gfx_ok, next=0;
  for(int i=0;i<app_count();i++){
    if(has_autostart(cmd,app_name(i)) && !seq) proc_spawn(app_name(i));
  }
  if(gfx_ok){                                                     /* C:\System\autorun.cfg: one command line, run in a terminal at boot (used by tools/dev/mktest.sh) */
    struct ramfile *ar=fs_find("System/autorun.cfg");
    if(ar && !ar->isdir && ar->size) proc_spawn_cmd("terminal","terminal -c @/System/autorun.cfg");     /* each line is run in turn */
  }
  for(;;){
    if(seq){                                                        /* text mode: run every program in order */
      if(procs_alive()==0 && next<app_count()){ if(!cmd||!has_autostart_any(cmd)||has_autostart(cmd,app_name(next))) proc_spawn(app_name(next)); next++; }
      else if(procs_alive()==0 && next>=app_count()){ kputs("\nNothing left to run. System idle.\n"); seq=0; }
    }
    if(debug_dump_request){ debug_dump_request=0; debug_dump(); }   /* Ctrl+F12 */
    fs_service();                                                   /* deferred saving of changed files (at most every 2 s) */
    wm_service();
    idle_wait();
  }
}
