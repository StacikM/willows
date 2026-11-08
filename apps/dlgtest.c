/* Dialog manager test: a dialog built from an in-memory DLGTEMPLATE, like the ones inside .exe resources. */
#include <stdio.h>
#include <string.h>
#include "winui.h"
static unsigned char tpl[4096]; static int tp;
static void put_w(unsigned v){ tpl[tp++] = v & 255; tpl[tp++] = (v >> 8) & 255; }
static void put_d(unsigned v){ put_w(v & 0xFFFF); put_w(v >> 16); }
static void put_s(const char *s){ do put_w((unsigned char)*s); while(*s++); }
static void al4(void){ while(tp & 3) tpl[tp++] = 0; }
static int nitems; static int cdit_at;
static void item(unsigned style, int x, int y, int cx, int cy, int id, unsigned cls, const char *text){
  al4(); put_d(style | 0x50000000u); put_d(0); put_w(x); put_w(y); put_w(cx); put_w(cy); put_w(id); put_w(0xFFFF); put_w(cls); put_s(text); put_w(0); nitems++;
}
#define WS_TAB 0x10000u
#define WS_GRP 0x20000u
static void header(const char *title, int cx, int cy){
  put_d(0x80C80000u | 0x40); put_d(0); cdit_at = tp; put_w(0); put_w(0); put_w(0); put_w(cx); put_w(cy); put_w(0); put_w(0); put_s(title); put_w(8); put_s("MS Shell Dlg");
}
#define B_(x) 0x80u
static LRESULT WINAPI dlgproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  if(m == 0x110){                                        /* WM_INITDIALOG */
    API(user32, SetDlgItemTextA)(h, 101, "Willows");
    const char *names[] = {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel", "india", "juliet"};
    for(int i = 0; i < 10; i++){ API(user32, SendDlgItemMessageA)(h, 110, 0x180, 0, (LPARAM)names[i]); API(user32, SendDlgItemMessageA)(h, 111, 0x143, 0, (LPARAM)names[i]); }
    API(user32, SendDlgItemMessageA)(h, 111, 0x14E, 2, 0);
    API(user32, SendDlgItemMessageA)(h, 110, 0x186, 1, 0);
    API(user32, CheckDlgButton)(h, 102, 1); API(user32, CheckRadioButton)(h, 103, 105, 104);
    return 1;
  }
  if(m == 0x111){                                        /* WM_COMMAND */
    int id = (int)(wp & 0xFFFF);
    if(id == 1){ char t[100]; API(user32, GetDlgItemTextA)(h, 101, t, sizeof t); int cs = (int)API(user32, SendDlgItemMessageA)(h, 110, 0x188, 0, 0);
      int rad = API(user32, IsDlgButtonChecked)(h, 103) ? 1 : API(user32, IsDlgButtonChecked)(h, 104) ? 2 : 3;
      char msg[300]; snprintf(msg, sizeof msg, "text='%s' check=%d radio=%d list=%d", t, API(user32, IsDlgButtonChecked)(h, 102), rad, cs);
      API(user32, EndDialog)(h, 1); printf("RESULT %s\n", msg); return 1; }
    if(id == 2){ API(user32, EndDialog)(h, 2); printf("CANCELLED\n"); return 1; }
  }
  return 0;
}
int main(void){
  header("Dialog test", 220, 130);
  item(0x00000000u | 0, 8, 8, 60, 10, -1, 0x82, "&Name:");
  item(WS_TAB | 0x80, 70, 6, 90, 12, 101, 0x81, "");
  item(WS_TAB | 3, 8, 24, 80, 10, 102, 0x80, "Auto &check");
  item(7, 8, 38, 90, 44, -1, 0x80, "Mode");
  item(WS_TAB | WS_GRP | 9, 14, 48, 70, 10, 103, 0x80, "Fast");
  item(9, 14, 60, 70, 10, 104, 0x80, "Normal");
  item(9, 14, 72, 70, 10, 105, 0x80, "Slow");
  item(WS_TAB | 0x00800000u, 110, 24, 70, 58, 110, 0x83, "");
  item(WS_TAB | 3, 8, 88, 90, 80, 111, 0x85, "");
  item(WS_TAB | 1, 110, 104, 50, 14, 1, 0x80, "OK");
  item(WS_TAB, 164, 104, 50, 14, 2, 0x80, "Cancel");
  tpl[cdit_at] = (unsigned char)nitems;
  long long r = API(user32, DialogBoxIndirectParamA)(0, tpl, 0, (void*)dlgproc, 0);
  printf("dialog returned %lld\n", r);
  return 0;
}
