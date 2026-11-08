/* Tiny freestanding "Windows SDK" for x64: imports are pointers named __imp_<dll>__<func>, which tools/mkpe.py
   turns into a real PE import table. Compile with -m64 -mabi=ms (Win64 calling convention). */
#define WINAPI __attribute__((ms_abi))
typedef unsigned int DWORD, UINT, BOOL; typedef void *HANDLE; typedef unsigned long long SIZE_T;
#define IMP(dll, ret, name, args) extern ret (WINAPI *__imp_##dll##__##name) args;
IMP(kernel32, void,   ExitProcess,   (UINT))
IMP(kernel32, HANDLE, GetStdHandle,  (DWORD))
IMP(kernel32, BOOL,   WriteFile,     (HANDLE, const void*, DWORD, DWORD*, void*))
IMP(kernel32, BOOL,   ReadFile,      (HANDLE, void*, DWORD, DWORD*, void*))
IMP(kernel32, DWORD,  GetTickCount,  (void))
IMP(kernel32, void,   Sleep,         (DWORD))
IMP(kernel32, HANDLE, GetProcessHeap,(void))
IMP(kernel32, void*,  HeapAlloc,     (HANDLE, DWORD, SIZE_T))
IMP(kernel32, DWORD,  GetLastError,  (void))
IMP(kernel32, void,   SetLastError,  (DWORD))
IMP(kernel32, BOOL,   CreateDirectoryA, (const char*, void*))   /* deliberately unimplemented */
IMP(kernel32, DWORD,  TlsAlloc,      (void))
IMP(kernel32, BOOL,   TlsSetValue,   (DWORD, void*))
IMP(kernel32, void*,  TlsGetValue,   (DWORD))
IMP(kernel32, HANDLE, GetModuleHandleA, (const char*))
IMP(kernel32, void*,  GetProcAddress,(HANDLE, const char*))
IMP(kernel32, HANDLE, LoadLibraryA,  (const char*))
IMP(user32,   int,    MessageBoxA,   (HANDLE, const char*, const char*, UINT))
IMP(user32,   HANDLE, GetDC,         (HANDLE))
IMP(user32,   int,    ReleaseDC,     (HANDLE, HANDLE))
IMP(user32,   int,    GetSystemMetrics, (int))
IMP(user32,   int,    FillRect,      (HANDLE, const void*, HANDLE))
IMP(user32,   int,    GetAsyncKeyState, (int))
IMP(gdi32,    HANDLE, CreateSolidBrush, (DWORD))
IMP(gdi32,    BOOL,   DeleteObject,  (HANDLE))
IMP(gdi32,    DWORD,  SetPixel,      (HANDLE, int, int, DWORD))
IMP(gdi32,    DWORD,  SetTextColor,  (HANDLE, DWORD))
IMP(gdi32,    DWORD,  SetBkColor,    (HANDLE, DWORD))
IMP(gdi32,    int,    SetBkMode,     (HANDLE, int))
IMP(gdi32,    BOOL,   TextOutA,      (HANDLE, int, int, const char*, int))
typedef struct { int left, top, right, bottom; } RECT;
typedef struct { int x, y; } POINT;
IMP(user32,   BOOL,   GetCursorPos,  (POINT*))
#define RGB(r,g,b) ((DWORD)((r)|((g)<<8)|((b)<<16)))
extern int  (*__imp_msvcrt__printf)(const char*, ...);
extern int  (*__imp_msvcrt____getmainargs)(int*, char***, char***, int, void*);
extern void (*__imp_msvcrt____set_app_type)(int);
extern void (*__imp_msvcrt___initterm)(void (**)(void), void (**)(void));
extern void (*__imp_msvcrt__exit)(int);
extern int  (*__imp_msvcrt__atexit)(void (*)(void));
extern int  (*__imp_msvcrt__sprintf)(char*, const char*, ...);
extern int  (*__imp_msvcrt__fprintf)(void*, const char*, ...);
extern char *(*__imp_msvcrt____iob_func)(void);
#define ExitProcess __imp_kernel32__ExitProcess
#define GetStdHandle __imp_kernel32__GetStdHandle
#define WriteFile __imp_kernel32__WriteFile
#define ReadFile __imp_kernel32__ReadFile
#define GetTickCount __imp_kernel32__GetTickCount
#define Sleep __imp_kernel32__Sleep
#define GetProcessHeap __imp_kernel32__GetProcessHeap
#define HeapAlloc __imp_kernel32__HeapAlloc
#define GetLastError __imp_kernel32__GetLastError
#define SetLastError __imp_kernel32__SetLastError
#define CreateDirectoryA __imp_kernel32__CreateDirectoryA
#define TlsAlloc __imp_kernel32__TlsAlloc
#define TlsSetValue __imp_kernel32__TlsSetValue
#define TlsGetValue __imp_kernel32__TlsGetValue
#define GetModuleHandleA __imp_kernel32__GetModuleHandleA
#define GetProcAddress __imp_kernel32__GetProcAddress
#define LoadLibraryA __imp_kernel32__LoadLibraryA
#define MessageBoxA __imp_user32__MessageBoxA
#define GetDC __imp_user32__GetDC
#define ReleaseDC __imp_user32__ReleaseDC
#define GetSystemMetrics __imp_user32__GetSystemMetrics
#define FillRect __imp_user32__FillRect
#define GetAsyncKeyState __imp_user32__GetAsyncKeyState
#define GetCursorPos __imp_user32__GetCursorPos
#define CreateSolidBrush __imp_gdi32__CreateSolidBrush
#define DeleteObject __imp_gdi32__DeleteObject
#define SetPixel __imp_gdi32__SetPixel
#define SetTextColor __imp_gdi32__SetTextColor
#define SetBkColor __imp_gdi32__SetBkColor
#define SetBkMode __imp_gdi32__SetBkMode
#define TextOutA __imp_gdi32__TextOutA
#define printf __imp_msvcrt__printf
#define __getmainargs __imp_msvcrt____getmainargs
#define __set_app_type __imp_msvcrt____set_app_type
#define _initterm __imp_msvcrt___initterm
#define exit __imp_msvcrt__exit
#define atexit __imp_msvcrt__atexit
#define sprintf __imp_msvcrt__sprintf
#define fprintf __imp_msvcrt__fprintf
#define stderr (__imp_msvcrt____iob_func() + 128)

/* ---- Winsock / ICMP ---- */
typedef unsigned long long SOCKET; typedef unsigned short WORD;
struct sockaddr_in { WORD family; WORD port; unsigned int addr; char zero[8]; };
struct hostent { char *h_name; char **h_aliases; short h_addrtype; short h_length; char **h_addr_list; };
IMP(ws2_32, int,    WSAStartup,  (WORD, void*))
IMP(ws2_32, int,    WSAGetLastError, (void))
IMP(ws2_32, SOCKET, socket,      (int, int, int))
IMP(ws2_32, int,    connect,     (SOCKET, const void*, int))
IMP(ws2_32, int,    bind,        (SOCKET, const void*, int))
IMP(ws2_32, int,    listen,      (SOCKET, int))
IMP(ws2_32, SOCKET, accept,      (SOCKET, void*, int*))
IMP(ws2_32, int,    send,        (SOCKET, const void*, int, int))
IMP(ws2_32, int,    recv,        (SOCKET, void*, int, int))
IMP(ws2_32, int,    closesocket, (SOCKET))
IMP(ws2_32, WORD,   htons,       (WORD))
IMP(ws2_32, unsigned int, inet_addr, (const char*))
IMP(ws2_32, char*,  inet_ntoa,   (unsigned int))
IMP(ws2_32, struct hostent*, gethostbyname, (const char*))
IMP(iphlpapi, HANDLE, IcmpCreateFile, (void))
IMP(iphlpapi, DWORD,  IcmpSendEcho,   (HANDLE, unsigned int, void*, WORD, void*, void*, DWORD, DWORD))
#define WSAStartup __imp_ws2_32__WSAStartup
#define WSAGetLastError __imp_ws2_32__WSAGetLastError
#define socket __imp_ws2_32__socket
#define connect __imp_ws2_32__connect
#define bind __imp_ws2_32__bind
#define listen __imp_ws2_32__listen
#define accept __imp_ws2_32__accept
#define send __imp_ws2_32__send
#define recv __imp_ws2_32__recv
#define closesocket __imp_ws2_32__closesocket
#define htons __imp_ws2_32__htons
#define inet_addr __imp_ws2_32__inet_addr
#define inet_ntoa __imp_ws2_32__inet_ntoa
#define gethostbyname __imp_ws2_32__gethostbyname
#define IcmpCreateFile __imp_iphlpapi__IcmpCreateFile
#define IcmpSendEcho __imp_iphlpapi__IcmpSendEcho

/* ---- WinINet / URLMon ---- */
IMP(wininet, HANDLE, InternetOpenA,    (const char*, DWORD, const char*, const char*, DWORD))
IMP(wininet, HANDLE, InternetOpenUrlA, (HANDLE, const char*, const char*, DWORD, DWORD, unsigned long long))
IMP(wininet, BOOL,   InternetReadFile, (HANDLE, void*, DWORD, DWORD*))
IMP(wininet, BOOL,   InternetCloseHandle, (HANDLE))
IMP(wininet, BOOL,   HttpQueryInfoA,   (HANDLE, DWORD, void*, DWORD*, DWORD*))
IMP(urlmon,  DWORD,  URLDownloadToFileA, (void*, const char*, const char*, DWORD, void*))
#define InternetOpenA __imp_wininet__InternetOpenA
#define InternetOpenUrlA __imp_wininet__InternetOpenUrlA
#define InternetReadFile __imp_wininet__InternetReadFile
#define InternetCloseHandle __imp_wininet__InternetCloseHandle
#define HttpQueryInfoA __imp_wininet__HttpQueryInfoA
#define URLDownloadToFileA __imp_urlmon__URLDownloadToFileA
