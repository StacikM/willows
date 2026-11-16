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

/* Preemption (timer interrupt, boot.S irq0_stub): a thread that keeps running its own code for a whole time slice gives the CPU
   to the others, as on Windows - programs spin-wait for other threads (Firefox at shutdown) and would otherwise freeze the system.
   Only program code is interrupted this way: the kernel is not written to be re-entered halfway through one of its functions. */
static u32 slice_t0;
#define SLICE_MS 10
void timer_preempt(u64 rip, u64 cs){
  extern char kernel_text_end[];
  if(cs!=0x08 || !cur || cur==&procs[0]) return;                    /* 64-bit code of a program (not WOW's 32-bit mode, not the kernel's own loop) */
  if(rip<(u64)kernel_text_end) return;                               /* inside the kernel */
  if((u32)(ticks-slice_t0)<SLICE_MS) return;
  schedule();
}
/* Run the next runnable thread: slots 0..MAXPROC-1 are the processes' main threads, MAXPROC.. the extra threads (thread.c). */
void schedule(void){
  slice_t0=ticks;
  const int N=MAXPROC+MAXTHR; int pos= cur_thr? MAXPROC+(int)(cur_thr-threads) : (int)(cur-procs);
  for(int k=1;k<=N;k++){
    int q=(pos+k)%N;
    if(q<MAXPROC){ if(!procs[q].used || procs[q].main_done || procs[q].main_suspended) continue; }
    else if(!thread_runnable(q-MAXPROC)) continue;
    if(q==pos) return;
    u64 *save= cur_thr? &cur_thr->rsp : &cur->rsp, next;
    __asm__ volatile("cli");
    if(q<MAXPROC){ cur=&procs[q]; cur_thr=0; cur_teb=cur->teb; next=cur->rsp; }
    else { struct thread *t=&threads[q-MAXPROC]; cur=&procs[t->pid]; cur_thr=t; cur_teb=t->teb; next=t->rsp; }
    win32_set_context(cur);
    ctx_switch(save,next);
    __asm__ volatile("sti");                       /* resumed: back in the previous thread's context */
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
  if(p->img.is32){ u32 code=(u32)wow_enter(p->img.entry,p->img.stack_top&~15ull,&p->wow_rsp); proc_exit((int)code); }      /* a 32-bit program: run in compatibility mode */
  win32_run_tls_callbacks(p);
  int code=((int (WINAPI*)(void))p->img.entry)();
  proc_exit(code);
}
static void release_pipes(struct proc *p){                 /* the child's copies of its redirected stdin/stdout go away with it: readers then see end-of-file */
  p->out_h=p->in_h=0;
  pipe_proc_exit(p->pid);                                          /* pending pipe reads go (their buffers are gone), then every pipe end it held */
  { void ws_proc_exit(int pid); ws_proc_exit(p->pid); }            /* and its sockets */
  for(int k=0;k<p->ninh;k++){ u64 h=p->inh[k]; if(map_is_handle(h)) map_close(h); }   /* inherited mappings (objects: kobj_proc_exit) */
  p->ninh=0;
}
void proc_exit(int code){
  struct proc *p=cur; proc_last_exit[p->pid]=code;
  release_pipes(p);
  p->cap_on=0;                                        /* the exit note belongs on the Console, not in the terminal output */
  con_quiet++; kprintf("\n< %s exited with code %u (0x%x)\n",p->name,(u32)code,(u32)code); con_quiet--;
  wm_cleanup(p->pid);
  thread_kill_all(p->pid);
  int detach=!p->exiting && !p->crashed; p->exiting=1;            /* DllMain(DETACH) runs once, and not after a crash (as on Windows) */
  if(detach) dll_exit(p->pid); else dll_cleanup(p->pid);
  vmem_cleanup(p->pid);
  heap_free_process(p->pid);                          /* its HeapAlloc / malloc memory it never freed */
  kobj_proc_exit(p->pid);                             /* and the events, mutexes, threads ... it still had handles to */
  { extern void veh_proc_exit(int); veh_proc_exit(p->pid); }   /* its vectored exception handlers */
  pe_unload(p->img.base);
  p->used=0;
  for(;;) schedule();                                 /* never resumed */
}

void proc_terminate(int code){ cur->crashed=1; proc_exit(code); }   /* end the program without running its DLLs' detach code (crashes, fail-fast) */
void proc_kill(int pid){                             /* close button on a window whose program has no message loop */
  if(pid<=0||pid>=MAXPROC||!procs[pid].used) return;
  struct proc *p=&procs[pid];
  if(p==cur) proc_exit(0xC000013A);
  release_pipes(p);
  con_quiet++; kprintf("\n< %s closed\n",p->name); con_quiet--;
  wm_cleanup(pid); p->used=0; thread_kill_all(pid); dll_cleanup(pid); vmem_cleanup(pid); heap_free_process(pid); kobj_proc_exit(pid); { extern void veh_proc_exit(int); veh_proc_exit(pid); } pe_unload(p->img.base);
}

static int name_matches(const char *app, const char *want){             /* "notepad" matches "notepad.exe" */
  if(!stricmp(app,want)) return 1;
  unsigned l=strlen(app); return l>4 && !stricmp(app+l-4,".exe") && strlen(want)==l-4 && !strncmp_ci(app,want,l-4);
}
static char next_exe[128];                            /* canonical path of the .exe about to start (proc_set_next_exe) */
void proc_set_next_exe(const char *canon){ unsigned k=0; if(canon) for(;canon[k]&&k<127;k++) next_exe[k]=canon[k]; next_exe[k]=0; }
static int spawn_core(const char *display, const void *data, u64 size, const char *cmdline, const char *cwd){
  char exe[128]; memcpy(exe,next_exe,128); next_exe[0]=0;
  int slot=-1; for(int i=1;i<MAXPROC;i++) if(!procs[i].used){ slot=i; break; }
  if(slot<0){ kputs("proc: too many processes\n"); return -1; }
  struct proc *p=&procs[slot];
  con_quiet++; kprintf("\n> Starting %s (%u bytes)\n",display,(u32)size);
  struct pe_image img; dll_cleanup(slot); memcpy(procs[slot].exe,exe,128); pe_owner=slot; int bad=pe_load(data,size,&img); pe_owner=0; con_quiet--;
  if(bad){ dll_cleanup(slot); return -1; }
  if(p->cap) heap_free(p->cap);
  if(vmem_owns(p->img.stack_lo)) vmem_release((void*)p->img.stack_lo);       /* the previous program's stack in this slot (it no longer runs) */
  memset(p,0,sizeof *p); p->used=1; p->pid=slot; memcpy(p->exe,exe,128); ucrt_proc_reset(slot); thread_proc_reset(slot); com_proc_reset(slot); { void win32_atexit_reset(int pid); win32_atexit_reset(slot); void env_reset(int pid); env_reset(slot); }
  unsigned i=0; for(;display[i]&&i<31;i++) p->name[i]=display[i]; proc_last_exit[slot]=259;
  if(cwd){ unsigned k=0; for(;cwd[k]&&k<127;k++) p->cwd[k]=cwd[k]; p->cwd[k]=0; }
  p->img=img;
  win32_init_proc(p,&img,cmdline? cmdline : display);
  kdbg("  [proc] pid %d command line: %s (%d arguments)\n",slot,p->cmdline,p->argc);
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
