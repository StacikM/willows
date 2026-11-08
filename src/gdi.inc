/* Willows GDI (included into wm.c): per-DC state, memory DCs and bitmaps, pens/brushes/fonts, text, lines, rectangles, BitBlt,
   plus the user32 odds and ends classic programs expect (timers, capture, clipboard, keyboard state, window placement). */
#define K_MEM 8
#define GO_BASE 0x9000
#define GO_MAX 1024
enum { GT_BRUSH = 1, GT_PEN, GT_FONT, GT_BITMAP, GT_PALETTE };
struct gobj { u8 used, type, style, hollow; u32 color; int px, w, h, penw; u32 *pix; u8 mono; };
static struct gobj gobjs[GO_MAX];
struct dcst { u8 init, bkmode, align, mem; u32 fg, bg; u64 font, pen, brush, bmp; i32 px, py; };
static struct dcst dcs[MAXW];
static u64 stock_obj[24];
static u64 gh(int i){ return GO_BASE + (u64)i * 4; }
static struct gobj *go_get(u64 h){ if(h < GO_BASE || ((h - GO_BASE) & 3)) return 0; u64 i = (h - GO_BASE) >> 2; return (i < GO_MAX && gobjs[i].used) ? &gobjs[i] : 0; }
static u64 go_new(int type){ for(int i = 1; i < GO_MAX; i++) if(!gobjs[i].used){ memset(&gobjs[i], 0, sizeof gobjs[i]); gobjs[i].used = 1; gobjs[i].type = (u8)type; return gh(i); } return 0; }
static u32 cref(u32 c){ return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }      /* COLORREF (BGR) -> 0xRRGGBB */
static struct dcst *dc_state(u64 dc){
  if(dc < DCBASE || dc >= DCBASE + MAXW) return 0; struct dcst *d = &dcs[dc - DCBASE];
  if(!d->init){ memset(d, 0, sizeof *d); d->init = 1; d->bkmode = 2; d->fg = 0; d->bg = 0xFFFFFF; }
  return d;
}
static void dc_reset(int widx){ dcs[widx].init = 0; }
static u64 mk_stock(int i);
static u64 stock(int i){ if(i < 0 || i >= 24) return 0; if(!stock_obj[i]) stock_obj[i] = mk_stock(i); return stock_obj[i]; }
static u64 mk_brush(u32 rgb, int hollow){ u64 h = go_new(GT_BRUSH); struct gobj *o = go_get(h); if(o){ o->color = rgb; o->hollow = (u8)hollow; } return h; }
static u64 mk_pen(u32 rgb, int w, int style){ u64 h = go_new(GT_PEN); struct gobj *o = go_get(h); if(o){ o->color = rgb; o->penw = w < 1 ? 1 : w; o->style = (u8)style; } return h; }
static int str_has_ci(const char *s, const char *needle){ if(!s) return 0; int n = (int)strlen(needle); for(; *s; s++){ int i = 0; while(i < n && s[i] && (s[i] | 32) == needle[i]) i++; if(i == n) return 1; } return 0; }
static u64 mk_font(int h, int weight, int italic, const char *face){
  int px = h < 0 ? -h : (h ? h * 85 / 100 : 13); int mono = str_has_ci(face, "courier") || str_has_ci(face, "consolas") || str_has_ci(face, "mono") || str_has_ci(face, "console") || str_has_ci(face, "fixed") || str_has_ci(face, "terminal");
  int bold = weight >= 600; int style = mono ? (bold ? FS_MONOBOLD : FS_MONO) : bold ? FS_BOLD : italic ? FS_ITALIC : FS_REGULAR;
  u64 hh = go_new(GT_FONT); struct gobj *o = go_get(hh); if(o){ o->px = px; o->style = (u8)style; o->mono = (u8)mono; o->penw = weight; } return hh;
}
static u64 mk_stock(int i){
  switch(i){
    case 0: return mk_brush(0xFFFFFF, 0); case 1: return mk_brush(0xC0C0C0, 0); case 2: return mk_brush(0x808080, 0); case 3: return mk_brush(0x404040, 0); case 4: return mk_brush(0, 0); case 5: return mk_brush(0, 1);
    case 6: return mk_pen(0xFFFFFF, 1, 0); case 7: return mk_pen(0, 1, 0); case 8: return mk_pen(0, 1, 5);
    case 10: case 11: case 16: return mk_font(-13, 400, 0, "Courier New");
    default: return mk_font(-13, 400, 0, "Open Sans");
  }
}
static const struct font *font_of_dc(struct dcst *d){
  struct gobj *o = d ? go_get(d->font) : 0; if(o) return font_find(o->style, o->px); return font_find(FS_REGULAR, 13);
}
static int dc_font_mono(struct dcst *d){ struct gobj *o = d ? go_get(d->font) : 0; return o && o->mono; }

/* ---- objects ---- */
static u64 WINAPI w_CreateSolidBrush(u32 c){ return mk_brush(cref(c), 0); }
static u64 WINAPI w_CreatePen(i32 style, i32 w, u32 c){ return mk_pen(cref(c), w, style); }
static u64 WINAPI w_CreateFontA(i32 h, i32 w, i32 e, i32 o, i32 weight, u32 italic, u32 u, u32 s, u32 cs, u32 op, u32 cp, u32 q, u32 pitch, const char *face){ (void)w; (void)e; (void)o; (void)u; (void)s; (void)cs; (void)op; (void)cp; (void)q; (void)pitch; return mk_font(h, weight, italic != 0, face); }
static u64 WINAPI w_CreateFontIndirectA(const u8 *lf){ return mk_font(*(const i32 *)lf, *(const i32 *)(lf + 16), lf[20], (const char *)(lf + 28)); }
static u64 WINAPI w_GetStockObject(i32 i){ return stock(i); }
static u64 WINAPI w_CreatePalette(const void *p){ (void)p; return go_new(GT_PALETTE); }
static u64 WINAPI w_CreateBitmap(i32 w, i32 h, u32 planes, u32 bpp, const void *bits){
  (void)planes; (void)bpp; if(w <= 0 || h <= 0 || (u64)w * h > 16000000) return 0; u64 hh = go_new(GT_BITMAP); struct gobj *o = go_get(hh); if(!o) return 0;
  o->w = w; o->h = h; o->pix = heap_alloc((u64)w * h * 4); if(!o->pix){ o->used = 0; return 0; }
  if(bits && bpp == 32) memcpy(o->pix, bits, (u64)w * h * 4); return hh;
}
static u64 WINAPI w_CreateCompatibleBitmap(u64 dc, i32 w, i32 h){
  struct wnd *s = 0; if(dc >= DCBASE && dc < DCBASE + MAXW) s = &W[dc - DCBASE];
  u64 hh = w_CreateBitmap(w, h, 1, 32, 0); struct gobj *o = go_get(hh); if(o && s && s->pix) for(u64 i = 0; i < (u64)w * h; i++) o->pix[i] = 0xFFFFFF; return hh;
}
static i32 WINAPI w_DeleteObject(u64 h){
  struct gobj *o = go_get(h); if(!o) return h != 0; if(h == stock_obj[0] || h == stock_obj[4]) return 1;
  for(int i = 0; i < 24; i++) if(stock_obj[i] == h) return 1;
  for(int i = 0; i < MAXW; i++) if(dcs[i].init){ if(dcs[i].font == h) dcs[i].font = 0; if(dcs[i].pen == h) dcs[i].pen = 0; if(dcs[i].brush == h) dcs[i].brush = 0; if(dcs[i].bmp == h) return 1; }
  if(o->pix) heap_free(o->pix); o->used = 0; return 1;
}
static u64 WINAPI w_CreateCompatibleDC(u64 dc){
  (void)dc; int idx = -1; for(int i = 0; i < MAXW; i++) if(!W[i].used){ idx = i; break; } if(idx < 0) return 0;
  struct wnd *w = &W[idx]; memset(w, 0, sizeof *w); w->used = 1; w->kind = K_MEM; w->owner = cur->pid; w->visible = 0; w->w = w->h = 1; w->seq = ++g_seq;
  dcs[idx].init = 0; struct dcst *d = dc_state(DCBASE + idx); d->mem = 1; return DCBASE + idx;
}
static i32 WINAPI w_DeleteDC(u64 dc){ if(dc < DCBASE || dc >= DCBASE + MAXW) return 0; struct wnd *w = &W[dc - DCBASE]; if(w->used && w->kind == K_MEM){ w->used = 0; w->pix = 0; dcs[dc - DCBASE].init = 0; } return 1; }
static u64 WINAPI w_SelectObject(u64 dc, u64 o){
  struct dcst *d = dc_state(dc); if(!d) return 0; struct gobj *g = go_get(o); if(!g) return 0; u64 old = 0;
  switch(g->type){
    case GT_FONT: old = d->font; d->font = o; break;
    case GT_PEN: old = d->pen; d->pen = o; break;
    case GT_BRUSH: old = d->brush; d->brush = o; break;
    case GT_BITMAP: if(d->mem){ struct wnd *w = &W[dc - DCBASE]; old = d->bmp; d->bmp = o; w->pix = g->pix; w->pw = g->w; w->ph = g->h; w->stride = g->w; w->w = g->w; w->h = g->h; } break;
    case GT_PALETTE: return o;
  }
  return old ? old : (g->type == GT_FONT ? stock(13) : g->type == GT_PEN ? stock(7) : g->type == GT_BRUSH ? stock(0) : 0x2F10);
}
static u64 WINAPI w_GetCurrentObject(u64 dc, u32 type){ struct dcst *d = dc_state(dc); if(!d) return 0; return type == 1 ? (d->pen ? d->pen : stock(7)) : type == 2 ? (d->brush ? d->brush : stock(0)) : type == 6 ? (d->font ? d->font : stock(13)) : type == 7 ? d->bmp : 0; }
static i32 WINAPI w_GetObjectA(u64 h, i32 n, void *buf){
  struct gobj *o = go_get(h); if(!o || !buf) return 0;
  if(o->type == GT_BITMAP && n >= 32){ memset(buf, 0, 32); i32 *b = buf; b[1] = o->w; b[2] = o->h; b[3] = o->w * 4; ((u16 *)buf)[8] = 1; ((u16 *)buf)[9] = 32; *(void **)((u8 *)buf + 24) = o->pix; return 32; }
  if(o->type == GT_FONT && n >= 60){ memset(buf, 0, 60); i32 *b = buf; b[0] = -o->px; b[4] = o->penw ? o->penw : 400; strcpy_k((char *)buf + 28, o->mono ? "Courier New" : "Open Sans"); return 60; }
  return 0;
}
static u32 WINAPI w_SetTextColor(u64 dc, u32 c){ struct dcst *d = dc_state(dc); if(!d) return 0; u32 o = d->fg; d->fg = cref(c); return cref(o); }
static u32 WINAPI w_SetBkColor(u64 dc, u32 c){ struct dcst *d = dc_state(dc); if(!d) return 0; u32 o = d->bg; d->bg = cref(c); return cref(o); }
static i32 WINAPI w_SetBkMode(u64 dc, i32 m){ struct dcst *d = dc_state(dc); if(!d) return 0; i32 o = d->bkmode; d->bkmode = (u8)m; return o; }
static i32 WINAPI w_GetBkMode(u64 dc){ struct dcst *d = dc_state(dc); return d ? d->bkmode : 2; }
static u32 WINAPI w_SetTextAlign(u64 dc, u32 a){ struct dcst *d = dc_state(dc); if(!d) return 0; u32 o = d->align; d->align = (u8)a; return o; }
static i32 WINAPI w_SetMapMode(u64 dc, i32 m){ (void)dc; (void)m; return 1; }
static u32 WINAPI w_GetTextColor(u64 dc){ struct dcst *d = dc_state(dc); return d ? cref(d->fg) : 0; }
static i32 WINAPI w_GetDeviceCaps(u64 dc, i32 i){
  (void)dc; switch(i){ case 2: return 1; case 4: return 338; case 6: return 190; case 8: return wm_screen_w(); case 10: return wm_screen_h(); case 12: return 32; case 14: return 1; case 24: return -1; case 38: return 0x7E99;
    case 40: case 42: return 36; case 44: return 51; case 88: case 90: return 96; case 104: return 0; case 108: return 24; default: return 0; }
}
/* palettes are not needed for 32-bit colour */
static u32 WINAPI w_RealizePalette(u64 dc){ (void)dc; return 0; }
static u64 WINAPI w_SelectPalette(u64 dc, u64 p, i32 bk){ (void)dc; (void)bk; return p; }
static u32 WINAPI w_SetPaletteEntries(u64 p, u32 a, u32 n, const void *e){ (void)p; (void)a; (void)e; return n; }
static i32 WINAPI w_UnrealizeObject(u64 h){ (void)h; return 1; }
static i32 WINAPI w_UpdateColors(u64 dc){ (void)dc; return 1; }
static i32 WINAPI w_ExcludeClipRect(u64 dc, i32 a, i32 b, i32 c, i32 d){ (void)dc; (void)a; (void)b; (void)c; (void)d; return 2; }
static i32 WINAPI w_IntersectClipRect(u64 dc, i32 a, i32 b, i32 c, i32 d){ (void)dc; (void)a; (void)b; (void)c; (void)d; return 2; }
static i32 WINAPI w_GetDIBits(u64 dc, u64 bmp, u32 a, u32 b, void *bits, void *bi, u32 u){ (void)dc; (void)bmp; (void)a; (void)b; (void)bits; (void)bi; (void)u; return 0; }

/* ---- drawing ---- */
static void px_put(struct wnd *s, int x, int y, u32 c){ if(x >= 0 && y >= 0 && x < s->pw && y < s->ph) s->pix[(u64)y * s->stride + x] = c; }
static u32 pen_color(struct dcst *d, int *skip){ struct gobj *p = go_get(d->pen); if(p){ *skip = p->style == 5; return p->color; } *skip = 0; return 0; }
static void draw_line(struct wnd *s, int x0, int y0, int x1, int y1, u32 c, int pw){
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0; int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1; int err = dx + dy;
  for(int n = 0; n < 20000; n++){
    for(int a = 0; a < pw; a++) for(int b = 0; b < pw; b++) px_put(s, x0 + a - pw / 2, y0 + b - pw / 2, c);
    if(x0 == x1 && y0 == y1) break; int e2 = 2 * err; if(e2 >= dy){ err += dy; x0 += sx; } if(e2 <= dx){ err += dx; y0 += sy; }
  }
}
static i32 WINAPI w_MoveToEx(u64 dc, i32 x, i32 y, i32 *old){ struct dcst *d = dc_state(dc); if(!d) return 0; if(old){ old[0] = d->px; old[1] = d->py; } d->px = x; d->py = y; return 1; }
static i32 WINAPI w_LineTo(u64 dc, i32 x, i32 y){
  struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s) return 0; int skip; u32 c = pen_color(d, &skip); struct gobj *p = go_get(d->pen);
  if(!skip) draw_line(s, d->px, d->py, x, y, c, p ? p->penw : 1); d->px = x; d->py = y; dirty = 1; return 1;
}
static i32 WINAPI w_Polyline(u64 dc, const i32 *pts, i32 n){ struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s || n < 2) return 0; int skip; u32 c = pen_color(d, &skip); struct gobj *p = go_get(d->pen);
  if(!skip) for(int i = 0; i + 1 < n; i++) draw_line(s, pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3], c, p ? p->penw : 1); dirty = 1; return 1; }
static i32 WINAPI w_Rectangle(u64 dc, i32 l, i32 t, i32 r, i32 b){
  struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s) return 0; struct gobj *br = go_get(d->brush ? d->brush : stock(0)); int skip; u32 pc = pen_color(d, &skip);
  if(br && !br->hollow) wm_gdi_rect(dc, l + (skip ? 0 : 1), t + (skip ? 0 : 1), r - l - (skip ? 0 : 2), b - t - (skip ? 0 : 2), br->color);
  if(!skip){ wm_gdi_rect(dc, l, t, r - l, 1, pc); wm_gdi_rect(dc, l, b - 1, r - l, 1, pc); wm_gdi_rect(dc, l, t, 1, b - t, pc); wm_gdi_rect(dc, r - 1, t, 1, b - t, pc); }
  dirty = 1; return 1;
}
static i32 WINAPI w_FillRect(u64 dc, const i32 *r, u64 br){
  u32 col; struct gobj *o = go_get(br);
  if(o && o->type == GT_BRUSH){ if(o->hollow) return 1; col = o->color; }
  else if(br < 32){ static const u32 sc[] = {0xC8C8C8, 0x6B4A1F, 0xFFFFFF, 0xFFFFFF, 0xF0F0F0, 0xFFFFFF, 0x646464, 0, 0, 0, 0xFFFFFF, 0xFFFFFF, 0xABABAB, 0xD77800, 0xFFFFFF, 0xF0F0F0, 0xA0A0A0, 0x6D6D6D, 0, 0xF0F0F0}; col = br >= 1 && br - 1 < sizeof sc / sizeof *sc ? sc[br - 1] : 0xF0F0F0; }
  else return 0;
  wm_gdi_rect(dc, r[0], r[1], r[2] - r[0], r[3] - r[1], col); return 1;
}
static i32 WINAPI w_PatBlt(u64 dc, i32 x, i32 y, i32 w, i32 h, u32 rop){
  struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s) return 0;
  if(rop == 0x42) wm_gdi_rect(dc, x, y, w, h, 0); else if(rop == 0xFF0062) wm_gdi_rect(dc, x, y, w, h, 0xFFFFFF);
  else if(rop == 0xF00021){ struct gobj *br = go_get(d->brush ? d->brush : stock(0)); if(br && !br->hollow) wm_gdi_rect(dc, x, y, w, h, br->color); }
  else if(rop == 0x550009){ for(int j = 0; j < h; j++) for(int i = 0; i < w; i++){ int px = x + i, py = y + j; if(px >= 0 && py >= 0 && px < s->pw && py < s->ph) s->pix[(u64)py * s->stride + px] ^= 0xFFFFFF; } }
  dirty = 1; return 1;
}
static u32 WINAPI w_GetPixel(u64 dc, i32 x, i32 y){ struct wnd *s = dc_target(dc); if(!s || x < 0 || y < 0 || x >= s->pw || y >= s->ph) return 0xFFFFFFFFu; return cref(s->pix[(u64)y * s->stride + x] & 0xFFFFFF); }
static u32 WINAPI w_SetPixel(u64 dc, i32 x, i32 y, u32 c){ wm_gdi_pixel(dc, x, y, cref(c)); return c; }
static i32 WINAPI w_BitBlt(u64 dst, i32 dx, i32 dy, i32 w, i32 h, u64 src, i32 sx, i32 sy, u32 rop){
  struct wnd *d = dc_target(dst); if(!d) return 0;
  if(rop == 0x42 || rop == 0xFF0062 || rop == 0xF00021 || rop == 0x550009) return w_PatBlt(dst, dx, dy, w, h, rop);
  struct wnd *s = dc_target(src); if(!s) return 0;
  for(int j = 0; j < h; j++){
    int y = dy + j, yy = sy + j; if(y < 0 || y >= d->ph || yy < 0 || yy >= s->ph) continue;
    for(int i = 0; i < w; i++){
      int x = dx + i, xx = sx + i; if(x < 0 || x >= d->pw || xx < 0 || xx >= s->pw) continue;
      u32 sp = s->pix[(u64)yy * s->stride + xx] & 0xFFFFFF; u32 *dp = &d->pix[(u64)y * d->stride + x];
      switch(rop){ case 0x660046: *dp ^= sp; break; case 0x8800C6: *dp &= sp; break; case 0xEE0086: *dp |= sp; break; case 0x330008: *dp = ~sp & 0xFFFFFF; break; default: *dp = sp; }
    }
  }
  dirty = 1; return 1;
}
static i32 WINAPI w_StretchBlt(u64 dst, i32 dx, i32 dy, i32 dw, i32 dh, u64 src, i32 sx, i32 sy, i32 sw, i32 sh, u32 rop){
  (void)rop; struct wnd *d = dc_target(dst), *s = dc_target(src); if(!d || !s || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 0;
  for(int j = 0; j < dh; j++){ int y = dy + j; if(y < 0 || y >= d->ph) continue; int yy = sy + j * sh / dh; if(yy < 0 || yy >= s->ph) continue;
    for(int i = 0; i < dw; i++){ int x = dx + i; if(x < 0 || x >= d->pw) continue; int xx = sx + i * sw / dw; if(xx < 0 || xx >= s->pw) continue; d->pix[(u64)y * d->stride + x] = s->pix[(u64)yy * s->stride + xx]; } }
  dirty = 1; return 1;
}

/* ---- text ---- */
static int text_w(const struct font *f, const char *s, int n){ return ui_width(f, s, n); }
static int utf8_put_k(char *o, unsigned cp){ if(cp < 0x80){ o[0] = (char)cp; return 1; } if(cp < 0x800){ o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); return 2; } o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
static i32 do_text(u64 dc, int x, int y, const char *s, int n, const i32 *dxs, u32 opts, const i32 *rc){
  struct dcst *d = dc_state(dc); struct wnd *t = dc_target(dc); if(!d || !t) return 0; const struct font *f = font_of_dc(d); if(n < 0) n = (int)strlen(s);
  int tw = 0; if(dxs){ for(int i = 0; i < n; i++) tw += dxs[i]; } else tw = text_w(f, s, n);
  int lh = ui_lineh(f); u32 al = d->align;
  if((al & 6) == 6) x -= tw / 2; else if(al & 2) x -= tw;
  if((al & 24) == 24) y -= ui_ascent(f); else if(al & 8) y -= lh;
  if(rc && (opts & 2)) wm_gdi_rect(dc, rc[0], rc[1], rc[2] - rc[0], rc[3] - rc[1], d->bg);
  else if(d->bkmode == 2 && !(opts & 2)) wm_gdi_rect(dc, x, y, tw, lh, d->bg);
  if(dxs){ int cx = x; for(int i = 0; i < n; i++){ int cl = 1; while(i + cl < n && ((unsigned char)s[i + cl] & 0xC0) == 0x80) cl++; if(s[i] != ' ') ui_text_to(f, t->pix, t->stride, t->pw, t->ph, cx, y, s + i, cl, d->fg); cx += dxs[i]; i += cl - 1; } }
  else ui_text_to(f, t->pix, t->stride, t->pw, t->ph, x, y, s, n, d->fg);
  dirty = 1; return 1;
}
static i32 WINAPI w_TextOutA(u64 dc, i32 x, i32 y, const char *s, i32 n){ return do_text(dc, x, y, s, n, 0, 0, 0); }
static i32 WINAPI w_TextOutW(u64 dc, i32 x, i32 y, const u16 *s, i32 n){ char a[600]; if(n < 0){ n = 0; while(s[n]) n++; } if(n > 590) n = 590; for(int i = 0; i < n; i++) a[i] = s[i] < 0x80 ? (char)s[i] : '?'; return do_text(dc, x, y, a, n, 0, 0, 0); }
static i32 WINAPI w_ExtTextOutA(u64 dc, i32 x, i32 y, u32 opts, const i32 *rc, const char *s, u32 n, const i32 *dxs){ if(!s){ if(rc && (opts & 2)){ struct dcst *d = dc_state(dc); if(d) wm_gdi_rect(dc, rc[0], rc[1], rc[2] - rc[0], rc[3] - rc[1], d->bg); } return 1; } return do_text(dc, x, y, s, (int)n, dxs, opts, rc); }
static i32 WINAPI w_ExtTextOutW(u64 dc, i32 x, i32 y, u32 opts, const i32 *rc, const u16 *s, u32 n, const i32 *dxs){
  if(!s){ return w_ExtTextOutA(dc, x, y, opts, rc, 0, 0, 0); }
  char a[1200]; i32 dd[1200]; int k = 0; for(u32 i = 0; i < n && k < 1190; i++){ u16 c = s[i]; if(c < 0x80) a[k++] = (char)c; else { char t[4]; int l = utf8_put_k(t, c); for(int q = 0; q < l && k < 1190; q++){ a[k] = t[q]; dd[k] = q == 0 ? (dxs ? dxs[i] : 0) : 0; k++; } continue; } dd[k - 1] = dxs ? dxs[i] : 0; }
  return do_text(dc, x, y, a, k, dxs ? dd : 0, opts, rc);
}
static i32 WINAPI w_GetTextExtentPoint32A(u64 dc, const char *s, i32 n, i32 *sz){ struct dcst *d = dc_state(dc); const struct font *f = font_of_dc(d); if(n < 0) n = (int)strlen(s); sz[0] = text_w(f, s, n); sz[1] = ui_lineh(f); return 1; }
static i32 WINAPI w_GetTextExtentPoint32W(u64 dc, const u16 *s, i32 n, i32 *sz){ char a[600]; if(n < 0){ n = 0; while(s[n]) n++; } if(n > 590) n = 590; for(int i = 0; i < n; i++) a[i] = s[i] < 0x80 ? (char)s[i] : '?'; return w_GetTextExtentPoint32A(dc, a, n, sz); }
static i32 WINAPI w_GetTextExtentExPointA(u64 dc, const char *s, i32 n, i32 maxw, i32 *fit, i32 *dxs, i32 *sz){
  struct dcst *d = dc_state(dc); const struct font *f = font_of_dc(d); int cnt = 0; int total = 0;
  for(int i = 0; i < n; i++){ int w = text_w(f, s, i + 1); if(maxw > 0 && w > maxw) break; if(dxs) dxs[i] = w; total = w; cnt = i + 1; }
  if(fit) *fit = cnt; if(sz){ sz[0] = total; sz[1] = ui_lineh(f); } return 1;
}
static i32 WINAPI w_GetTextMetricsA(u64 dc, i32 *tm){
  struct dcst *d = dc_state(dc); const struct font *f = font_of_dc(d); struct gobj *o = d ? go_get(d->font) : 0; memset(tm, 0, 60);
  tm[0] = ui_lineh(f); tm[1] = ui_ascent(f); tm[2] = ui_lineh(f) - ui_ascent(f); tm[5] = ui_width(f, "x", 1); tm[6] = ui_width(f, "W", 1); tm[7] = o && o->penw ? o->penw : 400; tm[9] = 96; tm[10] = 96;
  u8 *b = (u8 *)tm; b[44] = 32; b[45] = 255; b[46] = '?'; b[47] = ' '; b[51] = (o && o->mono) ? 0 : 1; b[52] = 0; return 1;
}
static i32 WINAPI w_GetTextMetricsW(u64 dc, i32 *tm){
  struct dcst *d = dc_state(dc); const struct font *f = font_of_dc(d); struct gobj *o = d ? go_get(d->font) : 0; memset(tm, 0, 64);
  tm[0] = ui_lineh(f); tm[1] = ui_ascent(f); tm[2] = ui_lineh(f) - ui_ascent(f); tm[5] = ui_width(f, "x", 1); tm[6] = ui_width(f, "W", 1); tm[7] = o && o->penw ? o->penw : 400; tm[9] = 96; tm[10] = 96;
  u16 *w = (u16 *)(tm + 11); w[0] = 32; w[1] = 255; w[2] = '?'; w[3] = ' '; ((u8 *)tm)[52] = 0; ((u8 *)tm)[55] = (o && o->mono) ? 0 : 1; return 1;
}
static i32 char_widths(u64 dc, u32 first, u32 last, i32 *out){ struct dcst *d = dc_state(dc); const struct font *f = font_of_dc(d); for(u32 c = first; c <= last && c - first < 65536; c++){ char b[4]; int l = c < 0x80 ? (b[0] = (char)c, 1) : utf8_put_k(b, c); out[c - first] = ui_width(f, b, l); } return 1; }
static i32 WINAPI w_GetCharWidth32A(u64 dc, u32 a, u32 b, i32 *o){ return char_widths(dc, a, b, o); }
static i32 WINAPI w_GetCharWidthA(u64 dc, u32 a, u32 b, i32 *o){ return char_widths(dc, a, b, o); }
static i32 WINAPI w_GetCharABCWidthsFloatA(u64 dc, u32 a, u32 b, u32 *out){            /* ABCFLOAT: three floats (a=0, b=advance, c=0); IEEE bits written by hand */
  struct dcst *d = dc_state(dc); const struct font *f = font_of_dc(d);
  for(u32 c = a; c <= b && c - a < 256; c++){ char ch = (char)c; int w = ui_width(f, &ch, 1); u32 bits = 0; if(w > 0){ int e = 31 - __builtin_clz((unsigned)w); u32 m = ((u32)w << (23 - e)) & 0x7FFFFF; bits = ((u32)(127 + e) << 23) | m; } out[(c - a) * 3] = 0; out[(c - a) * 3 + 1] = bits; out[(c - a) * 3 + 2] = 0; }
  return 1;
}
static u32 WINAPI w_GetCharacterPlacementW(u64 dc, const u16 *s, i32 n, i32 mx, void *res, u32 fl){ (void)dc; (void)s; (void)n; (void)mx; (void)res; (void)fl; return 0; }
static u32 WINAPI w_GetOutlineTextMetricsA(u64 dc, u32 n, void *buf){ (void)dc; (void)n; (void)buf; return 0; }
static u32 WINAPI w_TranslateCharsetInfo(u32 *src, void *cs, u32 fl){ (void)src; (void)fl; if(cs) memset(cs, 0, 32); return 1; }
static i32 WINAPI w_DrawFocusRect(u64 dc, const i32 *r){ struct wnd *s = dc_target(dc); if(!s) return 0; for(int x = r[0]; x < r[2]; x += 2){ px_put(s, x, r[1], 0); px_put(s, x, r[3] - 1, 0); } for(int y = r[1]; y < r[3]; y += 2){ px_put(s, r[0], y, 0); px_put(s, r[2] - 1, y, 0); } dirty = 1; return 1; }
static i32 WINAPI w_DrawEdge(u64 dc, i32 *r, u32 edge, u32 flags){ (void)edge; (void)flags; wm_gdi_rect(dc, r[0], r[1], r[2] - r[0], 1, 0x808080); wm_gdi_rect(dc, r[0], r[1], 1, r[3] - r[1], 0x808080); wm_gdi_rect(dc, r[0], r[3] - 1, r[2] - r[0], 1, 0xFFFFFF); wm_gdi_rect(dc, r[2] - 1, r[1], 1, r[3] - r[1], 0xFFFFFF); return 1; }
int win32_messagebox(const char *text, const char *caption, unsigned type);
static u32 sys_color(int i){ static const u32 c[] = {0xC8C8C8,0x6B4A1F,0xFFFFFF,0xFFFFFF,0xF0F0F0,0xFFFFFF,0x646464,0,0,0,0xFFFFFF,0xFFFFFF,0xABABAB,0xD77800,0xFFFFFF,0xF0F0F0,0xA0A0A0,0x6D6D6D,0,0xF0F0F0}; return (unsigned)i < sizeof c / sizeof *c ? c[i] : 0xF0F0F0; }
static u64 WINAPI w_GetSysColorBrush(i32 i){ static u64 sb[32]; if(i < 0 || i >= 32) i = 0; if(!sb[i]) sb[i] = mk_brush(cref(sys_color(i)), 0); return sb[i]; }

/* ---- timers ---- */
struct timer_ent { u8 used, owner; u64 hwnd, id, proc; u32 ms, last; };
static struct timer_ent timers[32];
static u64 WINAPI w_SetTimer(u64 hwnd, u64 id, u32 ms, u64 proc){
  if(hwnd) { for(int i = 0; i < 32; i++) if(timers[i].used && timers[i].hwnd == hwnd && timers[i].id == id){ timers[i].ms = ms; timers[i].last = ticks; timers[i].proc = proc; return id; } }
  for(int i = 0; i < 32; i++) if(!timers[i].used){ timers[i].used = 1; timers[i].owner = (u8)cur->pid; timers[i].hwnd = hwnd; timers[i].id = hwnd ? id : (u64)(0x100 + i); timers[i].proc = proc; timers[i].ms = ms ? ms : 1; timers[i].last = ticks; return timers[i].id; }
  return 0;
}
static i32 WINAPI w_KillTimer(u64 hwnd, u64 id){ for(int i = 0; i < 32; i++) if(timers[i].used && timers[i].hwnd == hwnd && timers[i].id == id){ timers[i].used = 0; return 1; } return 0; }
static void timers_service(void){
  for(int i = 0; i < 32; i++){ struct timer_ent *t = &timers[i]; if(!t->used || t->owner != cur->pid) continue;
    if((u32)(ticks - t->last) >= t->ms){ t->last = ticks;
      if(t->hwnd && !get(t->hwnd)){ t->used = 0; continue; }
      if(t->proc) ((void (WINAPI *)(u64, u32, u64, u32))t->proc)(t->hwnd, 0x113, t->id, ticks);
      else if(t->hwnd) post(t->hwnd, 0x113, t->id, 0);
      else { post(top_visible(), 0x113, t->id, 0); } } }
}
/* ---- capture, clipboard, keyboard, placement ---- */
static u64 WINAPI w_SetCapture(u64 h){ u64 o = g_capture; g_capture = h; return o; }
static i32 WINAPI w_ReleaseCapture(void){ g_capture = 0; return 1; }
static u64 WINAPI w_GetCapture(void){ return g_capture; }
static char clip_buf[16384]; static int clip_len; static u32 clip_fmt;
static i32 WINAPI w_OpenClipboard(u64 h){ (void)h; return 1; }
static i32 WINAPI w_CloseClipboard(void){ return 1; }
static i32 WINAPI w_EmptyClipboard(void){ clip_len = 0; clip_fmt = 0; return 1; }
static u64 WINAPI w_SetClipboardData(u32 fmt, u64 data){ if(fmt == 1 && data){ const char *s = (const char *)data; int n = (int)strlen(s); if(n > (int)sizeof clip_buf - 1) n = (int)sizeof clip_buf - 1; memcpy(clip_buf, s, n); clip_buf[n] = 0; clip_len = n; clip_fmt = 1; } return data; }
static u64 WINAPI w_GetClipboardData(u32 fmt){ return fmt == 1 && clip_fmt == 1 ? (u64)clip_buf : 0; }
static u64 WINAPI w_GetClipboardOwner(void){ return 0; }
static u32 WINAPI w_RegisterClipboardFormatA(const char *n){ (void)n; static u32 next = 0xC000; return next++; }
static u32 WINAPI w_RegisterWindowMessageA(const char *n){ (void)n; static u32 next = 0xC100; return next++; }
static i32 WINAPI w_CreateCaret(u64 h, u64 b, i32 w, i32 hh){ (void)h; (void)b; (void)w; (void)hh; return 1; }
static i32 WINAPI w_Caret0(void){ return 1; }
static u32 WINAPI w_GetCaretBlinkTime(void){ return 500; }
static u32 WINAPI w_GetDoubleClickTime(void){ return 500; }
static u32 WINAPI w_GetMessageTime(void){ return ticks; }
static u64 WINAPI w_GetKeyboardLayout(u32 t){ (void)t; return 0x04090409; }
static u64 WINAPI w_GetForegroundWindow(void){ return top_visible(); }
static i32 WINAPI w_SetForegroundWindow(u64 h){ if(get(h)){ restore_and_focus(h); } return 1; }
static u64 WINAPI w_SetActiveWindow(u64 h){ u64 o = top_visible(); if(get(h)) restore_and_focus(h); return o; }
static i32 WINAPI w_FlashWindow(u64 h, i32 inv){ (void)h; (void)inv; return 0; }
static u64 WINAPI w_FindWindowA(const char *c, const char *t){ (void)c; (void)t; return 0; }
static i32 WINAPI w_IsIconic(u64 h){ struct wnd *w = get(h); return w && w->minimized; }
static i32 WINAPI w_IsZoomed(u64 h){ struct wnd *w = get(h); return w && w->maximized; }
static i32 WINAPI w_GetKeyboardState(u8 *st){ memset(st, 0, 256); for(int vk = 1; vk < 256; vk++) if(kb_vk(vk)) st[vk] = 0x80; return 1; }
static i32 WINAPI w_SetKeyboardState(const u8 *st){ (void)st; return 1; }
static i32 WINAPI w_ToAsciiEx(u32 vk, u32 sc, const u8 *st, u16 *out, u32 fl, u64 hkl){
  (void)sc; (void)fl; (void)hkl; int shift = st[0x10] & 0x80, caps = st[0x14] & 1, ctrl = st[0x11] & 0x80, alt = st[0x12] & 0x80; (void)alt; int c = 0;
  if(vk >= 0x41 && vk <= 0x5A){ c = (shift ^ caps) ? (int)vk : (int)vk + 32; if(ctrl) c = (int)vk - 0x40; }
  else if(vk >= 0x30 && vk <= 0x39){ static const char sh[] = ")!@#$%^&*("; c = shift ? sh[vk - 0x30] : (int)vk; }
  else if(vk == 0x20) c = ' '; else if(vk == 0x0D) c = 13; else if(vk == 0x08) c = 8; else if(vk == 0x09) c = 9; else if(vk == 0x1B) c = 27;
  else { static const struct { u8 vk; char n, s; } oem[] = {{0xBA,';',':'},{0xBB,'=','+'},{0xBC,',','<'},{0xBD,'-','_'},{0xBE,'.','>'},{0xBF,'/','?'},{0xC0,'`','~'},{0xDB,'[','{'},{0xDC,'\\','|'},{0xDD,']','}'},{0xDE,'\'','"'}};
    for(unsigned i = 0; i < sizeof oem / sizeof *oem; i++) if(oem[i].vk == vk) c = shift ? oem[i].s : oem[i].n; }
  if(!c) return 0; out[0] = (u16)c; return 1;
}
static i32 WINAPI w_ToUnicodeEx(u32 vk, u32 sc, const u8 *st, u16 *out, i32 cch, u32 fl, u64 hkl){ (void)cch; return w_ToAsciiEx(vk, sc, st, out, fl, hkl); }
static i32 WINAPI w_ToUnicode(u32 vk, u32 sc, const u8 *st, u16 *out, i32 cch, u32 fl){ (void)cch; return w_ToAsciiEx(vk, sc, st, out, fl, 0); }
static i32 WINAPI w_ScreenToClient(u64 h, i32 *pt){ struct wnd *w = get(h); if(!w) return 0; int cx, cy; client_origin(w, &cx, &cy); pt[0] -= cx; pt[1] -= cy; return 1; }
static i32 WINAPI w_ClientToScreen(u64 h, i32 *pt){ struct wnd *w = get(h); if(!w) return 0; int cx, cy; client_origin(w, &cx, &cy); pt[0] += cx; pt[1] += cy; return 1; }
static i32 WINAPI w_SetWindowPos(u64 h, u64 after, i32 x, i32 y, i32 cx, i32 cy, u32 fl){
  struct wnd *w = get(h); (void)after; if(!w) return 0;
  if(!(fl & 2)){ w->x = x; w->y = y; } if(!(fl & 1)){ int resized = (w->w != cx || w->h != cy); w->w = cx; w->h = cy; if(resized && w->kind == K_APP){ int a, b; client_size(w, &a, &b); send(h, WM_SIZE, 0, ((i64)b << 16) | (a & 0xFFFF)); invalidate(w); } }
  if(fl & 0x40){ w->visible = 1; on_show(w); } if(fl & 0x80) w->visible = 0; dirty = 1; return 1;
}
static i32 WINAPI w_GetWindowPlacement(u64 h, u32 *wp){ struct wnd *w = get(h); if(!w) return 0; memset(wp + 1, 0, 40); wp[0] = 44; wp[2] = w->minimized ? 2 : w->maximized ? 3 : 1;
  i32 *rc = (i32 *)(wp + 7); rc[0] = w->maximized ? w->rx : w->x; rc[1] = w->maximized ? w->ry : w->y; rc[2] = rc[0] + (w->maximized ? w->rw : w->w); rc[3] = rc[1] + (w->maximized ? w->rh : w->h); return 1; }
static i32 WINAPI w_SetWindowPlacement(u64 h, const u32 *wp){ struct wnd *w = get(h); if(!w) return 0; const i32 *rc = (const i32 *)(wp + 7); w->x = rc[0]; w->y = rc[1]; w->w = rc[2] - rc[0]; w->h = rc[3] - rc[1]; dirty = 1; return 1; }
static i32 WINAPI w_SystemParametersInfoA(u32 act, u32 p, void *pv, u32 fl){ (void)p; (void)fl;
  if(act == 0x30 && pv){ i32 *r = pv; r[0] = 0; r[1] = 0; r[2] = wm_screen_w(); r[3] = wm_screen_h(); return 1; }
  if(act == 0x68 && pv){ *(u32 *)pv = 3; return 1; } return 0; }
static i32 WINAPI w_SetScrollInfo(u64 h, i32 bar, const u32 *si, i32 redraw){ (void)h; (void)bar; (void)si; (void)redraw; return 0; }
static i32 WINAPI w_GetScrollInfo(u64 h, i32 bar, u32 *si){ (void)h; (void)bar; (void)si; return 0; }
static u64 WINAPI w_SetClassLongPtrA(u64 h, i32 i, u64 v){ (void)h; (void)i; (void)v; return 0; }
extern int kobj_signaled(u64 h);
static u32 WINAPI w_MsgWaitForMultipleObjects(u32 n, const u64 *hs, i32 all, u32 ms, u32 mask){
  (void)mask; u32 t0 = ticks;
  for(;;){
    wm_service();
    for(u32 i = 0; i < n; i++){ if(kobj_signaled(hs[i])) return i; }
    if(cur->qh != cur->qt) return n;
    if(ms != 0xFFFFFFFFu && (u32)(ticks - t0) >= ms) return 0x102;
    (void)all; idle_wait();
  }
}
static u32 WINAPI w_GetQueueStatus(u32 f){ return cur->qh != cur->qt ? (f << 16) | f : 0; }
static i32 WINAPI w_InitCommonControls(void){ return 1; }
static i32 WINAPI w_MessageBoxIndirectW(const u8 *mb){ const u16 *t = *(const u16 **)(mb + 24), *c = *(const u16 **)(mb + 32); u32 style = *(const u32 *)(mb + 40); char a[400], b[100]; wd_to_a(t, a, sizeof a); wd_to_a(c, b, sizeof b); return win32_messagebox(a, b, style); }
/* ---- menus ---- */
static u64 sysmenu_of[MAXW];
static u64 mh(int i){ return 0x8100 + (u64)i * 4; }
static struct menu *menu_get(u64 h){ if(h < 0x8100 || ((h - 0x8100) & 3)) return 0; u64 i = (h - 0x8100) >> 2; return (i < 32 && menus[i].used) ? &menus[i] : 0; }
static u64 WINAPI w_CreateMenu(void){ for(int i = 0; i < 32; i++) if(!menus[i].used){ memset(&menus[i], 0, sizeof menus[i]); menus[i].used = 1; return mh(i); } return 0; }
static i32 WINAPI w_DestroyMenu(u64 h){ struct menu *m = menu_get(h); if(m) m->used = 0; return 1; }
static i32 menu_insert(u64 h, int pos, u32 flags, u64 id, const char *text){
  struct menu *m = menu_get(h); if(!m || m->n >= 40) return 0; if(pos < 0 || pos > m->n) pos = m->n;
  for(int i = m->n; i > pos; i--) m->it[i] = m->it[i - 1];
  struct menu_item *it = &m->it[pos]; memset(it, 0, sizeof *it); it->flags = flags; it->id = id;
  if(!(flags & MF_SEPARATOR) && (u64)text > 0x10000){ int k = 0; for(; text[k] && k < 63; k++) it->text[k] = text[k]; it->text[k] = 0; }
  m->n++; return 1;
}
static i32 WINAPI w_AppendMenuA(u64 h, u32 fl, u64 id, const char *text){ return menu_insert(h, -1, fl, id, text); }
static i32 WINAPI w_AppendMenuW(u64 h, u32 fl, u64 id, const u16 *text){ char a[64]; if((fl & MF_SEPARATOR) || (u64)text < 0x10000) a[0] = 0; else wd_to_a(text, a, sizeof a); return menu_insert(h, -1, fl, id, a); }
static i32 menu_find(struct menu *m, u64 key, u32 flags){ if(flags & MF_BYPOSITION) return key < (u64)m->n ? (int)key : -1; for(int i = 0; i < m->n; i++) if(m->it[i].id == key && !(m->it[i].flags & MF_SEPARATOR)) return i; return -1; }
static i32 WINAPI w_InsertMenuA(u64 h, u32 pos, u32 fl, u64 id, const char *text){ struct menu *m = menu_get(h); if(!m) return 0; int p = (fl & MF_BYPOSITION) ? (int)pos : menu_find(m, pos, fl); return menu_insert(h, p, fl, id, text); }
static i32 WINAPI w_DeleteMenu(u64 h, u32 key, u32 fl){ struct menu *m = menu_get(h); if(!m) return 0; int i = menu_find(m, key, fl); if(i < 0) return 0; for(; i < m->n - 1; i++) m->it[i] = m->it[i + 1]; m->n--; return 1; }
static i32 WINAPI w_GetMenuItemCount(u64 h){ struct menu *m = menu_get(h); return m ? m->n : -1; }
static u32 WINAPI w_GetMenuItemID(u64 h, i32 pos){ struct menu *m = menu_get(h); if(!m || pos < 0 || pos >= m->n) return (u32)-1; return (u32)m->it[pos].id; }
static i32 WINAPI w_CheckMenuItem(u64 h, u32 key, u32 fl){ struct menu *m = menu_get(h); if(!m) return -1; int i = menu_find(m, key, fl); if(i < 0) return -1; int was = (m->it[i].flags & MF_CHECKED) != 0; if(fl & MF_CHECKED) m->it[i].flags |= MF_CHECKED; else m->it[i].flags &= ~(u32)MF_CHECKED; return was; }
static i32 WINAPI w_EnableMenuItem(u64 h, u32 key, u32 fl){ struct menu *m = menu_get(h); if(!m) return -1; int i = menu_find(m, key, fl); if(i < 0) return -1; m->it[i].flags = (m->it[i].flags & ~3u) | (fl & 3u); return 0; }
static u64 WINAPI w_GetSubMenu(u64 h, i32 pos){ struct menu *m = menu_get(h); if(!m || pos < 0 || pos >= m->n || !(m->it[pos].flags & MF_POPUP)) return 0; return m->it[pos].id; }
static u64 WINAPI w_GetSystemMenu(u64 h, i32 revert){
  struct wnd *w = get(h); if(!w) return 0; int wi = (int)(w - W);
  if(revert){ if(sysmenu_of[wi]) w_DestroyMenu(sysmenu_of[wi]); sysmenu_of[wi] = 0; return 0; }
  if(!sysmenu_of[wi] || !menu_get(sysmenu_of[wi])) sysmenu_of[wi] = w_CreateMenu(); return sysmenu_of[wi];
}
static i32 WINAPI w_SetMenuDefaultItem(u64 h, u32 k, u32 f){ (void)h; (void)k; (void)f; return 1; }
static i32 WINAPI w_Menu0(void){ return 1; }
static i32 WINAPI w_TrackPopupMenu(u64 mh_, u32 fl, i32 x, i32 y, i32 r, u64 hwnd, const void *rc){
  (void)r; (void)rc; struct menu *m = menu_get(mh_); struct wnd *par = get(hwnd); if(!m || !par || m->n == 0) return 0;
  int idx = -1; for(int i = 0; i < MAXW; i++) if(!W[i].used){ idx = i; break; } if(idx < 0) return 0;
  int rh = ui_lineh(ui(FS_REGULAR, 13)) + 6; int wd = 120; const struct font *f = ui(FS_REGULAR, 13);
  for(int i = 0; i < m->n; i++){ int tw = ui_width(f, m->it[i].text, (int)strlen(m->it[i].text)) + 40; if(tw > wd) wd = tw; }
  int cx, cy; client_origin(par, &cx, &cy);
  struct wnd *p = &W[idx]; memset(p, 0, sizeof *p); p->used = 1; p->owner = cur->pid; p->kind = K_CTRL; p->ctype = CT_LIST; p->proc = B_CTRL; p->style = WS_CHILD | WS_VISIBLE | 0x80000000u; p->parent = hwnd;
  p->x = x - cx; p->y = y - cy; p->w = wd; p->h = m->n * rh + 4; p->id = 0xC0DD; p->visible = 1; p->seq = ++g_seq; p->check = (int)(m - menus);
  struct lst *pl = lst_of(p); pl->sel = -1; for(int i = 0; i < m->n; i++){ char b[80]; int k = 0; if(m->it[i].flags & MF_CHECKED){ b[k++] = '*'; b[k++] = ' '; } const char *src = (m->it[i].flags & MF_SEPARATOR) ? "-------" : m->it[i].text; for(int q = 0; src[q] && k < 78; q++) b[k++] = src[q]; b[k] = 0; lst_insert(pl, -1, b); }
  g_menu_result = 0; g_menu_done = 0; u64 ph = hof(p); set_focus(ph); dirty = 1;
  while(!g_menu_done && get(ph)){ u8 msg[48]; if(!w_GetMessageA(msg, 0, 0, 0)){ cur->quit = 1; break; } w_TranslateMessage(msg); w_DispatchMessageA(msg); }
  if(get(ph)) destroy(ph);
  int id = (int)g_menu_result; if(id && !(fl & 0x100)) post(hwnd, WM_COMMAND, (u64)id, 0); return (fl & 0x100) ? id : (id != 0);
}

/* ---- monitors and system metrics ---- */
static u64 WINAPI w_MonitorFromWindow(u64 h, u32 fl){ (void)h; (void)fl; return 0x1234; }
static u64 WINAPI w_MonitorFromPoint(i64 pt, u32 fl){ (void)pt; (void)fl; return 0x1234; }
static u64 WINAPI w_MonitorFromRect(const i32 *r, u32 fl){ (void)r; (void)fl; return 0x1234; }
static i32 WINAPI w_GetMonitorInfoA(u64 m, u32 *mi){
  (void)m; u32 sz = mi[0]; if(sz < 40) return 0; i32 *rc = (i32 *)(mi + 1);
  rc[0] = 0; rc[1] = 0; rc[2] = wm_screen_w() + 2 * FRAME; rc[3] = (i32)gfx_h; rc[4] = 0; rc[5] = 0; rc[6] = rc[2]; rc[7] = (i32)gfx_h - 60; mi[9] = 1;
  if(sz >= 72) memset((u8 *)mi + 40, 0, sz - 40); return 1;
}
static i32 WINAPI w_EnumDisplayMonitors(u64 dc, const i32 *clip, i32 (WINAPI *cb)(u64, u64, i32 *, i64), i64 data){ (void)dc; (void)clip; i32 rc[4] = {0, 0, (i32)gfx_w, (i32)gfx_h}; return cb ? cb(0x1234, 0, rc, data) : 0; }
static i32 WINAPI w_GetSystemMetrics(i32 i){
  switch(i){
    case 0: return (i32)gfx_w; case 1: return (i32)gfx_h; case 2: case 3: case 20: case 21: return 17; case 4: return 32; case 5: case 6: return 1; case 7: case 8: return 3; case 11: case 12: case 13: case 14: return 32;
    case 15: return 20; case 16: return (i32)gfx_w; case 17: return (i32)gfx_h - 60; case 19: return 1; case 28: return 136; case 29: return 39; case 30: case 31: return 32; case 32: case 33: return 4; case 34: return 136; case 35: return 39;
    case 36: case 37: return 4; case 43: return 3; case 45: case 46: return 2; case 49: case 50: return 16; case 51: return 20; case 61: return (i32)gfx_w; case 62: return (i32)gfx_h - 60;
    case 76: case 77: return 0; case 78: return (i32)gfx_w; case 79: return (i32)gfx_h; case 80: return 1; case 81: return 1; default: return 0; }
}

static i32 WINAPI w_MessageBoxW(u64 h, const u16 *text, const u16 *cap, u32 type){ (void)h; char a[400], b[100]; wd_to_a(text, a, sizeof a); wd_to_a(cap, b, sizeof b); return win32_messagebox(a, b, type); }
static u64 WINAPI w_GetActiveWindow(void){ return 0; }
static u64 WINAPI w_GetProcessWindowStation(void){ return 1; }
static i32 WINAPI w_GetUserObjectInformationW(u64 h, i32 idx, u32 *info, u32 len, u32 *need){ (void)h; (void)idx; if(need) *need = 12; if(len < 12) return 0; info[0] = 0; info[1] = 0; info[2] = 1; return 1; }
#define GDI_EXPORTS \
  {"user32","MessageBoxW",w_MessageBoxW}, {"user32","GetActiveWindow",w_GetActiveWindow}, {"user32","GetProcessWindowStation",w_GetProcessWindowStation}, {"user32","GetUserObjectInformationW",w_GetUserObjectInformationW}, \
  {"user32","MonitorFromWindow",w_MonitorFromWindow}, {"user32","MonitorFromPoint",w_MonitorFromPoint}, {"user32","MonitorFromRect",w_MonitorFromRect}, {"user32","GetMonitorInfoA",w_GetMonitorInfoA}, {"user32","GetMonitorInfoW",w_GetMonitorInfoA}, \
  {"user32","EnumDisplayMonitors",w_EnumDisplayMonitors}, {"user32","GetSystemMetrics",w_GetSystemMetrics}, \
  {"gdi32","CreateSolidBrush",w_CreateSolidBrush}, {"gdi32","CreatePen",w_CreatePen}, {"gdi32","CreateFontA",w_CreateFontA}, {"gdi32","CreateFontIndirectA",w_CreateFontIndirectA}, {"gdi32","GetStockObject",w_GetStockObject}, \
  {"gdi32","CreatePalette",w_CreatePalette}, {"gdi32","CreateBitmap",w_CreateBitmap}, {"gdi32","CreateCompatibleBitmap",w_CreateCompatibleBitmap}, {"gdi32","DeleteObject",w_DeleteObject}, {"gdi32","CreateCompatibleDC",w_CreateCompatibleDC}, \
  {"gdi32","DeleteDC",w_DeleteDC}, {"gdi32","SelectObject",w_SelectObject}, {"gdi32","GetCurrentObject",w_GetCurrentObject}, {"gdi32","GetObjectA",w_GetObjectA}, {"gdi32","SetTextColor",w_SetTextColor}, {"gdi32","GetTextColor",w_GetTextColor}, \
  {"gdi32","SetBkColor",w_SetBkColor}, {"gdi32","SetBkMode",w_SetBkMode}, {"gdi32","GetBkMode",w_GetBkMode}, {"gdi32","SetTextAlign",w_SetTextAlign}, {"gdi32","SetMapMode",w_SetMapMode}, {"gdi32","GetDeviceCaps",w_GetDeviceCaps}, \
  {"gdi32","RealizePalette",w_RealizePalette}, {"gdi32","SelectPalette",w_SelectPalette}, {"gdi32","SetPaletteEntries",w_SetPaletteEntries}, {"gdi32","UnrealizeObject",w_UnrealizeObject}, {"gdi32","UpdateColors",w_UpdateColors}, \
  {"gdi32","ExcludeClipRect",w_ExcludeClipRect}, {"gdi32","IntersectClipRect",w_IntersectClipRect}, {"gdi32","GetDIBits",w_GetDIBits}, {"gdi32","MoveToEx",w_MoveToEx}, {"gdi32","LineTo",w_LineTo}, {"gdi32","Polyline",w_Polyline}, \
  {"gdi32","Rectangle",w_Rectangle}, {"user32","FillRect",w_FillRect}, {"gdi32","PatBlt",w_PatBlt}, {"gdi32","GetPixel",w_GetPixel}, {"gdi32","SetPixel",w_SetPixel}, {"gdi32","BitBlt",w_BitBlt}, {"gdi32","StretchBlt",w_StretchBlt}, \
  {"gdi32","TextOutA",w_TextOutA}, {"gdi32","TextOutW",w_TextOutW}, {"gdi32","ExtTextOutA",w_ExtTextOutA}, {"gdi32","ExtTextOutW",w_ExtTextOutW}, {"gdi32","GetTextExtentPoint32A",w_GetTextExtentPoint32A}, {"gdi32","GetTextExtentPointA",w_GetTextExtentPoint32A}, \
  {"gdi32","GetTextExtentPoint32W",w_GetTextExtentPoint32W}, {"gdi32","GetTextExtentPointW",w_GetTextExtentPoint32W}, {"gdi32","GetTextExtentExPointA",w_GetTextExtentExPointA}, {"gdi32","GetTextMetricsA",w_GetTextMetricsA}, {"gdi32","GetTextMetricsW",w_GetTextMetricsW}, \
  {"gdi32","GetCharWidth32A",w_GetCharWidth32A}, {"gdi32","GetCharWidth32W",w_GetCharWidth32A}, {"gdi32","GetCharWidthA",w_GetCharWidthA}, {"gdi32","GetCharWidthW",w_GetCharWidthA}, {"gdi32","GetCharABCWidthsFloatA",w_GetCharABCWidthsFloatA}, \
  {"gdi32","GetCharacterPlacementW",w_GetCharacterPlacementW}, {"gdi32","GetOutlineTextMetricsA",w_GetOutlineTextMetricsA}, {"gdi32","TranslateCharsetInfo",w_TranslateCharsetInfo}, {"user32","DrawFocusRect",w_DrawFocusRect}, {"user32","DrawEdge",w_DrawEdge}, \
  {"user32","GetSysColorBrush",w_GetSysColorBrush}, {"user32","SetTimer",w_SetTimer}, {"user32","KillTimer",w_KillTimer}, {"user32","SetCapture",w_SetCapture}, {"user32","ReleaseCapture",w_ReleaseCapture}, {"user32","GetCapture",w_GetCapture}, \
  {"user32","OpenClipboard",w_OpenClipboard}, {"user32","CloseClipboard",w_CloseClipboard}, {"user32","EmptyClipboard",w_EmptyClipboard}, {"user32","SetClipboardData",w_SetClipboardData}, {"user32","GetClipboardData",w_GetClipboardData}, \
  {"user32","GetClipboardOwner",w_GetClipboardOwner}, {"user32","RegisterClipboardFormatA",w_RegisterClipboardFormatA}, {"user32","RegisterWindowMessageA",w_RegisterWindowMessageA}, {"user32","CreateCaret",w_CreateCaret}, \
  {"user32","DestroyCaret",w_Caret0}, {"user32","HideCaret",w_Caret0}, {"user32","ShowCaret",w_Caret0}, {"user32","SetCaretPos",w_Caret0}, {"user32","GetCaretBlinkTime",w_GetCaretBlinkTime}, {"user32","GetDoubleClickTime",w_GetDoubleClickTime}, \
  {"user32","GetMessageTime",w_GetMessageTime}, {"user32","GetKeyboardLayout",w_GetKeyboardLayout}, {"user32","GetForegroundWindow",w_GetForegroundWindow}, {"user32","SetForegroundWindow",w_SetForegroundWindow}, {"user32","SetActiveWindow",w_SetActiveWindow}, \
  {"user32","FlashWindow",w_FlashWindow}, {"user32","FindWindowA",w_FindWindowA}, {"user32","IsIconic",w_IsIconic}, {"user32","IsZoomed",w_IsZoomed}, {"user32","GetKeyboardState",w_GetKeyboardState}, {"user32","SetKeyboardState",w_SetKeyboardState}, \
  {"user32","ToAsciiEx",w_ToAsciiEx}, {"user32","ToUnicodeEx",w_ToUnicodeEx}, {"user32","ToUnicode",w_ToUnicode}, {"user32","ScreenToClient",w_ScreenToClient}, {"user32","ClientToScreen",w_ClientToScreen}, {"user32","SetWindowPos",w_SetWindowPos}, {"user32","GetWindowPlacement",w_GetWindowPlacement}, \
  {"user32","SetWindowPlacement",w_SetWindowPlacement}, {"user32","SystemParametersInfoA",w_SystemParametersInfoA}, {"user32","SetScrollInfo",w_SetScrollInfo}, {"user32","GetScrollInfo",w_GetScrollInfo}, {"user32","SetClassLongPtrA",w_SetClassLongPtrA}, \
  {"user32","MsgWaitForMultipleObjects",w_MsgWaitForMultipleObjects}, {"user32","GetQueueStatus",w_GetQueueStatus}, {"comctl32","InitCommonControls",w_InitCommonControls}, {"comctl32","InitCommonControlsEx",w_InitCommonControls}, \
  {"user32","MessageBoxIndirectW",w_MessageBoxIndirectW}, {"user32","CreateMenu",w_CreateMenu}, {"user32","CreatePopupMenu",w_CreateMenu}, {"user32","DestroyMenu",w_DestroyMenu}, {"user32","AppendMenuA",w_AppendMenuA}, {"user32","AppendMenuW",w_AppendMenuW}, {"user32","InsertMenuA",w_InsertMenuA}, \
  {"user32","DeleteMenu",w_DeleteMenu}, {"user32","RemoveMenu",w_DeleteMenu}, {"user32","GetMenuItemCount",w_GetMenuItemCount}, {"user32","GetMenuItemID",w_GetMenuItemID}, {"user32","CheckMenuItem",w_CheckMenuItem}, \
  {"user32","EnableMenuItem",w_EnableMenuItem}, {"user32","GetSubMenu",w_GetSubMenu}, {"user32","SetMenuDefaultItem",w_SetMenuDefaultItem}, {"user32","GetSystemMenu",w_GetSystemMenu}, {"user32","TrackPopupMenu",w_TrackPopupMenu},
