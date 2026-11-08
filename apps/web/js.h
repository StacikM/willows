/* JavaScript for the Willows browser: QuickJS plus a DOM layer. The C side exposes a small set of node primitives (object `__n`);
   apps/web/prelude.js builds document, window, Element, events, timers, fetch ... on top of them. */
#ifndef WEB_JS_H
#define WEB_JS_H
#include "quickjs.h"
#include "prelude_js.h"

static JSRuntime *js_rt; static JSContext *js_ctx;
static int js_dirty, js_img_dirty, js_title_dirty, js_nav_pending; static char js_nav_url[900];
static unsigned long long js_deadline;
static int js_timers_active;
#define JSFN(name) static JSValue name(JSContext *c, JSValueConst t, int argc, JSValueConst *argv)
#define ARGI(i) ({ int32_t _v = 0; if((i) < argc) JS_ToInt32(c, &_v, argv[i]); _v; })
static JSValue js_str(JSContext *c, const char *s){ return JS_NewString(c, s ? s : ""); }
static int valid_node(int n){ return n >= 0 && n < nnodes; }

/* ---- string helpers: an owned UTF-8 copy of a JS string argument ---- */
static char *argstr(JSContext *c, JSValueConst v){ const char *s = JS_ToCString(c, v); if(!s) return strdup(""); char *d = strdup(s); JS_FreeCString(c, s); return d; }

static void set_attr(int n, const char *name, const char *val){
  for(int i = 0; i < N(n).nattr; i++) if(!strcmp(N(n).attrs[i].name, name)){ N(n).attrs[i].val = a_str(val, (int)strlen(val)); return; }
  struct attr *na = a_alloc((N(n).nattr + 1) * (int)sizeof(struct attr)); if(!na) return;
  if(N(n).nattr) memcpy(na, N(n).attrs, N(n).nattr * sizeof(struct attr));
  na[N(n).nattr].name = a_str(name, (int)strlen(name)); na[N(n).nattr].val = a_str(val, (int)strlen(val)); N(n).attrs = na; N(n).nattr++;
}
static void del_attr(int n, const char *name){
  for(int i = 0; i < N(n).nattr; i++) if(!strcmp(N(n).attrs[i].name, name)){ for(int k = i; k + 1 < N(n).nattr; k++) N(n).attrs[k] = N(n).attrs[k + 1]; N(n).nattr--; return; }
}
static void unlink_node(int c){
  int p = N(c).parent; if(p < 0) return;
  if(N(p).first == c) N(p).first = N(c).next;
  else for(int s = N(p).first; s >= 0; s = N(s).next) if(N(s).next == c){ N(s).next = N(c).next; if(N(p).last == c) N(p).last = s; break; }
  if(N(p).last == c){ int l = -1; for(int s = N(p).first; s >= 0; s = N(s).next) l = s; N(p).last = l; }
  N(c).parent = -1; N(c).next = -1;
}
static int is_ancestor(int a, int n){ for(int p = n; p >= 0; p = N(p).parent) if(p == a) return 1; return 0; }
static void insert_before(int parent, int c, int ref){
  if(is_ancestor(c, parent)) return;
  unlink_node(c);
  if(ref < 0 || N(ref).parent != parent){ node_append(parent, c); return; }
  int prev = -1; for(int s = N(parent).first; s >= 0 && s != ref; s = N(s).next) prev = s;
  N(c).parent = parent; N(c).next = ref; if(prev < 0) N(parent).first = c; else N(prev).next = c;
}

/* ---- selectors: parse once into compound chains and match with the CSS engine ---- */
#define MAXQ 16
static struct simple qparts[MAXQ][12]; static int qnp[MAXQ], nq; static char qlast[400];
static int parse_query(const char *sel){
  if(!strcmp(sel, qlast)) return nq;
  nq = 0; snprintf(qlast, sizeof qlast, "%s", sel); const char *p = sel, *end = sel + strlen(sel);
  while(p < end && nq < MAXQ){
    const char *q = p; int depth = 0;
    while(q < end && !(depth == 0 && *q == ',')){ if(*q == '(' || *q == '[') depth++; else if(*q == ')' || *q == ']') depth--; else if(*q == '"' || *q == '\''){ q = skip_block(q, end); continue; } q++; }
    const char *se = q, *s = p; p = q < end ? q + 1 : end;
    int np = 0, bad = 0; char comb = 0;
    while(s < se){
      while(s < se && css_ws(*s)) s++; if(s >= se) break;
      if(*s == '>' || *s == '+' || *s == '~'){ comb = *s++; continue; }
      if(np >= 12){ bad = 1; break; }
      const char *e = parse_compound(s, se, &qparts[nq][np]); if(!e){ bad = 1; break; }
      qparts[nq][np].comb = np ? (comb ? comb : ' ') : 0; comb = 0; np++; s = e;
    }
    if(!bad && np){ qnp[nq] = np; nq++; }
  }
  return nq;
}
static int query_match(int n){ struct rule r; memset(&r, 0, sizeof r); for(int i = 0; i < nq; i++){ r.parts = qparts[i]; r.nparts = (short)qnp[i]; if(match_from(n, &r, qnp[i] - 1)) return 1; } return 0; }
static void query_walk(int n, int root, JSContext *c, JSValue arr, int *cnt, int all, int *first){
  for(int ch = N(n).first; ch >= 0 && (all || *first < 0); ch = N(ch).next){
    if(N(ch).type == 0 && query_match(ch)){ if(*first < 0) *first = ch; if(all) JS_SetPropertyUint32(c, arr, (*cnt)++, JS_NewInt32(c, ch)); }
    if(N(ch).type == 0) query_walk(ch, root, c, arr, cnt, all, first);
  }
}

/* ---- serialising ---- */
struct sbuf { char *p; int n, cap; };
static void sb_add(struct sbuf *b, const char *s, int n){ if(b->n + n + 1 > b->cap){ b->cap = (b->n + n + 1) * 2 + 256; b->p = realloc(b->p, b->cap); } memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0; }
static void sb_esc(struct sbuf *b, const char *s, int attr){ for(; *s; s++){ if(*s == '&') sb_add(b, "&amp;", 5); else if(*s == '<') sb_add(b, "&lt;", 4); else if(*s == '>') sb_add(b, "&gt;", 4); else if(attr && *s == '"') sb_add(b, "&quot;", 6); else sb_add(b, s, 1); } }
static void ser_node(struct sbuf *b, int n, int self){
  if(N(n).type == 1){ int raw = N(n).parent >= 0 && (N(N(n).parent).tag == TAG_script || N(N(n).parent).tag == TAG_style); if(raw) sb_add(b, N(n).text, (int)strlen(N(n).text)); else sb_esc(b, N(n).text, 0); return; }
  const char *tn = tag_name(N(n).tag);
  if(self){ sb_add(b, "<", 1); sb_add(b, tn, (int)strlen(tn)); for(int i = 0; i < N(n).nattr; i++){ sb_add(b, " ", 1); sb_add(b, N(n).attrs[i].name, (int)strlen(N(n).attrs[i].name)); sb_add(b, "=\"", 2); sb_esc(b, N(n).attrs[i].val, 1); sb_add(b, "\"", 1); } sb_add(b, ">", 1); if(is_void(N(n).tag)) return; }
  for(int c = N(n).first; c >= 0; c = N(c).next) ser_node(b, c, 1);
  if(self){ sb_add(b, "</", 2); sb_add(b, tn, (int)strlen(tn)); sb_add(b, ">", 1); }
}
static void text_of(struct sbuf *b, int n){ for(int c = N(n).first; c >= 0; c = N(c).next){ if(N(c).type == 1) sb_add(b, N(c).text, (int)strlen(N(c).text)); else if(N(c).tag != TAG_script && N(c).tag != TAG_style) text_of(b, c); } }

/* ---- natives ---- */
JSFN(n_root){ return JS_NewInt32(c, g_root); }
JSFN(n_body){ return JS_NewInt32(c, g_body); }
JSFN(n_head){ return JS_NewInt32(c, g_head); }
JSFN(n_type){ int n = ARGI(0); return JS_NewInt32(c, valid_node(n) ? (N(n).type == 0 ? 1 : 3) : 0); }
JSFN(n_tag){ int n = ARGI(0); return valid_node(n) && N(n).type == 0 ? js_str(c, tag_name(N(n).tag)) : JS_NULL; }
JSFN(n_parent){ int n = ARGI(0); return JS_NewInt32(c, valid_node(n) ? N(n).parent : -1); }
JSFN(n_first){ int n = ARGI(0); return JS_NewInt32(c, valid_node(n) ? N(n).first : -1); }
JSFN(n_last){ int n = ARGI(0); return JS_NewInt32(c, valid_node(n) ? N(n).last : -1); }
JSFN(n_next){ int n = ARGI(0); return JS_NewInt32(c, valid_node(n) ? N(n).next : -1); }
JSFN(n_prev){ int n = ARGI(0), r = -1; if(valid_node(n) && N(n).parent >= 0) for(int s = N(N(n).parent).first; s >= 0 && s != n; s = N(s).next) r = s; return JS_NewInt32(c, r); }
JSFN(n_attr){ int n = ARGI(0); if(!valid_node(n) || N(n).type) return JS_NULL; char *nm = argstr(c, argv[1]); const char *v = attr_of(n, nm); free(nm); return v ? js_str(c, v) : JS_NULL; }
JSFN(n_setAttr){ int n = ARGI(0); if(!valid_node(n) || N(n).type) return JS_UNDEFINED; char *nm = argstr(c, argv[1]), *v = argstr(c, argv[2]);
  for(char *p = nm; *p; p++) *p = (char)tolower((unsigned char)*p);
  set_attr(n, nm, v); if(!strcmp(nm, "value") && N(n).value){ free(N(n).value); N(n).value = 0; }
  if(!strcmp(nm, "src") || !strcmp(nm, "srcset")){ js_img_dirty = 1; N(n).res = 0; } js_dirty = 1; free(nm); free(v); return JS_UNDEFINED; }
JSFN(n_removeAttr){ int n = ARGI(0); if(valid_node(n) && !N(n).type){ char *nm = argstr(c, argv[1]); del_attr(n, nm); free(nm); js_dirty = 1; } return JS_UNDEFINED; }
JSFN(n_attrs){ int n = ARGI(0); JSValue a = JS_NewArray(c); if(!valid_node(n) || N(n).type) return a;
  for(int i = 0; i < N(n).nattr; i++){ JSValue p = JS_NewArray(c); JS_SetPropertyUint32(c, p, 0, js_str(c, N(n).attrs[i].name)); JS_SetPropertyUint32(c, p, 1, js_str(c, N(n).attrs[i].val)); JS_SetPropertyUint32(c, a, i, p); } return a; }
JSFN(n_text){ int n = ARGI(0); return valid_node(n) && N(n).type == 1 ? js_str(c, N(n).text) : JS_NULL; }
JSFN(n_setText){ int n = ARGI(0); if(valid_node(n) && N(n).type == 1){ char *s = argstr(c, argv[1]); N(n).text = a_str(s, (int)strlen(s)); free(s); js_dirty = 1; } return JS_UNDEFINED; }
JSFN(n_create){ char *tg = argstr(c, argv[0]); int el = node_new(0, tag_id(tg, (int)strlen(tg))); free(tg); if(el >= 0 && N(el).tag == TAG_img) js_img_dirty = 1; return JS_NewInt32(c, el); }
JSFN(n_createText){ char *s = argstr(c, argv[0]); int tn = node_new(1, TAG_TEXT); if(tn >= 0) N(tn).text = a_str(s, (int)strlen(s)); free(s); return JS_NewInt32(c, tn); }
JSFN(n_append){ int p = ARGI(0), ch = ARGI(1); if(valid_node(p) && valid_node(ch) && p != ch){ if(is_ancestor(ch, p)) return JS_UNDEFINED; unlink_node(ch); node_append(p, ch); js_dirty = 1; js_img_dirty = 1; } return JS_UNDEFINED; }
JSFN(n_insertBefore){ int p = ARGI(0), ch = ARGI(1), ref = ARGI(2); if(valid_node(p) && valid_node(ch) && p != ch){ insert_before(p, ch, valid_node(ref) ? ref : -1); js_dirty = 1; js_img_dirty = 1; } return JS_UNDEFINED; }
JSFN(n_remove){ int n = ARGI(0); if(valid_node(n)){ unlink_node(n); js_dirty = 1; } return JS_UNDEFINED; }
JSFN(n_textContent){ int n = ARGI(0); if(!valid_node(n)) return js_str(c, ""); if(N(n).type == 1) return js_str(c, N(n).text);
  struct sbuf b = {0}; text_of(&b, n); JSValue r = js_str(c, b.p ? b.p : ""); free(b.p); return r; }
JSFN(n_setTextContent){ int n = ARGI(0); if(!valid_node(n)) return JS_UNDEFINED; char *s = argstr(c, argv[1]);
  if(N(n).type == 1){ N(n).text = a_str(s, (int)strlen(s)); } else { N(n).first = N(n).last = -1; if(*s){ int tn = node_new(1, TAG_TEXT); if(tn >= 0){ N(tn).text = a_str(s, (int)strlen(s)); node_append(n, tn); } } }
  js_dirty = 1; free(s); return JS_UNDEFINED; }
JSFN(n_innerHTML){ int n = ARGI(0); if(!valid_node(n)) return js_str(c, ""); struct sbuf b = {0}; ser_node(&b, n, ARGI(1)); JSValue r = js_str(c, b.p ? b.p : ""); free(b.p); return r; }
JSFN(n_setInnerHTML){ int n = ARGI(0); if(!valid_node(n) || N(n).type) return JS_UNDEFINED; char *s = argstr(c, argv[1]);
  N(n).first = N(n).last = -1; if(*s) html_fragment(n, s, (int)strlen(s)); js_dirty = 1; js_img_dirty = 1; free(s); return JS_UNDEFINED; }
JSFN(n_query){ int root = ARGI(0), all = ARGI(2); char *sel = argstr(c, argv[1]); JSValue arr = JS_NewArray(c); int cnt = 0, first = -1;
  if(valid_node(root) && parse_query(sel)) query_walk(root, root, c, arr, &cnt, all, &first);
  free(sel); if(all) return arr; JS_FreeValue(c, arr); return JS_NewInt32(c, first); }
JSFN(n_matches){ int n = ARGI(0); char *sel = argstr(c, argv[1]); int r = valid_node(n) && N(n).type == 0 && parse_query(sel) && query_match(n); free(sel); return JS_NewBool(c, r); }
JSFN(n_value){ int n = ARGI(0); if(!valid_node(n) || N(n).type) return js_str(c, ""); int ct = ctl_type(n);
  if(ct == 4){ const char *v = 0; select_option(n, select_index(n), &v); return js_str(c, v ? v : ""); } return js_str(c, ctl_value(n)); }
JSFN(n_setValue){ int n = ARGI(0); if(valid_node(n) && !N(n).type){ char *s = argstr(c, argv[1]); int ct = ctl_type(n);
  if(ct == 4){ int cnt = select_count(n); for(int i = 0; i < cnt; i++){ const char *v = 0; select_option(n, i, &v); if(v && !strcmp(v, s)){ char b[16]; snprintf(b, sizeof b, "%d", i); set_value(n, b); break; } } }
  else set_value(n, s); free(s); js_dirty = 1; } return JS_UNDEFINED; }
JSFN(n_checked){ int n = ARGI(0); return JS_NewBool(c, valid_node(n) && !N(n).type && ctl_checked(n)); }
JSFN(n_setChecked){ int n = ARGI(0); if(valid_node(n) && !N(n).type){ set_value(n, ARGI(1) ? "1" : "0"); js_dirty = 1; } return JS_UNDEFINED; }
JSFN(n_rect){ int n = ARGI(0); JSValue a = JS_NewArray(c); int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
  if(valid_node(n)) for(int i = 0; i < nops; i++){ struct op *o = &ops[i]; if(o->kind == OP_RECT || o->node < 0 || !is_ancestor(n, o->node)) continue;
    if(o->x < x0) x0 = o->x; if(o->y < y0) y0 = o->y; if(o->x + o->w > x1) x1 = o->x + o->w; if(o->y + o->h > y1) y1 = o->y + o->h; }
  if(x1 < 0){ x0 = y0 = 0; x1 = y1 = 0; }
  JS_SetPropertyUint32(c, a, 0, JS_NewInt32(c, x0)); JS_SetPropertyUint32(c, a, 1, JS_NewInt32(c, y0 - scroll)); JS_SetPropertyUint32(c, a, 2, JS_NewInt32(c, x1 - x0)); JS_SetPropertyUint32(c, a, 3, JS_NewInt32(c, y1 - y0)); return a; }
JSFN(n_log){ for(int i = 0; i < argc; i++){ const char *s = JS_ToCString(c, argv[i]); if(i) putchar(' '); fputs(s ? s : "?", stdout); JS_FreeCString(c, s); } putchar('\n'); return JS_UNDEFINED; }
JSFN(n_now){ return JS_NewFloat64(c, (double)API(kernel32, GetTickCount64)()); }
JSFN(n_navigate){ char *u = argstr(c, argv[0]); char full[900]; url_resolve(cur_url, u, full, sizeof full); snprintf(js_nav_url, sizeof js_nav_url, "%s", full); js_nav_pending = 1; free(u); return JS_UNDEFINED; }
JSFN(n_url){ return js_str(c, cur_url); }
JSFN(n_title){ return js_str(c, page_title); }
JSFN(n_setTitle){ char *s = argstr(c, argv[0]); snprintf(page_title, sizeof page_title, "%s", s); free(s); js_title_dirty = 1; return JS_UNDEFINED; }
JSFN(n_alert){ char *s = argstr(c, argv[0]); API(user32, MessageBoxA)(hwnd, s, "Willows Browser", 0); free(s); return JS_UNDEFINED; }
JSFN(n_size){ JSValue a = JS_NewArray(c); JS_SetPropertyUint32(c, a, 0, JS_NewInt32(c, view_w)); JS_SetPropertyUint32(c, a, 1, JS_NewInt32(c, view_h)); JS_SetPropertyUint32(c, a, 2, JS_NewInt32(c, scroll)); return a; }
JSFN(n_scrollTo){ scroll = ARGI(0); clamp_scroll(); repaint(); return JS_UNDEFINED; }
JSFN(n_timers){ js_timers_active = ARGI(0); return JS_UNDEFINED; }
JSFN(n_focus){ int n = ARGI(0); if(valid_node(n) && !N(n).type){ int ct = ctl_type(n); if(ct == 0 || ct == 5){ g_focus = n; url_focus = 0; repaint(); } } return JS_UNDEFINED; }
JSFN(n_http){                                                          /* synchronous GET: returns {status, body, url, type} */
  char *u = argstr(c, argv[0]); char full[900]; url_resolve(cur_url, u, full, sizeof full); free(u);
  char *method = argc > 1 ? argstr(c, argv[1]) : strdup("GET"); char *body = argc > 2 && !JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]) ? argstr(c, argv[2]) : 0; char *hdrs = argc > 3 ? argstr(c, argv[3]) : strdup("");
  JSValue r = JS_NewObject(c);
  char fin[900], ct[100]; DWORD st = 0; int n = 0; unsigned char *b = http_request(full, method, hdrs, body, body ? (int)strlen(body) : 0, 4 << 20, &n, ct, sizeof ct, fin, sizeof fin, &st, 0);
  free(method); free(body); free(hdrs);
  if(!b){ JS_SetPropertyStr(c, r, "status", JS_NewInt32(c, 0)); JS_SetPropertyStr(c, r, "body", js_str(c, "")); JS_SetPropertyStr(c, r, "url", js_str(c, full)); JS_SetPropertyStr(c, r, "type", js_str(c, "")); return r; }
  JS_SetPropertyStr(c, r, "status", JS_NewInt32(c, (int)st)); JS_SetPropertyStr(c, r, "body", JS_NewStringLen(c, (const char *)b, (size_t)n)); JS_SetPropertyStr(c, r, "url", js_str(c, fin)); JS_SetPropertyStr(c, r, "type", js_str(c, ct)); free(b); return r; }
JSFN(n_rand){ static unsigned s = 2463534242u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return JS_NewInt32(c, (int)(s & 0x7FFFFFFF)); }

static const JSCFunctionListEntry js_n_funcs[] = {
#define F(nm, len) JS_CFUNC_DEF(#nm, len, n_##nm)
  F(root,0), F(body,0), F(head,0), F(type,1), F(tag,1), F(parent,1), F(first,1), F(last,1), F(next,1), F(prev,1), F(attr,2), F(setAttr,3), F(removeAttr,2), F(attrs,1),
  F(text,1), F(setText,2), F(create,1), F(createText,1), F(append,2), F(insertBefore,3), F(remove,1), F(textContent,1), F(setTextContent,2), F(innerHTML,2), F(setInnerHTML,2),
  F(query,3), F(matches,2), F(value,1), F(setValue,2), F(checked,1), F(setChecked,2), F(rect,1), F(log,1), F(now,0), F(navigate,1), F(url,0), F(title,0), F(setTitle,1),
  F(alert,1), F(size,0), F(scrollTo,1), F(timers,1), F(focus,1), F(http,4), F(rand,0),
#undef F
};

/* ---- running code ---- */
static int js_interrupt(JSContext *cx, void *op){ (void)cx; (void)op; return API(kernel32, GetTickCount64)() > js_deadline; }
static void js_report(void){
  JSValue ex = JS_GetException(js_ctx); const char *s = JS_ToCString(js_ctx, ex);
  JSValue st = JS_GetPropertyStr(js_ctx, ex, "stack"); const char *ss = JS_IsUndefined(st) ? 0 : JS_ToCString(js_ctx, st);
  printf("[js error] %s%s%s\n", s ? s : "?", ss ? "\n" : "", ss ? ss : ""); JS_FreeCString(js_ctx, ss); JS_FreeValue(js_ctx, st); JS_FreeCString(js_ctx, s); JS_FreeValue(js_ctx, ex);
}
static void js_run_jobs(void){ JSContext *c2; int guard = 0; while(guard++ < 10000 && JS_ExecutePendingJob(js_rt, &c2) > 0){} }
static void js_arm(void){ js_deadline = API(kernel32, GetTickCount64)() + 4000; }
static void js_eval(const char *code, int len, const char *name){
  if(!js_ctx) return; js_arm();
  JSValue r = JS_Eval(js_ctx, code, (size_t)len, name, JS_EVAL_TYPE_GLOBAL); if(JS_IsException(r)) js_report(); JS_FreeValue(js_ctx, r); js_run_jobs();
}
static void js_page_reset(void){
  if(js_ctx){ JS_FreeContext(js_ctx); js_ctx = 0; } if(js_rt){ JS_FreeRuntime(js_rt); js_rt = 0; }
  js_dirty = js_img_dirty = js_title_dirty = js_nav_pending = 0; js_timers_active = 0; qlast[0] = 0; nq = 0;
}
static void js_page_start(void){
  js_page_reset();
  js_rt = JS_NewRuntime(); if(!js_rt) return; JS_SetMaxStackSize(js_rt, 300 * 1024); JS_SetMemoryLimit(js_rt, 96u << 20); JS_SetInterruptHandler(js_rt, js_interrupt, 0);
  js_ctx = JS_NewContext(js_rt); if(!js_ctx){ JS_FreeRuntime(js_rt); js_rt = 0; return; }
  JSValue g = JS_GetGlobalObject(js_ctx), n = JS_NewObject(js_ctx); JS_SetPropertyFunctionList(js_ctx, n, js_n_funcs, sizeof js_n_funcs / sizeof *js_n_funcs);
  JS_SetPropertyStr(js_ctx, g, "__n", n); JS_FreeValue(js_ctx, g);
  js_eval(PRELUDE_JS, (int)sizeof PRELUDE_JS - 1, "prelude.js");
}
/* call a global function `name` with (id, type, x, y, extra); returns 1 when the handler cancelled the default action */
static int js_dispatch(int node, const char *type, int x, int y, int key){
  if(!js_ctx) return 0; js_arm();
  JSValue g = JS_GetGlobalObject(js_ctx), f = JS_GetPropertyStr(js_ctx, g, "__dispatch"); int cancelled = 0;
  if(JS_IsFunction(js_ctx, f)){ JSValue a[5] = { JS_NewInt32(js_ctx, node), JS_NewString(js_ctx, type), JS_NewInt32(js_ctx, x), JS_NewInt32(js_ctx, y), JS_NewInt32(js_ctx, key) };
    JSValue r = JS_Call(js_ctx, f, g, 5, a); if(JS_IsException(r)) js_report(); else cancelled = JS_ToBool(js_ctx, r) > 0; JS_FreeValue(js_ctx, r); for(int i = 0; i < 5; i++) JS_FreeValue(js_ctx, a[i]); }
  JS_FreeValue(js_ctx, f); JS_FreeValue(js_ctx, g); js_run_jobs(); return cancelled;
}
static void js_tick_call(const char *fn){
  if(!js_ctx) return; js_arm();
  JSValue g = JS_GetGlobalObject(js_ctx), f = JS_GetPropertyStr(js_ctx, g, fn);
  if(JS_IsFunction(js_ctx, f)){ JSValue r = JS_Call(js_ctx, f, g, 0, 0); if(JS_IsException(r)) js_report(); JS_FreeValue(js_ctx, r); }
  JS_FreeValue(js_ctx, f); JS_FreeValue(js_ctx, g); js_run_jobs();
}
/* the page's scripts, in document order */
static void js_run_scripts(int n){
  for(int c = N(n).first; c >= 0; c = N(c).next){
    if(N(c).type != 0) continue;
    if(N(c).tag == TAG_script){
      const char *ty = attr_of(c, "type"); const char *src = attr_of(c, "src");
      int ok = !ty || !*ty || !strcasecmp_(ty, "text/javascript") || !strcasecmp_(ty, "application/javascript") || !strcasecmp_(ty, "text/ecmascript");
      if(attr_of(c, "nomodule")) ok = 1;
      if(!ok) continue;
      if(src && *src){ char full[900]; url_resolve(cur_url, src, full, sizeof full); char fin[900], ct[100]; DWORD st = 0; int len = 0; unsigned char *b = http_get(full, 3 << 20, &len, ct, sizeof ct, fin, sizeof fin, &st, 0);
        if(b && st == 200){ js_eval((const char *)b, len, full); } else printf("[js] could not load script %s\n", full); free(b); }
      else { for(int t = N(c).first; t >= 0; t = N(t).next) if(N(t).type == 1 && N(t).text) js_eval(N(t).text, (int)strlen(N(t).text), "<inline script>"); }
    } else js_run_scripts(c);
  }
}
static void js_page_run(void){
  js_page_start(); if(!js_ctx) return;
  js_run_scripts(g_root);
  js_dispatch(-1, "__domready", 0, 0, 0);
}
#endif
