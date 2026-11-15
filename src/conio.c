#include "kernel.h"
/* Windows console API for games and tools: input events (ReadConsoleInput & co), cursor position and visibility, colours, screen info.
   Output control is expressed as ANSI escape sequences written to the program's stdout, which both the Terminal app and the kernel Console window interpret. */
typedef u32 DWORD; typedef i32 BOOL; typedef u64 HANDLE;
static void emit(const char *s){ for(;*s;s++) kputc(*s); }
static void emit_num(int v){ char b[12]; int i=11; b[i]=0; if(v<0) v=0; do{ b[--i]=(char)('0'+v%10); v/=10; }while(v); emit(b+i); }

/* INPUT_RECORD (20 bytes): EventType u16 (KEY_EVENT = 1), then KEY_EVENT_RECORD { bKeyDown u32, wRepeatCount u16, wVirtualKeyCode u16, wVirtualScanCode u16, char u16, dwControlKeyState u32 } */
static void fill_rec(u8 *r, int vk, int ch, int down, int st, int wide){
  memset(r,0,20); *(u16*)r=1; *(u32*)(r+4)=down? 1 : 0; *(u16*)(r+8)=1; *(u16*)(r+10)=(u16)vk;
  static const u8 scan[256]={[0x08]=0x0E,[0x09]=0x0F,[0x0D]=0x1C,[0x1B]=0x01,[0x20]=0x39,[0x25]=0x4B,[0x26]=0x48,[0x27]=0x4D,[0x28]=0x50,[0x21]=0x49,[0x22]=0x51,[0x24]=0x47,[0x23]=0x4F,[0x2E]=0x53};
  *(u16*)(r+12)=scan[vk&255]; *(u16*)(r+14)=(u16)(wide? ch : (ch&255)); *(u32*)(r+16)=(u32)st;
}
static int WINAPI c_GetNumberOfConsoleInputEvents(HANDLE h, DWORD *n){ (void)h; cur->con_in=1; if(n) *n=(DWORD)ci_count(); return 1; }
static int read_events(u8 *rec, DWORD len, DWORD *got, int remove, int wide, int block){
  cur->con_in=1; if(got) *got=0; if(!len) return 1;
  if(block) while(ci_count()==0) idle_wait();
  DWORD k=0; int vk,ch,down,st;
  while(k<len && ci_pop(&vk,&ch,&down,&st,remove)){ fill_rec(rec+k*20,vk,ch,down,st,wide); k++; if(!remove) break; }   /* peek returns the first event only */
  if(got) *got=k; return 1;
}
static int WINAPI c_ReadConsoleInputA(HANDLE h, u8 *rec, DWORD len, DWORD *got){ (void)h; return read_events(rec,len,got,1,0,1); }
static int WINAPI c_ReadConsoleInputW(HANDLE h, u8 *rec, DWORD len, DWORD *got){ (void)h; return read_events(rec,len,got,1,1,1); }
static int WINAPI c_PeekConsoleInputA(HANDLE h, u8 *rec, DWORD len, DWORD *got){ (void)h; return read_events(rec,len,got,0,0,0); }
static int WINAPI c_PeekConsoleInputW(HANDLE h, u8 *rec, DWORD len, DWORD *got){ (void)h; return read_events(rec,len,got,0,1,0); }
static int WINAPI c_FlushConsoleInputBuffer(HANDLE h){ (void)h; cur->con_in=1; ci_flush(); kb_flush(); return 1; }

/* output control */
static int WINAPI c_SetConsoleCursorPosition(HANDLE h, u32 coord){ (void)h; emit("\x1b["); emit_num((int)(coord>>16)+1); kputc(';'); emit_num((int)(coord&0xFFFF)+1); kputc('H'); return 1; }
static int WINAPI c_SetConsoleCursorInfo(HANDLE h, const u32 *ci){ (void)h; emit(ci && ci[1]==0? "\x1b[?25l" : "\x1b[?25h"); return 1; }
static int WINAPI c_GetConsoleCursorInfo(HANDLE h, u32 *ci){ (void)h; if(ci){ ci[0]=25; ci[1]=1; } return 1; }
static int WINAPI c_SetConsoleTextAttribute(HANDLE h, u16 a){
  (void)h; static const int ansi[8]={0,4,2,6,1,5,3,7};                                  /* Windows colour bits are BGR, ANSI is RGB */
  int fg=ansi[a&7], bg=ansi[(a>>4)&7]; emit("\x1b[0;"); emit_num((a&8)? 90+fg : 30+fg); kputc(';'); emit_num(((a&0x80)? 100 : 40)+bg); kputc('m'); return 1;
}
static int WINAPI c_GetConsoleScreenBufferInfo(HANDLE h, u8 *i){                          /* CONSOLE_SCREEN_BUFFER_INFO: size, cursor, attributes, window, maximum size */
  (void)h; if(!i) return 0; memset(i,0,22); *(u16*)i=80; *(u16*)(i+2)=25; *(u16*)(i+8)=7; *(u16*)(i+12)=0; *(u16*)(i+14)=0; *(u16*)(i+16)=79; *(u16*)(i+18)=24; *(u16*)(i+20)=80; return 1;
}
static int WINAPI c_FillConsoleOutputCharacterA(HANDLE h, char ch, DWORD n, u32 coord, DWORD *written){
  (void)h; c_SetConsoleCursorPosition(0,coord); for(DWORD i=0;i<n && i<4000;i++) kputc(ch); c_SetConsoleCursorPosition(0,coord); if(written) *written=n; return 1;
}
static int WINAPI c_FillConsoleOutputAttribute(HANDLE h, u16 a, DWORD n, u32 coord, DWORD *written){ (void)h;(void)a;(void)coord; if(written) *written=n; return 1; }
static int WINAPI c_SetConsoleScreenBufferSize(HANDLE h, u32 s){ (void)h;(void)s; return 1; }
static int WINAPI c_SetConsoleWindowInfo(HANDLE h, BOOL abs_, const void *r){ (void)h;(void)abs_;(void)r; return 1; }
/* Willows extension: the Terminal forwards the keys you type to the program it started (it receives them as window messages) */
static int WINAPI c_WillowsSendKey(int vk, int ch){ if(vk) ci_key(vk,ch); if(ch && !vk){ int c=ch==13? '\n' : ch; if(c=='\n'||c==8||c>=32) kb_inject((char)c); } return 1; }

#define CO(n) {"kernel32", #n, c_##n}
const struct export con_exports[] = {
  CO(GetNumberOfConsoleInputEvents), CO(ReadConsoleInputA), CO(ReadConsoleInputW), CO(PeekConsoleInputA), CO(PeekConsoleInputW), CO(FlushConsoleInputBuffer),
  CO(SetConsoleCursorPosition), CO(SetConsoleCursorInfo), CO(GetConsoleCursorInfo), CO(SetConsoleTextAttribute), CO(GetConsoleScreenBufferInfo),
  CO(FillConsoleOutputCharacterA), CO(FillConsoleOutputAttribute), CO(SetConsoleScreenBufferSize), CO(SetConsoleWindowInfo), CO(WillowsSendKey),
};
const unsigned con_nexports = sizeof con_exports / sizeof *con_exports;
