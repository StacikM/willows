/* Real DLL loading: LoadLibrary/GetProcAddress/FreeLibrary and static imports of DLLs that are files in the Willows file system.
   Built-in DLLs (kernel32, user32, ...) stay built in; a DLL file is used for names the built-ins do not provide (msvcp140, app libraries, ...).
   Each process gets its own copy of a DLL (all processes share one address space, so every copy has its own base). */
#include "kernel.h"
struct mod { int used, owner, refs, inited; char key[40], path[128]; u64 base, entry; u32 exp_rva, exp_size; };
static struct mod mods[64]; static int depth;
static int lower(int c){ return c>='A'&&c<='Z'? c+32 : c; }
static void normalize(const char *name, char *out, int max){                        /* "C:\x\Foo.DLL" -> "foo" */
  for(const char *p=name;*p;p++) if(*p=='\\'||*p=='/') name=p+1;
  int n=0; for(;name[n]&&n<max-1;n++) out[n]=(char)lower(name[n]); out[n]=0;
  if(n>4 && out[n-4]=='.' && out[n-3]=='d' && out[n-2]=='l' && out[n-1]=='l') out[n-4]=0;
}
static struct mod *find_mod(int owner, const char *key){ for(int i=0;i<64;i++) if(mods[i].used && mods[i].owner==owner && !strcmp(mods[i].key,key)) return &mods[i]; return 0; }
static struct mod *mod_of(u64 h){ if(!cur) return 0; for(int i=0;i<64;i++) if(mods[i].used && mods[i].base==h && mods[i].owner==cur->pid) return &mods[i]; return 0; }
static struct ramfile *find_file(const char *name){
  static const char *const dirs[]={"","/","/Downloads/","/Documents/","/Desktop/","/System/","/System32/","/Windows/System32/"};
  char base[100]; for(const char *p=name;*p;p++) if(*p=='\\'||*p=='/') name=p+1;
  int n=0; for(;name[n]&&n<90;n++) base[n]=name[n]; base[n]=0;
  int has_ext=0; for(int i=0;i<n;i++) if(base[i]=='.') has_ext=1; if(!has_ext){ memcpy(base+n,".dll",5); }
  for(unsigned d=0;d<sizeof dirs/sizeof *dirs;d++){
    char path[128]; unsigned k=0; for(const char *s=dirs[d];*s;) path[k++]=*s++; for(const char *s=base;*s&&k<126;) path[k++]=*s++; path[k]=0;
    struct ramfile *f=fs_find(path); if(f && !f->isdir && f->size>64) return f;
  }
  return 0;
}
static struct mod *dll_load(const char *name, int owner){
  char key[40]; normalize(name,key,sizeof key); if(!key[0]) return 0;
  struct mod *m=find_mod(owner,key); if(m){ m->refs++; return m; }
  if(depth>=8) return 0;
  struct ramfile *f=find_file(name); if(!f){ kdbg("  [dll] %s: no such file\n",name); return 0; }
  int slot=-1; for(int i=0;i<64;i++) if(!mods[i].used){ slot=i; break; } if(slot<0) return 0;
  struct pe_image img; int saved=pe_owner; pe_owner=owner; depth++;
  int bad=pe_load_ex(f->data,f->size,&img,1); depth--; pe_owner=saved;
  if(bad){ kdbg("  [dll] %s: load failed\n",f->name); return 0; }
  m=&mods[slot]; memset(m,0,sizeof *m); m->used=1; m->owner=owner; m->refs=1; memcpy(m->key,key,sizeof m->key);
  unsigned k=0; for(;f->name[k]&&k<127;k++) m->path[k]=f->name[k]; m->path[k]=0;
  m->base=img.base; m->entry=img.entry; m->exp_rva=img.exp_rva; m->exp_size=img.exp_size;
  kdbg("  [dll] loaded %s at %p (pid %d)\n",f->name,(void*)m->base,owner);
  return m;
}
#pragma pack(push,1)
struct expdir { u32 chars, ts; u16 maj, min; u32 name, base, nfuncs, nnames, aof, aon, aoo; };
#pragma pack(pop)
static void *export_of(struct mod *m, const char *name, int owner, int hops);
static void *forward(const char *fwd, int owner, int hops){                            /* "OTHER.Func" or "OTHER.#12" */
  char d[40], fn[80]; int i=0; while(fwd[i] && fwd[i]!='.' && i<39){ d[i]=fwd[i]; i++; } d[i]=0; if(fwd[i]=='.') i++;
  int j=0; while(fwd[i] && j<79) fn[j++]=fwd[i++]; fn[j]=0;
  void *a=win32_resolve(d,fn); if(a) return a;
  if(hops>6 || win32_dll_is_builtin(d)) return 0;
  struct mod *o=dll_load(d,owner); return o? export_of(o,fn,owner,hops+1) : 0;
}
static void *export_of(struct mod *m, const char *name, int owner, int hops){
  if(!m->exp_rva) return 0;
  u8 *b=(u8*)m->base; const struct expdir *e=(void*)(b+m->exp_rva);
  u32 *aof=(u32*)(b+e->aof), *aon=(u32*)(b+e->aon); u16 *aoo=(u16*)(b+e->aoo); int idx=-1;
  u64 want=0; int byord=0;
  if(name[0]=='#'){ byord=1; for(const char *p=name+1;*p>='0'&&*p<='9';p++) want=want*10+(*p-'0'); }
  else if((u64)name<0x10000){ byord=1; want=(u64)name; }
  if(byord){ if(want>=e->base && want-e->base<e->nfuncs) idx=(int)(want-e->base); }
  else for(u32 i=0;i<e->nnames;i++) if(!strcmp((const char*)(b+aon[i]),name)){ idx=aoo[i]; break; }
  if(idx<0 || (u32)idx>=e->nfuncs) return 0;
  u32 rva=aof[idx]; if(!rva) return 0;
  if(rva>=m->exp_rva && rva<m->exp_rva+m->exp_size) return forward((const char*)(b+rva),owner,hops);
  return b+rva;
}
/* ---- used by the PE loader for static imports ---- */
void *dll_import(const char *dll, const char *fn, int owner){
  if(win32_dll_is_builtin(dll)) return 0;
  struct mod *m=dll_load(dll,owner); return m? export_of(m,fn,owner,0) : 0;
}
void dll_init_owner(int owner){                                                       /* DllMain(DLL_PROCESS_ATTACH) in load order */
  for(int i=0;i<64;i++){ struct mod *m=&mods[i]; if(!m->used||m->owner!=owner||m->inited) continue; m->inited=1;
    if(m->entry){ int ok=((int (WINAPI*)(u64,u32,void*))m->entry)(m->base,1,0); if(!ok) kdbg("  [dll] %s: DllMain returned FALSE\n",m->path); } }
}
void dll_exit(int owner){                                                              /* DLL_PROCESS_DETACH in reverse order, then free */
  for(int i=63;i>=0;i--){ struct mod *m=&mods[i]; if(m->used&&m->owner==owner&&m->inited&&m->entry) ((int (WINAPI*)(u64,u32,void*))m->entry)(m->base,0,(void*)1); }
  dll_cleanup(owner);
}
void dll_cleanup(int owner){ for(int i=0;i<64;i++) if(mods[i].used&&mods[i].owner==owner){ pe_unload(mods[i].base); mods[i].used=0; } }
/* ---- kernel32 side ---- */
u64 dll_loadlib(const char *name){
  if(!cur||!name) return 0; if(win32_dll_is_builtin(name)) return 0;
  struct mod *m=dll_load(name,cur->pid); if(!m) return 0;
  dll_init_owner(cur->pid); return m->base;
}
u64 dll_modhandle(const char *name){ if(!cur||!name) return 0; char key[40]; normalize(name,key,sizeof key); struct mod *m=find_mod(cur->pid,key); return m? m->base : 0; }
void *dll_getproc(u64 h, const char *name, int *real){ struct mod *m=mod_of(h); *real=m!=0; return m? export_of(m,name,cur->pid,0) : 0; }
const char *dll_modpath(u64 h){ struct mod *m=mod_of(h); return m? m->path : 0; }
int dll_freelib(u64 h){ struct mod *m=mod_of(h); if(!m) return 1; if(--m->refs>0) return 1;
  if(m->inited&&m->entry) ((int (WINAPI*)(u64,u32,void*))m->entry)(m->base,0,0);
  pe_unload(m->base); m->used=0; return 1; }
