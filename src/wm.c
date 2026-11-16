#include "kernel.h"
#include "font.h"
/* A small window manager + user32 layer: window classes, window procedures, a message queue,
   title bars/drag/focus, and built-in EDIT / BUTTON / STATIC controls. Windows repaint themselves
   (apps are not asked for WM_PAINT yet). */
typedef i64 (WINAPI *wndproc_t)(u64, u32, u64, i64);
typedef u64 HANDLE;
#define WM_CREATE 0x1
#define WM_DESTROY 0x2
#define WM_SIZE 0x5
#define WM_SETFOCUS 0x7
#define WM_KILLFOCUS 0x8
#define WM_SETTEXT 0xC
#define WM_GETTEXT 0xD
#define WM_GETTEXTLENGTH 0xE
#define WM_PAINT 0xF
#define WM_CLOSE 0x10
#define WM_QUIT 0x12
#define WM_ERASEBKGND 0x14
#define WM_SHOWWINDOW 0x18
#define WM_NCCREATE 0x81
#define WM_KEYDOWN 0x100
#define WM_CHAR 0x102
#define WM_COMMAND 0x111
#define WM_MOUSEMOVE 0x200
#define WM_W_CLICK 0x03EF                                           /* internal: a click on a program's window, decided in its thread (system range, never sent by programs) */
#define WM_LBUTTONDOWN 0x201
#define WM_LBUTTONUP 0x202
#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define WS_BORDER 0x00800000u
#define WS_CAPTION 0x00C00000u
#define CW_USEDEFAULT 0x80000000u
#define TITLE_H 32
#define FRAME 1
#define MAXW 512
enum { K_APP, K_EDIT, K_BUTTON, K_STATIC, K_CONSOLE, K_SURFACE, K_CTRL, K_DIALOG };
typedef short i16;
enum { CT_PUSH = 1, CT_CHECK, CT_RADIO, CT_GROUP, CT_STATIC, CT_EDIT, CT_LIST, CT_COMBO, CT_TREE, CT_LINE };

#define WM_MOUSEWHEEL 0x20A
#define DCBASE 0x4000
extern const unsigned char font8x16[128][16];
#define B_EDIT 1
#define B_BUTTON 2
#define B_STATIC 3
#define B_CTRL 4
#define B_DIALOG 5
#define HW(i) (0x10000ull+(u64)(i)*4)
#define COL_FACE 0xD4D0C8
static const struct font *mono_f; static int cell_w=8, cell_h=16;
static void mono_init(void){
  if(mono_f) return; mono_f=font_find(FS_MONO,15);
  if(mono_f->nglyph){ cell_w=(ui_width(mono_f,"MMMMMMMMMM",10)+5)/10; cell_h=ui_lineh(mono_f); }
}
static void cell_char(int x,int y,char c,u32 fg){                          /* one character cell, top-left at (x,y) */
  mono_init(); if(mono_f->nglyph){ if(c>32) ui_text(mono_f,x,y,&c,1,fg); } else gfx_char(x,y,c,fg,0,0);
}

struct wnd {
  u8 used, visible, kind, owner, minimized, maximized; i32 rx, ry, rw, rh; i32 pw, ph; u64 parent; u32 style, exstyle; i32 x, y, w, h; u64 proc, userdata, id, extra[8];
  char cls[24]; char *text; u32 tlen, tcap;
  u32 caret, top_line, left_col; u8 pressed, paint_pending, mm_pending; u32 *pix; i32 stride;
  void *ext; u8 ctype, disabled, sized; i32 check; u32 seq;
  u32 tid;                                                        /* the thread that created it: its messages go to that thread's loop */
  u64 ownerw;                                                     /* top-level windows: the owner window (hWndParent of a popup) */
  u8 custom_frame;                                                /* the program draws its own title bar (WM_NCCALCSIZE gave it the whole window) */
  u8 alpha;                                                       /* frameless popup whose pixels carry alpha (Firefox menus): blended over what is behind */
  u8 ownpix;                                                      /* pix was allocated for this window (backing store): freed with it */
};
static u32 g_seq;
static struct wnd W[MAXW];
static u64 Z[MAXW]; static int nz;                 /* top-level windows, back to front */
static u64 g_focus, g_capture, g_drag; static int drag_dx, drag_dy;
static u64 g_track_leave;                                          /* the window that asked for WM_MOUSELEAVE (TrackMouseEvent) */
static int dirty=1, created, wm_on, start_open, start_hover=-1;
static u64 con_win;

static struct wnd *get(u64 h){
  if(h<0x10000 || ((h-0x10000)&3)) return 0;
  u64 i=(h-0x10000)>>2; return (i<MAXW && W[i].used)? &W[i] : 0;
}
static u64 hof(struct wnd *w){ return HW(w-W); }
static u64 top_visible(void){ for(int i=nz-1;i>=0;i--){ struct wnd *w=get(Z[i]); if(w&&w->visible&&!w->minimized) return Z[i]; } return 0; }
int wm_active(void){ return created; }

/* ---- window classes ---- */
/* WNDCLASSA and WNDCLASSEXA have the same x64 layout from offset 8 on: proc 8, cbClsExtra 16, cbWndExtra 20, hInstance 24, hIcon 32,
   hCursor 40, hbrBackground 48, menu name 56, class name 64. A class name may also be given as the atom RegisterClass returned. */
#define MAXCLS 256                                                  /* per-process classes: every Firefox process registers its own */
struct cls { char name[64]; u64 proc, inst, icon, cursor, bg64; u32 bg, style; i32 clsextra, wndextra; int used, owner; };
static struct cls classes[MAXCLS];
static void cpy(char *d, const char *s, int n){ int i=0; for(;s[i]&&i<n-1;i++) d[i]=s[i]; d[i]=0; }
/* window classes belong to the process that registered them (as on Windows): another process - or a new run of the same program
   - registering the same name gets its own class, with its own window procedure */
static int class_mine(const struct cls *c){ return c->used && (c->owner==0 || (cur && c->owner==cur->pid)); }
static struct cls *find_class(const char *n){
  if((u64)n>=0xC000 && (u64)n<0xC000+MAXCLS){ struct cls *c=&classes[(u64)n-0xC000]; return class_mine(c)? c : 0; }   /* an atom */
  if((u64)n<0x10000) return 0;
  for(int i=0;i<MAXCLS;i++) if(class_mine(&classes[i]) && stricmp(classes[i].name,n)==0) return &classes[i]; return 0;
}
static u64 WINAPI w_RegisterClassA(const u8 *wc){
  const char *n=*(const char**)(wc+64); if((u64)n<0x10000) return 0;
  if(find_class(n)){ win32_set_last_error(1410); return 0; }                    /* ERROR_CLASS_ALREADY_EXISTS */
  for(int i=0;i<MAXCLS;i++) if(!classes[i].used){
    struct cls *c=&classes[i]; memset(c,0,sizeof *c); c->used=1; c->owner=cur->pid;
    c->style=*(u32*)(wc+4); c->proc=*(u64*)(wc+8); c->clsextra=*(i32*)(wc+16); c->wndextra=*(i32*)(wc+20); c->inst=*(u64*)(wc+24);
    c->icon=*(u64*)(wc+32); c->cursor=*(u64*)(wc+40); c->bg64=*(u64*)(wc+48); c->bg=(u32)c->bg64;
    cpy(c->name,n,64); return 0xC000+i;
  }
  win32_set_last_error(8); return 0;
}
/* GetClassInfo(Ex): fills WNDCLASS(EX) from offset 4 (style) on; the name pointer is the one the caller passed */
static u64 class_info(const char *n, const void *callers_name, u8 *wc, int ex){
  struct cls *c=find_class(n); if(!c){ win32_set_last_error(1411); return 0; }    /* ERROR_CLASS_DOES_NOT_EXIST */
  *(u32*)(wc+4)=c->style; *(u64*)(wc+8)=c->proc; *(i32*)(wc+16)=c->clsextra; *(i32*)(wc+20)=c->wndextra; *(u64*)(wc+24)=c->inst;
  *(u64*)(wc+32)=c->icon; *(u64*)(wc+40)=c->cursor; *(u64*)(wc+48)=c->bg64; *(u64*)(wc+56)=0; *(const void**)(wc+64)=callers_name; if(ex) *(u64*)(wc+72)=c->icon;
  return 0xC000+(u64)(c-classes);
}
static i32 WINAPI w_GetClassInfoA(u64 inst, const char *n, u8 *wc){ (void)inst; return class_info(n,n,wc,0)!=0; }
static u64 WINAPI w_GetClassInfoExA(u64 inst, const char *n, u8 *wc){ (void)inst; return class_info(n,n,wc,1); }
static i32 WINAPI w_UnregisterClassA(const char *n, u64 inst){ (void)inst; struct cls *c=find_class(n); if(!c){ win32_set_last_error(1411); return 0; } c->used=0; return 1; }

/* ---- text storage ---- */
static void settext(struct wnd *w, const char *s){
  u32 n=s?strlen(s):0;
  if(n+1>w->tcap){ u32 c=(n+1)*2+32; char *nb=kmalloc(c,16); if(!nb) return; w->text=nb; w->tcap=c; }
  if(n) memcpy(w->text,s,n); w->text[n]=0; w->tlen=n;
}

/* ---- geometry ---- */
static int has_frame(struct wnd *w){ return !(w->style&WS_CHILD) && (w->style&WS_CAPTION)==WS_CAPTION && !w->custom_frame; }
static void client_size(struct wnd *w, int *cw, int *ch){
  if(has_frame(w)){ *cw=w->w-2*FRAME; *ch=w->h-FRAME-TITLE_H-FRAME; } else { *cw=w->w; *ch=w->h; }
}
static void client_origin(struct wnd *w, int *ox, int *oy){        /* absolute screen position of the client area */
  if(w->style&WS_CHILD){
    struct wnd *p=get(w->parent); int px=0, py=0; if(p) client_origin(p,&px,&py); *ox=px+w->x; *oy=py+w->y;
  } else if(has_frame(w)){ *ox=w->x+FRAME; *oy=w->y+FRAME+TITLE_H; } else { *ox=w->x; *oy=w->y; }
}
static void outer_origin(struct wnd *w, int *ox, int *oy){
  if(has_frame(w)){ *ox=w->x; *oy=w->y; } else client_origin(w,ox,oy);
}

/* ---- message plumbing ---- */
static i64 builtin_proc(int id, u64 h, u32 m, u64 wp, i64 lp);
static i64 ctrl_proc(u64 h, u32 m, u64 wp, i64 lp);
static i64 dlg_proc(u64 h, u32 m, u64 wp, i64 lp);
static i64 call_proc(u64 proc, u64 h, u32 m, u64 wp, i64 lp){
  if(proc<16) return builtin_proc((int)proc,h,m,wp,lp);
  return ((wndproc_t)proc)(h,m,wp,lp);
}
static const char *narrowed_text;                                     /* WM_SETTEXT text Willows hands to a window procedure (always narrow; see DefWindowProcW) */
static i64 send(u64 h, u32 m, u64 wp, i64 lp){
  struct wnd *w=get(h); if(!w) return 0;
  if(m!=0x000C) return call_proc(w->proc,h,m,wp,lp);                   /* WM_SETTEXT */
  const char *o=narrowed_text; narrowed_text=(const char*)lp; i64 r=call_proc(w->proc,h,m,wp,lp); narrowed_text=o; return r;
}
/* every process has one queue; each message is tagged with the thread that owns the target window, and a thread's GetMessage /
   PeekMessage only see its own messages (as with Windows' per-thread queues) */
static void post_to(struct proc *p, u32 tid, u64 h, u32 m, u64 wp, i64 lp){
  u32 n=(p->qh+1)&(MSGQ-1); if(n!=p->qt){ p->q[p->qh]=(struct msg){h,m,wp,lp,tid}; p->qh=n; }
}
static int wm_trace(void){ static int t=-1; if(t<0){ struct ramfile *f=fs_find("/System/apitrace.cfg"); t= f && !f->isdir; } return t; }   /* TRACE=1: log window activity */
void wm_dump_queue(int pid){                                       /* Ctrl+F12: what sits in a process's message queue */
  struct proc *p=&procs[pid]; u32 n=(p->qh-p->qt)&(MSGQ-1); if(!n) return;
  kdbg("    message queue: %u of %u used:",n,MSGQ-1); u32 shown=0;
  for(u32 i=p->qt; i!=p->qh && shown<12; i=(i+1)&(MSGQ-1), shown++) kdbg(" [tid %x msg %x hwnd %p]",p->q[i].tid,p->q[i].msg,(void*)p->q[i].hwnd);
  kdbg("\n");
}
static void post(u64 h, u32 m, u64 wp, i64 lp){
  struct wnd *w=get(h); struct proc *p= w? &procs[w->owner] : cur;   /* PostMessage(NULL, ...): the calling thread's own queue */
  { static int t=-1; if(t<0){ struct ramfile *f=fs_find("/System/apitrace.cfg"); t= f && !f->isdir; }
    if(t && (m>=0xC000 || (w && w->tid!=thread_cur_tid()))) kdbg("  [msg] post %x to hwnd %p (tid %x, pid %d) from tid %x, queue %u\n",m,(void*)h,w? w->tid:0,(int)(p-procs),thread_cur_tid(),(p->qh-p->qt)&(MSGQ-1)); }
  if(w && m==WM_MOUSEMOVE){ if(w->mm_pending) return; w->mm_pending=1; }
  post_to(p, w? w->tid : thread_cur_tid(), h, m, wp, lp);
}

static void set_focus(u64 h){
  if(h==g_focus) return;
  u64 old=g_focus; g_focus=h; dirty=1; if(h) desktop_defocus();
  if(get(old)) post(old,WM_KILLFOCUS,h,0);
  if(get(h)) post(h,WM_SETFOCUS,old,0);
}
static void raise_one(u64 h){
  for(int i=0;i<nz;i++) if(Z[i]==h){ for(int j=i;j<nz-1;j++) Z[j]=Z[j+1]; Z[nz-1]=h; dirty=1; return; }
}
static void raise_top(u64 h){                                      /* to the front, with the windows it owns (popups, menus) above it */
  raise_one(h);
  for(int pass=0;pass<2;pass++) for(int i=0;i<MAXW;i++) if(W[i].used && W[i].ownerw==h && W[i].visible && !(W[i].style&WS_CHILD)) raise_one(HW(i));
}
static u64 first_edit(u64 parent){
  for(int i=0;i<MAXW;i++) if(W[i].used && W[i].parent==parent && W[i].kind==K_EDIT && (W[i].style&WS_CHILD)) return HW(i);
  return 0;
}
static void invalidate(struct wnd *w){                              /* ask the program to repaint (WM_PAINT, coalesced) */
  if(w->kind==K_APP && !w->paint_pending){ w->paint_pending=1; post(hof(w),WM_PAINT,0,0); }
  dirty=1;
}
/* a window was shown: top-level windows come to the front; they also get the focus unless shown without activation
   (SWP_NOACTIVATE, SW_SHOWNOACTIVATE / SW_SHOWNA, WS_EX_NOACTIVATE) - menus and tooltips must not take the focus from their owner */
static void on_show_ex(struct wnd *w, int activate){
  if(w->exstyle&0x08000000u) activate=0;                            /* WS_EX_NOACTIVATE */
  if(!(w->style&WS_CHILD)){ raise_top(hof(w)); if(activate){ u64 e=first_edit(hof(w)); set_focus(e? e : hof(w)); } }
  invalidate(w);
}
static void on_show(struct wnd *w){ on_show_ex(w,1); }

/* ---- default + built-in window procedures ---- */
static void destroy(u64 h);
static void set_max(struct wnd *w, int on, int in_thread); static void minimize(struct wnd *w);
static i64 def_proc(u64 h, u32 m, u64 wp, i64 lp){
  struct wnd *w=get(h); if(!w) return 0;
  switch(m){
    case 0x112: switch(wp&0xFFF0){                                      /* WM_SYSCOMMAND: what the caption buttons and the window menu do */
      case 0xF030: set_max(w,1,1); return 0;                            /* SC_MAXIMIZE */
      case 0xF120: if(w->minimized){ w->minimized=0; dirty=1; } else set_max(w,0,1); return 0;   /* SC_RESTORE */
      case 0xF020: minimize(w); return 0;                               /* SC_MINIMIZE */
      case 0xF060: post(h,WM_CLOSE,0,0); return 0;                      /* SC_CLOSE */
      } return 0;
    case 0x83: if(wp && lp && !(w->style&WS_CHILD) && (w->style&WS_CAPTION)==WS_CAPTION){ i32 *r=(i32*)lp; r[0]+=FRAME; r[1]+=FRAME+TITLE_H; r[2]-=FRAME; r[3]-=FRAME; }   /* WM_NCCALCSIZE: our frame */
      return 0;
    case 0x47: { const u8 *wpos=(const u8*)lp; u32 fl= wpos? *(const u32*)(wpos+32) : 0;     /* WM_WINDOWPOSCHANGED: as on Windows, WM_SIZE and WM_MOVE follow */
      if(!(fl&1)){ int a,b; client_size(w,&a,&b); send(h,WM_SIZE,w->maximized? 2 : 0,((i64)b<<16)|(a&0xFFFF)); }
      if(!(fl&2)){ int x,y; client_origin(w,&x,&y); send(h,3,0,((i64)(y&0xFFFF)<<16)|(x&0xFFFF)); }
      return 0; }
    case WM_NCCREATE: return 1;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: destroy(h); return 0;
    case WM_SETTEXT: settext(w,(const char*)lp); dirty=1; if(wm_trace() && !(w->style&WS_CHILD)) kdbg("  [wm] %p title: %s\n",(void*)h,lp? (const char*)lp : "(null)"); return 1;
    case WM_GETTEXT: { u64 n=wp? (w->tlen<wp-1? w->tlen : wp-1) : 0; if(wp){ memcpy((void*)lp,w->text?w->text:"",n); ((char*)lp)[n]=0; } return n; }
    case WM_GETTEXTLENGTH: return w->tlen;
  }
  return 0;
}

/* EDIT: multiline text with a caret (no selection yet). Newlines are CR LF as in Windows; a lone LF also breaks lines. */
static void ed_ensure(struct wnd *w, u32 extra){
  if(w->tlen+extra+1>w->tcap){ u32 c=(w->tlen+extra+1)*2+64; char *nb=kmalloc(c,16); if(!nb) return; if(w->text) memcpy(nb,w->text,w->tlen+1); w->text=nb; w->tcap=c; }
}
static void ed_insert(struct wnd *w, const char *s, u32 n){
  ed_ensure(w,n); if(!w->text) return;
  memmove(w->text+w->caret+n,w->text+w->caret,w->tlen-w->caret+1); memcpy(w->text+w->caret,s,n); w->tlen+=n; w->caret+=n;
}
static void ed_erase(struct wnd *w, u32 pos, u32 n){ memmove(w->text+pos,w->text+pos+n,w->tlen-pos-n+1); w->tlen-=n; }
static u32 line_start(struct wnd *w, u32 p){ while(p>0 && w->text[p-1]!='\n') p--; return p; }
static u32 line_end(struct wnd *w, u32 p){ while(p<w->tlen && w->text[p]!='\n' && w->text[p]!='\r') p++; return p; }
static void ed_key(struct wnd *w, int vk){
  char *t=w->text; if(!t) return; u32 c=w->caret;
  switch(vk){
    case 0x25: if(c>0){ c--; if(c>0 && t[c]=='\n' && t[c-1]=='\r') c--; } break;
    case 0x27: if(c<w->tlen){ if(t[c]=='\r' && t[c+1]=='\n') c+=2; else c++; } break;
    case 0x24: c=line_start(w,c); break;
    case 0x23: c=line_end(w,c); break;
    case 0x26: { u32 s=line_start(w,c), col=c-s; if(s==0) break; u32 e=s-1; if(e>0&&t[e-1]=='\r') e--; u32 ps=line_start(w,e); c=ps+(col<e-ps?col:e-ps); break; }
    case 0x28: { u32 e=c; while(e<w->tlen && t[e]!='\n') e++; if(e>=w->tlen) break; u32 col=c-line_start(w,c), ns=e+1, ne=line_end(w,ns); c=ns+(col<ne-ns?col:ne-ns); break; }
    case 0x2E: if(c<w->tlen){ u32 n=(t[c]=='\r'&&t[c+1]=='\n')?2:1; ed_erase(w,c,n); } break;
  }
  w->caret=c; dirty=1;
}
static void ed_char(struct wnd *w, int ch){
  if(ch==8){ if(w->caret>0){ u32 n=(w->caret>1 && w->text[w->caret-1]=='\n' && w->text[w->caret-2]=='\r')?2:1; ed_erase(w,w->caret-n,n); w->caret-=n; } }
  else if(ch==13) ed_insert(w,"\r\n",2);
  else if(ch==9) ed_insert(w,"\t",1);
  else if(ch>=32 && ch<127){ char c=ch; ed_insert(w,&c,1); }
  else if(ch>=160 && ch<256){ char u[2]={(char)(0xC0|(ch>>6)),(char)(0x80|(ch&0x3F))}; ed_insert(w,u,2); }   /* Latin-1 -> UTF-8 */
  else return;
  dirty=1;
}
static int ch_cols(char c){ return c=='\t'? 4 : 1; }
static void ed_click(struct wnd *w, int x, int y){
  mono_init(); int line=w->top_line+(y-2)/cell_h, col=w->left_col+(x-2)/cell_w; if(!w->text) return;
  u32 p=0; for(int l=0;l<line;l++){ while(p<w->tlen && w->text[p]!='\n') p++; if(p>=w->tlen) break; p++; }
  int cc=0; while(p<w->tlen && w->text[p]!='\n' && w->text[p]!='\r' && cc+ch_cols(w->text[p])<=col){ cc+=ch_cols(w->text[p]); p++; }
  w->caret=p; dirty=1;
}
static i64 builtin_proc(int id, u64 h, u32 m, u64 wp, i64 lp){
  if(id==B_CTRL) return ctrl_proc(h,m,wp,lp);
  if(id==B_DIALOG) return dlg_proc(h,m,wp,lp);
  struct wnd *w=get(h); if(!w) return 0;
  if(id==B_EDIT){
    switch(m){
      case WM_SETTEXT: settext(w,(const char*)lp); w->caret=0; w->top_line=w->left_col=0; dirty=1; return 1;
      case WM_KEYDOWN: ed_key(w,(int)wp); return 0;
      case WM_CHAR: ed_char(w,(int)wp); return 0;
      case WM_LBUTTONDOWN: set_focus(h); ed_click(w,(int)(lp&0xFFFF),(int)((lp>>16)&0xFFFF)); return 0;
    }
  } else if(id==B_BUTTON){
    switch(m){
      case WM_LBUTTONDOWN: w->pressed=1; g_capture=h; dirty=1; return 0;
      case WM_LBUTTONUP: if(w->pressed){ w->pressed=0; dirty=1;
          int x=(int)(lp&0xFFFF), y=(int)((lp>>16)&0xFFFF);
          if(x>=0&&x<w->w&&y>=0&&y<w->h) post(w->parent,WM_COMMAND,(w->id&0xFFFF),(i64)h); } return 0;
    }
  }
  return def_proc(h,m,wp,lp);
}

/* ---- window creation / destruction ---- */
static void kill_window(int i){                         /* silent removal (owner process is gone) */
  u64 h=HW(i); if(g_focus==h) g_focus=0; if(g_capture==h) g_capture=0; if(g_drag==h) g_drag=0;
  for(int k=0;k<nz;k++) if(Z[k]==h){ for(int j=k;j<nz-1;j++) Z[j]=Z[j+1]; nz--; break; }
  if(W[i].ownpix && W[i].pix){ heap_free(W[i].pix); W[i].pix=0; }
  W[i].used=0; dirty=1;
}
static void gdi_cleanup(int pid);
void wm_cleanup(int pid){
  gdi_cleanup(pid);
  for(int i=0;i<MAXW;i++) if(W[i].used && W[i].owner==pid) kill_window(i);
  for(int i=0;i<MAXCLS;i++) if(classes[i].used && classes[i].owner==pid) classes[i].used=0;   /* its window classes go with it */
  if(!get(g_track_leave)) g_track_leave=0;
  if(!g_focus) g_focus=top_visible(); dirty=1;
}
static void destroy(u64 h);
static void destroy(u64 h){
  struct wnd *w=get(h); if(!w) return;
  for(int i=0;i<MAXW;i++) if(W[i].used && W[i].parent==h) destroy(HW(i));
  send(h,WM_DESTROY,0,0);
  if(g_focus==h) g_focus=0; if(g_capture==h) g_capture=0; if(g_drag==h) g_drag=0; if(g_track_leave==h) g_track_leave=0;
  for(int i=0;i<nz;i++) if(Z[i]==h){ for(int j=i;j<nz-1;j++) Z[j]=Z[j+1]; nz--; break; }
  if(w->ownpix && w->pix){ heap_free(w->pix); w->pix=0; }
  w->used=0; dirty=1;
  if(!g_focus && nz) set_focus(top_visible());
}
static int ctrl_type_for(const char *cls, u32 style);
static void ask_frame(struct wnd *w);
static int is_dialog_wnd(u64 h);
static u64 make_ctrl(u64 parent, int ct, const char *text, u32 style, int x, int y, int w, int h, u64 id);
static u64 WINAPI w_CreateWindowExA(u32 ex, const char *cls, const char *name, u32 style, i32 x, i32 y, i32 cw, i32 ch,
                                    u64 parent, u64 menu, u64 inst, void *param){
  struct cls *c=find_class(cls);
  if(!gfx_ok || (!c && (u64)cls<0x10000)){ win32_set_last_error(1411); return 0; }
  if(c) cls=c->name;                                                               /* created by atom */
  int msgonly= parent==(u64)-3;                                                    /* HWND_MESSAGE: never shown, not in the window list */
  if(msgonly){ parent=0; style&=~(WS_VISIBLE|WS_CHILD); }
  int idx=-1; for(int i=0;i<MAXW;i++) if(!W[i].used){ idx=i; break; } if(idx<0){ kdbg("  [wm] window table full\n"); win32_set_last_error(8); return 0; }
  struct wnd *w=&W[idx]; memset(w,0,sizeof *w); w->used=1; w->owner=cur->pid; w->tid=thread_cur_tid(); cpy(w->cls,cls,24);
  w->seq=++g_seq;
  if(!c){ int ct=ctrl_type_for(cls,style); if(ct && (style&WS_CHILD) && (ct>=CT_LIST || is_dialog_wnd(parent))){ w->used=0; u64 hh=make_ctrl(parent,ct,name,style,x,y,cw,ch,menu); if(hh){ dirty=1; } return hh; } }
  if(c){ w->proc=c->proc; w->kind=K_APP; }
  else if(!stricmp(cls,"EDIT")){ w->proc=B_EDIT; w->kind=K_EDIT; }
  else if(!stricmp(cls,"BUTTON")){ w->proc=B_BUTTON; w->kind=K_BUTTON; }
  else if(!stricmp(cls,"STATIC")){ w->proc=B_STATIC; w->kind=K_STATIC; }
  else { w->used=0; return 0; }
  w->style=style; w->exstyle=ex; w->parent=(style&WS_CHILD)? parent : 0; w->id=(style&WS_CHILD)? menu : 0; w->ownerw=(style&WS_CHILD)? 0 : parent;
  if(!(style&WS_CHILD)){
    int n=nz;
    w->x=(u32)x==CW_USEDEFAULT? 60+30*n : x; w->y=(u32)y==CW_USEDEFAULT? 40+30*n : y;
    w->w=(u32)cw==CW_USEDEFAULT? 640 : cw; w->h=(u32)ch==CW_USEDEFAULT? 480 : ch;
  } else { w->x=x; w->y=y; w->w=cw; w->h=ch; }
  settext(w,name?name:"");
  u64 h=hof(w);
  if(!(style&WS_CHILD) && !msgonly) Z[nz++]=h;
  if(wm_trace() && !(style&WS_CHILD)) kdbg("  [wm] CreateWindow %p class %s style %x ex %x at %d,%d %dx%d%s (pid %d)\n",(void*)h,cls,style,ex,w->x,w->y,w->w,w->h,msgonly? " message-only" : "",cur->pid);
  created=1;
  u64 cs[10]={ (u64)param, inst, menu, parent, 0, 0, 0, 0, 0, 0 };             /* CREATESTRUCTA */
  ((i32*)cs)[8]=w->h; ((i32*)cs)[9]=w->w; ((i32*)cs)[10]=w->y; ((i32*)cs)[11]=w->x; ((u32*)cs)[12]=style;
  cs[7]=(u64)name; cs[8]=(u64)cls; ((u32*)cs)[18]=ex;
  if(w->kind==K_APP){
    if(!send(h,WM_NCCREATE,0,(i64)cs) || (i32)send(h,WM_CREATE,0,(i64)cs)==-1){ destroy(h); return 0; }
    ask_frame(w);
    if(style&WS_VISIBLE){ int cw2,ch2; client_size(w,&cw2,&ch2); w->sized=1; send(h,WM_SIZE,0,(ch2<<16)|(cw2&0xFFFF)); }     /* like Windows: WM_SIZE arrives when the window is shown */
  }
  if(style&WS_VISIBLE){ w->visible=1; on_show(w); }
  dirty=1; return h;
}
static i64 WINAPI w_DefWindowProcA(u64 h, u32 m, u64 wp, i64 lp){ return def_proc(h,m,wp,lp); }
static i32 WINAPI w_DestroyWindow(u64 h){ if(!get(h)) return 0; destroy(h); return 1; }
static void pos_changed(struct wnd *w, u32 fl);
static i32 WINAPI w_ShowWindow(u64 h, i32 cmd){
  struct wnd *w=get(h); if(!w) return 0; int was=w->visible; w->visible=cmd!=0;
  if(wm_trace() && !(w->style&WS_CHILD)) kdbg("  [wm] ShowWindow(%p %s, %d)\n",(void*)h,w->cls,cmd);
  if(w->visible && w->kind==K_APP && !w->sized){ w->sized=1; pos_changed(w,4|0x40); }   /* its size and position (WM_WINDOWPOSCHANGED, then WM_SIZE / WM_MOVE), as it is now shown */
  if(!(w->style&WS_CHILD)){                                            /* SW_MAXIMIZE, SW_RESTORE / SW_SHOWNORMAL, SW_MINIMIZE ... */
    if(cmd==3) set_max(w,1,1);
    else if(cmd==9 || cmd==1){ if(w->minimized){ w->minimized=0; dirty=1; } else if(cmd==9) set_max(w,0,1); }
    else if(cmd==6 || cmd==2 || cmd==7 || cmd==11){ minimize(w); w->visible=1; dirty=1; return was; }
  }
  if(w->visible) on_show_ex(w, cmd!=4 && cmd!=8 && cmd!=7);           /* SW_SHOWNOACTIVATE, SW_SHOWNA, SW_SHOWMINNOACTIVE */
  else if(was && g_focus==h){ u64 o=w->ownerw; set_focus(get(o)? o : top_visible()); }   /* a hidden window gives the focus back to its owner */
  dirty=1; return was;
}
/* Does the program draw its own title bar? Ask with WM_NCCALCSIZE, as Windows does: a program that keeps the area where our title
   bar would be (Firefox, Chrome, Electron apps with tabs in the title bar) gets no Willows frame; its WM_NCHITTEST answers then say
   where its caption and buttons are (click_in_context). */
static void pos_changed(struct wnd *w, u32 fl);
static void ask_frame(struct wnd *w){
  if(w->kind!=K_APP || (w->style&WS_CHILD) || (w->style&WS_CAPTION)!=WS_CAPTION) return;
  struct { i32 r[12]; u64 pos; } p; memset(&p,0,sizeof p);
  int ox=w->x, oy=w->y; p.r[0]=ox; p.r[1]=oy; p.r[2]=ox+w->w; p.r[3]=oy+w->h; for(int k=0;k<4;k++){ p.r[4+k]=p.r[k]; p.r[8+k]=p.r[k]; }
  u8 wpos[40]; memset(wpos,0,40); *(u64*)wpos=hof(w); *(i32*)(wpos+16)=ox; *(i32*)(wpos+20)=oy; *(i32*)(wpos+24)=w->w; *(i32*)(wpos+28)=w->h; *(u32*)(wpos+32)=0x20|4;   /* WINDOWPOS: SWP_FRAMECHANGED */
  p.pos=(u64)wpos;
  send(hof(w),0x83,1,(i64)&p);
  u8 custom= p.r[1]-oy < (FRAME+TITLE_H)/2;
  if(custom!=w->custom_frame){
    w->custom_frame=custom; dirty=1;
    if(wm_trace()) kdbg("  [wm] %p %s draws %s title bar\n",(void*)hof(w),w->cls,custom? "its own" : "no");
    if(w->sized) pos_changed(w,2|4);                                       /* the client area changed size: WM_WINDOWPOSCHANGED, then WM_SIZE */
  }
}
/* a window was moved or resized: WM_WINDOWPOSCHANGED with a WINDOWPOS {hwnd, after, x, y, cx, cy, flags}; DefWindowProc turns it
   into WM_SIZE / WM_MOVE (programs such as Firefox take the new size from WM_WINDOWPOSCHANGED itself) */
/* WM_WINDOWPOSCHANGING comes first, with the requested position: the program may change it (or add SWP_NOSIZE / SWP_NOMOVE) and
   that is what happens - as on Windows. Then the change is made and WM_WINDOWPOSCHANGED reports it. */
static void pos_changing(struct wnd *w, i32 *x, i32 *y, i32 *cx, i32 *cy, u32 *fl){
  if(w->kind!=K_APP) return;
  u8 wpos[40]; memset(wpos,0,40); *(u64*)wpos=hof(w); *(i32*)(wpos+16)=*x; *(i32*)(wpos+20)=*y; *(i32*)(wpos+24)=*cx; *(i32*)(wpos+28)=*cy; *(u32*)(wpos+32)=*fl;
  send(hof(w),0x46,0,(i64)wpos);                                      /* WM_WINDOWPOSCHANGING */
  *x=*(i32*)(wpos+16); *y=*(i32*)(wpos+20); *cx=*(i32*)(wpos+24); *cy=*(i32*)(wpos+28); *fl=*(u32*)(wpos+32);
}
static void pos_changed(struct wnd *w, u32 fl){
  if(w->kind!=K_APP) return;
  u8 wpos[40]; memset(wpos,0,40); *(u64*)wpos=hof(w); *(i32*)(wpos+16)=w->x; *(i32*)(wpos+20)=w->y; *(i32*)(wpos+24)=w->w; *(i32*)(wpos+28)=w->h; *(u32*)(wpos+32)=fl;
  send(hof(w),0x47,0,(i64)wpos);                                      /* WM_WINDOWPOSCHANGED */
  if(!(fl&1)) invalidate(w);
}
static i32 WINAPI w_MoveWindow(u64 h, i32 x, i32 y, i32 cw, i32 ch, i32 repaint){
  struct wnd *w=get(h); (void)repaint; if(!w) return 0;
  u32 fl=4|((w->w==cw && w->h==ch)? 1 : 0)|((w->x==x && w->y==y)? 2 : 0);   /* SWP_NOZORDER, SWP_NOSIZE, SWP_NOMOVE */
  if((fl&3)==3) return 1;
  pos_changing(w,&x,&y,&cw,&ch,&fl);
  if(!(fl&2)){ w->x=x; w->y=y; } if(!(fl&1)){ w->w=cw; w->h=ch; } dirty=1;
  pos_changed(w,fl);
  return 1;
}
#define DESKTOP_H 0x7FFF0000ull
static i32 WINAPI w_GetClientRect(u64 h, i32 *r){ if(h==DESKTOP_H){ r[0]=0; r[1]=0; r[2]=(i32)gfx_w; r[3]=(i32)gfx_h-60; return 1; } struct wnd *w=get(h); if(!w) return 0; int cw,ch; client_size(w,&cw,&ch); r[0]=0; r[1]=0; r[2]=cw; r[3]=ch; return 1; }
static i32 WINAPI w_GetWindowRect(u64 h, i32 *r){ if(h==DESKTOP_H){ r[0]=0; r[1]=0; r[2]=(i32)gfx_w; r[3]=(i32)gfx_h-60; return 1; } struct wnd *w=get(h); if(!w) return 0; int ox,oy; outer_origin(w,&ox,&oy); r[0]=ox; r[1]=oy; r[2]=ox+w->w; r[3]=oy+w->h; return 1; }
static u64 WINAPI w_GetParent(u64 h){ struct wnd *w=get(h); return w? w->parent : 0; }
static i32 WINAPI w_IsWindow(u64 h){ return get(h)!=0; }
static i32 WINAPI w_SetWindowTextA(u64 h, const char *s){ return get(h)? (i32)send(h,WM_SETTEXT,0,(i64)s) : 0; }
static i32 WINAPI w_GetWindowTextA(u64 h, char *b, i32 n){ return get(h)? (i32)send(h,WM_GETTEXT,n,(i64)b) : 0; }
static i32 WINAPI w_GetWindowTextLengthA(u64 h){ return get(h)? (i32)send(h,WM_GETTEXTLENGTH,0,0) : 0; }
static i64 WINAPI w_GetWindowLongPtrA(u64 h, i32 i){
  struct wnd *w=get(h); if(!w) return 0;
  switch(i){ case -4: return w->proc; case -21: return w->userdata; case -16: return w->style; case -20: return w->exstyle; case -12: return w->id;
             case -6: return procs[w->owner].imagebase; case -8: return w->parent; }                  /* GWLP_HINSTANCE, GWLP_HWNDPARENT */
  if(i>=0 && i+8<=64) return *(i64*)((u8*)w->extra+i);                                             /* cbWndExtra bytes (64 at most) */
  if(i>=0 && i+4<=64) return *(i32*)((u8*)w->extra+i);
  win32_set_last_error(1413); return 0;                                                            /* ERROR_INVALID_INDEX */
}
static i64 WINAPI w_SetWindowLongPtrA(u64 h, i32 i, i64 v){
  struct wnd *w=get(h); if(!w) return 0; i64 old=w_GetWindowLongPtrA(h,i);
  switch(i){ case -4: w->proc=v; break; case -21: w->userdata=v; break; case -16: w->style=(u32)v; break; case -20: w->exstyle=(u32)v; break; case -12: w->id=v; break;
             case -8: w->parent=(u64)v; break;
             default: if(i>=0 && i+8<=64) *(i64*)((u8*)w->extra+i)=v; else if(i>=0 && i+4<=64) *(i32*)((u8*)w->extra+i)=(i32)v; }
  dirty=1; return old;
}
static i32 WINAPI w_GetWindowLongA(u64 h, i32 i){                                  /* the 32-bit versions read 4 bytes of the extra area */
  struct wnd *w=get(h); if(!w) return 0; if(i<0) return (i32)w_GetWindowLongPtrA(h,i);
  return (i+4<=64)? *(i32*)((u8*)w->extra+i) : 0;
}
static i32 WINAPI w_SetWindowLongA(u64 h, i32 i, i32 v){
  struct wnd *w=get(h); if(!w) return 0; if(i<0) return (i32)w_SetWindowLongPtrA(h,i,v);
  if(i+4>64) return 0; i32 old=*(i32*)((u8*)w->extra+i); *(i32*)((u8*)w->extra+i)=v; return old;
}
static u64 WINAPI w_GetAncestor(u64 h, u32 flags){                                  /* GA_PARENT 1, GA_ROOT 2, GA_ROOTOWNER 3 */
  struct wnd *w=get(h); if(!w) return 0;
  if(flags==1) return w->parent? w->parent : DESKTOP_H;
  while(w->parent && get(w->parent)){ h=w->parent; w=get(h); }
  return h;
}
static i64 WINAPI w_CallWindowProcA(u64 proc, u64 h, u32 m, u64 wp, i64 lp){ return call_proc(proc,h,m,wp,lp); }
static i64 WINAPI w_SendMessageA(u64 h, u32 m, u64 wp, i64 lp){ return send(h,m,wp,lp); }
static i32 WINAPI w_PostMessageA(u64 h, u32 m, u64 wp, i64 lp){ post(h,m,wp,lp); return 1; }
static void WINAPI w_PostQuitMessage(i32 c){ cur->quit=1; cur->quit_code=c; }
static u64 WINAPI w_LoadCursorA(u64 i, const char *n){ return (!i && (u64)n<0x10000)? (u64)n : 1; }   /* IDC_ARROW 32512, IDC_HAND 32649 ... */
static u64 WINAPI w_SetCursor(u64 c){ gfx_cursor_shape(c==32649? 1 : 0); return 1; }
static u64 WINAPI w_SetFocus(u64 h){ u64 o=g_focus; if(get(h)) set_focus(h); return o; }
static u64 WINAPI w_GetFocus(void){ return g_focus; }
static i32 WINAPI w_InvalidateRect(u64 h, void *r, i32 e){ (void)r;(void)e; struct wnd *w=get(h); if(w) invalidate(w); else dirty=1; return 1; }
static i32 WINAPI w_UpdateWindow(u64 h){                          /* paint now if a repaint is pending */
  struct wnd *w=get(h); if(w && w->kind==K_APP && w->paint_pending && w->owner==cur->pid){ w->paint_pending=0; send(h,WM_PAINT,0,0); wm_service(); }
  return 1;
}

/* ---- painting ---- */
/* ---- theme ---- */
#define ACCENT      0x3B82F6
#define TXT_DARK    0x111827
#define TXT_MUTED   0x6B7280
#define CHROME_BG   0xF5F6F8
#define TB_BG       0x1F2430
#define TB_HOVER    0x2C3444
#define TB_ACTIVE   0x2A3142
#define PANEL_BG    0x1F2430
#define PANEL_LINE  0x394152
#define PANEL_HOVER 0x323B4F
static const struct font *ui(int style,int px){ return font_find(style,px); }
static int fit(const struct font *f,const char *s,int n,int maxw){ int k=0; while(k<n && s[k] && ui_width(f,s,k+1)<=maxw) k++; return k; }
static void strcpy_k(char *d,const char *s){ while((*d++=*s++)); }
static int strncmp_ci_(const char *a,const char *b,int n){ for(;n;n--,a++,b++){ int x=*a,y=*b; if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32; if(x!=y) return x-y; if(!x) return 0; } return 0; }
#define strcpy strcpy_k
#include "dialog.inc"
#undef strcpy
static void frame3d(int x,int y,int w,int h,int sunken){
  u32 hi=sunken?0x808080:0xFFFFFF, lo=sunken?0xFFFFFF:0x808080;
  gfx_rect(x,y,w,1,hi); gfx_rect(x,y,1,h,hi); gfx_rect(x,y+h-1,w,1,lo); gfx_rect(x+w-1,y,1,h,lo);
}
static void outline(int x,int y,int w,int h,u32 c){ gfx_rect(x,y,w,1,c); gfx_rect(x,y+h-1,w,1,c); gfx_rect(x,y,1,h,c); gfx_rect(x+w-1,y,1,h,c); }
static void shadow(int x,int y,int w,int h){                           /* soft drop shadow: concentric translucent rings */
  for(int d=1; d<=8; d++){
    int a=(9-d)*3, ox=x-d, oy=y-d+3, ow=w+2*d, oh=h+2*d;
    gfx_rect_alpha(ox,oy,ow,1,0,a); gfx_rect_alpha(ox,oy+oh-1,ow,1,0,a);
    gfx_rect_alpha(ox,oy+1,1,oh-2,0,a); gfx_rect_alpha(ox+ow-1,oy+1,1,oh-2,0,a);
  }
}
static u32 name_color(const char *n){
  static const u32 pal[8]={0x3B82F6,0x10B981,0xF59E0B,0xEF4444,0x8B5CF6,0x06B6D4,0xEC4899,0x84CC16};
  u32 h=5381; for(;*n;n++) h=h*33+(u8)*n; return pal[h&7];
}
/* "C:\P\putty.exe" -> "putty": the bare lower-case program name used to pick an icon and a display name */
static void app_key(const char *n, char *out, int max){
  const char *s=n; for(const char *p=n;*p;p++) if(*p=='/'||*p=='\\') s=p+1;
  int k=0; for(;s[k] && k<max-1;k++) out[k]=(s[k]>='A'&&s[k]<='Z')? s[k]+32 : s[k]; out[k]=0;
  if(k>4 && !strcmp(out+k-4,".exe")) out[k-4]=0;
}
/* name shown in the Start menu: "putty" -> "PuTTY", "notes" -> "Notes" */
static void app_display(const char *n, char *out, int max){
  static const char *const known[][2]={{"putty","PuTTY"},{"fastfetch","Fastfetch"},{"tmlwn","TMLWN"},{"librewolf","LibreWolf"},{"firefox","Firefox"},{"jstest","JS test"},{"apitest","API test"},{"apitest2","API test 2"},{"apitest3","API test 3"},{"dlgtest","Dialog test"},{"dlltest","DLL test"},{"nettest","Network test"},{"httpd","Web server"}};
  char k[32]; app_key(n,k,32);
  for(unsigned i=0;i<sizeof known/sizeof *known;i++) if(!strcmp(k,known[i][0])){ int j=0; for(;known[i][1][j]&&j<max-1;j++) out[j]=known[i][1][j]; out[j]=0; return; }
  int j=0; for(;k[j]&&j<max-1;j++) out[j]=k[j]; out[j]=0; if(out[0]>='a'&&out[0]<='z') out[0]-=32;
}
#define L16(x0,y0,x1,y1,t,c) gfx_line_aa(x+(x0)*sz/64,y+(y0)*sz/64,x+(x1)*sz/64,y+(y1)*sz/64,(t)*sz/24,c)
static void app_icon(int x,int y,int sz,const char *name,int selected){
  char k[32]; app_key(name,k,32); (void)selected;
  #define S(v) ((v)*sz/64)
  if(!strcmp(k,"terminal")||!strcmp(k,"console")||!strcmp(k,"cmd")||!strcmp(k,"powershell")){
    gfx_round_rect(x,y,sz,sz,sz/4,0x3A4256); gfx_round_rect(x+1,y+1,sz-2,sz-2,sz/4,0x161B26);
    L16(16,22,28,32,40,0x4ADE80); L16(28,32,16,42,40,0x4ADE80); L16(32,43,48,43,40,0xE5E7EB); return; }
  if(!strcmp(k,"files")||!strcmp(k,"explorer")){
    gfx_round_rect(x+S(4),y+S(10),S(26),S(14),S(5),0xDB9628); gfx_round_rect(x+S(4),y+S(14),S(56),S(42),S(6),0xE3A233);
    gfx_round_rect(x+S(4),y+S(22),S(56),S(34),S(6),0xFFC94D); gfx_rect_alpha(x+S(9),y+S(23),S(46),1,0xFFFFFF,120); return; }
  if(!strcmp(k,"browser")){
    gfx_round_rect(x,y,sz,sz,sz/4,0x0EA5E9); int c=sz/2, r=S(21);
    gfx_round_rect(x+c-r,y+c-r,2*r,2*r,r,0xFFFFFF); gfx_round_rect(x+c-r+S(4),y+c-r+S(4),2*r-S(8),2*r-S(8),r-S(4),0x0EA5E9);
    gfx_round_rect(x+c-S(9),y+c-r,S(18),2*r,S(9),0xFFFFFF); gfx_round_rect(x+c-S(5),y+c-r+S(4),S(10),2*r-S(8),S(5),0x0EA5E9);
    gfx_rect(x+c-r+S(2),y+c-S(2),2*r-S(4),S(4)>0? S(4) : 1,0xFFFFFF); gfx_rect(x+c-S(2),y+c-r+S(2),S(4)>0? S(4) : 1,2*r-S(4),0xFFFFFF); return; }
  if(!strcmp(k,"notes")||!strcmp(k,"notepad")){
    gfx_round_rect(x+S(8),y+S(4),S(48),S(56),S(8),0xFBBF24); gfx_round_rect(x+S(8),y+S(14),S(48),S(46),S(8),0xFFF8E1); gfx_rect(x+S(8),y+S(14),S(48),S(8),0xFFF8E1);
    for(int i=0;i<3;i++) gfx_round_rect(x+S(16),y+S(26+i*9),S(i==2? 20 : 32),S(3)>1? S(3) : 2,1,0xD6A419); return; }
  if(!strcmp(k,"settings")||!strcmp(k,"control")){
    gfx_round_rect(x,y,sz,sz,sz/4,0x64748B); int c=sz/2;
    static const int dx[8]={0,14,20,14,0,-14,-20,-14}, dy[8]={-20,-14,0,14,20,14,0,-14};
    for(int i=0;i<8;i++) gfx_line_aa(x+c,y+c,x+c+dx[i]*sz/64,y+c+dy[i]*sz/64,sz*9/24,0xF1F5F9);
    gfx_round_rect(x+c-S(16),y+c-S(16),S(32),S(32),S(16),0xF1F5F9); gfx_round_rect(x+c-S(7),y+c-S(7),S(14),S(14),S(7),0x64748B); return; }
  if(!strcmp(k,"snake")){
    gfx_round_rect(x,y,sz,sz,sz/4,0x16A34A); L16(16,46,32,46,72,0xBBF7D0); L16(32,46,32,22,72,0xBBF7D0); L16(32,22,48,22,72,0xBBF7D0);
    gfx_round_rect(x+S(45),y+S(18),S(5),S(5),S(3),0x14532D); gfx_round_rect(x+S(44),y+S(42),S(8),S(8),S(4),0xF87171); return; }
  if(!strcmp(k,"paint")){
    gfx_round_rect(x,y,sz,sz,sz/4,0xF97316); int c=sz/2; gfx_round_rect(x+c-S(20),y+c-S(18),S(40),S(36),S(18),0xFFF7ED);
    static const u32 dots[4]={0xEF4444,0x3B82F6,0x22C55E,0xEAB308}; for(int i=0;i<4;i++) gfx_round_rect(x+S(18+i*8),y+S(i&1? 30 : 22),S(7),S(7),S(4),dots[i]); return; }
  if(!strcmp(k,"putty")){
    gfx_round_rect(x,y,sz,sz,sz/4,0x334155); gfx_round_rect(x+S(10),y+S(12),S(30),S(22),S(4),0xCBD5E1); gfx_round_rect(x+S(13),y+S(15),S(24),S(15),S(2),0x0F172A);
    gfx_round_rect(x+S(24),y+S(30),S(30),S(22),S(4),0xE2E8F0); gfx_round_rect(x+S(27),y+S(33),S(24),S(15),S(2),0x0F172A); L16(30,37,36,41,24,0x4ADE80); return; }
  if(!strcmp(k,"fastfetch")){
    gfx_round_rect(x,y,sz,sz,sz/4,0x7C3AED); for(int i=0;i<4;i++){ gfx_round_rect(x+S(14),y+S(16+i*9),S(6),S(5),S(2),0xC4B5FD); gfx_round_rect(x+S(24),y+S(16+i*9),S(i&1? 18 : 26),S(5),S(2),0xFFFFFF); } return; }
  if(!strcmp(k,"getlibrewolf")){                                     /* download arrow: "Get LibreWolf" */
    gfx_round_rect(x,y,sz,sz,sz/2,0x16A34A); L16(32,16,32,44,48,0xFFFFFF); L16(20,32,32,44,48,0xFFFFFF); L16(44,32,32,44,48,0xFFFFFF); L16(18,50,46,50,40,0xBBF7D0); return; }
  if(!strcmp(k,"librewolf")){
    gfx_round_rect(x,y,sz,sz,sz/2,0x1D74D8); gfx_round_rect(x+S(6),y+S(6),sz-S(12),sz-S(12),(sz-S(12))/2,0x2E8FF0);
    const struct font *f=ui(FS_BOLD,sz*9/16); ui_text(f,x+(sz-ui_width(f,"L",1))/2,y+(sz-ui_lineh(f))/2,"L",1,0xFFFFFF); return; }
  #undef S
  gfx_round_rect(x,y,sz,sz,sz/4,name_color(k));
  char l[2]={*k>='a'&&*k<='z'? *k-32 : *k? *k : '?',0}; const struct font *f=ui(FS_BOLD,sz*5/9);
  ui_text(f,x+(sz-ui_width(f,l,1))/2,y+(sz-ui_lineh(f))/2,l,1,0xFFFFFF);
}
#undef L16
/* the program that owns a window, for its taskbar icon */
static const char *owner_name(struct wnd *w){
  for(int i=1;i<MAXPROC;i++) if(procs[i].used && procs[i].pid==(int)w->owner) return procs[i].exe[0]? procs[i].exe : procs[i].name;
  return w->text? w->text : "";
}

static int hover_part, hover_tb=-1; static u64 hover_win;

static void draw_edit(struct wnd *w, int ax, int ay){
  gfx_rect(ax,ay,w->w,w->h,0xFFFFFF); outline(ax,ay,w->w,w->h,0xA3A9B3);
  mono_init(); int rows=(w->h-4)/cell_h, cols=(w->w-4)/cell_w; if(rows<1||cols<1||!w->text) return;
  int cline=0, ccol=0;                                                       /* caret line/col */
  for(u32 p=0;p<w->caret;p++){ char c=w->text[p]; if(c=='\n'){ cline++; ccol=0; } else if(c!='\r') ccol+=ch_cols(c); }
  if((u32)cline<w->top_line) w->top_line=cline; else if(cline>=(int)w->top_line+rows) w->top_line=cline-rows+1;
  if((u32)ccol<w->left_col) w->left_col=ccol; else if(ccol>=(int)w->left_col+cols) w->left_col=ccol-cols+1;
  u32 p=0; for(u32 l=0;l<w->top_line;l++){ while(p<w->tlen && w->text[p]!='\n') p++; if(p<w->tlen) p++; }
  for(int r=0;r<rows && p<=w->tlen;r++){
    int col=0;
    for(;p<w->tlen && w->text[p]!='\n';p++){
      char c=w->text[p]; if(c=='\r') continue;
      int sc=col-(int)w->left_col;
      if(c!='\t' && sc>=0 && sc<cols) cell_char(ax+2+sc*cell_w,ay+2+r*cell_h,c,0x111827);
      col+=ch_cols(c);
    }
    if(p<w->tlen) p++; else break;
  }
  if(g_focus==hof(w) && ((ticks/500)&1)==0){
    int sx=ccol-(int)w->left_col, sy=cline-(int)w->top_line;
    if(sx>=0&&sx<=cols&&sy>=0&&sy<rows) gfx_rect(ax+2+sx*cell_w,ay+2+sy*cell_h,2,cell_h,ACCENT);
  }
}
static void draw_children(u64 parent, int cx, int cy);
/* The title bar follows the window's content: a dark window (terminal, console) gets a dark bar with light text. */
static u32 title_color(struct wnd *w, int *dark){
  u32 c=0xFFFFFF; *dark=0;
  if(w->kind==K_CONSOLE) c=0x0B0E14;
  else if((w->kind==K_APP||w->kind==K_SURFACE) && w->pix && w->stride>8 && w->ph>8){ c=w->pix[(u64)4*w->stride+4]&0xFFFFFF; }
  else return 0xFFFFFF;
  u32 r=(c>>16)&255, g=(c>>8)&255, b=c&255;
  if((r*299+g*587+b*114)/1000<110){ *dark=1; return c; }
  return 0xFFFFFF;
}
/* Willows window buttons: a cluster of small coloured dots at the right end of the title bar (hover shows the glyph).
   From the right: close (coral), maximize (teal, apps only), minimize (amber). Each dot has a 24px wide hit area. */
#define DOT_PITCH 24
#define DOT_EDGE 18
static int ncap(struct wnd *w){ return w->kind==K_APP? 3 : w->kind==K_DIALOG? 1 : 2; }
static int dots_width(struct wnd *w){ return DOT_PITCH*ncap(w)+DOT_EDGE-DOT_PITCH/2+8; }
static void caption_buttons(struct wnd *w, int ox, int oy){
  int n=ncap(w); int cy=oy+FRAME+TITLE_H/2; int active=(hof(w)==top_visible());
  for(int k=0;k<n;k++){
    int cx=ox+w->w-FRAME-DOT_EDGE-DOT_PITCH*k;
    int part= k==0? 2 : (w->kind==K_APP? (k==1? 3 : 4) : 4);
    int hov=(hover_win==hof(w) && hover_part==part);
    int dk; title_color(w,&dk);
    u32 col= !active && !hov? (dk? 0x4B5563 : 0xC9CED6) : part==2? 0xF2706A : part==3? 0x3CC9A5 : 0xF5B84B;
    gfx_round_rect(cx-7,cy-7,14,14,7,col);
    if(hov){
      u32 fg=0x3A2A1A;
      if(part==2){ gfx_line_aa(cx-3,cy-3,cx+3,cy+3,22,fg); gfx_line_aa(cx+3,cy-3,cx-3,cy+3,22,fg); }
      else if(part==4){ gfx_rect(cx-3,cy,7,2,fg); }
      else if(!w->maximized){ outline(cx-3,cy-3,7,7,fg); }
      else { outline(cx-3,cy-2,6,6,fg); outline(cx-1,cy-4,6,6,fg); }
    }
  }
}
static void draw_wnd_inner(struct wnd *w, int ox, int oy){
  if(!w->visible) return;
  if(w->kind==K_CTRL){ ctrl_draw(w,ox,oy); return; }
  if(w->kind==K_EDIT){ draw_edit(w,ox,oy); return; }
  if(w->kind==K_BUTTON){
    u32 bg=w->pressed? 0xD1D5DB : 0xE5E7EB; gfx_round_rect(ox,oy,w->w,w->h,3,0x9CA3AF); gfx_round_rect(ox+1,oy+1,w->w-2,w->h-2,3,bg);
    const struct font *f=ui(FS_REGULAR,13); int n=w->tlen; if(w->text) ui_text(f,ox+(w->w-ui_width(f,w->text,n))/2,oy+(w->h-ui_lineh(f))/2+(w->pressed?1:0),w->text,n,TXT_DARK);
    return;
  }
  if(w->kind==K_STATIC){
    gfx_rect(ox,oy,w->w,w->h,CHROME_BG); const struct font *f=ui(FS_REGULAR,13);
    if(w->text) ui_text(f,ox+2,oy+(w->h-ui_lineh(f))/2,w->text,fit(f,w->text,w->tlen,w->w-4),TXT_DARK); return;
  }
  int cx, cy, cw, ch;
  if(has_frame(w)){
    int active=(hof(w)==top_visible());
    gfx_rect(ox,oy,w->w,w->h,active? ACCENT : 0x9CA3AF);                    /* 1px border */
    int dk; u32 tbg=title_color(w,&dk);
    gfx_rect(ox+1,oy+1,w->w-2,TITLE_H,dk? tbg : (active? 0xFFFFFF : 0xF3F4F6));
    gfx_rect(ox+1,oy+1+TITLE_H,w->w-2,w->h-2-TITLE_H,CHROME_BG);
    const struct font *tf=ui(FS_REGULAR,13);
    if(w->text){ int avail=w->w-2-dots_width(w)-20, k=fit(tf,w->text,w->tlen,avail); ui_text(tf,ox+14,oy+1+(TITLE_H-ui_lineh(tf))/2,w->text,k,dk? (active? 0xE5E7EB : 0x9CA3AF) : (active? TXT_DARK : TXT_MUTED)); }
    caption_buttons(w,ox,oy);
  }
  client_origin(w,&cx,&cy); client_size(w,&cw,&ch);
  if(w->kind==K_SURFACE){ gfx_blit_stride(cx,cy,w->pw,w->ph,w->pix,w->stride); return; }
  if(w->kind==K_CONSOLE){
    gfx_rect(cx,cy,cw,ch,0x0C0F14); mono_init();
    for(int r=0;r<CON_ROWS;r++) for(int c=0;c<CON_COLS;c++) if(con_buf[r][c]>' ') cell_char(cx+2+c*cell_w,cy+2+r*cell_h,con_buf[r][c],0xD0D4DC);
    if(g_focus==hof(w) && ((ticks/500)&1)==0) gfx_rect(cx+2+con_cx*cell_w,cy+2+con_cy*cell_h+cell_h-3,cell_w,3,0xD0D4DC);
    return;
  }
  if(w->kind==K_APP && w->pix && w->alpha){ gfx_blit_alpha(cx,cy,w->pw<cw? w->pw : cw,w->ph<ch? w->ph : ch,w->pix,w->stride); draw_children(hof(w),cx,cy); return; }
  gfx_rect(cx,cy,cw,ch,((w->style&WS_CHILD)||w->kind==K_DIALOG)? CHROME_BG : 0xFFFFFF);
  if(w->kind==K_APP && w->pix) gfx_blit_stride(cx,cy,w->pw<cw? w->pw : cw,w->ph<ch? w->ph : ch,w->pix,w->stride);
  draw_children(hof(w),cx,cy);
}
static void draw_wnd(struct wnd *w, int ox, int oy){
  if(!w->visible) return;
  if(!has_frame(w) || w->kind==K_EDIT || w->kind==K_BUTTON || w->kind==K_STATIC || w->kind==K_CTRL){ draw_wnd_inner(w,ox,oy); return; }
  shadow(ox,oy,w->w,w->h);
  int R= w->maximized? 0 : 8; u32 sv[4][64];
  int cxs[4]={ox, ox+w->w-R, ox, ox+w->w-R}, cys[4]={oy, oy, oy+w->h-R, oy+w->h-R};
  if(R) for(int k=0;k<4;k++) for(int j=0;j<R;j++) for(int i=0;i<R;i++) sv[k][j*8+i]=gfx_raw_get(cxs[k]+i,cys[k]+j);
  draw_wnd_inner(w,ox,oy);
  if(R) for(int k=0;k<4;k++) for(int j=0;j<R;j++) for(int i=0;i<R;i++){        /* smooth the corners: blend the saved background back in */
    int ci=(k&1)? R-1-i : i, cj=(k&2)? R-1-j : j, cov=gfx_corner_cov(R,ci,cj); if(cov==255) continue;
    int px=cxs[k]+i, py=cys[k]+j; gfx_raw_put(px,py,gfx_blend(sv[k][j*8+i],gfx_raw_get(px,py),cov));
  }
}
static void draw_children(u64 parent, int cx, int cy){
  for(int i=0;i<MAXW;i++) if(W[i].used && W[i].parent==parent){ struct wnd *c=&W[i]; draw_wnd(c,cx+c->x,cy+c->y); }
}

/* ---- taskbar + Start menu ---- */
#define TB_H 60
#define MW 300
#define MPAD 10
#define MHEAD 30
static int tb_y(void){ return (int)gfx_h-TB_H; }
/* Start menu: real programs on the main panel; demos/tests live behind "Demos & tests" in a side panel.
   Item codes: 0.. main program, 100 demos toggle, 101 restart, 102 shut down, 200.. demo program. */
static int main_idx[40], nmain, demo_idx[40], ndemo, demo_open;
/* Installed programs: C:\System\shortcuts.cfg, one "Name|C:\path\program.exe" per line (written by installers such as the terminal's
   getlibrewolf). In main_idx they are 1000 + their index; SC_GET_LW is the "Get LibreWolf" entry shown while none is installed. */
#define MAXSC 16
#define SC_BASE 1000
#define SC_GET_LW 1999
static struct { char name[32], path[160]; } sc[MAXSC]; static int nsc;
static void shortcuts_load(void){
  nsc=0; struct ramfile *f=fs_find("/System/shortcuts.cfg"); if(!f || f->isdir || !f->data) return;
  const char *p=(const char *)f->data, *end=p+f->size;
  while(p<end && nsc<MAXSC){
    const char *e=p; while(e<end && *e!='\n') e++;
    const char *bar=p; while(bar<e && *bar!='|') bar++;
    if(bar<e && bar>p){ int n=0; for(const char *q=p;q<bar && n<31;q++) sc[nsc].name[n++]=*q; sc[nsc].name[n]=0;
      n=0; for(const char *q=bar+1;q<e && *q!='\r' && n<159;q++) sc[nsc].path[n++]=*q; sc[nsc].path[n]=0; if(sc[nsc].path[0]) nsc++; }
    p=e+1;
  }
}
static int has_librewolf(void){ for(int i=0;i<nsc;i++){ int l=strlen(sc[i].path); if(l>=13 && !stricmp(sc[i].path+l-13,"librewolf.exe")) return 1; } return 0; }
static void menu_build(void){
  nmain=ndemo=0; shortcuts_load();
  for(int i=0;i<app_count();i++){ if(app_is_demo(i)){ if(ndemo<40) demo_idx[ndemo++]=i; } else if(nmain<40) main_idx[nmain++]=i; }
  for(int i=0;i<nsc && nmain<40;i++) main_idx[nmain++]=SC_BASE+i;
  if(!has_librewolf() && nmain<40) main_idx[nmain++]=SC_GET_LW;
}
/* a Start menu entry: the key that picks its icon, and the name shown */
static const char *item_key(int idx){ return idx==SC_GET_LW? "getlibrewolf" : idx>=SC_BASE? sc[idx-SC_BASE].path : app_name(idx); }
static void app_display(const char *n, char *out, int max);
static void item_display(int idx, char *out, int max){
  if(idx==SC_GET_LW){ cpy(out,"Get LibreWolf",max); return; }
  if(idx>=SC_BASE){ cpy(out,sc[idx-SC_BASE].name,max); return; }
  app_display(app_name(idx),out,max);
}
static void shortcut_launch(int i){
  char cmd[200], dir[160]; int l=0; cmd[l++]='"'; for(const char *q=sc[i].path;*q && l<196;q++) cmd[l++]=*q; cmd[l++]='"'; cmd[l]=0;
  cpy(dir,sc[i].path,160); int n=strlen(dir); while(n>0 && dir[n-1]!='\\' && dir[n-1]!='/') n--; if(n>0) n--; dir[n]=0;
  int (WINAPI *cp)(const char*,char*,void*,void*,int,u32,void*,const char*,void*,u8*)=win32_resolve("kernel32","CreateProcessA");
  u8 si[104]; memset(si,0,sizeof si); *(u32 *)si=104; u8 pi[24];
  if(!cp || !cp(0,cmd,0,0,0,0,0,dir[0]? dir : 0,si,pi)) kprintf("\nStart menu: cannot start %s\n",sc[i].path);
}
static int menu_rows(void){ int r=nmain+(ndemo? 1 : 0); return r? r : 1; }
static int ih(void){                                              /* item height: shrink so the menu always fits on screen */
  int fixed=2*MPAD+MHEAD+16, avail=(int)gfx_h-TB_H-16-fixed, h=avail/(menu_rows()+2); return h>38? 38 : h<22? 22 : h;
}
static int menu_h(void){ return MPAD+MHEAD+menu_rows()*ih()+16+2*ih()+MPAD; }
static int menu_x(void){ return 8; }
static int menu_dy; static u32 start_t0;
static int menu_y(void){ return tb_y()-menu_h()-8+menu_dy; }
#define FW 250                                                    /* side panel width */
static int fly_ih(void){ int h=((int)gfx_h-TB_H-24-2*MPAD)/(ndemo? ndemo : 1); return h>34? 34 : h<22? 22 : h; }
static int fly_h(void){ return 2*MPAD+ndemo*fly_ih(); }
static int fly_x(void){ return menu_x()+MW+6; }
static int fly_y(void){ int y=menu_y()+MPAD+MHEAD+nmain*ih(); int lim=tb_y()-8-fly_h(); return y<lim? y : lim; }
static int in_start_btn(int mx,int my){ return mx>=12&&mx<68&&my>=tb_y(); }
static int start_item_at(int mx,int my){
  int x0=menu_x(), y0=menu_y(), rows=menu_rows();
  if(demo_open && ndemo && mx>=fly_x() && mx<fly_x()+FW && my>=fly_y() && my<fly_y()+fly_h()){
    int j=(my-fly_y()-MPAD)/fly_ih(); return (my>=fly_y()+MPAD && j>=0 && j<ndemo)? 200+j : -1;
  }
  if(mx<x0||mx>=x0+MW||my<y0||my>=y0+menu_h()) return -1;
  int y=my-y0-MPAD-MHEAD;
  if(y>=0 && y<rows*ih()){ int r=y/ih(); if(r<nmain) return r; return ndemo? 100 : -1; }
  y-=rows*ih()+16; if(y>=0 && y<2*ih()) return 101+y/ih();
  return -1;
}
static int tb_windows(u64 *out){                                  /* top-level visible windows in creation order */
  int n=0; for(int i=0;i<MAXW;i++) if(W[i].used && W[i].visible && !(W[i].style&WS_CHILD) && n<16){
    int appwin=(W[i].exstyle&0x40000)!=0;                                    /* as on Windows: tool windows and owned popups (menus) get no button */
    if(!appwin && ((W[i].exstyle&0x80) || get(W[i].ownerw))) continue;
    out[n++]=HW(i); }
  return n;
}
#define DK_X 12                                                    /* the dock floats 12px in from the screen edges */
static int tb_tile_w(int n){ int avail=(int)gfx_w-DK_X-60-190-(net_up()? 150 : 0)-12; int bw=avail/(n? n : 1); return bw>190? 190 : bw; }
static int tb_button_at(int mx,int my){
  if(my<tb_y()) return -1;
  u64 ws[16]; int n=tb_windows(ws); if(!n) return -1;
  int bw=tb_tile_w(n), x=DK_X+60; for(int i=0;i<n;i++,x+=bw) if(mx>=x&&mx<x+bw-4) return i;
  return -1;
}
static int rtc(int reg){ outb(0x70,reg); return inb(0x71); }
static void clock_text(char *out){
  int tries=0; while((rtc(0x0A)&0x80) && tries++<1000);
  int h=rtc(4), m=rtc(2), b=rtc(0x0B);
  if(!(b&4)){ h=(h&0x0F)+((h>>4)&0x0F)*10; m=(m&0x0F)+(m>>4)*10; }
  out[0]='0'+h/10; out[1]='0'+h%10; out[2]=':'; out[3]='0'+m/10; out[4]='0'+m%10; out[5]=0;
}
/* The Willows logo: a weeping willow - a trunk and drooping fronds drawn as smooth curves. */
static void willow_logo(int x,int y,int s,u32 c1,u32 c2){
  int cx=x+s/2, crown=y+s*26/100, base=y+s-1;
  gfx_line_aa(cx,base,cx,crown+s/8,26,0xC4B49A);                               /* trunk */
  for(int k=-3;k<=3;k++){
    int ak=k<0? -k : k;
    int ex=cx+k*s*17/100, ey=y+s*(98-ak*7)/100, px=cx+k*s*30/100, py=crown-s*22/100;      /* arch up and out, then droop */
    int r1=(int)((c1>>16)&255), g1=(int)((c1>>8)&255), b1=(int)(c1&255), r2=(int)((c2>>16)&255), g2=(int)((c2>>8)&255), b2=(int)(c2&255);
    u32 col=((u32)(r1+(r2-r1)*ak/3)<<16)|((u32)(g1+(g2-g1)*ak/3)<<8)|(u32)(b1+(b2-b1)*ak/3); int lx=cx, ly=crown;
    for(int t=1;t<=12;t++){ int nx=((12-t)*(12-t)*cx+2*t*(12-t)*px+t*t*ex)/144, ny=((12-t)*(12-t)*crown+2*t*(12-t)*py+t*t*ey)/144; gfx_line_aa(lx,ly,nx,ny,24,col); lx=nx; ly=ny; }
  }
  gfx_round_rect(cx-s/9,crown-s/9,2*(s/9),2*(s/9),s/9,c1);                       /* crown */
}
static void draw_taskbar(void){
  int by=tb_y()+8, H=44, bx=DK_X, bw_=(int)gfx_w-2*DK_X;                         /* the dock: a floating rounded bar */
  shadow(bx,by,bw_,H);
  gfx_round_rect(bx,by,bw_,H,16,0x394152); gfx_round_rect(bx+1,by+1,bw_-2,H-2,15,TB_BG);
  gfx_rect_alpha(bx+16,by+1,bw_-32,1,0xFFFFFF,40);
  if(start_open) gfx_round_rect(bx+4,by+4,48,H-8,12,TB_ACTIVE); else if(hover_tb==-2) gfx_round_rect(bx+4,by+4,48,H-8,12,TB_HOVER);
  willow_logo(bx+13,by+9,27,0x5EEAD4,0x4ADE80);
  u64 ws[16]; int n=tb_windows(ws);
  const struct font *tf=ui(FS_REGULAR,13);
  if(n){
    int bw=tb_tile_w(n), x=bx+60;
    for(int i=0;i<n;i++,x+=bw){
      struct wnd *w=get(ws[i]); int act=(ws[i]==top_visible()), hov=(hover_tb==i);
      if(act) gfx_round_rect(x,by+4,bw-4,H-8,10,TB_ACTIVE); else if(hov) gfx_round_rect(x,by+4,bw-4,H-8,10,TB_HOVER);
      if(act) gfx_round_rect(x+bw/2-14,by+H-6,28,3,1,ACCENT); else if(!w->minimized) gfx_round_rect(x+bw/2-4,by+H-6,8,3,1,0x6B7280);
      char dn[48]; const char *on=w->kind==K_CONSOLE? "Console" : owner_name(w); app_display(on,dn,sizeof dn);
      const char *nm= w->text&&w->text[0]? w->text : dn;
      app_icon(x+9,by+(H-22)/2,22,on,0);
      int k=fit(tf,nm,strlen(nm),bw-4-46); ui_text(tf,x+38,by+(H-ui_lineh(tf))/2,nm,k,w->minimized? 0x9CA3AF : 0xF3F4F6);
    }
  }
  /* tray: network, then clock + date */
  char clk[8]; clock_text(clk); int y2,mo,d,h,mi,s,wd; unix_to_civil(time_now(),&y2,&mo,&d,&h,&mi,&s,&wd);
  static const char *const mon[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  char date[24]; int dn=0; if(d>=10) date[dn++]='0'+d/10; date[dn++]='0'+d%10; date[dn++]=' '; for(int i=0;i<3;i++) date[dn++]=mon[mo-1][i]; date[dn++]=' ';
  date[dn++]='0'+(y2/1000)%10; date[dn++]='0'+(y2/100)%10; date[dn++]='0'+(y2/10)%10; date[dn++]='0'+y2%10; date[dn]=0;
  const struct font *cf=ui(FS_REGULAR,14), *df=ui(FS_REGULAR,11);
  int tx=(int)gfx_w-DK_X-96; ui_text(cf,tx+(80-ui_width(cf,clk,5))/2,by+5,clk,5,0xF3F4F6); ui_text(df,tx+(80-ui_width(df,date,dn))/2,by+5+ui_lineh(cf)+1,date,dn,0x9CA3AF);
  if(net_up()){ char ip[20]; net_ip_str(net_ip(),ip); int l=strlen(ip); const struct font *nf=ui(FS_REGULAR,12); int nw=ui_width(nf,ip,l), nx=tx-nw-34;
    gfx_round_rect(nx+6,by+H/2-4,8,8,4,0x22C55E); ui_text(nf,nx+22,by+(H-ui_lineh(nf))/2,ip,l,0xD1D5DB); }
}
static void power_icon(int x,int y,int kind){                     /* 0 restart, 1 shut down */
  gfx_round_rect(x,y,22,22,11,kind? 0xEF4444 : 0xF59E0B);
  gfx_round_rect(x+4,y+4,14,14,7,0xFFFFFF); gfx_round_rect(x+6,y+6,10,10,5,kind? 0xEF4444 : 0xF59E0B);
  gfx_rect(x+9,y+2,4,8,kind? 0xEF4444 : 0xF59E0B); if(kind) gfx_rect(x+10,y+4,2,8,0xFFFFFF); else gfx_rect(x+14,y+3,5,3,0xFFFFFF);
}
static void draw_start_menu(void){
  if(!start_open) return;
  menu_build();
  int x0=menu_x(), y0=menu_y(), mh=menu_h(), rows=menu_rows();
  shadow(x0,y0,MW,mh);
  gfx_round_rect(x0,y0,MW,mh,8,PANEL_LINE); gfx_round_rect(x0+1,y0+1,MW-2,mh-2,8,PANEL_BG);
  const struct font *hf=ui(FS_BOLD,11), *nf=ui(FS_REGULAR,14);
  ui_text(hf,x0+MPAD+8,y0+MPAD+6,"PROGRAMS",8,0x9CA3AF);
  for(int i=0;i<rows;i++){
    int iy=y0+MPAD+MHEAD+i*ih();
    if(i<nmain){
      int hov=(start_hover==i); const char *an=item_key(main_idx[i]); char nm[48]; item_display(main_idx[i],nm,sizeof nm); int l=strlen(nm);
      if(hov) gfx_round_rect(x0+MPAD,iy,MW-2*MPAD,ih()-2,6,PANEL_HOVER);
      app_icon(x0+MPAD+8,iy+(ih()-2-26)/2,26,an,hov); ui_text(nf,x0+MPAD+44,iy+(ih()-2-ui_lineh(nf))/2,nm,l,0xF3F4F6);
    } else if(ndemo){                                                 /* "Demos & tests" row with a chevron */
      int hov=(start_hover==100||demo_open);
      if(hov) gfx_round_rect(x0+MPAD,iy,MW-2*MPAD,ih()-2,6,PANEL_HOVER);
      gfx_round_rect(x0+MPAD+8,iy+(ih()-2-26)/2,26,26,6,0x4B5563); ui_text(ui(FS_BOLD,14),x0+MPAD+8+8,iy+(ih()-2-ui_lineh(ui(FS_BOLD,14)))/2,"#",1,0xFFFFFF);
      ui_text(nf,x0+MPAD+44,iy+(ih()-2-ui_lineh(nf))/2,"Demos & tests",13,0xF3F4F6);
      int cx=x0+MW-MPAD-22, cy=iy+(ih()-2)/2; for(int k=0;k<5;k++){ gfx_rect(cx+k,cy-4+k,1,1,0xD1D5DB); gfx_rect(cx+k,cy+4-k,1,1,0xD1D5DB); }
    } else if(!nmain) ui_text(nf,x0+MPAD+10,iy+(ih()-2-ui_lineh(nf))/2,"(no programs)",13,0x9CA3AF);
  }
  int sy=y0+MPAD+MHEAD+rows*ih()+6; gfx_rect(x0+MPAD+6,sy,MW-2*MPAD-12,1,PANEL_LINE);
  static const char *const pw[2]={"Restart","Shut down"};
  for(int k=0;k<2;k++){
    int iy=y0+MPAD+MHEAD+rows*ih()+16+k*ih(), hov=(start_hover==101+k);
    if(hov) gfx_round_rect(x0+MPAD,iy,MW-2*MPAD,ih()-2,6,PANEL_HOVER);
    power_icon(x0+MPAD+10,iy+(ih()-2-22)/2,k); ui_text(nf,x0+MPAD+44,iy+(ih()-2-ui_lineh(nf))/2,pw[k],strlen(pw[k]),0xF3F4F6);
  }
  if(demo_open && ndemo){                                             /* side panel */
    int fx=fly_x(), fy=fly_y(), fh=fly_h(), fi=fly_ih();
    shadow(fx,fy,FW,fh); gfx_round_rect(fx,fy,FW,fh,8,PANEL_LINE); gfx_round_rect(fx+1,fy+1,FW-2,fh-2,8,PANEL_BG);
    for(int j=0;j<ndemo;j++){
      int iy=fy+MPAD+j*fi, hov=(start_hover==200+j); const char *an=app_name(demo_idx[j]); char nm[48]; app_display(an,nm,sizeof nm); int l=strlen(nm);
      if(hov) gfx_round_rect(fx+MPAD-4,iy,FW-2*MPAD+8,fi-2,6,PANEL_HOVER);
      app_icon(fx+MPAD+2,iy+(fi-2-22)/2,22,an,hov); ui_text(nf,fx+MPAD+34,iy+(fi-2-ui_lineh(nf))/2,nm,l,0xF3F4F6);
    }
  }
}
static u32 *wallpaper; static int wp_theme;
/* theme: gradient top/bottom colours + three glow colours */
static const struct { u32 top, bot, g0, g1, g2; } themes[]={
  {0x0B1026,0x1A2A5C,0x7C3AED,0x06B6D4,0x2563EB},   /* Willow (default) */
  {0x07160F,0x123524,0x16A34A,0x65A30D,0x0D9488},   /* Forest */
  {0x1F0B14,0x4A1D2E,0xF97316,0xE11D48,0xA21CAF},   /* Sunset */
  {0x05060A,0x10131C,0x475569,0x334155,0x1E3A8A},   /* Midnight */
  {0x06192B,0x0B3A5C,0x0EA5E9,0x14B8A6,0x3B82F6},   /* Ocean */
  {0x1A1A1D,0x2B2B31,0x9CA3AF,0x6B7280,0x4B5563},   /* Graphite */
};
#define NTHEMES 6
void wm_set_theme(int t){ if(t<0||t>=NTHEMES) t=0; if(t==wp_theme && wallpaper) return; wp_theme=t; wallpaper=0; dirty=1; }
int wm_get_theme(void){ return wp_theme; }
static void wallpaper_build(void){                                       /* gradient + three soft colour glows, rendered once */
  int W_=gfx_w, H_=gfx_h; wallpaper=kmalloc((u64)W_*H_*4,4096); if(!wallpaper) return;
  struct { int cx, cy, r; u32 col; int amax; } g[3]={ {W_*18/100,H_*25/100,W_*48/100,themes[wp_theme].g0,105}, {W_*86/100,H_*30/100,W_*52/100,themes[wp_theme].g1,85}, {W_*48/100,H_*100/100,W_*60/100,themes[wp_theme].g2,115} };
  u32 T=themes[wp_theme].top, B=themes[wp_theme].bot;
  for(int y=0;y<H_;y++){
    u32 base=0; { int r=(int)((T>>16)&255)+((int)((B>>16)&255)-(int)((T>>16)&255))*y/H_, gg=(int)((T>>8)&255)+((int)((B>>8)&255)-(int)((T>>8)&255))*y/H_, b=(int)(T&255)+((int)(B&255)-(int)(T&255))*y/H_; base=((u32)r<<16)|((u32)gg<<8)|(u32)b; }
    for(int x=0;x<W_;x++){
      u32 c=base;
      for(int k=0;k<3;k++){
        int dx=x-g[k].cx, dy=y-g[k].cy; long d2=(long)dx*dx+(long)dy*dy, R2=(long)g[k].r*g[k].r; if(d2>=R2) continue;
        long t=(R2-d2)*256/R2; int a=(int)((t*t>>8)*g[k].amax>>8); c=gfx_blend(c,g[k].col,a);
      }
      wallpaper[(u64)y*W_+x]=c;
    }
  }
}
void wm_dirty(void){ dirty=1; }
void wm_app_icon(int x,int y,int sz,const char *name){ app_icon(x,y,sz,name,0); }
void wm_shadow(int x,int y,int w,int h){ shadow(x,y,w,h); }
static void wm_paint(void){
  int off=gfx_offscreen_begin();
  if(!wallpaper) wallpaper_build();
  if(wallpaper) gfx_blit_stride(0,0,gfx_w,gfx_h,wallpaper,gfx_w); else gfx_gradient(0,0,gfx_w,tb_y(),0x0E1B38,0x3E6FB0,0);
  { const struct font *wf=ui(FS_BOLD,24); const char *t="Willows"; ui_text(wf,gfx_w-ui_width(wf,t,7)-28,tb_y()-ui_lineh(wf)-22,t,7,0x7FA2D6); }
  desktop_draw();
  for(int i=0;i<nz;i++){ struct wnd *w=get(Z[i]); if(w && !w->minimized){ int ox,oy; outer_origin(w,&ox,&oy); draw_wnd(w,ox,oy); } }
  desktop_ctx_draw();
  { int p=(int)(ticks-start_t0)*256/200; if(p>256) p=256; if(p<0) p=0; int q=256-p; int ease=256-(int)((long)q*q*q>>16);   /* ease-out cubic */
    menu_dy= start_open? (256-ease)*(menu_h()+16)/256 : 0; }
  draw_start_menu(); draw_taskbar();
  if(off) gfx_present();
}

/* ---- GDI surfaces ---- */
int wm_screen_w(void){ return wm_on? (int)gfx_w-120 : (int)gfx_w; }          /* size a GDI program sees as its "screen" */
int wm_screen_h(void){ return wm_on? (int)gfx_h-TB_H-2*FRAME-TITLE_H-70 : (int)gfx_h; }
static struct wnd *surface(void){
  for(int i=0;i<MAXW;i++) if(W[i].used && W[i].kind==K_SURFACE && W[i].owner==cur->pid) return &W[i];
  int idx=-1; for(int i=0;i<MAXW;i++) if(!W[i].used){ idx=i; break; } if(idx<0) return 0;
  struct wnd *w=&W[idx]; memset(w,0,sizeof *w); w->used=1; w->owner=cur->pid; w->kind=K_SURFACE; w->proc=4;
  w->pw=wm_screen_w(); w->ph=wm_screen_h(); w->stride=w->pw; w->w=w->pw+2*FRAME; w->h=w->ph+FRAME+TITLE_H+FRAME; w->x=24+16*nz; w->y=16+16*nz; w->style=WS_VISIBLE|WS_CAPTION; cpy(w->cls,"GDI",24);
  w->pix=heap_alloc((u64)w->pw*w->ph*4); if(!w->pix){ w->used=0; return 0; } w->ownpix=1;
  for(i32 k=0;k<w->pw*w->ph;k++) w->pix[k]=0x202020;
  settext(w,cur->name); w->visible=1; Z[nz++]=hof(w); created=1; set_focus(hof(w)); dirty=1; return w;
}
/* Windows with a message loop get a backing bitmap (max screen size) on first GetDC/BeginPaint; the compositor shows it. */
static void ensure_backing(struct wnd *w){
  if(w->kind!=K_APP) return;
  if(!w->pix){ w->pix=heap_alloc((u64)gfx_w*gfx_h*4); if(!w->pix) return; w->ownpix=1; w->stride=gfx_w; for(u64 k=0;k<(u64)gfx_w*gfx_h;k++) w->pix[k]=0xFFFFFF; }
  client_size(w,&w->pw,&w->ph);
}
u64 wm_get_dc(u64 hwnd){
  struct wnd *w=get(hwnd); if(!w) return gfx_ok? 0x1000 : 0;
  ensure_backing(w); return w->pix? DCBASE+(w-W) : 0;
}
static struct wnd *dc_target(u64 dc){
  if(dc>=DCBASE && dc<DCBASE+MAXW){ struct wnd *w=&W[dc-DCBASE]; return (w->used&&w->pix)? w : 0; }
  for(int i=0;i<MAXW;i++) if(W[i].used && W[i].kind==K_APP && W[i].owner==cur->pid) return 0;   /* drawing on the screen DC by a program with windows: nothing to show (Windows paints the desktop, briefly) */
  return surface();
}
int dc_clip(u64 dc, int *r);                                          /* gdi.inc: the DC's clip rectangle, if one is set */
void wm_gdi_rect(u64 dc,int x,int y,int w,int h,u32 c){
  if(!wm_on){ gfx_rect(x,y,w,h,c); return; }
  struct wnd *s=dc_target(dc); if(!s) return;
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; } if(x+w>s->pw) w=s->pw-x; if(y+h>s->ph) h=s->ph-y;
  { int cl[4]; if(dc_clip(dc,cl)){ if(x<cl[0]){ w-=cl[0]-x; x=cl[0]; } if(y<cl[1]){ h-=cl[1]-y; y=cl[1]; } if(x+w>cl[2]) w=cl[2]-x; if(y+h>cl[3]) h=cl[3]-y; } }
  if(w<=0||h<=0) return;
  for(int j=0;j<h;j++){ u32 *p=s->pix+(u64)(y+j)*s->stride+x; for(int i=0;i<w;i++) p[i]=c; }
  dirty=1;
}
void wm_gdi_pixel(u64 dc,int x,int y,u32 c){
  if(!wm_on){ gfx_pixel(x,y,c); return; }
  struct wnd *s=dc_target(dc); if(s && x>=0&&y>=0&&x<s->pw&&y<s->ph){ s->pix[(u64)y*s->stride+x]=c; dirty=1; }
}
void wm_gdi_text(u64 dc,const struct font *f,int x,int y,const char *str,int n,u32 fg,u32 bg,int opaque){
  if(!wm_on){ ui_text(f,x,y,str,n,fg); return; }
  struct wnd *s=dc_target(dc); if(!s) return;
  if(opaque){ int tw=ui_width(f,str,n); wm_gdi_rect(dc,x,y,tw,ui_lineh(f),bg); }
  ui_text_to(f,s->pix,s->stride,s->pw,s->ph,x,y,str,n,fg); dirty=1;
}
/* StretchDIBits for 32-bit DIBs (top-down if biHeight<0). rop 0x00CC0020 = copy (alpha ignored), 0x00AA0029 = alpha-blend with the source alpha byte. */
static i32 WINAPI w_StretchDIBits(u64 dc,int dx,int dy,int dw,int dh,int sx,int sy,int sw,int sh,const u32 *bits,const u8 *bmi,u32 usage,u32 rop){
  (void)usage; struct wnd *t=dc_target(dc);
  if(!t||!bits||!bmi||!dw||!dh||!sw||!sh) return 0;
  int bw=*(const i32*)(bmi+4), bh=*(const i32*)(bmi+8); int bpp=*(const u16*)(bmi+14); if(bpp!=32) return 0;
  int topdown=bh<0; if(topdown) bh=-bh;
  /* As on Windows (and Wine): the source y is measured from the bottom of the image, for top-down DIBs too; a negative width or
     height mirrors that rectangle (it then spans x+w .. x-1), and the copy is flipped when source and destination signs differ. */
  int syt=bh-sy-sh;                                                  /* top of the source rectangle, counting rows from the top of the image */
  int hflip=(dw<0)!=(sw<0), vflip=(dh<0)!=(sh<0);
  if(dw<0){ dx+=dw; dw=-dw; } if(dh<0){ dy+=dh; dh=-dh; } if(sw<0){ sx+=sw; sw=-sw; } if(sh<0){ syt+=sh; sh=-sh; }
  u64 xs=((u64)sw<<16)/(u64)dw, ys=((u64)sh<<16)/(u64)dh;
  int keep_alpha= !(t->style&WS_CHILD) && (t->style&0x80000000u) && !has_frame(t) && dw>=t->pw && dh>=t->ph;   /* a whole frame for a popup: keep its alpha */
  if(keep_alpha) t->alpha=1;
  for(int j=0;j<dh;j++){
    int y=dy+j; if(y<0||y>=t->ph) continue;
    int oj=(int)(((u64)j*ys)>>16); int srow=syt+(vflip? sh-1-oj : oj); if(srow<0||srow>=bh) continue; if(!topdown) srow=bh-1-srow;
    const u32 *srcrow=bits+(u64)srow*bw; u32 *dstrow=t->pix+(u64)y*t->stride;
    for(int i=0;i<dw;i++){
      int x=dx+i; if(x<0||x>=t->pw) continue;
      int oi=(int)(((u64)i*xs)>>16); int scol=sx+(hflip? sw-1-oi : oi); if(scol<0||scol>=bw) continue;
      u32 p=srcrow[scol];
      if(rop==0x00AA0029u){ u32 a=p>>24; if(a==0) continue; if(a==255){ dstrow[x]=p&0xFFFFFF; continue; }
        u32 d=dstrow[x]; u32 r=(((p>>16)&255)*a+((d>>16)&255)*(255-a))/255, g=(((p>>8)&255)*a+((d>>8)&255)*(255-a))/255, b=((p&255)*a+(d&255)*(255-a))/255; dstrow[x]=(r<<16)|(g<<8)|b; }
      else dstrow[x]= keep_alpha? p : (p&0xFFFFFF);
    }
  }
  dirty=1; return dh;
}
static u64 WINAPI w_BeginPaint(u64 h, u8 *ps){
  struct wnd *w=get(h); if(!w||!ps) return 0; ensure_backing(w); w->paint_pending=0;
  memset(ps,0,72); *(u64*)ps=w->pix? DCBASE+(w-W) : 0; *(i32*)(ps+12)=0; *(i32*)(ps+16)=0; *(i32*)(ps+20)=w->pw; *(i32*)(ps+24)=w->ph;
  return *(u64*)ps;
}
static i32 WINAPI w_EndPaint(u64 h, const u8 *ps){ (void)h;(void)ps; dirty=1; return 1; }
/* the mouse in the coordinates of the calling program's GDI surface */
void wm_cursor_pos(int *x,int *y){
  *x=mouse_x; *y=mouse_y; if(!wm_on) return;
  for(int i=0;i<MAXW;i++) if(W[i].used && W[i].kind==K_SURFACE && W[i].owner==cur->pid){ int cx,cy; client_origin(&W[i],&cx,&cy); *x-=cx; *y-=cy; return; }
}
/* background processes don't see the keyboard/mouse (GetAsyncKeyState semantics) */
int wm_proc_focused(void){ struct wnd *t=get(top_visible()); return !t || t->owner==cur->pid; }

/* ---- the kernel-owned Console window (text of console programs and kernel errors) ---- */
static void create_console(void){
  int idx=-1; for(int i=0;i<MAXW;i++) if(!W[i].used){ idx=i; break; } if(idx<0) return;
  struct wnd *w=&W[idx]; memset(w,0,sizeof *w); w->used=1; w->owner=0; w->kind=K_CONSOLE; w->proc=4;
  w->style=WS_CAPTION|WS_VISIBLE; w->exstyle=0; cpy(w->cls,"CONSOLE",24);
  mono_init(); w->x=120+20*nz; w->y=60+20*nz; w->w=CON_COLS*cell_w+4+2*FRAME; w->h=CON_ROWS*cell_h+4+FRAME+TITLE_H+FRAME;
  settext(w,"Console"); w->visible=1; con_win=hof(w); Z[nz++]=con_win; created=1; set_focus(con_win); dirty=1;
}
void wm_console_dirty(void){
  if(!wm_on) return;
  if(!get(con_win) && !con_quiet) create_console();
  dirty=1;
}
int wm_desktop_on(void){ return wm_on; }
void wm_desktop_start(void){ wm_on=1; kb_desktop=1; console_desktop(1); dirty=1; }

/* ---- input -> messages ---- */
static u64 modal_of(int owner);
static u64 hit(int mx, int my, int *part, int *lx, int *ly){
  for(int i=nz-1;i>=0;i--){
    struct wnd *w=get(Z[i]); if(!w||!w->visible||w->minimized) continue;
    { u64 md=modal_of(w->owner); if(md && md!=Z[i]) continue; }
    int ox,oy; outer_origin(w,&ox,&oy);
    if(mx<ox||my<oy||mx>=ox+w->w||my>=oy+w->h) continue;
    if(has_frame(w) && my<oy+FRAME+TITLE_H && my>=oy+FRAME){
      *part=1;                                                           /* 1 title, 2 close, 3 maximize, 4 minimize */
      for(int k=0;k<ncap(w);k++){ int dc=ox+w->w-FRAME-DOT_EDGE-DOT_PITCH*k; if(mx>=dc-DOT_PITCH/2 && mx<dc+DOT_PITCH/2){ *part= k==0? 2 : (w->kind==K_APP? (k==1? 3 : 4) : 4); } }
      *lx=mx-ox; *ly=my-oy; return Z[i];
    }
    u64 best=Z[i];
    for(int pass=0;pass<4;pass++){                                    /* deepest child under the pointer */
      u64 found=0;
      for(int j=MAXW-1;j>=0;j--) if(W[j].used && W[j].parent==best && W[j].visible && !(W[j].kind==K_CTRL && (W[j].ctype==CT_GROUP||W[j].ctype==CT_LINE||W[j].ctype==CT_STATIC))){
        int cx,cy; client_origin(&W[j],&cx,&cy);
        if(mx>=cx&&my>=cy&&mx<cx+W[j].w&&my<cy+W[j].h){ found=HW(j); break; }
      }
      if(!found) break; best=found;
    }
    struct wnd *b=get(best); int cx,cy; client_origin(b,&cx,&cy);
    *part=0; *lx=mx-cx; *ly=my-cy; return best;
  }
  return 0;
}
/* maximize / restore. In the window's own thread (ShowWindow, WM_SYSCOMMAND, a click on its own caption buttons) it is told at once
   with WM_WINDOWPOSCHANGED (then WM_SIZE with SIZE_MAXIMIZED / SIZE_RESTORED), as on Windows - programs such as Firefox take their
   size from that; from elsewhere (our own caption buttons, handled in whichever thread runs the input) WM_SIZE is posted. */
static void set_max(struct wnd *w, int on, int in_thread){
  if(on==w->maximized) return;
  if(on){ w->rx=w->x; w->ry=w->y; w->rw=w->w; w->rh=w->h; w->x=0; w->y=0; w->w=gfx_w; w->h=gfx_h-TB_H; w->maximized=1; }
  else { w->x=w->rx; w->y=w->ry; w->w=w->rw; w->h=w->rh; w->maximized=0; }
  if(in_thread && w->kind==K_APP) pos_changed(w,4);
  else { int cw,ch; client_size(w,&cw,&ch); post(hof(w),WM_SIZE,w->maximized?2:0,((i64)ch<<16)|(cw&0xFFFF)); }
  invalidate(w); dirty=1;
}
static void toggle_max(struct wnd *w){ set_max(w,!w->maximized,0); }
static void minimize(struct wnd *w){
  w->minimized=1; if(g_focus==hof(w) || get(g_focus)==0 || get(g_focus)->minimized){ u64 t=top_visible(); g_focus=0; if(t){ u64 e=first_edit(t); set_focus(e? e : t); } }
  dirty=1;
}
static void restore_and_focus(u64 h){ struct wnd *w=get(h); if(!w) return; w->minimized=0; raise_top(h); u64 e=first_edit(h); set_focus(e? e : h); dirty=1; }
static void start_action(int code){
  if(code==101) power_restart();
  else if(code==102) power_off();
  else if(code>=200){ if(code-200<ndemo) proc_spawn(app_name(demo_idx[code-200])); }
  else if(code<nmain){
    int idx=main_idx[code];
    if(idx==SC_GET_LW) proc_spawn_cmd("terminal","terminal -c getlibrewolf");      /* the terminal downloads and installs it */
    else if(idx>=SC_BASE) shortcut_launch(idx-SC_BASE);
    else proc_spawn(app_name(idx));
  }
}
static void pump_input(void){
  int vk, ch;
  while(kb_event(&vk,&ch)){
    if(start_open && vk==0x1B){ start_open=0; demo_open=0; dirty=1; continue; }
    if(vk==0x54 && kb_vk(0x11) && kb_vk(0x10)){ proc_spawn("terminal"); continue; }          /* Ctrl+Shift+T */
    if(desktop_key(vk,ch)) continue;
    if(desktop_has_focus()) continue;                                  /* the desktop has the keyboard: typing goes nowhere */
    u64 t=g_focus; if(!get(t)) t=top_visible(); if(!t) continue;
    struct wnd *tw=get(t);
    if(tw->kind==K_CONSOLE){ ci_key(vk,ch); if(ch){ if(ch==13) ch='\n'; if(ch=='\n'||ch==8||ch>=32) { kb_inject(ch); } } continue; }
    post(t,WM_KEYDOWN,vk,1|((u64)(u16)ch<<32));                       /* the character rides along; TranslateMessage turns it into WM_CHAR like on Windows */
  }
  int mx=mouse_x, my=mouse_y, b=mouse_buttons&1;
  static int lb;
  if(start_open){ menu_build(); int it=start_item_at(mx,my); if(it!=start_hover){ start_hover=it; dirty=1; } }
  desktop_hover(mx,my);
  { static int lrb; static u64 rtarget; int rb=(mouse_buttons>>1)&1;      /* right button: the desktop's context menu, or the program's window */
    if(rb && !lrb && my<tb_y() && !start_open){ int part=0,ax=0,ay=0; u64 h=hit(mx,my,&part,&ax,&ay);
      if(!h){ if(g_focus) set_focus(0); desktop_rclick(mx,my); }
      else { struct wnd *w=get(h); rtarget=0; if(w && part==0 && w->owner!=0){ int cx,cy; client_origin(w,&cx,&cy); rtarget=h; post(h,0x204,2,((i64)((my-cy)&0xFFFF)<<16)|((mx-cx)&0xFFFF)); } } }   /* WM_RBUTTONDOWN */
    if(!rb && lrb && get(rtarget)){ struct wnd *w=get(rtarget); int cx,cy; client_origin(w,&cx,&cy);
      post(rtarget,0x205,0,((i64)((my-cy)&0xFFFF)<<16)|((mx-cx)&0xFFFF));                                       /* WM_RBUTTONUP */
      post(rtarget,0x7B,rtarget,((i64)(my&0xFFFF)<<16)|(mx&0xFFFF)); rtarget=0; }                                  /* WM_CONTEXTMENU (screen coordinates) */
    lrb=rb; }
  { int nt=-1; u64 nw=0; int np=0;                                      /* hover state for taskbar and caption buttons */
    if(my>=tb_y()){ nt= in_start_btn(mx,my)? -2 : tb_button_at(mx,my); }
    else if(!(start_open && start_item_at(mx,my)>=0)){ int part=0,ax=0,ay=0; u64 hh=hit(mx,my,&part,&ax,&ay); if(hh && part>=2){ nw=hh; np=part; } }
    if(nt!=hover_tb || nw!=hover_win || np!=hover_part){ hover_tb=nt; hover_win=nw; hover_part=np; dirty=1; } }
  { static int lmx=-1, lmy=-1; static u64 last_win;                  /* WM_MOUSEMOVE to the window under (or capturing) the pointer */
    if(mx!=lmx||my!=lmy){
      lmx=mx; lmy=my; u64 target=g_capture; int part=0,ax=0,ay=0;
      if(my>=tb_y() || (start_open && start_item_at(mx,my)>=0)) target=0;
      else if(!target) target=hit(mx,my,&part,&ax,&ay);
      if(target!=last_win){ gfx_cursor_shape(0); last_win=target; }
      if(g_track_leave && g_track_leave!=target){ if(get(g_track_leave)) post(g_track_leave,0x2A3,0,0); g_track_leave=0; }   /* TrackMouseEvent(TME_LEAVE): WM_MOUSELEAVE */
      struct wnd *tw=get(target);
      if(tw && part==0 && tw->owner!=0){ int cx,cy; client_origin(tw,&cx,&cy); post(target,WM_MOUSEMOVE,(mouse_buttons&1)|((mouse_buttons&2)?2:0),((i64)(my-cy)<<16)|((mx-cx)&0xFFFF)); }
    }
    int wz=mouse_wheel;
    if(wz){ mouse_wheel=0; int part=0,ax=0,ay=0; u64 target=(my>=tb_y())? 0 : hit(mx,my,&part,&ax,&ay); if(!get(target)) target=g_focus; if(!get(target)) target=top_visible();
      struct wnd *tw=get(target); if(tw && tw->owner!=0){ i64 delta=-wz*120; post(target,WM_MOUSEWHEEL,((u64)(u16)(short)delta<<16)|(mouse_buttons&1),((i64)my<<16)|(mx&0xFFFF)); } }
  }
  if(g_drag){ struct wnd *w=get(g_drag); if(w && b){ w->x=mx-drag_dx; w->y=my-drag_dy; dirty=1; } if(!b) g_drag=0; }
  int was=lb; lb=b;                                                   /* remember before acting: acting may never return (process killed) */
  if(b && !was){                                                      /* button down */
    int handled=0;
    if(desktop_ctx_open()){ handled=desktop_ctx_click(mx,my); }
    else if(start_open){
      menu_build(); int it=start_item_at(mx,my); dirty=1;
      if(it==100){ demo_open=!demo_open; handled=1; }                 /* toggles the side panel, menu stays open */
      else { start_open=0; demo_open=0; if(it>=0){ handled=1; start_action(it); } else if(in_start_btn(mx,my) || my>=tb_y()) handled=1; }   /* closing click is consumed on the taskbar */
    } else if(in_start_btn(mx,my)){ start_open=1; start_t0=ticks; demo_open=0; start_hover=-1; dirty=1; handled=1; }
    else if(my>=tb_y()){
      int bi=tb_button_at(mx,my);
      if(bi>=0){ u64 ws[16]; int n=tb_windows(ws); if(bi<n){ struct wnd *tw=get(ws[bi]); if(tw->minimized) restore_and_focus(ws[bi]); else if(ws[bi]==top_visible()) minimize(tw); else restore_and_focus(ws[bi]); } }
      handled=1;
    }
    if(!handled){
      int part=0, ax=0, ay=0; u64 h=hit(mx,my,&part,&ax,&ay);
      if(h){
        struct wnd *w=get(h); u64 top=h; while(get(top) && (get(top)->style&WS_CHILD)) top=get(top)->parent;
        if(get(top)->kind==K_APP && part==0){                               /* a program's window: it decides (in its own thread) whether */
          g_capture=h;                                                       /* the click activates it and whether it hit its own title bar */
          post(h,WM_W_CLICK,((u64)(u32)ay<<32)|(u32)ax,((i64)(u32)my<<32)|(u32)mx);
          return;
        }
        raise_top(top);
        if(part==2){ if(w->kind==K_CONSOLE) destroy(h); else if(w->kind==K_SURFACE) proc_kill(w->owner); else post(h,WM_CLOSE,0,0); }
        else if(part==3) toggle_max(w);
        else if(part==4) minimize(w);
        else if(part==1){ if(!w->maximized){ g_drag=h; drag_dx=mx-w->x; drag_dy=my-w->y; } if(w->kind==K_CONSOLE||!first_edit(h)) set_focus(h); }
        else { g_capture=h; post(h,WM_LBUTTONDOWN,1,((i64)ay<<16)|(ax&0xFFFF)); if(w->kind!=K_EDIT && !(w->style&WS_CHILD)) set_focus(first_edit(h)?:h); }
        if(w->kind==K_EDIT) set_focus(h);
      } else { if(g_focus) set_focus(0); desktop_click(mx,my); }       /* clicked the bare desktop */
    }
  } else if(!b && was){
    if(g_capture){ struct wnd *w=get(g_capture); if(w){ int cx,cy; client_origin(w,&cx,&cy); post(g_capture,WM_LBUTTONUP,0,(((i64)(my-cy))<<16)|((mx-cx)&0xFFFF)); } g_capture=0; }
  }
}
/* Called by every waiting loop: route input, repaint if anything changed. */
static void timers_service(void);
void wm_post_msg(u64 hwnd,u32 msg,u64 wp,i64 lp){ post(hwnd,msg,wp,lp); }
void wm_service(void){
  if(!wm_on) return;
  static u32 blink, clk_sec;
  pump_input(); timers_service(); ws_async_poll();
  struct wnd *f=get(g_focus);
  if(((ticks/500)&1)!=blink && f && (f->kind==K_EDIT||f->kind==K_CONSOLE)){ blink=(ticks/500)&1; dirty=1; }
  if(ticks/1000!=clk_sec){ clk_sec=ticks/1000; dirty=dirty||0; char c[8]; static char last[8]; clock_text(c); if(strcmp(c,last)){ memcpy(last,c,8); dirty=1; } }
  if(start_open && (u32)(ticks-start_t0)<260) dirty=1;                  /* keep repainting while the menu slides in */
  static u32 last_paint;
  if(dirty && (u32)(ticks-last_paint)>=16){ dirty=0; last_paint=ticks; wm_paint(); }
}
/* the first queued message for the calling thread that matches the filters (hwnd: 0 = any, -1 = thread messages only;
   lo..hi: message range, 0..0 = all); remove = PM_REMOVE */
static int msg_matches(const struct msg *m, u32 me, u64 h, u32 lo, u32 hi){
  if(m->tid && m->tid!=me) return 0;
  if(h==(u64)-1){ if(m->hwnd) return 0; } else if(h && m->hwnd!=h) return 0;
  if((lo||hi) && (m->msg<lo || m->msg>hi)) return 0;
  return 1;
}
/* A click on a program's window, handled in the window's own thread (its window procedure may only run there):
   WM_MOUSEACTIVATE decides whether it takes the focus (menus say no), WM_NCHITTEST whether a program that draws its own title
   bar was hit there (drag, close, maximize, minimize). Returns 1 if the click goes on as WM_LBUTTONDOWN. */
static int click_in_context(u64 h, int mx, int my){
  struct wnd *w=get(h); if(!w) return 0;
  u64 top=h; while(get(top) && (get(top)->style&WS_CHILD)) top=get(top)->parent; struct wnd *tw=get(top); if(!tw) return 0;
  i64 ma=send(top,0x21,top,((i64)WM_LBUTTONDOWN<<16)|1);                 /* WM_MOUSEACTIVATE, HTCLIENT */
  if(ma==4){ g_capture=0; return 0; }                                      /* MA_NOACTIVATEANDEAT */
  if(ma==3) return 1;                                                      /* MA_NOACTIVATE */
  raise_top(top);
  if(tw->custom_frame){
    i64 ht=send(top,0x84,0,((i64)(my&0xFFFF)<<16)|(mx&0xFFFF));            /* WM_NCHITTEST */
    if(wm_trace()) kdbg("  [wm] click at %d,%d: hit test %d\n",mx,my,(int)ht);
    if(ht==2){ g_capture=0; if(!tw->maximized){ g_drag=top; drag_dx=mx-tw->x; drag_dy=my-tw->y; } set_focus(top); return 0; }   /* HTCAPTION */
    if(ht==20){ g_capture=0; post(top,WM_CLOSE,0,0); return 0; }           /* HTCLOSE */
    if(ht==9){ g_capture=0; set_max(tw,!tw->maximized,1); return 0; }      /* HTMAXBUTTON */
    if(ht==8){ g_capture=0; minimize(tw); return 0; }                      /* HTMINBUTTON */
  }
  if(w->kind!=K_EDIT && !(w->style&WS_CHILD)) set_focus(first_edit(h)?:h);
  return 1;
}
int wm_has_msg(void){ struct proc *p=cur; u32 me=thread_cur_tid(); for(u32 i=p->qt;i!=p->qh;i=(i+1)&(MSGQ-1)) if(msg_matches(&p->q[i],me,0,0,0)) return 1; return 0; }
static i32 pop_msg_ex(u8 *msg, u64 h, u32 lo, u32 hi, int remove){
  struct proc *p=cur; u32 me=thread_cur_tid();
  for(u32 i=p->qt;i!=p->qh;i=(i+1)&(MSGQ-1)){
    if(p->q[i].msg==WM_W_CLICK && p->q[i].tid==me){                      /* our click: decide now, in the window's thread */
      struct msg c=p->q[i]; int mx=(int)(u32)c.lp, my=(int)(u32)((u64)c.lp>>32), ax=(int)(u32)c.wp, ay=(int)(u32)(c.wp>>32);
      if(click_in_context(c.hwnd,mx,my)){ p->q[i].msg=WM_LBUTTONDOWN; p->q[i].wp=1; p->q[i].lp=((i64)(ay&0xFFFF)<<16)|(ax&0xFFFF); }
      else { for(u32 j=i;j!=p->qt;j=(j-1)&(MSGQ-1)) p->q[j]=p->q[(j-1)&(MSGQ-1)]; p->qt=(p->qt+1)&(MSGQ-1); i=(p->qt-1)&(MSGQ-1); continue; }   /* dropped: rescan */
    }
    if(!msg_matches(&p->q[i],me,h,lo,hi)) continue;
    struct msg m=p->q[i];
    if(remove){ for(u32 j=i;j!=p->qt;j=(j-1)&(MSGQ-1)) p->q[j]=p->q[(j-1)&(MSGQ-1)]; p->qt=(p->qt+1)&(MSGQ-1);   /* close the gap */
      struct wnd *tw=get(m.hwnd); if(tw){ if(m.msg==WM_PAINT) tw->paint_pending=0; if(m.msg==WM_MOUSEMOVE) tw->mm_pending=0; }
      { static int t=-1; if(t<0){ struct ramfile *f=fs_find("/System/apitrace.cfg"); t= f && !f->isdir; } if(t && m.msg>=0xC000) kdbg("  [msg] got %x hwnd %p (tid %x, pid %d)\n",m.msg,(void*)m.hwnd,me,p->pid); } }
    *(u64*)(msg+0)=m.hwnd; *(u32*)(msg+8)=m.msg; *(u64*)(msg+16)=m.wp; *(i64*)(msg+24)=m.lp; *(u32*)(msg+32)=ticks;
    *(i32*)(msg+36)=mouse_x; *(i32*)(msg+40)=mouse_y; return 1;
  }
  return 0;
}
static i32 pop_msg(u8 *msg){ return pop_msg_ex(msg,0,0,0,1); }
static i32 WINAPI w_GetMessageA(u8 *msg, u64 h, u32 lo, u32 hi){
  thread_note_wait("GetMessage",h,(u64)__builtin_return_address(0));
  for(;;){
    if(cur->quit){ cur->quit=0; memset(msg,0,48); *(u32*)(msg+8)=WM_QUIT; *(u64*)(msg+16)=cur->quit_code; thread_end_wait(); return 0; }
    wm_service();
    if(pop_msg_ex(msg,h,lo,hi,1)){ thread_end_wait(); return 1; }
    idle_wait();
  }
}
static i32 WINAPI w_PeekMessageA(u8 *msg, u64 h, u32 lo, u32 hi, u32 rm){
  if(cur->quit){ if(rm&1) cur->quit=0; memset(msg,0,48); *(u32*)(msg+8)=WM_QUIT; *(u64*)(msg+16)=cur->quit_code; return 1; }
  wm_service(); return pop_msg_ex(msg,h,lo,hi,rm&1);                 /* PM_REMOVE = 1; PM_NOREMOVE leaves it queued */
}
/* PostThreadMessage: a message with no window for one thread (of any process) */
static i32 WINAPI w_PostThreadMessageA(u32 tid, u32 m, u64 wp, i64 lp){
  for(int i=0;i<MAXTHR;i++) if(threads[i].used && !threads[i].done && threads[i].tid==tid){ post_to(&procs[threads[i].pid],tid,0,m,wp,lp); return 1; }
  for(int p=1;p<MAXPROC;p++) if(procs[p].used && procs[p].teb && *(u32*)((u8*)procs[p].teb+0x48)==tid){ post_to(&procs[p],tid,0,m,wp,lp); return 1; }
  win32_set_last_error(1444); return 0;                                     /* ERROR_INVALID_THREAD_ID */
}
static i32 WINAPI w_TranslateMessage(const u8 *msg){
  if(*(const u32*)(msg+8)==WM_KEYDOWN){ u64 lp=*(const u64*)(msg+24); u32 ch=(u32)((lp>>32)&0xFFFF); if(ch) post(*(const u64*)msg,WM_CHAR,ch,1); }
  return 1;
}
static i64 WINAPI w_DispatchMessageA(const u8 *msg){
  u64 h=*(u64*)msg; return send(h,*(u32*)(msg+8),*(u64*)(msg+16),*(i64*)(msg+24));
}

/* ---- common dialog: Open / Save As - browses the folders of the filesystem ---- */
#define DW 640
#define DH 440
static u32 dsave[DW*DH];
static int fd_cmp(struct ramfile *a, struct ramfile *b){ if(a->isdir!=b->isdir) return b->isdir-a->isdir; return stricmp(fs_leaf(a),fs_leaf(b)); }
static int snprintf_dlg(char *o,u64 v,int kb){ char t[24]; int n=0; do{ t[n++]='0'+v%10; v/=10; }while(v); int k=0; while(n) o[k++]=t[--n]; o[k++]=' '; if(kb) o[k++]='K'; o[k++]='B'; o[k]=0; return k; }
static i32 file_dialog(u8 *ofn, int saving){
  if(!gfx_ok) return 0;
  char *out=*(char**)(ofn+48); u32 maxn=*(u32*)(ofn+56); const char *title=*(const char**)(ofn+88);
  if(!title) title=saving? "Save As" : "Open";
  char dir[128]; const char *idir=*(const char**)(ofn+80);                  /* lpstrInitialDir (e.g. Downloads for a download), else the current folder */
  if(idir && idir[0] && fs_isdir(idir)) fs_canon(idir,dir,sizeof dir); else fs_canon(".",dir,sizeof dir);
  if(!dir[0] && !(idir && idir[0])) cpy(dir,"Documents",128);
  int x=((int)gfx_w-DW)/2, y=((int)gfx_h-DH)/2;
  for(int j=0;j<DH;j++) for(int i=0;i<DW;i++) dsave[j*DW+i]=gfx_get(x+i,y+j);
  char name[64]; name[0]=0; int nlen=0, sel=-1, result=0, scroll=0; u32 lastclick=0; int lastsel=-2;
  if(out && out[0]){ const char *b=out; for(const char *q=out;*q;q++) if(*q=='\\'||*q=='/') b=q+1; cpy(name,b,64); nlen=strlen(name); }
  struct ramfile *ents[200]; int ne=0; int up=0;
  int prevb=mouse_buttons&1; kb_flush();
  const struct font *tf=ui(FS_REGULAR,13), *nf=ui(FS_REGULAR,14), *sf=ui(FS_REGULAR,12), *bf=ui(FS_BOLD,12);
  int need=1;
  for(;;){
    if(need){                                                            /* (re)read the folder */
      char adir[130]; adir[0]='/'; cpy(adir+1,dir,129);                   /* dir is canonical: list it from the root, not the program's current folder */
      struct ramfile *raw[200]; int n=fs_list(adir,raw,200); ne=0; up=dir[0]!=0;
      for(int i=0;i<n;i++){ int j=ne++; ents[j]=raw[i]; while(j>0 && fd_cmp(ents[j-1],ents[j])>0){ struct ramfile *t=ents[j]; ents[j]=ents[j-1]; ents[j-1]=t; j--; } }
      sel=-1; scroll=0; need=0;
    }
    int rows=ne+(up?1:0), vis=10, RH=28;
    shadow(x,y,DW,DH); gfx_round_rect(x,y,DW,DH,10,0x394152); gfx_round_rect(x+1,y+1,DW-2,DH-2,9,0xF5F6F8);
    gfx_rect(x+1,y+1,DW-2,36,0xFFFFFF); gfx_rect(x+1,y+36,DW-2,1,0xE1E4E8); ui_text(nf,x+16,y+9,title,(int)strlen(title),TXT_DARK);
    char pth[160]; int pn=0; pth[pn++]='C'; pth[pn++]=':'; pth[pn++]='\\'; for(const char *q=dir;*q&&pn<150;q++) pth[pn++]=*q=='/'? '\\' : *q; pth[pn]=0;
    gfx_round_rect(x+16,y+48,DW-32,30,6,0xE9ECF0); ui_text(tf,x+26,y+54,pth,pn,TXT_DARK);
    gfx_round_rect(x+16,y+88,DW-32,vis*RH+6,6,0xD8DCE2); gfx_round_rect(x+17,y+89,DW-34,vis*RH+4,5,0xFFFFFF);
    for(int r=0;r<vis;r++){
      int ri=r+scroll; if(ri>=rows) break; int ry=y+92+r*RH;
      int isup=up && ri==0; struct ramfile *f= isup? 0 : ents[ri-(up?1:0)];
      if(ri==sel) gfx_round_rect(x+22,ry,DW-44,RH-2,6,0xDDE8FB);
      if(isup||f->isdir){ gfx_round_rect(x+32,ry+8,20,13,3,0xE9A33A); gfx_round_rect(x+32,ry+11,20,12,3,0xFFC857); }
      else { gfx_round_rect(x+36,ry+5,14,18,3,0xC9CED6); gfx_round_rect(x+37,ry+6,12,16,2,0xFAFAFB); }
      const char *nm= isup? ".." : fs_leaf(f); ui_text(nf,x+64,ry+(RH-ui_lineh(nf))/2,nm,(int)strlen(nm),TXT_DARK);
      if(f && !f->isdir){ char sz[24]; u64 v=f->size; int k=0; if(v<1024){ k=snprintf_dlg(sz,v,0); } else k=snprintf_dlg(sz,v/1024,1); ui_text(sf,x+DW-130,ry+(RH-ui_lineh(sf))/2,sz,k,TXT_MUTED); }
    }
    if(!rows) ui_text(nf,x+36,y+100,"This folder is empty.",21,0x9CA3AF);
    ui_text(tf,x+16,y+DH-92+4,"File name:",10,TXT_DARK);
    gfx_round_rect(x+100,y+DH-96,DW-120,30,6,0x9CA3AF); gfx_round_rect(x+101,y+DH-95,DW-122,28,5,0xFFFFFF);
    ui_text(nf,x+110,y+DH-96+4,name,nlen,TXT_DARK); { int cw=ui_width(nf,name,nlen); gfx_rect(x+110+cw,y+DH-96+6,1,18,ACCENT); }
    int by=y+DH-52; const char *go=saving? "Save" : "Open";
    gfx_round_rect(x+DW-232,by,100,34,7,ACCENT); ui_text(bf,x+DW-232+50-ui_width(bf,go,(int)strlen(go))/2,by+8,go,(int)strlen(go),0xFFFFFF);
    gfx_round_rect(x+DW-120,by,100,34,7,0xD1D5DB); ui_text(bf,x+DW-120+50-ui_width(bf,"Cancel",6)/2,by+8,"Cancel",6,TXT_DARK);
    int accept=0, cancel=0, vk, ch, redraw=0;
    for(int spin=0; spin<10000 && !accept && !cancel && !redraw && !need; spin++){
      while(kb_event(&vk,&ch)){
        redraw=1;
        if(vk==0x1B) cancel=1;
        else if(ch==13){ if(sel>=0){ int isup=up&&sel==0; struct ramfile *f=isup?0:ents[sel-(up?1:0)]; if(isup||f->isdir) goto enter_sel; } accept=1; }
        else if(ch==8){ if(nlen) name[--nlen]=0; }
        else if(ch>=32 && ch<127 && nlen<62){ name[nlen++]=ch; name[nlen]=0; sel=-1; }
        else if(vk==0x26 && rows){ sel= sel<=0? 0 : sel-1; goto picked; }
        else if(vk==0x28 && rows){ sel= sel+1<rows? sel+1 : rows-1; goto picked; }
        continue;
        picked:
        if(sel<scroll) scroll=sel; if(sel>=scroll+vis) scroll=sel-vis+1;
        { int isup=up&&sel==0; struct ramfile *f=isup?0:ents[sel-(up?1:0)]; if(f && !f->isdir){ cpy(name,fs_leaf(f),64); nlen=strlen(name); } }
        continue;
        enter_sel: { int isup=up&&sel==0; if(isup){ int n=strlen(dir); while(n>0&&dir[n-1]!='/') n--; if(n>0) n--; dir[n]=0; } else { struct ramfile *f=ents[sel-(up?1:0)]; cpy(dir,f->name,128); } need=1; }
      }
      int b=mouse_buttons&1, mx=mouse_x, my=mouse_y;
      if(prevb && !b){
        if(my>=by&&my<by+34&&mx>=x+DW-232&&mx<x+DW-132) accept=1;
        else if(my>=by&&my<by+34&&mx>=x+DW-120&&mx<x+DW-20) cancel=1;
        else if(mx>=x+22&&mx<x+DW-22&&my>=y+92&&my<y+92+vis*RH){
          int ri=(my-y-92)/RH+scroll; if(ri<rows){
            sel=ri; int isup=up&&ri==0; struct ramfile *f=isup?0:ents[ri-(up?1:0)];
            if(f && !f->isdir){ cpy(name,fs_leaf(f),64); nlen=strlen(name); }
            if(ri==lastsel && ticks-lastclick<500){
              if(isup){ int n=strlen(dir); while(n>0&&dir[n-1]!='/') n--; if(n>0) n--; dir[n]=0; need=1; }
              else if(f->isdir){ cpy(dir,f->name,128); need=1; } else accept=1;
            }
            lastsel=ri; lastclick=ticks;
          }
        }
        redraw=1;
      }
      prevb=b; if(!accept&&!cancel&&!redraw&&!need) __asm__ volatile("hlt");
    }
    if(accept && nlen){
      char full[200]; int fn=0; full[fn++]='C'; full[fn++]=':'; full[fn++]='\\'; for(const char *q=dir;*q&&fn<150;q++) full[fn++]=*q=='/'? '\\' : *q; if(dir[0]) full[fn++]='\\'; for(int k=0;k<nlen&&fn<198;k++) full[fn++]=name[k]; full[fn]=0;
      struct ramfile *f=fs_find(full);
      if((f && !f->isdir) || (saving && !f)){ if(out && maxn>(u32)fn) memcpy(out,full,fn+1); result=1; break; }
    }
    if(cancel) break;
  }
  for(int j=0;j<DH;j++) for(int i=0;i<DW;i++) gfx_pixel(x+i,y+j,dsave[j*DW+i]);
  return result;
}
static i32 WINAPI cd_GetOpenFileNameA(u8 *ofn){ return file_dialog(ofn,0); }
static i32 WINAPI cd_GetSaveFileNameA(u8 *ofn){ return file_dialog(ofn,1); }

#define U(n) {"user32", #n, w_##n}
#include "dialog2.inc"
#include "gdi.inc"
#include "gdi2.inc"
const struct export wm_exports[]={
  GDI_EXPORTS
  GDI2_EXPORTS
  DLG_EXPORTS
  {"gdi32","StretchDIBits",w_StretchDIBits},
  U(RegisterClassA), {"user32","RegisterClassExA",w_RegisterClassA}, U(CreateWindowExA), U(DefWindowProcA), U(DestroyWindow),
  U(ShowWindow), U(MoveWindow), U(GetClientRect), U(GetWindowRect), U(GetParent), U(IsWindow), U(SetWindowTextA),
  U(GetWindowTextA), U(GetWindowTextLengthA), U(GetWindowLongPtrA), U(SetWindowLongPtrA), U(CallWindowProcA),
  U(PostThreadMessageA), {"user32","PostThreadMessageW",w_PostThreadMessageA},
  {"user32","GetWindowLongPtrW",w_GetWindowLongPtrA}, {"user32","SetWindowLongPtrW",w_SetWindowLongPtrA}, U(GetWindowLongA), U(SetWindowLongA),
  {"user32","GetWindowLongW",w_GetWindowLongA}, {"user32","SetWindowLongW",w_SetWindowLongA}, U(GetAncestor),
  U(SendMessageA), U(PostMessageA), U(PostQuitMessage), U(LoadCursorA), {"user32","LoadIconA",w_LoadCursorA}, {"user32","LoadCursorW",w_LoadCursorA},
  U(SetFocus), U(GetFocus), U(InvalidateRect), U(UpdateWindow), U(BeginPaint), U(EndPaint), U(SetCursor), U(GetMessageA), U(PeekMessageA), U(TranslateMessage), U(DispatchMessageA),
  {"comdlg32","GetOpenFileNameA",cd_GetOpenFileNameA}, {"comdlg32","GetSaveFileNameA",cd_GetSaveFileNameA},
};
const unsigned wm_nexports=sizeof wm_exports/sizeof *wm_exports;
