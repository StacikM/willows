#include "kernel.h"
/* PS/2 keyboard, scancode set 1. Chars go to a ring buffer; vk[] tracks key-down state for GetAsyncKeyState. */
static const char lo[58]="\0\033" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char hi[58]="\0\033" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
/* Keyboard layouts. The US tables above are the base; a layout only lists the keys that differ (index = scancode, 0 = same as US).
   Characters are Latin-1 bytes. AltGr (right Alt) selects a third level. */
#define NSC 0x58
static const unsigned char fr_lo[NSC]={ [0x02]='&',[0x03]=0xE9,[0x04]='"',[0x05]='\'',[0x06]='(',[0x07]='-',[0x08]=0xE8,[0x09]='_',[0x0A]=0xE7,[0x0B]=0xE0,[0x0C]=')',[0x0D]='=',
  [0x10]='a',[0x11]='z',[0x1A]='^',[0x1B]='$',[0x1E]='q',[0x27]='m',[0x28]=0xF9,[0x29]=0xB2,[0x2B]='*',[0x2C]='w',[0x32]=',',[0x33]=';',[0x34]=':',[0x35]='!',[0x56]='<' };
static const unsigned char fr_hi[NSC]={ [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',[0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',[0x0C]=0xB0,[0x0D]='+',
  [0x1A]=0xA8,[0x1B]=0xA3,[0x28]='%',[0x2B]=0xB5,[0x32]='?',[0x33]='.',[0x34]='/',[0x35]=0xA7,[0x56]='>' };
static const unsigned char fr_ag[NSC]={ [0x03]='~',[0x04]='#',[0x05]='{',[0x06]='[',[0x07]='|',[0x08]='`',[0x09]='\\',[0x0A]='^',[0x0B]='@',[0x0C]=']',[0x0D]='}',[0x1B]=0xA4 };
volatile int kb_layout;                                  /* 0 = US, 1 = French AZERTY */
void kb_set_layout(int l){ kb_layout = l == 1 ? 1 : 0; }
int kb_get_layout(void){ return kb_layout; }
static unsigned char key_lo(int sc){ if(kb_layout==1 && fr_lo[sc]) return fr_lo[sc]; return sc<58? (unsigned char)lo[sc] : 0; }
static unsigned char key_hi(int sc){ unsigned char l=key_lo(sc);
  if(kb_layout==1 && fr_hi[sc]) return fr_hi[sc]; if(kb_layout==1 && fr_lo[sc]>='a' && fr_lo[sc]<='z') return fr_lo[sc]-32;
  if(sc<58) { unsigned char h=(unsigned char)hi[sc]; if(kb_layout==1 && fr_lo[sc]) return l; return h; } return l; }
static unsigned char key_ag(int sc){ return kb_layout==1? fr_ag[sc] : 0; }
static volatile u8 vk[256]; static volatile char buf[64]; static volatile u32 head, tail;
volatile int kb_desktop;
static int shift, ctrl, caps, ext, altgr;
static volatile u16 evq[64]; static volatile u32 ehead, etail;
static void push_ev(int vk,int ch){ u32 n=(ehead+1)&63; if(n!=etail){ evq[ehead]=(vk&0xFF)|((ch&0xFF)<<8); ehead=n; } }
int kb_event(int *vk,int *ch){ if(ehead==etail) return 0; u16 e=evq[etail]; etail=(etail+1)&63; *vk=e&0xFF; *ch=e>>8; return 1; }
void kb_flush(void){ tail=head; etail=ehead; }

void keyboard_init(void){
  while(inb(0x64)&1) inb(0x60);
  outb(0x21, inb(0x21)&~2);              /* unmask IRQ1 */
}
static void push(char c){ u32 n=(head+1)&63; if(n!=tail){ buf[head]=c; head=n; } }

void keyboard_irq(void){
  u8 sc=inb(0x60);
  if(sc==0xE0){ ext=1; return; }
  int up=sc&0x80; sc&=0x7F; int e=ext; ext=0;
  int code=0;
  if(e){ switch(sc){ case 0x38: altgr=!up; break; case 0x48: code=0x26; break; case 0x4B: code=0x25; break; case 0x4D: code=0x27; break;
                     case 0x50: code=0x28; break; case 0x1D: code=0x11; break;
                     case 0x47: code=0x24; break; case 0x4F: code=0x23; break; case 0x53: code=0x2E; break;
                     case 0x49: code=0x21; break; case 0x51: code=0x22; break; } }
  else if(sc==0x2A||sc==0x36){ shift=!up; code=0x10; }
  else if(sc==0x1D){ ctrl=!up; code=0x11; }
  else if(sc==0x3A){ if(!up) caps^=1; }
  else if(sc<NSC && key_lo(sc)){
    unsigned char c=key_lo(sc), u=sc<58? (unsigned char)lo[sc] : 0;
    if(c>='a'&&c<='z') code=c-32;                                              /* letters: VK = the letter the layout produces */
    else { unsigned char b= (u && !(u>='a'&&u<='z'))? u : c; code = b=='\n'? 0x0D : b=='\033'? 0x1B : b=='\b'? 0x08 : b; }
  }
  if(code) vk[code]=!up;
  if(!up && e && code && code!=0x11) push_ev(code,0);            /* arrows, Home/End/Delete... */
  if(!up && !e && sc<NSC && key_lo(sc)){
    unsigned char lc=key_lo(sc), ac=key_ag(sc), c;
    if(altgr && ac) c=ac; else c=(shift^(caps&&lc>='a'&&lc<='z'))? key_hi(sc) : lc;
    if(ctrl && c>='a' && c<='z') c-=96;
    if(!kb_desktop) push(c);                               /* desktop mode: the window manager routes keys */
    push_ev(code, c=='\n'?13:c);
  }
}
void kb_inject(char c){ push(c); }
int kb_poll(void){ return head!=tail; }
int kb_getc(void){
  while(head==tail) idle_wait();
  char c=buf[tail]; tail=(tail+1)&63; return (u8)c;
}
int kb_vk(int k){ return (k>=0&&k<256)? vk[k] : 0; }

/* Cooked line input with echo + backspace; result ends with "\r\n" like a Windows console. */
int kb_readline(char *out,int max){
  int n=0;
  for(;;){
    int c=kb_getc();
    if(c=='\n'){ kputc('\n'); if(n<max) out[n++]='\r'; if(n<max) out[n++]='\n'; return n; }
    if(c=='\b'){ if(n){ n--; kputs("\b \b"); } continue; }
    if(c>=32 && c<127 && n<max-2){ out[n++]=c; kputc(c); }
  }
}
