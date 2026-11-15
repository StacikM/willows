#include "kernel.h"
/* DirectWrite (dwrite.dll): the font system that Firefox, Chromium and modern Windows programs use.
   Fonts are the .ttf/.otf/.ttc files in C:\Windows\Fonts (files/Windows/Fonts in the source tree) plus fonts that programs load
   themselves through their own IDWriteFontFileLoader (web fonts). TrueType parsing and glyph rasterising use stb_truetype
   (tools/host/stb_truetype.h, public domain). Implemented: the factory, the system font collection (with common Windows family
   names mapped onto the fonts that exist: "Segoe UI", "Arial", ... -> Open Sans, "Consolas", "Courier New" -> Hack), families,
   fonts, font faces (metrics, glyph indices and metrics, raw OpenType tables, glyph outlines), font files and loaders, localized
   strings, rendering parameters, GDI interop and glyph-run analysis (anti-aliased alpha textures). Text layout and analysis
   (IDWriteTextLayout, IDWriteTextAnalyzer) are not implemented: Firefox and Chromium shape text with HarfBuzz instead.
   COM objects are C structs whose first field is the vtable; every method uses the Microsoft calling convention.
   Built with SSE (see the Makefile) because the rasteriser uses floating point. */
typedef u32 DWORD; typedef i32 BOOL; typedef u16 wc; typedef i32 HRESULT; typedef short i16; typedef u64 size_t;
#define M WINAPI
#define S_OK 0
#define S_FALSE 1
#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_POINTER ((HRESULT)0x80004003)
#define E_FAIL ((HRESULT)0x80004005)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define DWRITE_E_FILEFORMAT ((HRESULT)0x88985000)
#define DWRITE_E_FILENOTFOUND ((HRESULT)0x88985003)

/* ---- small math (the kernel's libm functions use the Microsoft convention; this file calls none of them) ---- */
static double dw_floor(double x){ long long i=(long long)x; double r=(double)i; return r>x? r-1 : r; }
static double dw_ceil(double x){ long long i=(long long)x; double r=(double)i; return r<x? r+1 : r; }
static double dw_sqrt(double x){ double r; __asm__("sqrtsd %1,%0":"=x"(r):"x"(x)); return r; }
static double dw_fabs(double x){ return x<0? -x : x; }
static double dw_fmod(double x, double y){ return x-y*(double)(long long)(x/y); }
static double dw_cos(double x){ x=dw_fmod(x,6.283185307179586); double x2=x*x, t=1, s=1; for(int i=1;i<12;i++){ t*=-x2/((2*i-1)*(2*i)); s+=t; } return s; }
static double dw_acos(double x){ double lo=0, hi=3.141592653589793; for(int i=0;i<50;i++){ double m=(lo+hi)/2; if(dw_cos(m)>x) lo=m; else hi=m; } return (lo+hi)/2; }
static double dw_pow(double b, double e){ if(e==0.5) return dw_sqrt(b); double r=1; long long n=(long long)e; for(long long i=0;i<n;i++) r*=b; return r; }   /* only SDF code (unused) needs pow */
#define STBTT_ifloor(x) ((int)dw_floor(x))
#define STBTT_iceil(x) ((int)dw_ceil(x))
#define STBTT_sqrt(x) dw_sqrt(x)
#define STBTT_pow(x,y) dw_pow(x,y)
#define STBTT_fmod(x,y) dw_fmod(x,y)
#define STBTT_cos(x) dw_cos(x)
#define STBTT_acos(x) dw_acos(x)
#define STBTT_fabs(x) dw_fabs(x)
#define STBTT_malloc(x,u) ((void)(u),heap_alloc(x))
#define STBTT_free(x,u) ((void)(u),heap_free(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_strlen(x) strlen(x)
#define STBTT_memcpy memcpy
#define STBTT_memset memset
#ifndef NULL
#define NULL ((void*)0)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../tools/host/stb_truetype.h"
#pragma GCC diagnostic pop

/* ---- GUIDs ---- */
struct guid { u32 a; u16 b, c; u8 d[8]; };
static const struct guid IID_IUnknown={0x00000000,0x0000,0x0000,{0xC0,0,0,0,0,0,0,0x46}};
static const struct guid IID_Factory={0xb859ee5a,0xd838,0x4b5b,{0xa2,0xe8,0x1a,0xdc,0x7d,0x93,0xdb,0x48}};
static const struct guid IID_Collection={0xa84cee02,0x3eea,0x4eee,{0xa8,0x27,0x87,0xc1,0xa0,0x2a,0x0f,0xcc}};
static const struct guid IID_Family={0xda20d8ef,0x812a,0x4c43,{0x98,0x02,0x62,0xec,0x4a,0xbd,0x7a,0xdd}};
static const struct guid IID_FontList={0x1a0d8438,0x1d97,0x4ec1,{0xae,0xf9,0xa2,0xfb,0x86,0xed,0x6a,0xcb}};
static const struct guid IID_Font={0xacd16696,0x8c14,0x4f5d,{0x87,0x7e,0xfe,0x3f,0xc1,0xd3,0x27,0x37}};
static const struct guid IID_FontFace={0x5f49804d,0x7024,0x4d43,{0xbf,0xa9,0xd2,0x59,0x84,0xf5,0x38,0x49}};
static const struct guid IID_FontFile={0x739d886a,0xcef5,0x47dc,{0x87,0x69,0x1a,0x8b,0x41,0xbe,0xbb,0xb0}};
static const struct guid IID_FileLoader={0x727cad4e,0xd6af,0x4c9e,{0x8a,0x08,0xd6,0x95,0xb1,0x1c,0xaa,0x49}};
static const struct guid IID_LocalLoader={0xb2d9f3ec,0xc9fe,0x4a11,{0xa2,0xec,0xd8,0x62,0x08,0xf7,0xc0,0xa2}};
static const struct guid IID_FileStream={0x6d4865fe,0x0ab8,0x4d91,{0x8f,0x62,0x5d,0xd6,0xbe,0x34,0xa3,0xe0}};
static const struct guid IID_Strings={0x08256209,0x099a,0x4b34,{0xb8,0x6d,0xc2,0x2b,0x11,0x0e,0x77,0x71}};
static const struct guid IID_Params={0x2f0da53a,0x2add,0x47cd,{0x82,0xee,0xd9,0xec,0x34,0x68,0x8e,0x75}};
static const struct guid IID_Analysis={0x7d97dbf7,0xe085,0x42d4,{0x81,0xe3,0x6a,0x88,0x3b,0xde,0xd1,0x18}};
static const struct guid IID_GdiInterop={0x1edd9491,0x9853,0x4299,{0x89,0x8f,0x64,0x32,0x98,0x3b,0x6f,0x3a}};
static int guid_eq(const struct guid *a, const struct guid *b){ return !memcmp(a,b,16); }
static void guid_log(const char *what, const struct guid *g){
  kdbg("  [dwrite] %s: interface %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x not supported\n",what,g->a,g->b,g->c,g->d[0],g->d[1],g->d[2],g->d[3],g->d[4],g->d[5],g->d[6],g->d[7]);
}

/* ---- COM plumbing ---- */
typedef HRESULT (M *fn_t)();
struct com { void **vt; i32 refs; int fixed; };                      /* fixed: lives forever (the system collection and what it owns) */
static u32 M com_addref(struct com *o){ return (u32)++o->refs; }
static u32 M com_release(struct com *o){ if(o->fixed) return 1; i32 r=--o->refs; if(r<=0) heap_free(o); return (u32)(r>0? r : 0); }
static HRESULT qi(struct com *o, const struct guid *want, const struct guid *mine, const struct guid *mine2, void **out, const char *name){
  if(!out) return E_POINTER;
  if(guid_eq(want,&IID_IUnknown) || guid_eq(want,mine) || (mine2 && guid_eq(want,mine2))){ com_addref(o); *out=o; return S_OK; }
  guid_log(name,want); *out=0; return E_NOINTERFACE;
}
static HRESULT M not_impl(void){ return E_NOTIMPL; }
static void *obj_new(void **vt, u64 size){ struct com *o=heap_alloc(size); if(!o) return 0; o->vt=vt; o->refs=1; return o; }
static void addref_out(void **out, void *o){ if(o) com_addref(o); *out=o; }
/* calling another program's COM object (a custom font file loader and its stream) */
#define VCALL(obj,idx,type) ((type)(((void***)(obj))[0][idx]))

/* ---- font data ---- */
struct font {                                                         /* one face of one font file */
  u8 *data; u64 size; int index, ok; stbtt_fontinfo info; u32 offset;
  wc family[64], style[64], full[96], psname[64]; u32 weight, stretch, slant; int symbol;
  u16 upem; i16 ascent, descent, linegap, capheight, xheight, ulpos, ulsize, stpos, stsize;
  char path[128];                                                     /* the file in Willows' file system ("" for memory fonts) */
  void *file_key; u32 key_size; struct com *loader;                   /* a font from a program's own loader */
};
static u16 be16(const u8 *p){ return (u16)(p[0]<<8|p[1]); }
static u32 be32(const u8 *p){ return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3]; }
static const u8 *table(const struct font *f, const char *tag, u32 *len){
  const u8 *d=f->data+f->offset; u16 n=be16(d+4);
  for(u16 i=0;i<n;i++){ const u8 *e=d+12+16*i; if(!memcmp(e,tag,4)){ u32 o=be32(e+8), l=be32(e+12); if((u64)o+l>f->size) return 0; if(len) *len=l; return f->data+o; } }
  return 0;
}
static int name_entry(const struct font *f, int id, wc *out, int max){  /* Windows Unicode English name, else any Unicode/Mac Roman */
  u32 len; const u8 *t=table(f,"name",&len); if(!t||len<6) return 0; u16 cnt=be16(t+2), so=be16(t+4); int best=-1, score=0;
  for(u16 i=0;i<cnt;i++){ const u8 *r=t+6+12*i; if(be16(r+6)!=id) continue; u16 pid=be16(r), lid=be16(r+4); int s= pid==3&&lid==0x409? 4 : pid==3? 3 : pid==0? 2 : pid==1&&lid==0? 1 : 0; if(s>score){ score=s; best=i; } }
  if(best<0) return 0; const u8 *r=t+6+12*best; u16 pid=be16(r), l=be16(r+8), o=be16(r+10); const u8 *s=t+so+o; int n=0;
  if(pid==1) for(u16 k=0;k<l && n<max-1;k++) out[n++]=s[k]; else for(u16 k=0;k+1<l && n<max-1;k+=2) out[n++]=be16(s+k);
  out[n]=0; return n;
}
static int load_font(struct font *f, u8 *data, u64 size, int index){
  memset(f,0,sizeof *f); f->data=data; f->size=size; f->index=index;
  int off=stbtt_GetFontOffsetForIndex(data,index); if(off<0) return 0; f->offset=(u32)off;
  if(!stbtt_InitFont(&f->info,data,off)) return 0;
  if(!name_entry(f,16,f->family,64) && !name_entry(f,1,f->family,64)) return 0;
  if(!name_entry(f,17,f->style,64) && !name_entry(f,2,f->style,64)){ f->style[0]='R'; f->style[1]=0; }
  name_entry(f,4,f->full,96); name_entry(f,6,f->psname,64);
  u32 l; const u8 *head=table(f,"head",&l), *hhea=table(f,"hhea",0), *os2=table(f,"OS/2",&l), *post=table(f,"post",0);
  f->upem= head? be16(head+18) : 1000; if(!f->upem) f->upem=1000;
  f->weight=400; f->stretch=5; f->slant=0;
  if(hhea){ f->ascent=(i16)be16(hhea+4); f->descent=(i16)-(i16)be16(hhea+6); f->linegap=(i16)be16(hhea+8); }
  if(os2){ f->weight=be16(os2+4); f->stretch=be16(os2+6); u16 sel=be16(os2+62); f->slant= (sel&1)? 2 : (sel&0x200)? 1 : 0;
    f->stsize=(i16)be16(os2+26); f->stpos=(i16)be16(os2+28); f->ascent=(i16)be16(os2+74); f->descent=(i16)be16(os2+76);
    if(be16(os2)>=2 && l>=90){ f->xheight=(i16)be16(os2+86); f->capheight=(i16)be16(os2+88); } }
  else if(head && (be16(head+44)&2)) f->slant=2;
  if(!f->weight) f->weight=400; if(!f->stretch||f->stretch>9) f->stretch=5;
  if(!f->capheight) f->capheight=(i16)(f->ascent*7/10); if(!f->xheight) f->xheight=(i16)(f->upem/2);
  if(post){ f->ulpos=(i16)be16(post+8); f->ulsize=(i16)be16(post+10); } if(!f->ulsize) f->ulsize=(i16)(f->upem/14);
  if(!f->stsize) f->stsize=f->ulsize; if(!f->stpos) f->stpos=(i16)(f->xheight/2);
  f->ok=1; return 1;
}
static int wcmp_ci(const wc *a, const wc *b){ for(;;a++,b++){ u32 x=*a, y=*b; if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32; if(x!=y) return (int)x-(int)y; if(!x) return 0; } }
static void a2w(const char *a, wc *w, int max){ int i=0; for(;a[i]&&i<max-1;i++) w[i]=(u8)a[i]; w[i]=0; }
static int wlen_(const wc *s){ int n=0; while(s[n]) n++; return n; }

/* ---- the system font collection ---- */
#define MAXFONTS 64
#define MAXFAM 48
struct family { struct com c; wc name[64]; int fonts[16], nfonts; };
struct dwfont { struct com c; int idx; struct family *fam; };      /* an IDWriteFont: one face in one family */
static struct font fonts[MAXFONTS]; static int nfonts;
static struct family fams[MAXFAM]; static int nfams;
static struct dwfont fontobjs[MAXFAM][16];
static struct com sys_coll; static int sys_ready;
static void **vt_family, **vt_font, **vt_coll;
static struct family *add_family(const wc *name){
  for(int i=0;i<nfams;i++) if(!wcmp_ci(fams[i].name,name)) return &fams[i];
  if(nfams>=MAXFAM) return 0; struct family *f=&fams[nfams++]; memset(f,0,sizeof *f); f->c.vt=vt_family; f->c.refs=1; f->c.fixed=1;
  int k=0; for(;name[k]&&k<63;k++) f->name[k]=name[k]; f->name[k]=0; return f;
}
static void add_font_to(struct family *fam, int idx){ if(fam && fam->nfonts<16){ int j=fam->nfonts++; fam->fonts[j]=idx; struct dwfont *o=&fontobjs[fam-fams][j]; o->c.vt=vt_font; o->c.refs=1; o->c.fixed=1; o->idx=idx; o->fam=fam; } }
/* common Windows family names, served by the fonts that exist (like Windows' own font substitutes) */
static const char *const sans_alias[]={"Segoe UI","Arial","Helvetica","Tahoma","Verdana","Calibri","Microsoft Sans Serif","MS Shell Dlg","MS Shell Dlg 2","Times New Roman","Georgia","Cambria","Segoe UI Symbol","Segoe UI Emoji","Malgun Gothic","Yu Gothic","Microsoft YaHei","Nirmala UI","Gadugi","Ebrima","Leelawadee UI"};
static const char *const mono_alias[]={"Consolas","Courier New","Lucida Console","Cascadia Mono","Cascadia Code"};
static void scan_fonts(void){
  if(sys_ready) return; sys_ready=1;
  struct ramfile *ents[128]; int n=fs_list("/Windows/Fonts",ents,128);
  for(int i=0;i<n && nfonts<MAXFONTS;i++){
    struct ramfile *rf=ents[i]; if(rf->isdir) continue; const char *nm=fs_leaf(rf); int l=(int)strlen(nm); if(l<5) continue;
    const char *ext=nm+l-4; if(stricmp(ext,".ttf") && stricmp(ext,".otf") && stricmp(ext,".ttc")) continue;
    int faces=stbtt_GetNumberOfFonts(rf->data); if(faces<1) faces=1;
    for(int fi=0;fi<faces && nfonts<MAXFONTS;fi++){
      struct font *f=&fonts[nfonts]; if(!load_font(f,rf->data,rf->size,fi)) continue;
      unsigned k=0; for(;rf->name[k]&&k<127;k++) f->path[k]=rf->name[k]; f->path[k]=0;
      add_font_to(add_family(f->family),nfonts); nfonts++;
    }
  }
  /* aliases: every face of the base family becomes a face of the alias family too */
  static const char *const icon_alias[]={"Segoe Fluent Icons","Segoe MDL2 Assets"};      /* window caption icons (WillowsIcons.ttf, tools/mkiconfont.py) */
  static const struct { const char *base; const char *const *names; int n; int fallback; } al[]={
    {"Open Sans",sans_alias,(int)(sizeof sans_alias/sizeof *sans_alias),1}, {"Hack",mono_alias,(int)(sizeof mono_alias/sizeof *mono_alias),1},
    {"Willows Caption Icons",icon_alias,2,0} };
  for(unsigned pass=0;pass<sizeof al/sizeof *al;pass++){
    wc bn[64]; a2w(al[pass].base,bn,64);
    struct family *base=0; for(int i=0;i<nfams;i++) if(!wcmp_ci(fams[i].name,bn)) base=&fams[i];
    if(!base && nfams && al[pass].fallback) base=&fams[0]; if(!base) continue;
    int bi=(int)(base-fams);
    for(int a=0;a<al[pass].n;a++){ wc w[64]; a2w(al[pass].names[a],w,64); struct family *fa=add_family(w); if(!fa || fa->nfonts) continue;
      for(int j=0;j<fams[bi].nfonts;j++) add_font_to(fa,fams[bi].fonts[j]); }
  }
  kdbg("  [dwrite] system font collection: %d faces, %d families (with aliases)\n",nfonts,nfams);
}

/* ---- IDWriteLocalizedStrings: one en-us string ---- */
struct strings { struct com c; wc s[128]; };
static void **vt_strings;
static HRESULT M ls_qi(struct strings *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_Strings,0,out,"LocalizedStrings"); }
static u32 M ls_count(struct strings *o){ (void)o; return 1; }
static HRESULT M ls_find(struct strings *o, const wc *loc, u32 *idx, BOOL *ex){ (void)o;(void)loc; *idx=0; *ex=1; return S_OK; }
static HRESULT M ls_loclen(struct strings *o, u32 i, u32 *len){ (void)o; if(i) return E_INVALIDARG; *len=5; return S_OK; }
static HRESULT M ls_loc(struct strings *o, u32 i, wc *buf, u32 size){ (void)o; if(i||size<6) return E_INVALIDARG; a2w("en-us",buf,6); return S_OK; }
static HRESULT M ls_len(struct strings *o, u32 i, u32 *len){ if(i) return E_INVALIDARG; *len=(u32)wlen_(o->s); return S_OK; }
static HRESULT M ls_get(struct strings *o, u32 i, wc *buf, u32 size){ u32 n=(u32)wlen_(o->s); if(i||size<n+1) return E_INVALIDARG; memcpy(buf,o->s,(n+1)*2); return S_OK; }
static void *vt_strings_[]={ls_qi,com_addref,com_release,ls_count,ls_find,ls_loclen,ls_loc,ls_len,ls_get};
static struct strings *strings_of(const wc *s){ struct strings *o=obj_new(vt_strings,sizeof *o); if(!o) return 0; int k=0; for(;s[k]&&k<127;k++) o->s[k]=s[k]; o->s[k]=0; return o; }

/* ---- font files and loaders ---- */
struct dwfile { struct com c; char path[128]; struct com *loader; u8 *key; u32 keysize; };
struct stream { struct com c; u8 *data; u64 size; };
static void **vt_file, **vt_loader, **vt_stream;
static struct com local_loader;
static HRESULT M st_qi(struct stream *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_FileStream,0,out,"FontFileStream"); }
static HRESULT M st_read(struct stream *o, const void **start, u64 off, u64 n, void **ctx){ if(off+n>o->size || off+n<off) return E_FAIL; *start=o->data+off; if(ctx) *ctx=0; return S_OK; }
static void M st_rel(struct stream *o, void *ctx){ (void)o;(void)ctx; }
static HRESULT M st_size(struct stream *o, u64 *s){ *s=o->size; return S_OK; }
static HRESULT M st_time(struct stream *o, u64 *t){ (void)o; *t=0x01D0000000000000ull; return S_OK; }
static void *vt_stream_[]={st_qi,com_addref,com_release,st_read,st_rel,st_size,st_time};
/* the local loader: the reference key is the file's path in Willows' file system */
static HRESULT M ll_qi(struct com *o, const struct guid *g, void **out){ return qi(o,g,&IID_FileLoader,&IID_LocalLoader,out,"LocalFontFileLoader"); }
static HRESULT M ll_stream(struct com *o, const char *key, u32 size, struct stream **out){
  (void)o;(void)size; struct ramfile *f=fs_find(key); if(!f||f->isdir) return DWRITE_E_FILENOTFOUND;
  struct stream *s=obj_new(vt_stream,sizeof *s); if(!s) return E_OUTOFMEMORY; s->data=f->data; s->size=f->size; *out=s; return S_OK;
}
static HRESULT M ll_pathlen(struct com *o, const char *key, u32 size, u32 *len){ (void)o;(void)size; *len=(u32)strlen(key)+3; return S_OK; }   /* "C:\" + path */
static HRESULT M ll_path(struct com *o, const char *key, u32 size, wc *buf, u32 n){
  (void)o;(void)size; u32 l=(u32)strlen(key); if(n<l+4) return E_INVALIDARG; buf[0]='C'; buf[1]=':'; buf[2]='\\';
  for(u32 i=0;i<=l;i++) buf[3+i]= key[i]=='/'? '\\' : (u8)key[i]; return S_OK;
}
static HRESULT M ll_time(struct com *o, const void *key, u32 size, u64 *t){ (void)o;(void)key;(void)size; *t=0x01D0000000000000ull; return S_OK; }
static void *vt_loader_[]={ll_qi,com_addref,com_release,ll_stream,ll_pathlen,ll_path,ll_time};
static HRESULT M fi_qi(struct dwfile *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_FontFile,0,out,"FontFile"); }
static u32 M fi_release(struct dwfile *o){ if(--o->c.refs>0) return (u32)o->c.refs; if(o->loader) VCALL(o->loader,2,u32 (M*)(void*))(o->loader); if(o->key) heap_free(o->key); heap_free(o); return 0; }
static HRESULT M fi_key(struct dwfile *o, const void **key, u32 *size){ if(o->loader){ *key=o->key; *size=o->keysize; } else { *key=o->path; *size=(u32)strlen(o->path)+1; } return S_OK; }
static HRESULT M fi_loader(struct dwfile *o, void **out){ if(o->loader){ VCALL(o->loader,1,u32 (M*)(void*))(o->loader); *out=o->loader; } else addref_out(out,&local_loader); return S_OK; }
static int file_data(struct dwfile *f, u8 **data, u64 *size);
static HRESULT M fi_analyze(struct dwfile *o, BOOL *ok, u32 *ftype, u32 *ftype2, u32 *faces){
  u8 *d; u64 n; if(!file_data(o,&d,&n) || n<12){ *ok=0; return S_OK; }
  int ttc=!memcmp(d,"ttcf",4), cff=!memcmp(d,"OTTO",4); *ok=1;
  if(ftype) *ftype= ttc? 3 : cff? 1 : 2;                                  /* DWRITE_FONT_FILE_TYPE: TRUETYPE_COLLECTION, CFF, TRUETYPE */
  if(ftype2) *ftype2= ttc? 2 : cff? 0 : 1; if(faces) *faces= ttc? (u32)stbtt_GetNumberOfFonts(d) : 1; return S_OK;
}
static void *vt_file_[]={fi_qi,com_addref,fi_release,fi_key,fi_loader,fi_analyze};
static struct dwfile *file_for_path(const char *path){ struct dwfile *f=obj_new(vt_file,sizeof *f); if(!f) return 0; unsigned k=0; for(;path[k]&&k<127;k++) f->path[k]=path[k]; f->path[k]=0; return f; }
/* the bytes of a font file: straight from the file system, or read whole through the program's loader (and kept) */
static struct { void *loader; u8 *key; u32 ks; u8 *data; u64 size; } cache[64]; static int ncache;
static int file_data(struct dwfile *f, u8 **data, u64 *size){
  if(!f->loader){ struct ramfile *r=fs_find(f->path); if(!r||r->isdir) return 0; *data=r->data; *size=r->size; return 1; }
  for(int i=0;i<ncache;i++) if(cache[i].loader==f->loader && cache[i].ks==f->keysize && !memcmp(cache[i].key,f->key,f->keysize)){ *data=cache[i].data; *size=cache[i].size; return 1; }
  void *st=0; if(VCALL(f->loader,3,HRESULT (M*)(void*,const void*,u32,void**))(f->loader,f->key,f->keysize,&st)<0 || !st) return 0;
  u64 n=0; VCALL(st,5,HRESULT (M*)(void*,u64*))(st,&n); const void *p=0; void *ctx=0; u8 *copy= n? heap_alloc(n) : 0;
  if(copy && VCALL(st,3,HRESULT (M*)(void*,const void**,u64,u64,void**))(st,&p,0,n,&ctx)>=0 && p){ memcpy(copy,p,n); VCALL(st,4,void (M*)(void*,void*))(st,ctx); }
  else { if(copy) heap_free(copy); copy=0; }
  VCALL(st,2,u32 (M*)(void*))(st);
  if(!copy) return 0;
  int slot= ncache<64? ncache++ : 0; cache[slot].loader=f->loader; cache[slot].key=heap_alloc(f->keysize? f->keysize : 1); memcpy(cache[slot].key,f->key,f->keysize); cache[slot].ks=f->keysize; cache[slot].data=copy; cache[slot].size=n;
  *data=copy; *size=n; return 1;
}

/* ---- IDWriteFontFace ---- */
struct face { struct com c; struct font *f; struct dwfile *file; u32 sim; int own; };
static void **vt_face;
static HRESULT M fa_qi(struct face *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_FontFace,0,out,"FontFace"); }
static u32 M fa_release(struct face *o){ if(--o->c.refs>0) return (u32)o->c.refs; if(o->file) fi_release(o->file); if(o->own) heap_free(o->f); heap_free(o); return 0; }
static u32 M fa_type(struct face *o){ return !memcmp(o->f->data,"ttcf",4)? 2 : !memcmp(o->f->data+o->f->offset,"OTTO",4)? 0 : 1; }
static HRESULT M fa_files(struct face *o, u32 *n, struct dwfile **files){
  if(!files){ *n=1; return S_OK; } if(*n<1) return E_INVALIDARG;
  if(!o->file) o->file=file_for_path(o->f->path);
  if(o->file) com_addref(&o->file->c); files[0]=o->file; *n=1; return S_OK;
}
static u32 M fa_index(struct face *o){ return (u32)o->f->index; }
static u32 M fa_sim(struct face *o){ return o->sim; }
static BOOL M fa_symbol(struct face *o){ return o->f->symbol; }
struct fmetrics { u16 upem, ascent, descent; i16 linegap; u16 capheight, xheight; i16 ulpos; u16 ulsize; i16 stpos; u16 stsize; };
static void metrics_of(const struct font *f, struct fmetrics *m){
  m->upem=f->upem; m->ascent=(u16)f->ascent; m->descent=(u16)f->descent; m->linegap=f->linegap; m->capheight=(u16)f->capheight; m->xheight=(u16)f->xheight;
  m->ulpos=f->ulpos; m->ulsize=(u16)f->ulsize; m->stpos=f->stpos; m->stsize=(u16)f->stsize;
}
static void M fa_metrics(struct face *o, struct fmetrics *m){ metrics_of(o->f,m); }
static u16 M fa_count(struct face *o){ return (u16)o->f->info.numGlyphs; }
struct gmetrics { i32 lsb; u32 adv; i32 rsb, tsb; u32 advh; i32 bsb, vorigin; };
static void glyph_metrics(const struct font *f, u16 g, struct gmetrics *m){
  int adv, lsb, x0=0, y0=0, x1=0, y1=0; stbtt_GetGlyphHMetrics(&f->info,g,&adv,&lsb);
  if(!stbtt_GetGlyphBox(&f->info,g,&x0,&y0,&x1,&y1)){ x0=y0=x1=y1=0; }
  m->lsb=lsb; m->adv=(u32)adv; m->rsb=adv-lsb-(x1-x0); m->advh=(u32)(f->ascent+f->descent); m->tsb=f->ascent-y1; m->bsb=(i32)m->advh-m->tsb-(y1-y0); m->vorigin=f->ascent;
}
static HRESULT M fa_gmetrics(struct face *o, const u16 *g, u32 n, struct gmetrics *m, BOOL sideways){ (void)sideways; for(u32 i=0;i<n;i++) glyph_metrics(o->f,g[i],&m[i]); return S_OK; }
static HRESULT M fa_indices(struct face *o, const u32 *cp, u32 n, u16 *out){ for(u32 i=0;i<n;i++) out[i]=(u16)stbtt_FindGlyphIndex(&o->f->info,(int)cp[i]); return S_OK; }
static HRESULT M fa_table(struct face *o, u32 tag, const void **data, u32 *size, void **ctx, BOOL *exists){
  char t[4]={(char)tag,(char)(tag>>8),(char)(tag>>16),(char)(tag>>24)}; u32 l=0; const u8 *p=table(o->f,t,&l);
  *data=p; *size= p? l : 0; if(ctx) *ctx=0; *exists=p!=0; return S_OK;
}
static void M fa_reltable(struct face *o, void *ctx){ (void)o;(void)ctx; }
/* outlines: ID2D1SimplifiedGeometrySink methods 3 SetFillMode, 5 BeginFigure(point by value), 6 AddLines, 7 AddBeziers, 8 EndFigure */
static u64 pt(float x, float y){ union { float f[2]; u64 u; } v; v.f[0]=x; v.f[1]=y; return v.u; }
struct goff { float adv, asc; };
static HRESULT M fa_outline(struct face *o, float em, const u16 *g, const float *advs, const struct goff *offs, u32 n, BOOL sideways, BOOL rtl, void *sink){
  (void)sideways; if(!sink) return E_INVALIDARG; float sc=em/o->f->upem, pen=0;
  VCALL(sink,3,void (M*)(void*,u32))(sink,1);                                        /* D2D1_FILL_MODE_WINDING */
  for(u32 i=0;i<n;i++){
    float adv; if(advs) adv=advs[i]; else { struct gmetrics gm; glyph_metrics(o->f,g[i],&gm); adv=gm.adv*sc; }
    float ox= rtl? pen-adv : pen, oy=0; if(offs){ ox+= rtl? -offs[i].adv : offs[i].adv; oy-=offs[i].asc; }
    stbtt_vertex *v=0; int nv=stbtt_GetGlyphShape(&o->f->info,g[i],&v); int open=0; float cx=0, cy=0;
    for(int k=0;k<nv;k++){
      float x=ox+v[k].x*sc, y=oy-v[k].y*sc;
      if(v[k].type==STBTT_vmove){ if(open) VCALL(sink,8,void (M*)(void*,u32))(sink,1); VCALL(sink,5,void (M*)(void*,u64,u32))(sink,pt(x,y),0); open=1; }
      else if(v[k].type==STBTT_vline){ u64 p=pt(x,y); VCALL(sink,6,void (M*)(void*,const u64*,u32))(sink,&p,1); }
      else if(v[k].type==STBTT_vcurve){ float qx=ox+v[k].cx*sc, qy=oy-v[k].cy*sc; u64 b[3]={pt(cx+2.0f/3*(qx-cx),cy+2.0f/3*(qy-cy)),pt(x+2.0f/3*(qx-x),y+2.0f/3*(qy-y)),pt(x,y)};   /* quadratic -> cubic */
        VCALL(sink,7,void (M*)(void*,const u64*,u32))(sink,b,1); }
      else if(v[k].type==STBTT_vcubic){ u64 b[3]={pt(ox+v[k].cx*sc,oy-v[k].cy*sc),pt(ox+v[k].cx1*sc,oy-v[k].cy1*sc),pt(x,y)}; VCALL(sink,7,void (M*)(void*,const u64*,u32))(sink,b,1); }
      cx=x; cy=y;
    }
    if(open) VCALL(sink,8,void (M*)(void*,u32))(sink,1);
    if(v) stbtt_FreeShape(&o->f->info,v);
    pen+= rtl? -adv : adv;
  }
  return S_OK;
}
static HRESULT M fa_rmode(struct face *o, float em, float ppd, u32 mm, void *params, u32 *mode){ (void)o;(void)mm;(void)params; *mode= em*ppd>=100? 6 : 5; return S_OK; }   /* OUTLINE for big text, else NATURAL_SYMMETRIC */
static HRESULT M fa_gdimetrics(struct face *o, float em, float ppd, const void *t, struct fmetrics *m){ (void)em;(void)ppd;(void)t; metrics_of(o->f,m); return S_OK; }
static HRESULT M fa_gdigmetrics(struct face *o, float em, float ppd, const void *t, BOOL nat, const u16 *g, u32 n, struct gmetrics *m, BOOL side){ (void)em;(void)ppd;(void)t;(void)nat; return fa_gmetrics(o,g,n,m,side); }
static void *vt_face_[]={fa_qi,com_addref,fa_release,fa_type,fa_files,fa_index,fa_sim,fa_symbol,fa_metrics,fa_count,fa_gmetrics,fa_indices,fa_table,fa_reltable,fa_outline,fa_rmode,fa_gdimetrics,fa_gdigmetrics};
static struct face *face_of(struct font *f, int own, u32 sim){ struct face *o=obj_new(vt_face,sizeof *o); if(!o) return 0; o->f=f; o->own=own; o->sim=sim; return o; }

/* ---- IDWriteFont ---- */
static HRESULT M fo_qi(struct dwfont *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_Font,0,out,"Font"); }
static HRESULT M fo_family(struct dwfont *o, void **out){ addref_out(out,o->fam); return S_OK; }
static u32 M fo_weight(struct dwfont *o){ return fonts[o->idx].weight; }
static u32 M fo_stretch(struct dwfont *o){ return fonts[o->idx].stretch; }
static u32 M fo_style(struct dwfont *o){ return fonts[o->idx].slant; }
static BOOL M fo_symbol(struct dwfont *o){ return fonts[o->idx].symbol; }
static HRESULT M fo_names(struct dwfont *o, void **out){ *out=strings_of(fonts[o->idx].style); return *out? S_OK : E_OUTOFMEMORY; }
static HRESULT M fo_info(struct dwfont *o, u32 id, void **out, BOOL *exists){
  static const signed char nid[19]={-1,0,5,7,8,9,12,10,11,13,14,1,2,16,17,19,4,6,20};
  *out=0; *exists=0; if(id>18 || nid[id]<0) return S_OK; wc s[128];
  if(id==11 && o->fam){ int k=0; for(;o->fam->name[k]&&k<127;k++) s[k]=o->fam->name[k]; s[k]=0; }      /* WIN32_FAMILY_NAMES: the family it is listed under */
  else if(!name_entry(&fonts[o->idx],nid[id],s,128)) return S_OK;
  *out=strings_of(s); *exists=*out!=0; return S_OK;
}
static u32 M fo_sim(struct dwfont *o){ (void)o; return 0; }
static void M fo_metrics(struct dwfont *o, struct fmetrics *m){ metrics_of(&fonts[o->idx],m); }
static HRESULT M fo_has(struct dwfont *o, u32 cp, BOOL *has){ *has=stbtt_FindGlyphIndex(&fonts[o->idx].info,(int)cp)!=0; return S_OK; }
static HRESULT M fo_face(struct dwfont *o, void **out){ *out=face_of(&fonts[o->idx],0,0); return *out? S_OK : E_OUTOFMEMORY; }
static void *vt_font_[]={fo_qi,com_addref,com_release,fo_family,fo_weight,fo_stretch,fo_style,fo_symbol,fo_names,fo_info,fo_sim,fo_metrics,fo_has,fo_face};

/* ---- IDWriteFontFamily (and IDWriteFontList) ---- */
struct flist { struct com c; struct family *fam; int order[16], n; };
static void **vt_flist;
static int match_score(const struct font *f, u32 w, u32 st, u32 sl){ int s=(f->slant==sl? 0 : (sl && f->slant)? 500 : 2000); int dw=(int)f->weight-(int)w; if(dw<0) dw=-dw*2+ (w>=500? 100 : 0); s+=dw; int ds=(int)f->stretch-(int)st; s+=(ds<0? -ds : ds)*50; return s; }
static HRESULT M fm_qi(struct family *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_Family,&IID_FontList,out,"FontFamily"); }
static HRESULT M fm_coll(struct family *o, void **out){ (void)o; addref_out(out,&sys_coll); return S_OK; }
static u32 M fm_count(struct family *o){ return (u32)o->nfonts; }
static HRESULT M fm_font(struct family *o, u32 i, void **out){ if(i>=(u32)o->nfonts) return E_INVALIDARG; addref_out(out,&fontobjs[o-fams][i]); return S_OK; }
static HRESULT M fm_names(struct family *o, void **out){ *out=strings_of(o->name); return *out? S_OK : E_OUTOFMEMORY; }
static HRESULT M fm_first(struct family *o, u32 w, u32 st, u32 sl, void **out){
  int best=-1, bs=0; for(int i=0;i<o->nfonts;i++){ int s=match_score(&fonts[o->fonts[i]],w,st,sl); if(best<0||s<bs){ best=i; bs=s; } }
  if(best<0) return E_FAIL; addref_out(out,&fontobjs[o-fams][best]); return S_OK;
}
static HRESULT M fm_matching(struct family *o, u32 w, u32 st, u32 sl, void **out){
  struct flist *l=obj_new(vt_flist,sizeof *l); if(!l) return E_OUTOFMEMORY; l->fam=o; l->n=o->nfonts;
  for(int i=0;i<o->nfonts;i++) l->order[i]=i;
  for(int i=1;i<l->n;i++){ int x=l->order[i], j=i; while(j>0 && match_score(&fonts[o->fonts[l->order[j-1]]],w,st,sl)>match_score(&fonts[o->fonts[x]],w,st,sl)){ l->order[j]=l->order[j-1]; j--; } l->order[j]=x; }
  *out=l; return S_OK;
}
static void *vt_family_[]={fm_qi,com_addref,com_release,fm_coll,fm_count,fm_font,fm_names,fm_first,fm_matching};
static HRESULT M fl_qi(struct flist *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_FontList,0,out,"FontList"); }
static HRESULT M fl_coll(struct flist *o, void **out){ (void)o; addref_out(out,&sys_coll); return S_OK; }
static u32 M fl_count(struct flist *o){ return (u32)o->n; }
static HRESULT M fl_font(struct flist *o, u32 i, void **out){ if(i>=(u32)o->n) return E_INVALIDARG; addref_out(out,&fontobjs[o->fam-fams][o->order[i]]); return S_OK; }
static void *vt_flist_[]={fl_qi,com_addref,com_release,fl_coll,fl_count,fl_font};

/* ---- IDWriteFontCollection ---- */
static HRESULT M co_qi(struct com *o, const struct guid *g, void **out){ return qi(o,g,&IID_Collection,0,out,"FontCollection"); }
static u32 M co_count(struct com *o){ (void)o; return (u32)nfams; }
static HRESULT M co_family(struct com *o, u32 i, void **out){ (void)o; if(i>=(u32)nfams) return E_INVALIDARG; addref_out(out,&fams[i]); return S_OK; }
static HRESULT M co_find(struct com *o, const wc *name, u32 *idx, BOOL *exists){
  (void)o; for(int i=0;i<nfams;i++) if(!wcmp_ci(fams[i].name,name)){ *idx=(u32)i; *exists=1; return S_OK; }
  *idx=0xFFFFFFFF; *exists=0; return S_OK;
}
static HRESULT M co_fromface(struct com *o, struct face *fc, void **out){
  (void)o; for(int i=0;i<nfams;i++) for(int j=0;j<fams[i].nfonts;j++){ struct font *f=&fonts[fams[i].fonts[j]]; if(f->data==fc->f->data && f->index==fc->f->index){ addref_out(out,&fontobjs[i][j]); return S_OK; } }
  *out=0; return (HRESULT)0x88985002;                                                  /* DWRITE_E_NOFONT */
}
static void *vt_coll_[]={co_qi,com_addref,com_release,co_count,co_family,co_find,co_fromface};

/* ---- IDWriteRenderingParams ---- */
struct params { struct com c; float gamma, contrast, cleartype; u32 geometry, mode; };
static void **vt_params;
static HRESULT M rp_qi(struct params *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_Params,0,out,"RenderingParams"); }
static float M rp_gamma(struct params *o){ return o->gamma; }
static float M rp_contrast(struct params *o){ return o->contrast; }
static float M rp_ct(struct params *o){ return o->cleartype; }
static u32 M rp_geo(struct params *o){ return o->geometry; }
static u32 M rp_mode(struct params *o){ return o->mode; }
static void *vt_params_[]={rp_qi,com_addref,com_release,rp_gamma,rp_contrast,rp_ct,rp_geo,rp_mode};
static struct params *params_new(float g, float c, float ct, u32 geo, u32 mode){ struct params *p=obj_new(vt_params,sizeof *p); if(p){ p->gamma=g; p->contrast=c; p->cleartype=ct; p->geometry=geo; p->mode=mode; } return p; }

/* ---- IDWriteGlyphRunAnalysis: anti-aliased coverage of a run of glyphs ---- */
struct grun { struct com c; struct font *f; struct face *face; float scale, sx, sy, dx, dy; u32 n; u16 *g; float *x, *y; int aliased; int bx0, by0, bx1, by1; };
static void **vt_analysis;
static HRESULT M an_qi(struct grun *o, const struct guid *g, void **out){ return qi(&o->c,g,&IID_Analysis,0,out,"GlyphRunAnalysis"); }
static u32 M an_release(struct grun *o){ if(--o->c.refs>0) return (u32)o->c.refs; fa_release(o->face); heap_free(o->g); heap_free(o); return 0; }
static void an_bounds(struct grun *o){
  o->bx0=o->by0=0x7FFFFFFF; o->bx1=o->by1=-0x7FFFFFFF;
  for(u32 i=0;i<o->n;i++){ float fx=o->x[i], fy=o->y[i]; int ix=(int)dw_floor(fx), iy=(int)dw_floor(fy); int x0,y0,x1,y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&o->f->info,o->g[i],o->scale*o->sx,o->scale*o->sy,fx-ix,fy-iy,&x0,&y0,&x1,&y1);
    if(x1<=x0||y1<=y0) continue; if(ix+x0<o->bx0) o->bx0=ix+x0; if(iy+y0<o->by0) o->by0=iy+y0; if(ix+x1>o->bx1) o->bx1=ix+x1; if(iy+y1>o->by1) o->by1=iy+y1; }
  if(o->bx0>o->bx1){ o->bx0=o->by0=o->bx1=o->by1=0; }
}
static HRESULT M an_texbounds(struct grun *o, u32 type, i32 *r){ (void)type; r[0]=o->bx0; r[1]=o->by0; r[2]=o->bx1; r[3]=o->by1; return S_OK; }   /* both texture types: grey-scale coverage */
static HRESULT M an_texture(struct grun *o, u32 type, const i32 *r, u8 *buf, u32 size){
  int w=r[2]-r[0], h=r[3]-r[1], bpp= type? 3 : 1; if(w<=0||h<=0) return S_OK; if((u64)w*h*bpp>size) return E_INVALIDARG;
  memset(buf,0,(u64)w*h*bpp);
  for(u32 i=0;i<o->n;i++){
    float fx=o->x[i], fy=o->y[i]; int ix=(int)dw_floor(fx), iy=(int)dw_floor(fy); int x0,y0,x1,y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&o->f->info,o->g[i],o->scale*o->sx,o->scale*o->sy,fx-ix,fy-iy,&x0,&y0,&x1,&y1);
    int gw=x1-x0, gh=y1-y0; if(gw<=0||gh<=0) continue; u8 *tmp=heap_alloc((u64)gw*gh); if(!tmp) continue;
    stbtt_MakeGlyphBitmapSubpixel(&o->f->info,tmp,gw,gh,gw,o->scale*o->sx,o->scale*o->sy,fx-ix,fy-iy,o->g[i]);
    for(int yy=0;yy<gh;yy++){ int ty=iy+y0+yy-r[1]; if(ty<0||ty>=h) continue;
      for(int xx=0;xx<gw;xx++){ int tx=ix+x0+xx-r[0]; if(tx<0||tx>=w) continue; u8 v=tmp[yy*gw+xx]; if(o->aliased) v= v>=128? 255 : 0;
        u8 *d=buf+((u64)ty*w+tx)*bpp; for(int k=0;k<bpp;k++) if(v>d[k]) d[k]=v; } }
    heap_free(tmp);
  }
  return S_OK;
}
static HRESULT M an_blend(struct grun *o, void *params, float *g, float *c, float *ct){ (void)o;(void)params; *g=1.8f; *c=0.5f; *ct=0; return S_OK; }
static void *vt_analysis_[]={an_qi,com_addref,an_release,an_texbounds,an_texture,an_blend};
struct glyphrun { struct face *face; float em; u32 n; const u16 *g; const float *adv; const struct goff *off; BOOL sideways; u32 bidi; };
struct matrix { float m11, m12, m21, m22, dx, dy; };
static HRESULT make_analysis(const struct glyphrun *run, float ppd, const struct matrix *t, u32 mode, float bx, float by, void **out){
  if(!run || !run->face || !out) return E_INVALIDARG;
  struct grun *o=obj_new(vt_analysis,sizeof *o); if(!o) return E_OUTOFMEMORY;
  u32 n=run->n; o->g=heap_alloc((u64)n*(2+4+4)+16); if(!o->g){ heap_free(o); return E_OUTOFMEMORY; }
  o->x=(float*)((u8*)o->g+((n*2+7)&~7u)); o->y=o->x+n; o->n=n; o->face=run->face; com_addref(&run->face->c); o->f=run->face->f;
  o->scale=run->em*ppd/o->f->upem; o->sx=1; o->sy=1; float m11=1, m22=1, dx=0, dy=0;
  if(t){ m11=t->m11; m22=t->m22; dx=t->dx; dy=t->dy; o->sx= m11<0? -m11 : m11; o->sy= m22<0? -m22 : m22; if(o->sx==0) o->sx=1; if(o->sy==0) o->sy=1; }   /* scale and translation (no rotation) */
  o->aliased= mode==1;
  float pen=0; int rtl=run->bidi&1;
  for(u32 i=0;i<n;i++){
    float adv; if(run->adv) adv=run->adv[i]; else { struct gmetrics gm; glyph_metrics(o->f,run->g[i],&gm); adv=gm.adv*(run->em/o->f->upem); }
    float gx= rtl? pen-adv : pen, gy=0; if(run->off){ gx+= rtl? -run->off[i].adv : run->off[i].adv; gy-=run->off[i].asc; }
    o->g[i]=run->g[i]; o->x[i]=(bx+gx*m11)*ppd+dx*ppd; o->y[i]=(by+gy*m22)*ppd+dy*ppd;
    pen+= rtl? -adv : adv;
  }
  an_bounds(o); *out=o; return S_OK;
}

/* ---- IDWriteGdiInterop ---- */
static HRESULT M gi_qi(struct com *o, const struct guid *g, void **out){ return qi(o,g,&IID_GdiInterop,0,out,"GdiInterop"); }
static HRESULT M gi_fromlogfont(struct com *o, const u8 *lf, void **out){                /* LOGFONTW: weight @16, italic @20, face @28 */
  (void)o; scan_fonts(); u32 idx; BOOL ex; co_find(&sys_coll,(const wc*)(lf+28),&idx,&ex); if(!ex) idx=0;
  if(idx>=(u32)nfams) return (HRESULT)0x88985002; i32 w=*(const i32*)(lf+16); return fm_first(&fams[idx],w? (u32)w : 400,5,lf[20]? 2 : 0,out);
}
static HRESULT M gi_tologfont(struct com *o, struct dwfont *f, u8 *lf, BOOL *sys){
  (void)o; memset(lf,0,92); *(i32*)(lf+16)=(i32)fonts[f->idx].weight; lf[20]=fonts[f->idx].slant!=0; lf[23]=1;
  for(int k=0;f->fam->name[k]&&k<31;k++) ((wc*)(lf+28))[k]=f->fam->name[k]; if(sys) *sys=1; return S_OK;
}
static HRESULT M gi_facetologfont(struct com *o, struct face *fc, u8 *lf){
  (void)o; memset(lf,0,92); *(i32*)(lf+16)=(i32)fc->f->weight; lf[20]=fc->f->slant!=0; lf[23]=1; for(int k=0;fc->f->family[k]&&k<31;k++) ((wc*)(lf+28))[k]=fc->f->family[k]; return S_OK;
}
static HRESULT M gi_fromhdc(struct com *o, u64 hdc, void **out){ (void)o;(void)hdc; scan_fonts(); if(!nfonts) return E_FAIL; *out=face_of(&fonts[0],0,0); return *out? S_OK : E_OUTOFMEMORY; }
static void *vt_gdi_[]={gi_qi,com_addref,com_release,gi_fromlogfont,gi_tologfont,gi_facetologfont,gi_fromhdc,not_impl};
static struct com gdi_interop;

/* ---- IDWriteFactory ---- */
static struct com factory; static void **vt_factory;
static HRESULT M fc_qi(struct com *o, const struct guid *g, void **out){ return qi(o,g,&IID_Factory,0,out,"Factory"); }
static HRESULT M fc_syscoll(struct com *o, void **out, BOOL check){ (void)o;(void)check; scan_fonts(); addref_out(out,&sys_coll); return S_OK; }
static HRESULT M fc_filereference(struct com *o, const wc *path, const u64 *time, void **out){
  (void)o;(void)time; char p[300]; int k=0; for(;path[k]&&k<299;k++) p[k]= path[k]<0x80? (char)path[k] : '?'; p[k]=0;
  struct ramfile *f=fs_find(p); if(!f||f->isdir) return DWRITE_E_FILENOTFOUND;
  char canon[128]; unsigned c=0; canon[c++]='/'; for(const char *s=f->name;*s&&c<127;) canon[c++]=*s++; canon[c]=0;
  *out=file_for_path(canon); return *out? S_OK : E_OUTOFMEMORY;
}
static HRESULT M fc_customfile(struct com *o, const void *key, u32 size, struct com *loader, void **out){
  (void)o; if(!loader) return E_INVALIDARG; if(loader==&local_loader) return fc_filereference(o,0,0,out);
  struct dwfile *f=obj_new(vt_file,sizeof *f); if(!f) return E_OUTOFMEMORY; f->key=heap_alloc(size? size : 1); memcpy(f->key,key,size); f->keysize=size;
  f->loader=loader; VCALL(loader,1,u32 (M*)(void*))(loader); *out=f; return S_OK;
}
static HRESULT M fc_face(struct com *o, u32 type, u32 nfiles, struct dwfile **files, u32 index, u32 sim, void **out){
  (void)o;(void)type; if(!nfiles || !files || !files[0]) return E_INVALIDARG; scan_fonts();
  u8 *d; u64 n; if(!file_data(files[0],&d,&n)) return DWRITE_E_FILENOTFOUND;
  for(int i=0;i<nfonts;i++) if(fonts[i].data==d && fonts[i].index==(int)index){ struct face *fc=face_of(&fonts[i],0,sim); if(!fc) return E_OUTOFMEMORY; fc->file=files[0]; com_addref(&files[0]->c); *out=fc; return S_OK; }
  struct font *f=heap_alloc(sizeof *f); if(!f) return E_OUTOFMEMORY;
  if(!load_font(f,d,n,(int)index)){ heap_free(f); return DWRITE_E_FILEFORMAT; }
  struct face *fc=face_of(f,1,sim); if(!fc){ heap_free(f); return E_OUTOFMEMORY; } fc->file=files[0]; com_addref(&files[0]->c); *out=fc; return S_OK;
}
static HRESULT M fc_params(struct com *o, void **out){ (void)o; *out=params_new(1.8f,0.5f,0.0f,1,5); return *out? S_OK : E_OUTOFMEMORY; }
static HRESULT M fc_monparams(struct com *o, u64 mon, void **out){ (void)mon; return fc_params(o,out); }
static HRESULT M fc_customparams(struct com *o, float g, float c, float ct, u32 geo, u32 mode, void **out){ (void)o; *out=params_new(g,c,ct,geo,mode); return *out? S_OK : E_OUTOFMEMORY; }
static HRESULT M fc_regloader(struct com *o, void *l){ (void)o; if(l) VCALL(l,1,u32 (M*)(void*))(l); return S_OK; }   /* loaders are called directly through the file objects */
static HRESULT M fc_unregloader(struct com *o, void *l){ (void)o;(void)l; return S_OK; }
static HRESULT M fc_gdi(struct com *o, void **out){ (void)o; addref_out(out,&gdi_interop); return S_OK; }
static HRESULT M fc_analysis(struct com *o, const struct glyphrun *run, float ppd, const struct matrix *t, u32 mode, u32 mm, float bx, float by, void **out){ (void)o;(void)mm; return make_analysis(run,ppd,t,mode,bx,by,out); }
static void *vt_factory_[]={fc_qi,com_addref,com_release,fc_syscoll,not_impl,not_impl,not_impl,fc_filereference,fc_customfile,fc_face,fc_params,fc_monparams,fc_customparams,
  fc_regloader,fc_unregloader,not_impl,not_impl,fc_gdi,not_impl,not_impl,not_impl,not_impl,not_impl,fc_analysis};
/* object kinds whose vtables are in the static arrays above */
static void init_vtables(void){
  static int done; if(done) return; done=1;
  vt_strings=vt_strings_; vt_file=vt_file_; vt_loader=vt_loader_; vt_stream=vt_stream_; vt_face=vt_face_; vt_font=vt_font_; vt_family=vt_family_;
  vt_flist=vt_flist_; vt_coll=vt_coll_; vt_params=vt_params_; vt_analysis=vt_analysis_; vt_factory=vt_factory_;
  factory=(struct com){vt_factory,1,1}; sys_coll=(struct com){vt_coll,1,1}; local_loader=(struct com){vt_loader,1,1}; gdi_interop=(struct com){vt_gdi_,1,1};
}
static HRESULT M d_DWriteCreateFactory(u32 type, const struct guid *iid, void **out){
  (void)type; init_vtables(); if(!out) return E_INVALIDARG;
  if(!guid_eq(iid,&IID_Factory) && !guid_eq(iid,&IID_IUnknown)){ guid_log("DWriteCreateFactory",iid); *out=0; return E_NOINTERFACE; }
  addref_out(out,&factory); return S_OK;
}
const struct export dwrite_exports[] = { {"dwrite","DWriteCreateFactory",d_DWriteCreateFactory} };
const unsigned dwrite_nexports = sizeof dwrite_exports / sizeof *dwrite_exports;
