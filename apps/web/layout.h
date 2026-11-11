/* Layout for the Willows browser: block / inline / float / table / flex layout producing a flat display list.
   The embedding program supplies: font_get(), font_w(), font_asc(), font_h(), and the image table `res`. */
#ifndef WEB_LAYOUT_H
#define WEB_LAYOUT_H
#include "css.h"
#include "image.h"

/* ---------- hooks / resources ---------- */
static int font_get(int mono, int bold, int italic, int px);
static int font_w(int fi, const char *s, int n);
static int font_asc(int fi);
static int font_h(int fi);
struct res { char *url; int state; wimg *im; };               /* state: 0 pending, 1 loaded, 2 failed */
#define MAXRES 256
static struct res res[MAXRES]; static int nres;

/* ---------- display list ---------- */
enum { OP_RECT, OP_TEXT, OP_IMG, OP_INPUT, OP_BOX };
struct op { unsigned char kind, flags; short font; int x, y, w, h; unsigned color; int link, node; const char *text; int len; short rad, bwid; unsigned bcol, icol; };   /* rad/bwid/bcol/icol: rounded box with a uniform border */
static struct op *ops; static int nops, opcap;
static int page_h, page_w; static unsigned canvas_bg = 0xFFFFFFFFu;
static int g_link_count;
static int op_add(int kind, int x, int y, int w, int h){
  if(nops >= opcap){ int nc = opcap ? opcap * 2 : 4096; struct op *p = realloc(ops, (size_t)nc * sizeof *p); if(!p) return -1; ops = p; opcap = nc; }
  struct op *o = &ops[nops]; memset(o, 0, sizeof *o); o->kind = (unsigned char)kind; o->x = x; o->y = y; o->w = w; o->h = h; o->link = -1; o->node = -1; return nops++;
}
static int op_rect(int x, int y, int w, int h, unsigned color){ if((color >> 24) == 0) return -1; int i = op_add(OP_RECT, x, y, w, h); if(i >= 0) ops[i].color = color; return i; }
static void ops_shift(int from, int to, int dx, int dy){ for(int i = from; i < to && i < nops; i++){ ops[i].x += dx; ops[i].y += dy; } }

/* ---------- small helpers ---------- */
static int imax(int a, int b){ return a > b ? a : b; }
static int imin(int a, int b){ return a < b ? a : b; }
static int resolve_len(struct len l, int base){ return l.u == 1 ? l.v : l.u == 2 ? (int)((long long)base * l.v / 100000) : 0; }
static int is_inline_level(int n){
  if(N(n).type == 1) return 1;
  struct style *s = N(n).st; if(!s) return 1;
  if(s->flt) return 0;
  return s->display == D_INLINE || s->display == D_INLINE_BLOCK || s->display == D_INLINE_FLEX;
}
static int is_display_none(int n){ return N(n).type == 0 && N(n).st && N(n).st->display == D_NONE; }
static int is_blank_text(int n){ if(N(n).type != 1) return 0; for(const char *p = N(n).text; *p; p++) if(!css_ws((unsigned char)*p)) return 0; return 1; }
static int link_of(int n){ for(; n >= 0; n = N(n).parent) if(N(n).type == 0 && N(n).link >= 0) return N(n).link; return -1; }
static int line_height_of(const struct style *st, int fi){
  int fh = font_h(fi);
  if(st->lh_mode == 1) return imax(1, (int)((long long)st->font_px * st->lh_val / 1000));
  if(st->lh_mode == 2) return imax(1, st->lh_val);
  return fh;
}
static int fi_of(const struct style *st){ return font_get(st->mono, st->bold, st->italic, st->font_px); }
static int has_class(int n, const char *c){ const char *cl = attr_of(n, "class"); return cl && class_has(cl, c); }
static int establishes_bfc(int n){
  struct style *s = N(n).st; if(N(n).parent < 0) return 1;
  { int pp = N(n).parent; if(pp >= 0 && N(pp).st && (N(pp).st->display == D_FLEX || N(pp).st->display == D_INLINE_FLEX || N(pp).st->display == D_GRID)) return 1; }
  return s->overflow_hidden || s->flt || s->display == D_INLINE_BLOCK || s->display == D_CELL || s->display == D_FLEX || s->display == D_INLINE_FLEX || s->display == D_GRID || has_class(n, "clearfix") || has_class(n, "cf") || has_class(n, "clear");
}

/* ---------- floats ---------- */
struct fl { int side, x0, x1, y0, y1; };
static struct fl floats[256]; static int nfl, fl_base;
static void avail_range(int y, int h, int *lx, int *rx){
  for(int i = fl_base; i < nfl; i++){ struct fl *f = &floats[i]; if(f->y1 <= y || f->y0 >= y + (h > 0 ? h : 1)) continue; if(f->side == FL_LEFT){ if(f->x1 > *lx) *lx = f->x1; } else if(f->x0 < *rx) *rx = f->x0; }
}
static int floats_bottom(int side_mask){ int b = 0; for(int i = fl_base; i < nfl; i++) if((side_mask & (floats[i].side == FL_LEFT ? 1 : 2)) && floats[i].y1 > b) b = floats[i].y1; return b; }

/* ---------- box resolution ---------- */
struct box { int ml, mr, mt, mb, pl, pr, pt, pb, bl, br, bt, bb; int mlauto, mrauto; };
static void resolve_box(int n, int cbw, struct box *b){
  struct style *s = N(n).st;
  b->ml = resolve_len(s->margin[3], cbw); b->mr = resolve_len(s->margin[1], cbw); b->mt = resolve_len(s->margin[0], cbw); b->mb = resolve_len(s->margin[2], cbw);
  b->mlauto = s->margin[3].u == 0; b->mrauto = s->margin[1].u == 0;
  b->pl = imax(0, resolve_len(s->padding[3], cbw)); b->pr = imax(0, resolve_len(s->padding[1], cbw)); b->pt = imax(0, resolve_len(s->padding[0], cbw)); b->pb = imax(0, resolve_len(s->padding[2], cbw));
  b->bt = s->bstyle[0] ? s->border_w[0] : 0; b->br = s->bstyle[1] ? s->border_w[1] : 0; b->bb = s->bstyle[2] ? s->border_w[2] : 0; b->bl = s->bstyle[3] ? s->border_w[3] : 0;
}

/* ---------- measuring (min / max content width, border box) ---------- */
static void measure(int n, int *minw, int *maxw);
static int img_natural(int n, int *w, int *h){
  int r = N(n).res - 1; if(r >= 0 && r < nres && res[r].state == 1 && res[r].im){ *w = res[r].im->w; *h = res[r].im->h; return 1; } return 0;
}
static int is_control(int n){
  int t = N(n).tag; if(t == TAG_input){ const char *ty = attr_of(n, "type"); if(ty && !strcasecmp_(ty, "hidden")) return 0; return 1; }
  return t == TAG_select || t == TAG_textarea;
}
static void control_size(int n, int *w, int *h){
  struct style *s = N(n).st; int fi = fi_of(s); int fh = font_h(fi); int cw = font_w(fi, "0", 1);
  int t = N(n).tag; const char *ty = attr_of(n, "type"); if(!ty) ty = "text";
  *h = fh + 10; *w = 20 * cw + 12;
  if(t == TAG_input){
    if(!strcasecmp_(ty, "checkbox") || !strcasecmp_(ty, "radio")){ *w = *h = 14; }
    else if(!strcasecmp_(ty, "submit") || !strcasecmp_(ty, "button") || !strcasecmp_(ty, "reset") || !strcasecmp_(ty, "image")){
      const char *v = N(n).value ? N(n).value : attr_of(n, "value"); if(!v || !*v) v = !strcasecmp_(ty, "reset") ? "Reset" : "Submit";
      *w = font_w(fi, v, (int)strlen(v)) + 28; *h = fh + 12;
    } else { const char *sz = attr_of(n, "size"); int c = sz ? atoi(sz) : 20; if(c < 1 || c > 100) c = 20; *w = c * cw + 12; }
  } else if(t == TAG_select){
    int mw = 40; for(int c = N(n).first; c >= 0; c = N(c).next) if(N(c).tag == TAG_option){ for(int d = N(c).first; d >= 0; d = N(d).next) if(N(d).type == 1){ int ww = font_w(fi, N(d).text, (int)strlen(N(d).text)); if(ww > mw) mw = ww; } }
    *w = mw + 34; *h = fh + 10;
  } else if(t == TAG_textarea){
    const char *c = attr_of(n, "cols"), *r = attr_of(n, "rows"); int cc = c ? atoi(c) : 20, rr = r ? atoi(r) : 2; if(cc < 1) cc = 20; if(rr < 1) rr = 2; *w = cc * cw + 12; *h = rr * fh + 10;
  }
  if(s->width.u == 1) *w = s->width.v; if(s->height.u == 1) *h = s->height.v;
}
static int replaced_size(int n, int *w, int *h, int availw){
  /* returns 1 if n is a replaced element and fills its border-box size */
  struct style *s = N(n).st; int t = N(n).tag;
  if(t == TAG_img){
    int nw, nh; int have = img_natural(n, &nw, &nh); int cw = -1, ch = -1;
    if(s->width.u == 1) cw = s->width.v; else if(s->width.u == 2 && availw > 0) cw = (int)((long long)availw * s->width.v / 100000);
    if(s->height.u == 1) ch = s->height.v;
    if(cw < 0 && ch < 0){ if(have){ cw = nw; ch = nh; } else { cw = 0; ch = 0; } }
    else if(cw < 0){ cw = have && nh ? (int)((long long)nw * ch / nh) : ch; }
    else if(ch < 0){ ch = have && nw ? (int)((long long)nh * cw / nw) : (s->height.u ? 0 : cw * 3 / 4); if(!have) ch = cw > 0 && N(n).res - 1 >= 0 ? cw * 9 / 16 : 0; }
    if(s->maxw.u == 1 && cw > s->maxw.v){ if(ch > 0) ch = (int)((long long)ch * s->maxw.v / cw); cw = s->maxw.v; }
    if(availw > 0 && cw > availw){ if(ch > 0) ch = (int)((long long)ch * availw / cw); cw = availw; }
    *w = cw; *h = ch; return 1;
  }
  if(is_control(n)){ control_size(n, w, h); return 1; }
  if(t == TAG_svg || t == TAG_canvas || t == TAG_video || t == TAG_iframe){ *w = s->width.u == 1 ? s->width.v : 0; *h = s->height.u == 1 ? s->height.v : 0; return 1; }
  return 0;
}
static int g_ms_space;                                       /* a collapsible space is pending between measured words */
static void text_words_extent(int n, int *minw, int *maxw, int *cur){
  struct style *s = N(n).st; int fi = fi_of(s); const char *p = N(n).text; int pre = s->white_space == WS_PRE || s->white_space == WS_PRE_WRAP;
  int sp = font_w(fi, " ", 1);
  while(*p){
    if(pre && *p == '\n'){ if(*cur > *maxw) *maxw = *cur; *cur = 0; g_ms_space = 0; p++; continue; }
    if(css_ws((unsigned char)*p)){ if(pre){ *cur += (*p == '\t' ? 4 : 1) * sp; } else g_ms_space = 1; p++; continue; }
    const char *e = p; while(*e && !css_ws((unsigned char)*e)) e++;
    int wl = font_w(fi, p, (int)(e - p)); if(wl > *minw) *minw = wl;
    if(*cur > 0 && g_ms_space && !pre) *cur += sp;
    *cur += wl; g_ms_space = 0; p = e;
  }
}
static int table_measure(int n, int *minw, int *maxw);
static void measure_inline_run(int first, int *minw, int *maxw, int *cur){
  for(int c = first; c >= 0; c = N(c).next){
    if(N(c).type == 1){ text_words_extent(c, minw, maxw, cur); continue; }
    struct style *s = N(c).st; if(s->display == D_NONE) continue;
    if(N(c).tag == TAG_br){ if(*cur > *maxw) *maxw = *cur; *cur = 0; continue; }
    int rw, rh; if(replaced_size(c, &rw, &rh, 0)){ int ow = rw + (s->margin[3].u == 1 ? s->margin[3].v : 0) + (s->margin[1].u == 1 ? s->margin[1].v : 0); *cur += ow; if(ow > *minw) *minw = ow; continue; }
    if(s->display == D_INLINE){ struct box ib; resolve_box(c, 0, &ib); *cur += ib.ml + ib.pl + ib.bl; measure_inline_run(N(c).first, minw, maxw, cur); *cur += ib.mr + ib.pr + ib.br; continue; }
    int mn, mx; measure(c, &mn, &mx); *cur += mx; if(mn > *minw) *minw = mn;
  }
}
static void measure(int n, int *minw, int *maxw){
  struct style *s = N(n).st; struct box b; resolve_box(n, 0, &b);
  int extra = b.pl + b.pr + b.bl + b.br;
  int rw, rh;
  if(N(n).type == 0 && replaced_size(n, &rw, &rh, 0)){ *minw = *maxw = rw + extra; return; }
  int mn = 0, mx = 0;
  if(s->display == D_TABLE){ table_measure(n, &mn, &mx); *minw = mn + extra; *maxw = mx + extra; if(s->width.u == 1){ *maxw = imax(*minw, s->width.v); } return; }
  int c = N(n).first; int cur = 0; g_ms_space = 0;
  while(c >= 0){
    if(is_display_none(c)){ c = N(c).next; continue; }
    if(is_inline_level(c)){
      int rs = c; int rmin = 0, rmax = 0, rcur = 0;
      for(; c >= 0 && (is_display_none(c) || is_inline_level(c)); c = N(c).next){ int one = N(c).next; N(c).next = -1; measure_inline_run(c, &rmin, &rmax, &rcur); N(c).next = one; }
      (void)rs; if(rcur > rmax) rmax = rcur;
      mn = imax(mn, rmin); mx = imax(mx, rmax); (void)cur;
    } else {
      int cmn, cmx; measure(c, &cmn, &cmx);
      struct box cb; resolve_box(c, 0, &cb); int mm = cb.ml + cb.mr;
      if(N(c).st->flt || s->display == D_FLEX || s->display == D_INLINE_FLEX){ mx += 0; }
      mn = imax(mn, cmn + mm); mx = imax(mx, cmx + mm);
      if(s->display == D_FLEX || s->display == D_INLINE_FLEX){ if(!s->flex_dir){ /* row: items add up */ } }
      c = N(c).next;
    }
  }
  if(s->display == D_FLEX || s->display == D_INLINE_FLEX){
    if(!s->flex_dir){ mn = 0; mx = 0; int cnt = 0; for(int k = N(n).first; k >= 0; k = N(k).next){ if(N(k).type != 1 && !is_display_none(k)){ int cmn, cmx; measure(k, &cmn, &cmx); struct box cb; resolve_box(k, 0, &cb); mn = imax(mn, cmn + cb.ml + cb.mr); mx += cmx + cb.ml + cb.mr; cnt++; } } if(cnt > 1) mx += (cnt - 1) * s->gap_x; if(s->flex_wrap == 0) mn = mx; }
  }
  if(s->width.u == 1){ int w = s->width.v + (s->box_sizing ? 0 : extra); *maxw = w; *minw = imin(mn + extra, w); if(s->display != D_INLINE_BLOCK) *minw = w <= mn + extra ? w : mn + extra; return; }
  *minw = mn + extra; *maxw = mx + extra;
  if(s->maxw.u == 1 && *maxw > s->maxw.v + (s->box_sizing ? 0 : extra)) *maxw = s->maxw.v + (s->box_sizing ? 0 : extra);
}

/* ---------- inline layout ---------- */
struct atom { unsigned char kind, deco, valign; short font; int x, w, asc, desc, node, link; unsigned color, bg; const char *text; int len; int i0, i1; int boxh; };
#define MAXATOMS 1024
struct ictx {
  int cx0, cw;                     /* container content box */
  int y;                           /* top of the current line */
  int x;                           /* current pen x (absolute) */
  struct atom a[MAXATOMS]; int na;
  int space;                       /* pending collapsible space */
  int first;                       /* next line is the first of the block */
  int indent, ta, nowrap, any;     /* text-indent, text-align, ever produced a line */
  int strut_asc, strut_desc;       /* used for empty <br> lines */
};
/* nested layouts (inline-blocks, floats, blocks inside inline elements) each get their own line context */
static struct ictx lstack[10]; static int ldepth;
#define L (lstack[ldepth])
#define NEST_BEGIN() int _nested = ldepth < 9; if(_nested) ldepth++
#define NEST_END() if(_nested) ldepth--
static int g_md, g_mark_pend[24], g_mark_base[24], g_mark_font[24];                 /* list-item marker: baseline of the first line */
static void lay_children(int n, int cx, int *cy, int cw);
static int lay_block(int n, int x, int y, int availw, int bw);
static int ctop(int n, int cbw, int depth);
static int g_r_bg, g_r_b0, g_r_bn, g_r_bw, g_r_x;                     /* info about the box laid out last (for stretching) */

static void line_range(int *lx, int *rx, int h){
  *lx = L.cx0; *rx = L.cx0 + L.cw; avail_range(L.y, h, lx, rx);
  if(L.first && L.indent) *lx += L.indent;
}
static void end_line(void){
  if(!L.na){ return; }
  int maxasc = 0, maxdesc = 0, tall = 0;
  for(int i = 0; i < L.na; i++){ struct atom *a = &L.a[i]; if(a->valign == VA_BASELINE || a->valign == VA_SUB || a->valign == VA_SUPER){ maxasc = imax(maxasc, a->asc); maxdesc = imax(maxdesc, a->desc); } else tall = imax(tall, a->asc + a->desc); }
  int lh = imax(maxasc + maxdesc, tall);
  int base = L.y + maxasc; if(tall > maxasc + maxdesc) base += (tall - (maxasc + maxdesc)) / 2;
  int lx, rx; line_range(&lx, &rx, lh);
  int used = 0; for(int i = 0; i < L.na; i++) used = imax(used, L.a[i].x + L.a[i].w);
  int off = 0; int avail = rx - lx; if(L.ta == TA_CENTER) off = (avail - used) / 2; else if(L.ta == TA_RIGHT) off = avail - used; if(off < 0) off = 0;
  for(int i = 0; i < L.na; i++){
    struct atom *a = &L.a[i]; int ax = lx + off + a->x;
    int ay;                                                          /* top of the atom's box */
    switch(a->valign){ case VA_TOP: ay = L.y; break; case VA_BOTTOM: ay = L.y + lh - (a->asc + a->desc); break; case VA_MIDDLE: ay = base - (a->asc + a->desc) / 2 - a->asc / 4 + a->asc / 4; ay = base - (a->asc + a->desc) / 2 - 2; break;
      case VA_SUB: ay = base - a->asc + a->desc / 3 + 2; break; case VA_SUPER: ay = base - a->asc - a->desc / 2 - 2; break; default: ay = base - a->asc; }
    if(a->kind == 0){                                                /* word */
      int fa = font_asc(a->font), fh = font_h(a->font);
      int ty = ay + a->asc - fa;
      if(a->bg >> 24){ int bi = op_rect(ax, ay + a->asc - fa - (a->asc - fa) / 2 - 0, a->w, a->asc + a->desc, a->bg); (void)bi; if(bi >= 0){ ops[bi].y = base - a->asc; ops[bi].h = a->asc + a->desc; } }
      if(a->text && a->len > 0){ int oi = op_add(OP_TEXT, ax, ty, a->w, fh); if(oi >= 0){ ops[oi].font = a->font; ops[oi].color = a->color; ops[oi].link = a->link; ops[oi].text = a->text; ops[oi].len = a->len; ops[oi].node = a->node; ops[oi].flags = a->deco; } }
      if(a->deco & 1){ int uw = a->w; if(i + 1 < L.na && (L.a[i + 1].deco & 1) && L.a[i + 1].kind == 0 && L.a[i + 1].color == a->color) uw = L.a[i + 1].x - a->x; op_rect(ax, ty + fa + 2, uw, 1, a->color); }
      if(a->deco & 2){ op_rect(ax, ty + fa * 2 / 3, a->w, 1, a->color); }
    } else if(a->kind == 1){                                         /* image / control */
      int oi = op_add(a->boxh == -1 ? OP_IMG : OP_INPUT, ax, ay, a->w, a->asc + a->desc); if(oi >= 0){ ops[oi].node = a->node; ops[oi].link = a->link; ops[oi].color = a->color; }
    } else if(a->kind == 2){                                         /* inline-block already laid out at (0,0) */
      ops_shift(a->i0, a->i1, ax, ay);
    }
  }
  if(g_md > 0 && g_mark_pend[g_md]){ g_mark_pend[g_md] = 0; g_mark_base[g_md] = base; g_mark_font[g_md] = L.na ? L.a[0].font : 0; }
  L.y += lh; L.na = 0; L.x = 0; L.space = 0; L.first = 0; L.any = 1;
}
static void line_begin_x(void){ L.x = 0; }
/* x position (relative to the line's left edge) at which an atom of width w still fits? returns 1 if it fits */
static int fits(int w){
  int lx, rx; line_range(&lx, &rx, 0);
  return L.x + w <= rx - lx;
}
static int atom_push(struct atom *a){
  if(L.na >= MAXATOMS) end_line();
  L.a[L.na++] = *a; return 1;
}
static void apply_transform(int mode, char *buf, int n){
  if(!mode) return;
  int start = 1;
  for(int i = 0; i < n; i++){ unsigned char c = (unsigned char)buf[i];
    if(mode == 1 && c >= 'a' && c <= 'z') buf[i] = (char)(c - 32); else if(mode == 2 && c >= 'A' && c <= 'Z') buf[i] = (char)(c + 32);
    else if(mode == 3){ if(start && c >= 'a' && c <= 'z') buf[i] = (char)(c - 32); start = css_ws(c); } }
}
static void add_word(int tn, const char *w, int n){
  struct style *s = N(tn).st; int fi = fi_of(s);
  char tmp[512]; if(n > 500) n = 500; memcpy(tmp, w, n); tmp[n] = 0; apply_transform(s->text_transform, tmp, n);
  int ww = font_w(fi, tmp, n); int sp = L.space ? font_w(fi, " ", 1) : 0;
  const char *txt = s->text_transform ? a_str(tmp, n) : w;
  int lh = line_height_of(s, fi), fa = font_asc(fi), fh = font_h(fi);
  struct atom a; memset(&a, 0, sizeof a); a.kind = 0; a.font = (short)fi; a.node = tn; a.color = s->color; a.link = link_of(tn);
  a.deco = (unsigned char)((s->underline ? 1 : 0) | (s->linethru ? 2 : 0)); a.valign = s->valign;
  { int p = N(tn).parent; while(p >= 0 && N(p).st && N(p).st->display == D_INLINE){ if(N(p).st->bg >> 24){ a.bg = N(p).st->bg; break; } p = N(p).parent; } }
  a.asc = fa + (lh - fh) / 2; a.desc = lh - a.asc; if(a.desc < 0) a.desc = 0;
  if(!L.nowrap){
    int lx, rx; line_range(&lx, &rx, lh); int avail = rx - lx;
    if(L.na && L.x + sp + ww > avail) end_line();
    if(!L.na){                                                        /* empty line: break overlong words by character */
      line_range(&lx, &rx, lh); avail = rx - lx; if(avail < 20) avail = 20;
      if(ww > avail && n > 1){
        int pos = 0;
        while(pos < n){
          int len = 1; int cw_ = font_w(fi, tmp + pos, 1);
          while(pos + len < n){ int step = 1; while(pos + len + step < n && ((unsigned char)tmp[pos + len] & 0xC0) == 0x80) step++; int w2 = font_w(fi, tmp + pos, len + step); if(w2 > avail) break; len += step; cw_ = w2; }
          struct atom b = a; b.x = 0; b.w = cw_; b.text = a_str(tmp + pos, len); b.len = len; L.x = 0; atom_push(&b); L.x = cw_; pos += len; if(pos < n) end_line();
        }
        L.space = 0; return;
      }
    }
  }
  if(L.na && sp) L.x += sp; else if(!L.na) L.x = 0;
  a.x = L.x; a.w = ww; a.text = txt; a.len = n; atom_push(&a); L.x += ww; L.space = 0;
}
static void inline_text(int tn){
  struct style *s = N(tn).st; const char *t = N(tn).text; int pre = s->white_space == WS_PRE || s->white_space == WS_PRE_WRAP;
  int saved_nowrap = L.nowrap; L.nowrap = s->white_space == WS_NOWRAP || s->white_space == WS_PRE;
  const char *p = t;
  while(*p){
    if(pre){
      if(*p == '\n'){ int fi = fi_of(s); L.strut_asc = font_asc(fi); L.strut_desc = line_height_of(s, fi) - font_asc(fi);
        if(!L.na){ int lh = line_height_of(s, fi); L.y += lh; L.first = 0; L.any = 1; } else end_line(); p++; continue; }
      if(*p == ' ' || *p == '\t'){ const char *e = p; int cnt = 0; while(*e == ' ' || *e == '\t'){ cnt += *e == '\t' ? 4 : 1; e++; }
        int fi = fi_of(s); int w = cnt * font_w(fi, " ", 1);
        if(L.na || cnt) { L.x += w; L.space = 0; if(!L.na){ /* leading spaces in an empty line: keep via a blank atom */ struct atom a; memset(&a, 0, sizeof a); a.font = (short)fi; a.node = tn; a.x = 0; a.w = w; int lh = line_height_of(s, fi); a.asc = font_asc(fi) + (lh - font_h(fi)) / 2; a.desc = lh - a.asc; a.color = s->color; L.x = 0; atom_push(&a); L.x = w; } }
        p = e; continue; }
      const char *e = p; while(*e && *e != '\n' && *e != ' ' && *e != '\t') e++;
      add_word(tn, p, (int)(e - p)); p = e; continue;
    }
    if(css_ws((unsigned char)*p)){ if(s->white_space == WS_PRE_LINE && *p == '\n'){ end_line(); } else if(L.na) L.space = 1; p++; continue; }
    const char *e = p; while(*e && !css_ws((unsigned char)*e)) e++;
    /* a non-breaking space (U+00A0 = C2 A0) glues words: it is rendered as a space inside the word */
    add_word(tn, p, (int)(e - p)); p = e;
  }
  L.nowrap = saved_nowrap;
}
static int clamp_w(int v, struct style *s, int extra, int cbw);
static void place_float(int n, int cx0, int cw, int y);
static void inline_node(int n);
static void inline_children(int n){ for(int c = N(n).first; c >= 0; c = N(c).next) inline_node(c); }
static void block_in_inline(int n){
  end_line(); L.space = 0;
  struct style *s = N(n).st; struct box b; resolve_box(n, L.cw, &b);
  int y = L.y + ctop(n, L.cw, 0); int h;
  { int cx0 = L.cx0, cw0 = L.cw; NEST_BEGIN(); h = lay_block(n, cx0, y, cw0, 0); NEST_END(); }
  L.y = y + h + b.mb; L.first = 0; L.x = 0;
  (void)s;
}
static void inline_node(int n){
  if(N(n).type == 1){ inline_text(n); return; }
  struct style *s = N(n).st; if(s->display == D_NONE) return;
  int tag = N(n).tag;
  if(tag == TAG_br){ int fi = fi_of(s); int lh = line_height_of(s, fi); if(L.na) end_line(); else { L.y += lh; L.first = 0; L.any = 1; } L.space = 0; return; }
  if(s->flt){ end_line(); place_float(n, L.cx0, L.cw, L.y); return; }
  int rw, rh;
  if(replaced_size(n, &rw, &rh, L.cw)){
    struct box b; resolve_box(n, L.cw, &b);
    int ow = rw + b.ml + b.mr;
    if(tag == TAG_input && (is_control(n) == 0)) return;
    if(tag == TAG_img){
      int r = N(n).res - 1; const char *alt = attr_of(n, "alt");
      if(rw <= 0 || rh <= 0){                                          /* not loaded (or no size): show the alt text */
        if(alt && *alt && !(r >= 0 && res[r].state == 0 && 0)){ unsigned oc = N(n).st->color; (void)oc; for(const char *p = alt; *p; ){ while(*p && css_ws((unsigned char)*p)) { L.space = L.na ? 1 : 0; p++; } const char *e = p; while(*e && !css_ws((unsigned char)*e)) e++; if(e > p){ /* temporarily style via parent */ struct node save = N(n); (void)save; add_word(n, p, (int)(e - p)); } p = e; } }
        return;
      }
    }
    int lx, rx; line_range(&lx, &rx, rh);
    if(!L.nowrap && L.na && L.x + (L.space ? 4 : 0) + ow > rx - lx) end_line();
    if(L.na && L.space) L.x += 4; L.space = 0;
    struct atom a; memset(&a, 0, sizeof a); a.kind = 1; a.node = n; a.link = link_of(n); a.x = L.x + b.ml; a.w = rw; a.asc = rh + b.mt; a.desc = b.mb; a.valign = s->valign; a.boxh = tag == TAG_img ? -1 : 0; a.color = s->color; if(tag != TAG_img && !is_control(n)) a.kind = 3;
    if(a.valign == VA_BASELINE && tag != TAG_img && is_control(n)){ a.asc = rh * 2 / 3; a.desc = rh - a.asc; }
    atom_push(&a); L.x += ow; return;
  }
  if(s->display == D_INLINE_BLOCK || s->display == D_INLINE_FLEX){
    struct box b; resolve_box(n, L.cw, &b); int mn, mx; measure(n, &mn, &mx);
    int lx, rx; line_range(&lx, &rx, 0); int avail = rx - lx - b.ml - b.mr; if(avail < 20) avail = L.cw - b.ml - b.mr;
    int bextra = b.pl + b.pr + b.bl + b.br; int w;
    if(s->width.u == 1) w = s->width.v + (s->box_sizing ? 0 : bextra); else if(s->width.u == 2) w = (int)((long long)(avail) * s->width.v / 100000);
    else { w = imin(mx, avail); if(w < mn) w = imin(mn, avail); }
    w = clamp_w(w, s, bextra, avail); if(w < bextra) w = bextra;
    int save_nfl = nfl, save_base = fl_base; fl_base = nfl;
    int i0 = nops; NEST_BEGIN(); int h = lay_block(n, 0, 0, w, w); NEST_END(); int i1 = nops;
    nfl = save_nfl; fl_base = save_base;
    int bwid = g_r_bw; int ow = bwid + b.ml + b.mr;
    if(!L.nowrap && L.na && L.x + (L.space ? font_w(fi_of(s), " ", 1) : 0) + ow > rx - lx) end_line();
    if(L.na && L.space) L.x += font_w(fi_of(s), " ", 1); L.space = 0;
    struct atom a; memset(&a, 0, sizeof a); a.kind = 2; a.node = n; a.x = L.x + b.ml; a.w = bwid; a.asc = h + b.mt; a.desc = b.mb; a.valign = s->valign; a.i0 = i0; a.i1 = i1;
    atom_push(&a); L.x += ow; return;
  }
  if(s->display != D_INLINE){ block_in_inline(n); return; }
  /* plain inline element */
  struct box b; resolve_box(n, L.cw, &b);
  if(b.ml + b.pl + b.bl){ if(L.na) { if(L.space){ L.x += font_w(fi_of(s), " ", 1); L.space = 0; } L.x += b.ml + b.pl + b.bl; } }
  inline_children(n);
  if(b.mr + b.pr + b.br){ if(L.na) L.x += b.mr + b.pr + b.br; }
}
static void run_begin(int n, int cx, int cw, int y){
  L.cx0 = cx; L.cw = cw; L.y = y; L.x = 0; L.na = 0; L.space = 0; L.first = 1; L.any = 0;
  struct style *s = N(n).st; L.indent = s->text_indent; L.ta = s->text_align; L.nowrap = s->white_space == WS_NOWRAP || s->white_space == WS_PRE;
}
static int run_end(void){ end_line(); return L.y; }

/* ---------- floats ---------- */
static void place_float(int n, int cx0, int cw, int y){
  struct style *s = N(n).st; struct box b; resolve_box(n, cw, &b);
  int mn, mx; measure(n, &mn, &mx);
  int bw;
  if(s->width.u == 1) bw = s->width.v + (s->box_sizing ? 0 : b.pl + b.pr + b.bl + b.br);
  else if(s->width.u == 2) bw = (int)((long long)cw * s->width.v / 100000);
  else bw = imin(mx, cw - b.ml - b.mr);
  if(bw < mn && s->width.u == 0) bw = imin(mn, cw - b.ml - b.mr);
  if(s->maxw.u == 1 && bw > s->maxw.v) bw = s->maxw.v;
  if(bw < 1) bw = 1;
  int save_nfl = nfl, save_base = fl_base; fl_base = nfl;
  int i0 = nops; NEST_BEGIN(); int h = lay_block(n, 0, 0, bw, bw); NEST_END(); int i1 = nops;
  nfl = save_nfl; fl_base = save_base;
  int ow = bw + b.ml + b.mr, oh = h + b.mt + b.mb;
  if(s->clear){ int bt = floats_bottom(s->clear == 1 ? 1 : s->clear == 2 ? 2 : 3); if(bt > y) y = bt; }
  int fy = y, fx = cx0;
  for(int iter = 0; iter < 40; iter++){
    int lx = cx0, rx = cx0 + cw; avail_range(fy, oh, &lx, &rx);
    if(rx - lx >= ow || iter == 39){ fx = s->flt == FL_LEFT ? lx : rx - ow; break; }
    int next = 0x7FFFFFFF; for(int i = fl_base; i < nfl; i++) if(floats[i].y1 > fy && floats[i].y1 < next && floats[i].y0 < fy + oh) next = floats[i].y1;
    if(next == 0x7FFFFFFF) { fx = s->flt == FL_LEFT ? lx : rx - ow; break; }
    fy = next;
  }
  ops_shift(i0, i1, fx + b.ml, fy + b.mt);
  if(nfl < 256){ floats[nfl].side = s->flt; floats[nfl].x0 = fx; floats[nfl].x1 = fx + ow; floats[nfl].y0 = fy; floats[nfl].y1 = fy + oh; nfl++; }
}

/* ---------- block layout ---------- */
static int collapse2(int a, int b){ if(a >= 0 && b >= 0) return imax(a, b); if(a <= 0 && b <= 0) return imin(a, b); return a + b; }
static int is_block_level(int n){ if(N(n).type == 1) return 0; struct style *s = N(n).st; return s->display != D_NONE && !s->flt && !is_inline_level(n); }
static int top_open(int n){ struct box b; resolve_box(n, 0, &b); struct style *s = N(n).st; return !(b.pt || b.bt) && !establishes_bfc(n) && s->display != D_TABLE; }
static int bottom_open(int n){ struct box b; resolve_box(n, 0, &b); struct style *s = N(n).st; return !(b.pb || b.bb) && !establishes_bfc(n) && s->display != D_TABLE && s->height.u == 0 && s->minh.u == 0; }
static int first_flow_child(int n){ for(int c = N(n).first; c >= 0; c = N(c).next){ if(is_display_none(c)) continue; if(N(c).type == 1){ if(is_blank_text(c)) continue; return -1; } if(N(c).st->flt) continue; return is_block_level(c) ? c : -1; } return -1; }
static int last_flow_child(int n){ int last = -1; for(int c = N(n).first; c >= 0; c = N(c).next){ if(is_display_none(c)) continue; if(N(c).type == 1){ if(is_blank_text(c)) continue; last = -2; continue; } if(N(c).st->flt) continue; last = is_block_level(c) ? c : -2; } return last >= 0 ? last : -1; }
static int ctop(int n, int cbw, int depth){
  struct box b; resolve_box(n, cbw, &b); int m = b.mt; if(depth > 12 || !top_open(n)) return m;
  int c = first_flow_child(n); if(c < 0) return m; return collapse2(m, ctop(c, cbw, depth + 1));
}
static int cbot(int n, int cbw, int depth){
  struct box b; resolve_box(n, cbw, &b); int m = b.mb; if(depth > 12 || !bottom_open(n)) return m;
  int c = last_flow_child(n); if(c < 0) return m; return collapse2(m, cbot(c, cbw, depth + 1));
}
static int list_index(int n){
  int idx = 1; int par = N(n).parent;
  if(par >= 0){ const char *st = attr_of(par, "start"); if(st) idx = atoi(st); }
  const char *v = attr_of(n, "value"); if(v) return atoi(v);
  for(int p = N(par).first; p >= 0 && p != n; p = N(p).next) if(N(p).type == 0 && N(p).st && N(p).st->display == D_LIST_ITEM){ const char *pv = attr_of(p, "value"); if(pv) idx = atoi(pv); idx++; }
  return idx;
}
static void emit_marker(int n, int cx, int base, int fi){
  struct style *s = N(n).st; int lt = s->list_type; if(lt == LT_NONE) return;
  int fa = font_asc(fi), fh = font_h(fi); (void)fh;
  if(lt == LT_DISC || lt == LT_CIRCLE || lt == LT_SQUARE){
    int d = imax(4, s->font_px / 3); int mx = cx - d - imax(6, s->font_px / 2), my = base - fa / 2 - d / 2 + 1;
    if(lt == LT_DISC){ op_rect(mx + 1, my, d - 2, d, s->color); op_rect(mx, my + 1, d, d - 2, s->color); }
    else if(lt == LT_SQUARE) op_rect(mx, my, d, d, s->color);
    else { op_rect(mx + 1, my, d - 2, 1, s->color); op_rect(mx + 1, my + d - 1, d - 2, 1, s->color); op_rect(mx, my + 1, 1, d - 2, s->color); op_rect(mx + d - 1, my + 1, 1, d - 2, s->color); }
    return;
  }
  int idx = list_index(n); char buf[24]; int k = 0;
  if(lt == LT_DECIMAL){ char t[12]; int l = 0; int v = idx < 0 ? -idx : idx; do { t[l++] = (char)('0' + v % 10); v /= 10; } while(v); if(idx < 0) buf[k++] = '-'; while(l) buf[k++] = t[--l]; }
  else if(lt == LT_LOWER_ALPHA || lt == LT_UPPER_ALPHA){ int v = idx - 1; char t[8]; int l = 0; do { t[l++] = (char)((lt == LT_LOWER_ALPHA ? 'a' : 'A') + v % 26); v = v / 26 - 1; } while(v >= 0 && l < 7); while(l) buf[k++] = t[--l]; }
  else { static const int val[] = {1000,900,500,400,100,90,50,40,10,9,5,4,1}; static const char *const sym[] = {"m","cm","d","cd","c","xc","l","xl","x","ix","v","iv","i"}; int v = idx > 0 && idx < 4000 ? idx : 1;
    for(int i = 0; i < 13; i++) while(v >= val[i]){ const char *q = sym[i]; while(*q) buf[k++] = lt == LT_UPPER_ROMAN ? (char)(*q++ - 32) : *q++; v -= val[i]; } }
  buf[k++] = '.'; buf[k] = 0;
  int w = font_w(fi, buf, k); int oi = op_add(OP_TEXT, cx - w - 6, base - fa, w, font_h(fi));
  if(oi >= 0){ ops[oi].font = (short)fi; ops[oi].color = s->color; ops[oi].text = a_str(buf, k); ops[oi].len = k; }
}
static void lay_table(int n, int x, int *py, int availw, int bw);
static void lay_flex(int n, int cx, int *cy, int cw);
static void lay_grid(int n, int cx, int *cy, int cw);

/* lay out the children of n (a block container) in the content box [cx, cx+cw), starting at *cy */
static void lay_children(int n, int cx, int *cy, int cw){
  struct style *ps = N(n).st; int y = *cy; int pend = 0; int first = 1;
  int c = N(n).first;
  int top_collapsed = top_open(n) && n != g_root;                       /* first child's margin already merged into ours */
  while(c >= 0){
    if(is_display_none(c)){ c = N(c).next; continue; }
    if(N(c).type == 1 && is_blank_text(c) && ps->white_space != WS_PRE){ c = N(c).next; continue; }
    if(N(c).type == 0 && N(c).st->flt){
      struct style *fs = N(c).st; if(fs->clear){} place_float(c, cx, cw, y + (pend > 0 ? 0 : 0)); c = N(c).next; continue;
    }
    if(is_block_level(c)){
      struct style *cs = N(c).st;
      if(cs->clear){ int bt = floats_bottom(cs->clear == 1 ? 1 : cs->clear == 2 ? 2 : 3); if(bt > y + pend){ y = bt; pend = 0; } }
      int mt = ctop(c, cw, 0);
      int gap;
      if(first && top_collapsed){ gap = 0; }
      else gap = collapse2(pend, mt);
      if(first && !top_collapsed) gap = mt;
      y += gap; pend = 0;
      int h;
      if(cs->display == D_TABLE){ struct box b; resolve_box(c, cw, &b); int ty = y; lay_table(c, cx, &ty, cw, 0); h = ty - y; }
      else h = lay_block(c, cx, y, cw, 0);
      y += h; pend = cbot(c, cw, 0); first = 0; c = N(c).next; continue;
    }
    /* a run of inline content */
    run_begin(n, cx, cw, y + (pend > 0 ? pend : 0));
    while(c >= 0){
      if(is_display_none(c)){ c = N(c).next; continue; }
      if(N(c).type == 0 && N(c).st->flt){ end_line(); place_float(c, cx, cw, L.y); c = N(c).next; continue; }
      if(is_block_level(c)) break;
      if(N(c).type == 1 && is_blank_text(c) && ps->white_space != WS_PRE){ if(L.na) L.space = 1; c = N(c).next; continue; }
      inline_node(c); c = N(c).next;
    }
    int ny = run_end();
    if(L.any){ y = ny; first = 0; pend = 0; }
  }
  if(pend && !bottom_open(n)) y += pend;
  *cy = y;
  if(bottom_open(n) && pend){ /* the last child's bottom margin is reported through cbot() */ }
}

static void ops_clip_below(int i0, int limit){            /* drop ops that start below `limit` (crude overflow:hidden) */
  int o = i0; for(int i = i0; i < nops; i++){ if(ops[i].y >= limit) continue; if(o != i) ops[o] = ops[i]; o++; } nops = o;
}
static void stretch_box(int bg, int b0, int bn, int newh){
  if(bg >= 0) ops[bg].h = newh;
  if(b0 >= 0 && bn == 4){ int oy = ops[b0].y; ops[b0 + 1].h = newh; ops[b0 + 2].y = oy + newh - ops[b0 + 2].h; ops[b0 + 3].h = newh; }
}
static int clamp_w(int v, struct style *s, int extra, int cbw){
  /* v is a border-box width; apply min/max (css values are content-box unless border-box) */
  int add = s->box_sizing ? 0 : extra;
  if(s->maxw.u){ int m = resolve_len(s->maxw, cbw) + add; if(v > m) v = m; }
  if(s->minw.u){ int m = resolve_len(s->minw, cbw) + add; if(v < m) v = m; }
  return v;
}
/* Lay out element n as a block whose border box has its top-left at (x, y) (x is the left edge of the containing block's content,
   margins are applied here horizontally). Returns the border-box height. bw > 0 forces the border-box width. */
static int lay_block(int n, int x, int y, int availw, int bw){
  struct style *s = N(n).st; struct box b; resolve_box(n, availw, &b);
  int extra = b.pl + b.pr + b.bl + b.br;
  int ox = x;
  if(bw <= 0){
    if(s->width.u == 1) bw = s->width.v + (s->box_sizing ? 0 : extra);
    else if(s->width.u == 2) bw = (int)((long long)availw * s->width.v / 100000) + (s->box_sizing ? 0 : 0);
    else bw = availw - b.ml - b.mr;
    bw = clamp_w(bw, s, extra, availw);
    if(s->width.u && (b.mlauto || b.mrauto)){
      int free = availw - bw - (b.mlauto ? 0 : b.ml) - (b.mrauto ? 0 : b.mr);
      if(free > 0){ if(b.mlauto && b.mrauto) ox = x + free / 2; else if(b.mlauto) ox = x + free; else ox = x + b.ml; } else ox = x + (b.mlauto ? 0 : b.ml);
    } else if(!s->width.u && s->maxw.u && bw < availw - b.ml - b.mr && (b.mlauto || b.mrauto)){ int free = availw - bw; ox = b.mlauto && b.mrauto ? x + free / 2 : b.mlauto ? x + free : x + b.ml; }
    else ox = x + b.ml;
  } else ox = x + 0;
  if(bw < extra) bw = extra;
  int cw = bw - extra; if(cw < 0) cw = 0;
  int cx = ox + b.bl + b.pl;
  int bgop = -1; unsigned bg = s->bg;
  if(bg >> 24) bgop = op_rect(ox, y, bw, 0, bg);
  else if(s->radius > 0 && (b.bt || b.br || b.bb || b.bl)){ bgop = op_add(OP_RECT, ox, y, bw, 0); }       /* rounded border without a background */
  int save_nfl = nfl, save_base = fl_base; int bfc = establishes_bfc(n) && N(n).parent >= 0;
  if(bfc){ fl_base = nfl; }
  int cy = y + b.bt + b.pt; int content_top = cy;
  int mark = s->display == D_LIST_ITEM && g_md < 22; int mark_lvl = 0;
  if(mark){ g_md++; mark_lvl = g_md; g_mark_pend[g_md] = 1; g_mark_base[g_md] = 0; g_mark_font[g_md] = 0; }
  int ctl_w, ctl_h; int child_ops0 = nops;
  int mypp = 0;
  int tag = N(n).tag;
  if(tag == TAG_hr){ /* nothing */ }
  if(N(n).type == 0 && (tag == TAG_input || tag == TAG_img || tag == TAG_select || tag == TAG_textarea) && replaced_size(n, &ctl_w, &ctl_h, cw)){
    /* block-level replaced element (display:block image) */
    int oi = op_add(tag == TAG_img ? OP_IMG : OP_INPUT, cx, cy, ctl_w, ctl_h); if(oi >= 0){ ops[oi].node = n; ops[oi].link = link_of(n); }
    cy += ctl_h; mypp = 1;
  }
  if(!mypp){
    if(s->display == D_FLEX || s->display == D_INLINE_FLEX){ lay_flex(n, cx, &cy, cw); }
    else if(s->display == D_GRID){ lay_grid(n, cx, &cy, cw); }
    else if(s->display == D_TABLE){ /* handled by the caller normally; here via inline-block table */ int ty = cy; lay_table(n, cx, &ty, cw, 0); cy = ty; }
    else lay_children(n, cx, &cy, cw);
  }
  if(bfc){ int fb = floats_bottom(3); if(fb > cy) cy = fb; nfl = save_nfl; fl_base = save_base; }
  if(mark){
    int fi = fi_of(s);
    if(g_mark_pend[mark_lvl]) emit_marker(n, cx, content_top + font_asc(fi) + (line_height_of(s, fi) - font_h(fi)) / 2, fi);
    else emit_marker(n, cx, g_mark_base[mark_lvl], g_mark_font[mark_lvl] ? g_mark_font[mark_lvl] : fi);
    g_md--;
  }
  int ch = cy - content_top;
  if(s->height.u == 1){ int hv = s->height.v - (s->box_sizing ? extra - 0 : 0); (void)hv; int target = s->box_sizing ? s->height.v - b.pt - b.pb - b.bt - b.bb : s->height.v; if(target >= 0) ch = target; }
  if(s->minh.u == 1){ int m = s->box_sizing ? s->minh.v - b.pt - b.pb - b.bt - b.bb : s->minh.v; if(ch < m) ch = m; }
  if(s->maxh.u == 1 && s->overflow_hidden){ int m = s->box_sizing ? s->maxh.v - b.pt - b.pb - b.bt - b.bb : s->maxh.v; if(ch > m) ch = m; }
  int bh = ch + b.pt + b.pb + b.bt + b.bb;
  if(s->overflow_hidden && (s->height.u == 1 || s->maxh.u == 1)) ops_clip_below(child_ops0, y + bh);
  if(bgop >= 0) ops[bgop].h = bh;
  g_r_bg = bgop; g_r_b0 = -1; g_r_bn = 0;
  int ring = 0;                                                         /* rounded box: draw the uniform border as a ring in the same op */
  if(s->radius > 0 && bgop >= 0){
    int r = s->radius; if(r > bw / 2) r = bw / 2; if(r > bh / 2) r = bh / 2; ops[bgop].rad = (short)r; ops[bgop].icol = (bg >> 24) ? bg : s->eff_bg;
    if(b.bt && b.bt == b.br && b.br == b.bb && b.bb == b.bl && s->border_c[0] == s->border_c[1] && s->border_c[1] == s->border_c[2] && s->border_c[2] == s->border_c[3]){
      unsigned bc = s->border_c[0] == COL_CURRENT ? s->color : s->border_c[0]; if(bc >> 24){ ops[bgop].bwid = (short)b.bt; ops[bgop].bcol = bc; ring = 1; } }
    if(!(bg >> 24) && !ring) ops[bgop].rad = 0;
  }
  if(!ring && (b.bt || b.br || b.bb || b.bl)){
    unsigned cc[4]; for(int i = 0; i < 4; i++){ cc[i] = s->border_c[i] == COL_CURRENT ? s->color : s->border_c[i]; if(!(cc[i] >> 24)) cc[i] = 0; }
    int first_i = nops; int ok = 1;
    int i0 = op_add(OP_RECT, ox, y, bw, b.bt); if(i0 < 0) ok = 0; else { ops[i0].color = cc[0]; if(!b.bt || !cc[0]) ops[i0].h = 0; }
    int i1 = op_add(OP_RECT, ox + bw - b.br, y, b.br, bh); if(i1 >= 0){ ops[i1].color = cc[1]; if(!b.br || !cc[1]) ops[i1].w = 0; }
    int i2 = op_add(OP_RECT, ox, y + bh - b.bb, bw, b.bb); if(i2 >= 0){ ops[i2].color = cc[2]; if(!b.bb || !cc[2]) ops[i2].h = 0; }
    int i3 = op_add(OP_RECT, ox, y, b.bl, bh); if(i3 >= 0){ ops[i3].color = cc[3]; if(!b.bl || !cc[3]) ops[i3].w = 0; }
    if(ok){ g_r_b0 = first_i; g_r_bn = 4; }
  }
  g_r_bw = bw; g_r_x = ox;
  if(y + bh > page_h) page_h = y + bh;
  if(ox + bw > page_w) page_w = ox + bw;
  return bh;
}

/* ---------- tables ---------- */
struct tcell { int n, row, col, cs, rs, minw, maxw, fixed, pct; int h; int i0, i1, bg, b0, bn; };
static struct tcell *tcells; static int ntcells, tcap;
static int tcell_attr(int n, const char *name){ const char *v = attr_of(n, name); int x = v ? atoi(v) : 1; return x < 1 ? 1 : x > 64 ? 64 : x; }
static int table_collect(int n, int rows_out[], int maxrows, int *nrows){
  /* fills tcells with the cells of table n; returns number of columns */
  ntcells = 0; int r = 0; int ncols = 0;
  static unsigned char occ[200][64];
  memset(occ, 0, sizeof occ);
  int groups[2]; (void)groups;
  int stack_n[64]; int sd = 0; stack_n[sd++] = N(n).first;
  /* walk children: rows directly, or inside row groups */
  for(int g = N(n).first; g >= 0; g = N(g).next){
    if(is_display_none(g) || N(g).type != 0) continue;
    int d = N(g).st->display;
    int rowlist[2] = {-1, -1}; (void)rowlist;
    if(d == D_ROW_GROUP || d == D_ROW){
      int first_row = d == D_ROW ? g : N(g).first; int stop_after_one = d == D_ROW;
      for(int row = first_row; row >= 0; row = stop_after_one ? -1 : N(row).next){
        if(N(row).type != 0 || is_display_none(row) || N(row).st->display != D_ROW) continue;
        if(r >= 200) break;
        rows_out[r] = row; int col = 0;
        for(int c = N(row).first; c >= 0; c = N(c).next){
          if(N(c).type != 0 || is_display_none(c) || N(c).st->display != D_CELL) continue;
          while(col < 64 && occ[r][col]) col++;
          if(col >= 64) break;
          int cs = tcell_attr(c, "colspan"), rs = tcell_attr(c, "rowspan"); if(col + cs > 64) cs = 64 - col; if(r + rs > 200) rs = 200 - r;
          if(ntcells >= tcap){ int nc = tcap ? tcap * 2 : 128; struct tcell *p = realloc(tcells, (size_t)nc * sizeof *p); if(!p) break; tcells = p; tcap = nc; }
          struct tcell *tc = &tcells[ntcells++]; memset(tc, 0, sizeof *tc); tc->n = c; tc->row = r; tc->col = col; tc->cs = cs; tc->rs = rs;
          for(int yy = 0; yy < rs; yy++) for(int xx = 0; xx < cs; xx++) occ[r + yy][col + xx] = 1;
          col += cs; if(col > ncols) ncols = col;
        }
        r++;
      }
    }
  }
  *nrows = r; (void)maxrows; return ncols;
}
static int table_spacing(int n){ struct style *s = N(n).st; return s->border_collapse ? 0 : s->spacing; }
/* computes column min/max widths; returns total min/max (without spacing/border of the table itself) */
static int g_colmin[65], g_colmax[65], g_colfix[65], g_colpct[65];
static int table_columns(int n, int *rows, int *nrows, int *ncols_out){
  int ncols = table_collect(n, rows, 200, nrows);
  if(ncols > 64) ncols = 64;
  for(int i = 0; i <= 64; i++){ g_colmin[i] = g_colmax[i] = g_colfix[i] = g_colpct[i] = 0; }
  for(int i = 0; i < ntcells; i++){
    int mn, mx; measure(tcells[i].n, &mn, &mx); struct tcell *tc = &tcells[i];
    struct style *cs = N(tc->n).st; struct box b; resolve_box(tc->n, 0, &b); int extra = b.pl + b.pr + b.bl + b.br;
    tc->minw = mn; tc->maxw = mx; tc->fixed = 0; tc->pct = 0;
    if(cs->width.u == 1){ tc->fixed = cs->width.v + (cs->box_sizing ? 0 : extra); tc->minw = imax(mn, 0); if(tc->fixed < tc->minw) tc->fixed = tc->minw; tc->maxw = tc->fixed; if(N(tc->n).tag == TAG_td || N(tc->n).tag == TAG_th) tc->minw = imin(tc->minw, tc->fixed); }
    else if(cs->width.u == 2) tc->pct = cs->width.v;
    if(tc->cs == 1){
      int c = tc->col; g_colmin[c] = imax(g_colmin[c], tc->minw); g_colmax[c] = imax(g_colmax[c], tc->maxw);
      if(tc->fixed) g_colfix[c] = imax(g_colfix[c], tc->fixed); if(tc->pct) g_colpct[c] = imax(g_colpct[c], tc->pct);
    }
  }
  for(int i = 0; i < ntcells; i++){                                   /* spanning cells: widen the spanned columns evenly when needed */
    struct tcell *tc = &tcells[i]; if(tc->cs == 1) continue;
    int smin = 0, smax = 0; for(int c = tc->col; c < tc->col + tc->cs; c++){ smin += g_colmin[c]; smax += g_colmax[c]; }
    if(tc->minw > smin){ int d = (tc->minw - smin) / tc->cs; for(int c = tc->col; c < tc->col + tc->cs; c++) g_colmin[c] += d; }
    if(tc->maxw > smax){ int d = (tc->maxw - smax) / tc->cs; for(int c = tc->col; c < tc->col + tc->cs; c++) g_colmax[c] += d; }
  }
  for(int c = 0; c < ncols; c++) if(g_colmax[c] < g_colmin[c]) g_colmax[c] = g_colmin[c];
  *ncols_out = ncols; return ncols;
}
static int table_measure(int n, int *minw, int *maxw){
  static int rows_m[200]; int nr, nc;
  int ntc_save = ntcells; (void)ntc_save;
  /* table_columns uses global scratch: measure of nested tables must not clobber the outer one, so copy results out immediately */
  struct tcell *saved = 0; int nsaved = ntcells; int savecols[5][65];
  if(ntcells){ saved = malloc((size_t)ntcells * sizeof *saved); if(saved) memcpy(saved, tcells, (size_t)ntcells * sizeof *saved); }
  memcpy(savecols[0], g_colmin, sizeof g_colmin); memcpy(savecols[1], g_colmax, sizeof g_colmax); memcpy(savecols[2], g_colfix, sizeof g_colfix); memcpy(savecols[3], g_colpct, sizeof g_colpct);
  table_columns(n, rows_m, &nr, &nc);
  int sp = table_spacing(n); int mn = sp, mx = sp;
  for(int c = 0; c < nc; c++){ mn += g_colmin[c] + sp; mx += g_colmax[c] + sp; }
  if(saved){ memcpy(tcells, saved, (size_t)nsaved * sizeof *saved); ntcells = nsaved; free(saved); } else ntcells = 0;
  memcpy(g_colmin, savecols[0], sizeof g_colmin); memcpy(g_colmax, savecols[1], sizeof g_colmax); memcpy(g_colfix, savecols[2], sizeof g_colfix); memcpy(g_colpct, savecols[3], sizeof g_colpct);
  *minw = mn; *maxw = mx; return nc;
}
static void lay_table(int n, int x, int *py, int availw, int bw){
  struct style *s = N(n).st; struct box b; resolve_box(n, availw, &b);
  int y = *py;
  int rows_l[200]; int nr, nc;
  int sp = table_spacing(n);
  int extra = b.pl + b.pr + b.bl + b.br;
  /* the table's own caption(s) */
  int cap = -1; for(int c = N(n).first; c >= 0; c = N(c).next) if(N(c).type == 0 && !is_display_none(c) && N(c).st->display == D_CAPTION){ cap = c; break; }
  struct tcell *cells; int ncells;
  table_columns(n, rows_l, &nr, &nc);
  if(nc == 0 || nr == 0){ if(cap >= 0){ int h = lay_block(cap, x, y, availw, 0); *py = y + h; } return; }
  cells = malloc((size_t)ntcells * sizeof *cells); if(!cells) return; memcpy(cells, tcells, (size_t)ntcells * sizeof *cells); ncells = ntcells;
  int colmin[65], colmax[65], colfix[65], colpct[65]; memcpy(colmin, g_colmin, sizeof colmin); memcpy(colmax, g_colmax, sizeof colmax); memcpy(colfix, g_colfix, sizeof colfix); memcpy(colpct, g_colpct, sizeof colpct);
  int summin = sp, summax = sp; for(int c = 0; c < nc; c++){ summin += colmin[c] + sp; summax += colmax[c] + sp; }
  int target;
  if(bw > 0) target = bw - extra;
  else if(s->width.u == 1) target = s->width.v - (s->box_sizing ? extra : 0);
  else if(s->width.u == 2) target = (int)((long long)availw * s->width.v / 100000) - (s->box_sizing ? extra : 0);
  else target = imin(summax, availw - b.ml - b.mr - extra);
  if(s->width.u == 0 && bw <= 0 && summax + extra > availw - b.ml - b.mr) target = availw - b.ml - b.mr - extra;
  if(target < summin) target = summin;
  int colw[65]; for(int c = 0; c < nc; c++) colw[c] = colmin[c];
  int avail = target - sp * (nc + 1);
  int pctsum = 0; for(int c = 0; c < nc; c++) pctsum += colpct[c];
  if(pctsum > 0 && pctsum <= 100000 + 1000){
    int fixed_total = 0; for(int c = 0; c < nc; c++) if(!colpct[c]) fixed_total += colmin[c];
    for(int c = 0; c < nc; c++) if(colpct[c]){ int w = (int)((long long)(target - sp * (nc + 1)) * colpct[c] / 100000); colw[c] = imax(colmin[c], w); }
    int used = 0; for(int c = 0; c < nc; c++) used += colw[c];
    int rest = avail - used; int nonpct_max = 0; for(int c = 0; c < nc; c++) if(!colpct[c]) nonpct_max += colmax[c] - colmin[c];
    if(rest > 0 && nonpct_max > 0) for(int c = 0; c < nc; c++) if(!colpct[c]){ int d = (int)((long long)rest * (colmax[c] - colmin[c]) / nonpct_max); colw[c] += imin(d, colmax[c] - colmin[c] + rest); }
  } else {
    int summin2 = 0, summax2 = 0; for(int c = 0; c < nc; c++){ summin2 += colmin[c]; summax2 += colmax[c]; }
    if(avail >= summax2){
      int extra_space = avail - summax2; for(int c = 0; c < nc; c++) colw[c] = colmax[c];
      if(extra_space > 0 && (s->width.u || bw > 0)){                       /* explicit table width: grow columns proportionally (flexible ones first) */
        int flex = 0; for(int c = 0; c < nc; c++) if(!colfix[c]) flex += colmax[c] > 0 ? colmax[c] : 1;
        if(!flex){ for(int c = 0; c < nc; c++) flex += colmax[c] > 0 ? colmax[c] : 1; for(int c = 0; c < nc; c++) colw[c] += (int)((long long)extra_space * (colmax[c] > 0 ? colmax[c] : 1) / flex); }
        else for(int c = 0; c < nc; c++) if(!colfix[c]) colw[c] += (int)((long long)extra_space * (colmax[c] > 0 ? colmax[c] : 1) / flex);
      }
    } else if(avail > summin2){
      int d = summax2 - summin2; for(int c = 0; c < nc; c++) colw[c] = colmin[c] + (int)((long long)(avail - summin2) * (colmax[c] - colmin[c]) / (d ? d : 1));
    }
  }
  int total = sp; for(int c = 0; c < nc; c++) total += colw[c] + sp;
  int tw = total + extra;
  int tx = x + b.ml;
  if(bw <= 0 && (b.mlauto && b.mrauto)){ int free = availw - tw; if(free > 0) tx = x + free / 2; }
  else if(bw <= 0 && b.mlauto){ int free = availw - tw; if(free > 0) tx = x + free; }
  if(bw > 0) tx = x;
  int bgop = (s->bg >> 24) ? op_rect(tx, y, tw, 0, s->bg) : -1;
  int cy = y + b.bt + b.pt; int cx = tx + b.bl + b.pl;
  if(cap >= 0){ int h = lay_block(cap, cx, cy, total, 0); cy += h; }
  cy += sp;
  int colx[65]; { int px_ = cx + sp; for(int c = 0; c < nc; c++){ colx[c] = px_; px_ += colw[c] + sp; } }
  int *rowh = calloc((size_t)nr + 1, sizeof(int)), *rowy = calloc((size_t)nr + 1, sizeof(int));
  if(!rowh || !rowy){ free(rowh); free(rowy); free(cells); return; }
  /* row background boxes: emitted before the cells */
  int *rowbg = malloc(((size_t)nr + 1) * sizeof(int)); for(int r = 0; r < nr; r++) rowbg[r] = -1;
  int ry = cy;
  for(int r = 0; r < nr; r++){
    rowy[r] = ry;
    int rh = 0;
    struct style *rs = N(rows_l[r]).st; if(rs->bg >> 24) rowbg[r] = op_rect(cx + sp, ry, total - 2 * sp, 0, rs->bg);
    for(int i = 0; i < ncells; i++){
      struct tcell *tc = &cells[i]; if(tc->row != r) continue;
      int cw = 0; for(int c = tc->col; c < tc->col + tc->cs; c++) cw += colw[c]; cw += sp * (tc->cs - 1);
      tc->i0 = nops; int h = lay_block(tc->n, colx[tc->col], ry, cw, cw); tc->i1 = nops; tc->h = h; tc->bg = g_r_bg; tc->b0 = g_r_b0; tc->bn = g_r_bn;
      if(tc->rs == 1) rh = imax(rh, h);
    }
    struct style *trs = N(rows_l[r]).st; if(trs->height.u == 1) rh = imax(rh, trs->height.v);
    rowh[r] = rh; ry += rh + sp;
    for(int i = 0; i < ncells; i++){ struct tcell *tc = &cells[i]; if(tc->row + tc->rs - 1 == r && tc->rs > 1){ int span_h = 0; for(int q = tc->row; q <= r; q++) span_h += rowh[q] + sp; span_h -= sp; if(tc->h > span_h){ rowh[r] += tc->h - span_h; ry += tc->h - span_h; } } }
  }
  /* stretch cell backgrounds/borders to the row height and apply vertical alignment */
  for(int i = 0; i < ncells; i++){
    struct tcell *tc = &cells[i]; int H = 0; for(int q = tc->row; q < tc->row + tc->rs && q < nr; q++) H += rowh[q] + (q > tc->row ? sp : 0);
    int dy = 0; struct style *cs = N(tc->n).st;
    if(H > tc->h){ if(cs->valign == VA_MIDDLE) dy = (H - tc->h) / 2; else if(cs->valign == VA_BOTTOM) dy = H - tc->h; stretch_box(tc->bg, tc->b0, tc->bn, H); }
    if(dy){ int from = tc->bg >= 0 ? tc->bg + 1 : tc->i0; int to = tc->b0 >= 0 ? tc->b0 : tc->i1; ops_shift(from, to, 0, dy); }
  }
  for(int r = 0; r < nr; r++) if(rowbg[r] >= 0) ops[rowbg[r]].h = rowh[r];
  int end_y = ry; if(nr == 0) end_y = cy;
  int th = (end_y - y) + b.pb + b.bb;
  if(s->height.u == 1 && th < s->height.v) th = s->height.v;
  if(bgop >= 0) ops[bgop].h = th;
  if(b.bt || b.br || b.bb || b.bl){
    unsigned col = s->border_c[0] == COL_CURRENT ? s->color : s->border_c[0];
    if(b.bt) op_rect(tx, y, tw, b.bt, col); if(b.bb) op_rect(tx, y + th - b.bb, tw, b.bb, col); if(b.bl) op_rect(tx, y, b.bl, th, col); if(b.br) op_rect(tx + tw - b.br, y, b.br, th, col);
  }
  free(rowh); free(rowy); free(rowbg); free(cells);
  g_r_bw = tw; g_r_bg = bgop; g_r_b0 = -1; g_r_bn = 0; g_r_x = tx;
  *py = y + th; if(y + th > page_h) page_h = y + th; if(tx + tw > page_w) page_w = tx + tw;
}

/* ---------- flexbox (single and multi line rows; columns fall back to block flow) ---------- */
struct fitem { int n, basis, minw, maxw, w, h, outer, mt, mb, ml, mr, grow, shrink, line, i0, i1, bg, b0, bn; };
static void lay_flex(int n, int cx, int *cy, int cw){
  struct style *s = N(n).st;
  if(s->flex_dir == 1){ lay_children(n, cx, cy, cw); return; }
  struct fitem *it = 0; int cnt = 0, cap = 0;
  for(int c = N(n).first; c >= 0; c = N(c).next){
    if(N(c).type != 0 || is_display_none(c)) continue;
    if(N(c).st->position == POS_ABSOLUTE) continue;
    if(cnt >= cap){ cap = cap ? cap * 2 : 16; struct fitem *p = realloc(it, (size_t)cap * sizeof *p); if(!p){ free(it); return; } it = p; }
    struct style *cs = N(c).st; struct box b; resolve_box(c, cw, &b); int mn, mx; measure(c, &mn, &mx);
    struct fitem *f = &it[cnt++]; memset(f, 0, sizeof *f); f->n = c; f->minw = mn; f->maxw = mx; f->ml = b.ml; f->mr = b.mr; f->mt = b.mt; f->mb = b.mb;
    int extra = b.pl + b.pr + b.bl + b.br;
    if(cs->flex_basis.u == 1) f->basis = cs->flex_basis.v + (cs->box_sizing ? 0 : extra);
    else if(cs->flex_basis.u == 2) f->basis = (int)((long long)cw * cs->flex_basis.v / 100000);
    else if(cs->width.u == 1) f->basis = cs->width.v + (cs->box_sizing ? 0 : extra);
    else if(cs->width.u == 2) f->basis = (int)((long long)cw * cs->width.v / 100000);
    else f->basis = mx;
    f->grow = cs->flex_grow; f->shrink = cs->flex_shrink;
  }
  int y = *cy; int i = 0; int gx = s->gap_x, gy = s->gap_y;
  while(i < cnt){
    int j = i, sum = 0, nitems = 0;
    while(j < cnt){ int w = it[j].basis + it[j].ml + it[j].mr; if(nitems && s->flex_wrap && sum + gx + w > cw) break; sum += (nitems ? gx : 0) + w; nitems++; j++; }
    int free = cw - sum; int totgrow = 0, totshrink = 0;
    for(int k = i; k < j; k++){ totgrow += it[k].grow; totshrink += it[k].shrink * (it[k].basis > 0 ? it[k].basis : 1); it[k].w = it[k].basis; }
    if(free > 0 && totgrow > 0){ for(int k = i; k < j; k++) it[k].w += (int)((long long)free * it[k].grow / totgrow); free = 0; }
    else if(free < 0){ int deficit = -free; for(int k = i; k < j; k++){ int sh = totshrink ? (int)((long long)deficit * it[k].shrink * (it[k].basis > 0 ? it[k].basis : 1) / totshrink) : 0; it[k].w = imax(it[k].minw < it[k].basis ? it[k].minw : it[k].basis, it[k].basis - sh); } free = 0; }
    int used = 0; for(int k = i; k < j; k++) used += it[k].w + it[k].ml + it[k].mr; used += gx * (nitems - 1);
    int left = cw - used; int startx = cx, between = gx;
    if(left > 0 && s->justify == 1) startx = cx + left / 2; else if(left > 0 && s->justify == 2) startx = cx + left;
    else if(left > 0 && s->justify == 3 && nitems > 1) between = gx + left / (nitems - 1);
    else if(left > 0 && s->justify == 4){ between = gx + left / nitems; startx = cx + left / nitems / 2; }
    int px = startx; int lineh = 0;
    for(int k = i; k < j; k++){
      it[k].i0 = nops;
      int h = lay_block(it[k].n, px + it[k].ml, y + it[k].mt, it[k].w, it[k].w);
      it[k].i1 = nops; it[k].h = h; it[k].bg = g_r_bg; it[k].b0 = g_r_b0; it[k].bn = g_r_bn;
      lineh = imax(lineh, h + it[k].mt + it[k].mb);
      px += it[k].w + it[k].ml + it[k].mr + between;
    }
    for(int k = i; k < j; k++){                                         /* cross-axis alignment: 0 stretch, 1 center, 2 end */
      struct style *cs = N(it[k].n).st; int mode = s->align_items;
      if(cs->align_self == 1) mode = 0; else if(cs->align_self == 2) mode = 1; else if(cs->align_self == 3) mode = 2;
      int avail_h = lineh - it[k].mt - it[k].mb; if(avail_h <= it[k].h) continue;
      if(mode == 0 && cs->height.u == 0 && cs->align_self != 1) stretch_box(it[k].bg, it[k].b0, it[k].bn, avail_h);
      else if(mode == 1) ops_shift(it[k].i0, it[k].i1, 0, (avail_h - it[k].h) / 2);
      else if(mode == 2) ops_shift(it[k].i0, it[k].i1, 0, avail_h - it[k].h);
    }
    y += lineh + (j < cnt ? gy : 0); i = j;
  }
  *cy = y; free(it);
}

/* CSS grid: explicit column tracks (fr/px/%/auto/repeat), auto row placement, gaps, align-items */
struct gitem { int n, col, span, row, i0, i1, h, mt, mb, ml, mr, bg, b0, bn, x, w; };
static void lay_grid(int n, int cx, int *cy, int cw){
  struct style *s = N(n).st; int gx = s->gap_x, gy = s->gap_y;
  int tt[40], tv[40], nc = 0;                                           /* expanded tracks: type, value */
  for(int i = 0; i < s->ncols && nc < 32; i++){
    int t = s->cols[i].t, v = s->cols[i].v;
    if(t == 5 || t == 6){ int cnt = (cw + gx) / (v + gx > 0 ? v + gx : 1); if(cnt < 1) cnt = 1; if(cnt > 24) cnt = 24; for(int k = 0; k < cnt && nc < 32; k++){ tt[nc] = t == 5 ? 3 : 1; tv[nc] = t == 5 ? 1000 : v; nc++; } }
    else { tt[nc] = t == 4 ? 3 : t; tv[nc] = t == 4 ? 1000 : v; nc++; }                         /* auto is treated as 1fr */
  }
  if(nc == 0){ tt[0] = 3; tv[0] = 1000; nc = 1; }
  int colw[32], colx[32], fixed = 0, frsum = 0;
  for(int i = 0; i < nc; i++){ if(tt[i] == 1){ colw[i] = tv[i]; fixed += colw[i]; } else if(tt[i] == 2){ colw[i] = (int)((long long)cw * tv[i] / 100000); fixed += colw[i]; } else { colw[i] = 0; frsum += tv[i]; } }
  int avail = cw - fixed - gx * (nc - 1); if(avail < 0) avail = 0;
  for(int i = 0; i < nc; i++) if(tt[i] == 3) colw[i] = frsum ? (int)((long long)avail * tv[i] / frsum) : 0;
  { int x = 0; for(int i = 0; i < nc; i++){ colx[i] = x; x += colw[i] + gx; } }
  struct gitem *it = 0; int cnt = 0, cap = 0;
  for(int c = N(n).first; c >= 0; c = N(c).next){
    if(N(c).type != 0 || is_display_none(c) || N(c).st->position == POS_ABSOLUTE) continue;
    if(cnt >= cap){ cap = cap ? cap * 2 : 16; struct gitem *p = realloc(it, (size_t)cap * sizeof *p); if(!p){ free(it); return; } it = p; }
    struct box b; resolve_box(c, cw, &b); struct gitem *g = &it[cnt++]; memset(g, 0, sizeof *g); g->n = c; g->ml = b.ml; g->mr = b.mr; g->mt = b.mt; g->mb = b.mb;
    g->span = N(c).st->grid_span > 0 ? N(c).st->grid_span : 1; if(g->span > nc) g->span = nc;
  }
  int col = 0, row = 0;
  for(int i = 0; i < cnt; i++){ if(col + it[i].span > nc){ col = 0; row++; } it[i].col = col; it[i].row = row; col += it[i].span; if(col >= nc){ col = 0; row++; } }
  int y = *cy, i = 0;
  while(i < cnt){
    int r = it[i].row, j = i, rowh = 0;
    while(j < cnt && it[j].row == r) j++;
    for(int k = i; k < j; k++){
      int w = 0; for(int c = it[k].col; c < it[k].col + it[k].span; c++) w += colw[c]; w += gx * (it[k].span - 1);
      int iw = w - it[k].ml - it[k].mr; if(iw < 0) iw = 0; it[k].w = iw;
      it[k].i0 = nops; int h = lay_block(it[k].n, cx + colx[it[k].col] + it[k].ml, y + it[k].mt, iw, iw);
      it[k].i1 = nops; it[k].h = h; it[k].bg = g_r_bg; it[k].b0 = g_r_b0; it[k].bn = g_r_bn;
      rowh = imax(rowh, h + it[k].mt + it[k].mb);
    }
    for(int k = i; k < j; k++){                                           /* align-items: 0 stretch (default), 1 center, 2 end, 4 start */
      struct style *cs = N(it[k].n).st; int mode = s->align_items;
      if(cs->align_self == 1) mode = 0; else if(cs->align_self == 2) mode = 1; else if(cs->align_self == 3) mode = 2;
      int avail_h = rowh - it[k].mt - it[k].mb; if(avail_h <= it[k].h) continue;
      if(mode == 0 && cs->height.u == 0) stretch_box(it[k].bg, it[k].b0, it[k].bn, avail_h);
      else if(mode == 1) ops_shift(it[k].i0, it[k].i1, 0, (avail_h - it[k].h) / 2);
      else if(mode == 2) ops_shift(it[k].i0, it[k].i1, 0, avail_h - it[k].h);
    }
    y += rowh + (j < cnt ? gy : 0); i = j;
  }
  *cy = y; free(it);
}

/* ---------- page layout ---------- */
static void layout_page(int view_w){
  nops = 0; page_h = 0; page_w = view_w; nfl = fl_base = 0; g_md = 0;
  canvas_bg = 0xFFFFFFFFu;
  if(g_root < 0) return;
  struct style *hs = N(g_root).st;
  unsigned cbg = hs ? hs->bg : 0;
  int body = g_body;
  if(!(cbg >> 24) && body >= 0 && N(body).st) cbg = N(body).st->bg;
  if(cbg >> 24) canvas_bg = cbg;
  int y = 0;
  lay_children(g_root, 0, &y, view_w);
  if(y > page_h) page_h = y;
  page_h += 8;
}
#endif
