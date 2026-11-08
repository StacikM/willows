#include "kernel.h"
#include "font.h"
static int WINAPI fmt_(char *b, u32 n, const char *f, ...){ margs ap; __builtin_ms_va_start(ap,f); int r=kvsnprintf(b,n,f,ap); __builtin_ms_va_end(ap); return r; }
#define snprintf_ fmt_
/* The desktop: the contents of the "Desktop" folder shown as icons. Select, double-click to open, right-click menus,
   rename in place, delete (with confirmation), new folder / new text document. */
#define ICON_W 96
#define ICON_H 104
#define MAXI 120
struct dicon { char name[64]; char path[128]; u8 isdir; };
static struct dicon ic[MAXI]; static int nic, dsel=-1, dhov=-1, scan_gen=-1;
static int desk_focus, ren, ren_idx, renlen, ren_fresh; static char renbuf[64];
static int ctx, ctx_x, ctx_y, ctx_hover=-1, ctx_icon=-1, ctx_n; static const char *ctx_lab[6]; static int ctx_act[6];
static u32 click_t; static int click_i=-1;
enum { A_NEWDIR=1, A_NEWTXT, A_TERM, A_REFRESH, A_OPEN, A_RENAME, A_DELETE };
extern void wm_dirty(void); extern void wm_app_icon(int x,int y,int sz,const char *name); extern void wm_shadow(int x,int y,int w,int h);
extern int win32_messagebox(const char *text, const char *caption, unsigned type);
extern void fs_flush(void);
static const struct font *ui(int style,int px){ return font_find(style,px); }
static int ends(const char *s,const char *e){ int n=(int)strlen(s), m=(int)strlen(e); return n>m && !stricmp(s+n-m,e); }
static int tb_top(void){ return (int)gfx_h-60; }
static int per_col(void){ int r=(tb_top()-24)/ICON_H; return r<1? 1 : r; }
static void cell_pos(int i,int *x,int *y){ int r=per_col(); *x=14+(i/r)*ICON_W; *y=14+(i%r)*ICON_H; }
static int icon_at(int mx,int my){
  for(int i=0;i<nic;i++){ int x,y; cell_pos(i,&x,&y); if(mx>=x+4&&mx<x+ICON_W-4&&my>=y+2&&my<y+ICON_H-4) return i; }
  return -1;
}
static int name_less(const char *a,const char *b){ return stricmp(a,b)<0; }
static void scan(void){
  struct ramfile *l[MAXI]; int n=fs_list("/Desktop",l,MAXI); char keep[64]; keep[0]=0;
  if(dsel>=0&&dsel<nic){ unsigned k=0; for(;ic[dsel].name[k]&&k<63;k++) keep[k]=ic[dsel].name[k]; keep[k]=0; }
  int ord[MAXI]; for(int i=0;i<n;i++) ord[i]=i;
  for(int i=1;i<n;i++){ int v=ord[i], j=i-1;                       /* folders first, then by name */
    while(j>=0 && ((l[ord[j]]->isdir<l[v]->isdir) || (l[ord[j]]->isdir==l[v]->isdir && name_less(fs_leaf(l[v]),fs_leaf(l[ord[j]]))))){ ord[j+1]=ord[j]; j--; }
    ord[j+1]=v; }
  nic=0; dsel=-1;
  for(int i=0;i<n && nic<MAXI;i++){
    struct ramfile *f=l[ord[i]]; struct dicon *d=&ic[nic]; const char *lf=fs_leaf(f); unsigned k=0;
    for(;lf[k]&&k<63;k++) d->name[k]=lf[k]; d->name[k]=0; d->isdir=f->isdir;
    k=0; const char *pre="/Desktop/"; for(;pre[k];k++) d->path[k]=pre[k]; for(unsigned j=0;lf[j]&&k<127;j++) d->path[k++]=lf[j]; d->path[k]=0;
    if(keep[0] && !stricmp(keep,d->name)) dsel=nic;
    nic++;
  }
  scan_gen=fs_gen;
}
static u32 ext_color(const char *n){ const char *e=n; for(const char *p=n;*p;p++) if(*p=='.') e=p+1; u32 h=5381; for(;*e;e++) h=h*33+(u8)*e; static const u32 pal[6]={0x3B82F6,0x10B981,0xF59E0B,0xEF4444,0x8B5CF6,0x06B6D4}; return pal[h%6]; }
static void icon_graphic(int x,int y,const struct dicon *d){
  if(d->isdir){
    gfx_round_rect(x+2,y+12,52,38,7,0xD9922A); gfx_round_rect(x+2,y+5,24,16,5,0xD9922A);
    gfx_round_rect(x+2,y+18,52,32,7,0xFFC857); gfx_rect_alpha(x+8,y+21,40,1,0xFFFFFF,90);
  } else if(ends(d->name,".exe")){
    wm_app_icon(x+4,y+4,48,d->name);
  } else {
    gfx_round_rect(x+9,y+3,38,50,5,0xC9CED6); gfx_round_rect(x+10,y+4,36,48,4,0xF8F9FB);
    for(int k=0;k<4;k++) gfx_rect(x+16,y+11+k*6,24-(k==3?10:0),2,0xC3C9D2);
    const char *e=d->name; for(const char *p=d->name;*p;p++) if(*p=='.') e=p+1; char tag[6]; int n=0; for(;e[n]&&n<4&&e!=d->name;n++) tag[n]=(e[n]>='a'&&e[n]<='z')? e[n]-32 : e[n]; tag[n]=0;
    if(n){ const struct font *tf=ui(FS_BOLD,10); int w=ui_width(tf,tag,n)+10; gfx_round_rect(x+28-w/2,y+38,w,15,4,ext_color(d->name)); ui_text(tf,x+28-w/2+5,y+38+(15-ui_lineh(tf))/2,tag,n,0xFFFFFF); }
  }
}
void desktop_draw(void){
  if(scan_gen!=fs_gen) scan();
  const struct font *f=ui(FS_REGULAR,12);
  for(int i=0;i<nic;i++){
    int x,y; cell_pos(i,&x,&y); struct dicon *d=&ic[i];
    if(i==dsel){ gfx_round_rect(x+4,y+2,ICON_W-8,ICON_H-6,10,0x4C7BC4); gfx_round_rect(x+5,y+3,ICON_W-10,ICON_H-8,9,0x2D4F8A); }
    else if(i==dhov) gfx_round_rect(x+4,y+2,ICON_W-8,ICON_H-6,10,0x2A4372);
    icon_graphic(x+(ICON_W-56)/2,y+8,d);
    if(ren && ren_idx==i){                                         /* in-place rename box */
      int bw=ICON_W-6; gfx_round_rect(x+3,y+68,bw,22,5,0x4C7BC4); gfx_round_rect(x+4,y+69,bw-2,20,4,0xFFFFFF);
      int shown=renlen; while(shown>0 && ui_width(f,renbuf+(renlen-shown),shown)>bw-14) shown--;
      if(ren_fresh && shown>0) gfx_rect(x+9,y+72,ui_width(f,renbuf+(renlen-shown),shown),15,0xBFD6FA);
      ui_text(f,x+9,y+69+(20-ui_lineh(f))/2,renbuf+(renlen-shown),shown,0x111827); int cw=ui_width(f,renbuf+(renlen-shown),shown); gfx_rect(x+9+cw,y+72,1,14,0x2563EB);
      continue;
    }
    char lab[70]; int n=(int)strlen(d->name); memcpy(lab,d->name,n+1);
    int maxw=ICON_W-10; if(ui_width(f,lab,n)>maxw){ while(n>0 && ui_width(f,lab,n)+ui_width(f,"..",2)>maxw) n--; lab[n++]='.'; lab[n++]='.'; lab[n]=0; }
    int tw=ui_width(f,lab,n), tx=x+(ICON_W-tw)/2, ty=y+70;
    ui_text(f,tx+1,ty+1,lab,n,0x0A1226); ui_text(f,tx,ty,lab,n,0xFFFFFF);
  }
}
static void open_icon(int i){ if(i>=0&&i<nic) shell_open(ic[i].path); }
static void unique_name(const char *base,const char *ext,char *out){
  for(int k=1;k<100;k++){ char t[100]; if(k==1) snprintf_(t,sizeof t,"/Desktop/%s%s",base,ext); else snprintf_(t,sizeof t,"/Desktop/%s (%d)%s",base,k,ext); if(!fs_find(t)){ unsigned j=0; for(;t[j]&&j<127;j++) out[j]=t[j]; out[j]=0; return; } }
  out[0]=0;
}
static void start_rename(int i){ if(i<0||i>=nic) return; ren=1; ren_idx=i; unsigned k=0; for(;ic[i].name[k]&&k<63;k++) renbuf[k]=ic[i].name[k]; renbuf[k]=0; renlen=(int)k; ren_fresh=1; dsel=i; desk_focus=1; wm_dirty(); }          /* all selected: typing replaces the name */
static void commit_rename(void){
  if(!ren) return; ren=0; if(ren_idx<0||ren_idx>=nic||!renlen||!stricmp(renbuf,ic[ren_idx].name)){ wm_dirty(); return; }
  char np[128]; snprintf_(np,sizeof np,"/Desktop/%s",renbuf);
  char keep[64]; unsigned k=0; for(;renbuf[k]&&k<63;k++) keep[k]=renbuf[k]; keep[k]=0;
  if(fs_rename(ic[ren_idx].path,np)==0){ scan(); for(int i=0;i<nic;i++) if(!stricmp(ic[i].name,keep)) dsel=i; }
  wm_dirty();
}
static void do_action(int act){
  char p[128];
  switch(act){
    case A_NEWDIR: unique_name("New folder","",p); if(p[0] && fs_mkdir(p)==0){ scan(); for(int i=0;i<nic;i++){ char q[128]; snprintf_(q,sizeof q,"/Desktop/%s",ic[i].name); if(!stricmp(q,p)){ start_rename(i); break; } } } break;
    case A_NEWTXT: unique_name("New Text Document",".txt",p); if(p[0]){ struct ramfile *f=fs_create(p); if(f){ fs_flush(); scan(); for(int i=0;i<nic;i++){ char q[128]; snprintf_(q,sizeof q,"/Desktop/%s",ic[i].name); if(!stricmp(q,p)){ start_rename(i); break; } } } } break;
    case A_TERM: proc_spawn_cwd("terminal","terminal /Desktop","/Desktop"); break;
    case A_REFRESH: scan(); break;
    case A_OPEN: open_icon(ctx_icon>=0? ctx_icon : dsel); break;
    case A_RENAME: start_rename(ctx_icon>=0? ctx_icon : dsel); break;
    case A_DELETE: { int i=ctx_icon>=0? ctx_icon : dsel; if(i<0||i>=nic) break; char t[160]; snprintf_(t,sizeof t,"Delete \"%s\"?\nThis cannot be undone.",ic[i].name);
      char path[128]; unsigned k=0; for(;ic[i].path[k];k++) path[k]=ic[i].path[k]; path[k]=0;
      if(win32_messagebox(t,"Delete",4)==6){ fs_remove_tree(path); scan(); dsel=-1; } break; }
  }
  wm_dirty();
}
static void ctx_setup(int icon){
  ctx_icon=icon; ctx_n=0;
  if(icon>=0){ ctx_lab[ctx_n]="Open"; ctx_act[ctx_n++]=A_OPEN; ctx_lab[ctx_n]="Rename"; ctx_act[ctx_n++]=A_RENAME; ctx_lab[ctx_n]=0; ctx_act[ctx_n++]=0; ctx_lab[ctx_n]="Delete"; ctx_act[ctx_n++]=A_DELETE; }
  else { ctx_lab[ctx_n]="New folder"; ctx_act[ctx_n++]=A_NEWDIR; ctx_lab[ctx_n]="New text document"; ctx_act[ctx_n++]=A_NEWTXT; ctx_lab[ctx_n]=0; ctx_act[ctx_n++]=0;
         ctx_lab[ctx_n]="Open terminal here"; ctx_act[ctx_n++]=A_TERM; ctx_lab[ctx_n]="Refresh"; ctx_act[ctx_n++]=A_REFRESH; }
}
#define CW_ 220
static int ctx_h(void){ int h=12; for(int i=0;i<ctx_n;i++) h+= ctx_lab[i]? 32 : 9; return h; }
static int ctx_item_at(int mx,int my){
  if(!ctx||mx<ctx_x||mx>=ctx_x+CW_||my<ctx_y||my>=ctx_y+ctx_h()) return -1;
  int y=ctx_y+6; for(int i=0;i<ctx_n;i++){ int h=ctx_lab[i]? 32 : 9; if(my>=y&&my<y+h) return ctx_lab[i]? i : -1; y+=h; } return -1;
}
void desktop_ctx_draw(void){
  if(!ctx) return; int h=ctx_h(); wm_shadow(ctx_x,ctx_y,CW_,h);
  gfx_round_rect(ctx_x,ctx_y,CW_,h,8,0x394152); gfx_round_rect(ctx_x+1,ctx_y+1,CW_-2,h-2,7,0x1F2430);
  const struct font *f=ui(FS_REGULAR,14); int y=ctx_y+6;
  for(int i=0;i<ctx_n;i++){
    if(!ctx_lab[i]){ gfx_rect(ctx_x+12,y+4,CW_-24,1,0x394152); y+=9; continue; }
    if(i==ctx_hover) gfx_round_rect(ctx_x+6,y,CW_-12,30,6,0x323B4F);
    ui_text(f,ctx_x+18,y+(30-ui_lineh(f))/2,ctx_lab[i],(int)strlen(ctx_lab[i]),ctx_act[i]==A_DELETE? 0xF87171 : 0xF3F4F6); y+=32;
  }
}
int desktop_ctx_click(int mx,int my){                               /* returns 1 if the click was consumed */
  if(!ctx) return 0; int it=ctx_item_at(mx,my); ctx=0; wm_dirty();
  if(it>=0) do_action(ctx_act[it]);
  return 1;
}
int desktop_ctx_open(void){ return ctx; }
void desktop_hover(int mx,int my){
  int nh=-1, nc=-1; if(ctx) nc=ctx_item_at(mx,my); else nh=icon_at(mx,my);
  if(nh!=dhov||nc!=ctx_hover){ dhov=nh; ctx_hover=nc; wm_dirty(); }
}
void desktop_click(int mx,int my){
  if(ren) commit_rename(); ctx=0; desk_focus=1; int i=icon_at(mx,my);
  if(i>=0){ dsel=i; if(i==click_i && (u32)(ticks-click_t)<700){ click_i=-1; open_icon(i); } else { click_i=i; click_t=ticks; } }
  else { dsel=-1; click_i=-1; }
  wm_dirty();
}
void desktop_rclick(int mx,int my){
  if(ren) commit_rename(); desk_focus=1; int i=icon_at(mx,my); dsel=i; ctx_setup(i);
  ctx_x=mx; ctx_y=my; int h=ctx_h(); if(ctx_x+CW_>(int)gfx_w-8) ctx_x=gfx_w-CW_-8; if(ctx_y+h>tb_top()) ctx_y=tb_top()-h-4; if(ctx_y<4) ctx_y=4;
  ctx=1; ctx_hover=-1; wm_dirty();
}
void desktop_defocus(void){ desk_focus=0; if(ren){ ren=0; } ctx=0; wm_dirty(); }
int desktop_has_focus(void){ return desk_focus; }
int desktop_key(int vk,int ch){                                      /* returns 1 if consumed */
  if(ren){
    if(ch==13) commit_rename();
    else if(ch==27||vk==0x1B){ ren=0; wm_dirty(); }
    else if(ch==8){ if(ren_fresh){ renlen=0; renbuf[0]=0; ren_fresh=0; } else if(renlen) renbuf[--renlen]=0; wm_dirty(); }
    else if(ch>=32 && ch<127 && (ren_fresh? (renlen=0,renbuf[0]=0,ren_fresh=0,1) : 1) && renlen<60 && !(ch=='/'||ch=='\\'||ch==':'||ch=='*'||ch=='?'||ch=='"'||ch=='<'||ch=='>'||ch=='|')){ renbuf[renlen++]=(char)ch; renbuf[renlen]=0; wm_dirty(); }
    return 1;
  }
  if(ctx){ if(vk==0x1B||ch==27){ ctx=0; wm_dirty(); return 1; } }
  if(!desk_focus) return 0;
  if(vk==0x2E){ ctx_icon=dsel; do_action(A_DELETE); return 1; }
  if(ch==13){ open_icon(dsel); return 1; }
  if(vk==0x25||vk==0x26||vk==0x27||vk==0x28){
    int r=per_col(); int i=dsel<0? 0 : dsel; if(dsel>=0){ if(vk==0x26&&i%r) i--; else if(vk==0x28&&i%r<r-1&&i+1<nic) i++; else if(vk==0x25&&i>=r) i-=r; else if(vk==0x27&&i+r<nic) i+=r; }
    if(nic) dsel=i; wm_dirty(); return 1;
  }
  return 0;
}
