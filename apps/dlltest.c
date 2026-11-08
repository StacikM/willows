/* dlltest: static import of wlib.dll (which imports wdep.dll), then LoadLibrary/GetProcAddress/FreeLibrary of wdyn.dll */
#include <stdio.h>
#include "winui.h"
IMP(wlib, int, Add, (int, int))
IMP(wlib, int, AddThenTriple, (int, int))
IMP(wlib, void, Hello, (void))
IMP(kernel32, HANDLE, LoadLibraryA, (const char*))
IMP(kernel32, void*, GetProcAddress, (HANDLE, const char*))
IMP(kernel32, BOOL, FreeLibrary, (HANDLE))
int main(void){
  printf("static: Add(2,3)=%d  AddThenTriple(2,3)=%d\n", API(wlib, Add)(2, 3), API(wlib, AddThenTriple)(2, 3));
  API(wlib, Hello)();
  printf("GetModuleHandle(wlib)=%s\n", API(kernel32, GetModuleHandleA)("wlib") ? "found" : "missing");
  HANDLE h = API(kernel32, LoadLibraryA)("wdyn.dll");
  int (WINAPI *mul)(int, int) = h ? API(kernel32, GetProcAddress)(h, "Mul") : 0;
  printf("dynamic: LoadLibrary=%s  Mul(6,7)=%d\n", h ? "ok" : "FAILED", mul ? mul(6, 7) : -1);
  printf("missing export -> %p\n", h ? API(kernel32, GetProcAddress)(h, "Nope") : (void*)1);
  printf("missing dll -> %p\n", API(kernel32, LoadLibraryA)("nosuch.dll"));
  if(h) API(kernel32, FreeLibrary)(h);
  printf("done\n"); return 0;
}
