/* DOM + HTML parser for the Willows browser. Pure C, no OS calls. */
#ifndef WEB_DOM_H
#define WEB_DOM_H
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int strncasecmp_(const char *a, const char *b, int n){ for(; n; n--, a++, b++){ int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if(x != y) return x - y; if(!x) return 0; } return 0; }
static int strcasecmp_(const char *a, const char *b){ for(;; a++, b++){ int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if(x != y) return x - y; if(!x) return 0; } }
/* ---------- string arena ---------- */
struct achunk { struct achunk *next; int used, cap; char data[1]; };
static struct achunk *arena_head;
static void *a_alloc(int n){
  if(!arena_head || arena_head->used + n > arena_head->cap){
    int cap = n > 65536 ? n + 16 : 65536; struct achunk *c = malloc(sizeof *c + cap); if(!c) return 0;
    c->next = arena_head; c->used = 0; c->cap = cap; arena_head = c;
  }
  char *p = arena_head->data + arena_head->used; arena_head->used += (n + 7) & ~7; return p;
}
static char *a_str(const char *s, int n){ char *d = a_alloc(n + 1); if(!d) return ""; memcpy(d, s, n); d[n] = 0; return d; }
static void a_reset(void){ while(arena_head){ struct achunk *n = arena_head->next; free(arena_head); arena_head = n; } }

/* ---------- tags ---------- */
#define TAGS(X) X(a) X(abbr) X(address) X(area) X(article) X(aside) X(audio) X(b) X(base) X(bdi) X(bdo) X(big) X(blockquote) X(body) X(br) X(button) X(canvas) X(caption) X(center) \
  X(cite) X(code) X(col) X(colgroup) X(data) X(dd) X(del) X(details) X(dfn) X(dialog) X(div) X(dl) X(dt) X(em) X(embed) X(fieldset) X(figcaption) X(figure) X(font) X(footer) X(form) \
  X(h1) X(h2) X(h3) X(h4) X(h5) X(h6) X(head) X(header) X(hgroup) X(hr) X(html) X(i) X(iframe) X(img) X(input) X(ins) X(kbd) X(label) X(legend) X(li) X(link) X(main) X(map) X(mark) \
  X(menu) X(meta) X(meter) X(nav) X(nobr) X(noscript) X(object) X(ol) X(optgroup) X(option) X(output) X(p) X(param) X(picture) X(pre) X(progress) X(q) X(rp) X(rt) X(ruby) X(s) \
  X(samp) X(script) X(search) X(section) X(select) X(small) X(source) X(span) X(strike) X(strong) X(style) X(sub) X(summary) X(sup) X(svg) X(table) X(tbody) X(td) X(template) \
  X(textarea) X(tfoot) X(th) X(thead) X(time) X(title) X(tr) X(track) X(tt) X(u) X(ul) X(var) X(video) X(wbr) X(xmp)
enum {
  TAG_NONE = 0, TAG_TEXT,
#define X(n) TAG_##n,
  TAGS(X)
#undef X
  TAG_FIRST_UNKNOWN
};
static const char *const tag_names[] = { "", "#text",
#define X(n) #n,
  TAGS(X)
#undef X
};
#define MAXUNK 2000
static char *unk_tags[MAXUNK]; static int nunk;
static int tag_id(const char *s, int n){
  char b[72]; if(n <= 0) return TAG_FIRST_UNKNOWN + MAXUNK - 1; if(n > 70) n = 70;
  for(int i = 0; i < n; i++) b[i] = (char)tolower((unsigned char)s[i]); b[n] = 0;
  /* binary search over the (alphabetical) known names */
  int lo = 2, hi = TAG_FIRST_UNKNOWN - 1;
  while(lo <= hi){ int mid = (lo + hi) / 2; int c = strcmp(b, tag_names[mid]); if(c == 0) return mid; if(c < 0) hi = mid - 1; else lo = mid + 1; }
  for(int i = 0; i < nunk; i++) if(!strcmp(unk_tags[i], b)) return TAG_FIRST_UNKNOWN + i;
  if(nunk < MAXUNK - 1){ unk_tags[nunk] = a_str(b, n); return TAG_FIRST_UNKNOWN + nunk++; }
  return TAG_FIRST_UNKNOWN + MAXUNK - 1;
}
static const char *tag_name(int t){ return t < TAG_FIRST_UNKNOWN ? tag_names[t] : (t - TAG_FIRST_UNKNOWN < nunk ? unk_tags[t - TAG_FIRST_UNKNOWN] : "unknown"); }

/* ---------- nodes ---------- */
struct attr { const char *name, *val; };
struct style;
struct node {
  unsigned char type;                    /* 0 element, 1 text */
  short tag; short nattr;
  int parent, first, last, next;         /* indices, -1 = none */
  const char *text;                      /* text node content (UTF-8, entities decoded) */
  struct attr *attrs;
  struct style *st;
  int link;                              /* link index for <a href>, -1 otherwise */
  char *value;                           /* form controls: current value (malloc'd) */
  int res;                               /* image resource index + 1 (0 = none) */
};
static struct node *nodes; static int nnodes, ncap;
#define N(i) (nodes[i])
static int node_new(int type, int tag){
  if(nnodes >= ncap){ int nc = ncap ? ncap * 2 : 2048; struct node *p = realloc(nodes, (size_t)nc * sizeof *p); if(!p) return -1; nodes = p; ncap = nc; }
  struct node *n = &nodes[nnodes]; memset(n, 0, sizeof *n); n->type = (unsigned char)type; n->tag = (short)tag; n->parent = n->first = n->last = n->next = -1; n->link = -1;
  return nnodes++;
}
static void node_append(int parent, int c){
  N(c).parent = parent; N(c).next = -1;
  if(N(parent).last < 0) N(parent).first = c; else N(N(parent).last).next = c;
  N(parent).last = c;
}
static const char *get_attrn(int n, const char *name){
  for(int i = 0; i < N(n).nattr; i++) if(!strcmp(N(n).attrs[i].name, name)) return N(n).attrs[i].val;
  return 0;
}
#define attr_of(n, name) get_attrn(n, name)

/* ---------- entities & UTF-8 ---------- */
static int utf8_put(char *o, unsigned cp){
  if(cp < 0x80){ o[0] = (char)cp; return 1; }
  if(cp < 0x800){ o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); return 2; }
  if(cp < 0x10000){ o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
  o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 63)); o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63)); return 4;
}
static const struct { const char *n; unsigned cp; } ENT[] = {
  {"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",0xA0},{"copy",0xA9},{"reg",0xAE},{"mdash",0x2014},{"ndash",0x2013},{"hellip",0x2026},{"rsquo",0x2019},{"lsquo",0x2018},
  {"rdquo",0x201D},{"ldquo",0x201C},{"sbquo",0x201A},{"bdquo",0x201E},{"bull",0x2022},{"middot",0xB7},{"raquo",0xBB},{"laquo",0xAB},{"times",0xD7},{"divide",0xF7},{"trade",0x2122},{"euro",0x20AC},
  {"deg",0xB0},{"pound",0xA3},{"yen",0xA5},{"cent",0xA2},{"larr",0x2190},{"rarr",0x2192},{"uarr",0x2191},{"darr",0x2193},{"harr",0x2194},{"para",0xB6},{"sect",0xA7},{"plusmn",0xB1},
  {"frac12",0xBD},{"frac14",0xBC},{"frac34",0xBE},{"sup1",0xB9},{"sup2",0xB2},{"sup3",0xB3},{"micro",0xB5},{"iexcl",0xA1},{"iquest",0xBF},{"ensp",0x2002},{"emsp",0x2003},{"thinsp",0x2009},
  {"zwnj",0x200C},{"zwj",0x200D},{"shy",0xAD},{"dagger",0x2020},{"Dagger",0x2021},{"permil",0x2030},{"prime",0x2032},{"Prime",0x2033},{"lsaquo",0x2039},{"rsaquo",0x203A},{"oline",0x203E},
  {"infin",0x221E},{"ne",0x2260},{"le",0x2264},{"ge",0x2265},{"minus",0x2212},{"sum",0x2211},{"radic",0x221A},{"asymp",0x2248},{"equiv",0x2261},{"hearts",0x2665},{"spades",0x2660},
  {"clubs",0x2663},{"diams",0x2666},{"check",0x2713},{"alpha",0x3B1},{"beta",0x3B2},{"gamma",0x3B3},{"delta",0x3B4},{"pi",0x3C0},{"sigma",0x3C3},{"omega",0x3C9},{"lambda",0x3BB},{"mu",0x3BC},
  {"Agrave",0xC0},{"Aacute",0xC1},{"Acirc",0xC2},{"Atilde",0xC3},{"Auml",0xC4},{"Aring",0xC5},{"AElig",0xC6},{"Ccedil",0xC7},{"Egrave",0xC8},{"Eacute",0xC9},{"Ecirc",0xCA},{"Euml",0xCB},
  {"Igrave",0xCC},{"Iacute",0xCD},{"Icirc",0xCE},{"Iuml",0xCF},{"Ntilde",0xD1},{"Ograve",0xD2},{"Oacute",0xD3},{"Ocirc",0xD4},{"Otilde",0xD5},{"Ouml",0xD6},{"Oslash",0xD8},{"Ugrave",0xD9},
  {"Uacute",0xDA},{"Ucirc",0xDB},{"Uuml",0xDC},{"szlig",0xDF},{"agrave",0xE0},{"aacute",0xE1},{"acirc",0xE2},{"atilde",0xE3},{"auml",0xE4},{"aring",0xE5},{"aelig",0xE6},{"ccedil",0xE7},
  {"egrave",0xE8},{"eacute",0xE9},{"ecirc",0xEA},{"euml",0xEB},{"igrave",0xEC},{"iacute",0xED},{"icirc",0xEE},{"iuml",0xEF},{"ntilde",0xF1},{"ograve",0xF2},{"oacute",0xF3},{"ocirc",0xF4},
  {"otilde",0xF5},{"ouml",0xF6},{"oslash",0xF8},{"ugrave",0xF9},{"uacute",0xFA},{"ucirc",0xFB},{"uuml",0xFC},{"yuml",0xFF},{"hairsp",0x200A},{"lrm",0x200E},{"rlm",0x200F},{"nbsp",0xA0},
};
/* s points at '&'. Returns bytes written to out (0 = not an entity); *used = input bytes consumed. */
static int entity_at(const char *s, int n, char *out, int *used){
  int k = 1; while(k < n && k < 12 && s[k] != ';' && (isalnum((unsigned char)s[k]) || s[k] == '#')) k++;
  if(k >= n || s[k] != ';') return 0;
  *used = k + 1;
  if(s[1] == '#'){
    unsigned v = 0; int i = 2;
    if(s[2] == 'x' || s[2] == 'X'){ i = 3; for(; i < k; i++){ int c = tolower((unsigned char)s[i]); int d = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1; if(d < 0) return 0; v = v * 16 + d; if(v > 0x10FFFF) return 0; } if(k == 3) return 0; }
    else { for(; i < k; i++){ if(!isdigit((unsigned char)s[i])) return 0; v = v * 10 + (s[i] - '0'); if(v > 0x10FFFF) return 0; } if(k == 2) return 0; }
    if(!v || v == 0xD800) v = 0xFFFD;
    if(v >= 0x80 && v < 0xA0){ static const unsigned w[32] = {0x20AC,0x81,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,0x2C6,0x2030,0x160,0x2039,0x152,0x8D,0x17D,0x8F,0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0x9D,0x17E,0x178}; v = w[v - 0x80]; }
    return utf8_put(out, v);
  }
  for(unsigned i = 0; i < sizeof ENT / sizeof *ENT; i++) if((int)strlen(ENT[i].n) == k - 1 && !strncmp(s + 1, ENT[i].n, k - 1)) return utf8_put(out, ENT[i].cp);
  return 0;
}
/* decode entities in [s, s+n) into a new arena string */
static char *decode_str(const char *s, int n){
  char *d = a_alloc(n + 1); if(!d) return "";
  int o = 0;
  for(int i = 0; i < n; ){
    if(s[i] == '&'){ char t[4]; int used; int w = entity_at(s + i, n - i, t, &used); if(w){ memcpy(d + o, t, w); o += w; i += used; continue; } }
    d[o++] = s[i++];
  }
  d[o] = 0; return d;
}

/* ---------- parser ---------- */
static int is_void(int t){
  switch(t){ case TAG_area: case TAG_base: case TAG_br: case TAG_col: case TAG_embed: case TAG_hr: case TAG_img: case TAG_input: case TAG_link: case TAG_meta: case TAG_param: case TAG_source: case TAG_track: case TAG_wbr: return 1; }
  return 0;
}
static int is_blockish(int t){      /* tags whose start closes an open <p> */
  switch(t){ case TAG_address: case TAG_article: case TAG_aside: case TAG_blockquote: case TAG_center: case TAG_details: case TAG_dialog: case TAG_div: case TAG_dl: case TAG_fieldset: case TAG_figcaption:
    case TAG_figure: case TAG_footer: case TAG_form: case TAG_h1: case TAG_h2: case TAG_h3: case TAG_h4: case TAG_h5: case TAG_h6: case TAG_header: case TAG_hgroup: case TAG_hr: case TAG_main: case TAG_menu: case TAG_nav:
    case TAG_ol: case TAG_p: case TAG_pre: case TAG_section: case TAG_table: case TAG_ul: case TAG_li: case TAG_dd: case TAG_dt: return 1; }
  return 0;
}
static int is_head_tag(int t){ return t == TAG_title || t == TAG_meta || t == TAG_link || t == TAG_style || t == TAG_script || t == TAG_base || t == TAG_template || t == TAG_noscript; }
static int g_root, g_head, g_body, g_frag;                              /* g_frag: parsing an innerHTML fragment into an existing element */
static int stack[512]; static int sp;
static int cur_node(void){ return sp ? stack[sp - 1] : g_root; }
static int in_scope(int tag, int stop1, int stop2){       /* is an open `tag` above the nearest stop element? */
  for(int i = sp - 1; i >= 0; i--){ int t = N(stack[i]).tag; if(t == tag) return i; if(t == stop1 || t == stop2 || t == TAG_table || t == TAG_html || t == TAG_td || t == TAG_th || t == TAG_caption) return -1; }
  return -1;
}
static void pop_to(int idx){ while(sp > idx) sp--; }
static void ensure_body(void){
  if(g_body >= 0) return;
  g_body = node_new(0, TAG_body); if(g_body < 0) return; node_append(g_root, g_body);
  pop_to(0); stack[sp++] = g_root; stack[sp++] = g_body;
}
static void add_text(const char *s, int n, int decode){
  if(n <= 0) return;
  int cur = cur_node();
  if(cur == g_root || cur == g_head){                                   /* stray text before <body>: whitespace is dropped, anything else starts the body */
    int ws = 1; for(int i = 0; i < n; i++) if(!isspace((unsigned char)s[i])) ws = 0;
    if(ws) return;
    if(cur == g_head) pop_to(1);
    ensure_body(); cur = cur_node();
  }
  const char *t = decode ? decode_str(s, n) : a_str(s, n);
  int last = N(cur).last;
  if(last >= 0 && N(last).type == 1){                                    /* merge with previous text node */
    int l1 = (int)strlen(N(last).text), l2 = (int)strlen(t); char *m = a_alloc(l1 + l2 + 1); if(!m) return; memcpy(m, N(last).text, l1); memcpy(m + l1, t, l2); m[l1 + l2] = 0; N(last).text = m; return;
  }
  int tn = node_new(1, TAG_TEXT); if(tn < 0) return; N(tn).text = t; node_append(cur, tn);
}
static void parse_attrs(int el, const char *s, int n){
  struct attr tmp[24]; int na = 0; int i = 0;
  while(i < n && na < 24){
    while(i < n && (isspace((unsigned char)s[i]) || s[i] == '/')) i++;
    if(i >= n) break;
    int ns = i; while(i < n && !isspace((unsigned char)s[i]) && s[i] != '=' && s[i] != '/' && s[i] != '>') i++;
    if(i == ns){ i++; continue; }
    char *name = a_alloc(i - ns + 1); for(int k = 0; k < i - ns; k++) name[k] = (char)tolower((unsigned char)s[ns + k]); name[i - ns] = 0;
    while(i < n && isspace((unsigned char)s[i])) i++;
    const char *val = "";
    if(i < n && s[i] == '='){
      i++; while(i < n && isspace((unsigned char)s[i])) i++;
      if(i < n && (s[i] == '"' || s[i] == '\'')){ char q = s[i++]; int vs = i; while(i < n && s[i] != q) i++; val = decode_str(s + vs, i - vs); if(i < n) i++; }
      else { int vs = i; while(i < n && !isspace((unsigned char)s[i]) && s[i] != '>') i++; val = decode_str(s + vs, i - vs); }
    }
    int dup = 0; for(int k = 0; k < na; k++) if(!strcmp(tmp[k].name, name)) dup = 1;
    if(!dup){ tmp[na].name = name; tmp[na].val = val; na++; }
  }
  if(na){ N(el).attrs = a_alloc(na * (int)sizeof(struct attr)); memcpy(N(el).attrs, tmp, na * sizeof(struct attr)); N(el).nattr = (short)na; }
}
static void start_tag(int tag, const char *as, int an, int selfclose){
  if(g_frag && (tag == TAG_html || tag == TAG_head || tag == TAG_body)) return;
  if(tag == TAG_html){
    /* merge attributes into the root; nothing else */
    if(an > 0 && !N(g_root).nattr) parse_attrs(g_root, as, an);
    return;
  }
  if(tag == TAG_head){ if(g_head < 0 && g_body < 0){ g_head = node_new(0, TAG_head); node_append(g_root, g_head); pop_to(1); stack[sp++] = g_head; } return; }
  if(tag == TAG_body){
    if(g_body < 0){ if(sp > 1) pop_to(1); ensure_body(); }
    if(an > 0 && !N(g_body).nattr) parse_attrs(g_body, as, an);
    return;
  }
  int cur = cur_node();
  if(N(cur).tag == TAG_head || cur == g_root){
    if(!is_head_tag(tag)){ if(N(cur).tag == TAG_head) pop_to(1); ensure_body(); }
    else if(cur == g_root && g_head < 0){ g_head = node_new(0, TAG_head); node_append(g_root, g_head); stack[sp++] = g_head; }
  }
  /* implied end tags */
  if(is_blockish(tag)){ int i = in_scope(TAG_p, 0, 0); if(i >= 0) pop_to(i); }
  switch(tag){
    case TAG_li: { for(int i = sp - 1; i >= 0; i--){ int t = N(stack[i]).tag; if(t == TAG_li){ pop_to(i); break; } if(t == TAG_ul || t == TAG_ol || t == TAG_menu || t == TAG_table || t == TAG_body) break; } break; }
    case TAG_dt: case TAG_dd: { for(int i = sp - 1; i >= 0; i--){ int t = N(stack[i]).tag; if(t == TAG_dt || t == TAG_dd){ pop_to(i); break; } if(t == TAG_dl || t == TAG_table || t == TAG_body) break; } break; }
    case TAG_tr: { for(int i = sp - 1; i >= 0; i--){ int t = N(stack[i]).tag; if(t == TAG_tr){ pop_to(i); break; } if(t == TAG_table || t == TAG_tbody || t == TAG_thead || t == TAG_tfoot) break; } break; }
    case TAG_td: case TAG_th: { for(int i = sp - 1; i >= 0; i--){ int t = N(stack[i]).tag; if(t == TAG_td || t == TAG_th){ pop_to(i); break; } if(t == TAG_tr || t == TAG_table) break; } break; }
    case TAG_thead: case TAG_tbody: case TAG_tfoot: { for(int i = sp - 1; i >= 0; i--){ int t = N(stack[i]).tag; if(t == TAG_tbody || t == TAG_thead || t == TAG_tfoot || t == TAG_tr || t == TAG_td || t == TAG_th){ pop_to(i); } else break; } break; }
    case TAG_option: { if(N(cur_node()).tag == TAG_option) pop_to(sp - 1); break; }
    case TAG_h1: case TAG_h2: case TAG_h3: case TAG_h4: case TAG_h5: case TAG_h6: { int t = N(cur_node()).tag; if(t >= TAG_h1 && t <= TAG_h6) pop_to(sp - 1); break; }
    case TAG_a: { int i = in_scope(TAG_a, 0, 0); if(i >= 0) pop_to(i); break; }
    case TAG_form: break;
  }
  int el = node_new(0, tag); if(el < 0) return;
  if(an > 0) parse_attrs(el, as, an);
  node_append(cur_node(), el);
  if(!is_void(tag) && !selfclose && sp < 500) stack[sp++] = el;
  else if(selfclose && !is_void(tag) && tag >= TAG_FIRST_UNKNOWN) { /* self-closed unknown tag: nothing */ }
  else if(selfclose && !is_void(tag) && sp < 500) stack[sp++] = el;      /* HTML ignores the slash on non-void elements */
}
static void end_tag(int tag){
  if(tag == TAG_br){ start_tag(TAG_br, "", 0, 0); return; }
  if(tag == TAG_html || tag == TAG_body || tag == TAG_head){ if(tag == TAG_head && sp > 1 && N(stack[sp-1]).tag == TAG_head) pop_to(1); return; }
  int tabletag = tag == TAG_tr || tag == TAG_td || tag == TAG_th || tag == TAG_tbody || tag == TAG_thead || tag == TAG_tfoot || tag == TAG_table || tag == TAG_caption;
  for(int i = sp - 1; i >= 1; i--){
    int t = N(stack[i]).tag;
    if(t == tag){ pop_to(i); return; }
    /* a stray end tag must not close table structure or the page body */
    if(!tabletag && (t == TAG_table || t == TAG_td || t == TAG_th || t == TAG_body || t == TAG_caption)) return;
    if(tabletag && t == TAG_table && tag != TAG_table) return;
  }
}
static void dom_reset(void){
  free(nodes); nodes = 0; nnodes = ncap = 0; a_reset(); nunk = 0; sp = 0; g_head = g_body = -1;
  g_root = node_new(0, TAG_html); stack[sp++] = g_root;
}
static int g_quirks;
static void html_parse_loop(const char *h, int len){
  int i = 0;
  while(i < len){
    if(h[i] != '<'){
      int j = i; while(j < len && h[j] != '<') j++;
      add_text(h + i, j - i, 1); i = j; continue;
    }
    if(i + 3 < len && !memcmp(h + i, "<!--", 4)){ const char *e = strstr(h + i + 4, "-->"); i = e && (int)(e - h) < len ? (int)(e - h) + 3 : len; continue; }
    if(i + 1 < len && (h[i + 1] == '!' || h[i + 1] == '?')){ int j = i; while(j < len && h[j] != '>') j++; i = j + 1; continue; }
    int closing = i + 1 < len && h[i + 1] == '/';
    int ns = i + 1 + closing;
    if(ns >= len || !isalpha((unsigned char)h[ns])){ add_text("<", 1, 0); i++; continue; }          /* a lone '<' is text */
    int ne = ns; while(ne < len && !isspace((unsigned char)h[ne]) && h[ne] != '>' && h[ne] != '/') ne++;
    int tag = tag_id(h + ns, ne - ns);
    int j = ne; char q = 0;                                               /* find the closing '>' outside quotes */
    while(j < len && (q || h[j] != '>')){ if(h[j] == '"' || h[j] == '\''){ if(!q) q = h[j]; else if(q == h[j]) q = 0; } j++; }
    int selfclose = j > ne && h[j - 1] == '/';
    const char *as = h + ne; int an = j - ne; if(selfclose) an--;
    i = j < len ? j + 1 : len;
    if(closing){ end_tag(tag); continue; }
    start_tag(tag, as, an, selfclose);
    if(tag == TAG_script || tag == TAG_style || tag == TAG_textarea || tag == TAG_title || tag == TAG_xmp || tag == TAG_iframe){   /* raw text up to the matching end tag */
      char endtag[24]; int el = (int)strlen(tag_names[tag]); endtag[0] = '<'; endtag[1] = '/'; memcpy(endtag + 2, tag_names[tag], el); el += 2;
      int k = i; while(k < len){ if(h[k] == '<' && k + el < len && h[k + 1] == '/' && !strncasecmp_(h + k, endtag, el)) break; k++; }
      if(tag != TAG_iframe){ int cur = cur_node(); if(N(cur).tag == tag){ int tn = node_new(1, TAG_TEXT); if(tn >= 0){ N(tn).text = (tag == TAG_style || tag == TAG_xmp || tag == TAG_script) ? a_str(h + i, k - i) : decode_str(h + i, k - i); node_append(cur, tn); } } }
      i = k; if(i < len){ while(i < len && h[i] != '>') i++; if(i < len) i++; }
      if(sp > 1 && N(stack[sp - 1]).tag == tag) sp--;
    }
  }
  if(g_body < 0) ensure_body();
}
static void html_parse(const char *h, int len){
  dom_reset();
  { int lim = len < 400 ? len : 400; g_quirks = 1; for(int i = 0; i + 9 < lim; i++) if(h[i] == '<' && h[i + 1] == '!' && !strncasecmp_(h + i + 2, "doctype", 7)){ g_quirks = 0; break; } }
  html_parse_loop(h, len);
}
/* parse `h` as the children of element `parent` (innerHTML) */
static void html_fragment(int parent, const char *h, int len){
  int s_root = g_root, s_head = g_head, s_body = g_body, s_sp = sp, s_stack[512]; memcpy(s_stack, stack, sizeof stack);
  g_frag = 1; g_root = -3; g_head = -1; g_body = parent; sp = 0; stack[sp++] = parent;
  html_parse_loop(h, len);
  g_frag = 0; g_root = s_root; g_head = s_head; g_body = s_body; sp = s_sp; memcpy(stack, s_stack, sizeof stack);
}
#endif
