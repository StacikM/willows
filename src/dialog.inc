/* Willows dialog controls (included into wm.c): push/check/radio/group buttons, labels, single-line edits, list boxes,
   combo boxes and a tree view, all drawn with the proportional UI font. Controls live in K_CTRL windows. */
#define WM_SETFONT 0x30
#define WM_GETFONT 0x31
#define WM_ENABLE 0xA
#define WM_KEYUP 0x101
#define WM_INITDIALOG 0x110
#define WM_NOTIFY 0x4E
#define WM_LBUTTONDBLCLK 0x203
#define BM_GETCHECK 0xF0
#define BM_SETCHECK 0xF1
#define BM_GETSTATE 0xF2
#define BM_SETSTATE 0xF3
#define BM_SETSTYLE 0xF4
#define BM_CLICK 0xF5
#define EM_GETSEL 0xB0
#define EM_SETSEL 0xB1
#define EM_REPLACESEL 0xC2
#define EM_LIMITTEXT 0xC5
#define EM_SETPASSWORDCHAR 0xCC
#define EM_SETREADONLY 0xCF
#define LB_ADDSTRING 0x180
#define LB_INSERTSTRING 0x181
#define LB_DELETESTRING 0x182
#define LB_RESETCONTENT 0x184
#define LB_SETSEL 0x185
#define LB_SETCURSEL 0x186
#define LB_GETSEL 0x187
#define LB_GETCURSEL 0x188
#define LB_GETTEXT 0x189
#define LB_GETTEXTLEN 0x18A
#define LB_GETCOUNT 0x18B
#define LB_FINDSTRING 0x18F
#define LB_GETTOPINDEX 0x18E
#define LB_SETTOPINDEX 0x197
#define LB_GETITEMDATA 0x199
#define LB_SETITEMDATA 0x19A
#define LB_SETITEMHEIGHT 0x1A0
#define LB_FINDSTRINGEXACT 0x1A2
#define CB_GETEDITSEL 0x140
#define CB_LIMITTEXT 0x141
#define CB_SETEDITSEL 0x142
#define CB_ADDSTRING 0x143
#define CB_DELETESTRING 0x144
#define CB_GETCOUNT 0x146
#define CB_GETCURSEL 0x147
#define CB_GETLBTEXT 0x148
#define CB_GETLBTEXTLEN 0x149
#define CB_INSERTSTRING 0x14A
#define CB_RESETCONTENT 0x14B
#define CB_FINDSTRING 0x14C
#define CB_SELECTSTRING 0x14D
#define CB_SETCURSEL 0x14E
#define CB_SHOWDROPDOWN 0x14F
#define CB_GETITEMDATA 0x150
#define CB_SETITEMDATA 0x151
#define CB_FINDSTRINGEXACT 0x158
#define CB_SETDROPPEDWIDTH 0x160
#define TVM_INSERTITEMA 0x1100
#define TVM_DELETEITEM 0x1101
#define TVM_EXPAND 0x1102
#define TVM_GETCOUNT 0x1105
#define TVM_GETNEXTITEM 0x110A
#define TVM_SELECTITEM 0x110B
#define TVM_GETITEMA 0x110C
#define TVM_SETITEMA 0x110D
#define TVM_ENSUREVISIBLE 0x1114
#define TVM_SETIMAGELIST 0x1109
#define TVM_SETBKCOLOR 0x111D
#define TVM_SETTEXTCOLOR 0x111E
#define TVM_SETITEMHEIGHT 0x111B
#define TVN_SELCHANGEDA (-402)
#define NM_CLICK (-2)
#define NM_DBLCLK (-3)
#define BN_CLICKED 0
#define LBN_SELCHANGE 1
#define LBN_DBLCLK 2
#define CBN_SELCHANGE 1
#define CBN_EDITCHANGE 5
#define EN_CHANGE 0x300
#define WS_TABSTOP 0x00010000u
#define WS_GROUP 0x00020000u
#define WS_DISABLED 0x08000000u
#define ES_PASSWORD 0x20
#define ES_READONLY 0x800
#define ES_MULTILINE 4
#define ES_AUTOHSCROLL 0x80

struct lst { int n, cap, sel, top; char **s; u64 *data; };
#define MF_POPUP 0x10
#define MF_SEPARATOR 0x800
#define MF_BYPOSITION 0x400
#define MF_GRAYED 1
#define MF_DISABLED 2
#define MF_CHECKED 8
struct menu_item { u32 flags; u64 id; char text[64]; };
struct menu { u8 used; int n; struct menu_item it[40]; };
static struct menu menus[32];
static i64 g_menu_result; static int g_menu_done;
struct tvi { char *text; u64 lparam; int parent, first, last, next, expanded, used; };
struct tree { struct tvi *it; int n, cap, sel, top; };
struct edt { int sel0, sel1; int limit; char pw; int scroll; };           /* single-line edit state (caret = sel1) */
static u64 g_dlg_focus_hint;

static i64 notify_parent(struct wnd *w, u32 code){
  if(!w->parent) return 0;
  post(w->parent, WM_COMMAND, ((u64)code << 16) | (w->id & 0xFFFF), (i64)hof(w)); return 0;
}
static int ctrl_enabled(struct wnd *w){ return !w->disabled; }
/* strip '&' accelerator marks for display; "&&" becomes "&" */
static int strip_amp(const char *s, char *out, int max){
  int k = 0; for(; *s && k < max - 1; s++){ if(*s == '&'){ if(s[1] == '&'){ out[k++] = '&'; s++; } continue; } out[k++] = *s; } out[k] = 0; return k;
}
static struct lst *lst_of(struct wnd *w){
  if(!w->ext){ w->ext = kmalloc(sizeof(struct lst), 16); }
  return (struct lst *)w->ext;
}
static int lst_insert(struct lst *l, int at, const char *s){
  if(l->n >= l->cap){ int nc = l->cap ? l->cap * 2 : 16; char **ns = kmalloc(nc * sizeof(char *), 16); u64 *nd = kmalloc(nc * sizeof(u64), 16); if(!ns || !nd) return -1;
    if(l->n){ memcpy(ns, l->s, l->n * sizeof(char *)); memcpy(nd, l->data, l->n * sizeof(u64)); } l->s = ns; l->data = nd; l->cap = nc; }
  if(at < 0 || at > l->n) at = l->n;
  for(int i = l->n; i > at; i--){ l->s[i] = l->s[i - 1]; l->data[i] = l->data[i - 1]; }
  u32 n = (u32)strlen(s); char *c = kmalloc(n + 1, 8); if(!c) return -1; memcpy(c, s, n + 1);
  l->s[at] = c; l->data[at] = 0; l->n++; if(l->sel >= at && l->sel >= 0 && l->n > 1) l->sel++; return at;
}
static void lst_delete(struct lst *l, int at){
  if(at < 0 || at >= l->n) return; for(int i = at; i < l->n - 1; i++){ l->s[i] = l->s[i + 1]; l->data[i] = l->data[i + 1]; } l->n--;
  if(l->sel == at) l->sel = -1; else if(l->sel > at) l->sel--;
}
static int lst_find(struct lst *l, const char *s, int exact, int from){
  int sl = (int)strlen(s);
  for(int k = 0; k < l->n; k++){ int i = (from + 1 + k) % l->n; if(exact ? !stricmp(l->s[i], s) : (int)strlen(l->s[i]) >= sl && !strncmp_ci_(l->s[i], s, sl)) return i; }
  return -1;
}
static int row_h(void){ return ui_lineh(ui(FS_REGULAR, 13)) + 4; }

/* ---- text helpers for single-line edits ---- */
static struct edt *edt_of(struct wnd *w){ if(!w->ext){ struct edt *e = kmalloc(sizeof *e, 16); if(e){ e->limit = 32000; e->sel0 = e->sel1 = (int)w->tlen; } w->ext = e; } return (struct edt *)w->ext; }
static void edt_notify(struct wnd *w){ notify_parent(w, EN_CHANGE); dirty = 1; }
static void edt_delete_sel(struct wnd *w, struct edt *e){
  int a = e->sel0 < e->sel1 ? e->sel0 : e->sel1, b = e->sel0 < e->sel1 ? e->sel1 : e->sel0; if(a == b) return;
  memmove(w->text + a, w->text + b, w->tlen - b + 1); w->tlen -= (u32)(b - a); e->sel0 = e->sel1 = a;
}
static void edt_insert(struct wnd *w, struct edt *e, const char *s, int n){
  edt_delete_sel(w, e); if((int)w->tlen + n > e->limit) n = e->limit - (int)w->tlen; if(n <= 0) return;
  if(w->tlen + n + 1 > w->tcap){ u32 c = (w->tlen + n + 1) * 2 + 32; char *nb = kmalloc(c, 16); if(!nb) return; if(w->text) memcpy(nb, w->text, w->tlen + 1); else nb[0] = 0; w->text = nb; w->tcap = c; }
  memmove(w->text + e->sel1 + n, w->text + e->sel1, w->tlen - e->sel1 + 1); memcpy(w->text + e->sel1, s, n); w->tlen += n; e->sel1 += n; e->sel0 = e->sel1;
}
static int edt_hit(struct wnd *w, struct edt *e, int x){
  const struct font *f = ui(FS_REGULAR, 13); int px = 5 - e->scroll; int best = 0;
  for(int i = 0; i <= (int)w->tlen; i++){ int cw = e->pw ? ui_width(f, "*", 1) * i : ui_width(f, w->text, i); if(px + cw <= x) best = i; else break; }
  return best;
}
static void edt_key(struct wnd *w, struct edt *e, int vk){
  int shift = kb_vk(0x10), ctrl = kb_vk(0x11); int c = e->sel1; int changed = 0;
  if(!w->text) return;
  switch(vk){
    case 0x25: if(c > 0) c--; if(ctrl) while(c > 0 && w->text[c - 1] != ' ') c--; goto move;
    case 0x27: if(c < (int)w->tlen) c++; if(ctrl) while(c < (int)w->tlen && w->text[c] != ' ') c++; goto move;
    case 0x24: c = 0; goto move;
    case 0x23: c = (int)w->tlen; goto move;
    move: e->sel1 = c; if(!shift) e->sel0 = c; dirty = 1; return;
    case 0x2E: if(e->sel0 == e->sel1){ if(e->sel1 < (int)w->tlen){ e->sel0 = e->sel1; e->sel1++; } } edt_delete_sel(w, e); changed = 1; break;
    case 0x41: if(ctrl){ e->sel0 = 0; e->sel1 = (int)w->tlen; dirty = 1; } return;
  }
  if(changed) edt_notify(w);
}

/* ---- tree helpers ---- */
static struct tree *tree_of(struct wnd *w){ if(!w->ext){ struct tree *t = kmalloc(sizeof *t, 16); if(t){ t->sel = -1; } w->ext = t; } return (struct tree *)w->ext; }
static int tree_add(struct tree *t, int parent, const char *text, u64 lparam){
  if(t->n >= t->cap){ int nc = t->cap ? t->cap * 2 : 32; struct tvi *ni = kmalloc(nc * sizeof(struct tvi), 16); if(!ni) return -1; if(t->n) memcpy(ni, t->it, t->n * sizeof(struct tvi)); t->it = ni; t->cap = nc; }
  int id = t->n++; struct tvi *v = &t->it[id]; u32 n = (u32)strlen(text); v->text = kmalloc(n + 1, 8); memcpy(v->text, text, n + 1);
  v->lparam = lparam; v->parent = parent; v->first = v->last = v->next = -1; v->expanded = 0; v->used = 1;
  if(parent >= 0){ struct tvi *p = &t->it[parent]; if(p->last < 0) p->first = id; else t->it[p->last].next = id; p->last = id; }
  else { int r = 0; int last = -1; for(int i = 0; i < id; i++) if(t->it[i].parent < 0){ last = i; } if(last >= 0) t->it[last].next = id; (void)r; }
  return id;
}
static int tree_visible(struct tree *t, int *out, int max){        /* items in display order */
  int n = 0; int stack[64]; int sp = 0; int first = -1; for(int i = 0; i < t->n; i++) if(t->it[i].parent < 0){ first = i; break; }
  int cur = first;
  while(cur >= 0 && n < max){
    out[n++] = cur;
    if(t->it[cur].expanded && t->it[cur].first >= 0 && sp < 64){ stack[sp++] = t->it[cur].next; cur = t->it[cur].first; continue; }
    int nx = t->it[cur].next;
    while(nx < 0 && sp > 0) nx = stack[--sp];
    cur = nx;
  }
  return n;
}
static int tree_depth(struct tree *t, int i){ int d = 0; while(t->it[i].parent >= 0){ i = t->it[i].parent; d++; } return d; }
static void tree_select(struct wnd *w, struct tree *t, int i, int notify){
  if(t->sel == i) return; t->sel = i;
  for(int p = i >= 0 ? t->it[i].parent : -1; p >= 0; p = t->it[p].parent) t->it[p].expanded = 1;
  dirty = 1;
  if(notify && w->parent){                                              /* WM_NOTIFY(TVN_SELCHANGED) with an NMTREEVIEW in kernel memory */
    static u64 nm[16]; memset(nm, 0, sizeof nm); nm[0] = hof(w); nm[1] = w->id & 0xFFFF; ((i32*)nm)[4] = TVN_SELCHANGEDA; ((u32*)nm)[6] = 0;
    /* NMTREEVIEWA: NMHDR(24) action(u32) itemOld{mask,hItem,...} itemNew{...} ptDrag */
    u8 *b = (u8*)nm; *(u32*)(b + 24) = 1; *(u32*)(b + 28 + 0) = 0;
    u8 *itn = b + 24 + 8 + 56;                                         /* itemNew starts after action(4+pad) and itemOld(56) */
    *(u32*)itn = 0x0004 | 0x0008; *(u64*)(itn + 8) = (u64)(i + 1); *(u64*)(itn + 48) = t->it[i].lparam;
    post(w->parent, WM_NOTIFY, w->id & 0xFFFF, (i64)nm);
  }
}

/* ---- drawing ---- */
static void focus_rect(int x, int y, int w, int h){ for(int i = 0; i < w; i += 2){ gfx_rect(x + i, y, 1, 1, 0x6B7280); gfx_rect(x + i, y + h - 1, 1, 1, 0x6B7280); } for(int j = 0; j < h; j += 2){ gfx_rect(x, y + j, 1, 1, 0x6B7280); gfx_rect(x + w - 1, y + j, 1, 1, 0x6B7280); } }
static void draw_wrapped(const struct font *f, int x, int y, int w, int h, const char *s, u32 col, int align){
  int lh = ui_lineh(f); int ly = y; const char *p = s;
  while(*p && ly + lh <= y + h + lh){
    const char *line_end = p; int last_space = -1; int cnt = 0;
    while(line_end[cnt] && line_end[cnt] != '\n'){ if(line_end[cnt] == ' ') last_space = cnt; if(ui_width(f, p, cnt + 1) > w){ if(last_space > 0) cnt = last_space; break; } cnt++; }
    int len = cnt; while(len > 0 && p[len - 1] == ' ') len--;
    int tw = ui_width(f, p, len); int tx = align == 1 ? x + (w - tw) / 2 : align == 2 ? x + w - tw : x;
    ui_text(f, tx, ly, p, len, col); ly += lh;
    p += cnt; if(*p == ' ' || *p == '\n') p++;
    if(!*p) break;
  }
}
static void draw_scroll_thumb(int x, int y, int h, int n, int vis, int top){
  if(n <= vis) return; int th = h * vis / n; if(th < 16) th = 16; int ty = y + (h - th) * top / (n - vis); gfx_round_rect(x, ty, 8, th, 3, 0xC5CAD3);
}
static void ctrl_draw(struct wnd *w, int ox, int oy){
  const struct font *f = ui(FS_REGULAR, 13); int lh = ui_lineh(f); int en = ctrl_enabled(w); u32 tcol = en ? TXT_DARK : 0x9CA3AF;
  int focus = hof(w) == g_focus; char lab[256]; strip_amp(w->text ? w->text : "", lab, sizeof lab); int ln = (int)strlen(lab);
  switch(w->ctype){
    case CT_PUSH: {
      int def = (w->style & 0xF) == 1; u32 bg = !en ? 0xEEF0F3 : w->pressed ? 0xD1D5DB : 0xE9ECF1;
      gfx_round_rect(ox, oy, w->w, w->h, 4, def && en ? ACCENT : (en ? 0x9CA3AF : 0xD1D5DB)); gfx_round_rect(ox + 1, oy + 1, w->w - 2, w->h - 2, 4, bg);
      int tw = ui_width(f, lab, ln); int k = ln; if(tw > w->w - 8){ k = fit(f, lab, ln, w->w - 8); tw = ui_width(f, lab, k); }
      ui_text(f, ox + (w->w - tw) / 2, oy + (w->h - lh) / 2 + (w->pressed ? 1 : 0), lab, k, tcol);
      if(focus) focus_rect(ox + 4, oy + 4, w->w - 8, w->h - 8); return; }
    case CT_CHECK: case CT_RADIO: {
      int bx = ox, by = oy + (w->h - 14) / 2; int r = w->ctype == CT_RADIO ? 7 : 3;
      gfx_round_rect(bx, by, 14, 14, r, en ? 0x6B7280 : 0xC5CAD3); gfx_round_rect(bx + 1, by + 1, 12, 12, r, en ? 0xFFFFFF : 0xF3F4F6);
      if(w->check == 1){ if(w->ctype == CT_RADIO) gfx_round_rect(bx + 4, by + 4, 6, 6, 3, en ? ACCENT : 0x9CA3AF); else { gfx_round_rect(bx + 3, by + 3, 8, 8, 2, en ? ACCENT : 0x9CA3AF); } }
      else if(w->check == 2){ gfx_rect(bx + 3, by + 6, 8, 2, 0x9CA3AF); }
      int tx = ox + 20; int k = fit(f, lab, ln, w->w - 20); ui_text(f, tx, oy + (w->h - lh) / 2, lab, k, tcol);
      if(focus) focus_rect(tx - 2, oy + (w->h - lh) / 2 - 1, ui_width(f, lab, k) + 4, lh + 2); return; }
    case CT_GROUP: {
      int tw = ui_width(f, lab, ln); int ty = oy + lh / 2;
      gfx_rect(ox, ty, w->w, 1, 0xC5CAD3); gfx_rect(ox, oy + w->h - 1, w->w, 1, 0xC5CAD3); gfx_rect(ox, ty, 1, w->h - lh / 2, 0xC5CAD3); gfx_rect(ox + w->w - 1, ty, 1, w->h - lh / 2, 0xC5CAD3);
      if(ln){ gfx_rect(ox + 8, oy, tw + 8, lh, CHROME_BG); ui_text(f, ox + 12, oy, lab, ln, 0x4B5563); } return; }
    case CT_LINE: gfx_rect(ox, oy + w->h / 2, w->w, 1, 0xC5CAD3); return;
    case CT_STATIC: {
      int al = (w->style & 3) == 1 ? 1 : (w->style & 3) == 2 ? 2 : 0;
      if((w->style & 0x1F) == 0x10 || (w->style & 0x1F) == 0x11 || (w->style & 0x1F) == 5 || (w->style & 0x1F) == 6){ gfx_rect(ox, oy, w->w, w->h, 0xC5CAD3); gfx_rect(ox + 1, oy + 1, w->w - 2, w->h - 2, CHROME_BG); return; }
      if(w->tlen == 0) return;
      if((w->style & 0x1F) == 0xC) { ui_text(f, ox, oy, lab, ln, tcol); return; }
      draw_wrapped(f, ox, oy, w->w, w->h, lab, tcol, al); return; }
    case CT_EDIT: {
      struct edt *e = edt_of(w);
      gfx_rect(ox, oy, w->w, w->h, focus ? ACCENT : (en ? 0x9CA3AF : 0xD1D5DB)); gfx_rect(ox + 1, oy + 1, w->w - 2, w->h - 2, en && !(w->style & ES_READONLY) ? 0xFFFFFF : 0xF3F4F6);
      char show[600]; int n = (int)w->tlen; if(n > 590) n = 590; if(e->pw){ for(int i = 0; i < n; i++) show[i] = '*'; } else memcpy(show, w->text ? w->text : "", n); show[n] = 0;
      int caret = e->sel1; if(caret > n) caret = n; int cx = ui_width(f, show, caret);
      if(cx - e->scroll > w->w - 12) e->scroll = cx - (w->w - 12); if(cx - e->scroll < 0) e->scroll = cx;
      int a = e->sel0 < e->sel1 ? e->sel0 : e->sel1, b = e->sel0 < e->sel1 ? e->sel1 : e->sel0; if(b > n) b = n; if(a > n) a = n;
      int ty = oy + (w->h - lh) / 2; int x0 = ox + 5 - e->scroll;
      if(a != b && focus){ int xa = ui_width(f, show, a), xb = ui_width(f, show, b); int sx = x0 + xa, sw = xb - xa; if(sx < ox + 2){ sw -= ox + 2 - sx; sx = ox + 2; } if(sx + sw > ox + w->w - 2) sw = ox + w->w - 2 - sx; if(sw > 0) gfx_rect(sx, ty, sw, lh, 0xBFD7FF); }
      /* clip by only drawing the characters that fit */
      int first = 0; while(first < n && ui_width(f, show, first) < e->scroll) first++; if(first > 0) first--;
      int last = n; while(last > first && ui_width(f, show, last - 1) - e->scroll > w->w - 8) last--;
      if(last > first) ui_text(f, x0 + ui_width(f, show, first), ty, show + first, last - first, tcol);
      if(focus && ((ticks / 500) & 1) == 0 && en) gfx_rect(x0 + cx, ty, 1, lh, 0x111827);
      return; }
    case CT_LIST: {
      struct lst *l = lst_of(w); int rh = row_h(); int vis = (w->h - 4) / rh;
      gfx_rect(ox, oy, w->w, w->h, focus ? ACCENT : 0x9CA3AF); gfx_rect(ox + 1, oy + 1, w->w - 2, w->h - 2, en ? 0xFFFFFF : 0xF3F4F6);
      for(int i = 0; i < vis; i++){
        int idx = l->top + i; if(idx >= l->n) break; int ry = oy + 2 + i * rh;
        if(idx == l->sel){ gfx_rect(ox + 2, ry, w->w - 4, rh, focus ? ACCENT : 0xC5CAD3); }
        int tw = w->w - 14; int k = fit(f, l->s[idx], (int)strlen(l->s[idx]), tw);
        ui_text(f, ox + 6, ry + 2, l->s[idx], k, idx == l->sel && focus ? 0xFFFFFF : tcol);
      }
      draw_scroll_thumb(ox + w->w - 10, oy + 2, w->h - 4, l->n, vis, l->top); return; }
    case CT_COMBO: {
      struct lst *l = lst_of(w); int drop = (w->style & 3) == 3;
      gfx_rect(ox, oy, w->w, w->h, focus ? ACCENT : (en ? 0x9CA3AF : 0xD1D5DB)); gfx_rect(ox + 1, oy + 1, w->w - 2, w->h - 2, en ? 0xFFFFFF : 0xF3F4F6);
      const char *t = (drop && l->sel >= 0 && l->sel < l->n) ? l->s[l->sel] : (w->text ? w->text : ""); int n = (int)strlen(t);
      int k = fit(f, t, n, w->w - 28); ui_text(f, ox + 6, oy + (w->h - lh) / 2, t, k, tcol);
      if(focus && !drop && ((ticks / 500) & 1) == 0) gfx_rect(ox + 6 + ui_width(f, t, k), oy + (w->h - lh) / 2, 1, lh, 0x111827);
      gfx_rect(ox + w->w - 20, oy + 1, 1, w->h - 2, 0xE5E7EB);
      int ax = ox + w->w - 14, ay = oy + w->h / 2 - 1; gfx_rect(ax, ay, 8, 2, 0x4B5563); gfx_rect(ax + 1, ay + 2, 6, 2, 0x4B5563); gfx_rect(ax + 2, ay + 4, 4, 2, 0x4B5563); gfx_rect(ax + 3, ay + 6, 2, 1, 0x4B5563);
      if(focus && drop) focus_rect(ox + 3, oy + 3, w->w - 26, w->h - 6); return; }
    case CT_TREE: {
      struct tree *t = tree_of(w); int rh = row_h(); int vis = (w->h - 4) / rh; int order[512]; int n = tree_visible(t, order, 512);
      gfx_rect(ox, oy, w->w, w->h, focus ? ACCENT : 0x9CA3AF); gfx_rect(ox + 1, oy + 1, w->w - 2, w->h - 2, 0xFFFFFF);
      for(int i = 0; i < vis; i++){
        int vi = t->top + i; if(vi >= n) break; int idx = order[vi]; int ry = oy + 2 + i * rh; int depth = tree_depth(t, idx); int x = ox + 6 + depth * 16;
        if(idx == t->sel) gfx_rect(ox + 2, ry, w->w - 4, rh, focus ? ACCENT : 0xC5CAD3);
        if(t->it[idx].first >= 0){ u32 ac = idx == t->sel && focus ? 0xFFFFFF : 0x6B7280; if(t->it[idx].expanded){ gfx_rect(x, ry + rh / 2 - 1, 7, 2, ac); gfx_rect(x + 1, ry + rh / 2 + 1, 5, 2, ac); gfx_rect(x + 2, ry + rh / 2 + 3, 3, 1, ac); } else { gfx_rect(x + 1, ry + rh / 2 - 4, 2, 8, ac); gfx_rect(x + 3, ry + rh / 2 - 3, 2, 6, ac); gfx_rect(x + 5, ry + rh / 2 - 1, 1, 2, ac); } }
        int tx = x + 12; int k = fit(f, t->it[idx].text, (int)strlen(t->it[idx].text), w->w - (tx - ox) - 10);
        ui_text(f, tx, ry + 2, t->it[idx].text, k, idx == t->sel && focus ? 0xFFFFFF : tcol);
      }
      draw_scroll_thumb(ox + w->w - 10, oy + 2, w->h - 4, n, vis, t->top); return; }
  }
}

/* ---- behaviour ---- */
static void check_radio_group(struct wnd *w){                          /* selecting a radio button clears the others of its group */
  u64 parent = w->parent; int start = (int)(w - W);
  int lo = start, hi = start;                                          /* group = run of radio buttons by creation order that share a WS_GROUP start */
  /* walk back to the control that starts the group */
  struct wnd *best = w; u32 bseq = w->seq;
  for(int i = 0; i < MAXW; i++){ struct wnd *c = &W[i]; if(!c->used || c->parent != parent || c->kind != K_CTRL) continue; if(c->seq < bseq && (c->style & WS_GROUP)){ if(best == w || c->seq > best->seq) best = c; } }
  (void)lo; (void)hi;
  u32 gstart = (w->style & WS_GROUP) ? w->seq : (best != w ? best->seq : 0);
  u32 gend = 0xFFFFFFFFu; for(int i = 0; i < MAXW; i++){ struct wnd *c = &W[i]; if(c->used && c->parent == parent && c->kind == K_CTRL && c->seq > gstart && c->seq > w->seq && (c->style & WS_GROUP) && c->seq < gend) gend = c->seq; }
  for(int i = 0; i < MAXW; i++){ struct wnd *c = &W[i]; if(c->used && c->parent == parent && c->kind == K_CTRL && c->ctype == CT_RADIO && c != w && c->seq >= gstart && c->seq < gend){ c->check = 0; } }
}
static void combo_close_popup(struct wnd *w){ if(w->userdata){ u64 p = w->userdata; w->userdata = 0; if(get(p)) destroy(p); dirty = 1; } }
static i64 ctrl_proc(u64 h, u32 m, u64 wp, i64 lp);
static void combo_open(struct wnd *w){
  struct lst *l = lst_of(w); if(w->userdata || l->n == 0) return;
  int rh = row_h(); int rows = l->n < 8 ? l->n : 8; if(w->extra[0] > 22){ int fit_rows = ((int)w->extra[0] - 4) / rh; if(fit_rows >= 3 && fit_rows < rows) rows = fit_rows; } int ph = rows * rh + 4;
  int idx = -1; for(int i = 0; i < MAXW; i++) if(!W[i].used){ idx = i; break; } if(idx < 0) return;
  struct wnd *p = &W[idx]; memset(p, 0, sizeof *p); p->used = 1; p->owner = w->owner; p->kind = K_CTRL; p->ctype = CT_LIST; p->proc = B_CTRL; p->style = WS_CHILD | WS_VISIBLE | 0x80000000u; p->parent = w->parent;
  p->x = w->x; p->y = w->y + w->h; p->w = w->w; p->h = ph; p->id = 0xC0DE; p->visible = 1; p->seq = ++g_seq; p->check = (int)(w - W);   /* check holds the owning combo's slot */
  struct lst *pl = lst_of(p); for(int i = 0; i < l->n; i++) lst_insert(pl, -1, l->s[i]);
  pl->sel = l->sel; if(pl->sel >= rows) pl->top = pl->sel - rows + 1;
  w->userdata = hof(p); set_focus(hof(p)); dirty = 1;
}
static i64 ctrl_proc(u64 h, u32 m, u64 wp, i64 lp){
  struct wnd *w = get(h); if(!w) return 0;
  int en = ctrl_enabled(w);
  switch(m){
    case WM_SETFONT: case WM_GETFONT: return 0;
    case WM_ENABLE: w->disabled = wp ? 0 : 1; dirty = 1; return 0;
    case WM_SETTEXT:
      settext(w, (const char *)lp); if(w->ctype == CT_EDIT){ struct edt *e = edt_of(w); e->sel0 = e->sel1 = (int)w->tlen; e->scroll = 0; }
      dirty = 1; return 1;
    case WM_SETFOCUS: case WM_KILLFOCUS:
      dirty = 1;
      if(m == WM_KILLFOCUS && w->ctype == CT_LIST && w->id == 0xC0DD){ g_menu_done = 1; destroy(h); return 0; }
      if(m == WM_KILLFOCUS && w->ctype == CT_LIST && w->id == 0xC0DE){                    /* a combo popup closes when it loses focus */
        struct wnd *cb = &W[w->check]; if(cb->used && cb->userdata == h){ cb->userdata = 0; } destroy(h); return 0; }
      if(m == WM_SETFOCUS && w->ctype == CT_EDIT){ struct edt *e = edt_of(w); e->sel0 = 0; e->sel1 = (int)w->tlen; }
      return 0;
    case WM_GETTEXT: case WM_GETTEXTLENGTH: return def_proc(h, m, wp, lp);
  }
  if(!en && (m == WM_LBUTTONDOWN || m == WM_KEYDOWN || m == WM_CHAR || m == WM_LBUTTONUP)) return 0;
  /* ---- push / check / radio ---- */
  if(w->ctype == CT_PUSH || w->ctype == CT_CHECK || w->ctype == CT_RADIO){
    switch(m){
      case BM_GETCHECK: return w->check;
      case BM_SETCHECK: w->check = (int)wp; if(w->ctype == CT_RADIO && wp) check_radio_group(w); dirty = 1; return 0;
      case BM_GETSTATE: return (w->check == 1 ? 3 : 0) | (w->pressed ? 4 : 0);
      case BM_SETSTYLE: w->style = (w->style & ~0xFu) | ((u32)wp & 0xF); dirty = 1; return 0;
      case WM_LBUTTONDOWN: w->pressed = 1; g_capture = h; set_focus(h); dirty = 1; return 0;
      case WM_LBUTTONUP: if(w->pressed){ w->pressed = 0; dirty = 1; int x = (int)(lp & 0xFFFF), y = (int)((lp >> 16) & 0xFFFF);
          if(x >= 0 && x < w->w && y >= 0 && y < w->h) goto click; } return 0;
      case BM_CLICK: click: {
          u32 t = w->style & 0xF;
          if(w->ctype == CT_CHECK){ if(t == 5 || t == 6) w->check = (w->check + 1) % 3; else if(t == 3 || t == 4) w->check = !w->check; }
          else if(w->ctype == CT_RADIO){ if(t == 9 || t == 4 + 5) { w->check = 1; check_radio_group(w); } }
          notify_parent(w, BN_CLICKED); dirty = 1; return 0; }
      case WM_KEYDOWN: if(wp == 0x20) return 0; return 0;
      case WM_CHAR: if(wp == 0x20){ ctrl_proc(h, BM_CLICK, 0, 0); } return 0;
    }
    return 0;
  }
  if(w->ctype == CT_EDIT){
    struct edt *e = edt_of(w);
    switch(m){
      case EM_SETSEL: { int a = (int)(i32)wp, b = (int)(i32)lp; if(a < 0){ a = b = e->sel1; } if(b < 0 || b > (int)w->tlen) b = (int)w->tlen; if(a > (int)w->tlen) a = (int)w->tlen; e->sel0 = a; e->sel1 = b; dirty = 1; return 0; }
      case EM_GETSEL: { if(wp) *(u32 *)wp = (u32)(e->sel0 < e->sel1 ? e->sel0 : e->sel1); if(lp) *(u32 *)lp = (u32)(e->sel0 < e->sel1 ? e->sel1 : e->sel0); return 0; }
      case EM_LIMITTEXT: e->limit = wp ? (int)wp : 32000; return 0;
      case EM_SETPASSWORDCHAR: e->pw = (char)wp; dirty = 1; return 0;
      case EM_SETREADONLY: if(wp) w->style |= ES_READONLY; else w->style &= ~ES_READONLY; dirty = 1; return 0;
      case EM_REPLACESEL: edt_insert(w, e, (const char *)lp, (int)strlen((const char *)lp)); edt_notify(w); return 0;
      case WM_LBUTTONDOWN: set_focus(h); e->sel1 = edt_hit(w, e, (int)(lp & 0xFFFF)); e->sel0 = e->sel1; g_capture = h; w->pressed = 1; dirty = 1; return 0;
      case WM_MOUSEMOVE: if(w->pressed && (g_capture == h)){ e->sel1 = edt_hit(w, e, (int)(lp & 0xFFFF)); dirty = 1; } return 0;
      case WM_LBUTTONUP: w->pressed = 0; return 0;
      case WM_KEYDOWN: edt_key(w, e, (int)wp); return 0;
      case WM_CHAR: {
        int c = (int)wp; if(w->style & ES_READONLY) return 0;
        if(c == 8){ if(e->sel0 == e->sel1 && e->sel1 > 0) e->sel0 = e->sel1 - 1; edt_delete_sel(w, e); edt_notify(w); }
        else if(c >= 32 && c < 127){ char ch = (char)c; edt_insert(w, e, &ch, 1); edt_notify(w); }
        else if(c >= 160 && c < 256){ char u[2] = {(char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 0x3F))}; edt_insert(w, e, u, 2); edt_notify(w); }
        return 0; }
    }
    return 0;
  }
  if(w->ctype == CT_LIST){
    struct lst *l = lst_of(w); int rh = row_h(); int vis = (w->h - 4) / rh;
    switch(m){
      case LB_ADDSTRING: case CB_ADDSTRING: return lst_insert(l, -1, (const char *)lp);
      case LB_INSERTSTRING: case CB_INSERTSTRING: return lst_insert(l, (int)wp, (const char *)lp);
      case LB_DELETESTRING: case CB_DELETESTRING: lst_delete(l, (int)wp); dirty = 1; return l->n;
      case LB_RESETCONTENT: case CB_RESETCONTENT: l->n = 0; l->sel = -1; l->top = 0; dirty = 1; return 0;
      case LB_SETCURSEL: case CB_SETCURSEL: l->sel = (int)(i32)wp < l->n ? (int)(i32)wp : -1; if(l->sel >= 0){ if(l->sel < l->top) l->top = l->sel; if(l->sel >= l->top + vis) l->top = l->sel - vis + 1; } dirty = 1; return l->sel >= 0 ? l->sel : -1;
      case LB_GETCURSEL: case CB_GETCURSEL: return l->sel;
      case LB_GETCOUNT: case CB_GETCOUNT: return l->n;
      case LB_GETTEXT: case CB_GETLBTEXT: if((int)wp < 0 || (int)wp >= l->n) return -1; strcpy_k((char *)lp, l->s[wp]); return (i64)strlen(l->s[wp]);
      case LB_GETTEXTLEN: case CB_GETLBTEXTLEN: if((int)wp < 0 || (int)wp >= l->n) return -1; return (i64)strlen(l->s[wp]);
      case LB_FINDSTRING: case CB_FINDSTRING: return l->n ? lst_find(l, (const char *)lp, 0, (int)(i32)wp) : -1;
      case LB_FINDSTRINGEXACT: case CB_FINDSTRINGEXACT: return l->n ? lst_find(l, (const char *)lp, 1, (int)(i32)wp) : -1;
      case LB_GETITEMDATA: case CB_GETITEMDATA: return (int)wp >= 0 && (int)wp < l->n ? (i64)l->data[wp] : -1;
      case LB_SETITEMDATA: case CB_SETITEMDATA: if((int)wp >= 0 && (int)wp < l->n) l->data[wp] = (u64)lp; return 0;
      case LB_GETTOPINDEX: return l->top;
      case LB_SETTOPINDEX: l->top = (int)wp; dirty = 1; return 0;
      case LB_GETSEL: return (int)wp == l->sel;
      case LB_SETSEL: return 0;
      case LB_SETITEMHEIGHT: return 0;
      case WM_LBUTTONDOWN: {
        set_focus(h); int y = (int)((lp >> 16) & 0xFFFF); int idx = l->top + (y - 2) / rh;
        if(idx >= 0 && idx < l->n){
          l->sel = idx; dirty = 1;
          if(w->id == 0xC0DD){ struct menu *mm = &menus[w->check]; if(!(mm->it[idx].flags & (MF_SEPARATOR | MF_GRAYED | MF_DISABLED))){ g_menu_result = (i64)mm->it[idx].id; g_menu_done = 1; } else return 0; destroy(h); return 0; }
          if(w->id == 0xC0DE){ struct wnd *cb = &W[w->check]; if(cb->used){ struct lst *cl = lst_of(cb); cl->sel = idx; if(!((cb->style & 3) == 3)){ settext(cb, l->s[idx]); } cb->userdata = 0; notify_parent(cb, CBN_SELCHANGE); } destroy(h); return 0; }
          notify_parent(w, LBN_SELCHANGE); }
        return 0; }
      case WM_LBUTTONDBLCLK: notify_parent(w, LBN_DBLCLK); return 0;
      case WM_MOUSEWHEEL: { int d = (int)(short)((wp >> 16) & 0xFFFF); l->top -= d / 120 * 3; if(l->top > l->n - vis) l->top = l->n - vis; if(l->top < 0) l->top = 0; dirty = 1; return 0; }
      case WM_KEYDOWN: {
        int vk = (int)wp; int s = l->sel; if(vk == 0x28 && s < l->n - 1) s++; else if(vk == 0x26 && s > 0) s--; else if(vk == 0x24) s = 0; else if(vk == 0x23) s = l->n - 1; else if(vk == 0x22) s = s + vis < l->n ? s + vis : l->n - 1; else if(vk == 0x21) s = s - vis > 0 ? s - vis : 0;
        else if(vk == 0x0D && w->id == 0xC0DE){ ctrl_proc(h, WM_LBUTTONDOWN, 0, ((i64)(2 + (l->sel - l->top) * rh) << 16)); return 0; }
        else if(vk == 0x1B && w->id == 0xC0DD){ g_menu_done = 1; destroy(h); return 0; }
        else if(vk == 0x0D && w->id == 0xC0DD){ ctrl_proc(h, WM_LBUTTONDOWN, 0, ((i64)(2 + (l->sel - l->top) * rh) << 16)); return 0; }
        else if(vk == 0x1B && w->id == 0xC0DE){ struct wnd *cb = &W[w->check]; if(cb->used) cb->userdata = 0; destroy(h); return 0; }
        else return 0;
        if(s != l->sel){ l->sel = s; if(s < l->top) l->top = s; if(s >= l->top + vis) l->top = s - vis + 1; dirty = 1; if(w->id != 0xC0DE) notify_parent(w, LBN_SELCHANGE); }
        return 0; }
    }
    return 0;
  }
  if(w->ctype == CT_COMBO){
    struct lst *l = lst_of(w); int drop = (w->style & 3) == 3;
    switch(m){
      case CB_ADDSTRING: return lst_insert(l, -1, (const char *)lp);
      case CB_INSERTSTRING: return lst_insert(l, (int)wp, (const char *)lp);
      case CB_DELETESTRING: lst_delete(l, (int)wp); dirty = 1; return l->n;
      case CB_RESETCONTENT: l->n = 0; l->sel = -1; dirty = 1; return 0;
      case CB_SETCURSEL: l->sel = (int)(i32)wp < l->n ? (int)(i32)wp : -1; if(l->sel >= 0 && !drop) settext(w, l->s[l->sel]); dirty = 1; return l->sel;
      case CB_GETCURSEL: return l->sel;
      case CB_GETCOUNT: return l->n;
      case CB_GETLBTEXT: if((int)wp < 0 || (int)wp >= l->n) return -1; strcpy_k((char *)lp, l->s[wp]); return (i64)strlen(l->s[wp]);
      case CB_GETLBTEXTLEN: if((int)wp < 0 || (int)wp >= l->n) return -1; return (i64)strlen(l->s[wp]);
      case CB_FINDSTRING: return l->n ? lst_find(l, (const char *)lp, 0, (int)(i32)wp) : -1;
      case CB_FINDSTRINGEXACT: return l->n ? lst_find(l, (const char *)lp, 1, (int)(i32)wp) : -1;
      case CB_SELECTSTRING: { int i = l->n ? lst_find(l, (const char *)lp, 0, (int)(i32)wp) : -1; if(i >= 0){ l->sel = i; if(!drop) settext(w, l->s[i]); dirty = 1; } return i; }
      case CB_GETITEMDATA: return (int)wp >= 0 && (int)wp < l->n ? (i64)l->data[wp] : -1;
      case CB_SETITEMDATA: if((int)wp >= 0 && (int)wp < l->n) l->data[wp] = (u64)lp; return 0;
      case CB_SHOWDROPDOWN: if(wp) combo_open(w); else combo_close_popup(w); return 0;
      case CB_LIMITTEXT: case CB_SETEDITSEL: case CB_GETEDITSEL: case CB_SETDROPPEDWIDTH: return 0;
      case WM_LBUTTONDOWN: set_focus(h); if(w->userdata) combo_close_popup(w); else combo_open(w); return 0;
      case WM_KEYDOWN: if(wp == 0x28 || wp == 0x26){ if(drop || 1){ int s = l->sel; if(wp == 0x28 && s < l->n - 1) s++; else if(wp == 0x26 && s > 0) s--; if(s != l->sel && s >= 0){ l->sel = s; if(!drop) settext(w, l->s[s]); notify_parent(w, CBN_SELCHANGE); dirty = 1; } } } return 0;
      case WM_CHAR: if(!drop){ int c = (int)wp; u32 n = w->tlen; if(c == 8){ if(n){ w->text[n - 1] = 0; w->tlen--; notify_parent(w, CBN_EDITCHANGE); dirty = 1; } } else if(c >= 32 && c < 127){ char s[2] = {(char)c, 0}; if(w->tlen + 2 > w->tcap){ u32 cc = (w->tlen + 2) * 2 + 32; char *nb = kmalloc(cc, 16); if(nb){ if(w->text) memcpy(nb, w->text, w->tlen + 1); else nb[0] = 0; w->text = nb; w->tcap = cc; } } if(w->text){ w->text[w->tlen++] = s[0]; w->text[w->tlen] = 0; notify_parent(w, CBN_EDITCHANGE); dirty = 1; } } }
        else { int c = (int)wp; if(c >= 32 && c < 127){ char s[2] = {(char)c, 0}; int i = l->n ? lst_find(l, s, 0, l->sel) : -1; if(i >= 0){ l->sel = i; notify_parent(w, CBN_SELCHANGE); dirty = 1; } } } return 0;
    }
    return 0;
  }
  if(w->ctype == CT_TREE){
    struct tree *t = tree_of(w); int rh = row_h(); int vis = (w->h - 4) / rh; int order[512];
    switch(m){
      case TVM_INSERTITEMA: {                                          /* TV_INSERTSTRUCT: hParent, hInsertAfter, TVITEM { mask, hItem, state, stateMask, pszText, cchText, iImage, iSelectedImage, cChildren, lParam } */
        u8 *s = (u8 *)lp; u64 hp = *(u64 *)s; u8 *it = s + 16; u32 mask = *(u32 *)it; const char *text = (mask & 1) ? *(const char **)(it + 24) : ""; u64 lparam = (mask & 0x4) ? *(u64 *)(it + 48) : 0;
        int parent = hp && hp != (u64)-0x10000 ? (int)hp - 1 : -1; int id = tree_add(t, parent, text ? text : "", lparam); dirty = 1; return id + 1; }
      case TVM_DELETEITEM: if(!wp || wp == (u64)-0x10000){ t->n = 0; t->sel = -1; } dirty = 1; return 1;
      case TVM_GETCOUNT: return t->n;
      case TVM_SELECTITEM: if((wp & 0xF) == 9 || wp == 9) tree_select(w, t, (int)lp - 1, 1); return 1;
      case TVM_GETNEXTITEM: if(wp == 9) return t->sel + 1; if(wp == 0){ for(int i = 0; i < t->n; i++) if(t->it[i].parent < 0) return i + 1; return 0; } if(wp == 4 && lp > 0) return t->it[lp - 1].parent + 1; if(wp == 2 && lp > 0) return t->it[lp - 1].next + 1; if(wp == 4 || wp == 3) return 0; return 0;
      case TVM_GETITEMA: { u8 *it = (u8 *)lp; int idx = (int)*(u64 *)(it + 8) - 1; if(idx >= 0 && idx < t->n){ u32 mask = *(u32 *)it; if(mask & 0x4) *(u64 *)(it + 48) = t->it[idx].lparam; if((mask & 1) && *(char **)(it + 24)){ strcpy(*(char **)(it + 24), t->it[idx].text); } return 1; } return 0; }
      case TVM_EXPAND: if(lp > 0){ t->it[lp - 1].expanded = (wp & 3) == 2 ? 1 : (wp & 3) == 1 ? 0 : !t->it[lp - 1].expanded; dirty = 1; } return 1;
      case TVM_ENSUREVISIBLE: case TVM_SETIMAGELIST: case TVM_SETBKCOLOR: case TVM_SETTEXTCOLOR: case TVM_SETITEMHEIGHT: case TVM_SETITEMA: return 0;
      case WM_LBUTTONDOWN: {
        set_focus(h); int n = tree_visible(t, order, 512); int y = (int)((lp >> 16) & 0xFFFF), x = (int)(lp & 0xFFFF); int vi = t->top + (y - 2) / rh;
        if(vi >= 0 && vi < n){ int idx = order[vi]; int depth = tree_depth(t, idx);
          if(t->it[idx].first >= 0 && x >= 4 + depth * 16 && x < 18 + depth * 16){ t->it[idx].expanded = !t->it[idx].expanded; dirty = 1; }
          else tree_select(w, t, idx, 1); }
        return 0; }
      case WM_MOUSEWHEEL: { int n = tree_visible(t, order, 512); int d = (int)(short)((wp >> 16) & 0xFFFF); t->top -= d / 120 * 3; if(t->top > n - vis) t->top = n - vis; if(t->top < 0) t->top = 0; dirty = 1; return 0; }
      case WM_KEYDOWN: {
        int n = tree_visible(t, order, 512); int pos = -1; for(int i = 0; i < n; i++) if(order[i] == t->sel) pos = i;
        if(wp == 0x28 && pos < n - 1) tree_select(w, t, order[pos + 1], 1); else if(wp == 0x26 && pos > 0) tree_select(w, t, order[pos - 1], 1);
        else if(wp == 0x27 && t->sel >= 0 && t->it[t->sel].first >= 0){ if(!t->it[t->sel].expanded) t->it[t->sel].expanded = 1; else tree_select(w, t, t->it[t->sel].first, 1); dirty = 1; }
        else if(wp == 0x25 && t->sel >= 0){ if(t->it[t->sel].expanded) t->it[t->sel].expanded = 0; else if(t->it[t->sel].parent >= 0) tree_select(w, t, t->it[t->sel].parent, 1); dirty = 1; }
        n = tree_visible(t, order, 512); pos = -1; for(int i = 0; i < n; i++) if(order[i] == t->sel) pos = i; if(pos >= 0){ if(pos < t->top) t->top = pos; if(pos >= t->top + vis) t->top = pos - vis + 1; }
        return 0; }
    }
    return 0;
  }
  return def_proc(h, m, wp, lp);
}
