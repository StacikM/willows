/* CSS for the Willows browser: parser, selector matching, cascade, computed style. Integer arithmetic only. */
#ifndef WEB_CSS_H
#define WEB_CSS_H
#include "dom.h"

enum { D_NONE, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_LIST_ITEM, D_TABLE, D_ROW_GROUP, D_ROW, D_CELL, D_CAPTION, D_FLEX, D_INLINE_FLEX, D_COL, D_CONTENTS, D_GRID };
enum { POS_STATIC, POS_RELATIVE, POS_ABSOLUTE, POS_FIXED };
enum { FL_NONE, FL_LEFT, FL_RIGHT };
enum { TA_LEFT, TA_CENTER, TA_RIGHT, TA_JUSTIFY };
enum { WS_NORMAL, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE };
enum { LT_NONE, LT_DISC, LT_CIRCLE, LT_SQUARE, LT_DECIMAL, LT_LOWER_ALPHA, LT_UPPER_ALPHA, LT_LOWER_ROMAN, LT_UPPER_ROMAN };
enum { VA_BASELINE, VA_TOP, VA_MIDDLE, VA_BOTTOM, VA_SUB, VA_SUPER };
enum { BS_NONE, BS_SOLID, BS_DASHED, BS_DOTTED };
#define COL_CURRENT 0xFE000000u
#define CV_MAX 320
struct cvars { int n; struct { const char *name, *val; short nl, vl; } e[CV_MAX]; };      /* custom properties in scope (shared by descendants) */
struct len { int v; unsigned char u; };                /* u: 0 auto/none, 1 px, 2 percent (v in 1/1000 of a percent... i.e. v/1000 %) */
struct gtrack { int t, v; };                             /* grid track: t 1 px, 2 % (milli), 3 fr (milli), 4 auto, 5 repeat(auto-fit, minmax(v px, 1fr)), 6 repeat(auto-fit, v px) */
struct style {
  unsigned char display, position, flt, clear, text_align, white_space, visibility, list_type, text_transform, underline, linethru, bold, italic, mono;
  unsigned char valign, overflow_hidden, box_sizing, flex_dir, flex_wrap, justify, align_items, align_self, hidden_all, border_collapse;
  unsigned char bstyle[4];
  int font_px, lh_mode, lh_val;                         /* lh_mode: 0 normal, 1 multiplier (lh_val/1000), 2 px */
  unsigned color, bg;                                   /* 0xAARRGGBB; alpha 0 = transparent */
  struct len margin[4], padding[4], width, height, minw, maxw, minh, maxh, flex_basis;   /* margin/padding: top,right,bottom,left */
  int border_w[4]; unsigned border_c[4];
  int flex_grow, flex_shrink, gap_x, gap_y, spacing, text_indent, opacity;
  struct cvars *cv;
  short radius;                                         /* border-radius in px (first value; percentages become large) */
  unsigned char ncols; int grid_span; struct gtrack cols[10];   /* grid-template-columns, grid-column span */
  unsigned eff_bg;                                      /* opaque colour behind this element's children (for alpha colours) */
};

/* ---------- number / colour helpers ---------- */
static int css_ws(int c){ return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int parse_milli(const char **pp, const char *end, int *out){     /* optional sign, digits, '.', digits -> value*1000 */
  const char *p = *pp; int neg = 0, v = 0, frac = 0, fd = 0, any = 0;
  if(p < end && (*p == '-' || *p == '+')){ neg = *p == '-'; p++; }
  while(p < end && isdigit((unsigned char)*p)){ if(v < 2000000) v = v * 10 + (*p - '0'); p++; any = 1; }
  if(p < end && *p == '.'){ p++; while(p < end && isdigit((unsigned char)*p)){ if(fd < 3){ frac = frac * 10 + (*p - '0'); fd++; } p++; any = 1; } }
  if(!any) return 0;
  while(fd < 3){ frac *= 10; fd++; }
  v = v * 1000 + frac; *out = neg ? -v : v; *pp = p; return 1;
}
static const struct { const char *n; unsigned c; } NAMED[] = {
  {"black",0x000000},{"white",0xFFFFFF},{"red",0xFF0000},{"green",0x008000},{"blue",0x0000FF},{"yellow",0xFFFF00},{"orange",0xFFA500},{"purple",0x800080},{"gray",0x808080},{"grey",0x808080},
  {"silver",0xC0C0C0},{"maroon",0x800000},{"olive",0x808000},{"lime",0x00FF00},{"aqua",0x00FFFF},{"cyan",0x00FFFF},{"teal",0x008080},{"navy",0x000080},{"fuchsia",0xFF00FF},{"magenta",0xFF00FF},
  {"pink",0xFFC0CB},{"brown",0xA52A2A},{"gold",0xFFD700},{"indigo",0x4B0082},{"violet",0xEE82EE},{"coral",0xFF7F50},{"crimson",0xDC143C},{"salmon",0xFA8072},{"khaki",0xF0E68C},{"tan",0xD2B48C},
  {"beige",0xF5F5DC},{"ivory",0xFFFFF0},{"lavender",0xE6E6FA},{"turquoise",0x40E0D0},{"tomato",0xFF6347},{"orchid",0xDA70D6},{"plum",0xDDA0DD},{"sienna",0xA0522D},{"chocolate",0xD2691E},
  {"darkgray",0xA9A9A9},{"darkgrey",0xA9A9A9},{"lightgray",0xD3D3D3},{"lightgrey",0xD3D3D3},{"dimgray",0x696969},{"dimgrey",0x696969},{"gainsboro",0xDCDCDC},{"whitesmoke",0xF5F5F5},
  {"darkblue",0x00008B},{"darkgreen",0x006400},{"darkred",0x8B0000},{"lightblue",0xADD8E6},{"lightgreen",0x90EE90},{"skyblue",0x87CEEB},{"steelblue",0x4682B4},{"royalblue",0x4169E1},
  {"dodgerblue",0x1E90FF},{"slategray",0x708090},{"slategrey",0x708090},{"firebrick",0xB22222},{"forestgreen",0x228B22},{"seagreen",0x2E8B57},{"limegreen",0x32CD32},{"hotpink",0xFF69B4},
  {"deeppink",0xFF1493},{"darkorange",0xFF8C00},{"goldenrod",0xDAA520},{"rebeccapurple",0x663399},{"mediumblue",0x0000CD},{"midnightblue",0x191970},{"cornflowerblue",0x6495ED},
  {"aliceblue",0xF0F8FF},{"antiquewhite",0xFAEBD7},{"azure",0xF0FFFF},{"snow",0xFFFAFA},{"honeydew",0xF0FFF0},{"mintcream",0xF5FFFA},{"linen",0xFAF0E6},{"oldlace",0xFDF5E6},
  {"seashell",0xFFF5EE},{"lightyellow",0xFFFFE0},{"lightcyan",0xE0FFFF},{"lightpink",0xFFB6C1},{"lightsalmon",0xFFA07A},{"darkviolet",0x9400D3},{"darkcyan",0x008B8B},{"darkslategray",0x2F4F4F},
  {"cadetblue",0x5F9EA0},{"darkkhaki",0xBDB76B},{"peru",0xCD853F},{"wheat",0xF5DEB3},{"thistle",0xD8BFD8},{"mediumpurple",0x9370DB},{"slateblue",0x6A5ACD},{"deepskyblue",0x00BFFF},
};
static unsigned part_alpha(unsigned r, unsigned g, unsigned b, unsigned a){ if(a >= 254) return 0xFF000000u | (r << 16) | (g << 8) | b; return (a << 24) | (r << 16) | (g << 8) | b; }   /* alpha 1..253: kept until the backdrop is known */
static unsigned flat_over(unsigned c, unsigned base){ unsigned a = c >> 24; if(a == 0 || a >= 254) return c; unsigned r = (((c >> 16) & 255) * a + ((base >> 16) & 255) * (255 - a)) / 255, g = (((c >> 8) & 255) * a + ((base >> 8) & 255) * (255 - a)) / 255, b = ((c & 255) * a + (base & 255) * (255 - a)) / 255; return 0xFF000000u | (r << 16) | (g << 8) | b; }
static unsigned blend_white(unsigned r, unsigned g, unsigned b, unsigned a){ r = (r * a + 255 * (255 - a)) / 255; g = (g * a + 255 * (255 - a)) / 255; b = (b * a + 255 * (255 - a)) / 255; return 0xFF000000u | (r << 16) | (g << 8) | b; }
static int hex1(int c){ return isdigit(c) ? c - '0' : (tolower(c) >= 'a' && tolower(c) <= 'f') ? tolower(c) - 'a' + 10 : -1; }
static int parse_color(const char *s, int n, unsigned *out){
  while(n > 0 && css_ws(*s)){ s++; n--; } while(n > 0 && css_ws(s[n - 1])) n--;
  if(n <= 0) return 0;
  if(s[0] == '#'){
    int h[8]; int k = n - 1; if(k != 3 && k != 4 && k != 6 && k != 8) return 0;
    for(int i = 0; i < k; i++){ h[i] = hex1((unsigned char)s[1 + i]); if(h[i] < 0) return 0; }
    unsigned r, g, b, a = 255;
    if(k <= 4){ r = h[0] * 17; g = h[1] * 17; b = h[2] * 17; if(k == 4) a = h[3] * 17; } else { r = h[0] * 16 + h[1]; g = h[2] * 16 + h[3]; b = h[4] * 16 + h[5]; if(k == 8) a = h[6] * 16 + h[7]; }
    *out = a == 255 ? (0xFF000000u | (r << 16) | (g << 8) | b) : (a == 0 ? 0 : part_alpha(r, g, b, a)); return 1;
  }
  if(n > 4 && !strncasecmp_(s, "rgb", 3)){
    const char *p = s + 3; const char *end = s + n; if(p < end && (*p == 'a' || *p == 'A')) p++;
    if(p >= end || *p != '(') return 0; p++;
    int v[4] = {0, 0, 0, 1000}; int nv = 0;
    while(p < end && nv < 4){
      while(p < end && (css_ws(*p) || *p == ',' || *p == '/')) p++;
      if(p >= end || *p == ')') break;
      int m; if(!parse_milli(&p, end, &m)) return 0;
      if(p < end && *p == '%'){ p++; if(nv < 3) m = m * 255 / 100; else m = m / 100 * 10; }
      else if(nv == 3){ /* alpha 0..1 */ }
      v[nv++] = m;
    }
    if(nv < 3) return 0;
    for(int i = 0; i < 3; i++){ v[i] = (v[i] + 500) / 1000; if(v[i] < 0) v[i] = 0; if(v[i] > 255) v[i] = 255; }
    int a = nv == 4 ? v[3] : 1000; if(a < 0) a = 0; if(a > 1000) a = 1000; a = a * 255 / 1000;
    *out = a >= 255 ? (0xFF000000u | (v[0] << 16) | (v[1] << 8) | v[2]) : (a == 0 ? 0 : part_alpha(v[0], v[1], v[2], a)); return 1;
  }
  if(n > 4 && !strncasecmp_(s, "hsl", 3)){
    const char *p = s + 3; const char *end = s + n; if(p < end && (*p == 'a' || *p == 'A')) p++;
    if(p >= end || *p != '(') return 0; p++;
    int v[4] = {0, 0, 0, 1000}; int nv = 0;
    while(p < end && nv < 4){
      while(p < end && (css_ws(*p) || *p == ',' || *p == '/')) p++;
      if(p >= end || *p == ')') break;
      int m; if(!parse_milli(&p, end, &m)) return 0;
      if(p < end && *p == '%'){ p++; if(nv == 3) m /= 100; } else if(p + 3 <= end && !strncasecmp_(p, "deg", 3)) p += 3;
      v[nv++] = m;
    }
    if(nv < 3) return 0;
    int hh = ((v[0] / 1000) % 360 + 360) % 360, ss = v[1] / 10, ll = v[2] / 10;       /* s,l in 0..100 (x10 -> 0..1000) */
    if(ss > 1000) ss = 1000; if(ss < 0) ss = 0; if(ll > 1000) ll = 1000; if(ll < 0) ll = 0;
    int c = (1000 - (2 * ll > 1000 ? 2 * ll - 1000 : 1000 - 2 * ll)) * ss / 1000;       /* chroma x1000 */
    int hp = hh * 1000 / 60; int xx = c * (1000 - ((hp % 2000) > 1000 ? (hp % 2000) - 1000 : 1000 - (hp % 2000))) / 1000;
    int r1 = 0, g1 = 0, b1 = 0; int seg = hp / 1000;
    switch(seg){ case 0: r1 = c; g1 = xx; break; case 1: r1 = xx; g1 = c; break; case 2: g1 = c; b1 = xx; break; case 3: g1 = xx; b1 = c; break; case 4: r1 = xx; b1 = c; break; default: r1 = c; b1 = xx; }
    int m = ll - c / 2; int R = (r1 + m) * 255 / 1000, G = (g1 + m) * 255 / 1000, B = (b1 + m) * 255 / 1000;
    if(R < 0) R = 0; if(R > 255) R = 255; if(G < 0) G = 0; if(G > 255) G = 255; if(B < 0) B = 0; if(B > 255) B = 255;
    int a = nv == 4 ? v[3] * 255 / 1000 : 255; if(a > 255) a = 255; if(a < 0) a = 0;
    *out = a >= 255 ? (0xFF000000u | (R << 16) | (G << 8) | B) : (a == 0 ? 0 : part_alpha(R, G, B, a)); return 1;
  }
  if(n == 11 && !strncasecmp_(s, "transparent", 11)){ *out = 0; return 1; }
  if(n == 12 && !strncasecmp_(s, "currentcolor", 12)){ *out = COL_CURRENT; return 1; }
  for(unsigned i = 0; i < sizeof NAMED / sizeof *NAMED; i++) if((int)strlen(NAMED[i].n) == n && !strncasecmp_(s, NAMED[i].n, n)){ *out = 0xFF000000u | NAMED[i].c; return 1; }
  return 0;
}

/* ---------- properties ---------- */
#define PROPS(X) X(display) X(position) X(float) X(clear) X(color) X(background) X(background_color) X(font) X(font_size) X(font_weight) X(font_style) X(font_family) X(line_height) \
  X(text_align) X(text_decoration) X(text_decoration_line) X(text_transform) X(white_space) X(visibility) X(opacity) X(overflow) X(overflow_x) X(overflow_y) X(margin) X(margin_top) X(margin_right) \
  X(margin_bottom) X(margin_left) X(padding) X(padding_top) X(padding_right) X(padding_bottom) X(padding_left) X(border) X(border_width) X(border_style) X(border_color) X(border_top) \
  X(border_right) X(border_bottom) X(border_left) X(border_top_width) X(border_right_width) X(border_bottom_width) X(border_left_width) X(border_top_color) X(border_right_color) \
  X(border_bottom_color) X(border_left_color) X(border_top_style) X(border_right_style) X(border_bottom_style) X(border_left_style) X(width) X(height) X(min_width) X(max_width) X(min_height) \
  X(max_height) X(box_sizing) X(list_style) X(list_style_type) X(vertical_align) X(flex) X(flex_grow) X(flex_shrink) X(flex_basis) X(flex_direction) X(flex_wrap) X(flex_flow) X(justify_content) \
  X(align_items) X(align_self) X(gap) X(column_gap) X(row_gap) X(border_collapse) X(border_spacing) X(text_indent) X(inset) X(top) X(left) X(right) X(bottom) X(grid_template_columns) X(grid_column) X(border_radius)
enum { P_NONE,
#define X(n) P_##n,
  PROPS(X)
#undef X
  P_COUNT, P_CUSTOM };
static const char *const prop_names[] = { "",
#define X(n) #n,
  PROPS(X)
#undef X
};
static int prop_id(const char *s, int n){
  char b[40]; if(n >= 40 || n <= 0) return P_NONE;
  for(int i = 0; i < n; i++) b[i] = s[i] == '-' ? '_' : (char)tolower((unsigned char)s[i]); b[n] = 0;
  if(b[0] == '_' && b[1] == '_') return P_CUSTOM;
  for(int i = 1; i < P_COUNT; i++) if(!strcmp(prop_names[i], b)) return i;
  return P_NONE;
}

/* ---------- rules ---------- */
struct decl { short prop; unsigned char imp; short vlen; const char *val; const char *cname; short cnlen; };
struct simple {
  short tag; const char *id; const char *cls[6]; unsigned char ncls;
  struct { const char *name, *val; unsigned char op; short nlen, vlen; } at[4]; unsigned char nat;
  unsigned short pseudo; short nth_a, nth_b; struct simple *not_; char comb;
};
enum { PS_FIRST = 1, PS_LAST = 2, PS_ONLY = 4, PS_ROOT = 8, PS_EMPTY = 16, PS_LINK = 32, PS_NEVER = 64, PS_NTH = 128, PS_NTHLAST = 256, PS_FIRSTT = 512, PS_LASTT = 1024, PS_CHECKED = 2048 };
struct rule { struct simple *parts; short nparts; int spec, order; unsigned char origin; struct decl *d; short nd; int next; };
static struct rule *rules; static int nrules, rcap; static int bucket[1025]; static int rule_order;
static const char *custom_name[256]; static short custom_nlen[256]; static const char *custom_val[256]; static short custom_vlen[256]; static int ncustom;
static int g_view_w = 1024, g_view_h = 700;

static unsigned hash_s(const char *s, int n){ unsigned h = 2166136261u; for(int i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u; return h; }
static int bk_id(const char *s){ return 1 + (int)((hash_s(s, (int)strlen(s)) * 3 + 1) % 1024); }
static int bk_cls(const char *s){ return 1 + (int)((hash_s(s, (int)strlen(s)) * 5 + 7) % 1024); }
static int bk_tag(int t){ return 1 + (int)(((unsigned)t * 2654435761u >> 7) % 1024); }

/* skip a balanced () [] {} or string starting at p; returns pointer after it */
static const char *skip_block(const char *p, const char *end){
  char open = *p, close = open == '(' ? ')' : open == '[' ? ']' : '}';
  if(open == '"' || open == '\''){ p++; while(p < end && *p != open){ if(*p == '\\' && p + 1 < end) p++; p++; } return p < end ? p + 1 : end; }
  int depth = 0;
  for(; p < end; p++){
    if(*p == '"' || *p == '\''){ p = skip_block(p, end) - 1; continue; }
    if(*p == '/' && p + 1 < end && p[1] == '*'){ const char *e = p + 2; while(e + 1 < end && !(e[0] == '*' && e[1] == '/')) e++; p = e + 1; continue; }
    if(*p == open) depth++; else if(*p == close){ if(--depth == 0) return p + 1; }
  }
  return end;
}
static const char *skip_ws_comments(const char *p, const char *end){
  for(;;){
    while(p < end && css_ws(*p)) p++;
    if(p + 1 < end && p[0] == '/' && p[1] == '*'){ p += 2; while(p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++; p = p + 2 < end ? p + 2 : end; continue; }
    if(p + 3 < end && !strncmp(p, "<!--", 4)){ p += 4; continue; }
    if(p + 2 < end && !strncmp(p, "-->", 3)){ p += 3; continue; }
    return p;
  }
}

/* ---------- selector parsing ---------- */
static const char *ident_end(const char *p, const char *end){
  while(p < end && (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || (unsigned char)*p >= 0x80 || *p == '\\')){ if(*p == '\\' && p + 1 < end) p++; p++; }
  return p;
}
static int parse_nth(const char *s, int n, short *a, short *b){
  char t[24]; int k = 0; for(int i = 0; i < n && k < 22; i++) if(!css_ws(s[i])) t[k++] = (char)tolower((unsigned char)s[i]); t[k] = 0;
  if(!strcmp(t, "odd")){ *a = 2; *b = 1; return 1; } if(!strcmp(t, "even")){ *a = 2; *b = 0; return 1; }
  char *np = strchr(t, 'n');
  if(!np){ *a = 0; *b = (short)atoi(t); return 1; }
  *np = 0; if(t[0] == 0 || !strcmp(t, "+")) *a = 1; else if(!strcmp(t, "-")) *a = -1; else *a = (short)atoi(t);
  *b = np[1] ? (short)atoi(np + 1) : 0; return 1;
}
/* parses one compound selector at p; returns pointer after it, 0 on unsupported */
static const char *parse_compound(const char *p, const char *end, struct simple *s){
  memset(s, 0, sizeof *s); int any = 0;
  for(;;){
    if(p >= end) break;
    char c = *p;
    if(c == '*'){ p++; any = 1; }
    else if(isalpha((unsigned char)c) || c == '_' || c == '\\' || (unsigned char)c >= 0x80){ const char *e = ident_end(p, end); if(e == p) return 0; s->tag = (short)tag_id(p, (int)(e - p)); p = e; any = 1; }
    else if(c == '#'){ const char *e = ident_end(p + 1, end); if(e == p + 1) return 0; s->id = a_str(p + 1, (int)(e - p - 1)); p = e; any = 1; }
    else if(c == '.'){ const char *e = ident_end(p + 1, end); if(e == p + 1) return 0; if(s->ncls < 6) s->cls[s->ncls++] = a_str(p + 1, (int)(e - p - 1)); p = e; any = 1; }
    else if(c == '['){
      const char *e = skip_block(p, end); const char *in = p + 1, *ie = e - 1; if(ie < in) return 0;
      while(in < ie && css_ws(*in)) in++;
      const char *ne = in; while(ne < ie && !css_ws(*ne) && *ne != '=' && *ne != '~' && *ne != '|' && *ne != '^' && *ne != '$' && *ne != '*' && *ne != ']') ne++;
      if(s->nat < 4){
        int k = s->nat++; s->at[k].name = a_str(in, (int)(ne - in)); s->at[k].nlen = (short)(ne - in); s->at[k].op = 0; s->at[k].val = ""; s->at[k].vlen = 0;
        while(ne < ie && css_ws(*ne)) ne++;
        if(ne < ie){
          int op = 1; if(*ne == '~'){ op = 2; ne++; } else if(*ne == '|'){ op = 3; ne++; } else if(*ne == '^'){ op = 4; ne++; } else if(*ne == '$'){ op = 5; ne++; } else if(*ne == '*'){ op = 6; ne++; }
          if(ne < ie && *ne == '='){
            ne++; while(ne < ie && css_ws(*ne)) ne++;
            const char *vs = ne; const char *ve;
            if(ne < ie && (*ne == '"' || *ne == '\'')){ char q = *ne; vs = ne + 1; ve = vs; while(ve < ie && *ve != q) ve++; }
            else { ve = vs; while(ve < ie && !css_ws(*ve)) ve++; }
            s->at[k].op = (unsigned char)op; s->at[k].val = a_str(vs, (int)(ve - vs)); s->at[k].vlen = (short)(ve - vs);
          } else return 0;
        }
      }
      p = e; any = 1;
    }
    else if(c == ':'){
      int dbl = p + 1 < end && p[1] == ':';
      const char *ns = p + 1 + dbl; const char *ne = ident_end(ns, end); int nl = (int)(ne - ns);
      if(dbl){ s->pseudo |= PS_NEVER; p = ne; if(p < end && *p == '(') p = skip_block(p, end); any = 1; continue; }        /* ::before etc: never matches */
      const char *arg = 0; int arglen = 0; const char *after = ne;
      if(ne < end && *ne == '('){ const char *e = skip_block(ne, end); arg = ne + 1; arglen = (int)(e - 1 - arg); after = e; }
      #define IS(x) (nl == (int)strlen(x) && !strncasecmp_(ns, x, nl))
      if(IS("first-child")) s->pseudo |= PS_FIRST; else if(IS("last-child")) s->pseudo |= PS_LAST; else if(IS("only-child")) s->pseudo |= PS_ONLY;
      else if(IS("root")) s->pseudo |= PS_ROOT; else if(IS("empty")) s->pseudo |= PS_EMPTY; else if(IS("link") || IS("any-link")) s->pseudo |= PS_LINK;
      else if(IS("first-of-type")) s->pseudo |= PS_FIRSTT; else if(IS("last-of-type")) s->pseudo |= PS_LASTT; else if(IS("checked")) s->pseudo |= PS_CHECKED;
      else if(IS("nth-child") || IS("nth-of-type")){ if(!arg || !parse_nth(arg, arglen, &s->nth_a, &s->nth_b)) return 0; s->pseudo |= PS_NTH; }
      else if(IS("nth-last-child") || IS("nth-last-of-type")){ if(!arg || !parse_nth(arg, arglen, &s->nth_a, &s->nth_b)) return 0; s->pseudo |= PS_NTHLAST; }
      else if(IS("not")){ if(!arg) return 0; struct simple *in = a_alloc(sizeof *in); if(!in) return 0; const char *q = arg; while(q < arg + arglen && css_ws(*q)) q++;
        const char *r = parse_compound(q, arg + arglen, in); if(!r) return 0; s->not_ = in; }
      else if(IS("is") || IS("where")){ if(!arg) return 0; struct simple *in = a_alloc(sizeof *in); if(!in) return 0; const char *q = arg; while(q < arg + arglen && css_ws(*q)) q++;
        const char *r = parse_compound(q, arg + arglen, in); if(!r) return 0; /* only a single compound inside :is()/:where() is supported: merge its constraints into this compound */
        if(in->tag && !s->tag) s->tag = in->tag; if(in->id && !s->id) s->id = in->id;
        for(int q = 0; q < in->ncls && s->ncls < 6; q++) s->cls[s->ncls++] = in->cls[q];
        for(int q = 0; q < in->nat && s->nat < 4; q++){ s->at[s->nat++] = in->at[q]; }
        s->pseudo |= in->pseudo; if(in->not_ && !s->not_) s->not_ = in->not_; if(in->pseudo & (PS_NTH | PS_NTHLAST)){ s->nth_a = in->nth_a; s->nth_b = in->nth_b; } }
      else s->pseudo |= PS_NEVER;                     /* :hover :focus :active :visited ... : dynamic states never apply */
      #undef IS
      p = after; any = 1;
    }
    else break;
  }
  return any ? p : 0;
}
static int bits_set(unsigned v){ int c = 0; while(v){ c += v & 1; v >>= 1; } return c; }
static int spec_of(struct simple *parts, int n){
  int ids = 0, cls = 0, tags = 0;
  for(int i = 0; i < n; i++){ struct simple *s = &parts[i]; if(s->id) ids++; cls += s->ncls + s->nat + bits_set(s->pseudo & ~PS_NEVER) + (s->not_ ? 1 : 0); if(s->tag) tags++; }
  return ids * 10000 + cls * 100 + tags;
}
static int add_rule(struct simple *parts, int np, struct decl *d, int nd, int origin){
  if(nrules >= rcap){ int nc = rcap ? rcap * 2 : 1024; struct rule *p = realloc(rules, (size_t)nc * sizeof *p); if(!p) return 0; rules = p; rcap = nc; }
  struct rule *r = &rules[nrules]; r->parts = parts; r->nparts = (short)np; r->spec = spec_of(parts, np); r->order = rule_order++; r->origin = (unsigned char)origin; r->d = d; r->nd = (short)nd;
  struct simple *last = &parts[np - 1]; int key = 0;
  if(last->id) key = bk_id(last->id); else if(last->ncls) key = bk_cls(last->cls[0]); else if(last->tag) key = bk_tag(last->tag);
  r->next = bucket[key]; bucket[key] = nrules + 1; nrules++; return 1;
}
/* parse a comma separated selector list; each becomes a rule sharing the declarations */
static void parse_selector_list(const char *p, const char *end, struct decl *d, int nd, int origin){
  while(p < end){
    const char *q = p; int depth = 0;                                   /* find the end of this selector (top-level comma) */
    while(q < end && !(depth == 0 && *q == ',')){ if(*q == '(' || *q == '[') depth++; else if(*q == ')' || *q == ']') depth--; else if(*q == '"' || *q == '\''){ q = skip_block(q, end); continue; } q++; }
    const char *se = q; const char *s = p; p = q < end ? q + 1 : end;
    struct simple tmp[12]; int np = 0; char comb = 0; int bad = 0;
    while(s < se){
      while(s < se && css_ws(*s)) s++;
      if(s >= se) break;
      if(*s == '>' || *s == '+' || *s == '~'){ comb = *s++; continue; }
      if(np >= 12){ bad = 1; break; }
      const char *e = parse_compound(s, se, &tmp[np]);
      if(!e){ bad = 1; break; }
      tmp[np].comb = np ? (comb ? comb : ' ') : 0; comb = 0; np++; s = e;
      if(s < se && !css_ws(*s) && *s != '>' && *s != '+' && *s != '~'){ bad = 1; break; }
    }
    if(bad || !np) continue;
    struct simple *parts = a_alloc(np * (int)sizeof(struct simple)); if(!parts) return; memcpy(parts, tmp, np * sizeof(struct simple));
    if(!add_rule(parts, np, d, nd, origin)) return;
  }
}

/* ---------- declarations ---------- */
static int parse_decls(const char *p, const char *end, struct decl *out, int max){
  int n = 0;
  while(p < end){
    p = skip_ws_comments(p, end); if(p >= end) break;
    const char *ne = p; while(ne < end && *ne != ':' && *ne != ';' && *ne != '{' && *ne != '}') ne++;
    if(ne >= end || *ne != ':'){                                          /* junk or a nested rule: skip it */
      if(ne < end && *ne == '{'){ p = skip_block(ne, end); continue; }
      p = ne < end ? ne + 1 : end; continue;
    }
    const char *nm = p; int nl = (int)(ne - p); while(nl > 0 && css_ws(nm[nl - 1])) nl--;
    const char *vs = ne + 1; const char *ve = vs; int depth = 0;
    while(ve < end && !(depth == 0 && *ve == ';')){ if(*ve == '(') depth++; else if(*ve == ')') depth--; else if(*ve == '"' || *ve == '\''){ ve = skip_block(ve, end); continue; } else if(*ve == '{' && depth == 0){ ve = skip_block(ve, end); break; } ve++; }
    p = ve < end ? ve + 1 : end;
    if(ve > vs && ve[-1] == '}') { /* nested rule swallowed as value: drop */ continue; }
    while(vs < ve && css_ws(*vs)) vs++; while(ve > vs && css_ws(ve[-1])) ve--;
    int imp = 0;
    if(ve - vs >= 10){ const char *t = ve - 10; if(!strncasecmp_(t, "!important", 10)){ imp = 1; ve = t; while(ve > vs && css_ws(ve[-1])) ve--; } }
    else if(ve - vs > 9 && ve[-1] == 't') {}
    { const char *t = ve; int k = 0; while(t > vs && css_ws(t[-1])) t--; (void)k; (void)t; }
    int pid = prop_id(nm, nl);
    if(pid == P_NONE || n >= max) continue;
    struct decl *d = &out[n++]; d->prop = (short)pid; d->imp = (unsigned char)imp; d->val = vs; d->vlen = (short)(ve - vs); d->cname = 0; d->cnlen = 0;
    if(pid == P_CUSTOM){ d->cname = nm; d->cnlen = (short)nl; }
  }
  return n;
}
/* ---------- @media ---------- */
static int media_true(const char *s, const char *end){
  /* comma separated queries; true if any is true */
  const char *p = s;
  while(p < end){
    const char *q = p; while(q < end && *q != ',') { if(*q == '(') q = skip_block(q, end); else q++; }
    const char *qe = q; int ok = 1, neg = 0; const char *t = p;
    while(t < qe){
      while(t < qe && css_ws(*t)) t++; if(t >= qe) break;
      if(*t == '('){
        const char *e = skip_block(t, qe); const char *in = t + 1, *ie = e - 1; t = e;
        const char *c = in; while(c < ie && *c != ':') c++;
        if(c >= ie){ ok = 0; continue; }                                  /* boolean features (hover, ...) : false */
        const char *nm = in; int nl = (int)(c - in); while(nl > 0 && css_ws(nm[nl - 1])) nl--; while(nl > 0 && css_ws(*nm)){ nm++; nl--; }
        const char *vs = c + 1; while(vs < ie && css_ws(*vs)) vs++;
        int m = 0; const char *vp = vs; int isw = (nl == 9 && !strncasecmp_(nm, "min-width", 9)) || (nl == 9 && !strncasecmp_(nm, "max-width", 9)) || (nl == 10 && !strncasecmp_(nm, "min-height", 10)) || (nl == 10 && !strncasecmp_(nm, "max-height", 10));
        if(isw && parse_milli(&vp, ie, &m)){
          int px = m; if(vp + 2 <= ie && !strncasecmp_(vp, "em", 2)) px = m * 16; else if(vp + 3 <= ie && !strncasecmp_(vp, "rem", 3)) px = m * 16;
          px /= 1000; int lim = (nm[4] == 'w' || nm[5] == 'w') ? g_view_w : g_view_h; int isw2 = nm[4] == 'w';
          if(nm[1] == 'i'){ if(!(isw2 ? g_view_w >= px : g_view_h >= px)) ok = 0; } else { if(!(isw2 ? g_view_w <= px : g_view_h <= px)) ok = 0; } (void)lim;
        } else if(nl == 6 && !strncasecmp_(nm, "orient", 6)){ ok = ok && 1; }
        else ok = 0;
      } else {
        const char *e = t; while(e < qe && !css_ws(*e) && *e != '(') e++; int wl = (int)(e - t);
        if(wl == 3 && !strncasecmp_(t, "not", 3)) neg = 1;
        else if((wl == 5 && !strncasecmp_(t, "print", 5)) || (wl == 6 && !strncasecmp_(t, "speech", 6))) ok = 0;
        t = e;
      }
    }
    if(neg) ok = !ok; if(ok) return 1;
    p = q < end ? q + 1 : end;
  }
  return 0;
}
static void parse_sheet(const char *p, const char *end, int origin);
static void parse_block_rules(const char *p, const char *end, int origin){ parse_sheet(p, end, origin); }
static void parse_sheet(const char *p, const char *end, int origin){
  while(p < end){
    p = skip_ws_comments(p, end); if(p >= end) break;
    if(*p == '}'){ p++; continue; }
    if(*p == '@'){
      const char *ns = p + 1; const char *ne = ident_end(ns, end); int nl = (int)(ne - ns);
      const char *q = ne; while(q < end && *q != '{' && *q != ';'){ if(*q == '(' || *q == '"' || *q == '\'') q = skip_block(q, end); else q++; }
      if(q >= end) break;
      if(*q == ';'){ p = q + 1; continue; }
      const char *be = skip_block(q, end);
      if(nl == 5 && !strncasecmp_(ns, "media", 5)){ if(media_true(ne, q)) parse_block_rules(q + 1, be - 1, origin); }
      else if(nl == 5 && !strncasecmp_(ns, "layer", 5)) parse_block_rules(q + 1, be - 1, origin);
      p = be; continue;                                                    /* @font-face @keyframes @supports @container ... skipped */
    }
    const char *q = p; while(q < end && *q != '{'){ if(*q == '(' || *q == '[' || *q == '"' || *q == '\'') q = skip_block(q, end); else q++; }
    if(q >= end) break;
    const char *be = skip_block(q, end);
    struct decl tmp[96]; int nd = parse_decls(q + 1, be - 1 > q + 1 ? be - 1 : q + 1, tmp, 96);
    struct decl *d = nd ? a_alloc(nd * (int)sizeof(struct decl)) : 0; if(nd && d) memcpy(d, tmp, nd * sizeof(struct decl));
    if(nd) parse_selector_list(p, q, d, nd, origin);
    p = be;
  }
}
static void css_reset(void){ nrules = 0; memset(bucket, 0, sizeof bucket); rule_order = 0; }

/* ---------- matching ---------- */
static int elem_prev(int n){ if(N(n).parent < 0) return -1; int p = N(N(n).parent).first, prev = -1; while(p >= 0 && p != n){ if(N(p).type == 0) prev = p; p = N(p).next; } return prev; }
static int elem_next(int n){ int p = N(n).next; while(p >= 0 && N(p).type != 0) p = N(p).next; return p; }
static int class_has(const char *cl, const char *c){
  int cn = (int)strlen(c);
  for(const char *p = cl; *p; ){ while(*p && css_ws(*p)) p++; const char *e = p; while(*e && !css_ws(*e)) e++; if((int)(e - p) == cn && !strncmp(p, c, cn)) return 1; p = e; }
  return 0;
}
static int match_simple(int n, struct simple *s){
  if(N(n).type != 0) return 0;
  if(s->pseudo & PS_NEVER) return 0;
  if(s->tag && N(n).tag != s->tag) return 0;
  if(s->id){ const char *v = attr_of(n, "id"); if(!v || strcmp(v, s->id)) return 0; }
  if(s->ncls){ const char *v = attr_of(n, "class"); if(!v) return 0; for(int i = 0; i < s->ncls; i++) if(!class_has(v, s->cls[i])) return 0; }
  for(int i = 0; i < s->nat; i++){
    const char *v = 0; for(int k = 0; k < N(n).nattr; k++) if(!strcasecmp_(N(n).attrs[k].name, s->at[i].name)) v = N(n).attrs[k].val;
    if(!v) return 0;
    int vl = (int)strlen(v), al = s->at[i].vlen; const char *a = s->at[i].val;
    switch(s->at[i].op){
      case 1: if(vl != al || strncmp(v, a, al)) return 0; break;
      case 2: if(!class_has(v, a)) return 0; break;
      case 3: if(!(vl == al && !strncmp(v, a, al)) && !(vl > al && !strncmp(v, a, al) && v[al] == '-')) return 0; break;
      case 4: if(vl < al || strncmp(v, a, al)) return 0; break;
      case 5: if(vl < al || strncmp(v + vl - al, a, al)) return 0; break;
      case 6: if(!al || !strstr(v, a)) return 0; break;
    }
  }
  if(s->pseudo){
    int par = N(n).parent;
    if((s->pseudo & PS_ROOT) && par >= 0) return 0;
    if((s->pseudo & PS_FIRST) && elem_prev(n) >= 0) return 0;
    if((s->pseudo & PS_LAST) && elem_next(n) >= 0) return 0;
    if((s->pseudo & PS_ONLY) && (elem_prev(n) >= 0 || elem_next(n) >= 0)) return 0;
    if((s->pseudo & PS_EMPTY) && N(n).first >= 0) return 0;
    if((s->pseudo & PS_LINK) && !(N(n).tag == TAG_a && attr_of(n, "href"))) return 0;
    if((s->pseudo & PS_CHECKED) && !attr_of(n, "checked")) return 0;
    if(s->pseudo & (PS_FIRSTT | PS_LASTT)){
      int p = N(par).first; int cnt_before = 0, cnt_after = 0; int seen = 0;
      for(; p >= 0; p = N(p).next){ if(p == n){ seen = 1; continue; } if(N(p).type == 0 && N(p).tag == N(n).tag){ if(seen) cnt_after++; else cnt_before++; } }
      if((s->pseudo & PS_FIRSTT) && cnt_before) return 0; if((s->pseudo & PS_LASTT) && cnt_after) return 0;
    }
    if(s->pseudo & (PS_NTH | PS_NTHLAST)){
      int idx = 1; if(par >= 0){ int p = N(par).first; int before = 0, after = 0, seen = 0; for(; p >= 0; p = N(p).next){ if(p == n){ seen = 1; continue; } if(N(p).type == 0){ if(seen) after++; else before++; } } idx = (s->pseudo & PS_NTH) ? before + 1 : after + 1; }
      int a = s->nth_a, b = s->nth_b;
      if(a == 0){ if(idx != b) return 0; }
      else { int d = idx - b; if(d % a != 0 || d / a < 0) return 0; }
    }
  }
  if(s->not_ && match_simple(n, s->not_)) return 0;
  return 1;
}
static int match_from(int n, struct rule *r, int pi){
  if(!match_simple(n, &r->parts[pi])) return 0;
  if(pi == 0) return 1;
  char comb = r->parts[pi].comb;
  if(comb == '>'){ int p = N(n).parent; return p >= 0 && match_from(p, r, pi - 1); }
  if(comb == '+'){ int p = elem_prev(n); return p >= 0 && match_from(p, r, pi - 1); }
  if(comb == '~'){ for(int p = elem_prev(n); p >= 0; p = elem_prev(p)) if(match_from(p, r, pi - 1)) return 1; return 0; }
  for(int p = N(n).parent; p >= 0; p = N(p).parent) if(match_from(p, r, pi - 1)) return 1;
  return 0;
}

/* ---------- value parsing ---------- */

static int len_from(const char *s, int n, int em, struct len *out){
  while(n > 0 && css_ws(*s)){ s++; n--; } while(n > 0 && css_ws(s[n - 1])) n--;
  if(n <= 0) return 0;
  if(n == 4 && !strncasecmp_(s, "auto", 4)){ out->u = 0; out->v = 0; return 1; }
  if((n == 4 && !strncasecmp_(s, "none", 4)) || (n == 7 && !strncasecmp_(s, "initial", 7)) || (n == 5 && !strncasecmp_(s, "unset", 5))){ out->u = 0; out->v = 0; return 1; }
  if((n > 4 && !strncasecmp_(s, "min(", 4)) || (n > 4 && !strncasecmp_(s, "max(", 4)) || (n > 6 && !strncasecmp_(s, "clamp(", 6))){   /* comparison functions: each argument is itself a length */
    int kind = s[1] == 'i' || s[1] == 'I' ? 0 : s[1] == 'a' || s[1] == 'A' ? 1 : 2; const char *p = s + (kind == 2 ? 6 : 4), *end = s + n - 1;
    struct len a[3]; int na = 0; int depth = 0; const char *st0 = p;
    for(const char *q = p; q <= end && na < 3; q++){
      if(q < end && *q == '(') depth++; else if(q < end && *q == ')') depth--;
      if(q == end || (*q == ',' && depth == 0)){ if(!len_from(st0, (int)(q - st0), em, &a[na])) return 0; na++; st0 = q + 1; }
    }
    if(na < 1 || (kind == 2 && na != 3)) return 0;
    for(int i = 1; i < na; i++) if(a[i].u != a[0].u){ *out = a[0]; return 1; }                  /* px and % cannot be compared here: keep the first */
    int v;
    if(kind == 0){ v = a[0].v; for(int i = 1; i < na; i++) if(a[i].v < v) v = a[i].v; }
    else if(kind == 1){ v = a[0].v; for(int i = 1; i < na; i++) if(a[i].v > v) v = a[i].v; }
    else { v = a[1].v < a[2].v ? a[1].v : a[2].v; if(a[0].v > v) v = a[0].v; }
    out->u = a[0].u; out->v = v; return 1;
  }
  if(n > 5 && !strncasecmp_(s, "calc(", 5)){                           /* a sum of terms of a single kind (all px/em, or a single %); mixed terms are not representable */
    const char *p = s + 5, *end = s + n - 1; int sum_px = 0, sum_pc = 0, sign = 1; int havepx = 0, havepc = 0;
    while(p < end){
      while(p < end && css_ws(*p)) p++; if(p >= end) break;
      if(*p == '+'){ sign = 1; p++; continue; } if(*p == '-' && p + 1 < end && css_ws(p[1])){ sign = -1; p++; continue; }
      int m; if(!parse_milli(&p, end, &m)) return 0;
      if(p < end && *p == '%'){ p++; sum_pc += sign * m; havepc = 1; }
      else if(p + 2 <= end && !strncasecmp_(p, "px", 2)){ p += 2; sum_px += sign * m / 1000; havepx = 1; }
      else if(p + 3 <= end && !strncasecmp_(p, "rem", 3)){ p += 3; sum_px += sign * m * 16 / 1000; havepx = 1; }
      else if(p + 2 <= end && !strncasecmp_(p, "em", 2)){ p += 2; sum_px += sign * (int)((long long)m * em / 1000); havepx = 1; }
      else if(p + 2 <= end && !strncasecmp_(p, "vw", 2)){ p += 2; sum_px += sign * (int)((long long)m * g_view_w / 100000); havepx = 1; }
      else return 0;
      sign = 1;
    }
    if(havepc && havepx){ out->u = 2; out->v = sum_pc; return 1; }                         /* approximate: percentage only (the fixed part is dropped) */
    if(havepc){ out->u = 2; out->v = sum_pc; return 1; }
    out->u = 1; out->v = sum_px; return 1;
  }
  const char *p = s, *end = s + n; int m;
  if(!parse_milli(&p, end, &m)) return 0;
  int ul = (int)(end - p);
  if(ul == 0){ if(m != 0) return 0; out->u = 1; out->v = 0; return 1; }
  #define U(x) (ul == (int)sizeof(x) - 1 && !strncasecmp_(p, x, ul))
  if(U("px")){ out->u = 1; out->v = (m + (m >= 0 ? 500 : -500)) / 1000; }
  else if(U("%")){ out->u = 2; out->v = m; }
  else if(U("em")){ out->u = 1; out->v = (int)((long long)m * em / 1000); }
  else if(U("rem")){ out->u = 1; out->v = m * 16 / 1000; }
  else if(U("ex") || U("ch")){ out->u = 1; out->v = (int)((long long)m * em / 2000); }
  else if(U("pt")){ out->u = 1; out->v = m * 4 / 3000; }
  else if(U("pc")){ out->u = 1; out->v = m * 16 / 1000; }
  else if(U("cm")){ out->u = 1; out->v = (int)((long long)m * 378 / 10000); }
  else if(U("mm")){ out->u = 1; out->v = (int)((long long)m * 378 / 100000); }
  else if(U("in")){ out->u = 1; out->v = m * 96 / 1000; }
  else if(U("vw")){ out->u = 1; out->v = (int)((long long)m * g_view_w / 100000); }
  else if(U("vh")){ out->u = 1; out->v = (int)((long long)m * g_view_h / 100000); }
  else if(U("vmin")){ out->u = 1; out->v = (int)((long long)m * (g_view_w < g_view_h ? g_view_w : g_view_h) / 100000); }
  else if(U("vmax")){ out->u = 1; out->v = (int)((long long)m * (g_view_w > g_view_h ? g_view_w : g_view_h) / 100000); }
  else return 0;
  #undef U
  return 1;
}
/* split a value into whitespace separated tokens (respecting parentheses/strings) */
struct tok { const char *s; int n; };
static int tokenize(const char *s, int n, struct tok *t, int max){
  int k = 0; const char *p = s, *end = s + n;
  while(p < end && k < max){
    while(p < end && (css_ws(*p) || *p == ',')) { if(*p == ',' && k > 0 && max > 0){ } p++; }
    if(p >= end) break;
    const char *q = p;
    while(q < end && !css_ws(*q)){ if(*q == '(' || *q == '"' || *q == '\'') q = skip_block(q, end); else q++; }
    t[k].s = p; t[k].n = (int)(q - p); k++; p = q;
  }
  return k;
}
static int tok_is(struct tok *t, const char *w){ return t->n == (int)strlen(w) && !strncasecmp_(t->s, w, t->n); }
/* substitute var(--x, fallback) occurrences */
static int subst_vars(const char *s, int n, char *out, int cap, int depth, const struct cvars *cv){
  int o = 0;
  for(int i = 0; i < n && o < cap - 1; ){
    if(i + 4 < n && !strncasecmp_(s + i, "var(", 4)){
      const char *e = skip_block(s + i + 3, s + n); const char *in = s + i + 4, *ie = e - 1;
      const char *c = in; int d = 0; while(c < ie && !(d == 0 && *c == ',')){ if(*c == '(') d++; else if(*c == ')') d--; c++; }
      const char *nm = in; int nl = (int)(c - in); while(nl > 0 && css_ws(nm[nl - 1])) nl--; while(nl > 0 && css_ws(*nm)){ nm++; nl--; }
      const char *val = 0; int vl = 0;
      if(cv) for(int k = 0; k < cv->n; k++) if(cv->e[k].nl == nl && !strncmp(cv->e[k].name, nm, nl)){ val = cv->e[k].val; vl = cv->e[k].vl; }
      if(!val && c < ie){ val = c + 1; vl = (int)(ie - c - 1); }
      if(val && depth < 4){ o += subst_vars(val, vl, out + o, cap - o, depth + 1, cv); }
      i = (int)(e - s); continue;
    }
    out[o++] = s[i++];
  }
  out[o] = 0; return o;
}

/* ---------- applying declarations ---------- */
static void set_border_side(struct style *st, int side, struct tok *t, int nt, int em){
  for(int i = 0; i < nt; i++){
    unsigned c; struct len l;
    if(tok_is(&t[i], "none") || tok_is(&t[i], "hidden")) st->bstyle[side] = BS_NONE;
    else if(tok_is(&t[i], "solid") || tok_is(&t[i], "double") || tok_is(&t[i], "groove") || tok_is(&t[i], "ridge") || tok_is(&t[i], "inset") || tok_is(&t[i], "outset")) st->bstyle[side] = BS_SOLID;
    else if(tok_is(&t[i], "dashed")) st->bstyle[side] = BS_DASHED; else if(tok_is(&t[i], "dotted")) st->bstyle[side] = BS_DOTTED;
    else if(tok_is(&t[i], "thin")) st->border_w[side] = 1; else if(tok_is(&t[i], "medium")) st->border_w[side] = 3; else if(tok_is(&t[i], "thick")) st->border_w[side] = 5;
    else if(parse_color(t[i].s, t[i].n, &c)) st->border_c[side] = c;
    else if(len_from(t[i].s, t[i].n, em, &l) && l.u == 1) st->border_w[side] = l.v;
  }
}
static void set_box4(struct len *dst, struct tok *t, int nt, int em){          /* margin/padding shorthand */
  struct len v[4]; if(nt < 1) return; int ok = 0;
  for(int i = 0; i < nt && i < 4; i++){ if(!len_from(t[i].s, t[i].n, em, &v[i])) return; ok++; }
  if(ok == 1){ dst[0] = dst[1] = dst[2] = dst[3] = v[0]; }
  else if(ok == 2){ dst[0] = dst[2] = v[0]; dst[1] = dst[3] = v[1]; }
  else if(ok == 3){ dst[0] = v[0]; dst[1] = dst[3] = v[1]; dst[2] = v[2]; }
  else { dst[0] = v[0]; dst[1] = v[1]; dst[2] = v[2]; dst[3] = v[3]; }
}
static int font_size_value(const char *s, int n, int parent_px, int *out){
  while(n > 0 && css_ws(*s)){ s++; n--; } while(n > 0 && css_ws(s[n - 1])) n--;
  static const struct { const char *n; int px; } kw[] = {{"xx-small",9},{"x-small",10},{"small",13},{"medium",16},{"large",18},{"x-large",24},{"xx-large",32},{"xxx-large",40}};
  for(unsigned i = 0; i < sizeof kw / sizeof *kw; i++) if((int)strlen(kw[i].n) == n && !strncasecmp_(s, kw[i].n, n)){ *out = kw[i].px; return 1; }
  if(n == 7 && !strncasecmp_(s, "smaller", 7)){ *out = parent_px * 5 / 6; return 1; }
  if(n == 6 && !strncasecmp_(s, "larger", 6)){ *out = parent_px * 6 / 5; return 1; }
  struct len l; if(!len_from(s, n, parent_px, &l) || l.u == 0) return 0;
  if(l.u == 2) *out = (int)((long long)parent_px * l.v / 100000); else *out = l.v;
  if(*out < 1) *out = 1; return 1;
}
static int strstr_n(const char *s, int n, const char *needle);
/* grid-template-columns: fr / px / % / auto tracks, repeat(n, ...), repeat(auto-fit|auto-fill, minmax(Npx, 1fr)) */
static void add_track(struct style *st, int t, int v){ if(st->ncols < 10){ st->cols[st->ncols].t = t; st->cols[st->ncols].v = v; st->ncols++; } }
static void one_track(const char *s, int n, int em, struct style *st){
  while(n > 0 && css_ws(*s)){ s++; n--; } while(n > 0 && css_ws(s[n - 1])) n--; if(n <= 0) return;
  if(n > 7 && !strncasecmp_(s, "minmax(", 7)){ const char *c = memchr(s, ',', n); if(!c){ add_track(st, 4, 0); return; } int rest = (int)(s + n - 1 - (c + 1)); one_track(c + 1, rest, em, st); return; }   /* minmax(a, b): use b */
  int m; const char *q = s; if(parse_milli(&q, s + n, &m) && s + n - q == 2 && !strncasecmp_(q, "fr", 2)){ add_track(st, 3, m); return; }
  struct len l; if(len_from(s, n, em, &l)){ if(l.u == 1) add_track(st, 1, l.v); else if(l.u == 2) add_track(st, 2, l.v); else add_track(st, 4, 0); return; }
  add_track(st, 4, 0);
}
static void parse_tracks(const char *v, int vl, int em, struct style *st){
  st->ncols = 0; int i = 0;
  while(i < vl && st->ncols < 10){
    while(i < vl && css_ws(v[i])) i++; if(i >= vl) break;
    int j = i, depth = 0; while(j < vl && (depth || !css_ws(v[j]))){ if(v[j] == '(') depth++; else if(v[j] == ')') depth--; j++; }
    const char *t = v + i; int tn = j - i; i = j;
    if(tn > 7 && !strncasecmp_(t, "repeat(", 7)){
      const char *a = t + 7, *e = t + tn - 1; const char *c = memchr(a, ',', e - a); if(!c) continue;
      int autofit = (c - a >= 8 && (!strncasecmp_(a, "auto-fit", 8) || !strncasecmp_(a, "auto-fill", 9)));
      if(autofit){ const char *b = c + 1; int bn = (int)(e - b); while(bn > 0 && css_ws(*b)){ b++; bn--; }
        struct style tmp; tmp.ncols = 0; one_track(b, bn, em, &tmp); if(tmp.ncols){ int mpx = 0; if(bn > 7 && !strncasecmp_(b, "minmax(", 7)){ struct len l; const char *cc = memchr(b, ',', bn); if(cc && len_from(b + 7, (int)(cc - b - 7), em, &l) && l.u == 1) mpx = l.v; add_track(st, 5, mpx > 0 ? mpx : 100); } else if(tmp.cols[0].t == 1) add_track(st, 6, tmp.cols[0].v); else add_track(st, 5, 100); }
      } else { int cnt = atoi(a); if(cnt < 1) cnt = 1; if(cnt > 10) cnt = 10; struct style tmp; tmp.ncols = 0; { const char *b = c + 1; int bn = (int)(e - b); int k = 0; while(k < bn && tmp.ncols < 10){ while(k < bn && css_ws(b[k])) k++; if(k >= bn) break; int k2 = k, d2 = 0; while(k2 < bn && (d2 || !css_ws(b[k2]))){ if(b[k2] == '(') d2++; else if(b[k2] == ')') d2--; k2++; } one_track(b + k, k2 - k, em, &tmp); k = k2; } }
        for(int r = 0; r < cnt; r++) for(int k = 0; k < tmp.ncols; k++) add_track(st, tmp.cols[k].t, tmp.cols[k].v); }
      continue;
    }
    one_track(t, tn, em, st);
  }
}
static void apply_decl(struct style *st, const struct style *par, struct decl *d, int pass){
  char buf[640]; const char *v = d->val; int vl = d->vlen;
  if(vl > 4 && memchr(v, '(', vl) && strstr_n(v, vl, "var(")){ vl = subst_vars(v, vl, buf, sizeof buf, 0, st->cv); v = buf; }
  int prop = d->prop; int em = st->font_px;
  int is_font = prop == P_font_size || prop == P_font;
  if(pass == 0 && !is_font) return; if(pass == 1 && is_font) return;
  struct tok t[12]; int nt;
  if(vl == 7 && !strncasecmp_(v, "inherit", 7)){
    /* only inherit what is cheap to express: copy from parent where the property is known */
    switch(prop){
      case P_color: st->color = par->color; break; case P_font_size: st->font_px = par->font_px; break; case P_font_weight: st->bold = par->bold; break; case P_font_style: st->italic = par->italic; break;
      case P_background_color: case P_background: st->bg = par->bg; break; case P_text_align: st->text_align = par->text_align; break; case P_display: st->display = par->display; break;
      case P_width: st->width = par->width; break; case P_height: st->height = par->height; break; case P_text_decoration: case P_text_decoration_line: st->underline = par->underline; st->linethru = par->linethru; break;
      case P_visibility: st->visibility = par->visibility; break; case P_line_height: st->lh_mode = par->lh_mode; st->lh_val = par->lh_val; break;
      case P_margin_top: st->margin[0] = par->margin[0]; break; case P_margin_bottom: st->margin[2] = par->margin[2]; break;
    }
    return;
  }
  if((vl == 7 && !strncasecmp_(v, "initial", 7)) || (vl == 5 && !strncasecmp_(v, "unset", 5)) || (vl == 6 && !strncasecmp_(v, "revert", 6))){
    if(prop == P_display) st->display = D_INLINE; else if(prop == P_color) st->color = par->color; else if(prop == P_background_color || prop == P_background) st->bg = 0;
    else if(prop == P_width) st->width.u = 0; else if(prop == P_height) st->height.u = 0; else if(prop == P_text_decoration || prop == P_text_decoration_line) st->underline = st->linethru = 0;
    return;
  }
  switch(prop){
    case P_display: {
      #define DV(x, val) if(vl == (int)sizeof(x) - 1 && !strncasecmp_(v, x, vl)){ st->display = val; return; }
      DV("none", D_NONE) DV("block", D_BLOCK) DV("inline", D_INLINE) DV("inline-block", D_INLINE_BLOCK) DV("list-item", D_LIST_ITEM) DV("table", D_TABLE) DV("inline-table", D_TABLE)
      DV("table-row-group", D_ROW_GROUP) DV("table-header-group", D_ROW_GROUP) DV("table-footer-group", D_ROW_GROUP) DV("table-row", D_ROW) DV("table-cell", D_CELL) DV("table-caption", D_CAPTION)
      DV("flex", D_FLEX) DV("inline-flex", D_INLINE_FLEX) DV("grid", D_GRID) DV("inline-grid", D_GRID) DV("flow-root", D_BLOCK) DV("table-column", D_COL) DV("table-column-group", D_COL) DV("contents", D_CONTENTS)
      #undef DV
      return; }
    case P_position: st->position = (vl == 8 && !strncasecmp_(v, "absolute", 8)) ? POS_ABSOLUTE : (vl == 5 && !strncasecmp_(v, "fixed", 5)) ? POS_FIXED : (vl == 8 && !strncasecmp_(v, "relative", 8)) ? POS_RELATIVE : POS_STATIC; return;
    case P_float: st->flt = (vl == 4 && !strncasecmp_(v, "left", 4)) ? FL_LEFT : (vl == 5 && !strncasecmp_(v, "right", 5)) ? FL_RIGHT : FL_NONE; return;
    case P_clear: st->clear = (vl == 4 && !strncasecmp_(v, "left", 4)) ? 1 : (vl == 5 && !strncasecmp_(v, "right", 5)) ? 2 : (vl == 4 && !strncasecmp_(v, "both", 4)) ? 3 : 0; return;
    case P_color: { unsigned c; if(parse_color(v, vl, &c)){ st->color = c == COL_CURRENT ? par->color : (c ? c : st->color); } return; }
    case P_background_color: { unsigned c; if(parse_color(v, vl, &c)) st->bg = c == COL_CURRENT ? st->color : c; return; }
    case P_background: {
      nt = tokenize(v, vl, t, 12); unsigned c; int got = 0;
      for(int i = 0; i < nt; i++) if(!(t[i].n > 4 && !strncasecmp_(t[i].s, "url(", 4)) && !(t[i].n > 6 && (!strncasecmp_(t[i].s, "linear-", 7) || !strncasecmp_(t[i].s, "radial-", 7))) && parse_color(t[i].s, t[i].n, &c)){ st->bg = c == COL_CURRENT ? st->color : c; got = 1; }
      if(!got) st->bg = 0;
      return; }
    case P_font_size: { int px; if(font_size_value(v, vl, par->font_px, &px)){ st->font_px = px; } return; }
    case P_font_weight: { if(vl == 4 && !strncasecmp_(v, "bold", 4)) st->bold = 1; else if(vl == 6 && !strncasecmp_(v, "bolder", 6)) st->bold = 1; else if(vl == 6 && !strncasecmp_(v, "normal", 6)) st->bold = 0; else if(vl == 7 && !strncasecmp_(v, "lighter", 7)) st->bold = 0; else { int w = atoi(v); if(w) st->bold = w >= 600; } return; }
    case P_font_style: st->italic = (vl >= 6 && (!strncasecmp_(v, "italic", 6) || !strncasecmp_(v, "oblique", 7))); return;
    case P_font_family: { char lc[200]; int k = 0; for(int i = 0; i < vl && k < 199; i++) lc[k++] = (char)tolower((unsigned char)v[i]); lc[k] = 0;
      /* only the first family decides: monospace stacks start with a monospace face or end with the generic family */
      const char *comma = strchr(lc, ','); int firstlen = comma ? (int)(comma - lc) : k; lc[firstlen] = 0;
      st->mono = strstr(lc, "mono") || strstr(lc, "courier") || strstr(lc, "consolas") || strstr(lc, "menlo") || strstr(lc, "code") || strstr(lc, "hack");
      if(!st->mono && comma){ char *tail = strrchr(comma + 1, ','); if(!tail) tail = (char *)comma; if(strstr(tail, "monospace") && !strstr(lc, "sans") && !strstr(lc, "serif")) st->mono = strstr(comma, "monospace") != 0 && firstlen < 1; }
      return; }
    case P_font: {
      nt = tokenize(v, vl, t, 12); st->bold = 0; st->italic = 0;
      for(int i = 0; i < nt; i++){
        if(tok_is(&t[i], "bold") || tok_is(&t[i], "bolder")) st->bold = 1; else if(tok_is(&t[i], "italic") || tok_is(&t[i], "oblique")) st->italic = 1;
        else if(tok_is(&t[i], "normal")) {}
        else if(t[i].n == 3 && isdigit((unsigned char)t[i].s[0]) && t[i].s[1] == '0' && t[i].s[2] == '0'){ st->bold = (t[i].s[0] - '0') >= 6; }
        else {
          const char *sl = memchr(t[i].s, '/', t[i].n); int sn = sl ? (int)(sl - t[i].s) : t[i].n; int px;
          if(font_size_value(t[i].s, sn, par->font_px, &px)){
            st->font_px = px;
            if(sl){ struct len l; if(len_from(sl + 1, t[i].n - sn - 1, px, &l) && l.u == 1){ st->lh_mode = 2; st->lh_val = l.v; } else { const char *q = sl + 1; int m; if(parse_milli(&q, t[i].s + t[i].n, &m)){ st->lh_mode = 1; st->lh_val = m; } } }
            char lc[120]; int k = 0; for(int j = i + 1; j < nt; j++){ for(int c = 0; c < t[j].n && k < 118; c++) lc[k++] = (char)tolower((unsigned char)t[j].s[c]); lc[k++] = ' '; } lc[k] = 0;
            st->mono = strstr(lc, "mono") != 0 || strstr(lc, "courier") != 0 || strstr(lc, "consolas") != 0;
            break;
          }
        }
      }
      return; }
    case P_line_height: {
      if(vl == 6 && !strncasecmp_(v, "normal", 6)){ st->lh_mode = 0; return; }
      const char *q = v; int m; if(parse_milli(&q, v + vl, &m) && q == v + vl){ st->lh_mode = 1; st->lh_val = m; return; }
      struct len l; if(len_from(v, vl, st->font_px, &l)){ if(l.u == 1){ st->lh_mode = 2; st->lh_val = l.v; } else if(l.u == 2){ st->lh_mode = 2; st->lh_val = (int)((long long)st->font_px * l.v / 100000); } } return; }
    case P_text_align: st->text_align = (vl >= 6 && !strncasecmp_(v, "center", 6)) ? TA_CENTER : (vl >= 5 && !strncasecmp_(v, "right", 5)) ? TA_RIGHT : (vl >= 3 && !strncasecmp_(v, "end", 3)) ? TA_RIGHT : (vl >= 7 && !strncasecmp_(v, "justify", 7)) ? TA_LEFT : TA_LEFT; return;
    case P_text_decoration: case P_text_decoration_line: {
      nt = tokenize(v, vl, t, 6); int u = 0, l2 = 0, none = 0;
      for(int i = 0; i < nt; i++){ if(tok_is(&t[i], "underline")) u = 1; else if(tok_is(&t[i], "line-through")) l2 = 1; else if(tok_is(&t[i], "none")) none = 1; }
      if(none || u || l2){ st->underline = (unsigned char)u; st->linethru = (unsigned char)l2; } return; }
    case P_text_transform: st->text_transform = (vl == 9 && !strncasecmp_(v, "uppercase", 9)) ? 1 : (vl == 9 && !strncasecmp_(v, "lowercase", 9)) ? 2 : (vl == 10 && !strncasecmp_(v, "capitalize", 10)) ? 3 : 0; return;
    case P_white_space: st->white_space = (vl == 6 && !strncasecmp_(v, "nowrap", 6)) ? WS_NOWRAP : (vl == 3 && !strncasecmp_(v, "pre", 3)) ? WS_PRE : (vl == 8 && !strncasecmp_(v, "pre-wrap", 8)) ? WS_PRE_WRAP : (vl == 8 && !strncasecmp_(v, "pre-line", 8)) ? WS_PRE_LINE : WS_NORMAL; return;
    case P_visibility: st->visibility = (vl >= 6 && (!strncasecmp_(v, "hidden", 6) || !strncasecmp_(v, "collapse", 8))) ? 1 : 0; return;
    case P_opacity: { const char *q = v; int m; if(parse_milli(&q, v + vl, &m)){ if(q < v + vl && *q == '%') m *= 10; if(m <= 0) st->opacity = 0; else if(m >= 1000) st->opacity = 100; else st->opacity = m / 10; } return; }
    case P_overflow: case P_overflow_x: case P_overflow_y: st->overflow_hidden = (vl >= 6 && (!strncasecmp_(v, "hidden", 6) || !strncasecmp_(v, "auto", 4) || !strncasecmp_(v, "scroll", 6) || !strncasecmp_(v, "clip", 4))); return;
    case P_margin: nt = tokenize(v, vl, t, 4); set_box4(st->margin, t, nt, em); return;
    case P_padding: nt = tokenize(v, vl, t, 4); set_box4(st->padding, t, nt, em); return;
    case P_margin_top: case P_margin_right: case P_margin_bottom: case P_margin_left: len_from(v, vl, em, &st->margin[prop - P_margin_top]); return;
    case P_padding_top: case P_padding_right: case P_padding_bottom: case P_padding_left: { struct len l; if(len_from(v, vl, em, &l)) st->padding[prop - P_padding_top] = l; return; }
    case P_border: { nt = tokenize(v, vl, t, 8); for(int s = 0; s < 4; s++){ st->bstyle[s] = BS_NONE; st->border_w[s] = 3; st->border_c[s] = COL_CURRENT; set_border_side(st, s, t, nt, em); if(st->bstyle[s] == BS_NONE && nt && !tok_is(&t[0], "none") && !tok_is(&t[0], "0")){ /* "border: 1px #ccc" has no style keyword: treat as solid */ int hasnone = 0; for(int i = 0; i < nt; i++) if(tok_is(&t[i], "none") || tok_is(&t[i], "hidden")) hasnone = 1; if(!hasnone) st->bstyle[s] = BS_SOLID; } if(st->bstyle[s] == BS_NONE) st->border_w[s] = 0; } return; }
    case P_border_top: case P_border_right: case P_border_bottom: case P_border_left: { int s = prop - P_border_top; nt = tokenize(v, vl, t, 8); st->bstyle[s] = BS_NONE; st->border_w[s] = 3; st->border_c[s] = COL_CURRENT; set_border_side(st, s, t, nt, em);
      int hasnone = 0; for(int i = 0; i < nt; i++) if(tok_is(&t[i], "none") || tok_is(&t[i], "hidden")) hasnone = 1; if(!hasnone && nt && st->bstyle[s] == BS_NONE && st->border_w[s] > 0) st->bstyle[s] = BS_SOLID; if(st->bstyle[s] == BS_NONE) st->border_w[s] = 0; return; }
    case P_border_width: { nt = tokenize(v, vl, t, 4); struct len l[4]; int ok = 0; for(int i = 0; i < nt && i < 4; i++){ if(tok_is(&t[i], "thin")){ l[i].u = 1; l[i].v = 1; } else if(tok_is(&t[i], "medium")){ l[i].u = 1; l[i].v = 3; } else if(tok_is(&t[i], "thick")){ l[i].u = 1; l[i].v = 5; } else if(!len_from(t[i].s, t[i].n, em, &l[i])) return; ok++; }
      if(!ok) return; int idx[4][4] = {{0,0,0,0},{0,1,0,1},{0,1,2,1},{0,1,2,3}}; for(int s = 0; s < 4; s++) st->border_w[s] = l[idx[ok - 1][s]].v; return; }
    case P_border_style: { nt = tokenize(v, vl, t, 4); if(!nt) return; int idx[4][4] = {{0,0,0,0},{0,1,0,1},{0,1,2,1},{0,1,2,3}}; for(int s = 0; s < 4; s++){ struct tok *k = &t[idx[nt - 1][s]]; st->bstyle[s] = tok_is(k, "none") || tok_is(k, "hidden") ? BS_NONE : tok_is(k, "dashed") ? BS_DASHED : tok_is(k, "dotted") ? BS_DOTTED : BS_SOLID; } return; }
    case P_border_color: { nt = tokenize(v, vl, t, 4); if(!nt) return; int idx[4][4] = {{0,0,0,0},{0,1,0,1},{0,1,2,1},{0,1,2,3}}; for(int s = 0; s < 4; s++){ unsigned c; if(parse_color(t[idx[nt - 1][s]].s, t[idx[nt - 1][s]].n, &c)) st->border_c[s] = c; } return; }
    case P_border_top_width: case P_border_right_width: case P_border_bottom_width: case P_border_left_width: { struct len l; int s = prop - P_border_top_width; if(tok_is(&(struct tok){v, vl}, "thin")) st->border_w[s] = 1; else if(tok_is(&(struct tok){v, vl}, "medium")) st->border_w[s] = 3; else if(tok_is(&(struct tok){v, vl}, "thick")) st->border_w[s] = 5; else if(len_from(v, vl, em, &l) && l.u == 1) st->border_w[s] = l.v; return; }
    case P_border_top_color: case P_border_right_color: case P_border_bottom_color: case P_border_left_color: { unsigned c; if(parse_color(v, vl, &c)) st->border_c[prop - P_border_top_color] = c; return; }
    case P_border_top_style: case P_border_right_style: case P_border_bottom_style: case P_border_left_style: { struct tok k = {v, vl}; st->bstyle[prop - P_border_top_style] = tok_is(&k, "none") || tok_is(&k, "hidden") ? BS_NONE : tok_is(&k, "dashed") ? BS_DASHED : tok_is(&k, "dotted") ? BS_DOTTED : BS_SOLID; return; }
    case P_width: len_from(v, vl, em, &st->width); return;
    case P_height: len_from(v, vl, em, &st->height); return;
    case P_min_width: len_from(v, vl, em, &st->minw); return;
    case P_max_width: len_from(v, vl, em, &st->maxw); return;
    case P_min_height: len_from(v, vl, em, &st->minh); return;
    case P_max_height: len_from(v, vl, em, &st->maxh); return;
    case P_box_sizing: st->box_sizing = (vl == 10 && !strncasecmp_(v, "border-box", 10)); return;
    case P_list_style: case P_list_style_type: {
      nt = tokenize(v, vl, t, 4);
      for(int i = 0; i < nt; i++){
        struct tok *k = &t[i];
        if(tok_is(k, "none")) st->list_type = LT_NONE; else if(tok_is(k, "disc")) st->list_type = LT_DISC; else if(tok_is(k, "circle")) st->list_type = LT_CIRCLE; else if(tok_is(k, "square")) st->list_type = LT_SQUARE;
        else if(tok_is(k, "decimal") || tok_is(k, "decimal-leading-zero")) st->list_type = LT_DECIMAL; else if(tok_is(k, "lower-alpha") || tok_is(k, "lower-latin")) st->list_type = LT_LOWER_ALPHA;
        else if(tok_is(k, "upper-alpha") || tok_is(k, "upper-latin")) st->list_type = LT_UPPER_ALPHA; else if(tok_is(k, "lower-roman")) st->list_type = LT_LOWER_ROMAN; else if(tok_is(k, "upper-roman")) st->list_type = LT_UPPER_ROMAN;
      }
      return; }
    case P_vertical_align: st->valign = (vl >= 3 && !strncasecmp_(v, "top", 3)) ? VA_TOP : (vl >= 6 && !strncasecmp_(v, "middle", 6)) ? VA_MIDDLE : (vl >= 6 && !strncasecmp_(v, "bottom", 6)) ? VA_BOTTOM : (vl >= 3 && !strncasecmp_(v, "sub", 3)) ? VA_SUB : (vl >= 5 && !strncasecmp_(v, "super", 5)) ? VA_SUPER : (vl >= 8 && !strncasecmp_(v, "text-top", 8)) ? VA_TOP : VA_BASELINE; return;
    case P_flex: { nt = tokenize(v, vl, t, 4); if(nt == 1 && tok_is(&t[0], "none")){ st->flex_grow = 0; st->flex_shrink = 0; return; } if(nt == 1 && tok_is(&t[0], "auto")){ st->flex_grow = 1000; st->flex_shrink = 1000; st->flex_basis.u = 0; return; }
      const char *q = t[0].s; int m; if(nt >= 1 && parse_milli(&q, t[0].s + t[0].n, &m) && q == t[0].s + t[0].n){ st->flex_grow = m; st->flex_shrink = 1000; st->flex_basis.u = 1; st->flex_basis.v = 0;
        if(nt >= 2){ q = t[1].s; if(parse_milli(&q, t[1].s + t[1].n, &m) && q == t[1].s + t[1].n){ st->flex_shrink = m; if(nt >= 3) len_from(t[2].s, t[2].n, em, &st->flex_basis); } else len_from(t[1].s, t[1].n, em, &st->flex_basis); } } return; }
    case P_flex_grow: { const char *q = v; int m; if(parse_milli(&q, v + vl, &m)) st->flex_grow = m; return; }
    case P_flex_shrink: { const char *q = v; int m; if(parse_milli(&q, v + vl, &m)) st->flex_shrink = m; return; }
    case P_flex_basis: len_from(v, vl, em, &st->flex_basis); return;
    case P_flex_direction: st->flex_dir = (vl >= 6 && !strncasecmp_(v, "column", 6)) ? 1 : 0; return;
    case P_flex_wrap: st->flex_wrap = (vl >= 4 && !strncasecmp_(v, "wrap", 4)); return;
    case P_flex_flow: { nt = tokenize(v, vl, t, 3); for(int i = 0; i < nt; i++){ if(tok_is(&t[i], "column") || tok_is(&t[i], "column-reverse")) st->flex_dir = 1; else if(tok_is(&t[i], "row")) st->flex_dir = 0; else if(tok_is(&t[i], "wrap")) st->flex_wrap = 1; } return; }
    case P_justify_content: st->justify = (vl >= 6 && !strncasecmp_(v, "center", 6)) ? 1 : (vl >= 8 && !strncasecmp_(v, "flex-end", 8)) ? 2 : (vl >= 3 && !strncasecmp_(v, "end", 3)) ? 2 : (vl >= 13 && !strncasecmp_(v, "space-between", 13)) ? 3 : (vl >= 12 && !strncasecmp_(v, "space-around", 12)) ? 4 : (vl >= 12 && !strncasecmp_(v, "space-evenly", 12)) ? 4 : 0; return;
    case P_align_items: st->align_items = (vl >= 6 && !strncasecmp_(v, "center", 6)) ? 1 : (vl >= 8 && !strncasecmp_(v, "flex-end", 8)) ? 2 : (vl >= 3 && !strncasecmp_(v, "end", 3)) ? 2 : (vl >= 8 && !strncasecmp_(v, "baseline", 8)) ? 3 : ((vl >= 5 && !strncasecmp_(v, "start", 5)) || (vl >= 10 && !strncasecmp_(v, "flex-start", 10))) ? 4 : 0; return;
    case P_align_self: st->align_self = (vl >= 6 && !strncasecmp_(v, "center", 6)) ? 2 : (vl >= 8 && !strncasecmp_(v, "flex-end", 8)) ? 3 : (vl >= 10 && !strncasecmp_(v, "flex-start", 10)) ? 1 : 0; return;
    case P_gap: { nt = tokenize(v, vl, t, 2); struct len l; if(nt >= 1 && len_from(t[0].s, t[0].n, em, &l) && l.u == 1){ st->gap_y = l.v; st->gap_x = l.v; if(nt >= 2 && len_from(t[1].s, t[1].n, em, &l) && l.u == 1) st->gap_x = l.v; } return; }
    case P_border_radius: { nt = tokenize(v, vl, t, 1); struct len l; st->radius = 0; if(nt >= 1 && len_from(t[0].s, t[0].n, em, &l)){ if(l.u == 1) st->radius = (short)(l.v > 999 ? 999 : l.v < 0 ? 0 : l.v); else if(l.u == 2 && l.v > 0) st->radius = 999; } return; }
    case P_grid_template_columns: parse_tracks(v, vl, em, st); return;
    case P_grid_column: { st->grid_span = 1; const char *sp = 0; for(int i = 0; i + 4 <= vl; i++) if(!strncasecmp_(v + i, "span", 4)){ sp = v + i + 4; break; }
      if(sp){ while(sp < v + vl && css_ws(*sp)) sp++; int k = atoi(sp); if(k > 0) st->grid_span = k; }
      else { const char *sl = memchr(v, '/', vl); if(sl){ int a = atoi(v), b = atoi(sl + 1); if(b < 0) st->grid_span = 99; else if(b > a) st->grid_span = b - a; } } return; }
    case P_column_gap: { struct len l; if(len_from(v, vl, em, &l) && l.u == 1) st->gap_x = l.v; return; }
    case P_row_gap: { struct len l; if(len_from(v, vl, em, &l) && l.u == 1) st->gap_y = l.v; return; }
    case P_border_collapse: st->border_collapse = (vl >= 8 && !strncasecmp_(v, "collapse", 8)); return;
    case P_border_spacing: { struct len l; if(len_from(v, vl, em, &l) && l.u == 1) st->spacing = l.v; return; }
    case P_text_indent: { struct len l; if(len_from(v, vl, em, &l) && l.u == 1) st->text_indent = l.v; return; }
    default: return;
  }
}
static int strstr_n(const char *s, int n, const char *needle){ int nl = (int)strlen(needle); for(int i = 0; i + nl <= n; i++) if(!strncmp(s + i, needle, nl)) return 1; return 0; }

/* ---------- cascade ---------- */
static struct style default_style;
static void style_defaults(struct style *st){
  memset(st, 0, sizeof *st);
  st->eff_bg = 0xFFFFFFFFu; st->display = D_INLINE; st->font_px = 16; st->color = 0xFF1F2937u; st->opacity = 100; st->flex_shrink = 1000; st->spacing = 2;
  st->margin[0].u = st->margin[1].u = st->margin[2].u = st->margin[3].u = 1; for(int i = 0; i < 4; i++){ st->padding[i].u = 1; st->border_c[i] = COL_CURRENT; }
  st->list_type = LT_DISC;
}
static void style_inherit(struct style *st, const struct style *par){
  style_defaults(st);
  st->font_px = par->font_px; st->color = par->color; st->bold = par->bold; st->italic = par->italic; st->mono = par->mono; st->text_align = par->text_align; st->white_space = par->white_space;
  st->visibility = par->visibility; st->list_type = par->list_type; st->text_transform = par->text_transform; st->underline = par->underline; st->linethru = par->linethru;
  st->eff_bg = par->eff_bg; st->lh_mode = par->lh_mode; st->lh_val = par->lh_val; st->border_collapse = par->border_collapse; st->spacing = par->spacing; st->text_indent = par->text_indent; st->cv = par->cv;
}
struct cand { int r; int key_spec; int key_order; };
static void compute_style(int n){
  int par = N(n).parent; const struct style *ps = par >= 0 && N(par).st ? N(par).st : &default_style;
  struct style *st = malloc(sizeof *st); if(!st) return; style_inherit(st, ps);
  struct cand cand[300]; int nc = 0;
  int buckets[24]; int nb = 0; buckets[nb++] = 0; buckets[nb++] = bk_tag(N(n).tag);
  const char *id = attr_of(n, "id"); if(id && *id) buckets[nb++] = bk_id(id);
  const char *cl = attr_of(n, "class");
  if(cl){ for(const char *p = cl; *p && nb < 22; ){ while(*p && css_ws(*p)) p++; const char *e = p; while(*e && !css_ws(*e)) e++; if(e > p){ char tmp[80]; int l = (int)(e - p); if(l < 80){ memcpy(tmp, p, l); tmp[l] = 0; buckets[nb++] = bk_cls(tmp); } } p = e; } }
  for(int b = 0; b < nb; b++){
    int dup = 0; for(int k = 0; k < b; k++) if(buckets[k] == buckets[b]) dup = 1; if(dup) continue;
    for(int ri = bucket[buckets[b]]; ri; ri = rules[ri - 1].next){
      struct rule *r = &rules[ri - 1];
      if(nc >= 300) break;
      if(match_from(n, r, r->nparts - 1)){ cand[nc].r = ri - 1; cand[nc].key_spec = r->origin * 1000000 + r->spec; cand[nc].key_order = r->order; nc++; }
    }
  }
  for(int i = 1; i < nc; i++){ struct cand c = cand[i]; int j = i - 1; while(j >= 0 && (cand[j].key_spec > c.key_spec || (cand[j].key_spec == c.key_spec && cand[j].key_order > c.key_order))){ cand[j + 1] = cand[j]; j--; } cand[j + 1] = c; }
  struct decl inl[48]; int ni = 0; const char *sa = attr_of(n, "style"); if(sa) ni = parse_decls(sa, sa + strlen(sa), inl, 48);
  { struct cvars *own = 0;                                              /* custom properties defined on this element (cascade order) */
    for(int imp = 0; imp < 2; imp++){
      for(int i = 0; i <= nc; i++){
        struct decl *ds; int nd2;
        if(i < nc){ ds = rules[cand[i].r].d; nd2 = rules[cand[i].r].nd; } else { ds = inl; nd2 = ni; }
        for(int k = 0; k < nd2; k++){
          if(ds[k].prop != P_CUSTOM || ds[k].imp != imp) continue;
          if(!own){ own = a_alloc(sizeof *own); if(!own) break; if(st->cv) memcpy(own, st->cv, sizeof *own); else own->n = 0; }
          int f = -1; for(int q = 0; q < own->n; q++) if(own->e[q].nl == ds[k].cnlen && !strncmp(own->e[q].name, ds[k].cname, ds[k].cnlen)){ f = q; break; }
          if(f < 0 && own->n < CV_MAX) f = own->n++;
          if(f >= 0){ own->e[f].name = ds[k].cname; own->e[f].nl = ds[k].cnlen; own->e[f].val = ds[k].val; own->e[f].vl = ds[k].vlen; }
        }
      }
    }
    if(own) st->cv = own; }
  for(int imp = 0; imp < 2; imp++) for(int pass = 0; pass < 2; pass++){
    for(int i = 0; i < nc; i++){ struct rule *r = &rules[cand[i].r]; for(int k = 0; k < r->nd; k++) if(r->d[k].imp == imp && r->d[k].prop != P_CUSTOM) apply_decl(st, ps, &r->d[k], pass); }
    for(int k = 0; k < ni; k++) if(inl[k].imp == imp && inl[k].prop != P_CUSTOM) apply_decl(st, ps, &inl[k], pass);
  }
  /* presentational attributes for a few elements */
  int tag = N(n).tag;
  if(tag == TAG_font){ const char *c = attr_of(n, "color"); unsigned col; if(c && parse_color(c, (int)strlen(c), &col) && col != COL_CURRENT) st->color = col; const char *sz = attr_of(n, "size"); if(sz){ int v = atoi(sz); static const int px[] = {10,13,16,18,24,32,48}; if(*sz == '+') v += 3; if(v < 1) v = 1; if(v > 7) v = 7; st->font_px = px[v - 1]; } }
  if(tag == TAG_body){ const char *c = attr_of(n, "bgcolor"); unsigned col; if(c && parse_color(c, (int)strlen(c), &col)) st->bg = col; c = attr_of(n, "text"); if(c && parse_color(c, (int)strlen(c), &col)) st->color = col; }
  if(tag == TAG_table || tag == TAG_td || tag == TAG_th || tag == TAG_tr){
    const char *c = attr_of(n, "bgcolor"); unsigned col; if(c && parse_color(c, (int)strlen(c), &col)) st->bg = col;
    const char *a = attr_of(n, "align"); if(a && tag != TAG_table){ if(!strcasecmp_(a, "center")) st->text_align = TA_CENTER; else if(!strcasecmp_(a, "right")) st->text_align = TA_RIGHT; else if(!strcasecmp_(a, "left")) st->text_align = TA_LEFT; }
    const char *w = attr_of(n, "width"); if(w && st->width.u == 0){ int m; const char *q = w; if(parse_milli(&q, w + strlen(w), &m)){ if(*q == '%'){ st->width.u = 2; st->width.v = m; } else { st->width.u = 1; st->width.v = m / 1000; } } }
    const char *hh = attr_of(n, "height"); if(hh && st->height.u == 0 && tag != TAG_tr){ int m; const char *q = hh; if(parse_milli(&q, hh + strlen(hh), &m) && *q != '%'){ st->height.u = 1; st->height.v = m / 1000; } }
    if(tag == TAG_table){
      const char *b = attr_of(n, "border"); if(b && !st->bstyle[0] && st->border_w[0] == 0){ int bw = atoi(b); if(bw > 0 || (*b == 0)){ if(*b == 0) bw = 1; for(int s = 0; s < 4; s++){ st->border_w[s] = bw; st->bstyle[s] = BS_SOLID; st->border_c[s] = 0xFF808080u; } } }
      const char *cs = attr_of(n, "cellspacing"); if(cs) st->spacing = atoi(cs);
    }
    if(tag == TAG_td || tag == TAG_th){
      int tbl = par; while(tbl >= 0 && N(tbl).tag != TAG_table) tbl = N(tbl).parent;
      if(tbl >= 0){
        const char *cp = attr_of(tbl, "cellpadding");
        if(cp && st->padding[0].v == 0 && st->padding[1].v == 0){ int v = atoi(cp); for(int s = 0; s < 4; s++){ st->padding[s].u = 1; st->padding[s].v = v; } }
        const char *b = attr_of(tbl, "border"); if(b && atoi(b) > 0 && st->bstyle[0] == BS_NONE){ for(int s = 0; s < 4; s++){ st->border_w[s] = 1; st->bstyle[s] = BS_SOLID; st->border_c[s] = 0xFFA0A0A0u; } }
      }
      const char *va = attr_of(n, "valign"); if(va){ if(!strcasecmp_(va, "top")) st->valign = VA_TOP; else if(!strcasecmp_(va, "middle")) st->valign = VA_MIDDLE; else if(!strcasecmp_(va, "bottom")) st->valign = VA_BOTTOM; }
    }
  }
  if(tag == TAG_img || tag == TAG_input || tag == TAG_iframe || tag == TAG_svg || tag == TAG_canvas || tag == TAG_video){
    const char *w = attr_of(n, "width"), *h = attr_of(n, "height");
    if(w && st->width.u == 0){ int m; const char *q = w; if(parse_milli(&q, w + strlen(w), &m)){ if(*q == '%'){ st->width.u = 2; st->width.v = m; } else { st->width.u = 1; st->width.v = m / 1000; } } }
    if(h && st->height.u == 0){ int m; const char *q = h; if(parse_milli(&q, h + strlen(h), &m)){ if(*q == '%'){ st->height.u = 2; st->height.v = m; } else { st->height.u = 1; st->height.v = m / 1000; } } }
  }
  if(attr_of(n, "hidden") && tag != TAG_input) st->display = D_NONE;
  if(tag == TAG_input){ const char *ty = attr_of(n, "type"); if(ty && !strcasecmp_(ty, "hidden")) st->display = D_NONE; }
  if(st->position == POS_ABSOLUTE) st->display = D_NONE;                 /* absolutely positioned boxes are not supported: hide them */
  if(st->opacity == 0) st->display = D_NONE;
  { unsigned base = par >= 0 && N(par).st ? N(par).st->eff_bg : 0xFFFFFFFFu; if(!base) base = 0xFFFFFFFFu;
    st->bg = flat_over(st->bg, base); st->eff_bg = (st->bg >> 24) == 255 ? st->bg : base;
    for(int i = 0; i < 4; i++) if(st->border_c[i] != COL_CURRENT) st->border_c[i] = flat_over(st->border_c[i], st->eff_bg);
    if(st->color != COL_CURRENT) st->color = flat_over(st->color, st->eff_bg); }
  N(n).st = st;
}
static void compute_all(int n){
  if(N(n).type == 0){
    compute_style(n);
    if(N(n).st->display == D_NONE){ return; }
    for(int c = N(n).first; c >= 0; c = N(c).next) compute_all(c);
  } else {
    int par = N(n).parent; N(n).st = par >= 0 ? N(par).st : &default_style;
  }
}
static const char UA_CSS[] =
  "html,body,address,article,aside,blockquote,center,dd,details,dialog,div,dl,dt,fieldset,figcaption,figure,footer,form,h1,h2,h3,h4,h5,h6,header,hgroup,hr,main,menu,nav,ol,p,pre,search,section,summary,ul,legend,noscript,optgroup{display:block}"
  "head,script,style,title,meta,link,base,template,noframes,param,source,track,area,datalist,iframe,audio,video,canvas,object,embed,map,dialog{display:none}"
  "li{display:list-item}table{display:table}caption{display:table-caption}thead,tbody,tfoot{display:table-row-group}tr{display:table-row}td,th{display:table-cell}col,colgroup{display:table-column}"
  "body{margin:8px}h1{font-size:32px;margin:21px 0;font-weight:bold}h2{font-size:24px;margin:20px 0;font-weight:bold}h3{font-size:19px;margin:18px 0;font-weight:bold}h4{font-size:16px;margin:21px 0;font-weight:bold}"
  "h5{font-size:13px;margin:22px 0;font-weight:bold}h6{font-size:11px;margin:25px 0;font-weight:bold}p,dl,menu,fieldset,figure{margin:16px 0}blockquote{margin:16px 40px}ul,ol,menu{margin:16px 0;padding-left:40px}"
  "ol{list-style-type:decimal}ul{list-style-type:disc}ul ul,ol ul{list-style-type:circle}ul ul ul,ol ul ul,ul ol ul{list-style-type:square}dd{margin-left:40px}"
  "pre{white-space:pre;font-family:monospace;margin:16px 0}code,kbd,samp,tt{font-family:monospace}b,strong,th{font-weight:bold}th{text-align:center}i,em,cite,dfn,var,address{font-style:italic}"
  "u,ins{text-decoration:underline}s,strike,del{text-decoration:line-through}small{font-size:13px}big{font-size:19px}sub,sup{font-size:13px}sub{vertical-align:sub}sup{vertical-align:super}"
  "mark{background:#ff0;color:#000}a[href]{color:#1a56c9;text-decoration:underline}hr{height:0;border-top:1px solid #c8cdd3;margin:8px 0;overflow:hidden}center{text-align:center}"
  "table{border-spacing:2px;text-indent:0}td,th{padding:1px}fieldset{padding:6px 12px;border:1px solid #bbb}legend{padding:0 4px}"
  "img{display:inline-block}button{padding:4px 12px;border:1px solid #8a8f98;background:#eceef1}button,input,select,textarea{display:inline-block}nobr{white-space:nowrap}summary{display:block}"
  "details>*:not(summary){display:none}details[open]>*:not(summary){display:block}";
#endif
