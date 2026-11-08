/* OS settings: a small key=value store kept in System\settings.cfg. Applies the keyboard layout and wallpaper theme live. */
#include "kernel.h"
#define NSET 16
static struct { char k[24], v[40]; } set[NSET]; static int nset;
void kb_set_layout(int l); int kb_get_layout(void); void wm_set_theme(int t); int wm_get_theme(void); void fs_flush(void);
int fs_append(struct ramfile *f, const void *d, u64 n);
static int ieq(const char *a, const char *b){ return stricmp(a,b)==0; }
const char *settings_get(const char *k){ for(int i=0;i<nset;i++) if(ieq(set[i].k,k)) return set[i].v; return 0; }
static void apply(const char *k, const char *v){
  if(ieq(k,"layout")) kb_set_layout(ieq(v,"azerty")||ieq(v,"fr") ? 1 : 0);
  else if(ieq(k,"theme")){ int t=0; for(const char *p=v;*p>='0'&&*p<='9';p++) t=t*10+(*p-'0'); wm_set_theme(t); }
}
static void store(const char *k, const char *v){
  int i=0; for(;i<nset;i++) if(ieq(set[i].k,k)) break;
  if(i==nset){ if(nset==NSET) return; nset++; }
  unsigned n=0; for(;k[n]&&n<23;n++) set[i].k[n]=k[n]; set[i].k[n]=0;
  n=0; for(;v[n]&&n<39;n++) set[i].v[n]=v[n]; set[i].v[n]=0;
}
static void save(void){
  static char buf[NSET*70]; int n=0;
  for(int i=0;i<nset;i++){ for(const char *p=set[i].k;*p;p++) buf[n++]=*p; buf[n++]='='; for(const char *p=set[i].v;*p;p++) buf[n++]=*p; buf[n++]='\n'; }
  fs_mkdir("System"); struct ramfile *f=fs_create("System/settings.cfg"); if(!f) return;
  fs_append(f,buf,n); f->persist=1; f->dirty=1; fs_flush();
}
void settings_set(const char *k, const char *v){ store(k,v); apply(k,v); save(); }
void settings_load(void){
  struct ramfile *f=fs_find("System/settings.cfg"); if(!f||f->isdir) return;
  char *d=(char*)f->data; u64 sz=f->size, i=0;
  while(i<sz){
    char k[24], v[40]; int kn=0, vn=0; while(i<sz && d[i]!='=' && d[i]!='\n'){ if(kn<23) k[kn++]=d[i]; i++; }
    if(i<sz && d[i]=='='){ i++; while(i<sz && d[i]!='\n' && d[i]!='\r'){ if(vn<39) v[vn++]=d[i]; i++; } }
    while(i<sz && d[i]!='\n') i++; i++;
    k[kn]=0; v[vn]=0; if(kn){ store(k,v); apply(k,v); }
  }
}
/* kernel32 exports for the Settings app */
static int WINAPI k_WillowsSetSetting(const char *k, const char *v){ if(!k||!v) return 0; settings_set(k,v); return 1; }
static int WINAPI k_WillowsGetSetting(const char *k, char *out, unsigned n){ const char *v=k?settings_get(k):0; if(!v||!out||!n) return 0; unsigned i=0; for(;v[i]&&i+1<n;i++) out[i]=v[i]; out[i]=0; return (int)i; }
static int WINAPI k_WillowsGetGpuName(char *out, unsigned n){ const char *g=hw_gpu_name(); unsigned i=0; for(;g[i]&&i+1<n;i++) out[i]=g[i]; out[i]=0; return (int)i; }
static u64 WINAPI k_WillowsGetMemory(int which){ return which ? mm_free() : mm_total(); }
static int WINAPI k_WillowsGetResolution(void){ return (gfx_w<<16)|gfx_h; }
#define S(n) {"kernel32", #n, k_##n}
const struct export settings_exports[] = { S(WillowsSetSetting), S(WillowsGetSetting), S(WillowsGetGpuName), S(WillowsGetMemory), S(WillowsGetResolution) };
const unsigned settings_nexports = sizeof settings_exports / sizeof *settings_exports;
