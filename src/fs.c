#include "kernel.h"
/* A hierarchical RAM filesystem rooted at "C:\". Paths use '\' or '/', are case-insensitive and stored canonically
   ("Desktop/notes.txt"). Directories are entries with isdir. Relative paths resolve against the calling process's current
   directory; a path starting with '/', '\' or a drive letter is absolute. Changes are written to the boot disk (see fs_sync). */
#define MAXFILES 512
#define MAXOPEN 16
#define MAXTOMB 64
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
static struct ramfile files[MAXFILES];
static int strncmp_ci(const char *a,const char *b,int n){ for(;n;n--,a++,b++){ if((*a|32)!=(*b|32)) return 1; if(!*a) return 0; } return 0; }
int fs_gen;                                             /* bumped on every change; the desktop re-reads when it changes */
static char tombs[MAXTOMB][128]; static int ntomb;      /* boot files the user deleted/renamed away: must stay gone after reboot */

static void canon_from(const char *cwd, const char *in, char *out, int max){
  char tmp[512]; int n=0; const char *p=in; int absolute=0;
  if(p[0] && p[1]==':'){ p+=2; absolute=1; }
  if(*p=='/'||*p=='\\') absolute=1;
  if(!absolute && cwd && *cwd){ while(*cwd && n<250) tmp[n++]=*cwd++; tmp[n++]='/'; }
  while(*p && n<510) tmp[n++]=*p++; tmp[n]=0;
  char comp[24][64]; int nc=0; char *t=tmp;
  while(*t){
    char *e=t; while(*e && *e!='/' && *e!='\\') e++;
    int l=(int)(e-t);
    if(l==0 || (l==1&&t[0]=='.')){}
    else if(l==2 && t[0]=='.' && t[1]=='.'){ if(nc) nc--; }
    else if(nc<24){ if(l>63) l=63; memcpy(comp[nc],t,l); comp[nc][l]=0; nc++; }
    t=e; while(*t=='/'||*t=='\\') t++;
  }
  int o=0; for(int i=0;i<nc;i++){ if(i && o<max-1) out[o++]='/'; for(int k=0;comp[i][k] && o<max-1;k++) out[o++]=comp[i][k]; }
  out[o]=0;
}
void fs_canon(const char *in, char *out, int max){ canon_from(cur? cur->cwd : "", in, out, max); }
static void parent_of(const char *c, char *out){ int n=(int)strlen(c); while(n>0 && c[n-1]!='/') n--; if(n>0) n--; memcpy(out,c,n); out[n]=0; }
static const char *leaf_of(const char *c){ const char *b=c; for(;*c;c++) if(*c=='/') b=c+1; return b; }
const char *fs_leaf(const struct ramfile *f){ return leaf_of(f->name); }
int fs_count(void){ int n=0; for(int i=0;i<MAXFILES;i++) if(files[i].used) n++; return n; }
struct ramfile *fs_get(int k){ for(int i=0;i<MAXFILES;i++) if(files[i].used && k--==0) return &files[i]; return NULL; }
static struct ramfile *find_canon(const char *c){
  if(!*c) return NULL;
  for(int i=0;i<MAXFILES;i++) if(files[i].used && stricmp(files[i].name,c)==0) return &files[i];
  return NULL;
}
struct ramfile *fs_find(const char *path){ char c[128]; fs_canon(path,c,sizeof c); return find_canon(c); }
int fs_isdir(const char *path){ char c[128]; fs_canon(path,c,sizeof c); if(!*c) return 1; struct ramfile *f=find_canon(c); return f && f->isdir; }
static int dir_canon_ok(const char *c){ if(!*c) return 1; struct ramfile *f=find_canon(c); return f && f->isdir; }
static struct ramfile *alloc_entry(const char *c){
  for(int i=0;i<MAXFILES;i++) if(!files[i].used){ struct ramfile *f=&files[i]; memset(f,0,sizeof *f); f->used=1; unsigned k=0; for(;c[k]&&k<127;k++) f->name[k]=c[k]; f->name[k]=0; return f; }
  return NULL;
}
static void untomb(const char *c){ for(int i=0;i<ntomb;i++) if(!stricmp(tombs[i],c)){ memcpy(tombs[i],tombs[--ntomb],128); return; } }
static void tomb_add(const char *c){ for(int i=0;i<ntomb;i++) if(!stricmp(tombs[i],c)) return; if(ntomb<MAXTOMB){ unsigned k=0; for(;c[k]&&k<127;k++) tombs[ntomb][k]=c[k]; tombs[ntomb][k]=0; ntomb++; } }
static int fs_sync(void);
struct ramfile *fs_create(const char *path){
  char c[128]; fs_canon(path,c,sizeof c); if(!*c) return NULL;
  struct ramfile *f=find_canon(c);
  if(f){ if(f->isdir) return NULL; f->size=0; f->persist=1; f->dirty=1; fs_gen++; return f; }
  char par[128]; parent_of(c,par); if(!dir_canon_ok(par)) return NULL;
  f=alloc_entry(c); if(!f) return NULL; f->persist=1; f->dirty=1; untomb(c); fs_gen++; return f;
}
int fs_mkdir(const char *path){
  char c[128]; fs_canon(path,c,sizeof c); if(!*c||find_canon(c)) return -1;
  char par[128]; parent_of(c,par); if(!dir_canon_ok(par)) return -1;
  struct ramfile *f=alloc_entry(c); if(!f) return -1; f->isdir=1; f->persist=1; f->dirty=1; untomb(c); fs_gen++; fs_sync(); return 0;
}
static void ensure_dirs(const char *c){                 /* mkdir -p for the parents of c (boot files) */
  char par[128]; parent_of(c,par); if(!*par||find_canon(par)) return;
  ensure_dirs(par); struct ramfile *f=alloc_entry(par); if(f){ f->isdir=1; f->boot=1; }
}
void fs_init(void){
  static const char *const dirs[]={"Desktop","Documents","Downloads"};
  for(int i=0;i<3;i++) if(!find_canon(dirs[i])){ struct ramfile *f=alloc_entry(dirs[i]); if(f){ f->isdir=1; f->boot=1; } }
}
void fs_add(const char *name, const void *data, u64 size){
  char c[128]; canon_from("",name,c,sizeof c); if(!*c) return; ensure_dirs(c);
  struct ramfile *f=find_canon(c); if(!f) f=alloc_entry(c); if(!f) return;
  f->isdir=0; f->boot=1; f->persist=0; f->dirty=0; f->data=(u8*)data; f->size=size; f->cap=size; fs_gen++;
}
int fs_list(const char *dir, struct ramfile **out, int max){
  char d[128]; fs_canon(dir,d,sizeof d); int n=0;
  for(int i=0;i<MAXFILES && n<max;i++) if(files[i].used){ char par[128]; parent_of(files[i].name,par); if(!stricmp(par,d)) out[n++]=&files[i]; }
  return n;
}
static int has_children(const char *c){ int n=(int)strlen(c); for(int i=0;i<MAXFILES;i++) if(files[i].used && !strncmp_ci(files[i].name,c,n) && files[i].name[n]=='/') return 1; return 0; }
static int is_default_dir(const char *c){ return !stricmp(c,"Desktop")||!stricmp(c,"Documents")||!stricmp(c,"Downloads"); }
int fs_remove(const char *path){
  char c[128]; fs_canon(path,c,sizeof c); struct ramfile *f=find_canon(c); if(!f) return -1;
  if(f->isdir && (has_children(c) || is_default_dir(c))) return -1;
  if(f->boot) tomb_add(c);
  f->used=0; fs_gen++; fs_sync(); return 0;
}
int fs_remove_tree(const char *path){
  char c[128]; fs_canon(path,c,sizeof c); struct ramfile *f=find_canon(c); if(!f) return -1;
  if(is_default_dir(c) && f->isdir){ int n=(int)strlen(c); for(int i=0;i<MAXFILES;i++) if(files[i].used && !strncmp_ci(files[i].name,c,n) && files[i].name[n]=='/'){ if(files[i].boot) tomb_add(files[i].name); files[i].used=0; } fs_gen++; fs_sync(); return 0; }
  int n=(int)strlen(c);
  for(int i=0;i<MAXFILES;i++) if(files[i].used && !strncmp_ci(files[i].name,c,n) && files[i].name[n]=='/'){ if(files[i].boot) tomb_add(files[i].name); files[i].used=0; }
  if(f->boot) tomb_add(c); f->used=0; fs_gen++; fs_sync(); return 0;
}
int fs_rename(const char *from, const char *to){
  char a[128], b[128]; fs_canon(from,a,sizeof a); fs_canon(to,b,sizeof b);
  struct ramfile *f=find_canon(a); if(!f||!*b||find_canon(b)||is_default_dir(a)) return -1;
  char par[128]; parent_of(b,par); if(!dir_canon_ok(par)) return -1;
  int na=(int)strlen(a); if(f->isdir && !strncmp_ci(b,a,na) && b[na]=='/') return -1;       /* into itself */
  for(int i=0;i<MAXFILES;i++){
    struct ramfile *e=&files[i]; if(!e->used) continue;
    int self= e==f, kid= f->isdir && !strncmp_ci(e->name,a,na) && e->name[na]=='/';
    if(!self && !kid) continue;
    char old[128]; memcpy(old,e->name,128); if(e->boot) tomb_add(old);
    char nn[128]; int k=0; for(const char *q=b; *q && k<127;) nn[k++]=*q++; for(const char *q=old+na; *q && k<127;) nn[k++]=*q++; nn[k]=0;
    memcpy(e->name,nn,128); e->boot=0; e->persist=1; e->dirty=1;
  }
  untomb(b); fs_gen++; fs_sync(); return 0;
}
int fs_chdir(const char *path){ char c[128]; fs_canon(path,c,sizeof c); if(*c && !dir_canon_ok(c)) return -1; unsigned k=0; for(;c[k]&&k<127;k++) cur->cwd[k]=c[k]; cur->cwd[k]=0; return 0; }
const char *fs_cwd(void){ return cur->cwd; }
static int fs_write(struct ramfile *f, u64 pos, const void *buf, u64 n){
  if(f->isdir) return -1;
  if(pos+n>f->cap){
    u64 nc=(pos+n)*2+64; u8 *nd=kmalloc(nc,16); if(!nd) return -1;
    if(f->size) memcpy(nd,f->data,f->size);
    f->data=nd; f->cap=nc;
  }
  if(pos>f->size) memset(f->data+f->size,0,pos-f->size);
  memcpy(f->data+pos,buf,n); if(pos+n>f->size) f->size=pos+n;
  f->dirty=1; f->persist=1; fs_gen++;
  return 0;
}

/* ---- persistence: records = [512B header: path[128], flags@128 (1=dir, 2=deleted), size@136][data padded to 512], format version 2 ---- */
static int disk_ok;
int fs_persistence(void){ return disk_ok; }
static u32 data_sectors(u64 size){ return (size+511)/512; }
static int fs_sync(void){
  if(!disk_ok) return -1;
  u32 total=1, n=0;
  for(int i=0;i<MAXFILES;i++) if(files[i].used && files[i].persist){ total+=1+data_sectors(files[i].isdir? 0 : files[i].size); n++; }
  total+=ntomb; n+=ntomb;
  if(total>DATA_SECTORS){ kputs("fs: saved files do not fit in the data area\n"); return -1; }
  static u8 sec[512]; u32 lba=DATA_LBA+1;
  for(int i=0;i<MAXFILES;i++){
    struct ramfile *f=&files[i]; if(!f->used||!f->persist) continue;
    memset(sec,0,512); memcpy(sec,f->name,128); sec[128]=f->isdir? 1 : 0; *(u64*)(sec+136)=f->isdir? 0 : f->size;
    if(ata_write(lba++,1,sec)) return -1;
    if(f->isdir){ f->dirty=0; continue; }
    u64 left=f->size, off=0;
    while(left){ u64 c=left>512?512:left; memset(sec,0,512); memcpy(sec,f->data+off,c); if(ata_write(lba++,1,sec)) return -1; off+=c; left-=c; }
    f->dirty=0;
  }
  for(int i=0;i<ntomb;i++){ memset(sec,0,512); memcpy(sec,tombs[i],128); sec[128]=2; if(ata_write(lba++,1,sec)) return -1; }
  memset(sec,0,512); *(u32*)sec=0x53464C57; *(u32*)(sec+4)=2; *(u32*)(sec+8)=n; *(u32*)(sec+12)=total;   /* "WLFS" header last */
  return ata_write(DATA_LBA,1,sec);
}
int fs_load_disk(void){
  if(!ata_init()){ return 0; }
  if(ata_sectors()<DATA_LBA+DATA_SECTORS){ kputs("disk: too small for the data area, files stay in RAM\n"); return 0; }
  disk_ok=1;
  static u8 hdr[512]; if(ata_read(DATA_LBA,1,hdr)) return 0;
  u32 ver=*(u32*)(hdr+4);
  if(*(u32*)hdr!=0x53464C57 || (ver!=1&&ver!=2)) return 1;                  /* nothing saved yet */
  u32 n=*(u32*)(hdr+8), total=*(u32*)(hdr+12);
  if(total<1||total>DATA_SECTORS) return 1;
  u8 *buf=kmalloc((u64)total*512,512); if(!buf) return 1;
  if(total>1 && ata_read(DATA_LBA+1,total-1,buf)) return 1;
  u64 off=0;
  for(u32 i=0;i<n && off+512<=(u64)(total-1)*512;i++){
    char name[128]; u64 size; int flags=0;
    if(ver==1){ char leaf[64]; memcpy(leaf,buf+off,64); leaf[63]=0; name[0]=0; const char *pre="Documents/"; int k=0; for(;pre[k];k++) name[k]=pre[k]; for(int j=0;leaf[j]&&k<127;j++) name[k++]=leaf[j]; name[k]=0; size=*(u64*)(buf+off+64); }
    else { memcpy(name,buf+off,128); name[127]=0; flags=buf[off+128]; size=*(u64*)(buf+off+136); }
    off+=512;
    if(flags&2){ struct ramfile *f=find_canon(name); if(f) f->used=0; tomb_add(name); continue; }
    if(flags&1){ ensure_dirs(name); if(!find_canon(name)){ struct ramfile *f=alloc_entry(name); if(f){ f->isdir=1; f->persist=1; } } else find_canon(name)->persist=1; continue; }
    ensure_dirs(name); struct ramfile *f=find_canon(name); if(!f) f=alloc_entry(name); if(!f) break;
    f->isdir=0; f->data=buf+off; f->size=size; f->cap=data_sectors(size)*512; f->persist=1; f->dirty=0;
    off+=data_sectors(size)*512;
  }
  fs_gen++; return 1;
}

void fs_flush(void){ fs_sync(); }
struct ramfile *fs_open_write(const char *name){ return fs_create(name); }
int fs_append(struct ramfile *f, const void *d, u64 n){ return fs_write(f,f->size,d,n); }
void fs_commit(struct ramfile *f){ if(f && f->dirty) fs_sync(); }

struct kfile { int used, canwrite, append; struct ramfile *f; u64 pos; };
static struct kfile opens[MAXOPEN];
int fs_is_file(void *fp){ return (u8*)fp>=(u8*)opens && (u8*)fp<(u8*)(opens+MAXOPEN); }
static struct kfile *alloc_open(struct ramfile *f, int w, int app){
  if(f->isdir) return NULL;
  for(int i=0;i<MAXOPEN;i++) if(!opens[i].used){ opens[i]=(struct kfile){1,w,app,f,app?f->size:0}; return &opens[i]; }
  return NULL;
}

/* ---- C stdio (msvcrt/ucrt) ---- */
static void *WINAPI c_fopen(const char *path, const char *mode){
  int w=mode[0]=='w'||mode[0]=='a'||(mode[0]&&mode[1]=='+')||(mode[1]&&mode[2]=='+');
  struct ramfile *f;
  if(mode[0]=='r') f=fs_find(path);
  else f=(mode[0]=='a')? (fs_find(path)?:fs_create(path)) : fs_create(path);
  if(!f) return NULL;
  return alloc_open(f,w,mode[0]=='a');
}
static int WINAPI c_fclose(void *fp){ if(!fs_is_file(fp)) return -1; struct kfile *k=fp; k->used=0; if(k->f->dirty) fs_sync(); return 0; }
static u64 WINAPI c_fread(void *buf, u64 sz, u64 n, void *fp){
  if(!fs_is_file(fp)||!sz) return 0; struct kfile *k=fp;
  u64 want=sz*n, have=k->pos<k->f->size? k->f->size-k->pos : 0; if(want>have) want=have;
  memcpy(buf,k->f->data+k->pos,want); k->pos+=want; return want/sz;
}
static u64 WINAPI c_fwrite(const void *buf, u64 sz, u64 n, void *fp){
  if(!fs_is_file(fp)){ const char *b=buf; for(u64 i=0;i<sz*n;i++) kputc(b[i]); return n; }
  struct kfile *k=fp; if(!k->canwrite) return 0;
  if(k->append) k->pos=k->f->size;
  if(fs_write(k->f,k->pos,buf,sz*n)) return 0; k->pos+=sz*n; return n;
}
static int WINAPI c_fseek(void *fp, i32 off, int whence){
  if(!fs_is_file(fp)) return -1; struct kfile *k=fp;
  i64 np= whence==SEEK_SET? off : whence==SEEK_CUR? (i64)k->pos+off : (i64)k->f->size+off;
  if(np<0) return -1; k->pos=np; return 0;
}
static i32 WINAPI c_ftell(void *fp){ return fs_is_file(fp)? (i32)((struct kfile*)fp)->pos : -1; }
static void WINAPI c_rewind(void *fp){ c_fseek(fp,0,SEEK_SET); }
static int WINAPI c_feof(void *fp){ if(!fs_is_file(fp)) return 0; struct kfile *k=fp; return k->pos>=k->f->size; }
static int WINAPI c_fgetc(void *fp){ u8 c; return c_fread(&c,1,1,fp)? c : -1; }
static char *WINAPI c_fgets(char *b, int n, void *fp){
  if(fs_is_file(fp)){
    int i=0; for(;i<n-1;){ int c=c_fgetc(fp); if(c<0) break; b[i++]=c; if(c=='\n') break; }
    if(!i) return NULL; b[i]=0; return b;
  }
  if(n<3) return 0; int r=kb_readline(b,n-1); int o=0;
  for(int i=0;i<r;i++) if(b[i]!='\r') b[o++]=b[i];       /* text mode: \r\n -> \n */
  b[o]=0; return b;
}
static int WINAPI c_fputs(const char *s, void *fp){ c_fwrite(s,1,strlen(s),fp); return 1; }
static int WINAPI c_fputc(int c, void *fp){ char ch=c; c_fwrite(&ch,1,1,fp); return c; }
static int WINAPI c_fprintf(void *fp, const char *f, ...){
  margs ap; __builtin_ms_va_start(ap,f); char tmp[1024]; int n=kvsnprintf(tmp,sizeof tmp,f,ap); __builtin_ms_va_end(ap);
  if(n>1023) n=1023; c_fwrite(tmp,1,n,fp); return n;
}
static int WINAPI c_fflush(void *fp){ (void)fp; return 0; }
static int WINAPI c_remove(const char *p){ struct ramfile *f=fs_find(p); if(!f||f->isdir) return -1; return fs_remove(p); }
static int WINAPI c_rename(const char *a, const char *b){ return fs_rename(a,b); }
static int WINAPI c__mkdir(const char *p){ return fs_mkdir(p); }
static int WINAPI c__rmdir(const char *p){ struct ramfile *f=fs_find(p); if(!f||!f->isdir) return -1; return fs_remove(p); }
static int WINAPI c__chdir(const char *p){ return fs_chdir(p); }
static char *WINAPI c__getcwd(char *b, int n){ char t[140]; int k=0; t[k++]='C'; t[k++]=':'; t[k++]='\\'; for(const char *q=cur->cwd;*q&&k<138;q++) t[k++]=*q=='/'? '\\' : *q; t[k]=0; if(!b||n<=k) return 0; memcpy(b,t,k+1); return b; }

/* ---- kernel32 file handles: 0x5000+i ---- */
#define H_FILE 0x5000
extern void win32_set_last_error(u32 e);
int fs_is_handle(u64 h){ return h>=H_FILE && h<H_FILE+MAXOPEN && opens[h-H_FILE].used; }
int fs_hread(u64 h, void *buf, u32 n, u32 *got){ u64 r=c_fread(buf,1,n,&opens[h-H_FILE]); if(got) *got=(u32)r; return 1; }
int fs_hwrite(u64 h, const void *buf, u32 n, u32 *wr){ u64 r=c_fwrite(buf,1,n,&opens[h-H_FILE]); if(wr) *wr=(u32)r; return r==n; }
int fs_hclose(u64 h){ struct kfile *k=&opens[h-H_FILE]; k->used=0; if(k->f->dirty) fs_sync(); return 1; }
static u64 WINAPI k_CreateFileA(const char *name, u32 access, u32 share, void *sa, u32 disp, u32 fl, u64 tmpl){
  (void)share;(void)sa;(void)fl;(void)tmpl;
  struct ramfile *f=fs_find(name); int w=(access&0x40000000)!=0;
  if(f && f->isdir){ win32_set_last_error(5); return (u64)-1; }
  switch(disp){
    case 1: if(f){ win32_set_last_error(80); return (u64)-1; } f=fs_create(name); break;       /* CREATE_NEW */
    case 2: f=fs_create(name); break;                                /* CREATE_ALWAYS */
    case 3: if(!f){ win32_set_last_error(2); return (u64)-1; } break;/* OPEN_EXISTING */
    case 4: if(!f) f=fs_create(name); break;                         /* OPEN_ALWAYS */
    case 5: if(!f){ win32_set_last_error(2); return (u64)-1; } f->size=0; break;   /* TRUNCATE_EXISTING */
  }
  if(!f){ win32_set_last_error(3); return (u64)-1; }
  struct kfile *k=alloc_open(f,w,0); return k? H_FILE+(k-opens) : (u64)-1;
}
static u32 WINAPI k_GetFileSize(u64 h, u32 *hi){ if(hi) *hi=0; return fs_is_handle(h)? (u32)opens[h-H_FILE].f->size : 0xFFFFFFFF; }
static u32 WINAPI k_SetFilePointer(u64 h, i32 lo, i32 *hi, u32 how){ (void)hi; return fs_is_handle(h) && !c_fseek(&opens[h-H_FILE],lo,how)? (u32)opens[h-H_FILE].pos : 0xFFFFFFFF; }
static u32 WINAPI k_GetFileAttributesA(const char *p){
  char c[128]; fs_canon(p,c,sizeof c); if(!*c) return 0x10;
  struct ramfile *f=find_canon(c); if(!f){ win32_set_last_error(2); return 0xFFFFFFFF; } return f->isdir? 0x10 : 0x80;
}
static int WINAPI k_DeleteFileA(const char *p){ struct ramfile *f=fs_find(p); if(!f){ win32_set_last_error(2); return 0; } if(f->isdir){ win32_set_last_error(5); return 0; } return fs_remove(p)==0; }
static int WINAPI k_CreateDirectoryA(const char *p, void *sa){ (void)sa; if(fs_find(p)){ win32_set_last_error(183); return 0; } if(fs_mkdir(p)){ win32_set_last_error(3); return 0; } return 1; }
static int WINAPI k_RemoveDirectoryA(const char *p){ struct ramfile *f=fs_find(p); if(!f||!f->isdir){ win32_set_last_error(3); return 0; } if(fs_remove(p)){ win32_set_last_error(145); return 0; } return 1; }

/* directory listing */
static int glob_ci(const char *p, const char *s){
  if(!*p) return !*s;
  if(*p=='*') return glob_ci(p+1,s) || (*s && glob_ci(p,s+1));
  return *s && (*p=='?' || (*p|32)==(*s|32)) && glob_ci(p+1,s+1);
}
static struct { u8 used; int n, next; struct ramfile *list[256]; } finds[8];
static int find_fill(int h, u8 *fd){
  if(finds[h].next>=finds[h].n) return 0;
  struct ramfile *f=finds[h].list[finds[h].next++];
  memset(fd,0,320); *(u32*)fd= f->isdir? 0x10 : 0x80; *(u32*)(fd+28)=(u32)(f->size>>32); *(u32*)(fd+32)=(u32)f->size;
  const char *lf=fs_leaf(f); unsigned n=0; while(lf[n] && n<259){ fd[44+n]=lf[n]; n++; } fd[44+n]=0; return 1;
}
static u64 WINAPI k_FindFirstFileA(const char *pat, u8 *fd){
  for(int i=0;i<8;i++) if(!finds[i].used){
    char dirp[128]; unsigned k=0; int cut=-1; for(;pat[k];k++) if(pat[k]=='/'||pat[k]=='\\') cut=(int)k;
    if(cut>=0){ memcpy(dirp,pat,cut); dirp[cut]=0; if(cut==0){ dirp[0]='/'; dirp[1]=0; } } else dirp[0]=0;
    const char *lp= cut>=0? pat+cut+1 : pat; char leaf[64]; unsigned n=0; while(lp[n] && n<63){ leaf[n]=lp[n]; n++; } leaf[n]=0;
    if(!n || !strcmp(leaf,"*.*")){ leaf[0]='*'; leaf[1]=0; }
    struct ramfile *all[256]; int cnt=fs_list(dirp[0]? dirp : ".", all, 256);
    finds[i].used=1; finds[i].n=0; finds[i].next=0;
    for(int j=0;j<cnt;j++) if(glob_ci(leaf,fs_leaf(all[j]))) finds[i].list[finds[i].n++]=all[j];
    if(find_fill(i,fd)) return 0x5100+i; finds[i].used=0; win32_set_last_error(2); return (u64)-1;
  }
  win32_set_last_error(4); return (u64)-1;
}
static int WINAPI k_FindNextFileA(u64 h, u8 *fd){ if(h<0x5100||h>=0x5108||!finds[h-0x5100].used) return 0; if(find_fill((int)(h-0x5100),fd)) return 1; win32_set_last_error(18); return 0; }
static int WINAPI k_FindClose(u64 h){ if(h>=0x5100&&h<0x5108) finds[h-0x5100].used=0; return 1; }
static int WINAPI k_CopyFileA(const char *src, const char *dst, int failIfExists){
  struct ramfile *a=fs_find(src); if(!a||a->isdir){ win32_set_last_error(2); return 0; }
  struct ramfile *ex=fs_find(dst); if(ex && (failIfExists||ex->isdir)){ win32_set_last_error(80); return 0; }
  u8 *data=a->data; u64 size=a->size;
  struct ramfile *b=fs_create(dst); if(!b){ win32_set_last_error(3); return 0; } b->size=0;
  if(size && fs_write(b,0,data,size)) return 0; b->persist=1; b->dirty=1; fs_sync(); return 1;
}
static int WINAPI k_MoveFileA(const char *src, const char *dst){
  if(!fs_find(src)){ win32_set_last_error(2); return 0; } if(fs_find(dst)){ win32_set_last_error(183); return 0; }
  if(fs_rename(src,dst)){ win32_set_last_error(3); return 0; } return 1;
}
static int WINAPI k_MoveFileExA(const char *src, const char *dst, u32 fl){ (void)fl; return k_MoveFileA(src,dst); }
static u32 WINAPI k_GetCurrentDirectoryA(u32 n, char *b){
  char t[140]; int k=0; t[k++]='C'; t[k++]=':'; t[k++]='\\'; for(const char *q=cur->cwd;*q&&k<138;q++) t[k++]=*q=='/'? '\\' : *q; t[k]=0;
  if(n<=(u32)k) return k+1; memcpy(b,t,k+1); return k;
}
static int WINAPI k_SetCurrentDirectoryA(const char *p){ if(fs_chdir(p)){ win32_set_last_error(2); return 0; } return 1; }
static u32 WINAPI k_GetFullPathNameA(const char *p, u32 n, char *b, char **filepart){
  char c[128]; fs_canon(p,c,sizeof c); char t[140]; int k=0; t[k++]='C'; t[k++]=':'; t[k++]='\\'; int lp=k; for(const char *q=c;*q&&k<138;q++){ t[k]=*q=='/'? '\\' : *q; if(*q=='/') lp=k+1; k++; } t[k]=0;
  if(n<=(u32)k) return k+1; memcpy(b,t,k+1); if(filepart) *filepart=b+lp; return k;
}

#define C(n) {"msvcrt", #n, c_##n}
const struct export fs_exports[]={
  C(fopen), C(fclose), C(fread), C(fwrite), C(fseek), C(ftell), C(rewind), C(feof), C(fgetc), C(fgets), C(fputs), C(fputc),
  C(fprintf), C(fflush), C(remove), C(rename), C(_mkdir), C(_rmdir), C(_chdir), C(_getcwd),
  {"msvcrt","getc",c_fgetc}, {"msvcrt","putc",c_fputc},
  {"kernel32","CreateFileA",k_CreateFileA}, {"kernel32","GetFileSize",k_GetFileSize}, {"kernel32","SetFilePointer",k_SetFilePointer},
  {"kernel32","GetFileAttributesA",k_GetFileAttributesA}, {"kernel32","DeleteFileA",k_DeleteFileA},
  {"kernel32","CreateDirectoryA",k_CreateDirectoryA}, {"kernel32","RemoveDirectoryA",k_RemoveDirectoryA},
  {"kernel32","FindFirstFileA",k_FindFirstFileA}, {"kernel32","FindNextFileA",k_FindNextFileA}, {"kernel32","FindClose",k_FindClose},
  {"kernel32","CopyFileA",k_CopyFileA}, {"kernel32","MoveFileA",k_MoveFileA}, {"kernel32","MoveFileExA",k_MoveFileExA},
  {"kernel32","GetCurrentDirectoryA",k_GetCurrentDirectoryA}, {"kernel32","SetCurrentDirectoryA",k_SetCurrentDirectoryA},
  {"kernel32","GetFullPathNameA",k_GetFullPathNameA},
};
const unsigned fs_nexports=sizeof fs_exports/sizeof *fs_exports;
