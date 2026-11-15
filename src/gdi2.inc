/* Willows GDI, batch 3 (included into wm.c after gdi.inc): clipping and regions (rectangles), Ellipse/RoundRect/Polygon/Arc/Pie/Bezier,
   DrawText, DIB sections and DIB transfers, AlphaBlend/TransparentBlt/GradientFill, SaveDC/RestoreDC, font enumeration and assorted DC state. */

/* ---------- small helpers ---------- */
static i64 isqrt64(i64 v){ if(v <= 0) return 0; i64 r = 0, b = (i64)1 << 40; while(b > v) b >>= 2; while(b){ if(v >= r + b){ v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static int imin_(int a, int b){ return a < b ? a : b; }
static int imax_(int a, int b){ return a > b ? a : b; }
static void rc_norm(i32 *r){ if(r[0] > r[2]){ i32 t = r[0]; r[0] = r[2]; r[2] = t; } if(r[1] > r[3]){ i32 t = r[1]; r[1] = r[3]; r[3] = t; } }
static u32 brush_rgb(struct dcst *d, int *none){ struct gobj *br = go_get(d->brush ? d->brush : stock(0)); if(!br || br->hollow){ *none = 1; return 0; } *none = 0; return br->color; }

/* ---------- clipping and regions ---------- */
static void clip_set(struct dcst *d, const i32 *r){ d->clip[0] = r[0]; d->clip[1] = r[1]; d->clip[2] = r[2]; d->clip[3] = r[3]; d->hasclip = 1; }
static i32 WINAPI w_IntersectClipRect(u64 dc, i32 l, i32 t, i32 r, i32 b){
  struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s) return 0; i32 n[4] = {l, t, r, b}; rc_norm(n);
  if(d->hasclip){ n[0] = imax_(n[0], d->clip[0]); n[1] = imax_(n[1], d->clip[1]); n[2] = imin_(n[2], d->clip[2]); n[3] = imin_(n[3], d->clip[3]); }
  if(n[2] < n[0]) n[2] = n[0]; if(n[3] < n[1]) n[3] = n[1]; clip_set(d, n); return (n[2] > n[0] && n[3] > n[1]) ? 2 : 1;
}
static i32 WINAPI w_ExcludeClipRect(u64 dc, i32 l, i32 t, i32 r, i32 b){                  /* only an exclusion that cuts a whole side can be represented */
  struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s) return 0; i32 c[4] = {0, 0, s->pw, s->ph}; if(d->hasclip){ for(int i = 0; i < 4; i++) c[i] = d->clip[i]; }
  i32 e[4] = {l, t, r, b}; rc_norm(e);
  if(e[1] <= c[1] && e[3] >= c[3]){ if(e[0] <= c[0]) c[0] = imax_(c[0], e[2]); else if(e[2] >= c[2]) c[2] = imin_(c[2], e[0]); }
  else if(e[0] <= c[0] && e[2] >= c[2]){ if(e[1] <= c[1]) c[1] = imax_(c[1], e[3]); else if(e[3] >= c[3]) c[3] = imin_(c[3], e[1]); }
  if(c[2] < c[0]) c[2] = c[0]; if(c[3] < c[1]) c[3] = c[1]; clip_set(d, c); return 2;
}
static i32 WINAPI w_GetClipBox(u64 dc, i32 *r){ struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s || !r) return 0; if(d->hasclip){ for(int i = 0; i < 4; i++) r[i] = d->clip[i]; } else { r[0] = r[1] = 0; r[2] = s->pw; r[3] = s->ph; } return (r[2] > r[0] && r[3] > r[1]) ? 2 : 1; }
static u64 rgn_new(i32 l, i32 t, i32 r, i32 b){ u64 h = go_new(GT_RGN); struct gobj *o = go_get(h); if(!o) return 0; o->rc[0] = l; o->rc[1] = t; o->rc[2] = r; o->rc[3] = b; rc_norm(o->rc); return h; }
static u64 WINAPI w_CreateRectRgn(i32 l, i32 t, i32 r, i32 b){ return rgn_new(l, t, r, b); }
static u64 WINAPI w_CreateRectRgnIndirect(const i32 *r){ return rgn_new(r[0], r[1], r[2], r[3]); }
static u64 WINAPI w_CreateRoundRectRgn(i32 l, i32 t, i32 r, i32 b, i32 w, i32 h){ (void)w; (void)h; return rgn_new(l, t, r, b); }
static u64 WINAPI w_CreateEllipticRgn(i32 l, i32 t, i32 r, i32 b){ return rgn_new(l, t, r, b); }
static u64 WINAPI w_CreateEllipticRgnIndirect(const i32 *r){ return rgn_new(r[0], r[1], r[2], r[3]); }
static u64 WINAPI w_CreatePolygonRgn(const i32 *p, i32 n, i32 mode){ (void)mode; if(n < 1) return 0; i32 l = p[0], t = p[1], r = p[0], b = p[1]; for(int i = 1; i < n; i++){ l = imin_(l, p[i * 2]); r = imax_(r, p[i * 2]); t = imin_(t, p[i * 2 + 1]); b = imax_(b, p[i * 2 + 1]); } return rgn_new(l, t, r, b); }
static i32 WINAPI w_SetRectRgn(u64 h, i32 l, i32 t, i32 r, i32 b){ struct gobj *o = go_get(h); if(!o || o->type != GT_RGN) return 0; o->rc[0] = l; o->rc[1] = t; o->rc[2] = r; o->rc[3] = b; rc_norm(o->rc); return 1; }
static i32 WINAPI w_CombineRgn(u64 dst, u64 a, u64 b, i32 mode){                          /* bounding-box arithmetic: AND, OR, XOR, DIFF, COPY */
  struct gobj *d = go_get(dst), *x = go_get(a), *y = go_get(b); if(!d || !x || d->type != GT_RGN) return 0; i32 r[4];
  for(int i = 0; i < 4; i++) r[i] = x->rc[i];
  if(mode != 5 && y){
    if(mode == 1){ r[0] = imax_(x->rc[0], y->rc[0]); r[1] = imax_(x->rc[1], y->rc[1]); r[2] = imin_(x->rc[2], y->rc[2]); r[3] = imin_(x->rc[3], y->rc[3]); }
    else if(mode == 2 || mode == 3){ r[0] = imin_(x->rc[0], y->rc[0]); r[1] = imin_(x->rc[1], y->rc[1]); r[2] = imax_(x->rc[2], y->rc[2]); r[3] = imax_(x->rc[3], y->rc[3]); }
    else if(mode == 4){ if(y->rc[0] <= x->rc[0] && y->rc[2] >= x->rc[2] && y->rc[1] <= x->rc[1] && y->rc[3] >= x->rc[3]) r[2] = r[0], r[3] = r[1]; }
  }
  if(r[2] < r[0]) r[2] = r[0]; if(r[3] < r[1]) r[3] = r[1]; for(int i = 0; i < 4; i++) d->rc[i] = r[i]; return (r[2] > r[0] && r[3] > r[1]) ? 2 : 1;
}
static i32 WINAPI w_GetRgnBox(u64 h, i32 *r){ struct gobj *o = go_get(h); if(!o || o->type != GT_RGN) return 0; for(int i = 0; i < 4; i++) r[i] = o->rc[i]; return (r[2] > r[0] && r[3] > r[1]) ? 2 : 1; }
static i32 WINAPI w_OffsetRgn(u64 h, i32 dx, i32 dy){ struct gobj *o = go_get(h); if(!o || o->type != GT_RGN) return 0; o->rc[0] += dx; o->rc[2] += dx; o->rc[1] += dy; o->rc[3] += dy; return 2; }
static i32 WINAPI w_PtInRegion(u64 h, i32 x, i32 y){ struct gobj *o = go_get(h); return o && o->type == GT_RGN && x >= o->rc[0] && x < o->rc[2] && y >= o->rc[1] && y < o->rc[3]; }
static i32 WINAPI w_RectInRegion(u64 h, const i32 *r){ struct gobj *o = go_get(h); return o && o->type == GT_RGN && r[0] < o->rc[2] && r[2] > o->rc[0] && r[1] < o->rc[3] && r[3] > o->rc[1]; }
static i32 WINAPI w_EqualRgn(u64 a, u64 b){ struct gobj *x = go_get(a), *y = go_get(b); return x && y && !memcmp(x->rc, y->rc, sizeof x->rc); }
static i32 WINAPI w_SelectClipRgn(u64 dc, u64 h){ struct dcst *d = dc_state(dc); if(!d) return 0; struct gobj *o = go_get(h); if(!o || o->type != GT_RGN){ d->hasclip = 0; return 2; } clip_set(d, o->rc); return 2; }
static i32 WINAPI w_ExtSelectClipRgn(u64 dc, u64 h, i32 mode){ struct gobj *o = go_get(h); if(mode == 5 || !o) return w_SelectClipRgn(dc, h); if(mode == 1) return w_IntersectClipRect(dc, o->rc[0], o->rc[1], o->rc[2], o->rc[3]); return w_SelectClipRgn(dc, h); }
static i32 WINAPI w_SetWindowRgn(u64 hwnd, u64 rgn, i32 redraw){ (void)hwnd; (void)rgn; (void)redraw; return 1; }
static i32 WINAPI w_GetClipRgn(u64 dc, u64 h){ struct dcst *d = dc_state(dc); struct gobj *o = go_get(h); if(!d || !o || !d->hasclip) return 0; for(int i = 0; i < 4; i++) o->rc[i] = d->clip[i]; return 1; }
static i32 WINAPI w_FillRgn(u64 dc, u64 rgn, u64 br){ struct gobj *o = go_get(rgn); if(!o || o->type != GT_RGN) return 0; i32 r[4] = {o->rc[0], o->rc[1], o->rc[2], o->rc[3]}; return w_FillRect(dc, r, br); }
static i32 WINAPI w_PaintRgn(u64 dc, u64 rgn){ struct dcst *d = dc_state(dc); if(!d) return 0; return w_FillRgn(dc, rgn, d->brush ? d->brush : stock(0)); }
static i32 WINAPI w_FrameRgn(u64 dc, u64 rgn, u64 br, i32 w, i32 h){ struct gobj *o = go_get(rgn), *b = go_get(br); if(!o || !b) return 0; i32 *r = o->rc; if(b->hollow) return 1; wm_gdi_rect(dc, r[0], r[1], r[2] - r[0], h, b->color); wm_gdi_rect(dc, r[0], r[3] - h, r[2] - r[0], h, b->color); wm_gdi_rect(dc, r[0], r[1], w, r[3] - r[1], b->color); wm_gdi_rect(dc, r[2] - w, r[1], w, r[3] - r[1], b->color); return 1; }
static i32 WINAPI w_InvalidateRgn(u64 hwnd, u64 rgn, i32 erase){ (void)rgn; return w_InvalidateRect(hwnd, 0, erase); }
static i32 WINAPI w_ValidateRgn(u64 hwnd, u64 rgn){ (void)hwnd; (void)rgn; return 1; }
static i32 WINAPI w_FrameRect(u64 dc, const i32 *r, u64 br){ struct gobj *b = go_get(br); if(!b || b->hollow) return 1; wm_gdi_rect(dc, r[0], r[1], r[2] - r[0], 1, b->color); wm_gdi_rect(dc, r[0], r[3] - 1, r[2] - r[0], 1, b->color); wm_gdi_rect(dc, r[0], r[1], 1, r[3] - r[1], b->color); wm_gdi_rect(dc, r[2] - 1, r[1], 1, r[3] - r[1], b->color); return 1; }
static i32 WINAPI w_InvertRect(u64 dc, const i32 *r){ struct wnd *s = dc_target(dc); struct dcst *d = dc_state(dc); if(!s || !d) return 0; int cl[4]; int hc = dc_clip(dc, cl);
  for(int y = imax_(r[1], hc ? cl[1] : 0); y < imin_(r[3], hc ? cl[3] : s->ph); y++) for(int x = imax_(r[0], hc ? cl[0] : 0); x < imin_(r[2], hc ? cl[2] : s->pw); x++) s->pix[(u64)y * s->stride + x] ^= 0xFFFFFF; dirty = 1; return 1; }

/* ---------- save / restore DC state ---------- */
static struct dcst dc_saved[MAXW][4]; static u8 dc_sp[MAXW];
static i32 WINAPI w_SaveDC(u64 dc){ struct dcst *d = dc_state(dc); if(!d) return 0; int i = (int)(dc - DCBASE); if(dc_sp[i] >= 4) return 0; dc_saved[i][dc_sp[i]++] = *d; return dc_sp[i]; }
static i32 WINAPI w_RestoreDC(u64 dc, i32 n){ struct dcst *d = dc_state(dc); if(!d) return 0; int i = (int)(dc - DCBASE); int target = n < 0 ? (int)dc_sp[i] + n : n - 1; if(target < 0 || target >= dc_sp[i]) return 0; u8 mem = d->mem; *d = dc_saved[i][target]; d->mem = mem; dc_sp[i] = (u8)target; return 1; }

/* ---------- simple DC state ---------- */
static i32 WINAPI w_True(void){ return 1; }
static i32 WINAPI w_SetStretchBltMode(u64 dc, i32 m){ struct dcst *d = dc_state(dc); if(!d) return 0; i32 o = d->stretch ? d->stretch : 1; d->stretch = (u8)m; return o; }
static i32 WINAPI w_GetStretchBltMode(u64 dc){ struct dcst *d = dc_state(dc); return d && d->stretch ? d->stretch : 1; }
static i32 WINAPI w_SetROP2(u64 dc, i32 m){ struct dcst *d = dc_state(dc); if(!d) return 0; i32 o = d->rop2 ? d->rop2 : 13; d->rop2 = (u8)m; return o; }
static i32 WINAPI w_GetROP2(u64 dc){ struct dcst *d = dc_state(dc); return d && d->rop2 ? d->rop2 : 13; }
static u32 WINAPI w_GetBkColor(u64 dc){ struct dcst *d = dc_state(dc); return d ? cref(d->bg) : 0xFFFFFF; }
static u32 WINAPI w_GetTextAlign(u64 dc){ struct dcst *d = dc_state(dc); return d ? d->align : 0; }
static u32 WINAPI w_SetPixelV(u64 dc, i32 x, i32 y, u32 c){ wm_gdi_pixel(dc, x, y, cref(c)); return 1; }
static i32 WINAPI w_OrgEx(u64 dc, i32 x, i32 y, i32 *old){ (void)dc; (void)x; (void)y; if(old){ old[0] = 0; old[1] = 0; } return 1; }          /* window/viewport origins are not translated */
static i32 WINAPI w_GetOrgEx(u64 dc, i32 *p){ (void)dc; if(p){ p[0] = 0; p[1] = 0; } return 1; }

/* ---------- shapes ---------- */
static void span(u64 dc, int x0, int x1, int y, u32 c){ if(x1 > x0) wm_gdi_rect(dc, x0, y, x1 - x0, 1, c); }
/* the x extent of row y of a rounded rectangle whose corners are ellipses ew x eh (all in doubled coordinates so pixel centres are exact) */
static int row_extent(int l, int t, int r, int b, int ew, int eh, int y, int *x0, int *x1){
  if(r <= l || b <= t) return 0; if(ew > r - l) ew = r - l; if(eh > b - t) eh = b - t; if(ew < 0) ew = 0; if(eh < 0) eh = 0;
  int Y2 = 2 * y + 1; if(Y2 < 2 * t || Y2 > 2 * b) return 0; int ct = 2 * t + eh, cb = 2 * b - eh; int dd = 0;
  if(Y2 < ct) dd = ct - Y2; else if(Y2 > cb) dd = Y2 - cb;
  int left2 = 2 * l, right2 = 2 * r;
  if(dd > 0 && eh > 0){ if(dd > eh) return 0; i64 hw2 = (i64)ew * isqrt64((i64)eh * eh - (i64)dd * dd) / eh; left2 = 2 * l + ew - (int)hw2; right2 = 2 * r - ew + (int)hw2; }
  *x0 = left2 / 2; *x1 = (right2 + 1) / 2; return *x1 > *x0;
}
/* fill and outline a rounded rectangle / ellipse (ew == r-l, eh == b-t gives an ellipse) */
static void round_shape(u64 dc, struct dcst *d, int l, int t, int r, int b, int ew, int eh){
  int nofill; u32 fc = brush_rgb(d, &nofill); int skip; u32 pc = pen_color(d, &skip); struct gobj *p = go_get(d->pen); int pw = skip ? 0 : (p ? p->penw : 1);
  for(int y = t; y < b; y++){
    int ox0, ox1; if(!row_extent(l, t, r, b, ew, eh, y, &ox0, &ox1)) continue;
    int ix0 = ox0, ix1 = ox1, has_in = 0;
    if(pw > 0) has_in = row_extent(l + pw, t + pw, r - pw, b - pw, ew > 2 * pw ? ew - 2 * pw : 0, eh > 2 * pw ? eh - 2 * pw : 0, y, &ix0, &ix1); else has_in = 1;
    if(pw > 0){ if(has_in){ span(dc, ox0, ix0, y, pc); span(dc, ix1, ox1, y, pc); } else span(dc, ox0, ox1, y, pc); }
    if(!nofill && has_in) span(dc, ix0, ix1, y, fc);
  }
}
static i32 WINAPI w_Ellipse(u64 dc, i32 l, i32 t, i32 r, i32 b){ struct dcst *d = dc_state(dc); if(!d || !dc_target(dc)) return 0; round_shape(dc, d, l, t, r, b, r - l, b - t); dirty = 1; return 1; }
static i32 WINAPI w_RoundRect(u64 dc, i32 l, i32 t, i32 r, i32 b, i32 w, i32 h){ struct dcst *d = dc_state(dc); if(!d || !dc_target(dc)) return 0; round_shape(dc, d, l, t, r, b, w, h); dirty = 1; return 1; }
/* scanline even-odd polygon fill + outline */
static void poly_fill(u64 dc, const i32 *p, int n, u32 col){
  if(n < 3) return; int y0 = p[1], y1 = p[1]; for(int i = 1; i < n; i++){ y0 = imin_(y0, p[i * 2 + 1]); y1 = imax_(y1, p[i * 2 + 1]); } if(y1 - y0 > 4096) return;
  for(int y = y0; y < y1; y++){
    int xs[64], nx = 0;
    for(int i = 0; i < n && nx < 64; i++){ int ax = p[i * 2], ay = p[i * 2 + 1], bx = p[((i + 1) % n) * 2], by = p[((i + 1) % n) * 2 + 1]; if(ay == by) continue;
      if((ay <= y && y < by) || (by <= y && y < ay)) xs[nx++] = ax + (int)(((i64)(2 * y + 1 - 2 * ay) * (bx - ax)) / (2 * (by - ay))); }
    for(int i = 1; i < nx; i++){ int v = xs[i], j = i - 1; while(j >= 0 && xs[j] > v){ xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
    for(int i = 0; i + 1 < nx; i += 2) span(dc, xs[i], xs[i + 1] + 1, y, col);
  }
}
static void poly_outline(struct wnd *s, struct dcst *d, const i32 *p, int n, int closed){
  int skip; u32 pc = pen_color(d, &skip); struct gobj *pn = go_get(d->pen); if(skip) return; clip_on(d);
  for(int i = 0; i + 1 < n; i++) draw_line(s, p[i * 2], p[i * 2 + 1], p[i * 2 + 2], p[i * 2 + 3], pc, pn ? pn->penw : 1);
  if(closed && n > 2) draw_line(s, p[(n - 1) * 2], p[(n - 1) * 2 + 1], p[0], p[1], pc, pn ? pn->penw : 1); g_clip = 0;
}
static i32 WINAPI w_Polygon(u64 dc, const i32 *p, i32 n){
  struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s || n < 2) return 0; int nofill; u32 fc = brush_rgb(d, &nofill);
  if(!nofill) poly_fill(dc, p, n, fc); poly_outline(s, d, p, n, 1); dirty = 1; return 1;
}
static i32 WINAPI w_PolyPolygon(u64 dc, const i32 *p, const i32 *counts, i32 np){ int off = 0; for(int i = 0; i < np; i++){ w_Polygon(dc, p + off * 2, counts[i]); off += counts[i]; } return 1; }
static i32 WINAPI w_PolylineTo(u64 dc, const i32 *p, u32 n){ struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s || !n) return 0; i32 tmp[130]; if(n > 64) n = 64; tmp[0] = d->px; tmp[1] = d->py; for(u32 i = 0; i < n * 2; i++) tmp[2 + i] = p[i];
  poly_outline(s, d, tmp, (int)n + 1, 0); d->px = p[(n - 1) * 2]; d->py = p[(n - 1) * 2 + 1]; dirty = 1; return 1; }
/* cubic Bezier segments: 3n+1 control points */
static void bezier_pts(const i32 *p, int nseg, i32 *out, int *nout){
  int k = 0; out[k * 2] = p[0]; out[k * 2 + 1] = p[1]; k++;
  for(int sgm = 0; sgm < nseg && k < 250; sgm++){ const i32 *q = p + sgm * 6; for(int i = 1; i <= 12; i++){ i64 t = i * 1024 / 12, u = 1024 - t; i64 b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t; const i64 sc = (i64)1024 * 1024 * 1024;
      out[k * 2] = (int)((b0 * q[0] + b1 * q[2] + b2 * q[4] + b3 * q[6]) / sc); out[k * 2 + 1] = (int)((b0 * q[1] + b1 * q[3] + b2 * q[5] + b3 * q[7]) / sc); k++; } }
  *nout = k;
}
static i32 WINAPI w_PolyBezier(u64 dc, const i32 *p, u32 n){ struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s || n < 4) return 0; i32 out[512]; int k; bezier_pts(p, (int)(n - 1) / 3, out, &k); poly_outline(s, d, out, k, 0); dirty = 1; return 1; }
static i32 WINAPI w_PolyBezierTo(u64 dc, const i32 *p, u32 n){ struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s || n < 3) return 0; i32 in[200]; if(n > 96) n = 96; in[0] = d->px; in[1] = d->py; for(u32 i = 0; i < n * 2; i++) in[2 + i] = p[i];
  i32 out[512]; int k; bezier_pts(in, (int)n / 3, out, &k); poly_outline(s, d, out, k, 0); d->px = p[(n - 1) * 2]; d->py = p[(n - 1) * 2 + 1]; dirty = 1; return 1; }
/* arcs: integer sine and arctangent (degrees) */
static int isin1024(int deg){ deg %= 360; if(deg < 0) deg += 360; int neg = 0; if(deg >= 180){ deg -= 180; neg = 1; } int v = 4 * deg * (180 - deg) * 1024 / (40500 - deg * (180 - deg)); return neg ? -v : v; }
static int icos1024(int deg){ return isin1024(deg + 90); }
static int iatan2deg(int y, int x){                                         /* angle of (x, y) in degrees, 0..359, counter-clockwise with y up */
  if(x == 0 && y == 0) return 0; int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y; int a;
  if(ay <= ax){ i64 z = (i64)ay * 1024 / (ax ? ax : 1); a = (int)((45 * z - z * (z - 1024) / 1024 * (14 * 1024 + 3 * z + z * 8 / 10) / 1024 / 1) / 1024); }
  else { i64 z = (i64)ax * 1024 / ay; int b = (int)((45 * z - z * (z - 1024) / 1024 * (14 * 1024 + 3 * z + z * 8 / 10) / 1024) / 1024); a = 90 - b; }
  if(x >= 0 && y >= 0) return a; if(x < 0 && y >= 0) return 180 - a; if(x < 0 && y < 0) return 180 + a; return (360 - a) % 360;
}
static i32 arc_shape(u64 dc, i32 l, i32 t, i32 r, i32 b, i32 xs, i32 ys, i32 xe, i32 ye, int kind){      /* 0 arc, 1 pie, 2 chord */
  struct dcst *d = dc_state(dc); struct wnd *s = dc_target(dc); if(!d || !s) return 0; int cx2 = l + r, cy2 = t + b, rx2 = r - l, ry2 = b - t;
  int a1 = iatan2deg(-(2 * ys - cy2) * rx2, (2 * xs - cx2) * ry2), a2 = iatan2deg(-(2 * ye - cy2) * rx2, (2 * xe - cx2) * ry2); if(a2 <= a1) a2 += 360;
  i32 pts[200]; int n = 0; for(int a = a1; a <= a2 && n < 90; a += 4){ pts[n * 2] = (cx2 + (int)((i64)rx2 * icos1024(a) / 1024)) / 2; pts[n * 2 + 1] = (cy2 - (int)((i64)ry2 * isin1024(a) / 1024)) / 2; n++; }
  pts[n * 2] = (cx2 + (int)((i64)rx2 * icos1024(a2) / 1024)) / 2; pts[n * 2 + 1] = (cy2 - (int)((i64)ry2 * isin1024(a2) / 1024)) / 2; n++;
  if(kind == 1){ pts[n * 2] = cx2 / 2; pts[n * 2 + 1] = cy2 / 2; n++; }
  if(kind){ int nofill; u32 fc = brush_rgb(d, &nofill); if(!nofill) poly_fill(dc, pts, n, fc); }
  poly_outline(s, d, pts, n, kind != 0); dirty = 1; return 1;
}
static i32 WINAPI w_Arc(u64 dc, i32 l, i32 t, i32 r, i32 b, i32 xs, i32 ys, i32 xe, i32 ye){ return arc_shape(dc, l, t, r, b, xs, ys, xe, ye, 0); }
static i32 WINAPI w_Pie(u64 dc, i32 l, i32 t, i32 r, i32 b, i32 xs, i32 ys, i32 xe, i32 ye){ return arc_shape(dc, l, t, r, b, xs, ys, xe, ye, 1); }
static i32 WINAPI w_Chord(u64 dc, i32 l, i32 t, i32 r, i32 b, i32 xs, i32 ys, i32 xe, i32 ye){ return arc_shape(dc, l, t, r, b, xs, ys, xe, ye, 2); }

/* ---------- DrawText ---------- */
#define DT_CENTER 1
#define DT_RIGHT 2
#define DT_VCENTER 4
#define DT_BOTTOM 8
#define DT_WORDBREAK 0x10
#define DT_SINGLELINE 0x20
#define DT_NOCLIP 0x100
#define DT_CALCRECT 0x400
#define DT_NOPREFIX 0x800
#define DT_END_ELLIPSIS 0x8000
static i32 draw_text_utf8(u64 dc, const char *text, int len, i32 *rc, u32 fmt){
  struct dcst *d = dc_state(dc); struct wnd *t = dc_target(dc); if(!d || !t || !rc) return 0; const struct font *f = font_of_dc(d); int lh = ui_lineh(f);
  if(len < 0) len = (int)strlen(text); static char buf[2048]; int n = 0;
  for(int i = 0; i < len && n < 2040; i++){ char c = text[i]; if(!(fmt & DT_NOPREFIX) && c == '&'){ if(i + 1 < len && text[i + 1] == '&'){ buf[n++] = '&'; i++; } continue; } if(c == '\t') c = ' '; buf[n++] = c; }
  buf[n] = 0;
  int width = rc[2] - rc[0]; int starts[128], lens[128], widths[128], nl = 0; int pos = 0, maxw = 0;
  while(pos <= n && nl < 128){
    int e = pos; while(e < n && buf[e] != '\n') e++; int ls = pos, le = e; if(le > ls && buf[le - 1] == '\r') le--;
    if((fmt & DT_WORDBREAK) && !(fmt & DT_SINGLELINE) && width > 0){                                     /* break the paragraph into lines that fit */
      int a = ls;
      while(a < le && nl < 128){ int b = a, lastsp = -1, w = 0; while(b < le){ int cl = 1; while(b + cl < le && ((unsigned char)buf[b + cl] & 0xC0) == 0x80) cl++; int cw = ui_width(f, buf + b, cl); if(w + cw > width && b > a){ break; } if(buf[b] == ' ') lastsp = b; w += cw; b += cl; }
        int brk = b; if(b < le && lastsp > a) brk = lastsp; starts[nl] = a; lens[nl] = brk - a; widths[nl] = ui_width(f, buf + a, brk - a); if(widths[nl] > maxw) maxw = widths[nl]; nl++; a = brk; while(a < le && buf[a] == ' ' && brk != b) a++; if(brk == b && a < le && buf[a] == ' ') a++; }
      if(ls == le && nl < 128){ starts[nl] = ls; lens[nl] = 0; widths[nl] = 0; nl++; }
    } else if(nl < 128){ starts[nl] = ls; lens[nl] = le - ls; widths[nl] = ui_width(f, buf + ls, le - ls); if(widths[nl] > maxw) maxw = widths[nl]; nl++; }
    if(fmt & DT_SINGLELINE) break; pos = e + 1; if(e >= n) break;
  }
  int total = nl * lh;
  if(fmt & DT_CALCRECT){ rc[2] = rc[0] + ((fmt & DT_WORDBREAK) && width > 0 && maxw > width ? width : maxw); rc[3] = rc[1] + total; return total; }
  int y = rc[1]; int rh = rc[3] - rc[1]; if(fmt & DT_VCENTER) y += (rh - total) / 2; else if(fmt & DT_BOTTOM) y += rh - total;
  u8 sv_al = d->align; d->align = 0; u8 sv_bk = d->bkmode; struct dcst *sv = d; int had = d->hasclip; i32 sc[4]; for(int i = 0; i < 4; i++) sc[i] = d->clip[i];
  if(!(fmt & DT_NOCLIP)){ i32 cr[4] = {rc[0], rc[1], rc[2], rc[3]}; if(had){ cr[0] = imax_(cr[0], sc[0]); cr[1] = imax_(cr[1], sc[1]); cr[2] = imin_(cr[2], sc[2]); cr[3] = imin_(cr[3], sc[3]); } if(cr[2] < cr[0]) cr[2] = cr[0]; if(cr[3] < cr[1]) cr[3] = cr[1]; clip_set(d, cr); }
  for(int i = 0; i < nl; i++){
    char line[512]; int ll = lens[i] < 500 ? lens[i] : 500; memcpy(line, buf + starts[i], ll); line[ll] = 0; int w = widths[i];
    if((fmt & DT_END_ELLIPSIS) && width > 0 && w > width && ll > 3){ while(ll > 1 && ui_width(f, line, ll) + ui_width(f, "...", 3) > width){ ll--; while(ll > 1 && ((unsigned char)line[ll] & 0xC0) == 0x80) ll--; } memcpy(line + ll, "...", 4); ll += 3; w = ui_width(f, line, ll); }
    int x = rc[0]; if(fmt & DT_CENTER) x += (width - w) / 2; else if(fmt & DT_RIGHT) x += width - w;
    do_text(dc, x, y + i * lh, line, ll, 0, 0, 0);
  }
  sv->hasclip = (u8)had; if(had) for(int i = 0; i < 4; i++) sv->clip[i] = sc[i]; sv->align = sv_al; sv->bkmode = sv_bk; return total;
}
static i32 WINAPI w_DrawTextA(u64 dc, const char *s, i32 n, i32 *rc, u32 fmt){ return draw_text_utf8(dc, s, n, rc, fmt); }
static i32 WINAPI w_DrawTextW(u64 dc, const u16 *s, i32 n, i32 *rc, u32 fmt){ static char a[2048]; int k = w_to_utf8(s, n, a, sizeof a); return draw_text_utf8(dc, a, k, rc, fmt); }
static i32 WINAPI w_DrawTextExA(u64 dc, const char *s, i32 n, i32 *rc, u32 fmt, void *p){ (void)p; return draw_text_utf8(dc, s, n, rc, fmt); }
static i32 WINAPI w_DrawTextExW(u64 dc, const u16 *s, i32 n, i32 *rc, u32 fmt, void *p){ (void)p; return w_DrawTextW(dc, s, n, rc, fmt); }
static i32 WINAPI w_GetTextFaceA(u64 dc, i32 n, char *out){ struct dcst *d = dc_state(dc); const char *face = dc_font_mono(d) ? "Courier New" : "Open Sans"; int l = (int)strlen(face) + 1; if(!out) return l; if(n < l) l = n; if(l > 0){ memcpy(out, face, l - 1); out[l - 1] = 0; } return l; }
static i32 WINAPI w_GetTextFaceW(u64 dc, i32 n, u16 *out){ char t[40]; int l = w_GetTextFaceA(dc, 40, t); if(!out) return l; for(int i = 0; i < l && i < n; i++) out[i] = (u8)t[i]; return l; }
static i32 WINAPI w_SetTextCharacterExtra(u64 dc, i32 e){ (void)dc; (void)e; return 0; }
static u64 WINAPI w_CreateFontIndirectW(const u8 *lf){ char face[40]; for(int i = 0; i < 32; i++){ u16 c = *(const u16 *)(lf + 28 + i * 2); face[i] = c < 0x80 ? (char)c : '?'; if(!c) break; } face[32] = 0; return mk_font(*(const i32 *)lf, *(const i32 *)(lf + 16), lf[20], face); }
static u64 WINAPI w_CreateFontW(i32 h, i32 w, i32 e, i32 o, i32 weight, u32 italic, u32 u, u32 s, u32 cs, u32 op, u32 cp, u32 q, u32 pitch, const u16 *face){ (void)w; (void)e; (void)o; (void)u; (void)s; (void)cs; (void)op; (void)cp; (void)q; (void)pitch; char f[40]; int i = 0; for(; face && face[i] && i < 39; i++) f[i] = face[i] < 0x80 ? (char)face[i] : '?'; f[i] = 0; return mk_font(h, weight, italic != 0, f); }
static i32 WINAPI w_GetObjectW(u64 h, i32 n, void *buf){
  struct gobj *o = go_get(h); if(o && o->type == GT_FONT && buf && n >= 92){ memset(buf, 0, 92); i32 *b = buf; b[0] = -o->px; b[4] = o->penw ? o->penw : 400; const char *face = o->mono ? "Courier New" : "Open Sans"; for(int i = 0; face[i]; i++) ((u16 *)((u8 *)buf + 28))[i] = (u8)face[i]; return 92; }
  return w_GetObjectA(h, n, buf);
}
/* EnumFontFamilies(Ex): report the few faces Willows has. The callback takes (LOGFONT*, TEXTMETRIC*, fontType, lParam). */
static i32 enum_fonts(int wide, void *cb, u64 lp){
  static const char *const faces[] = {"Open Sans", "Courier New"}; typedef i32 (WINAPI *efn)(const void *, const void *, u32, u64); i32 r = 1;
  for(int i = 0; i < 2 && r; i++){ u8 lf[92 + 64]; u8 tm[64 * 4]; memset(lf, 0, sizeof lf); memset(tm, 0, sizeof tm); *(i32 *)lf = -13; *(i32 *)(lf + 16) = 400; lf[23] = 1;
    if(wide){ for(int k = 0; faces[i][k]; k++) ((u16 *)(lf + 28))[k] = (u8)faces[i][k]; } else strcpy_k((char *)(lf + 28), faces[i]);
    *(i32 *)tm = 13; r = ((efn)cb)(lf, tm, 4, lp); }
  return r;
}
static i32 WINAPI w_EnumFontFamiliesExA(u64 dc, const void *lf, void *cb, u64 lp, u32 fl){ (void)dc; (void)lf; (void)fl; return enum_fonts(0, cb, lp); }
static i32 WINAPI w_EnumFontFamiliesExW(u64 dc, const void *lf, void *cb, u64 lp, u32 fl){ (void)dc; (void)lf; (void)fl; return enum_fonts(1, cb, lp); }
static i32 WINAPI w_EnumFontFamiliesA(u64 dc, const char *fam, void *cb, u64 lp){ (void)dc; (void)fam; return enum_fonts(0, cb, lp); }
static i32 WINAPI w_EnumFontFamiliesW(u64 dc, const u16 *fam, void *cb, u64 lp){ (void)dc; (void)fam; return enum_fonts(1, cb, lp); }

/* ---------- DIB sections and DIB transfers ---------- */
/* BITMAPINFOHEADER: size u32, width i32, height i32 (negative = top-down), planes u16, bitCount u16 */
static u64 dib_new(int w, int h, int bpp, int topdown, void **bits){
  if(w <= 0 || h <= 0 || (u64)w * h > 16000000 || (bpp != 32 && bpp != 24 && bpp != 16 && bpp != 8 && bpp != 1)) return 0;
  u64 hh = go_new(GT_BITMAP); struct gobj *o = go_get(hh); if(!o) return 0;
  o->w = w; o->h = h; o->dib = 1; o->bpp = (u16)bpp; o->topdown = (u8)topdown; o->rstride = ((w * bpp + 31) / 32) * 4;
  if(bpp == 32 && topdown){ o->pix = heap_alloc((u64)w * h * 4); o->raw = (u8 *)o->pix; if(!o->pix){ o->used = 0; return 0; } }       /* the program's pointer IS the pixel buffer */
  else { o->pix = heap_alloc((u64)w * h * 4); o->raw = heap_alloc((u64)o->rstride * h); if(!o->pix || !o->raw){ heap_free(o->pix); heap_free(o->raw); o->used = 0; return 0; } }
  if(bits) *bits = o->raw; return hh;
}
static u64 WINAPI w_CreateDIBSection(u64 dc, const u8 *bmi, u32 usage, void **bits, u64 section, u32 off){
  (void)dc; (void)usage; (void)section; (void)off; if(!bmi) return 0; int w = *(const i32 *)(bmi + 4), h = *(const i32 *)(bmi + 8), bpp = *(const u16 *)(bmi + 14); int td = h < 0; if(td) h = -h;
  return dib_new(w, h, bpp, td, bits);
}
static u64 WINAPI w_CreateBitmapIndirect(const u8 *bm){ return w_CreateBitmap(*(const i32 *)(bm + 4), *(const i32 *)(bm + 8), *(const u16 *)(bm + 14), *(const u16 *)(bm + 16), *(void *const *)(bm + 24)); }
static i32 WINAPI w_GetBitmapBits(u64 h, i32 n, void *out){ struct gobj *o = go_get(h); if(!o || o->type != GT_BITMAP || !o->pix) return 0; if(o->dib) dib_sync(o); i32 total = o->w * o->h * 4; if(n > total) n = total; memcpy(out, o->pix, n); return n; }
static i32 WINAPI w_SetBitmapBits(u64 h, u32 n, const void *in){ struct gobj *o = go_get(h); if(!o || o->type != GT_BITMAP || !o->pix) return 0; u32 total = (u32)(o->w * o->h * 4); if(n > total) n = total; memcpy(o->pix, in, n); return (i32)n; }
/* read pixels of a bitmap object out as a DIB (32 or 24 bit) */
static i32 WINAPI w_GetDIBits(u64 dc, u64 bmp, u32 start, u32 lines, void *bits, u8 *bmi, u32 usage){
  (void)dc; (void)usage; struct gobj *o = go_get(bmp); if(!o || o->type != GT_BITMAP || !bmi) return 0; if(o->dib) dib_sync(o);
  int bpp = *(u16 *)(bmi + 14); if(!bpp){ *(u32 *)bmi = 40; *(i32 *)(bmi + 4) = o->w; *(i32 *)(bmi + 8) = o->h; *(u16 *)(bmi + 12) = 1; *(u16 *)(bmi + 14) = 32; *(u32 *)(bmi + 16) = 0; *(u32 *)(bmi + 20) = (u32)(o->w * o->h * 4); return (i32)o->h; }
  if(!bits) return (i32)o->h; if(bpp != 32 && bpp != 24) return 0;
  int hh = *(i32 *)(bmi + 8); int td = hh < 0; int stride = ((o->w * bpp + 31) / 32) * 4; int done = 0;
  for(u32 i = 0; i < lines && start + i < (u32)o->h; i++){ int src_y = td ? (int)(start + i) : o->h - 1 - (int)(start + i); u8 *dst = (u8 *)bits + (u64)i * stride; const u32 *sp = o->pix + (u64)src_y * o->w;
    for(int x = 0; x < o->w; x++){ u32 p = sp[x]; if(bpp == 32) ((u32 *)dst)[x] = p; else { dst[x * 3] = (u8)p; dst[x * 3 + 1] = (u8)(p >> 8); dst[x * 3 + 2] = (u8)(p >> 16); } } done++; }
  return done;
}
static void dib_row(const u8 *bits, const u8 *bmi, int row, int w, u32 *out){                                    /* one source row (bottom-up unless the height is negative) -> 0x00RRGGBB */
  int bpp = *(const u16 *)(bmi + 14); int bh = *(const i32 *)(bmi + 8); int td = bh < 0; if(td) bh = -bh; int stride = ((w * bpp + 31) / 32) * 4; const u8 *r = bits + (u64)(td ? row : bh - 1 - row) * stride;
  for(int x = 0; x < w; x++){ if(bpp == 32) out[x] = ((const u32 *)r)[x] & 0xFFFFFF; else if(bpp == 24) out[x] = ((u32)r[x * 3 + 2] << 16) | ((u32)r[x * 3 + 1] << 8) | r[x * 3]; else if(bpp == 8) out[x] = (u32)r[x] * 0x010101; else out[x] = 0; }
}
static i32 WINAPI w_SetDIBitsToDevice(u64 dc, i32 x, i32 y, u32 w, u32 h, i32 xs, i32 ys, u32 start, u32 lines, const void *bits, const u8 *bmi, u32 usage){
  (void)usage; (void)start; (void)lines; struct wnd *t = dc_target(dc);  if(!t || !bits || !bmi) return 0; int bw = *(const i32 *)(bmi + 4); int bpp = *(const u16 *)(bmi + 14); if(bpp != 32 && bpp != 24 && bpp != 8) return 0;
  int bh = *(const i32 *)(bmi + 8); int td = bh < 0; if(td) bh = -bh; int cl[4]; int hc = dc_clip(dc, cl); static u32 row[4096]; if(bw > 4096) bw = 4096;
  for(u32 j = 0; j < h; j++){ int srow = ys + (int)j; if(srow < 0 || srow >= bh) continue; int py = y + (int)j; if(py < 0 || py >= t->ph || (hc && (py < cl[1] || py >= cl[3]))) continue; dib_row(bits, bmi, srow, bw, row);
    for(u32 i = 0; i < w; i++){ int sx = xs + (int)i; if(sx < 0 || sx >= bw) continue; int px = x + (int)i; if(px < 0 || px >= t->pw || (hc && (px < cl[0] || px >= cl[2]))) continue; t->pix[(u64)py * t->stride + px] = row[sx]; } }
  dirty = 1; return (i32)h;
}
static i32 WINAPI w_SetDIBits(u64 dc, u64 bmp, u32 start, u32 lines, const void *bits, const u8 *bmi, u32 usage){
  (void)dc; (void)usage; struct gobj *o = go_get(bmp); if(!o || o->type != GT_BITMAP || !bits || !bmi || !o->pix) return 0; int bw = *(const i32 *)(bmi + 4); if(bw > 4096) return 0; int bh = *(const i32 *)(bmi + 8); if(bh < 0) bh = -bh; static u32 row[4096];
  for(u32 i = 0; i < lines; i++){ int srow = (int)(start + i); if(srow >= bh) break; dib_row(bits, bmi, srow, bw, row); int dy = (*(const i32 *)(bmi + 8) < 0) ? srow : o->h - 1 - srow; if(dy < 0 || dy >= o->h) continue; for(int x = 0; x < bw && x < o->w; x++) o->pix[(u64)dy * o->w + x] = row[x]; }
  return (i32)lines;
}
static u64 WINAPI w_CreateDIBitmap(u64 dc, const u8 *bih, u32 init, const void *bits, const u8 *bmi, u32 usage){
  (void)dc; int w = *(const i32 *)(bih + 4), h = *(const i32 *)(bih + 8); if(h < 0) h = -h; u64 hb = w_CreateBitmap(w, h, 1, 32, 0); if(hb && (init & 4) && bits && bmi) w_SetDIBits(0, hb, 0, (u32)h, bits, bmi, usage); return hb;
}

/* ---------- AlphaBlend, TransparentBlt, GradientFill ---------- */
static i32 WINAPI w_AlphaBlend(u64 dst, i32 dx, i32 dy, i32 dw, i32 dh, u64 src, i32 sx, i32 sy, i32 sw, i32 sh, u32 bf){
  struct wnd *d = dc_target(dst), *s = dc_target(src); if(!d || !s || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 0; dib_sync_dc(src);
  u32 ca = (bf >> 16) & 255, fmt = (bf >> 24) & 255; struct dcst *sd = dc_state(src); struct gobj *so = sd && sd->mem ? go_get(sd->bmp) : 0; int per_pixel = fmt == 1 && so && so->dib && so->bpp == 32;
  int cl[4]; int hc = dc_clip(dst, cl);
  for(int j = 0; j < dh; j++){ int y = dy + j; if(y < 0 || y >= d->ph || (hc && (y < cl[1] || y >= cl[3]))) continue; int yy = sy + j * sh / dh; if(yy < 0 || yy >= s->ph) continue;
    for(int i = 0; i < dw; i++){ int x = dx + i; if(x < 0 || x >= d->pw || (hc && (x < cl[0] || x >= cl[2]))) continue; int xx = sx + i * sw / dw; if(xx < 0 || xx >= s->pw) continue;
      u32 p = s->pix[(u64)yy * s->stride + xx]; u32 a = ca; if(per_pixel) a = a * (p >> 24) / 255; if(!a) continue;
      u32 *dp = &d->pix[(u64)y * d->stride + x]; u32 q = *dp;
      if(per_pixel && ca == 255){ u32 pa = p >> 24; if(pa == 255){ *dp = p & 0xFFFFFF; continue; } }
      u32 r = (((p >> 16) & 255) * a + ((q >> 16) & 255) * (255 - a)) / 255, g = (((p >> 8) & 255) * a + ((q >> 8) & 255) * (255 - a)) / 255, bl = ((p & 255) * a + (q & 255) * (255 - a)) / 255; *dp = (r << 16) | (g << 8) | bl; } }
  dirty = 1; return 1;
}
static i32 WINAPI w_TransparentBlt(u64 dst, i32 dx, i32 dy, i32 dw, i32 dh, u64 src, i32 sx, i32 sy, i32 sw, i32 sh, u32 key){
  struct wnd *d = dc_target(dst), *s = dc_target(src); if(!d || !s || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 0; dib_sync_dc(src); u32 k = cref(key) & 0xFFFFFF; int cl[4]; int hc = dc_clip(dst, cl);
  for(int j = 0; j < dh; j++){ int y = dy + j; if(y < 0 || y >= d->ph || (hc && (y < cl[1] || y >= cl[3]))) continue; int yy = sy + j * sh / dh; if(yy < 0 || yy >= s->ph) continue;
    for(int i = 0; i < dw; i++){ int x = dx + i; if(x < 0 || x >= d->pw || (hc && (x < cl[0] || x >= cl[2]))) continue; int xx = sx + i * sw / dw; if(xx < 0 || xx >= s->pw) continue;
      u32 p = s->pix[(u64)yy * s->stride + xx] & 0xFFFFFF; if(p != k) d->pix[(u64)y * d->stride + x] = p; } }
  dirty = 1; return 1;
}
/* TRIVERTEX: x i32, y i32, then 16-bit Red, Green, Blue, Alpha (0..0xFF00) */
static u32 lerp_col(u32 a, u32 b, int num, int den){ if(den <= 0) return a; u32 r = ((a >> 16 & 255) * (den - num) + (b >> 16 & 255) * num) / den, g = ((a >> 8 & 255) * (den - num) + (b >> 8 & 255) * num) / den, bl = ((a & 255) * (den - num) + (b & 255) * num) / den; return (r << 16) | (g << 8) | bl; }
static u32 tv_col(const u8 *v){ return ((u32)(*(const u16 *)(v + 8) >> 8) << 16) | ((u32)(*(const u16 *)(v + 10) >> 8) << 8) | (u32)(*(const u16 *)(v + 12) >> 8); }
static i32 WINAPI w_GradientFill(u64 dc, const u8 *verts, u32 nv, const void *mesh, u32 nm, u32 mode){
  struct wnd *t = dc_target(dc); if(!t || !verts) return 0;
  if(mode <= 1){                                                           /* GRADIENT_FILL_RECT_H / _V */
    for(u32 m = 0; m < nm; m++){ u32 ul = ((const u32 *)mesh)[m * 2], lr = ((const u32 *)mesh)[m * 2 + 1]; if(ul >= nv || lr >= nv) continue; const u8 *a = verts + ul * 16, *b = verts + lr * 16;
      int x0 = *(const i32 *)a, y0 = *(const i32 *)(a + 4), x1 = *(const i32 *)b, y1 = *(const i32 *)(b + 4); u32 c0 = tv_col(a), c1 = tv_col(b);
      if(mode == 0){ for(int x = x0; x < x1; x++) wm_gdi_rect(dc, x, y0, 1, y1 - y0, lerp_col(c0, c1, x - x0, x1 - x0 - 1)); }
      else { for(int y = y0; y < y1; y++) wm_gdi_rect(dc, x0, y, x1 - x0, 1, lerp_col(c0, c1, y - y0, y1 - y0 - 1)); } }
  } else {                                                                 /* triangles: barycentric interpolation */
    for(u32 m = 0; m < nm; m++){ const u32 *tri = (const u32 *)mesh + m * 3; if(tri[0] >= nv || tri[1] >= nv || tri[2] >= nv) continue; const u8 *v0 = verts + tri[0] * 16, *v1 = verts + tri[1] * 16, *v2 = verts + tri[2] * 16;
      int x0 = *(const i32 *)v0, y0 = *(const i32 *)(v0 + 4), x1 = *(const i32 *)v1, y1 = *(const i32 *)(v1 + 4), x2 = *(const i32 *)v2, y2 = *(const i32 *)(v2 + 4); u32 c0 = tv_col(v0), c1 = tv_col(v1), c2 = tv_col(v2);
      i64 det = (i64)(y1 - y2) * (x0 - x2) + (i64)(x2 - x1) * (y0 - y2); if(!det) continue; int minx = imin_(x0, imin_(x1, x2)), maxx = imax_(x0, imax_(x1, x2)), miny = imin_(y0, imin_(y1, y2)), maxy = imax_(y0, imax_(y1, y2));
      if(maxy - miny > 2000 || maxx - minx > 2000) continue;
      for(int y = miny; y <= maxy; y++) for(int x = minx; x <= maxx; x++){ i64 l0 = ((i64)(y1 - y2) * (x - x2) + (i64)(x2 - x1) * (y - y2)) * 1024 / det, l1 = ((i64)(y2 - y0) * (x - x2) + (i64)(x0 - x2) * (y - y2)) * 1024 / det, l2 = 1024 - l0 - l1;
        if(l0 < 0 || l1 < 0 || l2 < 0) continue; u32 r = (u32)(((c0 >> 16 & 255) * l0 + (c1 >> 16 & 255) * l1 + (c2 >> 16 & 255) * l2) / 1024), g = (u32)(((c0 >> 8 & 255) * l0 + (c1 >> 8 & 255) * l1 + (c2 >> 8 & 255) * l2) / 1024), b = (u32)(((c0 & 255) * l0 + (c1 & 255) * l1 + (c2 & 255) * l2) / 1024);
        wm_gdi_rect(dc, x, y, 1, 1, (r << 16) | (g << 8) | b); } }
  }
  dirty = 1; return 1;
}

#define G2(d, n) {d, #n, w_##n}
#define GDI2_EXPORTS \
  G2("gdi32",IntersectClipRect), G2("gdi32",ExcludeClipRect), G2("gdi32",GetClipBox), G2("gdi32",CreateRectRgn), G2("gdi32",CreateRectRgnIndirect), G2("gdi32",CreateRoundRectRgn), G2("gdi32",CreateEllipticRgn), G2("gdi32",CreateEllipticRgnIndirect), \
  G2("gdi32",CreatePolygonRgn), G2("gdi32",SetRectRgn), G2("gdi32",CombineRgn), G2("gdi32",GetRgnBox), G2("gdi32",OffsetRgn), G2("gdi32",PtInRegion), G2("gdi32",RectInRegion), G2("gdi32",EqualRgn), G2("gdi32",SelectClipRgn), \
  G2("gdi32",ExtSelectClipRgn), G2("user32",SetWindowRgn), G2("gdi32",GetClipRgn), G2("gdi32",FillRgn), G2("gdi32",PaintRgn), G2("gdi32",FrameRgn), G2("user32",InvalidateRgn), G2("user32",ValidateRgn), G2("user32",FrameRect), G2("user32",InvertRect), \
  G2("gdi32",SaveDC), G2("gdi32",RestoreDC), G2("gdi32",SetStretchBltMode), G2("gdi32",GetStretchBltMode), G2("gdi32",SetROP2), G2("gdi32",GetROP2), G2("gdi32",GetBkColor), G2("gdi32",GetTextAlign), G2("gdi32",SetPixelV), \
  {"gdi32","SetViewportOrgEx",w_OrgEx}, {"gdi32","SetWindowOrgEx",w_OrgEx}, {"gdi32","OffsetViewportOrgEx",w_OrgEx}, {"gdi32","OffsetWindowOrgEx",w_OrgEx}, {"gdi32","GetViewportOrgEx",w_GetOrgEx}, {"gdi32","GetWindowOrgEx",w_GetOrgEx}, \
  {"gdi32","SetBrushOrgEx",w_OrgEx}, {"gdi32","GdiFlush",w_True}, {"gdi32","SetGraphicsMode",w_True}, {"gdi32","SetPolyFillMode",w_True}, {"gdi32","SetMapperFlags",w_True}, \
  G2("gdi32",Ellipse), G2("gdi32",RoundRect), G2("gdi32",Polygon), G2("gdi32",PolyPolygon), G2("gdi32",PolylineTo), G2("gdi32",PolyBezier), G2("gdi32",PolyBezierTo), G2("gdi32",Arc), G2("gdi32",Pie), G2("gdi32",Chord), \
  {"user32","DrawTextA",w_DrawTextA}, {"user32","DrawTextW",w_DrawTextW}, {"user32","DrawTextExA",w_DrawTextExA}, {"user32","DrawTextExW",w_DrawTextExW}, G2("gdi32",GetTextFaceA), G2("gdi32",GetTextFaceW), G2("gdi32",SetTextCharacterExtra), \
  G2("gdi32",CreateFontIndirectW), G2("gdi32",CreateFontW), G2("gdi32",GetObjectW), G2("gdi32",EnumFontFamiliesExA), G2("gdi32",EnumFontFamiliesExW), G2("gdi32",EnumFontFamiliesA), G2("gdi32",EnumFontFamiliesW), \
  G2("gdi32",CreateDIBSection), G2("gdi32",CreateBitmapIndirect), G2("gdi32",GetBitmapBits), G2("gdi32",SetBitmapBits), G2("gdi32",GetDIBits), G2("gdi32",SetDIBitsToDevice), G2("gdi32",SetDIBits), G2("gdi32",CreateDIBitmap), \
  G2("msimg32",AlphaBlend), G2("gdi32",AlphaBlend), G2("msimg32",TransparentBlt), G2("gdi32",TransparentBlt), G2("msimg32",GradientFill), G2("gdi32",GradientFill),
