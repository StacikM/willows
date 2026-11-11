#include "kernel.h"
/* An in-memory Windows registry (advapi32 Reg* functions). Keys form a tree; values hold typed data.
   Strings are stored as UTF-16 so the A and W functions see the same data. The hive starts with the values programs
   commonly read (Windows version, computer name, CPU name ...) and is not saved between boots yet. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE;
#define MAXK 768
#define MAXV 3072
struct rk { char name[64]; int parent, child, sibling, vfirst; u8 used; };
struct rv { char name[64]; u32 type, len; u8 *data; int next; u8 used; };
static struct rk K[MAXK]; static struct rv V[MAXV]; static int seeded;
#define HK_BASE 0x7200
#define ERR_FILE_NOT_FOUND 2
#define ERR_INVALID_HANDLE 6
#define ERR_MORE_DATA 234
#define ERR_NO_MORE_ITEMS 259
#define ERR_UNSUPPORTED_TYPE 1630
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_MULTI_SZ 7
#define REG_DWORD 4

static int wlen(const u16 *s){ int n=0; while(s&&s[n]) n++; return n; }
static void w2a(const u16 *w, char *a, int n){ int i=0; for(;w&&w[i]&&i<n-1;i++) a[i]= w[i]<0x80? (char)w[i] : '?'; a[i]=0; }
static int is_str(u32 t){ return t==REG_SZ||t==REG_EXPAND_SZ||t==REG_MULTI_SZ; }

static int key_new(const char *name, int parent){
  for(int i=6;i<MAXK;i++) if(!K[i].used){
    struct rk *k=&K[i]; memset(k,0,sizeof *k); k->used=1; k->parent=parent; k->child=k->sibling=k->vfirst=-1;
    unsigned n=0; for(;name[n]&&n<63;n++) k->name[n]=name[n]; k->name[n]=0;
    if(parent>=0){ if(K[parent].child<0) K[parent].child=i; else { int s=K[parent].child; while(K[s].sibling>=0) s=K[s].sibling; K[s].sibling=i; } }
    return i;
  }
  return -1;
}
static void init_roots(void){
  static const char *const rn[6]={"HKEY_CLASSES_ROOT","HKEY_CURRENT_USER","HKEY_LOCAL_MACHINE","HKEY_USERS","","HKEY_CURRENT_CONFIG"};
  for(int i=0;i<6;i++){ struct rk *k=&K[i]; memset(k,0,sizeof *k); k->used=1; k->parent=-1; k->child=k->sibling=k->vfirst=-1; unsigned n=0; for(;rn[i][n];n++) k->name[n]=rn[i][n]; }
}
static int key_of(HANDLE h){
  if(h>=0x80000000ull && h<=0x80000005ull){ int i=(int)(h&7); return i==4? -1 : i; }
  if(h>=HK_BASE && h<HK_BASE+MAXK){ int i=(int)(h-HK_BASE); return K[i].used? i : -1; }
  return -1;
}
static HANDLE hk(int idx){ return idx<6? 0x80000000ull+idx : HK_BASE+idx; }
/* follow a backslash separated path below key k; create missing keys when asked */
static int walk(int k, const char *path, int create, int *created){
  if(created) *created=0;
  while(k>=0 && path && *path){
    while(*path=='\\'||*path=='/') path++; if(!*path) break;
    char comp[64]; int n=0; while(*path && *path!='\\' && *path!='/'){ if(n<63) comp[n++]=*path; path++; } comp[n]=0;
    int c=K[k].child; while(c>=0 && stricmp(K[c].name,comp)) c=K[c].sibling;
    if(c<0){ if(!create) return -1; c=key_new(comp,k); if(c<0) return -1; if(created) *created=1; }
    k=c;
  }
  return k;
}
static struct rv *val_find(int k, const char *name){ for(int v=K[k].vfirst; v>=0; v=V[v].next) if(!stricmp(V[v].name,name)) return &V[v]; return 0; }
static int val_index(struct rv *v){ return (int)(v-V); }
static int val_set(int k, const char *name, u32 type, const void *data, u32 len){
  struct rv *v=val_find(k,name);
  if(!v){ int i=0; for(;i<MAXV;i++) if(!V[i].used) break; if(i==MAXV) return 8; v=&V[i]; memset(v,0,sizeof *v); v->used=1; v->next=-1; unsigned n=0; for(;name[n]&&n<63;n++) v->name[n]=name[n];
    if(K[k].vfirst<0) K[k].vfirst=i; else { int s=K[k].vfirst; while(V[s].next>=0) s=V[s].next; V[s].next=i; } }
  heap_free(v->data); v->data=0; v->type=type; v->len=len;
  if(len){ v->data=heap_alloc(len+2); if(!v->data){ v->len=0; return 14; } memcpy(v->data,data,len); v->data[len]=0; v->data[len+1]=0; }
  return 0;
}
static void val_drop(int k, struct rv *v){
  int idx=val_index(v);
  if(K[k].vfirst==idx) K[k].vfirst=v->next; else for(int s=K[k].vfirst; s>=0; s=V[s].next) if(V[s].next==idx){ V[s].next=v->next; break; }
  heap_free(v->data); memset(v,0,sizeof *v);
}
static void key_drop(int k);
static void key_clear(int k){                                                    /* values and all subkeys */
  while(K[k].vfirst>=0) val_drop(k,&V[K[k].vfirst]);
  while(K[k].child>=0) key_drop(K[k].child);
}
static void key_drop(int k){
  key_clear(k); int p=K[k].parent;
  if(p>=0){ if(K[p].child==k) K[p].child=K[k].sibling; else for(int s=K[p].child; s>=0; s=K[s].sibling) if(K[s].sibling==k){ K[s].sibling=K[k].sibling; break; } }
  memset(&K[k],0,sizeof K[k]);
}

/* ---- seed data ---- */
static void set_s(const char *path, const char *name, const char *val){
  int k=walk(2,path,1,0); if(k<0) return; u16 w[160]; int n=0; for(;val[n]&&n<158;n++) w[n]=(u8)val[n]; w[n]=0; val_set(k,name,REG_SZ,w,(u32)(n+1)*2);
}
static void set_d(const char *path, const char *name, u32 v){ int k=walk(2,path,1,0); if(k>=0) val_set(k,name,REG_DWORD,&v,4); }
static void cpu_brand(char *out){
  u32 a,b,c,d; __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000000u));
  if(a<0x80000004u){ const char *d="Willows virtual CPU"; memcpy(out,d,20); return; }
  for(u32 i=0;i<3;i++){ __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000002u+i)); memcpy(out+i*16,&a,4); memcpy(out+i*16+4,&b,4); memcpy(out+i*16+8,&c,4); memcpy(out+i*16+12,&d,4); }
  out[48]=0; char *p=out; while(*p==' ') p++; if(p!=out){ char *q=out; while((*q++=*p++)); }
}
static void seed(void){
  if(seeded) return; seeded=1; init_roots();
  const char *nt="SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  set_s(nt,"ProductName","Windows 10 Pro"); set_s(nt,"EditionID","Professional"); set_s(nt,"CurrentVersion","6.3"); set_s(nt,"CurrentBuild","19045"); set_s(nt,"CurrentBuildNumber","19045");
  set_s(nt,"DisplayVersion","22H2"); set_s(nt,"ReleaseId","2009"); set_s(nt,"BuildLab","19041.vb_release.191206-1406"); set_s(nt,"RegisteredOwner","user"); set_s(nt,"SystemRoot","C:\\Windows");
  set_s(nt,"InstallationType","Client"); set_s(nt,"BuildBranch","vb_release");
  set_d(nt,"CurrentMajorVersionNumber",10); set_d(nt,"CurrentMinorVersionNumber",0); set_d(nt,"UBR",3803);
  const char *cv="SOFTWARE\\Microsoft\\Windows\\CurrentVersion";
  set_s(cv,"ProgramFilesDir","C:\\Program Files"); set_s(cv,"ProgramFilesDir (x86)","C:\\Program Files (x86)"); set_s(cv,"CommonFilesDir","C:\\Program Files\\Common Files"); set_s(cv,"ProgramW6432Dir","C:\\Program Files");
  set_s("SOFTWARE\\Microsoft\\Cryptography","MachineGuid","7b1f6c2e-5d0a-4d3f-9a11-0a2b3c4d5e6f");
  set_s("SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName","ComputerName","WILLOWS");
  set_s("SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName","ComputerName","WILLOWS");
  set_d("SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage","ACP",0); set_s("SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage","ACP","1252"); set_s("SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage","OEMCP","437");
  set_s("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment","PROCESSOR_ARCHITECTURE","AMD64");
  char brand[64]; cpu_brand(brand);
  const char *cp="HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
  set_s(cp,"ProcessorNameString",brand); set_s(cp,"Identifier","Intel64 Family 6 Model 158 Stepping 10"); set_s(cp,"VendorIdentifier","Willows"); set_d(cp,"~MHz",2400);
  const char *bios="HARDWARE\\DESCRIPTION\\System\\BIOS";
  set_s(bios,"SystemManufacturer","Willows"); set_s(bios,"SystemProductName","Willows PC"); set_s(bios,"BIOSVendor","Willows"); set_s(bios,"BIOSVersion","1.0");
  set_s("HARDWARE\\DESCRIPTION\\System","Identifier","AT/AT COMPATIBLE");
  { int h=walk(1,"Environment",1,0); (void)h; walk(1,"Control Panel\\Desktop",1,0); walk(1,"Software",1,0); }
}

/* ---- core operations (names are ASCII; `wide` selects how string data is exchanged) ---- */
static long r_open(HANDLE hkey, const char *sub, int create, HANDLE *out, DWORD *disp){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE; int made=0;
  int r=walk(k,sub,create,&made); if(r<0){ if(out) *out=0; return create? 8 : ERR_FILE_NOT_FOUND; }
  if(out) *out=hk(r); if(disp) *disp= made? 1 : 2; return 0;
}
static long r_query(HANDLE hkey, const char *name, int wide, DWORD *type, u8 *buf, DWORD *len){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE;
  struct rv *v=val_find(k,name? name : ""); if(!v) return ERR_FILE_NOT_FOUND;
  if(type) *type=v->type;
  DWORD need= (is_str(v->type) && !wide)? v->len/2 : v->len;
  if(!len) return 0; if(!buf){ *len=need; return 0; } if(*len<need){ *len=need; return ERR_MORE_DATA; }
  if(is_str(v->type) && !wide){ const u16 *w=(const u16*)v->data; for(DWORD i=0;i<need;i++) buf[i]= w[i]<0x80? (u8)w[i] : '?'; }
  else if(need) memcpy(buf,v->data,need);
  *len=need; return 0;
}
static long r_set(HANDLE hkey, const char *name, int wide, DWORD type, const u8 *data, DWORD len){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE;
  if(is_str(type) && !wide){ u16 *w=heap_alloc((len+1)*2); if(!w) return 14; for(DWORD i=0;i<len;i++) w[i]=data[i]; long r=val_set(k,name? name : "",type,w,len*2); heap_free(w); return r; }
  return val_set(k,name? name : "",type,data,len);
}
static void put_name(char *dst_a, u16 *dst_w, int wide, const char *src, DWORD *nlen){
  int n=(int)strlen(src); if(wide){ for(int i=0;i<=n;i++) dst_w[i]=(u8)src[i]; } else memcpy(dst_a,src,n+1); *nlen=(DWORD)n;
}
static long r_enum_key(HANDLE hkey, DWORD idx, int wide, void *name, DWORD *nlen){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE;
  int c=K[k].child; while(c>=0 && idx--) c=K[c].sibling; if(c<0) return ERR_NO_MORE_ITEMS;
  DWORD n=(DWORD)strlen(K[c].name); if(!nlen||!name||*nlen<=n){ if(nlen) *nlen=n+1; return ERR_MORE_DATA; }
  put_name(name,name,wide,K[c].name,nlen); return 0;
}
static long r_enum_val(HANDLE hkey, DWORD idx, int wide, void *name, DWORD *nlen, DWORD *type, u8 *data, DWORD *dlen){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE;
  int v=K[k].vfirst; while(v>=0 && idx--) v=V[v].next; if(v<0) return ERR_NO_MORE_ITEMS;
  DWORD n=(DWORD)strlen(V[v].name); if(!nlen||!name||*nlen<=n){ if(nlen) *nlen=n+1; return ERR_MORE_DATA; }
  if(type) *type=V[v].type;
  if(dlen){ DWORD need=(is_str(V[v].type)&&!wide)? V[v].len/2 : V[v].len; if(data){ if(*dlen<need){ *dlen=need; return ERR_MORE_DATA; }
      if(is_str(V[v].type)&&!wide){ const u16 *w=(const u16*)V[v].data; for(DWORD i=0;i<need;i++) data[i]=w[i]<0x80? (u8)w[i] : '?'; } else if(need) memcpy(data,V[v].data,need); } *dlen=need; }
  put_name(name,name,wide,V[v].name,nlen); return 0;
}
static long r_info(HANDLE hkey, DWORD *nsub, DWORD *maxsub, DWORD *nval, DWORD *maxvn, DWORD *maxvd){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE;
  DWORD cs=0,ms=0,cv=0,mn=0,md=0;
  for(int c=K[k].child;c>=0;c=K[c].sibling){ cs++; DWORD l=(DWORD)strlen(K[c].name); if(l>ms) ms=l; }
  for(int v=K[k].vfirst;v>=0;v=V[v].next){ cv++; DWORD l=(DWORD)strlen(V[v].name); if(l>mn) mn=l; if(V[v].len>md) md=V[v].len; }
  if(nsub) *nsub=cs; if(maxsub) *maxsub=ms; if(nval) *nval=cv; if(maxvn) *maxvn=mn; if(maxvd) *maxvd=md; return 0;
}
static long r_delkey(HANDLE hkey, const char *sub){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE; int t=walk(k,sub,0,0); if(t<0||t<6) return ERR_FILE_NOT_FOUND;
  if(K[t].child>=0) return 5;                                                            /* RegDeleteKey refuses keys that have subkeys */
  key_drop(t); return 0;
}
static long r_deltree(HANDLE hkey, const char *sub){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE; int t=sub&&*sub? walk(k,sub,0,0) : k; if(t<0) return ERR_FILE_NOT_FOUND;
  if(t==k){ key_clear(t); return 0; } if(t<6) return 5; key_drop(t); return 0;
}
static long r_getvalue(HANDLE hkey, const char *sub, const char *name, DWORD flags, int wide, DWORD *type, u8 *data, DWORD *len){
  seed(); int k=key_of(hkey); if(k<0) return ERR_INVALID_HANDLE;
  if(sub&&*sub){ k=walk(k,sub,0,0); if(k<0) return ERR_FILE_NOT_FOUND; }
  struct rv *v=val_find(k,name? name : ""); if(!v) return ERR_FILE_NOT_FOUND;
  static const u32 bit[12]={1,2,4,8,0x10,0,0,0x20,0,0,0,0x40};
  u32 want=flags&0xFFFF; u32 have= v->type<12? bit[v->type] : 0; if(want && have && !(want&have)) return ERR_UNSUPPORTED_TYPE;
  return r_query(hk(k),name,wide,type,data,len);
}

/* ---- exported wrappers ---- */
#define L long WINAPI
static L r_RegOpenKeyExA(HANDLE k, const char *s, DWORD o, DWORD a, HANDLE *r){ (void)o;(void)a; return r_open(k,s,0,r,0); }
static L r_RegOpenKeyExW(HANDLE k, const u16 *s, DWORD o, DWORD a, HANDLE *r){ (void)o;(void)a; char t[260]; w2a(s,t,260); return r_open(k,t,0,r,0); }
static L r_RegOpenKeyA(HANDLE k, const char *s, HANDLE *r){ return r_open(k,s,0,r,0); }
static L r_RegOpenKeyW(HANDLE k, const u16 *s, HANDLE *r){ char t[260]; w2a(s,t,260); return r_open(k,t,0,r,0); }
static L r_RegCreateKeyExA(HANDLE k, const char *s, DWORD r, char *c, DWORD o, DWORD a, void *sa, HANDLE *res, DWORD *d){ (void)r;(void)c;(void)o;(void)a;(void)sa; return r_open(k,s,1,res,d); }
static L r_RegCreateKeyExW(HANDLE k, const u16 *s, DWORD r, u16 *c, DWORD o, DWORD a, void *sa, HANDLE *res, DWORD *d){ (void)r;(void)c;(void)o;(void)a;(void)sa; char t[260]; w2a(s,t,260); return r_open(k,t,1,res,d); }
static L r_RegCreateKeyA(HANDLE k, const char *s, HANDLE *r){ return r_open(k,s,1,r,0); }
static L r_RegCreateKeyW(HANDLE k, const u16 *s, HANDLE *r){ char t[260]; w2a(s,t,260); return r_open(k,t,1,r,0); }
static L r_RegCloseKey(HANDLE k){ (void)k; return 0; }
static L r_RegFlushKey(HANDLE k){ (void)k; return 0; }
static L r_RegQueryValueExA(HANDLE k, const char *n, DWORD *rs, DWORD *t, u8 *d, DWORD *l){ (void)rs; return r_query(k,n,0,t,d,l); }
static L r_RegQueryValueExW(HANDLE k, const u16 *n, DWORD *rs, DWORD *t, u8 *d, DWORD *l){ (void)rs; char a[260]; w2a(n,a,260); return r_query(k,a,1,t,d,l); }
static L r_RegSetValueExA(HANDLE k, const char *n, DWORD r, DWORD t, const u8 *d, DWORD l){ (void)r; return r_set(k,n,0,t,d,l); }
static L r_RegSetValueExW(HANDLE k, const u16 *n, DWORD r, DWORD t, const u8 *d, DWORD l){ (void)r; char a[260]; w2a(n,a,260); return r_set(k,a,1,t,d,l); }
static L r_RegSetKeyValueA(HANDLE k, const char *sub, const char *n, DWORD t, const u8 *d, DWORD l){ HANDLE h; long e=r_open(k,sub,1,&h,0); return e? e : r_set(h,n,0,t,d,l); }
static L r_RegSetKeyValueW(HANDLE k, const u16 *sub, const u16 *n, DWORD t, const u8 *d, DWORD l){ char s[260],a[260]; w2a(sub,s,260); w2a(n,a,260); HANDLE h; long e=r_open(k,s,1,&h,0); return e? e : r_set(h,a,1,t,d,l); }
static L r_RegGetValueA(HANDLE k, const char *s, const char *n, DWORD f, DWORD *t, u8 *d, DWORD *l){ return r_getvalue(k,s,n,f,0,t,d,l); }
static L r_RegGetValueW(HANDLE k, const u16 *s, const u16 *n, DWORD f, DWORD *t, u8 *d, DWORD *l){ char a[260],b[260]; w2a(s,a,260); w2a(n,b,260); return r_getvalue(k,a,b,f,1,t,d,l); }
static L r_RegDeleteValueA(HANDLE k, const char *n){ seed(); int i=key_of(k); if(i<0) return ERR_INVALID_HANDLE; struct rv *v=val_find(i,n? n : ""); if(!v) return ERR_FILE_NOT_FOUND; val_drop(i,v); return 0; }
static L r_RegDeleteValueW(HANDLE k, const u16 *n){ char a[260]; w2a(n,a,260); return r_RegDeleteValueA(k,a); }
static L r_RegDeleteKeyA(HANDLE k, const char *s){ return r_delkey(k,s); }
static L r_RegDeleteKeyW(HANDLE k, const u16 *s){ char a[260]; w2a(s,a,260); return r_delkey(k,a); }
static L r_RegDeleteKeyExA(HANDLE k, const char *s, DWORD v, DWORD r){ (void)v;(void)r; return r_delkey(k,s); }
static L r_RegDeleteKeyExW(HANDLE k, const u16 *s, DWORD v, DWORD r){ (void)v;(void)r; char a[260]; w2a(s,a,260); return r_delkey(k,a); }
static L r_RegDeleteTreeA(HANDLE k, const char *s){ return r_deltree(k,s); }
static L r_RegDeleteTreeW(HANDLE k, const u16 *s){ char a[260]; if(s) w2a(s,a,260); else a[0]=0; return r_deltree(k,a); }
static L r_RegEnumKeyExA(HANDLE k, DWORD i, char *n, DWORD *nl, DWORD *r, char *c, DWORD *cl, void *ft){ (void)r;(void)c;(void)ft; if(cl) *cl=0; return r_enum_key(k,i,0,n,nl); }
static L r_RegEnumKeyExW(HANDLE k, DWORD i, u16 *n, DWORD *nl, DWORD *r, u16 *c, DWORD *cl, void *ft){ (void)r;(void)c;(void)ft; if(cl) *cl=0; return r_enum_key(k,i,1,n,nl); }
static L r_RegEnumKeyA(HANDLE k, DWORD i, char *n, DWORD l){ DWORD nl=l; return r_enum_key(k,i,0,n,&nl); }
static L r_RegEnumKeyW(HANDLE k, DWORD i, u16 *n, DWORD l){ DWORD nl=l; return r_enum_key(k,i,1,n,&nl); }
static L r_RegEnumValueA(HANDLE k, DWORD i, char *n, DWORD *nl, DWORD *r, DWORD *t, u8 *d, DWORD *dl){ (void)r; return r_enum_val(k,i,0,n,nl,t,d,dl); }
static L r_RegEnumValueW(HANDLE k, DWORD i, u16 *n, DWORD *nl, DWORD *r, DWORD *t, u8 *d, DWORD *dl){ (void)r; return r_enum_val(k,i,1,n,nl,t,d,dl); }
static L r_RegQueryInfoKeyA(HANDLE k, char *c, DWORD *cl, DWORD *r, DWORD *ns, DWORD *ms, DWORD *mc, DWORD *nv, DWORD *mvn, DWORD *mvd, DWORD *sd, void *ft){ (void)c;(void)r;(void)sd;(void)ft; if(cl) *cl=0; if(mc) *mc=0; return r_info(k,ns,ms,nv,mvn,mvd); }
static L r_RegQueryInfoKeyW(HANDLE k, u16 *c, DWORD *cl, DWORD *r, DWORD *ns, DWORD *ms, DWORD *mc, DWORD *nv, DWORD *mvn, DWORD *mvd, DWORD *sd, void *ft){ (void)c;(void)r;(void)sd;(void)ft; if(cl) *cl=0; if(mc) *mc=0; return r_info(k,ns,ms,nv,mvn,mvd); }
static L r_RegNotifyChangeKeyValue(HANDLE k, BOOL sub, DWORD f, HANDLE ev, BOOL async){ (void)k;(void)sub;(void)f;(void)ev;(void)async; return 0; }
static L r_RegOverridePredefKey(HANDLE k, HANDLE n){ (void)k;(void)n; return 0; }
static L r_RegQueryValueA(HANDLE k, const char *sub, char *d, i32 *l){ HANDLE h; long e=r_open(k,sub,0,&h,0); if(e) return e; DWORD n= l? (DWORD)*l : 0; e=r_query(h,"",0,0,(u8*)d,&n); if(l) *l=(i32)n; return e; }

/* ---- the native (ntdll) registry API: NtOpenKey, NtQueryValueKey, ... Programs such as fastfetch use it instead of advapi32 ---- */
#define ST_OK 0
#define ST_OVERFLOW 0x80000005
#define ST_NO_MORE 0x8000001A
#define ST_INVALID_HANDLE 0xC0000008
#define ST_NOT_FOUND 0xC0000034
#define ST_PATH_NOT_FOUND 0xC000003A
#define ST_INVALID_CLASS 0xC0000003
#define ST_NO_MEMORY 0xC0000017
static void us2a(const u8 *us, char *out, int n){ int i=0; if(us){ u16 len=*(const u16*)us; const u16 *b=*(const u16*const*)(us+8); for(;i<len/2&&i<n-1;i++) out[i]=b[i]<0x80? (char)b[i] : '?'; } out[i]=0; }
static int starts_ci(const char *s, const char *p){ while(*p){ if((*s|32)!=(*p|32)) return 0; s++; p++; } return 1; }
static u32 nt_resolve(const u8 *oa, int *base, char *rest, int rmax){        /* OBJECT_ATTRIBUTES -> (key index, relative path) */
  u64 root=*(const u64*)(oa+8); char t[400]; us2a(*(const u8*const*)(oa+16),t,sizeof t); const char *p=t;
  if(root){ int k=key_of(root); if(k<0) return ST_INVALID_HANDLE; *base=k; while(*p=='\\') p++; int n=0; for(;p[n]&&n<rmax-1;n++) rest[n]=p[n]; rest[n]=0; return ST_OK; }
  if(!starts_ci(p,"\\Registry\\")) return ST_PATH_NOT_FOUND; p+=10;
  if(starts_ci(p,"Machine")){ *base=2; p+=7; }
  else if(starts_ci(p,"User")){ *base=1; p+=4; if(*p=='\\'){ p++; while(*p && *p!='\\') p++; } }                     /* \Registry\User\<sid> is the current user */
  else return ST_PATH_NOT_FOUND;
  while(*p=='\\') p++; int n=0; for(;p[n]&&n<rmax-1;n++) rest[n]=p[n]; rest[n]=0; return ST_OK;
}
static long WINAPI nt_NtOpenKey(u64 *out, DWORD acc, const u8 *oa){ (void)acc; seed(); int base; char rest[400]; u32 s=nt_resolve(oa,&base,rest,sizeof rest); if(s) return (long)s;
  int k=walk(base,rest,0,0); if(k<0) return (long)ST_NOT_FOUND; *out=hk(k); return 0; }
static long WINAPI nt_NtCreateKey(u64 *out, DWORD acc, const u8 *oa, DWORD ti, void *cls, DWORD opt, DWORD *disp){ (void)acc;(void)ti;(void)cls;(void)opt; seed(); int base; char rest[400]; u32 s=nt_resolve(oa,&base,rest,sizeof rest); if(s) return (long)s;
  int made=0; int k=walk(base,rest,1,&made); if(k<0) return (long)ST_NO_MEMORY; *out=hk(k); if(disp) *disp= made? 1 : 2; return 0; }
static long WINAPI nt_RtlOpenCurrentUser(DWORD acc, u64 *out){ (void)acc; seed(); *out=0x80000001ull; return 0; }
static u32 build_value(struct rv *v, int cls, u8 *buf, u32 len, u32 *res){
  u32 nl=(u32)strlen(v->name), nb=nl*2, need, off=0;
  if(cls==0) need=12+nb; else if(cls==1){ off=(20+nb+7)&~7u; need=off+v->len; } else if(cls==2) need=12+v->len; else return ST_INVALID_CLASS;
  if(res) *res=need; if(!buf||len<need) return ST_OVERFLOW;
  *(u32*)buf=0; *(u32*)(buf+4)=v->type;
  if(cls==0){ *(u32*)(buf+8)=nb; u16 *w=(u16*)(buf+12); for(u32 i=0;i<nl;i++) w[i]=(u8)v->name[i]; }
  else if(cls==1){ *(u32*)(buf+8)=off; *(u32*)(buf+12)=v->len; *(u32*)(buf+16)=nb; u16 *w=(u16*)(buf+20); for(u32 i=0;i<nl;i++) w[i]=(u8)v->name[i]; if(v->len) memcpy(buf+off,v->data,v->len); }
  else { *(u32*)(buf+8)=v->len; if(v->len) memcpy(buf+12,v->data,v->len); }
  return ST_OK;
}
static long WINAPI nt_NtQueryValueKey(u64 h, const u8 *name, int cls, u8 *buf, u32 len, u32 *res){
  seed(); int k=key_of(h); if(k<0) return (long)ST_INVALID_HANDLE; char n[200]; us2a(name,n,sizeof n); struct rv *v=val_find(k,n); if(!v) return (long)ST_NOT_FOUND; return (long)build_value(v,cls,buf,len,res);
}
static long WINAPI nt_NtEnumerateValueKey(u64 h, u32 idx, int cls, u8 *buf, u32 len, u32 *res){
  seed(); int k=key_of(h); if(k<0) return (long)ST_INVALID_HANDLE; int v=K[k].vfirst; while(v>=0 && idx--) v=V[v].next; if(v<0) return (long)ST_NO_MORE; return (long)build_value(&V[v],cls,buf,len,res);
}
static long WINAPI nt_NtEnumerateKey(u64 h, u32 idx, int cls, u8 *buf, u32 len, u32 *res){
  seed(); int k=key_of(h); if(k<0) return (long)ST_INVALID_HANDLE; int c=K[k].child; while(c>=0 && idx--) c=K[c].sibling; if(c<0) return (long)ST_NO_MORE;
  u32 nl=(u32)strlen(K[c].name), nb=nl*2, hdr= cls==0? 16 : cls==1? 24 : cls==3? 4 : 0; if(!hdr) return (long)ST_INVALID_CLASS;
  if(res) *res=hdr+nb; if(!buf||len<hdr+nb) return (long)ST_OVERFLOW;
  memset(buf,0,hdr); u16 *w; if(cls==0){ *(u32*)(buf+12)=nb; w=(u16*)(buf+16); } else if(cls==1){ *(u32*)(buf+12)=24; *(u32*)(buf+20)=nb; w=(u16*)(buf+24); } else { *(u32*)buf=nb; w=(u16*)(buf+4); }
  for(u32 i=0;i<nl;i++) w[i]=(u8)K[c].name[i]; return 0;
}
static long WINAPI nt_NtQueryKey(u64 h, int cls, u8 *buf, u32 len, u32 *res){
  seed(); int k=key_of(h); if(k<0) return (long)ST_INVALID_HANDLE;
  if(cls==2){ u32 need=48; if(res) *res=need; if(!buf||len<need) return (long)ST_OVERFLOW; memset(buf,0,48); DWORD ns,ms,nv,mvn,mvd; r_info(h,&ns,&ms,&nv,&mvn,&mvd); *(u32*)(buf+20)=ns; *(u32*)(buf+24)=ms*2; *(u32*)(buf+32)=nv; *(u32*)(buf+36)=mvn*2; *(u32*)(buf+40)=mvd; return 0; }
  if(cls==0){ u32 nl=(u32)strlen(K[k].name), need=16+nl*2; if(res) *res=need; if(!buf||len<need) return (long)ST_OVERFLOW; memset(buf,0,16); *(u32*)(buf+12)=nl*2; u16 *w=(u16*)(buf+16); for(u32 i=0;i<nl;i++) w[i]=(u8)K[k].name[i]; return 0; }
  return (long)ST_INVALID_CLASS;
}
static long WINAPI nt_NtSetValueKey(u64 h, const u8 *name, DWORD ti, DWORD type, const u8 *data, DWORD size){ (void)ti; char n[200]; us2a(name,n,sizeof n); return r_set(h,n,1,type,data,size)? (long)ST_INVALID_HANDLE : 0; }
static long WINAPI nt_NtDeleteValueKey(u64 h, const u8 *name){ char n[200]; us2a(name,n,sizeof n); return r_RegDeleteValueA(h,n)? (long)ST_NOT_FOUND : 0; }
static long WINAPI nt_NtDeleteKey(u64 h){ seed(); int k=key_of(h); if(k<6) return (long)ST_INVALID_HANDLE; key_drop(k); return 0; }
static long WINAPI nt_NtFlushKey(u64 h){ (void)h; return 0; }
static long WINAPI nt_NtClose(u64 h){ BOOL (WINAPI *cl)(u64)=win32_resolve("kernel32","CloseHandle"); if(key_of(h)>=0) return 0; if(cl) cl(h); return 0; }

#define RG(n) {"advapi32", #n, r_##n}
#define NTR(n) {"ntdll", #n, nt_##n}
const struct export reg_exports[] = {
  RG(RegOpenKeyExA), RG(RegOpenKeyExW), RG(RegOpenKeyA), RG(RegOpenKeyW), RG(RegCreateKeyExA), RG(RegCreateKeyExW), RG(RegCreateKeyA), RG(RegCreateKeyW),
  RG(RegCloseKey), RG(RegFlushKey), RG(RegQueryValueExA), RG(RegQueryValueExW), RG(RegSetValueExA), RG(RegSetValueExW), RG(RegSetKeyValueA), RG(RegSetKeyValueW),
  RG(RegGetValueA), RG(RegGetValueW), RG(RegDeleteValueA), RG(RegDeleteValueW), RG(RegDeleteKeyA), RG(RegDeleteKeyW), RG(RegDeleteKeyExA), RG(RegDeleteKeyExW),
  RG(RegDeleteTreeA), RG(RegDeleteTreeW), RG(RegEnumKeyExA), RG(RegEnumKeyExW), RG(RegEnumKeyA), RG(RegEnumKeyW), RG(RegEnumValueA), RG(RegEnumValueW),
  RG(RegQueryInfoKeyA), RG(RegQueryInfoKeyW), RG(RegNotifyChangeKeyValue), RG(RegOverridePredefKey), RG(RegQueryValueA),
  NTR(NtOpenKey), NTR(NtCreateKey), NTR(RtlOpenCurrentUser), NTR(NtQueryValueKey), NTR(NtEnumerateValueKey), NTR(NtEnumerateKey), NTR(NtQueryKey), NTR(NtSetValueKey), NTR(NtDeleteValueKey), NTR(NtDeleteKey), NTR(NtFlushKey), NTR(NtClose),
  {"ntdll","ZwClose",nt_NtClose}, {"ntdll","ZwOpenKey",nt_NtOpenKey}, {"ntdll","ZwQueryValueKey",nt_NtQueryValueKey},
};
const unsigned reg_nexports = sizeof reg_exports / sizeof *reg_exports;
