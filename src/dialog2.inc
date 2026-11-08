/* Willows dialog manager (included into wm.c after the message functions):
   resource lookup, dialog templates, DialogBox/CreateDialog, the modal loop and keyboard navigation, plus
   the user32/kernel32 entry points that go with them and wide-string window functions. */
struct dlgst { i64 result; int done; int defid; i64 msgres; int modal; };
static i32 WINAPI w_PeekMessageA(u8 *msg, u64 h, u32 lo, u32 hi, u32 rm);
static i32 WINAPI w_GetMessageA(u8 *msg, u64 h, u32 lo, u32 hi);
static i32 WINAPI w_TranslateMessage(const u8 *msg);
static i64 WINAPI w_DispatchMessageA(const u8 *msg);
static u64 WINAPI w_CreateWindowExA(u32 ex, const char *cls, const char *name, u32 style, i32 x, i32 y, i32 cw, i32 ch, u64 parent, u64 menu, u64 inst, void *param);
static u64 WINAPI w_RegisterClassA(const u8 *wc);

/* ---- narrow/wide strings ---- */
static void wd_to_a(const u16 *w, char *a, int max){ int i = 0; for(; w && w[i] && i < max - 1; i++) a[i] = w[i] < 0x80 ? (char)w[i] : '?'; a[i] = 0; }
static int a_to_wd(const char *a, u16 *w, int max){ int i = 0; for(; a[i] && i < max - 1; i++) w[i] = (u8)a[i]; w[i] = 0; return i; }

/* ---- resources (PE .rsrc of the running program) ---- */
struct rsrc_dir { u32 chars, stamp; u16 major, minor, nnamed, nid; };
struct rsrc_ent { u32 name, off; };
static const u8 *res_base(u64 hmod, u32 *rva, u32 *size){
  u64 base = hmod && hmod < 0x7FF000000000ull ? hmod : cur->imagebase; const u8 *img = (const u8 *)base;
  if(img[0] != 'M' || img[1] != 'Z') return 0; u32 nt = *(const u32 *)(img + 0x3C); const u8 *opt = img + nt + 24;
  const u32 *dd = (const u32 *)(opt + 112); *rva = dd[4]; *size = dd[5]; return *rva ? img : 0;
}
static int res_name_eq(const u8 *rs, u32 nameoff, const u16 *wname){ const u16 *p = (const u16 *)(rs + (nameoff & 0x7FFFFFFF)); int n = p[0]; p++; for(int i = 0; i < n; i++){ if(!wname[i]) return 0; u16 a = p[i], b = wname[i]; if(a >= 'a' && a <= 'z') a -= 32; if(b >= 'a' && b <= 'z') b -= 32; if(a != b) return 0; } return wname[n] == 0; }
/* name/type are either small integers (MAKEINTRESOURCE) or wide strings */
static const u8 *res_find(u64 hmod, u64 name, u64 type, u32 *size){
  u32 rva, rsz; const u8 *img = res_base(hmod, &rva, &rsz); if(!img) return 0; const u8 *rs = img + rva;
  u64 keys[2] = {type, name}; const u8 *dir = rs;
  for(int lvl = 0; lvl < 2; lvl++){
    const struct rsrc_dir *d = (const struct rsrc_dir *)dir; const struct rsrc_ent *e = (const struct rsrc_ent *)(dir + 16); int ne = d->nnamed + d->nid; int found = -1;
    for(int i = 0; i < ne; i++){
      if(keys[lvl] < 0x10000){ if(!(e[i].name & 0x80000000) && (e[i].name & 0xFFFF) == (u32)keys[lvl]) { found = i; break; } }
      else if((e[i].name & 0x80000000) && res_name_eq(rs, e[i].name, (const u16 *)keys[lvl])){ found = i; break; }
    }
    if(found < 0) return 0;
    if(!(e[found].off & 0x80000000)) return 0;
    dir = rs + (e[found].off & 0x7FFFFFFF);
  }
  const struct rsrc_dir *d = (const struct rsrc_dir *)dir; const struct rsrc_ent *e = (const struct rsrc_ent *)(dir + 16);
  if(d->nnamed + d->nid == 0) return 0; const u32 *de = (const u32 *)(rs + e[0].off);   /* first language */
  if(size) *size = de[1]; return img + de[0];
}
static u64 name_arg_a(const char *n, u16 *buf, int max){ if((u64)n < 0x10000) return (u64)n; a_to_wd(n, buf, max); return (u64)buf; }
static u64 WINAPI k_FindResourceA(u64 hm, const char *name, const char *type){ u16 b1[96], b2[96]; u32 sz; const u8 *p = res_find(hm, name_arg_a(name, b1, 96), name_arg_a(type, b2, 96), &sz); return (u64)p; }
static u64 WINAPI k_FindResourceW(u64 hm, const u16 *name, const u16 *type){ u32 sz; return (u64)res_find(hm, (u64)name, (u64)type, &sz); }
static u64 WINAPI k_FindResourceExA(u64 hm, const char *type, const char *name, u16 lang){ (void)lang; return k_FindResourceA(hm, name, type); }
static u64 WINAPI k_LoadResource(u64 hm, u64 res){ (void)hm; return res; }
static void *WINAPI k_LockResource(u64 h){ return (void *)h; }
static u32 WINAPI k_SizeofResource(u64 hm, u64 res){
  /* the data entry holds the size; recover it by searching the resource tree for this pointer is costly, so scan the data entries */
  u32 rva, rsz; const u8 *img = res_base(hm, &rva, &rsz); if(!img) return 0; const u8 *rs = img + rva;
  for(const u8 *p = rs; p + 16 <= rs + rsz; p += 4){ const u32 *de = (const u32 *)p; if(de[0] && img + de[0] == (const u8 *)res && de[1] > 0 && de[1] < 0x1000000 && de[3] == 0) return de[1]; }
  return 0;
}
/* string tables: RT_STRING (6), block id/16+1, 16 counted UTF-16 strings per block */
static int load_string_w(u64 hm, u32 id, u16 *out, int max){
  u32 sz; const u8 *p = res_find(hm, (id >> 4) + 1, 6, &sz); if(!p) return 0; const u16 *s = (const u16 *)p;
  for(u32 i = 0; i < (id & 15); i++) s += 1 + s[0];
  int n = s[0]; if(!out) return n; if(n >= max) n = max - 1; for(int i = 0; i < n; i++) out[i] = s[1 + i]; out[n] = 0; return n;
}
static i32 WINAPI w_LoadStringA(u64 hm, u32 id, char *buf, i32 max){ u16 t[512]; int n = load_string_w(hm, id, t, 512); if(max <= 0) return 0; wd_to_a(t, buf, max); return n < max ? n : max - 1; }
static i32 WINAPI w_LoadStringW(u64 hm, u32 id, u16 *buf, i32 max){ return load_string_w(hm, id, buf, max); }

/* ---- control creation ---- */
static int ctrl_type_for(const char *cls, u32 style){
  if(!stricmp(cls, "BUTTON")){ u32 t = style & 0xF; return t == 7 ? CT_GROUP : (t == 2 || t == 3 || t == 5 || t == 6) ? CT_CHECK : (t == 4 || t == 9) ? CT_RADIO : CT_PUSH; }
  if(!stricmp(cls, "STATIC")){ u32 t = style & 0x1F; return (t == 0x10 || t == 0x11 || t == 0x12) ? CT_LINE : CT_STATIC; }
  if(!stricmp(cls, "EDIT")) return CT_EDIT;
  if(!stricmp(cls, "LISTBOX")) return CT_LIST;
  if(!stricmp(cls, "COMBOBOX")) return CT_COMBO;
  if(!stricmp(cls, "SysTreeView32")) return CT_TREE;
  return 0;
}
static int is_dialog_wnd(u64 h){ struct wnd *w = get(h); return w && w->kind == K_DIALOG; }
static u64 make_ctrl(u64 parent, int ct, const char *text, u32 style, int x, int y, int w, int h, u64 id){
  int idx = -1; for(int i = 0; i < MAXW; i++) if(!W[i].used){ idx = i; break; } if(idx < 0) return 0;
  struct wnd *c = &W[idx]; memset(c, 0, sizeof *c); c->used = 1; c->owner = cur->pid; c->kind = K_CTRL; c->ctype = (u8)ct; c->proc = B_CTRL;
  c->style = style | WS_CHILD; c->parent = parent; c->id = id; c->x = x; c->y = y; c->w = w; c->h = h; c->seq = ++g_seq; c->disabled = (style & WS_DISABLED) ? 1 : 0;
  if(ct == CT_LIST || ct == CT_COMBO) lst_of(c)->sel = -1;
  if(ct == CT_CHECK || ct == CT_RADIO || ct == CT_PUSH || ct == CT_EDIT || ct == CT_STATIC || ct == CT_GROUP) settext(c, text ? text : "");
  if(ct == CT_EDIT){ struct edt *e = edt_of(c); e->pw = (style & ES_PASSWORD) ? '*' : 0; e->sel0 = e->sel1 = (int)c->tlen; }
  if(ct == CT_COMBO){ c->extra[0] = (u64)h; if(c->h > 22) c->h = 22; }
  c->visible = (style & WS_VISIBLE) ? 1 : 0; created = 1; dirty = 1; return hof(c);
}

/* ---- dialog templates ---- */
#define DLU_X(v) ((v) * 7 / 4)
#define DLU_Y(v) ((v) * 15 / 8)
static const u8 *align4(const u8 *p, const u8 *base){ return base + ((((u64)(p - base)) + 3) & ~3ull); }
static const u8 *read_szord(const u8 *p, char *out, int max, u32 *ord){        /* sz_Or_Ord: 0x0000, 0xFFFF+ordinal, or a wide string */
  const u16 *w = (const u16 *)p; *ord = 0; out[0] = 0;
  if(w[0] == 0) return p + 2;
  if(w[0] == 0xFFFF){ *ord = w[1]; return p + 4; }
  int n = 0; while(w[n]) n++; wd_to_a(w, out, max); return p + (n + 1) * 2;
}
static void dialog_children_layout(u64 dlg){ (void)dlg; }
static u64 build_dialog(const u8 *t, u64 owner_hwnd, u64 dlgproc, i64 lparam, int modal){
  (void)owner_hwnd;
  const u8 *base = t; const u8 *p = t; u32 style, ex; int cdit, dx, dy, dcx, dcy; int ext = 0;
  if(*(const u16 *)(p + 2) == 0xFFFF && *(const u16 *)p == 1){ ext = 1; style = *(const u32 *)(p + 12); ex = *(const u32 *)(p + 8); cdit = *(const u16 *)(p + 16); p += 18; }
  else { style = *(const u32 *)p; ex = *(const u32 *)(p + 4); cdit = *(const u16 *)(p + 8); p += 10; }
  (void)ex; dx = *(const i16 *)p; dy = *(const i16 *)(p + 2); dcx = *(const i16 *)(p + 4); dcy = *(const i16 *)(p + 6); p += 8; (void)dx; (void)dy;
  char tmp[256]; u32 ord; p = read_szord(p, tmp, sizeof tmp, &ord);              /* menu */
  p = read_szord(p, tmp, sizeof tmp, &ord);                                      /* class */
  char title[160]; { const u16 *wt = (const u16 *)p; int n = 0; while(wt[n]) n++; wd_to_a(wt, title, sizeof title); p += (n + 1) * 2; }
  if(style & 0x48){ p += 2; if(ext) p += 4; { const u16 *wf = (const u16 *)p; int n = 0; while(wf[n]) n++; p += (n + 1) * 2; } }    /* DS_SETFONT / DS_SHELLFONT */
  int cw = DLU_X(dcx), ch = DLU_Y(dcy);
  int idx = -1; for(int i = 0; i < MAXW; i++) if(!W[i].used){ idx = i; break; } if(idx < 0) return 0;
  struct wnd *w = &W[idx]; memset(w, 0, sizeof *w); w->used = 1; w->owner = cur->pid; w->kind = K_DIALOG; w->proc = B_DIALOG; w->style = WS_CAPTION | WS_VISIBLE; w->seq = ++g_seq;
  w->w = cw + 2 * FRAME; w->h = ch + FRAME + TITLE_H + FRAME; w->x = ((int)gfx_w - w->w) / 2 + 12 * nz; w->y = ((int)gfx_h - TB_H - w->h) / 2 - 10 + 12 * nz; if(w->x < 0) w->x = 0; if(w->y < 0) w->y = 0;
  settext(w, title); w->userdata = dlgproc;
  struct dlgst *d = kmalloc(sizeof *d, 16); d->modal = modal; d->defid = 1; w->ext = d; u64 h = hof(w);
  u64 firstfocus = 0;
  for(int i = 0; i < cdit; i++){
    p = align4(p, base);
    u32 istyle, iex; int ix, iy, icx, icy; u64 id;
    if(ext){ istyle = *(const u32 *)(p + 8); iex = *(const u32 *)(p + 4); ix = *(const i16 *)(p + 12); iy = *(const i16 *)(p + 14); icx = *(const i16 *)(p + 16); icy = *(const i16 *)(p + 18); id = *(const u32 *)(p + 20); p += 24; }
    else { istyle = *(const u32 *)p; iex = *(const u32 *)(p + 4); ix = *(const i16 *)(p + 8); iy = *(const i16 *)(p + 10); icx = *(const i16 *)(p + 12); icy = *(const i16 *)(p + 14); id = *(const u16 *)(p + 16); p += 18; }
    (void)iex; char cls[64]; u32 cord; p = read_szord(p, cls, sizeof cls, &cord);
    if(cord){ const char *nm = cord == 0x80 ? "BUTTON" : cord == 0x81 ? "EDIT" : cord == 0x82 ? "STATIC" : cord == 0x83 ? "LISTBOX" : cord == 0x84 ? "SCROLLBAR" : cord == 0x85 ? "COMBOBOX" : "STATIC"; strcpy_k(cls, nm); }
    char text[300]; u32 tord; p = read_szord(p, text, sizeof text, &tord);
    int extra = *(const u16 *)p; p += 2 + extra; if(ext && 0) p += 0;
    int ct = ctrl_type_for(cls, istyle); if(!ct){ if(!stricmp(cls, "SCROLLBAR")) continue; ct = CT_STATIC; }
    if((istyle & 0x1F) == 3 && ct == CT_STATIC){ continue; }                    /* SS_ICON */
    u32 vis = istyle | WS_VISIBLE; if(!(istyle & WS_VISIBLE) && 0) vis = istyle;
    if(ct == CT_PUSH && (istyle & 0xF) == 1) ((struct dlgst *)w->ext)->defid = (int)id;
    u64 c = make_ctrl(h, ct, text, istyle & ~(u32)WS_VISIBLE, DLU_X(ix), DLU_Y(iy), DLU_X(icx), DLU_Y(icy), id);
    struct wnd *cw2 = get(c); if(cw2){ cw2->visible = (istyle & WS_VISIBLE) ? 1 : 1; if(istyle & 0x08000000u) cw2->visible = 1; if(!firstfocus && (istyle & WS_TABSTOP) && !(istyle & WS_DISABLED) && ct != CT_STATIC && ct != CT_GROUP) firstfocus = c; (void)vis; }
  }
  w->visible = 0; Z[nz++] = h; created = 1;
  i64 r = send(h, WM_INITDIALOG, firstfocus, lparam);
  if(!get(h)) return 0;
  w = get(h); w->visible = 1; raise_top(h);
  if(r && firstfocus) set_focus(firstfocus); else if(!r) { /* the program chose the focus itself (or none) */ if(!get(g_focus) || W[(g_focus - 0x10000) >> 2].parent != h) set_focus(firstfocus ? firstfocus : h); } else set_focus(h);
  dirty = 1; return h;
}
static i64 dlg_proc(u64 h, u32 m, u64 wp, i64 lp){
  struct wnd *w = get(h); if(!w) return 0; struct dlgst *d = (struct dlgst *)w->ext;
  if(w->userdata){
    i64 r = ((wndproc_t)w->userdata)(h, m, wp, lp);
    w = get(h); if(!w) return r; d = (struct dlgst *)w->ext;
    if(r){ if(m == WM_INITDIALOG) return r; return d ? d->msgres : 1; }
  }
  switch(m){
    case WM_CLOSE: post(h, WM_COMMAND, 2, 0); return 0;
    case 0x400: return ((i64)0x534B << 16) | (d ? d->defid : 1);        /* DM_GETDEFID */
    case 0x401: if(d) d->defid = (int)wp; return 1;                     /* DM_SETDEFID */
    case WM_ERASEBKGND: return 1;
  }
  return def_proc(h, m, wp, lp);
}

/* ---- keyboard navigation (IsDialogMessage) ---- */
static struct wnd *dlg_next_tab(u64 dlg, struct wnd *from, int dir){
  struct wnd *best = 0; u32 bseq = 0;
  for(int i = 0; i < MAXW; i++){ struct wnd *c = &W[i]; if(!c->used || c->parent != dlg || c->kind != K_CTRL || c->disabled || !c->visible) continue; if(!(c->style & WS_TABSTOP)) continue;
    if(c->ctype == CT_STATIC || c->ctype == CT_GROUP || c->ctype == CT_LINE) continue;
    u32 s = c->seq; if(from){ if(dir > 0 ? s > from->seq : s < from->seq){ if(!best || (dir > 0 ? s < bseq : s > bseq)){ best = c; bseq = s; } } }
    else if(!best || (dir > 0 ? s < bseq : s > bseq)){ best = c; bseq = s; } }
  if(!best && from){ for(int i = 0; i < MAXW; i++){ struct wnd *c = &W[i]; if(!c->used || c->parent != dlg || c->kind != K_CTRL || c->disabled || !c->visible || !(c->style & WS_TABSTOP)) continue; if(c->ctype == CT_STATIC || c->ctype == CT_GROUP || c->ctype == CT_LINE) continue; if(!best || (dir > 0 ? c->seq < bseq : c->seq > bseq)){ best = c; bseq = c->seq; } } }
  return best;
}
static int dlg_is_msg(u64 dlg, const u8 *msg){
  struct wnd *d = get(dlg); if(!d || d->kind != K_DIALOG) return 0;
  u64 target = *(const u64 *)msg; u32 m = *(const u32 *)(msg + 8); u64 wp = *(const u64 *)(msg + 16);
  struct wnd *t = get(target); if(!t) return 0; u64 dh = t->parent ? t->parent : target; if(t->kind == K_DIALOG) dh = target;
  if(dh != dlg) return 0;
  if(m == WM_KEYDOWN){
    int vk = (int)wp; struct dlgst *st = (struct dlgst *)d->ext;
    if(vk == 0x09){ struct wnd *n = dlg_next_tab(dlg, t->kind == K_CTRL ? t : 0, kb_vk(0x10) ? -1 : 1); if(n) set_focus(hof(n)); return 1; }
    if(vk == 0x0D){
      if(t->kind == K_CTRL && t->ctype == CT_PUSH){ ctrl_proc(hof(t), BM_CLICK, 0, 0); return 1; }
      if(t->kind == K_CTRL && t->ctype == CT_LIST && t->id == 0xC0DE) return 0;
      post(dlg, WM_COMMAND, st ? st->defid : 1, 0); return 1;
    }
    if(vk == 0x1B){ if(t->kind == K_CTRL && t->ctype == CT_LIST && t->id == 0xC0DE) return 0; post(dlg, WM_COMMAND, 2, 0); return 1; }
    if(t->kind == K_CTRL && t->ctype == CT_RADIO && (vk >= 0x25 && vk <= 0x28)){        /* arrows move within a radio group */
      int dir = (vk == 0x25 || vk == 0x26) ? -1 : 1; struct wnd *best = 0;
      for(int i = 0; i < MAXW; i++){ struct wnd *c = &W[i]; if(!c->used || c->parent != dlg || c->ctype != CT_RADIO || c->disabled) continue; if(dir > 0 ? c->seq > t->seq : c->seq < t->seq){ if(c != t && (c->seq < t->seq ? !(t->style & WS_GROUP) : !(c->style & WS_GROUP))) if(!best || (dir > 0 ? c->seq < best->seq : c->seq > best->seq)) best = c; } }
      if(best){ set_focus(hof(best)); ctrl_proc(hof(best), BM_CLICK, 0, 0); } return 1;
    }
  }
  return 0;
}
/* the modal dialog (if any) owned by a process: other windows of that process ignore the mouse while it is open */
static u64 modal_of(int owner){ for(int i = nz - 1; i >= 0; i--){ struct wnd *w = get(Z[i]); if(w && w->kind == K_DIALOG && w->owner == owner && w->visible){ struct dlgst *d = (struct dlgst *)w->ext; if(d && d->modal && !d->done) return Z[i]; } } return 0; }

static i64 run_modal(u64 h){
  struct wnd *w = get(h); if(!w) return -1; struct dlgst *d = (struct dlgst *)w->ext;
  while(!d->done){
    u8 msg[48]; if(!w_GetMessageA(msg, 0, 0, 0)){ cur->quit = 1; break; }
    if(dlg_is_msg(h, msg)) continue;
    w_TranslateMessage(msg); w_DispatchMessageA(msg);
  }
  if(get(h)) destroy(h);
  return d->result;
}
static i32 WINAPI w_EndDialog(u64 h, i64 result){
  struct wnd *w = get(h); if(!w || w->kind != K_DIALOG) return 0; struct dlgst *d = (struct dlgst *)w->ext; if(d){ d->result = result; d->done = 1; }
  w->visible = 0; dirty = 1; return 1;
}
static i64 WINAPI w_DialogBoxIndirectParamA(u64 inst, const u8 *tpl, u64 parent, u64 proc, i64 lp){ (void)inst; (void)parent; u64 h = build_dialog(tpl, parent, proc, lp, 1); if(!h) return -1; return run_modal(h); }
static i64 WINAPI w_DialogBoxIndirectParamW(u64 inst, const u8 *tpl, u64 parent, u64 proc, i64 lp){ return w_DialogBoxIndirectParamA(inst, tpl, parent, proc, lp); }
static i64 WINAPI w_DialogBoxParamA(u64 inst, const char *name, u64 parent, u64 proc, i64 lp){
  u16 b[96]; u32 sz; const u8 *t = res_find(inst, name_arg_a(name, b, 96), 5, &sz); if(!t){ kprintf("DialogBox: dialog resource not found\n"); return -1; }
  return w_DialogBoxIndirectParamA(inst, t, parent, proc, lp);
}
static i64 WINAPI w_DialogBoxParamW(u64 inst, const u16 *name, u64 parent, u64 proc, i64 lp){ u32 sz; const u8 *t = res_find(inst, (u64)name, 5, &sz); if(!t) return -1; return w_DialogBoxIndirectParamA(inst, t, parent, proc, lp); }
static u64 WINAPI w_CreateDialogIndirectParamA(u64 inst, const u8 *tpl, u64 parent, u64 proc, i64 lp){ (void)inst; return build_dialog(tpl, parent, proc, lp, 0); }
static u64 WINAPI w_CreateDialogIndirectParamW(u64 inst, const u8 *tpl, u64 parent, u64 proc, i64 lp){ return w_CreateDialogIndirectParamA(inst, tpl, parent, proc, lp); }
static u64 WINAPI w_CreateDialogParamA(u64 inst, const char *name, u64 parent, u64 proc, i64 lp){ u16 b[96]; u32 sz; const u8 *t = res_find(inst, name_arg_a(name, b, 96), 5, &sz); return t ? build_dialog(t, parent, proc, lp, 0) : 0; }
static u64 WINAPI w_CreateDialogParamW(u64 inst, const u16 *name, u64 parent, u64 proc, i64 lp){ u32 sz; const u8 *t = res_find(inst, (u64)name, 5, &sz); return t ? build_dialog(t, parent, proc, lp, 0) : 0; }

/* ---- item helpers ---- */
static struct wnd *dlg_item(u64 dlg, i32 id){ for(int i = 0; i < MAXW; i++) if(W[i].used && W[i].parent == dlg && (i32)(W[i].id & 0xFFFFFFFF) == id) return &W[i]; return 0; }
static u64 WINAPI w_GetDlgItem(u64 dlg, i32 id){ struct wnd *c = dlg_item(dlg, id); return c ? hof(c) : 0; }
static i32 WINAPI w_GetDlgCtrlID(u64 h){ struct wnd *w = get(h); return w ? (i32)w->id : 0; }
static i32 WINAPI w_SetDlgItemTextA(u64 dlg, i32 id, const char *s){ struct wnd *c = dlg_item(dlg, id); if(!c) return 0; send(hof(c), WM_SETTEXT, 0, (i64)s); return 1; }
static i32 WINAPI w_SetDlgItemTextW(u64 dlg, i32 id, const u16 *s){ char a[512]; wd_to_a(s, a, sizeof a); return w_SetDlgItemTextA(dlg, id, a); }
static u32 WINAPI w_GetDlgItemTextA(u64 dlg, i32 id, char *buf, i32 max){ struct wnd *c = dlg_item(dlg, id); if(!c || max <= 0){ if(buf && max > 0) buf[0] = 0; return 0; } u32 n = c->tlen < (u32)max - 1 ? c->tlen : (u32)max - 1; if(c->text) memcpy(buf, c->text, n); buf[n] = 0; return n; }
static u32 WINAPI w_GetDlgItemTextW(u64 dlg, i32 id, u16 *buf, i32 max){ char a[512]; u32 n = w_GetDlgItemTextA(dlg, id, a, sizeof a); if(max > 0) a_to_wd(a, buf, max); return n; }
static i32 WINAPI w_SetDlgItemInt(u64 dlg, i32 id, u32 v, i32 sgn){ char b[16]; int k = 0; i64 x = sgn ? (i32)v : (i64)v; int neg = x < 0; if(neg) x = -x; char t[16]; int n = 0; do { t[n++] = (char)('0' + x % 10); x /= 10; } while(x); if(neg) b[k++] = '-'; while(n) b[k++] = t[--n]; b[k] = 0; return w_SetDlgItemTextA(dlg, id, b); }
static u32 WINAPI w_GetDlgItemInt(u64 dlg, i32 id, i32 *ok, i32 sgn){ char b[24]; w_GetDlgItemTextA(dlg, id, b, sizeof b); const char *p = b; int neg = 0; if(*p == '-'){ neg = 1; p++; } i64 v = 0; int any = 0; for(; *p >= '0' && *p <= '9'; p++){ v = v * 10 + (*p - '0'); any = 1; } if(ok) *ok = any; (void)sgn; return (u32)(neg ? -v : v); }
static i64 WINAPI w_SendDlgItemMessageA(u64 dlg, i32 id, u32 m, u64 wp, i64 lp){ struct wnd *c = dlg_item(dlg, id); return c ? send(hof(c), m, wp, lp) : 0; }
static i32 WINAPI w_CheckDlgButton(u64 dlg, i32 id, u32 chk){ struct wnd *c = dlg_item(dlg, id); if(!c) return 0; send(hof(c), BM_SETCHECK, chk, 0); return 1; }
static u32 WINAPI w_IsDlgButtonChecked(u64 dlg, i32 id){ struct wnd *c = dlg_item(dlg, id); return c ? (u32)send(hof(c), BM_GETCHECK, 0, 0) : 0; }
static i32 WINAPI w_CheckRadioButton(u64 dlg, i32 first, i32 last, i32 chk){ for(int i = 0; i < MAXW; i++){ struct wnd *c = &W[i]; if(c->used && c->parent == dlg && c->ctype == CT_RADIO){ i32 id = (i32)c->id; if(id >= first && id <= last) c->check = id == chk; } } dirty = 1; return 1; }
static i32 WINAPI w_EnableWindow(u64 h, i32 en){ struct wnd *w = get(h); if(!w) return 0; int was = w->disabled; w->disabled = en ? 0 : 1; dirty = 1; if(h == g_focus && !en){ } return was; }
static i32 WINAPI w_IsWindowEnabled(u64 h){ struct wnd *w = get(h); return w && !w->disabled; }
static i32 WINAPI w_IsDialogMessageA(u64 dlg, const u8 *msg){
  if(dlg_is_msg(dlg, msg)) return 1;
  u32 m = *(const u32 *)(msg + 8);
  if(m == WM_KEYDOWN || m == WM_KEYUP || m == WM_CHAR){                  /* like Windows: the dialog manager translates and delivers keys itself */
    struct wnd *t = get(*(const u64 *)msg); if(!t) return 0; u64 dh = t->kind == K_DIALOG ? *(const u64 *)msg : t->parent; if(dh != dlg) return 0;
    w_TranslateMessage(msg); w_DispatchMessageA(msg); return 1;
  }
  return 0;
}
static i64 WINAPI w_DefDlgProcA(u64 h, u32 m, u64 wp, i64 lp){ return def_proc(h, m, wp, lp); }
static i32 WINAPI w_MapDialogRect(u64 h, i32 *r){ (void)h; r[0] = DLU_X(r[0]); r[2] = DLU_X(r[2]); r[1] = DLU_Y(r[1]); r[3] = DLU_Y(r[3]); return 1; }
static u32 WINAPI w_GetDialogBaseUnits(void){ return (15u << 16) | 7u; }
static u64 WINAPI w_GetNextDlgTabItem(u64 dlg, u64 from, i32 prev){ struct wnd *f = get(from); struct wnd *n = dlg_next_tab(dlg, f && f->kind == K_CTRL ? f : 0, prev ? -1 : 1); return n ? hof(n) : 0; }
static u64 WINAPI w_GetNextDlgGroupItem(u64 dlg, u64 from, i32 prev){ return w_GetNextDlgTabItem(dlg, from, prev); }
static i32 WINAPI w_MessageBeep(u32 t){ (void)t; return 1; }
static u64 WINAPI w_LoadIconA(u64 inst, const char *name){ (void)inst; (void)name; return 0x7777; }
static u64 WINAPI w_LoadImageA(u64 inst, const char *name, u32 type, i32 cx, i32 cy, u32 fl){ (void)inst; (void)name; (void)type; (void)cx; (void)cy; (void)fl; return 0x7778; }
static i32 WINAPI w_DestroyIcon(u64 h){ (void)h; return 1; }
static i32 WINAPI w_DrawIconEx(u64 dc, i32 x, i32 y, u64 icon, i32 cx, i32 cy, u32 step, u64 br, u32 fl){ (void)dc; (void)x; (void)y; (void)icon; (void)cx; (void)cy; (void)step; (void)br; (void)fl; return 1; }

/* ---- wide-string window functions (strings are narrowed; windows are ASCII internally) ---- */
static u64 WINAPI w_RegisterClassW(const u8 *wcw){
  u8 wc[80]; memcpy(wc, wcw, 80); const u16 *nm = *(const u16 **)(wc + 64); static char names[16][24]; static int nn; char *a = names[nn++ & 15];
  if((u64)nm < 0x10000) { a[0] = 0; } else wd_to_a(nm, a, 24); *(const char **)(wc + 64) = a; return w_RegisterClassA(wc);
}
static u64 WINAPI w_CreateWindowExW(u32 ex, const u16 *cls, const u16 *name, u32 style, i32 x, i32 y, i32 cw, i32 ch, u64 parent, u64 menu, u64 inst, void *param){
  char c[48], n[300]; const char *cp; if((u64)cls < 0x10000) cp = (const char *)cls; else { wd_to_a(cls, c, sizeof c); cp = c; }
  wd_to_a(name, n, sizeof n); return w_CreateWindowExA(ex, cp, name ? n : "", style, x, y, cw, ch, parent, menu, inst, param);
}
static i32 WINAPI w_SetWindowTextW(u64 h, const u16 *s){ char a[512]; wd_to_a(s, a, sizeof a); return (i32)send(h, WM_SETTEXT, 0, (i64)a); }
static i32 WINAPI w_GetWindowTextW(u64 h, u16 *buf, i32 max){ char a[512]; i64 n = send(h, WM_GETTEXT, sizeof a, (i64)a); if(max > 0) a_to_wd(a, buf, max); return (i32)n; }
static i32 WINAPI w_GetWindowTextLengthW(u64 h){ return (i32)send(h, WM_GETTEXTLENGTH, 0, 0); }
static i64 wide_msg(u64 h, u32 m, u64 wp, i64 lp, int post_it){
  char a[600];
  if(m == WM_SETTEXT || m == LB_ADDSTRING || m == CB_ADDSTRING || m == LB_INSERTSTRING || m == CB_INSERTSTRING || m == EM_REPLACESEL || m == CB_FINDSTRING || m == LB_FINDSTRING || m == CB_SELECTSTRING){ wd_to_a((const u16 *)lp, a, sizeof a); lp = (i64)a; }
  else if(m == WM_GETTEXT || m == LB_GETTEXT || m == CB_GETLBTEXT){ char t[600]; i64 r = send(h, m, m == WM_GETTEXT ? (wp < 600 ? wp : 599) : wp, (i64)t); if(r >= 0){ a_to_wd(t, (u16 *)lp, m == WM_GETTEXT ? (int)wp : 600); } return r; }
  if(post_it){ post(h, m, wp, lp); return 1; }
  return send(h, m, wp, lp);
}
static i64 WINAPI w_SendMessageW(u64 h, u32 m, u64 wp, i64 lp){ return wide_msg(h, m, wp, lp, 0); }
static i32 WINAPI w_PostMessageW(u64 h, u32 m, u64 wp, i64 lp){ return (i32)wide_msg(h, m, wp, lp, 1); }
static i64 WINAPI w_DefWindowProcW(u64 h, u32 m, u64 wp, i64 lp){ return def_proc(h, m, wp, lp); }       /* text reaching a window procedure is already narrow */
static i32 WINAPI w_PeekMessageW(u8 *msg, u64 h, u32 lo, u32 hi, u32 rm){ return w_PeekMessageA(msg, h, lo, hi, rm); }
static i32 WINAPI w_GetMessageW(u8 *msg, u64 h, u32 lo, u32 hi){ return w_GetMessageA(msg, h, lo, hi); }
static i64 WINAPI w_DispatchMessageW(const u8 *msg){ return w_DispatchMessageA(msg); }

#define DLG_EXPORTS \
  U(DialogBoxParamA), U(DialogBoxParamW), U(DialogBoxIndirectParamA), U(DialogBoxIndirectParamW), U(CreateDialogParamA), U(CreateDialogParamW), U(CreateDialogIndirectParamA), U(CreateDialogIndirectParamW), \
  U(EndDialog), U(GetDlgItem), U(GetDlgCtrlID), U(SetDlgItemTextA), U(SetDlgItemTextW), U(GetDlgItemTextA), U(GetDlgItemTextW), U(SetDlgItemInt), U(GetDlgItemInt), U(SendDlgItemMessageA), \
  {"user32","SendDlgItemMessageW",w_SendDlgItemMessageA}, U(CheckDlgButton), U(IsDlgButtonChecked), U(CheckRadioButton), U(EnableWindow), U(IsWindowEnabled), U(IsDialogMessageA), {"user32","IsDialogMessageW",w_IsDialogMessageA}, \
  U(DefDlgProcA), {"user32","DefDlgProcW",w_DefDlgProcA}, U(MapDialogRect), U(GetDialogBaseUnits), U(GetNextDlgTabItem), U(GetNextDlgGroupItem), U(MessageBeep), U(LoadIconA), {"user32","LoadIconW",w_LoadIconA}, \
  U(LoadImageA), {"user32","LoadImageW",w_LoadImageA}, U(DestroyIcon), U(DrawIconEx), U(LoadStringA), U(LoadStringW), \
  {"kernel32","FindResourceA",k_FindResourceA}, {"kernel32","FindResourceW",k_FindResourceW}, {"kernel32","FindResourceExA",k_FindResourceExA}, {"kernel32","LoadResource",k_LoadResource}, {"kernel32","LockResource",k_LockResource}, {"kernel32","SizeofResource",k_SizeofResource}, \
  U(RegisterClassW), {"user32","RegisterClassExW",w_RegisterClassW}, U(CreateWindowExW), U(SetWindowTextW), U(GetWindowTextW), U(GetWindowTextLengthW), U(SendMessageW), U(PostMessageW), U(DefWindowProcW), U(PeekMessageW), U(GetMessageW), U(DispatchMessageW),
