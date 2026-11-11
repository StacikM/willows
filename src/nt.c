#include "kernel.h"
/* ntdll system queries that programs like fastfetch, Task Manager clones and runtimes use to learn about the machine:
   NtQuerySystemInformation, NtQueryInformationProcess/Token, RtlExpandEnvironmentStrings, RtlGetNtVersionNumbers, BrandingFormatString. */
typedef u32 DWORD; typedef u64 HANDLE;
#define ST_OK 0
#define ST_INFO_LENGTH_MISMATCH 0xC0000004
#define ST_INVALID_INFO_CLASS 0xC0000003
#define ST_BUFFER_TOO_SMALL 0xC0000023
#define ST_NOT_IMPLEMENTED 0xC0000002
static void *K32(const char *n){ return win32_resolve("kernel32",n); }
static int wlen(const u16 *s){ int n=0; while(s&&s[n]) n++; return n; }
static int a2w(const char *a, u16 *w, int n){ int i=0; for(;a[i]&&i<n-1;i++) w[i]=(u8)a[i]; w[i]=0; return i; }
static u64 filetime_now(void){ void (WINAPI *f)(u64*)=K32("GetSystemTimeAsFileTime"); u64 t=0; if(f) f(&t); return t; }

static long WINAPI nt_NtQuerySystemInformation(u32 cls, u8 *buf, u32 len, u32 *ret){
  u64 up_ms=ticks;                                                         /* the tick counter is in milliseconds */
  switch(cls){
    case 0: {                                                             /* SystemBasicInformation */
      if(ret) *ret=64; if(len<64) return (long)ST_INFO_LENGTH_MISMATCH; memset(buf,0,64);
      *(u32*)(buf+4)=156250; *(u32*)(buf+8)=4096; u32 pages=(u32)(mm_total()/4096); *(u32*)(buf+12)=pages; *(u32*)(buf+16)=1; *(u32*)(buf+20)=pages; *(u32*)(buf+24)=65536;
      *(u64*)(buf+32)=0x10000; *(u64*)(buf+40)=0x7FFFFFFEFFFFull; *(u64*)(buf+48)=1; buf[56]=1; return 0; }
    case 1: {                                                             /* SystemProcessorInformation: architecture 9 = AMD64 */
      if(ret) *ret=12; if(len<12) return (long)ST_INFO_LENGTH_MISMATCH; memset(buf,0,12); *(u16*)buf=9; *(u16*)(buf+2)=6; *(u16*)(buf+4)=0x9E0A; *(u16*)(buf+6)=1; *(u32*)(buf+8)=0; return 0; }
    case 3: {                                                             /* SystemTimeOfDayInformation: boot time, current time (100 ns since 1601) */
      if(ret) *ret=48; if(len<48) return (long)ST_INFO_LENGTH_MISMATCH; memset(buf,0,48); u64 now=filetime_now(); *(u64*)buf=now-up_ms*10000ull; *(u64*)(buf+8)=now; return 0; }
    case 8: {                                                             /* SystemProcessorPerformanceInformation: one entry per CPU */
      if(ret) *ret=48; if(len<48) return (long)ST_INFO_LENGTH_MISMATCH; memset(buf,0,48); u64 t=up_ms*10000ull; *(u64*)buf=t*9/10; *(u64*)(buf+8)=t/10; *(u64*)(buf+16)=t/20; return 0; }
    case 0x4B: {                                                          /* SystemProcessorBrandString-like requests are not provided */
      return (long)ST_NOT_IMPLEMENTED; }
    default: return (long)ST_INVALID_INFO_CLASS;
  }
}
static long WINAPI nt_NtQueryInformationProcess(HANDLE h, u32 cls, u8 *buf, u32 len, u32 *ret){
  (void)h;
  switch(cls){
    case 0: {                                                             /* ProcessBasicInformation */
      if(ret) *ret=48; if(len<48) return (long)ST_INFO_LENGTH_MISMATCH; memset(buf,0,48); *(u64*)(buf+8)=(u64)cur->peb; *(u64*)(buf+16)=1; *(u64*)(buf+32)=(u64)cur->pid; return 0; }
    case 26: { if(ret) *ret=8; if(len<8) return (long)ST_INFO_LENGTH_MISMATCH; *(u64*)buf=0; return 0; }              /* ProcessWow64Information: not WOW64 */
    case 27: {                                                            /* ProcessImageFileName: a UNICODE_STRING followed by its text */
      char t[80]="\\??\\C:\\Willows\\"; int n=(int)strlen(t); for(int i=0;cur->name[i]&&n<78;i++) t[n++]=cur->name[i]; t[n]=0;
      u32 need=16+(u32)(n+1)*2; if(ret) *ret=need; if(len<need) return (long)ST_INFO_LENGTH_MISMATCH;
      u16 *w=(u16*)(buf+16); a2w(t,w,n+1); *(u16*)buf=(u16)(n*2); *(u16*)(buf+2)=(u16)(n*2+2); *(u64*)(buf+8)=(u64)w; return 0; }
    case 7: { if(ret) *ret=8; if(len<8) return (long)ST_INFO_LENGTH_MISMATCH; *(u64*)buf=0; return 0; }                                 /* ProcessDebugPort: not debugged */
    case 31: { if(ret) *ret=4; if(len<4) return (long)ST_INFO_LENGTH_MISMATCH; *(u32*)buf=0; return 0; }                              /* ProcessDebugFlags */
    default: return (long)ST_INVALID_INFO_CLASS;
  }
}
static u8 fake_sid[16] = {1, 2, 0,0,0,0,0, 5, 21,0,0,0, 1,0,0,0};          /* S-1-5-21-1: one sub-authority */
static long WINAPI nt_NtQueryInformationToken(HANDLE h, u32 cls, u8 *buf, u32 len, u32 *ret){
  (void)h;
  switch(cls){
    case 1: {                                                             /* TokenUser: SID_AND_ATTRIBUTES then the SID */
      u32 need=16+16; if(ret) *ret=need; if(len<need) return (long)ST_BUFFER_TOO_SMALL; memcpy(buf+16,fake_sid,16); *(u64*)buf=(u64)(buf+16); *(u32*)(buf+8)=0; return 0; }
    case 8: { if(ret) *ret=4; if(len<4) return (long)ST_BUFFER_TOO_SMALL; *(u32*)buf=1; return 0; }                                    /* TokenType: primary */
    case 18: { if(ret) *ret=4; if(len<4) return (long)ST_BUFFER_TOO_SMALL; *(u32*)buf=1; return 0; }                                  /* TokenElevationType: default */
    case 20: { if(ret) *ret=4; if(len<4) return (long)ST_BUFFER_TOO_SMALL; *(u32*)buf=0; return 0; }                                  /* TokenElevation: not elevated */
    case 12: { if(ret) *ret=4; if(len<4) return (long)ST_BUFFER_TOO_SMALL; *(u32*)buf=1; return 0; }                                  /* TokenSessionId */
    default: return (long)ST_INVALID_INFO_CLASS;
  }
}
/* RtlExpandEnvironmentStrings(env, src, srcChars, dst, dstChars, &needed): replace %NAME% using the process environment */
static long expand(const u16 *src, u32 n, u16 *dst, u32 dmax, u32 *need){
  u32 o=0; u32 total=0; DWORD (WINAPI *ge)(const u16*,u16*,DWORD)=K32("GetEnvironmentVariableW");
  for(u32 i=0;i<n && src[i];){
    if(src[i]=='%'){ u32 j=i+1; while(j<n && src[j] && src[j]!='%') j++;
      if(j<n && src[j]=='%' && j>i+1){ u16 name[64]; u32 k=0; for(u32 q=i+1;q<j&&k<63;q++) name[k++]=src[q]; name[k]=0; u16 val[260]; DWORD vl= ge? ge(name,val,260) : 0;
        if(vl && vl<260){ for(DWORD q=0;q<vl;q++){ if(o<dmax) dst[o]=val[q]; o++; total++; } i=j+1; continue; } } }
    if(o<dmax) dst[o]=src[i]; o++; total++; i++;
  }
  if(o<dmax) dst[o]=0; o++; if(need) *need=o; return o<=dmax? 0 : (long)ST_BUFFER_TOO_SMALL;
}
static long WINAPI nt_RtlExpandEnvironmentStrings(void *env, const u16 *src, u32 sn, u16 *dst, u32 dn, u32 *need){ (void)env; return expand(src,sn,dst,dn,need); }
static long WINAPI nt_RtlExpandEnvironmentStrings_U(void *env, const u8 *src, u8 *dst, u32 *need){
  (void)env; const u16 *s=*(const u16*const*)(src+8); u32 sn=*(const u16*)src/2; u16 *d=*(u16**)(dst+8); u32 dn=*(const u16*)(dst+2)/2; u32 nd=0; long r=expand(s,sn,d,dn,&nd);
  if(need) *need=nd*2; if(r==0){ *(u16*)dst=(u16)((nd-1)*2); } return r;
}
static void WINAPI nt_RtlGetNtVersionNumbers(u32 *maj, u32 *min, u32 *build){ if(maj) *maj=10; if(min) *min=0; if(build) *build=19045|0xF0000000u; }
static u16 *WINAPI nt_BrandingFormatString(const u16 *fmt){                /* winbrand: expands %WINDOWS_LONG% and friends */
  static const char *const names[]={"%WINDOWS_LONG%","%WINDOWS_SHORT%","%WINDOWS_GENERIC%","%WINDOWS_COPYRIGHT%"}; static const char *const vals[]={"Windows 10 Pro","Windows 10","Windows","Microsoft Corporation"};
  char in[200]; int n=0; for(;fmt&&fmt[n]&&n<199;n++) in[n]=fmt[n]<0x80? (char)fmt[n] : '?'; in[n]=0;
  u16 *out=heap_alloc(512*2); if(!out) return 0; int o=0;
  for(int i=0;in[i]&&o<500;){ int hit=0; for(int k=0;k<4&&!hit;k++){ int l=(int)strlen(names[k]); if(!memcmp(in+i,names[k],l)){ for(const char *v=vals[k];*v;v++) out[o++]=(u8)*v; i+=l; hit=1; } } if(!hit) out[o++]=(u8)in[i++]; }
  out[o]=0; (void)wlen; return out;
}
#define NT(n) {"ntdll", #n, nt_##n}
const struct export nt_exports[] = {
  NT(NtQuerySystemInformation), {"ntdll","ZwQuerySystemInformation",nt_NtQuerySystemInformation}, NT(NtQueryInformationProcess), {"ntdll","ZwQueryInformationProcess",nt_NtQueryInformationProcess}, NT(NtQueryInformationToken),
  NT(RtlExpandEnvironmentStrings), {"ntdll","RtlExpandEnvironmentStrings_U",nt_RtlExpandEnvironmentStrings_U}, NT(RtlGetNtVersionNumbers), {"winbrand","BrandingFormatString",nt_BrandingFormatString},
};
const unsigned nt_nexports = sizeof nt_exports / sizeof *nt_exports;
