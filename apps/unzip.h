/* unzip for the Terminal: a small DEFLATE decoder (RFC 1951) plus a reader for .zip files (stored and deflated entries, no encryption, no zip64).
   Entries are read one at a time from the file, so the archive itself never has to fit in memory twice. */

struct inf { const unsigned char *in; unsigned long inlen, incnt; unsigned bitbuf; int bitcnt; unsigned char *out; unsigned long outlen, outcnt; int err; };
struct huff { short count[16]; short symbol[288]; };

static unsigned inf_bits(struct inf *s, int need){
  unsigned long val = s->bitbuf;
  while(s->bitcnt < need){ if(s->incnt >= s->inlen){ s->err = 1; return 0; } val |= (unsigned long)s->in[s->incnt++] << s->bitcnt; s->bitcnt += 8; }
  s->bitbuf = (unsigned)(val >> need); s->bitcnt -= need; return (unsigned)(val & ((1UL << need) - 1));
}
static int inf_decode(struct inf *s, const struct huff *h){
  int code = 0, first = 0, index = 0;
  for(int len = 1; len <= 15; len++){
    code |= (int)inf_bits(s, 1); if(s->err) return -1;
    int count = h->count[len];
    if(code - count < first) return h->symbol[index + (code - first)];
    index += count; first += count; first <<= 1; code <<= 1;
  }
  return -1;
}
static int inf_construct(struct huff *h, const short *length, int n){
  for(int i = 0; i <= 15; i++) h->count[i] = 0;
  for(int i = 0; i < n; i++) h->count[length[i]]++;
  if(h->count[0] == n) return 0;
  int left = 1; for(int len = 1; len <= 15; len++){ left <<= 1; left -= h->count[len]; if(left < 0) return left; }
  short offs[16]; offs[1] = 0; for(int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
  for(int i = 0; i < n; i++) if(length[i]) h->symbol[offs[length[i]]++] = (short)i;
  return left;
}
static int inf_codes(struct inf *s, const struct huff *lc, const struct huff *dc){
  static const short lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
  static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
  static const short dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
  static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
  for(;;){
    int sym = inf_decode(s, lc); if(sym < 0) return -1;
    if(sym < 256){ if(s->outcnt >= s->outlen) return -1; s->out[s->outcnt++] = (unsigned char)sym; }
    else if(sym == 256) return 0;
    else {
      sym -= 257; if(sym >= 29) return -1;
      unsigned len = (unsigned)lens[sym] + inf_bits(s, lext[sym]);
      int ds = inf_decode(s, dc); if(ds < 0 || ds >= 30) return -1;
      unsigned dist = (unsigned)dists[ds] + inf_bits(s, dext[ds]);
      if(s->err || dist > s->outcnt || s->outcnt + len > s->outlen) return -1;
      for(unsigned i = 0; i < len; i++){ s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
    }
  }
}
/* returns the number of bytes written to out, or -1 on a damaged stream */
static long inflate_raw(const unsigned char *in, unsigned long inlen, unsigned char *out, unsigned long outlen){
  struct inf s = {in, inlen, 0, 0, 0, out, outlen, 0, 0};
  static struct huff lc, dc, fl, fd; static int fixed_ready;
  int last;
  do {
    last = (int)inf_bits(&s, 1); int type = (int)inf_bits(&s, 2); if(s.err) return -1;
    if(type == 0){
      s.bitbuf = 0; s.bitcnt = 0;
      if(s.incnt + 4 > s.inlen) return -1;
      unsigned len = in[s.incnt] | (in[s.incnt + 1] << 8); s.incnt += 4;
      if(s.incnt + len > s.inlen || s.outcnt + len > s.outlen) return -1;
      memcpy(out + s.outcnt, in + s.incnt, len); s.incnt += len; s.outcnt += len;
    } else if(type == 1){
      if(!fixed_ready){ short l[288]; int i = 0; for(; i < 144; i++) l[i] = 8; for(; i < 256; i++) l[i] = 9; for(; i < 280; i++) l[i] = 7; for(; i < 288; i++) l[i] = 8; inf_construct(&fl, l, 288); for(i = 0; i < 30; i++) l[i] = 5; inf_construct(&fd, l, 30); fixed_ready = 1; }
      if(inf_codes(&s, &fl, &fd)) return -1;
    } else if(type == 2){
      static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
      short lengths[320]; int nlen = (int)inf_bits(&s, 5) + 257, ndist = (int)inf_bits(&s, 5) + 1, ncode = (int)inf_bits(&s, 4) + 4;
      if(s.err || nlen > 286 || ndist > 30) return -1;
      int i = 0; for(; i < ncode; i++) lengths[order[i]] = (short)inf_bits(&s, 3); for(; i < 19; i++) lengths[order[i]] = 0;
      if(inf_construct(&lc, lengths, 19) < 0) return -1;
      i = 0;
      while(i < nlen + ndist){
        int sym = inf_decode(&s, &lc); if(sym < 0) return -1;
        if(sym < 16) lengths[i++] = (short)sym;
        else {
          int rep, val = 0;
          if(sym == 16){ if(i == 0) return -1; val = lengths[i - 1]; rep = 3 + (int)inf_bits(&s, 2); }
          else if(sym == 17) rep = 3 + (int)inf_bits(&s, 3); else rep = 11 + (int)inf_bits(&s, 7);
          if(i + rep > nlen + ndist) return -1;
          while(rep--) lengths[i++] = (short)val;
        }
      }
      if(lengths[256] == 0) return -1;
      if(inf_construct(&lc, lengths, nlen) < 0 || inf_construct(&dc, lengths + nlen, ndist) < 0) return -1;
      if(inf_codes(&s, &lc, &dc)) return -1;
    } else return -1;
  } while(!last);
  return (long)s.outcnt;
}

/* ---- the .zip container ---- */
static unsigned zip_u16(const unsigned char *p){ return p[0] | (p[1] << 8); }
static unsigned zip_u32(const unsigned char *p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static int zip_read_at(HANDLE f, unsigned off, void *buf, unsigned n){
  API(kernel32, SetFilePointer)(f, (int)off, 0, 0); unsigned got = 0;
  while(got < n){ DWORD r = 0; DWORD want = n - got > (1u << 20) ? (1u << 20) : n - got; if(!API(kernel32, ReadFile)(f, (char *)buf + got, want, &r, 0) || !r) return 0; got += r; }
  return 1;
}
static void zip_make_dirs(const char *path){                    /* create every directory in front of the last '/' */
  char t[260]; int n = (int)strlen(path); if(n > 259) n = 259; memcpy(t, path, (size_t)n); t[n] = 0;
  for(int i = 1; i < n; i++) if(t[i] == '/'){ t[i] = 0; API(kernel32, CreateDirectoryA)(t, 0); t[i] = '/'; }
}
/* returns the number of files extracted, or -1 when the archive cannot be read */
static int unzip_file(const char *zipname, const char *destdir, void (*say)(const char *, ...), void (*batch)(int)){
  HANDLE f = API(kernel32, CreateFileA)(zipname, 0x80000000u, 1, 0, 3, 0x80, 0);
  if(f == (HANDLE)~0ull || !f){ say("unzip: cannot open %s\n", zipname); return -1; }
  DWORD size = API(kernel32, GetFileSize)(f, 0);
  unsigned tail = size < 65557 ? size : 65557; unsigned char *tb = malloc(tail);
  if(!tb || !zip_read_at(f, size - tail, tb, tail)){ say("unzip: cannot read %s\n", zipname); free(tb); API(kernel32, CloseHandle)(f); return -1; }
  int e = -1; for(int i = (int)tail - 22; i >= 0; i--) if(zip_u32(tb + i) == 0x06054b50u){ e = i; break; }
  if(e < 0){ say("unzip: %s is not a zip file\n", zipname); free(tb); API(kernel32, CloseHandle)(f); return -1; }
  unsigned count = zip_u16(tb + e + 10), cdsize = zip_u32(tb + e + 12), cdoff = zip_u32(tb + e + 16); free(tb);
  if(cdoff == 0xFFFFFFFFu || count == 0xFFFF){ say("unzip: zip64 archives are not supported\n"); API(kernel32, CloseHandle)(f); return -1; }
  unsigned char *cd = malloc(cdsize + 1);
  if(!cd || !zip_read_at(f, cdoff, cd, cdsize)){ say("unzip: damaged directory\n"); free(cd); API(kernel32, CloseHandle)(f); return -1; }
  if(batch) batch(1);
  int files = 0, skipped = 0; unsigned p = 0;
  for(unsigned n = 0; n < count && p + 46 <= cdsize; n++){
    const unsigned char *h = cd + p; if(zip_u32(h) != 0x02014b50u) break;
    unsigned method = zip_u16(h + 10), csize = zip_u32(h + 20), usize = zip_u32(h + 24), nl = zip_u16(h + 28), el = zip_u16(h + 30), cl = zip_u16(h + 32), lho = zip_u32(h + 42), flags = zip_u16(h + 8);
    char name[300]; unsigned k = 0; if(destdir && *destdir){ k = (unsigned)snprintf(name, sizeof name, "%s/", destdir); }
    for(unsigned i = 0; i < nl && k < sizeof name - 2; i++){ char c = (char)h[46 + i]; name[k++] = c == '\\' ? '/' : c; }
    name[k] = 0; p += 46 + nl + el + cl;
    if(strstr(name, "../")) { skipped++; continue; }
    if(k && name[k - 1] == '/'){ name[k - 1] = 0; zip_make_dirs(name); API(kernel32, CreateDirectoryA)(name, 0); continue; }
    if((flags & 1) || (method != 0 && method != 8)){ say("unzip: skipping %s (unsupported)\n", name); skipped++; continue; }
    unsigned char lh[30]; if(!zip_read_at(f, lho, lh, 30) || zip_u32(lh) != 0x04034b50u){ say("unzip: damaged entry %s\n", name); skipped++; continue; }
    unsigned dataoff = lho + 30 + zip_u16(lh + 26) + zip_u16(lh + 28);
    unsigned char *cb = malloc(csize ? csize : 1), *ub = (method == 8 && usize) ? malloc(usize) : 0;
    if(!cb || (method == 8 && usize && !ub) || !zip_read_at(f, dataoff, cb, csize)){ say("unzip: out of memory for %s (%u KB)\n", name, usize / 1024); free(cb); free(ub); skipped++; continue; }
    const unsigned char *data = cb; unsigned dlen = csize;
    if(method == 8){ long r = usize ? inflate_raw(cb, csize, ub, usize) : 0; if(r < 0 || (unsigned)r != usize){ say("unzip: %s is damaged\n", name); free(cb); free(ub); skipped++; continue; } data = ub; dlen = usize; }
    zip_make_dirs(name);
    HANDLE o = API(kernel32, CreateFileA)(name, 0x40000000u, 0, 0, 2, 0x80, 0); int ok = o != (HANDLE)~0ull && o;
    if(ok && dlen > (1u << 20)){ API(kernel32, SetFilePointer)(o, (int)dlen, 0, 0); API(kernel32, SetEndOfFile)(o); API(kernel32, SetFilePointer)(o, 0, 0, 0); }
    unsigned w = 0; if(ok){ while(w < dlen){ DWORD c = dlen - w > 65536 ? 65536 : dlen - w, r = 0; if(!API(kernel32, WriteFile)(o, data + w, c, &r, 0) || !r) break; w += r; } API(kernel32, CloseHandle)(o); }
    if(!ok || w != dlen){ say("unzip: cannot write %s\n", name); skipped++; } else files++;
    free(cb); free(ub);
    if(files % 100 == 0 && files) say("  ... %d files\n", files);
  }
  free(cd); API(kernel32, CloseHandle)(f);
  if(batch) batch(0);
  if(skipped) say("unzip: %d entries skipped\n", skipped);
  return files;
}
