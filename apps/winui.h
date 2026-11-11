#pragma once
/* Windows API declarations for GUI programs built with tools/buildc.sh (-mabi=ms; the libc comes from apps/shim).
   Imports are pointers named __imp_<dll>__<func> that tools/mkpe.py turns into a real import table. */
#define WINAPI __attribute__((ms_abi))
typedef unsigned int UINT, DWORD, BOOL; typedef void *HANDLE; typedef unsigned long long WPARAM; typedef long long LPARAM, LRESULT;
typedef struct { int left, top, right, bottom; } RECT; typedef struct { int x, y; } POINT; typedef struct { int cx, cy; } SIZE;
typedef LRESULT (WINAPI *WNDPROC)(HANDLE, UINT, WPARAM, LPARAM);
typedef struct { UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HANDLE hInstance, hIcon, hCursor, hbrBackground; const char *lpszMenuName, *lpszClassName; } WNDCLASSA;
typedef struct { HANDLE hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct { HANDLE hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; unsigned char rgb[32]; } PAINTSTRUCT;
typedef struct { int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth; int more[20]; } TEXTMETRICA;
#define IMP(dll, ret, name, args) extern ret (WINAPI *__imp_##dll##__##name) args;
#define API(dll, name) __imp_##dll##__##name
IMP(kernel32, HANDLE, GetModuleHandleA, (const char*))
IMP(kernel32, void,   ExitProcess,   (UINT))
IMP(user32, unsigned short, RegisterClassA, (const WNDCLASSA*))
IMP(user32, HANDLE, CreateWindowExA, (DWORD, const char*, const char*, DWORD, int, int, int, int, HANDLE, HANDLE, HANDLE, void*))
IMP(user32, BOOL,   ShowWindow,      (HANDLE, int))
IMP(user32, BOOL,   GetMessageA,     (MSG*, HANDLE, UINT, UINT))
IMP(user32, BOOL,   TranslateMessage,(const MSG*))
IMP(user32, LRESULT,DispatchMessageA,(const MSG*))
IMP(user32, LRESULT,DefWindowProcA,  (HANDLE, UINT, WPARAM, LPARAM))
IMP(user32, void,   PostQuitMessage, (int))
IMP(user32, BOOL,   GetClientRect,   (HANDLE, RECT*))
IMP(user32, BOOL,   InvalidateRect,  (HANDLE, const RECT*, BOOL))
IMP(user32, BOOL,   UpdateWindow,    (HANDLE))
IMP(user32, HANDLE, BeginPaint,      (HANDLE, PAINTSTRUCT*))
IMP(user32, BOOL,   EndPaint,        (HANDLE, const PAINTSTRUCT*))
IMP(user32, int,    FillRect,        (HANDLE, const RECT*, HANDLE))
IMP(user32, HANDLE, LoadCursorA,     (HANDLE, const char*))
IMP(user32, HANDLE, SetCursor,       (HANDLE))
IMP(user32, BOOL,   SetWindowTextA,  (HANDLE, const char*))
IMP(user32, short,  GetKeyState,     (int))
IMP(gdi32, HANDLE,  CreateFontA,     (int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,const char*))
IMP(gdi32, HANDLE,  SelectObject,    (HANDLE, HANDLE))
IMP(gdi32, BOOL,    DeleteObject,    (HANDLE))
IMP(gdi32, HANDLE,  CreateSolidBrush,(DWORD))
IMP(gdi32, DWORD,   SetTextColor,    (HANDLE, DWORD))
IMP(gdi32, int,     SetBkMode,       (HANDLE, int))
IMP(gdi32, BOOL,    TextOutA,        (HANDLE, int, int, const char*, int))
IMP(gdi32, BOOL,    GetTextExtentPoint32A, (HANDLE, const char*, int, SIZE*))
IMP(gdi32, BOOL,    GetTextMetricsA, (HANDLE, TEXTMETRICA*))
IMP(wininet, HANDLE, InternetOpenA,  (const char*, DWORD, const char*, const char*, DWORD))
IMP(wininet, HANDLE, InternetOpenUrlA, (HANDLE, const char*, const char*, DWORD, DWORD, unsigned long long))
IMP(kernel32, HANDLE, LoadLibraryA,    (const char*))
IMP(kernel32, void*,  GetProcAddress,  (HANDLE, const char*))
IMP(wininet, BOOL,  InternetReadFile,(HANDLE, void*, DWORD, DWORD*))
IMP(wininet, BOOL,  InternetCloseHandle, (HANDLE))
IMP(wininet, BOOL,  InternetQueryOptionA, (HANDLE, DWORD, void*, DWORD*))
IMP(wininet, BOOL,  HttpQueryInfoA,  (HANDLE, DWORD, void*, DWORD*, DWORD*))
#define WS_OVERLAPPEDWINDOW 0x00CF0000u
#define WS_VISIBLE 0x10000000u
#define CW_USEDEFAULT 0x80000000u
#define RGB(r,g,b) ((DWORD)((r)|((g)<<8)|((b)<<16)))
enum { WM_DESTROY=2, WM_SIZE=5, WM_PAINT=0xF, WM_CLOSE=0x10, WM_KEYDOWN=0x100, WM_CHAR=0x102, WM_MOUSEMOVE=0x200, WM_LBUTTONDOWN=0x201, WM_LBUTTONUP=0x202, WM_MOUSEWHEEL=0x20A };

/* ---- more kernel32 / misc (terminal) ---- */
typedef struct { DWORD attr, ct[2], at[2], wt[2], sizeHigh, sizeLow, r0, r1; char name[260]; char alt[14]; } WIN32_FIND_DATAA;
typedef struct { unsigned short y, mo, dow, d, h, mi, s, ms; } SYSTEMTIME;
typedef struct { DWORD len, load; unsigned long long total, avail, tpf, apf, tv, av, aev; } MEMORYSTATUSEX;
typedef struct { DWORD size, usage, pid; unsigned long long heap; DWORD module, threads, parent; int pri; DWORD flags; char exe[260]; } PROCESSENTRY32A;
typedef struct { HANDLE hProcess, hThread; DWORD pid, tid; } PROCESS_INFORMATION;
IMP(kernel32, HANDLE, FindFirstFileA, (const char*, WIN32_FIND_DATAA*))
IMP(kernel32, BOOL,   FindNextFileA,  (HANDLE, WIN32_FIND_DATAA*))
IMP(kernel32, BOOL,   FindClose,      (HANDLE))
IMP(kernel32, BOOL,   CopyFileA,      (const char*, const char*, BOOL))
IMP(kernel32, BOOL,   MoveFileA,      (const char*, const char*))
IMP(kernel32, BOOL,   DeleteFileA,    (const char*))
IMP(kernel32, HANDLE, CreateFileA,    (const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE))
IMP(kernel32, BOOL,   ReadFile,       (HANDLE, void*, DWORD, DWORD*, void*))
IMP(kernel32, BOOL,   WriteFile,      (HANDLE, const void*, DWORD, DWORD*, void*))
IMP(kernel32, BOOL,   CloseHandle,    (HANDLE))
IMP(kernel32, DWORD,  GetFileSize,    (HANDLE, DWORD*))
IMP(kernel32, BOOL,   CreateProcessA, (const char*, char*, void*, void*, BOOL, DWORD, void*, const char*, void*, PROCESS_INFORMATION*))
IMP(kernel32, HANDLE, OpenProcess,    (DWORD, BOOL, DWORD))
IMP(kernel32, BOOL,   TerminateProcess, (HANDLE, UINT))
IMP(kernel32, HANDLE, CreateToolhelp32Snapshot, (DWORD, DWORD))
IMP(kernel32, BOOL,   Process32First, (HANDLE, PROCESSENTRY32A*))
IMP(kernel32, BOOL,   Process32Next,  (HANDLE, PROCESSENTRY32A*))
IMP(kernel32, BOOL,   GlobalMemoryStatusEx, (MEMORYSTATUSEX*))
IMP(kernel32, unsigned long long, GetTickCount64, (void))
IMP(kernel32, void,   GetLocalTime,   (SYSTEMTIME*))
IMP(kernel32, void,   Sleep,          (DWORD))
IMP(user32,   BOOL,   PeekMessageA,   (MSG*, HANDLE, UINT, UINT, UINT))
IMP(user32,   BOOL,   ExitWindowsEx,  (DWORD, DWORD))
IMP(ws2_32,   int,    WSAStartup,     (unsigned short, void*))
struct hostent_ { char *h_name; char **h_aliases; short h_addrtype; short h_length; char **h_addr_list; };
IMP(ws2_32,   struct hostent_*, gethostbyname, (const char*))
IMP(ws2_32,   char*,  inet_ntoa,      (unsigned int))
IMP(iphlpapi, HANDLE, IcmpCreateFile, (void))
IMP(iphlpapi, DWORD,  IcmpSendEcho,   (HANDLE, unsigned int, void*, unsigned short, void*, void*, DWORD, DWORD))
IMP(kernel32, BOOL,   CreateDirectoryA, (const char*, void*))
IMP(kernel32, BOOL,   RemoveDirectoryA, (const char*))
IMP(kernel32, BOOL,   SetCurrentDirectoryA, (const char*))
IMP(kernel32, DWORD,  GetCurrentDirectoryA, (DWORD, char*))
IMP(kernel32, DWORD,  GetFileAttributesA, (const char*))
IMP(shell32,  HANDLE, ShellExecuteA,  (HANDLE, const char*, const char*, const char*, const char*, int))
IMP(user32,   HANDLE, SetFocus,        (HANDLE))
IMP(user32,   long long, SetWindowLongPtrA, (HANDLE, int, long long))
IMP(user32,   LRESULT, CallWindowProcA, (long long, HANDLE, UINT, WPARAM, LPARAM))
IMP(user32,   int,    GetWindowTextA,  (HANDLE, char*, int))
IMP(user32,   int,    GetWindowTextLengthA, (HANDLE))
IMP(user32,   BOOL,   MoveWindow,      (HANDLE, int, int, int, int, BOOL))
IMP(user32,   UINT,   SetTimer,        (HANDLE, UINT, UINT, void*))
IMP(user32,   BOOL,   KillTimer,       (HANDLE, UINT))
IMP(user32,   int,    MessageBoxA,     (HANDLE, const char*, const char*, UINT))
IMP(user32,   HANDLE, GetParent,       (HANDLE))
IMP(comdlg32, BOOL,   GetSaveFileNameA, (void*))
#define ES_MULTILINE 4
#define WS_CHILD 0x40000000u
#define WS_BORDER 0x00800000u
IMP(gdi32, int, StretchDIBits, (HANDLE, int, int, int, int, int, int, int, int, const void*, const void*, UINT, DWORD))
IMP(user32, long long, DialogBoxIndirectParamA, (HANDLE, const void*, HANDLE, void*, long long))
IMP(user32, BOOL, EndDialog, (HANDLE, long long))
IMP(user32, long long, SendDlgItemMessageA, (HANDLE, int, UINT, WPARAM, LPARAM))
IMP(user32, UINT, GetDlgItemTextA, (HANDLE, int, char*, int))
IMP(user32, BOOL, SetDlgItemTextA, (HANDLE, int, const char*))
IMP(user32, BOOL, CheckDlgButton, (HANDLE, int, UINT))
IMP(user32, UINT, IsDlgButtonChecked, (HANDLE, int))
IMP(user32, BOOL, CheckRadioButton, (HANDLE, int, int, int))
IMP(kernel32, int, WillowsReadOutput, (DWORD, char*, DWORD))
IMP(kernel32, int, WillowsProcessRunning, (DWORD))
IMP(kernel32, int, WillowsSetSetting, (const char*, const char*))
IMP(kernel32, int, WillowsGetSetting, (const char*, char*, DWORD))
IMP(kernel32, int, WillowsGetGpuName, (char*, DWORD))
IMP(kernel32, unsigned long long, WillowsGetMemory, (int))
IMP(kernel32, int, WillowsGetResolution, (void))
