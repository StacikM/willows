#include "kernel.h"
/* 32-bit (PE32) program support, WOW64-style.
   - Each import of a 32-bit program gets a stub in low memory: "push <table index>; jmp wow_common32" (see boot.S). The stub switches the CPU into
     64-bit mode, wow_thunk64 widens the stdcall arguments and calls the Win64 implementation listed in wow_table[index].
   - The number of 32-bit argument slots must be known for every function (stdcall pops its own arguments), hence the signature list below.
     An API that is not in the list gets a stub that reports it by name and ends the program, instead of corrupting the stack.
   - Each program has a 32-bit TEB/PEB; FS (GDT entry 0x20) points at the TEB of the program that is running. */
extern u64 gdt64[];
extern void wow_common32(void);
struct wow_ent { u64 fn; u32 nargs, pad; };
struct wow_ent wow_table[512]; static int nwow;
static char wow_names[512][48];                                      /* "dll!name" for the entry, to share stubs between imports of the same function */
static u32 wow_stub[512];

static const struct { const char *name; u8 nargs; } SIG[] = {
  /* kernel32 */
  {"GetStdHandle",1},{"WriteFile",5},{"ReadFile",5},{"ExitProcess",1},{"GetCommandLineA",0},{"GetModuleHandleA",1},{"GetLastError",0},{"SetLastError",1},{"Sleep",1},{"GetTickCount",0},{"GetTickCount64",0},
  {"GetProcessHeap",0},{"HeapAlloc",3},{"HeapFree",3},{"HeapReAlloc",4},{"HeapSize",3},{"HeapCreate",3},{"VirtualAlloc",4},{"VirtualFree",3},{"lstrlenA",1},{"lstrcpyA",2},{"lstrcmpA",2},{"CloseHandle",1},
  {"GetCurrentProcessId",0},{"GetCurrentThreadId",0},{"GetCurrentProcess",0},{"WriteConsoleA",5},{"OutputDebugStringA",1},{"CreateFileA",7},{"GetFileSize",2},{"SetFilePointer",4},{"GetEnvironmentVariableA",3},
  {"GetSystemTimeAsFileTime",1},{"QueryPerformanceCounter",1},{"QueryPerformanceFrequency",1},{"LoadLibraryA",1},{"GetProcAddress",2},{"GetVersion",0},{"TlsAlloc",0},{"TlsFree",1},{"TlsGetValue",1},{"TlsSetValue",2},
  {"InitializeCriticalSection",1},{"EnterCriticalSection",1},{"LeaveCriticalSection",1},{"DeleteCriticalSection",1},{"GetACP",0},{"GetStartupInfoA",1},{"GetModuleFileNameA",3},{"GetFileType",1},
  {"MultiByteToWideChar",6},{"WideCharToMultiByte",8},{"GetStringTypeW",5},{"GetCurrentDirectoryA",2},{"SetCurrentDirectoryA",1},{"DeleteFileA",1},{"GetFileAttributesA",1},{"CreateDirectoryA",2},
  {"FlushFileBuffers",1},{"SetEndOfFile",1},{"GetSystemInfo",1},{"IsDebuggerPresent",0},{"IsProcessorFeaturePresent",1},{"GlobalMemoryStatus",1},{"GetProcessAffinityMask",3},{"SetStdHandle",2},{"GetConsoleOutputCP",0},{"ReadConsoleW",5},{"EnumSystemLocalesW",2},{"InitializeSListHead",1},{"EncodePointer",1},{"DecodePointer",1},{"GetLocaleInfoEx",4},{"LCMapStringEx",9},{"AreFileApisANSI",0},{"FreeLibraryAndExitThread",2},{"SetUnhandledExceptionFilter",1},{"UnhandledExceptionFilter",1},{"TerminateProcess",2},{"GetExitCodeProcess",2},
  {"WaitForSingleObject",2},{"CreateEventA",4},{"SetEvent",1},{"ResetEvent",1},{"GetLocalTime",1},{"GetSystemTime",1},{"FindFirstFileA",2},{"FindNextFileA",2},{"FindClose",1},
  {"LoadLibraryW",1},{"LoadLibraryExA",3},{"LoadLibraryExW",3},{"FreeLibrary",1},{"GetModuleHandleW",1},{"GetModuleHandleExW",3},{"GetModuleFileNameW",3},{"GetCommandLineW",0},{"GetEnvironmentVariableW",3},
  {"SetEnvironmentVariableA",2},{"SetEnvironmentVariableW",2},{"ExpandEnvironmentStringsA",3},{"ExpandEnvironmentStringsW",3},{"GetCurrentDirectoryW",2},{"SetCurrentDirectoryW",1},{"GetFullPathNameA",4},{"GetFullPathNameW",4},
  {"CreateFileW",7},{"DeleteFileW",1},{"GetFileAttributesW",1},{"GetFileAttributesExW",3},{"SetFileAttributesW",2},{"CreateDirectoryW",2},{"RemoveDirectoryW",1},{"RemoveDirectoryA",1},{"MoveFileW",2},{"MoveFileExW",3},{"CopyFileW",3},{"CopyFileExW",6},
  {"FindFirstFileW",2},{"FindNextFileW",2},{"FindFirstFileExW",6},{"GetTempPathA",2},{"GetTempPathW",2},{"GetTempFileNameW",4},{"GetFileSizeEx",2},{"SetFilePointerEx",5},{"GetFileInformationByHandle",2},{"GetFileTime",4},{"SetFileTime",4},
  {"GetDiskFreeSpaceExW",4},{"GetDriveTypeW",1},{"GetLogicalDrives",0},{"GetWindowsDirectoryW",2},{"GetSystemDirectoryW",2},{"GetSystemWindowsDirectoryW",2},{"GetVersionExA",1},{"GetVersionExW",1},{"GetNativeSystemInfo",1},
  {"GetComputerNameW",2},{"GetUserDefaultLCID",0},{"GetUserDefaultUILanguage",0},{"GetSystemDefaultUILanguage",0},{"GetThreadLocale",0},{"GetLocaleInfoW",4},{"GetUserDefaultLocaleName",2},{"GetOEMCP",0},{"GetCPInfo",2},
  {"VirtualProtect",4},{"VirtualQuery",3},{"GlobalAlloc",2},{"GlobalFree",1},{"GlobalLock",1},{"GlobalUnlock",1},{"LocalAlloc",2},{"LocalFree",1},{"HeapDestroy",1},{"HeapCompact",2},{"HeapValidate",3},
  {"CreateEventW",4},{"CreateMutexA",3},{"CreateMutexW",3},{"ReleaseMutex",1},{"OpenMutexW",3},{"CreateSemaphoreW",4},{"ReleaseSemaphore",3},{"WaitForMultipleObjects",4},{"WaitForSingleObjectEx",3},{"SleepEx",2},
  {"CreateThread",6},{"ExitThread",1},{"GetExitCodeThread",2},{"SetThreadPriority",2},{"SuspendThread",1},{"ResumeThread",1},{"TlsAlloc",0},{"InitializeCriticalSectionAndSpinCount",2},{"InitializeCriticalSectionEx",3},{"TryEnterCriticalSection",1},
  {"InterlockedIncrement",1},{"InterlockedDecrement",1},{"InterlockedExchange",2},{"InterlockedCompareExchange",3},{"InterlockedExchangeAdd",2},
  {"CreateProcessW",10},{"CreateProcessA",10},{"OpenProcess",3},{"GetProcessId",1},{"GetCurrentProcessToken",0},{"OpenProcessToken",3},{"GetStartupInfoW",1},{"ExitProcess",1},{"GetEnvironmentStringsW",0},{"FreeEnvironmentStringsW",1},
  {"FormatMessageW",7},{"FormatMessageA",7},{"lstrlenW",1},{"lstrcpyW",2},{"lstrcmpW",2},{"lstrcmpiW",2},{"lstrcatW",2},{"lstrcpynW",3},{"CompareStringW",6},{"CompareStringA",6},{"LCMapStringW",6},{"GetStringTypeA",5},
  {"IsValidCodePage",1},{"IsValidLocale",2},{"GetLocalTime",1},{"FileTimeToSystemTime",2},{"SystemTimeToFileTime",2},{"FileTimeToLocalFileTime",2},{"GetTimeZoneInformation",1},{"GetSystemTimeAdjustment",3},
  {"OutputDebugStringW",1},{"RaiseException",4},{"SetLastErrorEx",2},{"SetErrorMode",1},{"GetErrorMode",0},{"DisableThreadLibraryCalls",1},{"SetConsoleCtrlHandler",2},{"GetConsoleMode",2},{"SetConsoleMode",2},{"WriteConsoleW",5},
  {"GetCurrentThread",0},{"DuplicateHandle",7},{"SetHandleInformation",3},{"GetHandleInformation",2},{"CreatePipe",4},{"PeekNamedPipe",6},{"DeviceIoControl",8},{"ReadFileEx",5},{"WriteFileEx",5},{"CancelIo",1},{"GetOverlappedResult",4},
  {"CreateFileMappingW",6},{"MapViewOfFile",5},{"UnmapViewOfFile",1},{"FlushViewOfFile",2},{"GetProcAddress",2},{"EnumResourceNamesW",4},{"FindResourceW",3},{"FindResourceExW",4},{"LoadResource",2},{"LockResource",1},{"SizeofResource",2},
  {"GetUserNameW",2},{"MulDiv",3},{"GetTickCount64",0},{"GetSystemFirmwareTable",4},{"GetLogicalProcessorInformation",2},{"GetPhysicallyInstalledSystemMemory",1},
  /* advapi32 */
  {"RegOpenKeyExW",5},{"RegOpenKeyExA",5},{"RegCreateKeyExW",9},{"RegCreateKeyExA",9},{"RegQueryValueExW",6},{"RegQueryValueExA",6},{"RegSetValueExW",6},{"RegSetValueExA",6},{"RegCloseKey",1},{"RegDeleteKeyW",2},{"RegDeleteValueW",2},
  {"RegEnumKeyExW",8},{"RegEnumValueW",8},{"RegQueryInfoKeyW",12},{"RegFlushKey",1},{"GetUserNameA",2},{"GetTokenInformation",5},{"AllocateAndInitializeSid",11},{"FreeSid",1},{"EqualSid",2},{"CheckTokenMembership",3},{"IsValidSid",1},
  {"OpenThreadToken",4},{"LookupPrivilegeValueW",3},{"AdjustTokenPrivileges",6},{"CryptAcquireContextW",5},{"CryptGenRandom",3},{"CryptReleaseContext",2},{"CryptCreateHash",5},{"CryptHashData",4},{"CryptDestroyHash",1},{"CryptGetHashParam",5},
  /* shell32 / ole32 */
  {"ShellExecuteW",6},{"ShellExecuteA",6},{"SHGetFolderPathW",5},{"SHGetSpecialFolderPathW",4},{"SHGetKnownFolderPath",4},{"CoInitialize",1},{"CoInitializeEx",2},{"CoUninitialize",0},{"CoTaskMemAlloc",1},{"CoTaskMemFree",1},{"CoCreateInstance",5},
  /* user32 */
  {"MessageBoxA",4},{"MessageBoxW",4},{"GetDesktopWindow",0},{"GetSystemMetrics",1},{"PostQuitMessage",1},{"GetForegroundWindow",0},{"GetKeyState",1},{"GetAsyncKeyState",1},
  /* msvcrt (cdecl in reality: the caller pops, so the stub must pop nothing - listed with 0 is wrong for them; left out on purpose) */
};
const char *wow_name(u32 i){ return i<512? wow_names[i] : "?"; }
static int sig_args(const char *name){ for(unsigned i=0;i<sizeof SIG/sizeof *SIG;i++) if(!strcmp(SIG[i].name,name)) return SIG[i].nargs; return -1; }

/* GetProcAddress for 32-bit programs: the answer must be a 32-bit stub, never the address of a 64-bit function. Functions whose stdcall signature is
   unknown to the table above are reported as missing (0), which makes the C runtime fall back (e.g. FlsAlloc -> TlsAlloc). */
u32 wow_import(const char *dll, const char *name);
static u64 WINAPI wow_getproc(u64 hmod, const char *name){
  (void)hmod; if((u64)name<0x10000 || sig_args(name)<0) return 0;
  static const char *const dlls[]={"KERNEL32.dll","USER32.dll","ADVAPI32.dll","SHELL32.dll","OLE32.dll"};
  for(unsigned i=0;i<sizeof dlls/sizeof *dlls;i++) if(win32_resolve(dlls[i],name)) return wow_import(dlls[i],name);
  return 0;
}

/* APIs that fill in structures containing pointers or handles: the 64-bit versions write bigger structures than a 32-bit program has room for,
   so 32-bit programs get these versions that write the 32-bit layouts. */
static void WINAPI wow_GetStartupInfo(u8 *si){ memset(si,0,68); *(u32*)si=68; }                      /* STARTUPINFO is 68 bytes in 32-bit */
static void WINAPI wow_GetSystemInfo(u8 *si){                                                          /* SYSTEM_INFO is 36 bytes */
  memset(si,0,36); *(u16*)si=0; *(u32*)(si+4)=4096; *(u32*)(si+8)=0x10000; *(u32*)(si+12)=0x7FFEFFFF; *(u32*)(si+16)=1; *(u32*)(si+20)=1; *(u32*)(si+24)=586; *(u32*)(si+28)=0x10000; *(u16*)(si+32)=6;
}
static void WINAPI wow_GlobalMemoryStatus(u8 *ms){                                                     /* MEMORYSTATUS: 32 bytes of DWORDs */
  u32 v[8]={32,30,0x40000000,0x30000000,0x60000000,0x50000000,0x7FFE0000,0x70000000}; memcpy(ms,v,32);
}
static void *wow_special(const char *name){
  if(!strcmp(name,"GetStartupInfoW")||!strcmp(name,"GetStartupInfoA")) return (void*)wow_GetStartupInfo;
  if(!strcmp(name,"GetSystemInfo")||!strcmp(name,"GetNativeSystemInfo")) return (void*)wow_GetSystemInfo;
  if(!strcmp(name,"GlobalMemoryStatus")) return (void*)wow_GlobalMemoryStatus;
  if(!strcmp(name,"GetProcAddress")) return (void*)wow_getproc;
  return 0;
}

/* the 32-bit address of the stub for dll!name (created on first use) */
u32 wow_import(const char *dll, const char *name){
  char key[48]; unsigned k=0; for(const char *p=dll;*p&&k<24;p++) key[k++]=(*p>='A'&&*p<='Z')? *p+32 : *p; key[k++]='!'; for(const char *p=name;*p&&k<46;p++) key[k++]=*p; key[k]=0;
  for(int i=0;i<nwow;i++) if(!strcmp(wow_names[i],key)) return wow_stub[i];
  if(nwow>=512) return 0;
  int na=sig_args(name); void *fn= na>=0? win32_resolve(dll,name) : 0;
  { void *sp=wow_special(name); if(sp){ fn=sp; if(na<0) na=1; } }
  if(!fn){ fn=pe_make_stub(dll,name); na=0; }                       /* unknown signature or unavailable: a stub that names it and ends the program */
  int idx=nwow++; memcpy(wow_names[idx],key,k+1); wow_table[idx].fn=(u64)fn; wow_table[idx].nargs=(u32)na;
  u8 *p=kmalloc(16,16); if(!p) return 0;
  p[0]=0x68; *(u32*)(p+1)=(u32)idx;                                    /* push idx */
  p[5]=0xE9; *(i32*)(p+6)=(i32)((i64)(u64)wow_common32-((i64)(u64)p+10));   /* jmp wow_common32 */
  wow_stub[idx]=(u32)(u64)p; return wow_stub[idx];
}

/* ---- per-process 32-bit environment ---- */
void wow_init_proc(struct proc *p, const struct pe_image *img){
  u8 *t=kmalloc(0x2000,0x1000), *pb=kmalloc(0x1000,16); if(!t||!pb) return; memset(t,0,0x2000); memset(pb,0,0x1000);
  *(u32*)(t+0x00)=0xFFFFFFFFu;                                         /* SEH chain: end */
  *(u32*)(t+0x04)=(u32)img->stack_top; *(u32*)(t+0x08)=(u32)img->stack_lo;
  *(u32*)(t+0x18)=(u32)(u64)t;                                         /* self */
  *(u32*)(t+0x20)=4+(u32)p->pid; *(u32*)(t+0x24)=8+(u32)p->pid;      /* process and thread ids */
  *(u32*)(t+0x30)=(u32)(u64)pb;                                        /* PEB */
  pb[0x02]=0; *(u32*)(pb+0x08)=(u32)img->base;                         /* BeingDebugged, ImageBaseAddress */
  *(u32*)(pb+0xA4)=10; *(u32*)(pb+0xA8)=0; *(u16*)(pb+0xAC)=19045; *(u32*)(pb+0xB0)=2;   /* Windows 10.0.19045 */
  p->teb32=t;
}
void wow_load_fs(struct proc *p){                                      /* FS must address the running 32-bit program's TEB */
  if(!p->teb32) return;
  u64 b=(u64)p->teb32; gdt64[4]=0x00CF92000000FFFFull | ((b&0xFFFFFFull)<<16) | (((b>>24)&0xFFull)<<56);
  __asm__ volatile("mov %0,%%fs"::"r"((u16)0x20));
}
