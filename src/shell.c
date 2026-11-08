#include "kernel.h"
static int WINAPI fmt_(char *b, u32 n, const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(b,n,f,ap); __builtin_ms_va_end(ap); return r; }
#define snprintf_ fmt_
/* The shell: decides what opens a file or folder (what a double-click does). */
static int ends_with(const char *s, const char *ext){ int n=(int)strlen(s), e=(int)strlen(ext); return n>e && !stricmp(s+n-e,ext); }
static int is_text(const char *n){
  static const char *const x[]={".txt",".md",".log",".c",".h",".cpp",".py",".sh",".json",".xml",".ini",".cfg",".csv",".bat",".yml",".yaml",".toml",".rs",".js",".css"};
  for(unsigned i=0;i<sizeof x/sizeof *x;i++) if(ends_with(n,x[i])) return 1; return 0;
}
int shell_open(const char *path){
  char c[128]; fs_canon(path,c,sizeof c); char full[160]; unsigned k=0; full[k++]='/'; for(const char *q=c;*q&&k<158;) full[k++]=*q++; full[k]=0;
  if(!c[0] || fs_isdir(full)){ char cmd[200]; snprintf_(cmd,sizeof cmd,"files %s",full); return proc_spawn_cwd("files",cmd,""); }
  struct ramfile *f=fs_find(full); if(!f) return -1;
  const char *lf=fs_leaf(f); char cmd[300];
  if(ends_with(lf,".exe")){ char dir[128]; unsigned n=(unsigned)strlen(c); while(n>0 && c[n-1]!='/') n--; memcpy(dir,c,n? n-1 : 0); dir[n? n-1 : 0]=0; snprintf_(cmd,sizeof cmd,"%s",lf);
    int pid=proc_spawn_cwd(lf,cmd,dir); if(pid<0) pid=proc_spawn_data(lf,f->data,f->size,cmd,dir); return pid; }
  if(ends_with(lf,".html")||ends_with(lf,".htm")){ snprintf_(cmd,sizeof cmd,"browser %s",full); return proc_spawn_cwd("browser",cmd,""); }
  (void)is_text; snprintf_(cmd,sizeof cmd,"notes %s",full); return proc_spawn_cwd("notes",cmd,"");     /* text and everything else: the editor */
}
