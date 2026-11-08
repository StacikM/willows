#include "kernel.h"
#include "font.h"
/* Font selection. If src/fontdata.c has been generated (tools/host/mkfont.c) its anti-aliased fonts are used;
   otherwise the 8x16 bitmap font is scaled (bold = drawn twice). */
extern const struct font all_fonts[] __attribute__((weak));
extern const int all_fonts_n __attribute__((weak));
extern const unsigned char all_fonts_style[] __attribute__((weak));

static const struct font fallback[] = {      /* name, px, ascent, lineh, nglyph, g, bits, scale, bold */
  {"bitmap1",   16, 13, 18, 0, 0, 0, 1, 0},
  {"bitmap1b",  16, 13, 18, 0, 0, 0, 1, 1},
  {"bitmap2",   32, 26, 36, 0, 0, 0, 2, 0},
  {"bitmap2b",  32, 26, 36, 0, 0, 0, 2, 1},
};
const struct font *font_find(int style, int px){
  if(all_fonts && all_fonts_n>0){
    const struct font *best=0; int bd=1<<30;
    for(int i=0;i<all_fonts_n;i++) if(all_fonts_style[i]==style){ int d=all_fonts[i].px-px; if(d<0) d=-d; if(d<bd){ bd=d; best=&all_fonts[i]; } }
    if(!best) for(int i=0;i<all_fonts_n;i++){ int d=all_fonts[i].px-px; if(d<0) d=-d; if(d<bd){ bd=d; best=&all_fonts[i]; } }   /* style missing: any font */
    if(best) return best;
  }
  int bold=(style==FS_BOLD||style==FS_MONOBOLD);
  return &fallback[(px>=22? 2 : 0)+bold];
}
