#include "kernel.h"
/* Cooperative multitasking: every process has its own stack and Windows context (TEB/PEB, TLS).
   Switches happen only at waiting points, so no locking is needed. Process 0 is the kernel/desktop itself. */
static int strncmp_ci(const char *a,const char *b,unsigned n){ for(;n;n--,a++,b++){ char x=*a|32, y=*b|32; if(x!=y) return 1; } return 0; }
extern void ctx_switch(u64 *save, u64 next); extern void proc_start(void);
struct proc procs[MAXPROC]; struct proc *cur=&procs[0]; int proc_last_exit[MAXPROC];
struct app { char name[32]; const void *data; u64 size; int demo; };
static struct app apps[32]; static int napps;

void app_register(const char *name, const void *data, u64 size, int demo){
  if(napps>=32) return; unsigned i=0; for(;name[i]&&i<31;i++) apps[napps].name[i]=name[i]; apps[napps].name[i]=0;
  apps[napps].data=data; apps[napps].size=size; apps[napps].demo=demo; napps++;
}
int app_count(void){ return napps; }
const char *app_name(int i){ return apps[i].name; }
int app_is_demo(int i){ return apps[i].demo; }

int procs_alive(void){ int n=0; for(int i=1;i<MAXPROC;i++) if(procs[i].used) n++; return n; }

void schedule(void){
  struct proc *prev=cur; int i=prev-procs;
  for(int k=1;k<=MAXPROC;k++){
    struct proc *n=&procs[(i+k)%MAXPROC];
    if(!n->used) continue;
    if(n==prev) return;
    __asm__ volatile("cli");
    cur=n; win32_set_context(n);
    ctx_switch(&prev->rsp,n->rsp);
    __asm__ volatile("sti");                       /* resumed: back in prev's context */
    return;
  }
}
/* Wait for something to happen: let other processes run; sleep the CPU if nobody else wants it. */
void idle_wait(void){
  static u32 last;
  net_poll();
  if(procs_alive()==0 || cur==&procs[0]){ if(last==ticks) __asm__ volatile("hlt"); last=ticks; }
  schedule();
}

void proc_main(void){
  struct proc *p=cur;
  dll_init_owner(p->pid);
  win32_run_tls_callbacks(p);
  int code=((int (WINAPI*)(void))p->img.entry)();
  proc_exit(code);
}
void proc_exit(int code){
  struct proc *p=cur; proc_last_exit[p->pid]=code;
  p->cap_on=0;                                          /* the exit note belongs on the Console, not in the terminal output */
  con_quiet++; kprintf("\n< %s exited with code %u (0x%x)\n",p->name,(u32)code,(u32)code); con_quiet--;
  wm_cleanup(p->pid);
  dll_exit(p->pid);
  pe_unload(p->img.base);
  p->used=0;
  for(;;) schedule();                                 /* never resumed */
}

void proc_kill(int pid){                              /* close button on a window whose program has no message loop */
  if(pid<=0||pid>=MAXPROC||!procs[pid].used) return;
  struct proc *p=&procs[pid];
  if(p==cur) proc_exit(0xC000013A);
  con_quiet++; kprintf("\n< %s closed\n",p->name); con_quiet--;
  wm_cleanup(pid); dll_cleanup(pid); pe_unload(p->img.base); p->used=0;
}

static int name_matches(const char *app, const char *want){             /* "notepad" matches "notepad.exe" */
  if(!stricmp(app,want)) return 1;
  unsigned l=strlen(app); return l>4 && !stricmp(app+l-4,".exe") && strlen(want)==l-4 && !strncmp_ci(app,want,l-4);
}
static int spawn_core(const char *display, const void *data, u64 size, const char *cmdline, const char *cwd){
  int slot=-1; for(int i=1;i<MAXPROC;i++) if(!procs[i].used){ slot=i; break; }
  if(slot<0){ kputs("proc: too many processes\n"); return -1; }
  struct proc *p=&procs[slot];
  con_quiet++; kprintf("\n> Starting %s (%u bytes)\n",display,(u32)size);
  struct pe_image img; dll_cleanup(slot); pe_owner=slot; int bad=pe_load(data,size,&img); pe_owner=0; con_quiet--;
  if(bad){ dll_cleanup(slot); return -1; }
  if(p->cap) heap_free(p->cap);
  memset(p,0,sizeof *p); p->used=1; p->pid=slot;
  unsigned i=0; for(;display[i]&&i<31;i++) p->name[i]=display[i]; proc_last_exit[slot]=259;
  if(cwd){ unsigned k=0; for(;cwd[k]&&k<127;k++) p->cwd[k]=cwd[k]; p->cwd[k]=0; }
  p->img=img;
  win32_init_proc(p,&img,cmdline? cmdline : display);
  u64 *sp=(u64*)(img.stack_top&~15ull); sp-=8;        /* r15 r14 r13 r12 rbx rbp ret + pad */
  for(int k=0;k<6;k++) sp[k]=0;
  sp[6]=(u64)proc_start;
  sp-=20; for(int k=0;k<20;k++) sp[k]=0;              /* the saved xmm6-xmm15 area that ctx_switch restores first */
  p->rsp=(u64)sp;
  return slot;
}
int proc_spawn_cwd(const char *name, const char *cmdline, const char *cwd){
  struct app *a=0; for(int i=0;i<napps;i++) if(name_matches(apps[i].name,name)){ a=&apps[i]; break; }
  if(!a) return -1;
  return spawn_core(a->name,a->data,a->size,cmdline,cwd);
}
int proc_spawn_cmd(const char *name, const char *cmdline){ return proc_spawn_cwd(name,cmdline,""); }
int proc_spawn(const char *name){ return proc_spawn_cwd(name,0,""); }
int proc_spawn_data(const char *name, const void *data, u64 size, const char *cmdline, const char *cwd){ return spawn_core(name,data,size,cmdline,cwd); }
