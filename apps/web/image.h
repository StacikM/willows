/* Image decoders for the Willows browser: PNG (all colour types, Adam7), JPEG (baseline + progressive), GIF (first frame).
   Output is 32-bit 0xAARRGGBB, top-down. No floating point, only malloc/free/memcpy/memset. */
#ifndef WEB_IMAGE_H
#define WEB_IMAGE_H
#include <stdlib.h>
#include <string.h>

typedef struct { int w, h; unsigned *px; } wimg;
#define IMG_MAXPIX (16 * 1000 * 1000)

static wimg *img_new(int w, int h){
  if(w <= 0 || h <= 0 || (long long)w * h > IMG_MAXPIX) return 0;
  wimg *im = malloc(sizeof *im); if(!im) return 0;
  im->w = w; im->h = h; im->px = calloc((size_t)w * h, 4);
  if(!im->px){ free(im); return 0; }
  return im;
}
static void img_free(wimg *im){ if(im){ free(im->px); free(im); } }

/* ---------------- inflate (zlib / deflate) ---------------- */
struct inf { const unsigned char *in; size_t n, pos; unsigned bitbuf; int bitcnt; unsigned char *out; size_t olen, ocap; int err; };
struct huff { unsigned short count[16], sym[320]; };
static int inf_bits(struct inf *s, int need){
  long v = s->bitbuf;
  while(s->bitcnt < need){
    if(s->pos >= s->n){ s->err = 1; return 0; }
    v |= (long)s->in[s->pos++] << s->bitcnt; s->bitcnt += 8;
  }
  s->bitbuf = (unsigned)(v >> need); s->bitcnt -= need;
  return (int)(v & ((1L << need) - 1));
}
static void inf_put(struct inf *s, unsigned char c){
  if(s->olen >= s->ocap){ size_t nc = s->ocap ? s->ocap * 2 : 65536; unsigned char *p = realloc(s->out, nc); if(!p){ s->err = 1; return; } s->out = p; s->ocap = nc; }
  s->out[s->olen++] = c;
}
static int huff_build(struct huff *h, const unsigned char *len, int n){
  unsigned short offs[16]; memset(h->count, 0, sizeof h->count);
  for(int i = 0; i < n; i++) h->count[len[i]]++;
  if(h->count[0] == n) return 0;
  offs[1] = 0; for(int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
  for(int i = 0; i < n; i++) if(len[i]) h->sym[offs[len[i]]++] = (unsigned short)i;
  return 0;
}
static int huff_dec(struct inf *s, const struct huff *h){
  int code = 0, first = 0, index = 0;
  for(int len = 1; len <= 15; len++){
    code |= inf_bits(s, 1); if(s->err) return -1;
    int cnt = h->count[len];
    if(code - cnt < first) return h->sym[index + (code - first)];
    index += cnt; first += cnt; first <<= 1; code <<= 1;
  }
  return -1;
}
static const unsigned short LBASE[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const unsigned short LEXT[]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const unsigned short DBASE[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const unsigned short DEXT[]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static int inf_codes(struct inf *s, const struct huff *lc, const struct huff *dc){
  for(;;){
    int sym = huff_dec(s, lc); if(sym < 0 || s->err) return -1;
    if(sym < 256) inf_put(s, (unsigned char)sym);
    else if(sym == 256) return 0;
    else {
      sym -= 257; if(sym >= 29) return -1;
      int len = LBASE[sym] + inf_bits(s, LEXT[sym]);
      int ds = huff_dec(s, dc); if(ds < 0 || ds >= 30) return -1;
      size_t dist = DBASE[ds] + inf_bits(s, DEXT[ds]);
      if(s->err || dist > s->olen) return -1;
      for(int i = 0; i < len; i++){ inf_put(s, s->out[s->olen - dist]); if(s->err) return -1; }
    }
  }
}
/* raw deflate stream; returns malloc'd buffer */
static unsigned char *inflate_raw(const unsigned char *in, size_t n, size_t *outlen){
  struct inf s; memset(&s, 0, sizeof s); s.in = in; s.n = n;
  static struct huff lc, dc; int last;
  do {
    last = inf_bits(&s, 1); int type = inf_bits(&s, 2); if(s.err) break;
    if(type == 0){
      s.bitbuf = 0; s.bitcnt = 0;
      if(s.pos + 4 > s.n){ s.err = 1; break; }
      unsigned len = s.in[s.pos] | (s.in[s.pos + 1] << 8); s.pos += 4;
      if(s.pos + len > s.n){ s.err = 1; break; }
      for(unsigned i = 0; i < len; i++) inf_put(&s, s.in[s.pos++]);
    } else if(type == 1){
      unsigned char l[320]; int i = 0;
      for(; i < 144; i++) l[i] = 8; for(; i < 256; i++) l[i] = 9; for(; i < 280; i++) l[i] = 7; for(; i < 288; i++) l[i] = 8;
      huff_build(&lc, l, 288); for(i = 0; i < 30; i++) l[i] = 5; huff_build(&dc, l, 30);
      if(inf_codes(&s, &lc, &dc)) s.err = 1;
    } else if(type == 2){
      static const unsigned char ord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
      unsigned char lens[320]; int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4;
      if(s.err || nlen > 286 || ndist > 30){ s.err = 1; break; }
      int i; for(i = 0; i < 19; i++) lens[i] = 0;
      for(i = 0; i < ncode; i++) lens[ord[i]] = (unsigned char)inf_bits(&s, 3);
      struct huff cl; huff_build(&cl, lens, 19);
      unsigned char all[320]; i = 0;
      while(i < nlen + ndist){
        int sym = huff_dec(&s, &cl); if(sym < 0){ s.err = 1; break; }
        if(sym < 16) all[i++] = (unsigned char)sym;
        else {
          int prev = 0, rep;
          if(sym == 16){ if(!i){ s.err = 1; break; } prev = all[i - 1]; rep = 3 + inf_bits(&s, 2); }
          else if(sym == 17) rep = 3 + inf_bits(&s, 3); else rep = 11 + inf_bits(&s, 7);
          if(i + rep > nlen + ndist){ s.err = 1; break; }
          while(rep--) all[i++] = (unsigned char)prev;
        }
      }
      if(s.err) break;
      huff_build(&lc, all, nlen); huff_build(&dc, all + nlen, ndist);
      if(inf_codes(&s, &lc, &dc)) s.err = 1;
    } else s.err = 1;
  } while(!last && !s.err);
  if(s.err && !s.olen){ free(s.out); return 0; }          /* keep partial output for truncated images */
  *outlen = s.olen; return s.out;
}

/* ---------------- PNG ---------------- */
static unsigned be32(const unsigned char *p){ return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static int paeth(int a, int b, int c){ int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p; return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }
static wimg *png_decode(const unsigned char *d, int n){
  if(n < 33 || memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return 0;
  int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0, pos = 8;
  unsigned char pal[256][4]; int npal = 0; memset(pal, 255, sizeof pal);
  int trns_gray = -1, trns_r = -1, trns_g = -1, trns_b = -1;
  unsigned char *idat = 0; size_t ilen = 0, icap = 0;
  while(pos + 12 <= n){
    unsigned len = be32(d + pos); const unsigned char *t = d + pos + 4, *c = d + pos + 8;
    if(pos + 12 + (long long)len > n) break;
    if(!memcmp(t, "IHDR", 4) && len >= 13){ w = (int)be32(c); h = (int)be32(c + 4); depth = c[8]; ctype = c[9]; interlace = c[12]; }
    else if(!memcmp(t, "PLTE", 4)){ npal = len / 3; if(npal > 256) npal = 256; for(int i = 0; i < npal; i++){ pal[i][0] = c[i*3]; pal[i][1] = c[i*3+1]; pal[i][2] = c[i*3+2]; } }
    else if(!memcmp(t, "tRNS", 4)){
      if(ctype == 3){ for(unsigned i = 0; i < len && i < 256; i++) pal[i][3] = c[i]; }
      else if(ctype == 0 && len >= 2) trns_gray = (c[0] << 8) | c[1];
      else if(ctype == 2 && len >= 6){ trns_r = (c[0] << 8) | c[1]; trns_g = (c[2] << 8) | c[3]; trns_b = (c[4] << 8) | c[5]; }
    }
    else if(!memcmp(t, "IDAT", 4)){
      if(ilen + len > icap){ icap = (ilen + len) * 2; unsigned char *p = realloc(idat, icap); if(!p){ free(idat); return 0; } idat = p; }
      memcpy(idat + ilen, c, len); ilen += len;
    }
    else if(!memcmp(t, "IEND", 4)) break;
    pos += 12 + len;
  }
  if(!w || !h || !idat || ilen < 3){ free(idat); return 0; }
  int chans = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
  if(depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16){ free(idat); return 0; }
  wimg *im = img_new(w, h); if(!im){ free(idat); return 0; }
  size_t rawlen; unsigned char *raw = inflate_raw(idat + 2, ilen - 2, &rawlen); free(idat);
  if(!raw){ img_free(im); return 0; }
  int bpp = (chans * depth + 7) / 8;                                /* bytes per pixel for filtering (min 1) */
  static const int px0[7] = {0,4,0,2,0,1,0}, py0[7] = {0,0,4,0,2,0,1}, pdx[7] = {8,8,4,4,2,2,1}, pdy[7] = {8,8,8,4,4,2,2};
  int passes = interlace ? 7 : 1; size_t off = 0;
  for(int ps = 0; ps < passes; ps++){
    int sx = interlace ? px0[ps] : 0, sy = interlace ? py0[ps] : 0, dx = interlace ? pdx[ps] : 1, dy = interlace ? pdy[ps] : 1;
    int pw = interlace ? (w - sx + dx - 1) / dx : w, ph = interlace ? (h - sy + dy - 1) / dy : h;
    if(pw <= 0 || ph <= 0) continue;
    size_t stride = ((size_t)pw * chans * depth + 7) / 8;
    if(off >= rawlen) break;
    if(off + (stride + 1) * ph > rawlen){ ph = (int)((rawlen - off) / (stride + 1)); if(ph <= 0) break; }
    unsigned char *prev = 0;
    for(int y = 0; y < ph; y++){
      unsigned char *row = raw + off + (size_t)y * (stride + 1); int f = row[0]; unsigned char *cur = row + 1;
      for(size_t i = 0; i < stride; i++){
        int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0, x = cur[i];
        switch(f){ case 1: x += a; break; case 2: x += b; break; case 3: x += (a + b) / 2; break; case 4: x += paeth(a, b, c); break; }
        cur[i] = (unsigned char)x;
      }
      prev = cur;
      for(int x = 0; x < pw; x++){
        unsigned r, g, b, a = 255; int idx;
        #define SAMPLE(ch) ({ int _v; if(depth == 8) _v = cur[x*chans+(ch)]; else if(depth == 16) _v = (cur[(x*chans+(ch))*2] << 8) | cur[(x*chans+(ch))*2+1]; \
            else { int _bit = (x*chans+(ch))*depth; _v = (cur[_bit>>3] >> (8 - depth - (_bit & 7))) & ((1<<depth)-1); } _v; })
        int s0 = SAMPLE(0);
        int maxv = depth == 16 ? 65535 : (1 << depth) - 1;
        if(ctype == 3){ idx = s0 < 256 ? s0 : 0; r = pal[idx][0]; g = pal[idx][1]; b = pal[idx][2]; a = pal[idx][3]; }
        else if(ctype == 0){ r = g = b = (unsigned)(s0 * 255 / maxv); if(trns_gray == s0) a = 0; }
        else if(ctype == 4){ r = g = b = (unsigned)(s0 * 255 / maxv); a = (unsigned)(SAMPLE(1) * 255 / maxv); }
        else { int s1 = SAMPLE(1), s2 = SAMPLE(2); r = (unsigned)(s0 * 255 / maxv); g = (unsigned)(s1 * 255 / maxv); b = (unsigned)(s2 * 255 / maxv);
               if(ctype == 6) a = (unsigned)(SAMPLE(3) * 255 / maxv); else if(trns_r == s0 && trns_g == s1 && trns_b == s2) a = 0; }
        im->px[(size_t)(sy + y * dy) * w + sx + x * dx] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
    off += (stride + 1) * (size_t)(interlace ? (h - sy + dy - 1) / dy : h);
  }
  free(raw); return im;
}

/* ---------------- JPEG (baseline + progressive) ---------------- */
struct jhuff { unsigned char bits[17], vals[256]; int maxcode[18], valptr[17], mincode[17]; unsigned char fast[512]; int ok; };
struct jcomp { int id, h, v, tq, td, ta; int bw, bh; short *coef; int pred; };
struct jpg {
  const unsigned char *d; int n, pos; unsigned bitbuf; int bitcnt; int hit_marker;
  unsigned short qt[4][64]; struct jhuff hdc[4], hac[4];
  struct jcomp c[4]; int nc, w, h, hmax, vmax, mcux, mcuy, progressive, restart, eobrun, adobe;
};
static const unsigned char ZZ[64] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static void jhuff_build(struct jhuff *h){
  int code = 0, k = 0;
  for(int l = 1; l <= 16; l++){
    h->valptr[l] = k; h->mincode[l] = code; code += h->bits[l]; k += h->bits[l];
    h->maxcode[l] = h->bits[l] ? code - 1 : -1; code <<= 1;
  }
  h->maxcode[17] = 0x7FFFFFFF; h->ok = 1;
}
static int j_fill(struct jpg *j){
  while(j->bitcnt <= 24){
    int b = 0;
    if(!j->hit_marker && j->pos < j->n){
      b = j->d[j->pos++];
      if(b == 0xFF){ int b2 = j->pos < j->n ? j->d[j->pos] : 0xD9; if(b2 == 0) j->pos++; else { j->hit_marker = 1; j->pos--; b = 0; } }
    }
    j->bitbuf |= (unsigned)b << (24 - j->bitcnt); j->bitcnt += 8;
  }
  return 0;
}
static int j_getbits(struct jpg *j, int n){ if(!n) return 0; if(j->bitcnt < n) j_fill(j); int v = (int)(j->bitbuf >> (32 - n)); j->bitbuf <<= n; j->bitcnt -= n; return v; }
static int j_getbit(struct jpg *j){ return j_getbits(j, 1); }
static int j_decode(struct jpg *j, const struct jhuff *h){
  if(j->bitcnt < 16) j_fill(j);
  int code = 0;
  for(int l = 1; l <= 16; l++){
    code = (int)(j->bitbuf >> (32 - l));
    if(h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]){
      j->bitbuf <<= l; j->bitcnt -= l; return h->vals[h->valptr[l] + code - h->mincode[l]];
    }
  }
  j->bitbuf <<= 16; j->bitcnt -= 16; return 0;
}
static int j_extend(int v, int t){ return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }
static void j_reset_bits(struct jpg *j){ j->bitbuf = 0; j->bitcnt = 0; j->hit_marker = 0; }

/* integer IDCT: fixed-point separable, 13-bit constants */
static void j_idct(const short *in, unsigned char *out, int stride){
  static int tab[8][8]; static int init;
  if(!init){
    /* c[k][n] = round(8192 * (k==0 ? 1/sqrt(8) : 1/2) * cos((2n+1)k pi/16)) */
    static const int cs[8][8] = {
      { 2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896 },
      { 4017, 3406, 2276,  799, -799,-2276,-3406,-4017 },
      { 3784, 1567,-1567,-3784,-3784,-1567, 1567, 3784 },
      { 3406, -799,-4017,-2276, 2276, 4017,  799,-3406 },
      { 2896,-2896,-2896, 2896, 2896,-2896,-2896, 2896 },
      { 2276,-4017,  799, 3406,-3406, -799, 4017,-2276 },
      { 1567,-3784, 3784,-1567,-1567, 3784,-3784, 1567 },
      {  799,-2276, 3406,-4017, 4017,-3406, 2276, -799 } };
    for(int k = 0; k < 8; k++) for(int m = 0; m < 8; m++) tab[k][m] = cs[k][m];
    init = 1;
  }
  int tmp[64];
  for(int y = 0; y < 8; y++){                                        /* rows: out[y][x] = sum_k in[y][k]*tab[k][x] (scale 4096*... ) */
    const short *r = in + y * 8;
    if(!r[1] && !r[2] && !r[3] && !r[4] && !r[5] && !r[6] && !r[7]){ int v = r[0] * 2896 >> 10; for(int x = 0; x < 8; x++) tmp[y*8+x] = v; continue; }
    for(int x = 0; x < 8; x++){ int s = 0; for(int k = 0; k < 8; k++) s += r[k] * tab[k][x]; s >>= 10; tmp[y*8+x] = s > 65536 ? 65536 : s < -65536 ? -65536 : s; }
  }
  for(int x = 0; x < 8; x++) for(int y = 0; y < 8; y++){
    int s = 0; for(int k = 0; k < 8; k++) s += tmp[k*8+x] * tab[k][y];
    s = (s >> 16) + 128; out[y * stride + x] = (unsigned char)(s < 0 ? 0 : s > 255 ? 255 : s);
  }
}
static int j_scan(struct jpg *j, int *comps, int ncomp, int ss, int se, int ah, int al){
  j_reset_bits(j); j->eobrun = 0; for(int i = 0; i < j->nc; i++) j->c[i].pred = 0;
  int single = ncomp == 1; int mcus_x, mcus_y;
  if(single){ struct jcomp *c = &j->c[comps[0]]; mcus_x = (j->w * c->h + j->hmax * 8 - 1) / (j->hmax * 8); mcus_y = (j->h * c->v + j->vmax * 8 - 1) / (j->vmax * 8); }
  else { mcus_x = j->mcux; mcus_y = j->mcuy; }
  int rst = 0, total = mcus_x * mcus_y;
  for(int m = 0; m < total; m++){
    if(j->restart && m && m % j->restart == 0){
      j_reset_bits(j);
      while(j->pos + 1 < j->n && !(j->d[j->pos] == 0xFF && j->d[j->pos+1] >= 0xD0 && j->d[j->pos+1] <= 0xD7)) { if(j->d[j->pos] == 0xFF && j->d[j->pos+1] != 0 && j->d[j->pos+1] != 0xFF) return 0; j->pos++; }
      j->pos += 2; rst++; j->eobrun = 0; for(int i = 0; i < j->nc; i++) j->c[i].pred = 0;
    }
    int mx = m % mcus_x, my = m / mcus_x;
    for(int ci = 0; ci < ncomp; ci++){
      struct jcomp *c = &j->c[comps[ci]];
      int hh = single ? 1 : c->h, vv = single ? 1 : c->v;
      for(int by = 0; by < vv; by++) for(int bx = 0; bx < hh; bx++){
        int bxx = single ? mx : mx * c->h + bx, byy = single ? my : my * c->v + by;
        if(bxx >= c->bw || byy >= c->bh) { /* padding block still must be decoded in interleaved scans */ }
        short tmpblk[64]; short *blk = (bxx < c->bw && byy < c->bh) ? c->coef + ((size_t)byy * c->bw + bxx) * 64 : tmpblk;
        if(blk == tmpblk) memset(tmpblk, 0, sizeof tmpblk);
        if(!j->progressive){
          int t = j_decode(j, &j->hdc[c->td]) & 15; int diff = t ? j_extend(j_getbits(j, t), t) : 0;
          c->pred += diff; blk[0] = (short)c->pred;
          for(int k = 1; k < 64; ){
            int rs = j_decode(j, &j->hac[c->ta]), r = rs >> 4, s = rs & 15;
            if(!s){ if(r == 15){ k += 16; continue; } break; }
            k += r; if(k > 63) break;
            blk[ZZ[k]] = (short)j_extend(j_getbits(j, s), s); k++;
          }
        } else if(ss == 0){
          if(!ah){ int t = j_decode(j, &j->hdc[c->td]) & 15; int diff = t ? j_extend(j_getbits(j, t), t) : 0; c->pred += diff; blk[0] = (short)(c->pred * (1 << al)); }
          else if(j_getbit(j)) blk[0] |= (short)(1 << al);
        } else if(!ah){                                              /* AC first */
          if(j->eobrun > 0){ j->eobrun--; continue; }
          for(int k = ss; k <= se; ){
            int rs = j_decode(j, &j->hac[c->ta]), r = rs >> 4, s = rs & 15;
            if(!s){ if(r < 15){ j->eobrun = (1 << r) - 1; if(r) j->eobrun += j_getbits(j, r); break; } k += 16; continue; }
            k += r; if(k > 63) break;
            blk[ZZ[k]] = (short)(j_extend(j_getbits(j, s), s) * (1 << al)); k++;
          }
        } else {                                                     /* AC refinement */
          int p1 = 1 << al, m1 = -1 * (1 << al), k = ss;
          if(j->eobrun <= 0){
            for(; k <= se; k++){
              int rs = j_decode(j, &j->hac[c->ta]), r = rs >> 4, s = rs & 15, val = 0;
              if(!s){ if(r < 15){ j->eobrun = (1 << r); if(r) j->eobrun += j_getbits(j, r); break; } }
              else val = j_getbit(j) ? p1 : m1;
              while(k <= se){
                short *cf = &blk[ZZ[k]];
                if(*cf){ if(j_getbit(j)){ if((*cf & p1) == 0){ if(*cf >= 0) *cf += (short)p1; else *cf += (short)m1; } } }
                else { if(r == 0) break; r--; }
                k++;
              }
              if(val && k <= se) blk[ZZ[k]] = (short)val;
            }
          }
          if(j->eobrun > 0){
            for(; k <= se; k++){ short *cf = &blk[ZZ[k]]; if(*cf && j_getbit(j)){ if((*cf & p1) == 0){ if(*cf >= 0) *cf += (short)p1; else *cf += (short)m1; } } }
            j->eobrun--;
          }
        }
      }
    }
  }
  return 1;
}
static wimg *jpeg_decode(const unsigned char *d, int n){
  if(n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 0;
  struct jpg *j = calloc(1, sizeof *j); if(!j) return 0;
  j->d = d; j->n = n; j->pos = 2; wimg *im = 0; int got_sof = 0;
  while(j->pos + 4 <= n){
    if(d[j->pos] != 0xFF){ j->pos++; continue; }
    int m = d[j->pos + 1]; if(m == 0xFF){ j->pos++; continue; }
    if(m == 0 || m == 1 || (m >= 0xD0 && m <= 0xD8)){ j->pos += 2; continue; }
    if(m == 0xD9) break;
    int len = (d[j->pos + 2] << 8) | d[j->pos + 3]; const unsigned char *p = d + j->pos + 4; int plen = len - 2;
    if(j->pos + 2 + len > n) break;
    if(m == 0xDB){
      while(plen >= 65){ int pq = p[0] >> 4, t = p[0] & 3; p++; plen--; for(int i = 0; i < 64; i++){ int v = pq ? (p[i*2] << 8) | p[i*2+1] : p[i]; j->qt[t][ZZ[i]] = (unsigned short)v; } p += pq ? 128 : 64; plen -= pq ? 128 : 64; }
    } else if(m == 0xC0 || m == 0xC1 || m == 0xC2){
      if(got_sof || plen < 6 || p[0] != 8) goto fail;
      j->progressive = m == 0xC2; j->h = (p[1] << 8) | p[2]; j->w = (p[3] << 8) | p[4]; j->nc = p[5];
      if(j->nc != 1 && j->nc != 3) goto fail;
      if(!j->w || !j->h || (long long)j->w * j->h > IMG_MAXPIX) goto fail;
      for(int i = 0; i < j->nc; i++){ struct jcomp *c = &j->c[i]; c->id = p[6+i*3]; c->h = p[7+i*3] >> 4; c->v = p[7+i*3] & 15; c->tq = p[8+i*3] & 3;
        if(c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) goto fail; if(c->h > j->hmax) j->hmax = c->h; if(c->v > j->vmax) j->vmax = c->v; }
      j->mcux = (j->w + j->hmax * 8 - 1) / (j->hmax * 8); j->mcuy = (j->h + j->vmax * 8 - 1) / (j->vmax * 8);
      for(int i = 0; i < j->nc; i++){ struct jcomp *c = &j->c[i]; c->bw = j->mcux * c->h; c->bh = j->mcuy * c->v; c->coef = calloc((size_t)c->bw * c->bh * 64, sizeof(short)); if(!c->coef) goto fail; }
      got_sof = 1;
    } else if(m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8 && m != 0xCC)) goto fail;       /* lossless / arithmetic: unsupported */
    else if(m == 0xC4){
      while(plen >= 17){ int tc = p[0] >> 4, th = p[0] & 3; struct jhuff *h = tc ? &j->hac[th] : &j->hdc[th]; int cnt = 0; h->bits[0] = 0; for(int i = 1; i <= 16; i++){ h->bits[i] = p[i]; cnt += p[i]; }
        if(cnt > 256 || 17 + cnt > plen) goto fail; memcpy(h->vals, p + 17, cnt); jhuff_build(h); p += 17 + cnt; plen -= 17 + cnt; }
    } else if(m == 0xDD){ j->restart = (p[0] << 8) | p[1]; }
    else if(m == 0xEE && plen >= 12 && !memcmp(p, "Adobe", 5)) j->adobe = 1 + p[11];
    else if(m == 0xDA){
      if(!got_sof) goto fail;
      int ns = p[0]; int comps[4]; if(ns < 1 || ns > 4 || plen < 1 + ns * 2 + 3) goto fail;
      for(int i = 0; i < ns; i++){ int cid = p[1 + i*2], found = -1; for(int k = 0; k < j->nc; k++) if(j->c[k].id == cid) found = k; if(found < 0) goto fail;
        comps[i] = found; j->c[found].td = p[2 + i*2] >> 4; j->c[found].ta = p[2 + i*2] & 15; if(j->c[found].td > 3 || j->c[found].ta > 3) goto fail; }
      int ss = p[1 + ns*2], se = p[2 + ns*2], ah = p[3 + ns*2] >> 4, al = p[3 + ns*2] & 15;
      if(!j->progressive){ ss = 0; se = 63; ah = al = 0; }
      if(se > 63 || ss > se || al > 13) goto fail;
      j->pos += 2 + len;
      j_scan(j, comps, ns, ss, se, ah, al);
      /* skip to next marker */
      while(j->pos + 1 < n && !(d[j->pos] == 0xFF && d[j->pos+1] != 0 && !(d[j->pos+1] >= 0xD0 && d[j->pos+1] <= 0xD7) && d[j->pos+1] != 0xFF)) j->pos++;
      continue;
    }
    j->pos += 2 + len;
  }
  if(!got_sof) goto fail;
  im = img_new(j->w, j->h); if(!im) goto fail;
  {
    /* IDCT every block into per-component planes */
    unsigned char *plane[4] = {0,0,0,0}; int pstride[4];
    for(int i = 0; i < j->nc; i++){
      struct jcomp *c = &j->c[i]; pstride[i] = c->bw * 8; plane[i] = malloc((size_t)c->bw * 8 * c->bh * 8);
      if(!plane[i]){ for(int k = 0; k < i; k++) free(plane[k]); img_free(im); im = 0; goto fail; }
      for(int by = 0; by < c->bh; by++) for(int bx = 0; bx < c->bw; bx++){
        short blk[64]; const short *src = c->coef + ((size_t)by * c->bw + bx) * 64; const unsigned short *q = j->qt[c->tq];
        for(int k = 0; k < 64; k++) blk[k] = (short)(src[k] * q[k]);
        j_idct(blk, plane[i] + (size_t)by * 8 * pstride[i] + bx * 8, pstride[i]);
      }
    }
    for(int y = 0; y < j->h; y++) for(int x = 0; x < j->w; x++){
      int v[4];
      for(int i = 0; i < j->nc; i++){ struct jcomp *c = &j->c[i]; int cx = x * c->h / j->hmax, cy = y * c->v / j->vmax; v[i] = plane[i][(size_t)cy * pstride[i] + cx]; }
      int r, g, b;
      if(j->nc == 1) r = g = b = v[0];
      else {
        int Y = v[0], cb = v[1] - 128, cr = v[2] - 128;
        r = Y + ((91881 * cr) >> 16); g = Y - ((22554 * cb + 46802 * cr) >> 16); b = Y + ((116130 * cb) >> 16);
        r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
      }
      im->px[(size_t)y * j->w + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    for(int i = 0; i < j->nc; i++) free(plane[i]);
  }
fail:
  for(int i = 0; i < 4; i++) free(j->c[i].coef);
  free(j); return im;
}

/* ---------------- GIF (first frame) ---------------- */
static wimg *gif_decode(const unsigned char *d, int n){
  if(n < 13 || memcmp(d, "GIF8", 4)) return 0;
  int w = d[6] | (d[7] << 8), h = d[8] | (d[9] << 8), flags = d[10], pos = 13;
  unsigned gct[256]; int gn = 0;
  if(flags & 0x80){ gn = 2 << (flags & 7); for(int i = 0; i < gn && pos + 3 <= n; i++, pos += 3) gct[i] = 0xFF000000u | (d[pos] << 16) | (d[pos+1] << 8) | d[pos+2]; }
  wimg *im = img_new(w, h); if(!im) return 0;
  int transp = -1;
  while(pos < n){
    int b = d[pos++];
    if(b == 0x21){
      int lab = pos < n ? d[pos++] : 0;
      if(lab == 0xF9 && pos + 5 < n && d[pos] >= 4){ if(d[pos+1] & 1) transp = d[pos+4]; }
      while(pos < n){ int sz = d[pos++]; if(!sz) break; pos += sz; }
    } else if(b == 0x2C){
      if(pos + 9 > n) break;
      int ix = d[pos] | (d[pos+1] << 8), iy = d[pos+2] | (d[pos+3] << 8), iw = d[pos+4] | (d[pos+5] << 8), ih = d[pos+6] | (d[pos+7] << 8), lf = d[pos+8]; pos += 9;
      unsigned lct[256]; unsigned *ct = gct; int cn = gn;
      if(lf & 0x80){ cn = 2 << (lf & 7); for(int i = 0; i < cn && pos + 3 <= n; i++, pos += 3) lct[i] = 0xFF000000u | (d[pos] << 16) | (d[pos+1] << 8) | d[pos+2]; ct = lct; }
      if(pos >= n) break;
      int mincode = d[pos++]; if(mincode < 2 || mincode > 11) break;
      /* collect sub-blocks */
      unsigned char *data = malloc(n - pos + 1); if(!data) break; int dl = 0;
      while(pos < n){ int sz = d[pos++]; if(!sz) break; if(pos + sz > n) sz = n - pos; memcpy(data + dl, d + pos, sz); dl += sz; pos += sz; }
      int clear = 1 << mincode, eoi = clear + 1, next = eoi + 1, size = mincode + 1;
      static unsigned short prefix[4096]; static unsigned char suffix[4096], stack[4097];
      int prev = -1, bits = 0, nb = 0, dp = 0; int x = 0, y = 0, pass = 0, interlaced = (lf & 0x40) != 0; unsigned char first = 0;
      for(int i = 0; i < clear; i++){ prefix[i] = 0xFFFF; suffix[i] = (unsigned char)i; }
      for(;;){
        while(nb < size && dp < dl){ bits |= data[dp++] << nb; nb += 8; }
        if(nb < size) break;
        int code = bits & ((1 << size) - 1); bits >>= size; nb -= size;
        if(code == clear){ next = eoi + 1; size = mincode + 1; prev = -1; continue; }
        if(code == eoi) break;
        int sp = 0, cur = code;
        if(prev < 0){ if(code >= clear) break; first = suffix[code]; }
        else {
          if(code >= next){ if(code > next) break; stack[sp++] = first; cur = prev; }
          while(cur >= clear && sp < 4096){ stack[sp++] = suffix[cur]; cur = prefix[cur]; }
          first = suffix[cur];
          if(next < 4096){ prefix[next] = (unsigned short)prev; suffix[next] = first; next++; if(next == (1 << size) && size < 12) size++; }
        }
        stack[sp++] = suffix[cur < clear ? cur : cur]; /* base symbol */
        prev = code;
        while(sp){
          int idx = stack[--sp];
          if(y < ih){
            int px = ix + x, py = iy + y;
            if(px < w && py < h && idx != transp && idx < cn) im->px[(size_t)py * w + px] = ct[idx];
          }
          if(++x >= iw){ x = 0;
            if(interlaced){ static const int st[4] = {8,8,4,2}, sy0[4] = {0,4,2,1}; y += st[pass]; if(y >= ih && pass < 3){ pass++; y = sy0[pass]; } }
            else y++; }
        }
      }
      free(data); return im;                                     /* first frame only */
    } else if(b == 0x3B) break;
    else break;
  }
  return im;
}

static wimg *img_decode(const unsigned char *d, int n){
  if(n > 8 && d[0] == 0x89 && d[1] == 'P') return png_decode(d, n);
  if(n > 4 && d[0] == 0xFF && d[1] == 0xD8) return jpeg_decode(d, n);
  if(n > 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') return gif_decode(d, n);
  return 0;
}
#endif
