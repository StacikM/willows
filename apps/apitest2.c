/* apitest2: Win32 batch 2 - pipes (anonymous, named, redirected child stdin/stdout), file mappings, native registry/system queries, file helpers. */
#include <stdio.h>
#include <string.h>
#include "winui.h"
typedef unsigned short WCHAR; typedef long LONG; typedef unsigned long long U64;
#define W(s) ((const WCHAR *)u##s)
/* pipe and mapping handles are 64-bit integers here; the Win32 file calls take HANDLE pointers */
#define RF(h, ...) API(kernel32, ReadFile)((HANDLE)(U64)(h), __VA_ARGS__)
#define WF(h, ...) API(kernel32, WriteFile)((HANDLE)(U64)(h), __VA_ARGS__)
#define CH(h) API(kernel32, CloseHandle)((HANDLE)(U64)(h))
IMP(kernel32, BOOL, CreatePipe, (U64*, U64*, void*, DWORD))
IMP(kernel32, BOOL, PeekNamedPipe, (U64, void*, DWORD, DWORD*, DWORD*, DWORD*))
IMP(kernel32, U64, CreateNamedPipeA, (const char*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*))
IMP(kernel32, BOOL, ConnectNamedPipe, (U64, void*))
IMP(kernel32, DWORD, GetLastError, (void))
IMP(kernel32, DWORD, WaitForSingleObject, (HANDLE, DWORD))
IMP(kernel32, BOOL, GetExitCodeProcess, (HANDLE, DWORD*))
IMP(kernel32, U64, CreateFileMappingA, (U64, void*, DWORD, DWORD, DWORD, const char*))
IMP(kernel32, U64, OpenFileMappingA, (DWORD, BOOL, const char*))
IMP(kernel32, void*, MapViewOfFile, (U64, DWORD, DWORD, DWORD, U64))
IMP(kernel32, BOOL, UnmapViewOfFile, (void*))
IMP(kernel32, DWORD, GetFinalPathNameByHandleA, (U64, char*, DWORD, DWORD))
IMP(kernel32, BOOL, GetDiskFreeSpaceExW, (const WCHAR*, U64*, U64*, U64*))
IMP(kernel32, BOOL, LockFileEx, (U64, DWORD, DWORD, DWORD, DWORD, void*))
IMP(kernel32, BOOL, SetEnvironmentVariableW, (const WCHAR*, const WCHAR*))
IMP(ntdll, long, NtOpenKey, (U64*, DWORD, void*))
IMP(ntdll, long, NtQueryValueKey, (U64, void*, int, void*, DWORD, DWORD*))
IMP(ntdll, long, NtEnumerateKey, (U64, DWORD, int, void*, DWORD, DWORD*))
IMP(ntdll, long, NtQuerySystemInformation, (int, void*, DWORD, DWORD*))
IMP(ntdll, long, NtQueryInformationProcess, (HANDLE, int, void*, DWORD, DWORD*))
IMP(ntdll, long, RtlExpandEnvironmentStrings, (void*, const WCHAR*, DWORD, WCHAR*, DWORD, DWORD*))
IMP(ntdll, long, NtClose, (U64))

static int fails, total, verbose;
static void check(const char *what, int ok){ total++; if(!ok || verbose) printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if(!ok) fails++; }
static int weq(const WCHAR *a, const WCHAR *b){ while(*a && *a == *b){ a++; b++; } return *a == *b; }
typedef struct { unsigned short len, max; unsigned pad; const WCHAR *buf; } USTR;
typedef struct { unsigned len, pad; U64 root; USTR *name; unsigned attr, pad2; void *sd, *sqos; } OBJATTR;
static void oa_init(OBJATTR *oa, USTR *us, const WCHAR *path){ int n = 0; while(path[n]) n++; us->len = (unsigned short)(n * 2); us->max = us->len + 2; us->pad = 0; us->buf = path; memset(oa, 0, sizeof *oa); oa->len = sizeof *oa; oa->name = us; oa->attr = 0x40; }

int main(int argc, char **argv){
  verbose = argc > 1; (void)argv; char buf[256]; DWORD n = 0;
  /* ---- anonymous pipe ---- */
  U64 rd, wr; check("CreatePipe", API(kernel32, CreatePipe)(&rd, &wr, 0, 0));
  WF(wr, "hello pipe", 10, &n, 0); check("WriteFile to the pipe", n == 10);
  DWORD avail = 0; API(kernel32, PeekNamedPipe)(rd, 0, 0, 0, &avail, 0); check("PeekNamedPipe sees 10 bytes", avail == 10);
  memset(buf, 0, sizeof buf); check("ReadFile from the pipe", RF(rd, buf, 5, &n, 0) && n == 5 && !memcmp(buf, "hello", 5));
  check("the rest is still there", RF(rd, buf, 64, &n, 0) && n == 5 && !memcmp(buf, " pipe", 5));
  CH(wr); check("reading after the writer closed -> end of file (ERROR_BROKEN_PIPE)", !RF(rd, buf, 8, &n, 0) && API(kernel32, GetLastError)() == 109);
  CH(rd);

  /* ---- named pipe, server and client in one process ---- */
  U64 srv = API(kernel32, CreateNamedPipeA)("\\\\.\\pipe\\willows-test", 3, 0, 1, 4096, 4096, 0, 0); check("CreateNamedPipeA", srv != (U64)-1);
  U64 cli = (U64)API(kernel32, CreateFileA)("\\\\.\\pipe\\willows-test", 0xC0000000u, 0, 0, 3, 0, 0); check("client opens \\\\.\\pipe\\willows-test with CreateFile", cli != (U64)-1);
  check("ConnectNamedPipe after the client connected -> ERROR_PIPE_CONNECTED", !API(kernel32, ConnectNamedPipe)(srv, 0) && API(kernel32, GetLastError)() == 535);
  WF(cli, "ping", 4, &n, 0); memset(buf, 0, sizeof buf); RF(srv, buf, 16, &n, 0); check("client -> server", n == 4 && !memcmp(buf, "ping", 4));
  WF(srv, "pong!", 5, &n, 0); memset(buf, 0, sizeof buf); RF(cli, buf, 16, &n, 0); check("server -> client", n == 5 && !memcmp(buf, "pong!", 5));
  CH(cli); CH(srv);

  /* ---- a child process whose stdin and stdout are pipes (echo.exe asks a name, greets, exits with the name's length) ---- */
  U64 in_r, in_w, out_r, out_w; API(kernel32, CreatePipe)(&in_r, &in_w, 0, 0); API(kernel32, CreatePipe)(&out_r, &out_w, 0, 0);
  unsigned char si[104]; memset(si, 0, sizeof si); *(DWORD*)si = 104; *(DWORD*)(si + 60) = 0x100; *(U64*)(si + 80) = in_r; *(U64*)(si + 88) = out_w; *(U64*)(si + 96) = out_w;
  unsigned pi[8]; memset(pi, 0, sizeof pi); char cmd[] = "echo.exe";
  int started = API(kernel32, CreateProcessA)(0, cmd, 0, 0, 1, 0, 0, 0, si, (PROCESS_INFORMATION*)pi); check("CreateProcessA with redirected stdin/stdout", started);
  CH(out_w); CH(in_r);                               /* the parent keeps only its own ends */
  WF(in_w, "willow\n", 7, &n, 0);
  char got[300]; int gl = 0; for(;;){ DWORD k = 0; if(!RF(out_r, got + gl, (DWORD)(sizeof got - 1 - gl), &k, 0) || !k) break; gl += (int)k; } got[gl] = 0;
  check("captured the child's output", strstr(got, "Hello, willow!") != 0);
  DWORD code = 0; API(kernel32, WaitForSingleObject)(*(HANDLE*)pi, 5000); API(kernel32, GetExitCodeProcess)(*(HANDLE*)pi, &code); check("child exit code is the name length (6)", code == 6);
  CH(in_w); CH(out_r);

  /* ---- file mappings ---- */
  U64 m1 = API(kernel32, CreateFileMappingA)((U64)-1, 0, 4, 0, 4096, "Local\\willows-shm"); check("CreateFileMapping (named, anonymous)", m1 != 0);
  char *v1 = API(kernel32, MapViewOfFile)(m1, 6, 0, 0, 0); check("MapViewOfFile", v1 != 0); if(v1) memcpy(v1, "shared bytes", 13);
  U64 m2 = API(kernel32, OpenFileMappingA)(6, 0, "Local\\willows-shm"); char *v2 = m2 ? API(kernel32, MapViewOfFile)(m2, 6, 0, 0, 0) : 0;
  check("a second open of the same name sees the same memory", v2 && !strcmp(v2, "shared bytes")); if(v2) v2[0] = 'S'; check("...and writes are shared", v1 && v1[0] == 'S');
  API(kernel32, UnmapViewOfFile)(v1); API(kernel32, UnmapViewOfFile)(v2); CH(m1); CH(m2);
  HANDLE hf = API(kernel32, CreateFileA)("/Documents/maptest.txt", 0xC0000000u, 0, 0, 2, 0, 0); WF(hf, "0123456789", 10, &n, 0);
  U64 mf = API(kernel32, CreateFileMappingA)((U64)hf, 0, 4, 0, 0, 0); char *vf = API(kernel32, MapViewOfFile)(mf, 6, 0, 0, 0); check("file-backed mapping shows the file contents", vf && !memcmp(vf, "0123456789", 10));
  if(vf){ vf[3] = 'X'; API(kernel32, UnmapViewOfFile)(vf); } CH(mf);
  HANDLE hg = API(kernel32, CreateFileA)("/Documents/maptest.txt", 0x80000000u, 0, 0, 3, 0, 0); memset(buf, 0, sizeof buf); RF(hg, buf, 10, &n, 0); check("changes through the view are written back to the file", n == 10 && !memcmp(buf, "012X456789", 10));
  char fp[260]; DWORD fl = API(kernel32, GetFinalPathNameByHandleA)((U64)hg, fp, sizeof fp, 0); check("GetFinalPathNameByHandle", fl > 4 && !memcmp(fp, "\\\\?\\C:\\", 7)); check("LockFileEx", API(kernel32, LockFileEx)((U64)hg, 2, 0, 10, 0, 0));
  CH(hg); CH(hf);
  U64 fa = 0, ft = 0, ff = 0; check("GetDiskFreeSpaceExW", API(kernel32, GetDiskFreeSpaceExW)(W("C:\\"), &fa, &ft, &ff) && ft > 0);

  /* ---- native registry and system queries ---- */
  OBJATTR oa; USTR us; oa_init(&oa, &us, W("\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion")); U64 key = 0;
  check("NtOpenKey(\\Registry\\Machine\\...)", API(ntdll, NtOpenKey)(&key, 0x20019, &oa) == 0 && key);
  USTR vn; vn.len = 22; vn.max = 24; vn.pad = 0; vn.buf = W("ProductName"); unsigned char kv[256]; DWORD rl = 0;
  check("NtQueryValueKey(ProductName, partial info)", API(ntdll, NtQueryValueKey)(key, &vn, 2, kv, sizeof kv, &rl) == 0 && *(DWORD*)(kv + 4) == 1 && weq((WCHAR*)(kv + 12), W("Windows 10 Pro")));
  check("NtQueryValueKey with a tiny buffer -> STATUS_BUFFER_OVERFLOW", API(ntdll, NtQueryValueKey)(key, &vn, 2, kv, 4, &rl) == (long)0x80000005 && rl > 12);
  vn.buf = W("Nope"); vn.len = 8; check("missing value -> STATUS_OBJECT_NAME_NOT_FOUND", API(ntdll, NtQueryValueKey)(key, &vn, 2, kv, sizeof kv, &rl) == (long)0xC0000034);
  U64 sk = 0; oa_init(&oa, &us, W("\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System")); API(ntdll, NtOpenKey)(&sk, 0x20019, &oa);
  check("NtEnumerateKey lists CentralProcessor", API(ntdll, NtEnumerateKey)(sk, 0, 0, kv, sizeof kv, &rl) == 0 || API(ntdll, NtEnumerateKey)(sk, 1, 0, kv, sizeof kv, &rl) == 0);
  API(ntdll, NtClose)(key); API(ntdll, NtClose)(sk);
  unsigned char si2[64]; check("NtQuerySystemInformation(processor) says AMD64", API(ntdll, NtQuerySystemInformation)(1, si2, sizeof si2, &rl) == 0 && *(unsigned short*)si2 == 9);
  check("NtQuerySystemInformation(time of day): boot time before now", API(ntdll, NtQuerySystemInformation)(3, si2, sizeof si2, &rl) == 0 && *(U64*)si2 < *(U64*)(si2 + 8));
  check("NtQuerySystemInformation(basic): page size 4096", API(ntdll, NtQuerySystemInformation)(0, si2, sizeof si2, &rl) == 0 && *(DWORD*)(si2 + 8) == 4096);
  unsigned char pbi[48]; check("NtQueryInformationProcess(basic) gives a PEB and a pid", API(ntdll, NtQueryInformationProcess)((HANDLE)-1, 0, pbi, sizeof pbi, &rl) == 0 && *(U64*)(pbi + 8) != 0 && *(U64*)(pbi + 32) != 0);
  API(kernel32, SetEnvironmentVariableW)(W("WTEST"), W("wood")); WCHAR ex[100]; DWORD need = 0; API(ntdll, RtlExpandEnvironmentStrings)(0, W("a-%WTEST%-b"), 11, ex, 100, &need);
  check("RtlExpandEnvironmentStrings", weq(ex, W("a-wood-b")));
  printf("\n%s: %d of %d checks passed\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", total - fails, total);
  return fails;
}
