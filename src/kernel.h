#pragma once
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
struct font;
typedef signed int i32; typedef signed long long i64; typedef u64 uptr;
#define NULL ((void*)0)
#define WINAPI __attribute__((ms_abi))       /* Win64 calling convention: rcx rdx r8 r9 + shadow space */
typedef __builtin_ms_va_list margs;

struct regs { u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax, vec,err, rip,cs,rflags,rsp,ss; };
struct mb_module { u32 start, end, cmdline, pad; };
struct mb_info { u32 flags, mem_lower, mem_upper, boot_dev, cmdline, mods_count, mods_addr;
  u32 syms[4], mmap_len, mmap_addr, drives_len, drives_addr, config, loader_name, apm,
      vbe_ctrl, vbe_mode_info; u16 vbe_mode, vbe_seg, vbe_off, vbe_len;
  u32 fb_addr_lo, fb_addr_hi, fb_pitch, fb_w, fb_h; u8 fb_bpp, fb_type; } __attribute__((packed));

static inline void outb(u16 p, u8 v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl(u16 p, u32 v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline u32 inl(u16 p){ u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline u8 inb(u16 p){ u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }

/* console.c  (printf family is ms_abi so Windows-style varargs can be forwarded) */
void console_init(void); void kputc(char c); void kputs(const char *s);
void WINAPI kprintf(const char *fmt, ...); void WINAPI kdbg(const char *fmt, ...);   /* kdbg: serial log only */ void WINAPI kvprintf(const char *fmt, margs ap);
int WINAPI kvsnprintf(char *buf, u32 n, const char *fmt, margs ap); int WINAPI ksnprintf(char *buf, u32 n, const char *fmt, ...);
/* gfx.c: linear framebuffer (0xRRGGBB colours) */
int gfx_init(struct mb_info *mbi); extern int gfx_ok; extern u32 gfx_w, gfx_h;
void gfx_pixel(int x,int y,u32 rgb); void gfx_rect(int x,int y,int w,int h,u32 rgb);
void gfx_char(int x,int y,char c,u32 fg,u32 bg,int opaque); void gfx_text(int x,int y,const char*s,int n,u32 fg,u32 bg,int opaque);
void gfx_blit(int x,int y,int w,int h,const u32 *src); void gfx_scroll_up(int px,u32 bg); u32 gfx_get(int x,int y);
int gfx_offscreen_begin(void); void gfx_present(void);
void gfx_cursor_enable(int x,int y); void gfx_cursor_shape(int kind);
void ui_text_to(const struct font *f,u32 *dst,int stride,int dw,int dh,int x,int y,const char *s,int n,u32 fg); void gfx_blit_stride(int x,int y,int w,int h,const u32 *src,int stride); void gfx_blit_alpha(int x,int y,int w,int h,const u32 *src,int stride); void gfx_cursor_move(int x,int y); void gfx_cursor_show(int on);
/* text (font.c / gfx.c): proportional, anti-aliased when fontdata.c exists; y is the top of the line box */
struct font;
const struct font *font_find(int style, int px);
int ui_width(const struct font *f, const char *s, int n); int ui_lineh(const struct font *f); int ui_ascent(const struct font *f);
void ui_text(const struct font *f, int x, int y, const char *s, int n, u32 fg);
void gfx_rect_alpha(int x,int y,int w,int h,u32 rgb,int alpha); void gfx_round_rect(int x,int y,int w,int h,int r,u32 rgb);
void gfx_gradient(int x,int y,int w,int h,u32 top,u32 bottom,int horizontal);
void gfx_line_aa(int x0,int y0,int x1,int y1,int thick16,u32 c); int gfx_corner_cov(int r,int i,int j);
u32 gfx_blend(u32 d,u32 s,int a); u32 gfx_raw_get(int x,int y); void gfx_raw_put(int x,int y,u32 c);
/* mouse.c */
int mouse_init(void); int mouse_has_wheel(void); extern volatile int mouse_x, mouse_y, mouse_buttons, mouse_wheel;
/* keyboard.c */
void keyboard_init(void); int kb_getc(void); int kb_poll(void); int kb_vk(int vk);
void ci_push(int vk, int ch, int down); void ci_key(int vk, int ch); int ci_count(void); int ci_pop(int *vk, int *ch, int *down, int *st, int remove); void ci_flush(void);   /* console input events */
int kb_event(int *vk, int *ch); void kb_flush(void);   /* key-down events: virtual-key code + translated char */
int kb_readline(char *buf,int max);
/* string.c */
void *memcpy(void*, const void*, unsigned long); void *memset(void*, int, unsigned long);
void *memmove(void*, const void*, unsigned long); int memcmp(const void*, const void*, unsigned long);
unsigned long strlen(const char*); int strcmp(const char*, const char*); int stricmp(const char*, const char*);
/* mm.c / vm.c */
void mm_init(u64 heap_start, u64 heap_end); void mm_guard_null(void); void *kmalloc(u64 size, u64 align);
void mm_set_wc(u64 phys, u64 len); void *heap_alloc(u64 n); void *heap_alloc_prog(u64 n); void *heap_alloc_for(int pid, u64 n); void *heap_alloc_for64(int pid, u64 n); void heap_free_process(int pid); void heap_free(void *p); u64 heap_size(void *p); void *heap_realloc(void *p, u64 n);
int vm_map(u64 virt, u64 phys, u64 len);       /* 2MB-granular mapping above 4GB */
/* idt.c */
void idt_init(void); void timer_init(void); extern volatile u32 ticks;
/* pe.c / win32.c */
struct pe_image { u64 base, entry, stack_top, stack_lo, tls_start, tls_end, tls_index, tls_callbacks; u32 tls_zero, exp_rva, exp_size, size; u8 is32; };   /* size: SizeOfImage */   /* is32: a 32-bit (PE32) program, run in compatibility mode */
int pe_load(const void *file, u64 size, struct pe_image *out); int pe_load_ex(const void *file, u64 size, struct pe_image *out, int dll); void pe_unload(u64 base); extern int pe_owner;
/* dll.c: real DLL loading (LoadLibrary / static imports of DLLs found in the file system) */
void *dll_import(const char *dll, const char *fn, int owner); void dll_init_owner(int owner); void dll_exit(int owner); void dll_cleanup(int owner);
u64 dll_module_at(u64 a); void *dll_find_export(int owner, const char *name);u64 win32_fake_module(const char *name); const char *win32_fake_name(u64 h); u64 win32_fake_at(u64 a); void pe_register_mapping(u64 base, u64 span); void *pe_trace_wrap(const char *dll, const char *fn, void *target); int pe_find_mapping(u64 a, u64 *base, u64 *span);
void dll_tls_fill(int owner, u64 *arr); void dll_thread_notify(int owner, u32 reason); int dll_addr_name(u64 a, char *out, int max);
u64 dll_loadlib(const char *name); u64 dll_modhandle(const char *name); void *dll_getproc(u64 h, const char *name, int *real); int dll_freelib(u64 h); const char *dll_modpath(u64 h);
int win32_dll_is_builtin(const char *name);
void *win32_resolve(const char *dll, const char *name);

/* proc.c: cooperative multitasking. Processes switch only when they wait (GetMessage, Sleep, keyboard input). */
#define MAXPROC 16
struct msg { u64 hwnd; u32 msg; u64 wp; i64 lp; u32 tid; };     /* tid: the thread whose message loop receives it (Windows queues are per thread) */
#define MSGQ 256
struct proc {
  int used, pid; char name[32];
  u64 rsp; struct pe_image img;                                   /* saved kernel rsp, loaded image */
  u64 *teb, *peb; u64 imagebase; char cmdline[2048]; u16 cmdline_w[2048]; char argbuf[2048]; char *argvp[65]; int argc; char cwd[128];   /* command line: 2047 characters, 64 arguments */
  u8 main_suspended;                                            /* CreateProcess with CREATE_SUSPENDED: the main thread waits for ResumeThread */
  u8 tls_used[64], fls_used[128]; u64 fls_slots[128];
  struct msg q[MSGQ]; u32 qh, qt; int quit, quit_code;
  char *cap; u32 cap_w, cap_r; u8 cap_on;                         /* console output captured for the parent (terminal) instead of the Console window */
  u64 out_h, in_h;                                                /* pipe handles standing in for stdout/stdin (CreateProcess with STARTF_USESTDHANDLES) */
  u64 inh[64]; int ninh;                                          /* handles inherited from the parent (its own references, closed when it exits) */
  char exe[128];                                                  /* canonical path of the program file (GetModuleFileName), empty for built-in apps */
  u8 con_in;                                                     /* the program reads console input (so the terminal keeps it attached and forwards keys) */
  u64 wow_rsp; u8 *teb32;                                       /* 32-bit programs: saved 64-bit stack while the program runs, and its 32-bit TEB (FS base) */
  u8 main_done;                                                 /* the main thread called ExitThread while other threads run on */
  u8 exiting, crashed;                                          /* proc_exit has started; the program crashed (no DllMain detach) */
  u64 fls_cb[128];                                              /* FlsAlloc callbacks */
};
/* thread.c: extra threads of a process (the main thread is the proc itself). Same cooperative scheduling as processes. */
#define MAXTHR 640                                              /* Firefox: ~60-100 threads in each of its processes */
struct thread { int used, pid; u64 rsp; u64 *teb; u8 *stack; u32 tid; u8 done, suspended, zombie; u32 code; u64 fn, arg; u64 apc_fn[8], apc_arg[8]; int napc; char name[32]; };   /* name: SetThreadDescription */
extern struct thread threads[MAXTHR]; extern struct thread *cur_thr; extern u64 *cur_teb;   /* cur_thr: 0 while a main thread runs; cur_teb: TEB of the running thread */
void thread_kill_all(int pid); int kobj_close(u64 h); int kobj_addref(u64 h); int kobj_addref_for(u64 h, int pid); int kobj_close_for(u64 h, int pid); void kobj_proc_exit(int pid); int kobj_is_handle(u64 h); int kobj_signaled(u64 h); u32 thread_cur_tid(void);
int thread_runnable(int i); void thread_free_zombies(void); u64 thread_main_handle(int pid);
extern volatile int debug_dump_request; void debug_dump(void); void thread_note_wait(const char *what, u64 arg, u64 ra); void thread_end_wait(void);   /* Ctrl+F12: every process and thread, what it waits for, and its stack, to the serial log */
/* vmem.c: VirtualAlloc & co. */
void vmem_cleanup(int pid); int vmem_owns(u64 a); void *vmem_alloc(u64 n); void *vmem_alloc_stack(u64 n); int vmem_grow_stack(u64 addr); void vmem_release(void *p); u64 vmem_committed(void); u64 vmem_reserved(void); u64 vmem_free_pages(void);
void win32_set_last_error(u32 e); u32 win32_get_last_error(void); u64 *win32_cur_teb(void);
/* wow.c: 32-bit (PE32) programs in compatibility mode */
int pe32_load(const void *file, u64 size, struct pe_image *out); void wow_init_proc(struct proc *p, const struct pe_image *img); void wow_load_fs(struct proc *p);
u64 wow_enter(u64 entry, u64 esp, u64 *save_slot); u32 wow_import(const char *dll, const char *name);
/* ipc.c: anonymous and named pipes, file mappings (shared memory) */
struct ramfile; struct export;
int pipe_is_handle(u64 h); int pipe_read(u64 h, void *buf, u32 n, u32 *got); int pipe_write(u64 h, const void *buf, u32 n, u32 *wrote); void pipe_close(u64 h); void pipe_addref(u64 h);
void pipe_addref_for(u64 h, int pid); void pipe_close_for(u64 h, int pid);   /* references held by a given process (released when it exits) */
int pipe_is_path(const char *name); void pipe_proc_exit(int pid); u64 pipe_open_client(const char *name);
int map_is_handle(u64 h); void map_close(u64 h); void map_addref(u64 h);
int fs_set_eof(u64 h); struct ramfile *fs_handle_file(u64 h); int fs_file_write_at(struct ramfile *f, u64 pos, const void *d, u64 n);
extern struct proc procs[MAXPROC]; extern struct proc *cur;
void app_register(const char *name, const void *data, u64 size, int demo); int app_count(void); const char *app_name(int i); int app_is_demo(int i);
int proc_spawn(const char *name);                 /* start a registered .exe; returns pid or -1 */
int proc_spawn_cmd(const char *name, const char *cmdline); int proc_spawn_cwd(const char *name, const char *cmdline, const char *cwd);
int proc_spawn_data(const char *name, const void *data, u64 size, const char *cmdline, const char *cwd);
void proc_set_next_exe(const char *canon);      /* path of the file the next proc_spawn_data runs (GetModuleFileName, DLL search in the program's folder) */
int shell_open(const char *path);          /* open a file or folder with the right program (double-click behaviour) */   /* name may omit .exe; cmdline is what GetCommandLine returns */
extern int proc_last_exit[MAXPROC];
u64 mm_free(void); u64 mm_total(void);
void mm_high_init(u64 phys, u64 len); u64 mm_high_page(void); u64 mm_v2p(u64 v); u64 mm_p2v(u64 p); u64 mm_high_total(void);   /* RAM above 4 GB */
u64 mm_chunk_alloc(void); void mm_chunk_free(u64 phys); u64 vm_unmap2m(u64 virt);                /* 2 MB chunks for program images */
int vm_map_page(u64 virt, u64 phys); int vm_map_page_ro(u64 virt, u64 phys); u64 vm_page_entry(u64 virt); u64 vm_unmap_page(u64 virt);
int pe_cow_fault(u64 addr);                                                                         /* a write to a shared image page */ u64 vmem_page_alloc(void); void vmem_page_free_phys(u64 phys);   /* 4 KB image pages */
void proc_exit(int code) __attribute__((noreturn)); void proc_terminate(int code) __attribute__((noreturn));void proc_kill(int pid);
void schedule(void); void idle_wait(void); int procs_alive(void);
void win32_init_proc(struct proc *p, const struct pe_image *img, const char *name);
void win32_set_context(struct proc *p); void win32_run_tls_callbacks(struct proc *p);
/* power.c */
void power_off(void) __attribute__((noreturn)); void power_restart(void) __attribute__((noreturn));
/* wm.c desktop */
void wm_desktop_start(void); void wm_service(void); void wm_cleanup(int pid); int wm_desktop_on(void);
void wm_console_dirty(void);
/* GDI surfaces: a process that calls GetDC gets a window with a backing bitmap */
u64 wm_get_dc(u64 hwnd);
void wm_gdi_rect(u64 dc,int x,int y,int w,int h,u32 rgb); void wm_gdi_pixel(u64 dc,int x,int y,u32 rgb);
void wm_gdi_text(u64 dc,const struct font *f,int x,int y,const char *s,int n,u32 fg,u32 bg,int opaque); int wm_proc_focused(void); int wm_screen_h(void); int wm_screen_w(void); void wm_cursor_pos(int *x,int *y);
/* console window buffer (console.c) */
#define CON_COLS 80
#define CON_ROWS 25
extern char con_buf[CON_ROWS][CON_COLS]; extern int con_cx, con_cy, con_on, con_quiet;   /* con_quiet: kernel log, don't pop up the window */
void console_desktop(int on);
extern volatile int kb_desktop; void kb_inject(char c);
/* exported Win32 functions: tables live in win32.c, wm.c, fs.c */
struct export { const char *dll, *name; void *fn; };
extern const struct export wm_exports[], fs_exports[]; extern const unsigned wm_nexports, fs_nexports;
/* fs.c: flat RAM filesystem (files come from GRUB modules; fopen/fwrite can create and modify) */
struct ramfile { char name[128]; u8 *data; u64 size, cap; int used; u8 persist, dirty, isdir, boot, heapd; u64 mtime; };   /* mtime: FILETIME of the last change (0 = unchanged since boot) */
u64 fs_mtime(const struct ramfile *f);           /* the time programs see as the file's creation/access/write time */   /* heapd: data came from heap_alloc (can be freed/replaced) */   /* name = full path below C:\ */
void fs_init(void); void fs_add(const char *name, const void *data, u64 size); extern int fs_gen;
void fs_canon(const char *in, char *out, int max); const char *fs_leaf(const struct ramfile *f);
struct ramfile *fs_create(const char *path); int fs_mkdir(const char *path); int fs_remove(const char *path); int fs_remove_tree(const char *path);
int fs_rename(const char *from, const char *to); int fs_isdir(const char *path); int fs_list(const char *dir, struct ramfile **out, int max);
int fs_chdir(const char *path); const char *fs_cwd(void);
int fs_count(void); struct ramfile *fs_get(int i); struct ramfile *fs_find(const char *path);
/* ata.c + fs.c persistence: modified files are saved in a data area of the boot disk (LBA 122880, 1 GB) */
#define DATA_LBA 122880
#define DATA_SECTORS 2097152
int ata_init(void); u32 ata_sectors(void); int ata_read(u32 lba,u32 n,void *buf); int ata_write(u32 lba,u32 n,const void *buf);
int fs_load_disk(void); int fs_persistence(void);
int fs_is_file(void *fp); int fs_is_handle(u64 h);
int fs_hread(u64 h, void *buf, u32 n, u32 *got); int fs_hwrite(u64 h, const void *buf, u32 n, u32 *wrote); int fs_hclose(u64 h); void fs_hseek(u64 h, u64 pos);
/* wm.c */
int wm_active(void);

/* e1000.c: Intel PRO/1000 NIC (PCI) */
int nic_init(u8 mac[6]); int nic_send(const void *frame, u32 len); const u8 *nic_rx(u32 *len); void nic_rx_done(void);
/* net.c: Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS/TCP. IPv4 addresses here are host-order numbers (a<<24|b<<16|c<<8|d). */
int net_init(void); void net_poll(void); int net_up(void); u32 net_ip(void); void net_ip_str(u32 ip, char *out); void net_info(u32 *mask, u32 *gw, u32 *dns, u8 *mac);
int tcp_connect(u32 ip, u16 port); int tcp_listen(u16 port); int tcp_accept(int lis, int nb, u32 *ip, u16 *port);
int tcp_write(int s, const void *d, u32 n); int tcp_read(int s, void *d, u32 n, int nb); void tcp_close(int s);
int tcp_state(int s); int tcp_avail(int s); int tcp_pending(int s); int tcp_rst(int s); int tcp_writable(int s);
int tcp_connect_start(u32 ip, u16 port); int tcp_connect_failed(int s); int tcp_connected(int s); int tcp_write_nb(int s, const void *d, u32 n, int nb); u16 tcp_local_port(int s); u16 tcp_free_port(void);
int udp_open(u16 port); int udp_sendto(int s, u32 ip, u16 port, const void *d, u32 n);
int udp_recvfrom(int s, void *d, u32 n, u32 *ip, u16 *port, int nb); void udp_close(int s); int udp_avail(int s);
int dns_resolve(const char *name, u32 *ip); int net_ping(u32 ip, u32 timeout_ms);
void *pe_make_stub(const char *dll, const char *name); extern const struct export crt2_exports[], math_exports[], nt2_exports[], hw_exports[]; extern const unsigned crt2_nexports, math_nexports, nt2_nexports, hw_nexports; const char *hw_gpu_name(void); extern const struct export settings_exports[], reg_exports[], y_exports[], ipc_exports[], nt_exports[], con_exports[]; extern const unsigned settings_nexports, reg_nexports, y_nexports, ipc_nexports, nt_nexports, con_nexports; void settings_load(void); void settings_set(const char *k,const char *v); const char *settings_get(const char *k); void wm_post_msg(u64 hwnd,u32 msg,u64 wp,i64 lp); void ws_async_poll(void); void *pe_make_soft_stub(const char *name);
extern const struct export x_exports[]; extern const unsigned x_nexports;
extern const struct export thread_exports[], vmem_exports[], math2_exports[], ucrt_exports[], sec_exports[], dwrite_exports[]; extern const unsigned thread_nexports, vmem_nexports, math2_nexports, ucrt_nexports, sec_nexports, dwrite_nexports;
void ucrt_proc_reset(int pid); void thread_proc_reset(int pid); void com_proc_reset(int pid);   /* per-process tables, cleared when a slot gets a new process */
extern const struct export net_exports[]; extern const unsigned net_nexports;

/* rtc.c */
u64 rtc_unix(void); u64 time_now(void); void unix_to_civil(u64 t,int *y,int *mo,int *d,int *h,int *mi,int *s,int *wd);
/* tls.c: HTTPS support (BearSSL) */
int tls_open(const char *host, int sock);               /* handshake over an established TCP socket; returns a handle or -1 */
int tls_write(int h, const void *d, u32 n); int tls_read(int h, void *d, u32 n); void tls_close(int h);
void random_bytes(void *buf, u32 n); extern const char *tls_last_error;
/* inet.c: WinINet / URLMon / BCrypt export table; fs.c helpers for downloads */
extern const struct export inet_exports[]; extern const unsigned inet_nexports;
struct ramfile *fs_open_write(const char *name); int fs_append(struct ramfile *f, const void *d, u64 n); void fs_commit(struct ramfile *f);
/* desktop.c: icons, selection, context menus on the desktop */
void desktop_draw(void); void desktop_ctx_draw(void); int desktop_ctx_click(int mx,int my); int desktop_ctx_open(void);
void desktop_hover(int mx,int my); void desktop_click(int mx,int my); void desktop_rclick(int mx,int my);
void desktop_defocus(void); int desktop_has_focus(void); int desktop_key(int vk,int ch);
void fs_flush(void); void fs_service(void);   /* fs_service: deferred save, called by the main loop */
