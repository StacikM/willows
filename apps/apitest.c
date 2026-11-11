/* apitest: exercises the Win32 APIs of batch 1 (registry, locks, COM/OLE, shell folders, paths, processes) and prints PASS/FAIL lines. */
#include <stdio.h>
#include <string.h>
#include "winui.h"
typedef unsigned short WCHAR; typedef long LONG; typedef unsigned long long U64;
#define W(s) ((const WCHAR *)u##s)
IMP(advapi32, LONG, RegOpenKeyExA, (U64, const char*, DWORD, DWORD, U64*))
IMP(advapi32, LONG, RegOpenKeyExW, (U64, const WCHAR*, DWORD, DWORD, U64*))
IMP(advapi32, LONG, RegCreateKeyExW, (U64, const WCHAR*, DWORD, void*, DWORD, DWORD, void*, U64*, DWORD*))
IMP(advapi32, LONG, RegQueryValueExA, (U64, const char*, DWORD*, DWORD*, unsigned char*, DWORD*))
IMP(advapi32, LONG, RegQueryValueExW, (U64, const WCHAR*, DWORD*, DWORD*, unsigned char*, DWORD*))
IMP(advapi32, LONG, RegSetValueExW, (U64, const WCHAR*, DWORD, DWORD, const unsigned char*, DWORD))
IMP(advapi32, LONG, RegGetValueW, (U64, const WCHAR*, const WCHAR*, DWORD, DWORD*, void*, DWORD*))
IMP(advapi32, LONG, RegEnumKeyExW, (U64, DWORD, WCHAR*, DWORD*, DWORD*, WCHAR*, DWORD*, void*))
IMP(advapi32, LONG, RegEnumValueW, (U64, DWORD, WCHAR*, DWORD*, DWORD*, DWORD*, unsigned char*, DWORD*))
IMP(advapi32, LONG, RegQueryInfoKeyW, (U64, WCHAR*, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*, void*))
IMP(advapi32, LONG, RegDeleteKeyW, (U64, const WCHAR*))
IMP(advapi32, LONG, RegDeleteValueW, (U64, const WCHAR*))
IMP(advapi32, LONG, RegCloseKey, (U64))
IMP(kernel32, void, InitializeSRWLock, (U64*))
IMP(kernel32, void, AcquireSRWLockExclusive, (U64*))
IMP(kernel32, void, ReleaseSRWLockExclusive, (U64*))
IMP(kernel32, void, AcquireSRWLockShared, (U64*))
IMP(kernel32, void, ReleaseSRWLockShared, (U64*))
IMP(kernel32, unsigned char, TryAcquireSRWLockExclusive, (U64*))
IMP(kernel32, BOOL, InitOnceExecuteOnce, (U64*, BOOL (WINAPI *)(void*, void*, void**), void*, void**))
IMP(kernel32, BOOL, WaitOnAddress, (volatile void*, void*, U64, DWORD))
IMP(kernel32, void, InitializeSListHead, (U64*))
IMP(kernel32, U64*, InterlockedPushEntrySList, (U64*, U64*))
IMP(kernel32, U64*, InterlockedPopEntrySList, (U64*))
IMP(kernel32, unsigned short, QueryDepthSList, (U64*))
IMP(kernel32, DWORD, GetProcessId, (HANDLE))
IMP(kernel32, BOOL, CreateProcessW, (const WCHAR*, WCHAR*, void*, void*, BOOL, DWORD, void*, const WCHAR*, void*, unsigned*))
IMP(kernel32, DWORD, WaitForSingleObject, (HANDLE, DWORD))
IMP(kernel32, BOOL, GetExitCodeProcess, (HANDLE, DWORD*))
IMP(kernel32, HANDLE, LocalFree, (HANDLE))
IMP(kernel32, HANDLE, GetCurrentProcess, (void))
IMP(ole32, long, CoInitializeEx, (void*, DWORD))
IMP(ole32, void*, CoTaskMemAlloc, (U64))
IMP(ole32, void, CoTaskMemFree, (void*))
IMP(ole32, long, CoCreateGuid, (unsigned char*))
IMP(ole32, int, StringFromGUID2, (const unsigned char*, WCHAR*, int))
IMP(ole32, long, CLSIDFromString, (const WCHAR*, unsigned char*))
IMP(ole32, long, CoCreateInstance, (const void*, void*, DWORD, const void*, void**))
IMP(oleaut32, WCHAR*, SysAllocString, (const WCHAR*))
IMP(oleaut32, unsigned, SysStringLen, (const WCHAR*))
IMP(oleaut32, void, SysFreeString, (WCHAR*))
IMP(shell32, long, SHGetKnownFolderPath, (const unsigned char*, DWORD, HANDLE, WCHAR**))
IMP(shell32, long, SHGetFolderPathW, (HANDLE, int, HANDLE, DWORD, WCHAR*))
IMP(shell32, WCHAR**, CommandLineToArgvW, (const WCHAR*, int*))
IMP(shlwapi, const WCHAR*, PathFindFileNameW, (const WCHAR*))
IMP(shlwapi, WCHAR*, PathCombineW, (WCHAR*, const WCHAR*, const WCHAR*))
IMP(shlwapi, BOOL, PathFileExistsW, (const WCHAR*))
IMP(shlwapi, BOOL, PathRemoveFileSpecW, (WCHAR*))
IMP(shlwapi, const WCHAR*, PathFindExtensionW, (const WCHAR*))
IMP(psapi, BOOL, EnumProcesses, (DWORD*, DWORD, DWORD*))

static int fails, total, verbose;
static void check(const char *what, int ok){ total++; if(!ok || verbose) printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if(!ok) fails++; }
static int weq(const WCHAR *a, const WCHAR *b){ while(*a && *a == *b){ a++; b++; } return *a == *b; }
static const char *narrow(const WCHAR *w){ static char buf[8][260]; static int k; char *b = buf[k++ & 7]; int i = 0; for(; w && w[i] && i < 259; i++) b[i] = w[i] < 128 ? (char)w[i] : '?'; b[i] = 0; return b; }
#define HKLM 0x80000002ull
#define HKCU 0x80000001ull
static U64 gl; static int once_calls;
static BOOL WINAPI once_fn(void *o, void *p, void **c){ (void)o; (void)p; once_calls++; *c = 0; return 1; }

int main(int argc_, char **argv_){
  verbose = argc_ > 1; (void)argv_;                                       /* apitest -v lists every check, otherwise only failures */
  /* ---- registry ---- */
  U64 k = 0; DWORD t = 0, n = 0; unsigned char buf[256];
  check("RegOpenKeyExW(HKLM\\...\\CurrentVersion)", API(advapi32, RegOpenKeyExW)(HKLM, W("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), 0, 0x20019, &k) == 0);
  n = sizeof buf; check("RegQueryValueExW(ProductName)", API(advapi32, RegQueryValueExW)(k, W("ProductName"), 0, &t, buf, &n) == 0 && t == 1 && weq((WCHAR*)buf, W("Windows 10 Pro")));
  n = sizeof buf; check("RegQueryValueExA(ProductName) sees the same string", API(advapi32, RegQueryValueExA)(k, "ProductName", 0, &t, buf, &n) == 0 && !strcmp((char*)buf, "Windows 10 Pro"));
  n = 4; DWORD dw = 0; check("RegQueryValueExW(CurrentMajorVersionNumber) = 10", API(advapi32, RegQueryValueExW)(k, W("CurrentMajorVersionNumber"), 0, &t, (unsigned char*)&dw, &n) == 0 && dw == 10);
  n = 1; check("RegQueryValueExW with a small buffer -> ERROR_MORE_DATA", API(advapi32, RegQueryValueExW)(k, W("ProductName"), 0, &t, buf, &n) == 234 && n == 30);
  check("missing value -> ERROR_FILE_NOT_FOUND", API(advapi32, RegQueryValueExW)(k, W("NoSuchValue"), 0, &t, buf, &n) == 2);
  n = sizeof buf; check("RegGetValueW(sub-key, RRF_RT_REG_SZ)", API(advapi32, RegGetValueW)(HKLM, W("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), W("CurrentBuild"), 2, &t, buf, &n) == 0 && weq((WCHAR*)buf, W("19045")));
  n = sizeof buf; check("RegGetValueW with the wrong type -> ERROR_UNSUPPORTED_TYPE", API(advapi32, RegGetValueW)(HKLM, W("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), W("CurrentBuild"), 0x10, &t, buf, &n) == 1630);
  API(advapi32, RegCloseKey)(k);
  U64 tk = 0; DWORD disp = 0;
  check("RegCreateKeyExW creates HKCU\\Software\\WillowsTest\\Sub", API(advapi32, RegCreateKeyExW)(HKCU, W("Software\\WillowsTest\\Sub"), 0, 0, 0, 0xF003F, 0, &tk, &disp) == 0 && disp == 1);
  dw = 12345; check("RegSetValueExW(DWORD)", API(advapi32, RegSetValueExW)(tk, W("Count"), 0, 4, (unsigned char*)&dw, 4) == 0);
  check("RegSetValueExW(string)", API(advapi32, RegSetValueExW)(tk, W("Name"), 0, 1, (const unsigned char*)W("willow"), 14) == 0);
  dw = 0; n = 4; check("value round trip (DWORD)", API(advapi32, RegQueryValueExW)(tk, W("Count"), 0, &t, (unsigned char*)&dw, &n) == 0 && dw == 12345);
  U64 pk = 0; API(advapi32, RegOpenKeyExW)(HKCU, W("Software\\WillowsTest"), 0, 0x20019, &pk);
  WCHAR name[64]; n = 64; check("RegEnumKeyExW lists the sub-key", API(advapi32, RegEnumKeyExW)(pk, 0, name, &n, 0, 0, 0, 0) == 0 && weq(name, W("Sub")));
  n = 64; check("RegEnumKeyExW past the end -> ERROR_NO_MORE_ITEMS", API(advapi32, RegEnumKeyExW)(pk, 1, name, &n, 0, 0, 0, 0) == 259);
  DWORD ns = 0, nv = 0; API(advapi32, RegQueryInfoKeyW)(tk, 0, 0, 0, &ns, 0, 0, &nv, 0, 0, 0, 0); check("RegQueryInfoKeyW: 0 subkeys, 2 values", ns == 0 && nv == 2);
  n = 64; DWORD dl = 8; check("RegEnumValueW(0) = Count", API(advapi32, RegEnumValueW)(tk, 0, name, &n, 0, &t, (unsigned char*)&dw, &dl) == 0 && weq(name, W("Count")) && t == 4);
  check("RegDeleteValueW", API(advapi32, RegDeleteValueW)(tk, W("Name")) == 0);
  API(advapi32, RegCloseKey)(tk);
  check("RegDeleteKeyW removes the sub-key", API(advapi32, RegDeleteKeyW)(pk, W("Sub")) == 0);

  /* ---- locks ---- */
  API(kernel32, InitializeSRWLock)(&gl); API(kernel32, AcquireSRWLockExclusive)(&gl); check("TryAcquireSRWLockExclusive fails while held", API(kernel32, TryAcquireSRWLockExclusive)(&gl) == 0);
  API(kernel32, ReleaseSRWLockExclusive)(&gl); check("SRW lock free after release", gl == 0);
  API(kernel32, AcquireSRWLockShared)(&gl); API(kernel32, AcquireSRWLockShared)(&gl); check("two shared holders", gl == 4); API(kernel32, ReleaseSRWLockShared)(&gl); API(kernel32, ReleaseSRWLockShared)(&gl); check("shared locks released", gl == 0);
  U64 once = 0; void *ctx; API(kernel32, InitOnceExecuteOnce)(&once, once_fn, 0, &ctx); API(kernel32, InitOnceExecuteOnce)(&once, once_fn, 0, &ctx); check("InitOnceExecuteOnce runs the callback once", once_calls == 1);
  int a = 1, b = 2; check("WaitOnAddress returns at once when the value differs", API(kernel32, WaitOnAddress)(&a, &b, 4, 1000) == 1);
  U64 head[2], e1[2], e2[2]; API(kernel32, InitializeSListHead)(head); API(kernel32, InterlockedPushEntrySList)(head, e1); API(kernel32, InterlockedPushEntrySList)(head, e2);
  check("SList depth 2, pops in LIFO order", API(kernel32, QueryDepthSList)(head) == 2 && API(kernel32, InterlockedPopEntrySList)(head) == e2 && API(kernel32, InterlockedPopEntrySList)(head) == e1 && !API(kernel32, InterlockedPopEntrySList)(head));

  /* ---- COM / OLE ---- */
  check("CoInitializeEx: S_OK then S_FALSE", API(ole32, CoInitializeEx)(0, 2) == 0 && API(ole32, CoInitializeEx)(0, 2) == 1);
  unsigned char *m = API(ole32, CoTaskMemAlloc)(100); check("CoTaskMemAlloc", m != 0); API(ole32, CoTaskMemFree)(m);
  unsigned char g1[16], g2[16], g3[16]; API(ole32, CoCreateGuid)(g1); API(ole32, CoCreateGuid)(g2); check("CoCreateGuid gives different GUIDs", memcmp(g1, g2, 16) != 0);
  WCHAR gs[64]; check("StringFromGUID2 -> 39 chars", API(ole32, StringFromGUID2)(g1, gs, 64) == 39 && gs[0] == '{' && gs[37] == '}');
  check("CLSIDFromString round trip", API(ole32, CLSIDFromString)(gs, g3) == 0 && !memcmp(g1, g3, 16));
  static const unsigned char clsid_ie[16] = {0x13,0x6D,0x2A,0x0D,0x80,0x6C,0xD1,0x11,0x93,0x7B,0x00,0xC0,0x4F,0xB6,0xC6,0x07}; void *obj = (void*)1;
  check("CoCreateInstance of an unknown class -> REGDB_E_CLASSNOTREG", API(ole32, CoCreateInstance)(clsid_ie, 0, 1, clsid_ie, &obj) == (long)0x80040154 && obj == 0);
  WCHAR *bs = API(oleaut32, SysAllocString)(W("hello")); check("SysAllocString/SysStringLen", bs && API(oleaut32, SysStringLen)(bs) == 5 && weq(bs, W("hello"))); API(oleaut32, SysFreeString)(bs);

  /* ---- shell folders, command line, paths ---- */
  static const unsigned char FOLDERID_Documents[16] = {0xD0,0x9A,0xD3,0xFD,0x8F,0x23,0xAF,0x46,0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7};
  WCHAR *kp = 0; check("SHGetKnownFolderPath(Documents)", API(shell32, SHGetKnownFolderPath)(FOLDERID_Documents, 0, 0, &kp) == 0 && kp && weq(kp, W("C:\\Documents"))); printf("     -> %s\n", narrow(kp)); API(ole32, CoTaskMemFree)(kp);
  WCHAR ap[260]; check("SHGetFolderPathW(CSIDL_APPDATA)", API(shell32, SHGetFolderPathW)(0, 0x1A, 0, 0, ap) == 0 && weq(ap, W("C:\\AppData\\Roaming"))); check("...and PathFileExistsW sees the new folder", API(shlwapi, PathFileExistsW)(ap));
  int argc = 0; WCHAR **av = API(shell32, CommandLineToArgvW)(W("prog.exe \"a b\" c\\\"d \"e\\\\\" last"), &argc);
  check("CommandLineToArgvW: 5 arguments", argc == 5 && av && weq(av[0], W("prog.exe")) && weq(av[1], W("a b")) && weq(av[2], W("c\"d")) && weq(av[3], W("e\\")) && weq(av[4], W("last")));
  if(av) API(kernel32, LocalFree)((HANDLE)av);
  check("PathFindFileNameW", weq(API(shlwapi, PathFindFileNameW)(W("C:\\dir\\file.txt")), W("file.txt")));
  check("PathFindExtensionW", weq(API(shlwapi, PathFindExtensionW)(W("C:\\dir\\file.tar.gz")), W(".gz")));
  WCHAR pc[260]; API(shlwapi, PathCombineW)(pc, W("C:\\dir"), W("sub\\x.txt")); check("PathCombineW", weq(pc, W("C:\\dir\\sub\\x.txt")));
  WCHAR rf[260] = u"C:\\dir\\file.txt"; API(shlwapi, PathRemoveFileSpecW)(rf); check("PathRemoveFileSpecW", weq(rf, W("C:\\dir")));

  /* ---- processes ---- */
  check("GetProcessId(current)", API(kernel32, GetProcessId)(API(kernel32, GetCurrentProcess)()) > 0);
  DWORD pids[16], need = 0; check("EnumProcesses lists at least this process", API(psapi, EnumProcesses)(pids, sizeof pids, &need) && need >= 4);
  unsigned pi[8]; memset(pi, 0, sizeof pi); WCHAR cmd[] = u"exit42.exe"; unsigned char si[104]; memset(si, 0, sizeof si); *(DWORD*)si = 104;
  if(API(kernel32, CreateProcessW)(0, cmd, 0, 0, 0, 0, 0, 0, si, pi)){ HANDLE hp = *(HANDLE*)pi; DWORD w = API(kernel32, WaitForSingleObject)(hp, 10000), code = 99; API(kernel32, GetExitCodeProcess)(hp, &code); check("CreateProcessW(exit42.exe) runs and exits with code 42", w == 0 && code == 42); }
  else check("CreateProcessW(exit42.exe)", 0);
  printf("\n%s: %d of %d checks passed\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", total - fails, total);
  return fails;
}
