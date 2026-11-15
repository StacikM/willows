#include "kernel.h"
#include "font.h"
extern const unsigned char font8x16[128][16];
int gfx_ok; u32 gfx_w, gfx_h; static u8 *fb; static u32 pitch;
static u8 *real_fb, *bb;

/* ---- software mouse cursor: saves the pixels under it, and hides itself while anything draws over it ---- */
static inline u32 blend(u32 d, u32 sc, int a);
#define CW 26
#define CH 32
static int cur_en, cur_vis, cur_on=1, cur_x, cur_y, mx, my, busy, hot_x=1, hot_y=1, cur_kind;
static u32 under[CW*CH];
static u32 curbmp[2][CW*CH];                      /* ARGB: vector cursors rendered once with 4x4 supersampling */
static int cursors_ready;

/* shapes are defined in 1/8 pixel units */
static int in_rr(int x,int y,int rx,int ry,int rw,int rh,int rr){            /* rounded rect, all in 1/8 px */
  int cx=rx+rw/2, cy=ry+rh/2, dx=x>cx? x-cx : cx-x, dy=y>cy? y-cy : cy-y; dx-=rw/2-rr; dy-=rh/2-rr; if(dx<0) dx=0; if(dy<0) dy=0; return dx*dx+dy*dy<=rr*rr;
}
static int in_shape(int kind,int x,int y){
  if(kind==0){                                                               /* arrow: even-odd polygon */
    static const int P[7][2]={{8,8},{8,160},{42,128},{64,172},{85,163},{63,112},{108,112}};
    int in=0; for(int i=0,j=6;i<7;j=i++){ int xi=P[i][0],yi=P[i][1],xj=P[j][0],yj=P[j][1];
      if((yi>y)!=(yj>y) && x < (xj-xi)*(y-yi)/(yj-yi)+xi) in=!in; }
    return in;
  }
  return in_rr(x,y,48,8,28,90,14) || in_rr(x,y,24,72,104,80,24) || in_rr(x,y,75,64,26,52,13) || in_rr(x,y,99,72,26,52,13) || in_rr(x,y,121,84,24,42,12) || in_rr(x,y,22,92,40,48,18);   /* pointing hand */
}
static int shape_cov(int kind,int ox,int oy,int px,int py,int dilate){      /* coverage 0..16 of pixel (px,py) of the shape shifted by (ox,oy) 1/8px */
  int n=0;
  for(int a=0;a<4;a++) for(int b=0;b<4;b++){
    int sx=px*8+a*2+1-ox, sy=py*8+b*2+1-oy, hit=0;
    if(!dilate) hit=in_shape(kind,sx,sy);
    else for(int d=0; d<8 && !hit; d++){ static const int D[8][2]={{8,0},{-8,0},{0,8},{0,-8},{6,6},{-6,6},{6,-6},{-6,-6}}; hit=in_shape(kind,sx+D[d][0],sy+D[d][1]); }
    n+=hit;
  }
  return n;
}
static void over(u32 *p,u32 c,int a){                                        /* composite colour c with alpha a over ARGB *p */
  int da=*p>>24; if(!a) return; int ra=a+da*(255-a)/255; if(!ra) return;
  u32 dr=(*p>>16)&255, dg=(*p>>8)&255, db=*p&255, sr=(c>>16)&255, sg=(c>>8)&255, sb=c&255;
  u32 r=(sr*a+dr*da*(255-a)/255)/ra, g=(sg*a+dg*da*(255-a)/255)/ra, b=(sb*a+db*da*(255-a)/255)/ra;
  *p=((u32)ra<<24)|(r<<16)|(g<<8)|b;
}
static void make_cursors(void){
  if(cursors_ready) return;
  for(int k=0;k<2;k++) for(int j=0;j<CH;j++) for(int i=0;i<CW;i++){
    u32 *p=&curbmp[k][j*CW+i]; *p=0;
    over(p,0x000000,shape_cov(k,12,20,i,j,1)*70/16);                         /* soft shadow */
    over(p,0x000000,shape_cov(k,0,0,i,j,1)*255/16);                          /* outline */
    over(p,0xFFFFFF,shape_cov(k,0,0,i,j,0)*255/16);                          /* fill */
  }
  cursors_ready=1;
}
static void rawput(int x,int y,u32 c){ if((u32)x<gfx_w && (u32)y<gfx_h) *(u32*)(fb+(u64)y*pitch+(u64)x*4)=c; }
static u32  rawget(int x,int y){ return ((u32)x<gfx_w && (u32)y<gfx_h)? *(u32*)(fb+(u64)y*pitch+(u64)x*4) : 0; }
static void cur_hide(void){
  if(!cur_vis) return;
  for(int j=0;j<CH;j++) for(int i=0;i<CW;i++) rawput(cur_x+i,cur_y+j,under[j*CW+i]);
  cur_vis=0;
}
static void cur_show(void){
  if(!cur_en||!cur_on||cur_vis) return;
  cur_x=mx-hot_x; cur_y=my-hot_y;
  for(int j=0;j<CH;j++) for(int i=0;i<CW;i++) under[j*CW+i]=rawget(cur_x+i,cur_y+j);
  const u32 *bmp=curbmp[cur_kind];
  for(int j=0;j<CH;j++) for(int i=0;i<CW;i++){
    u32 c=bmp[j*CW+i]; int a=c>>24; if(!a) continue;
    if((u32)(cur_x+i)>=gfx_w||(u32)(cur_y+j)>=gfx_h) continue;
    rawput(cur_x+i,cur_y+j, a==255? (c&0xFFFFFF) : blend(under[j*CW+i],c&0xFFFFFF,a));
  }
  cur_vis=1;
}
static void begin(int x,int y,int w,int h){
  busy++;
  if(cur_vis && x<cur_x+CW && x+w>cur_x && y<cur_y+CH && y+h>cur_y) cur_hide();
}
static void end(void){
  if(--busy==0 && cur_en){ if(cur_vis && (cur_x!=mx-hot_x||cur_y!=my-hot_y)) cur_hide(); cur_show(); }
}
void gfx_cursor_shape(int kind){                                      /* 0 arrow, 1 hand */
  kind= kind==1? 1 : 0; if(kind==cur_kind) return;
  if(cur_en && !busy) cur_hide();
  cur_kind=kind; hot_x= kind? 8 : 1; hot_y=1;
  if(cur_en && !busy) cur_show();
}
void gfx_cursor_enable(int x,int y){ if(!gfx_ok) return; make_cursors(); mx=x; my=y; cur_en=1; cur_show(); }
void gfx_cursor_move(int x,int y){ mx=x; my=y; if(!cur_en||busy) return; cur_hide(); cur_show(); }
void gfx_cursor_show(int on){ cur_on=on; if(!on) cur_hide(); else if(!busy) cur_show(); }

int gfx_init(struct mb_info *m){
  if(!(m->flags&(1<<12)) || m->fb_type!=1 || m->fb_bpp!=32 || m->fb_addr_hi) return 0;
  fb=(u8*)(u64)m->fb_addr_lo; pitch=m->fb_pitch; gfx_w=m->fb_w; gfx_h=m->fb_h; gfx_ok=1;
  if((u64)fb>=0xC0000000ull) mm_set_wc((u64)fb,(u64)pitch*gfx_h);          /* video memory: write-combining instead of uncached */
  return 1;
}
void gfx_pixel(int x,int y,u32 c){ begin(x,y,1,1); rawput(x,y,c); end(); }
u32 gfx_get(int x,int y){
  if(cur_vis && x>=cur_x && x<cur_x+CW && y>=cur_y && y<cur_y+CH) return under[(y-cur_y)*CW+(x-cur_x)];
  return rawget(x,y);
}
void gfx_rect(int x,int y,int w,int h,u32 c){
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; }
  if(x+w>(int)gfx_w) w=gfx_w-x; if(y+h>(int)gfx_h) h=gfx_h-y;
  if(w<=0||h<=0) return;
  begin(x,y,w,h);
  for(int j=0;j<h;j++){ u32 *p=(u32*)(fb+(u64)(y+j)*pitch)+x; for(int i=0;i<w;i++) p[i]=c; }
  end();
}
static void glyph(int x,int y,char ch,u32 fg,u32 bg,int opaque){
  const unsigned char *g=font8x16[(u8)ch&0x7F];
  for(int j=0;j<16;j++) for(int i=0;i<8;i++){
    if(g[j]&(0x80>>i)) rawput(x+i,y+j,fg); else if(opaque) rawput(x+i,y+j,bg);
  }
}
void gfx_char(int x,int y,char ch,u32 fg,u32 bg,int opaque){ begin(x,y,8,16); glyph(x,y,ch,fg,bg,opaque); end(); }
void gfx_text(int x,int y,const char *s,int n,u32 fg,u32 bg,int opaque){
  int len=0; while(len<n&&s[len]) len++;
  begin(x,y,len*8,16); for(int i=0;i<len;i++) glyph(x+i*8,y,s[i],fg,bg,opaque); end();
}
void gfx_scroll_up(int px,u32 bg){
  begin(0,0,gfx_w,gfx_h);
  memmove(fb,fb+(u64)px*pitch,(u64)(gfx_h-px)*pitch);
  for(u32 j=gfx_h-px;j<gfx_h;j++){ u32 *p=(u32*)(fb+(u64)j*pitch); for(u32 i=0;i<gfx_w;i++) p[i]=bg; }
  end();
}

/* Draw a whole frame off-screen, then copy it to the display in one go (no flicker). */
static void cur_hide_x(void){ cur_hide(); }
int gfx_offscreen_begin(void){
  if(!bb) bb=kmalloc((u64)pitch*gfx_h,4096);
  if(!bb) return 0;
  cur_hide_x(); busy++; real_fb=fb; fb=bb; return 1;
}
void gfx_present(void){
  if(!bb||fb!=bb) return;
  fb=real_fb; { u64 q=((u64)pitch*gfx_h)/8; void *d=real_fb; const void *sp=bb; __asm__ volatile("rep movsq":"+D"(d),"+S"(sp),"+c"(q)::"memory"); }
  busy--; if(!busy && cur_en) cur_show();
}

void gfx_blit(int x,int y,int w,int h,const u32 *src){
  begin(x,y,w,h);
  for(int j=0;j<h;j++){ int yy=y+j; if(yy<0||yy>=(int)gfx_h) continue;
    int x0=x<0?0:x, x1=x+w>(int)gfx_w?(int)gfx_w:x+w; if(x1>x0) memcpy(fb+(u64)yy*pitch+(u64)x0*4,src+(u64)j*w+(x0-x),(u64)(x1-x0)*4); }
  end();
}

/* ---- text ---- */
static int utf8(const char *s, int n, u32 *cp){                    /* decode one code point; returns bytes used */
  u8 c=(u8)s[0];
  if(c<0x80){ *cp=c; return 1; }
  if((c&0xE0)==0xC0 && n>=2){ *cp=((c&0x1F)<<6)|(s[1]&0x3F); return 2; }
  if((c&0xF0)==0xE0 && n>=3){ *cp=((c&0x0F)<<12)|((s[1]&0x3F)<<6)|(s[2]&0x3F); return 3; }
  if((c&0xF8)==0xF0 && n>=4){ *cp=0xFFFD; return 4; }
  *cp=c; return 1;                                                    /* Latin-1 byte or stray byte */
}
static const struct fglyph *glyph_of(const struct font *f, u32 cp){
  int lo=0, hi=f->nglyph-1;
  while(lo<=hi){ int mid=(lo+hi)/2; u32 c=f->g[mid].cp; if(c==cp) return &f->g[mid]; if(c<cp) lo=mid+1; else hi=mid-1; }
  if(cp!='?') return glyph_of(f,'?');
  return NULL;
}
int ui_lineh(const struct font *f){ return f->lineh; }
int ui_ascent(const struct font *f){ return f->ascent; }
int ui_width(const struct font *f, const char *s, int n){
  if(f->nglyph==0){ int w=0; for(int i=0;i<n&&s[i];i++){ u32 cp; i+=utf8(s+i,n-i,&cp)-1; w+=8*f->scale; } return w+(f->bold? 1:0); }
  int x16=0;
  for(int i=0;i<n&&s[i];){ u32 cp; i+=utf8(s+i,n-i,&cp); const struct fglyph *g=glyph_of(f,cp); if(g) x16+=g->adv16; }
  return (x16+8)>>4;
}
static inline u32 blend(u32 d, u32 sc, int a){                      /* a: 0..255 */
  u32 dr=(d>>16)&255, dg=(d>>8)&255, db=d&255, sr=(sc>>16)&255, sg=(sc>>8)&255, sb=sc&255;
  return (((sr*a+dr*(255-a))/255)<<16)|(((sg*a+dg*(255-a))/255)<<8)|((sb*a+db*(255-a))/255);
}
void ui_text_to(const struct font *f, u32 *dst, int stride, int dw, int dh, int x, int y, const char *s, int n, u32 fg){
  if(f->nglyph==0){                                                   /* scaled 8x16 bitmap font */
    int sc=f->scale, pen=x;
    for(int i=0;i<n&&s[i];){
      u32 cp; i+=utf8(s+i,n-i,&cp); if(cp>127||cp<32) cp= cp==0xA0? ' ' : '?';
      const unsigned char *g=font8x16[cp];
      for(int pass=0; pass<=f->bold; pass++)
        for(int j=0;j<16;j++) for(int b=0;b<8;b++) if(g[j]&(0x80>>b))
          for(int dy=0;dy<sc;dy++) for(int dx=0;dx<sc;dx++){
            int px=pen+b*sc+dx+pass, py=y+j*sc+dy+(sc==1?1:0);
            if((u32)px<(u32)dw && (u32)py<(u32)dh) dst[(u64)py*stride+px]=fg;
          }
      pen+=8*sc;
    }
    return;
  }
  int x16=x*16;
  for(int i=0;i<n&&s[i];){
    u32 cp; i+=utf8(s+i,n-i,&cp); const struct fglyph *g=glyph_of(f,cp); if(!g) continue;
    int gx=(x16+8)>>4, gy=y+f->ascent;
    for(int j=0;j<g->h;j++) for(int k=0;k<g->w;k++){
      int a=f->bits[g->off+j*g->w+k]; if(!a) continue;
      int px=gx+g->xoff+k, py=gy+g->yoff+j; if((u32)px>=(u32)dw||(u32)py>=(u32)dh) continue;
      u32 *d=&dst[(u64)py*stride+px]; *d= a==255? fg : blend(*d,fg,a);
    }
    x16+=g->adv16;
  }
}
void ui_text(const struct font *f, int x, int y, const char *s, int n, u32 fg){
  int w=ui_width(f,s,n); if(w<=0) return;
  begin(x,y,w+2,f->lineh+2);
  ui_text_to(f,(u32*)fb,pitch/4,gfx_w,gfx_h,x,y,s,n,fg);
  end();
}

/* ---- shapes ---- */
void gfx_rect_alpha(int x,int y,int w,int h,u32 c,int a){
  if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; } if(x+w>(int)gfx_w) w=gfx_w-x; if(y+h>(int)gfx_h) h=gfx_h-y; if(w<=0||h<=0) return;
  begin(x,y,w,h);
  for(int j=0;j<h;j++){ u32 *p=(u32*)(fb+(u64)(y+j)*pitch)+x; for(int i=0;i<w;i++) p[i]=blend(p[i],c,a); }
  end();
}
void gfx_gradient(int x,int y,int w,int h,u32 c0,u32 c1,int horiz){
  if(w<=0||h<=0) return;
  int steps=horiz? w : h;
  for(int k=0;k<steps;k++){
    int r=(int)((c0>>16)&255)+((int)((c1>>16)&255)-(int)((c0>>16)&255))*k/(steps>1?steps-1:1);
    int g=(int)((c0>>8)&255)+((int)((c1>>8)&255)-(int)((c0>>8)&255))*k/(steps>1?steps-1:1);
    int b=(int)(c0&255)+((int)(c1&255)-(int)(c0&255))*k/(steps>1?steps-1:1);
    u32 c=((u32)r<<16)|((u32)g<<8)|(u32)b;
    if(horiz) gfx_rect(x+k,y,1,h,c); else gfx_rect(x,y+k,w,1,c);
  }
}
static int isqrt(int v){ int r=0; while((r+1)*(r+1)<=v) r++; return r; }
static int isqrt64(u64 v){ u64 r=0, b=1ull<<62; while(b>v) b>>=2; while(b){ if(v>=r+b){ v-=r+b; r=(r>>1)+b; } else r>>=1; b>>=2; } return (int)r; }
/* coverage (0..255) of pixel (i,j) of an r x r corner block (centre at (r,r)) by the circle of radius r: 4x4 supersampling */
static int corner_cov(int r, int i, int j){
  int n=0, R=r*8;
  for(int a=0;a<4;a++) for(int b=0;b<4;b++){ int px=i*8+a*2+1, py=j*8+b*2+1, dx=R-px, dy=R-py; if(dx*dx+dy*dy<=R*R) n++; }
  return n*255/16;
}
void gfx_round_rect(int x,int y,int w,int h,int r,u32 c){
  if(w<=0||h<=0) return;
  if(r*2>h) r=h/2; if(r*2>w) r=w/2;
  if(r<=0){ gfx_rect(x,y,w,h,c); return; }
  gfx_rect(x,y+r,w,h-2*r,c); gfx_rect(x+r,y,w-2*r,r,c); gfx_rect(x+r,y+h-r,w-2*r,r,c);
  begin(x,y,w,h);
  for(int j=0;j<r;j++) for(int i=0;i<r;i++){
    int a=corner_cov(r,i,j); if(!a) continue;
    int px[4]={x+i, x+w-1-i, x+i, x+w-1-i}, py[4]={y+j, y+j, y+h-1-j, y+h-1-j};
    for(int k=0;k<4;k++) if((u32)px[k]<gfx_w && (u32)py[k]<gfx_h){ u32 *d=(u32*)(fb+(u64)py[k]*pitch)+px[k]; *d= a==255? c : blend(*d,c,a); }
  }
  end();
}
/* coverage of the rounded-rect shape at corner pixel offsets, for rounding an already-drawn window */
int gfx_corner_cov(int r,int i,int j){ return corner_cov(r,i,j); }
u32 gfx_blend(u32 d,u32 s,int a){ return blend(d,s,a); }
u32 gfx_raw_get(int x,int y){ return rawget(x,y); }
void gfx_raw_put(int x,int y,u32 c){ begin(x,y,1,1); rawput(x,y,c); end(); }
/* anti-aliased line segment; thick16 = thickness in 1/16 pixel */
void gfx_line_aa(int x0,int y0,int x1,int y1,int thick16,u32 c){
  int minx=(x0<x1?x0:x1)-2, maxx=(x0>x1?x0:x1)+2, miny=(y0<y1?y0:y1)-2, maxy=(y0>y1?y0:y1)+2;
  i64 ax=x0*16+8, ay=y0*16+8, bx=x1*16+8, by=y1*16+8, vx=bx-ax, vy=by-ay, len2=vx*vx+vy*vy; if(!len2) len2=1;
  begin(minx,miny,maxx-minx+1,maxy-miny+1);
  for(int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++){
    i64 px=x*16+8, py=y*16+8, t=((px-ax)*vx+(py-ay)*vy)*256/len2; if(t<0) t=0; if(t>256) t=256;
    i64 cx=ax+vx*t/256, cy=ay+vy*t/256, dx=px-cx, dy=py-cy; int d=isqrt64((u64)(dx*dx+dy*dy));
    int cov=(thick16/2 - d + 8)*255/16; if(cov<=0) continue; if(cov>255) cov=255;
    if((u32)x>=gfx_w||(u32)y>=gfx_h) continue; u32 *dd=(u32*)(fb+(u64)y*pitch)+x; *dd=blend(*dd,c,cov);
  }
  end();
}

/* like gfx_blit_stride, but the source's top byte is its alpha (premultiplied colour, as Windows' 32-bit DIBs): see-through
   corners and shadows of popup windows (menus) show what is behind them */
void gfx_blit_alpha(int x,int y,int w,int h,const u32 *src,int stride){
  begin(x,y,w,h);
  for(int j=0;j<h;j++){ int yy=y+j; if(yy<0||yy>=(int)gfx_h) continue; u32 *d=(u32*)(fb+(u64)yy*pitch); const u32 *s=src+(u64)j*stride;
    for(int i=0;i<w;i++){ int xx=x+i; if(xx<0||xx>=(int)gfx_w) continue; u32 p=s[i], a=p>>24;
      if(a==255){ d[xx]=p&0xFFFFFF; continue; } if(!a) continue;
      u32 q=d[xx], ia=255-a;
      u32 r=((p>>16)&255)+(((q>>16)&255)*ia)/255, g=((p>>8)&255)+(((q>>8)&255)*ia)/255, b=(p&255)+((q&255)*ia)/255;
      d[xx]=((r>255?255:r)<<16)|((g>255?255:g)<<8)|(b>255?255:b); } }
  end();
}
void gfx_blit_stride(int x,int y,int w,int h,const u32 *src,int stride){
  begin(x,y,w,h);
  for(int j=0;j<h;j++){ int yy=y+j; if(yy<0||yy>=(int)gfx_h) continue;
    int x0=x<0?0:x, x1=x+w>(int)gfx_w?(int)gfx_w:x+w; if(x1>x0) memcpy(fb+(u64)yy*pitch+(u64)x0*4,src+(u64)j*stride+(x0-x),(u64)(x1-x0)*4); }
  end();
}
