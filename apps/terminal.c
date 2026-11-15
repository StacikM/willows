/* Willows Terminal - a terminal emulator with a small shell.
   Scrollback, colours (ANSI SGR), command history, tab completion, pipes (| grep/head/tail/wc/sort) and > / >> redirection.
   Everything is built on the Windows API that Willows implements (files, processes, ICMP, WinINet/HTTPS...). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "winui.h"

static int strncasecmp_(const char *a, const char *b, int n){ for(; n; n--, a++, b++){ int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if(x != y) return x - y; if(!x) return 0; } return 0; }
static int stricmp_(const char *a, const char *b){ for(;; a++, b++){ int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b); if(x != y) return x - y; if(!x) return 0; } }
#define strdup_ strdup
/* ---------------------------------------------------------------- screen model */
#define MAXL 3000
#define MAXC 240
struct cell { unsigned char ch, col, bg; };
static struct cell scr[MAXL][MAXC];
static int head, nl = 1, ocol, ocolor, obg;               /* ring of lines; current output line is nl-1 */
static int cols = 80, rows = 24, cw = 9, chh = 20, view_off;
static unsigned palette[9] = {0xD0D4DC, 0x4444EF, 0x5EC522, 0x04B3EA, 0xFA9560, 0xF8A0C0, 0xEED322, 0xF6F4F3, 0x80726B};   /* BGR: default red green yellow blue magenta cyan white gray */
#define LINE(i) scr[(head + (i)) % MAXL]
static HANDLE hwnd, hfont;

static int ovr = -1, ztop = -1;   /* full-screen programs: ovr = line the cursor was moved to (-1: the last line), ztop = line that is row 0 of their screen */
static int curl(void){ return ovr >= 0 ? ovr : nl - 1; }
static void new_line(void){
  ocol = 0;
  if(ovr >= 0){ ovr++; if(ovr >= nl - 1) ovr = -1; return; }
  if(nl < MAXL) nl++; else { head = (head + 1) % MAXL; if(ztop > 0) ztop--; }
  memset(LINE(nl - 1), 0, sizeof LINE(0));
}
static void goto_line(int t){                          /* move the cursor to ring line t, growing the buffer when it lies below the end */
  if(t < 0) t = 0;
  ovr = -1; while(nl - 1 < t) new_line();
  ovr = (t == nl - 1) ? -1 : t;
}
static void put_char(unsigned char c){
  if(c == '\n') new_line();
  else if(c == '\r') ocol = 0;
  else if(c == '\b'){ if(ocol) ocol--; }
  else if(c == '\t'){ do put_char(' '); while(ocol % 8); }
  else if(c >= 32 || c >= 128){ if(ocol >= cols || ocol >= MAXC) new_line(); LINE(curl())[ocol].ch = c; LINE(curl())[ocol].col = ocolor; LINE(curl())[ocol].bg = obg; ocol++; }
}
static void csi(char f, int *p, int np){               /* cursor movement and erase, as sent by SetConsoleCursorPosition, system("cls") etc. */
  int a = np > 0 && p[0] ? p[0] : 1;
  switch(f){
    case 'H': case 'f': { if(ztop < 0){ if(ocol){ new_line(); } ztop = nl - 1; } int c = np > 1 && p[1] ? p[1] : 1; goto_line(ztop + a - 1); ocol = c - 1; if(ocol >= MAXC) ocol = MAXC - 1; break; }
    case 'A': goto_line((ztop >= 0 && curl() - a < ztop) ? ztop : curl() - a); break;
    case 'B': goto_line(curl() + a); break;
    case 'C': ocol += a; if(ocol >= MAXC) ocol = MAXC - 1; break;
    case 'D': ocol -= a; if(ocol < 0) ocol = 0; break;
    case 'K': { int m = np > 0 ? p[0] : 0; struct cell *l = LINE(curl()); if(m == 0) memset(l + ocol, 0, (MAXC - ocol) * sizeof *l); else if(m == 1) memset(l, 0, (ocol + 1) * sizeof *l); else memset(l, 0, sizeof LINE(0)); break; }
    case 'J': { int m = np > 0 ? p[0] : 0;
      if(m == 2 || m == 3){ if(ztop < 0){ if(ocol) new_line(); ztop = nl - 1; } for(int i = ztop; i < nl; i++) memset(LINE(i), 0, sizeof LINE(0)); ovr = -1; goto_line(ztop); }
      else if(m == 0){ struct cell *l = LINE(curl()); memset(l + ocol, 0, (MAXC - ocol) * sizeof *l); for(int i = curl() + 1; i < nl; i++) memset(LINE(i), 0, sizeof LINE(0)); }
      break; }
  }
}
static void apply_sgr(int code){
  if(code == 0){ ocolor = 0; obg = 0; }
  else if(code >= 30 && code <= 37) ocolor = code - 29;
  else if(code == 39) ocolor = 0;
  else if(code >= 90 && code <= 97) ocolor = code == 90 ? 8 : code - 90;
  else if(code == 40) obg = 9; else if(code >= 41 && code <= 47) obg = code - 40;
  else if(code == 49) obg = 0;
  else if(code == 100) obg = 8; else if(code >= 101 && code <= 107) obg = code - 100;
}
static void term_write(const char *s){                /* interprets ESC[..m colour codes; other escape sequences are skipped whole */
  for(; *s; s++){
    if(*s == 27 && s[1] == '['){
      s += 2; int code = 0, any = 0; int params[16], np = 0;
      while(*s && !(*s >= '@' && *s <= '~')){ if(isdigit((unsigned char)*s)){ code = code * 10 + (*s - '0'); any = 1; } else if(*s == ';'){ if(np < 16) params[np++] = any ? code : 0; code = 0; any = 0; } s++; }
      if(!*s) break;
      if(np < 16) params[np++] = any ? code : 0;
      if(*s == 'm'){ for(int i = 0; i < np; i++) apply_sgr(params[i]); }
      else if(*s != 'h' && *s != 'l') csi(*s, params, np);          /* ESC[?25l (hide cursor) and friends are ignored */
      continue;
    }
    if(*s == 27) continue;
    put_char((unsigned char)*s);
  }
}

/* ---------------------------------------------------------------- output capture (for pipes and redirection) */
static char *cap; static int caplen, capmax;
static void cap_add(const char *s, int n){
  if(caplen + n + 1 > capmax){ capmax = (caplen + n + 1) * 2 + 256; cap = realloc(cap, capmax); }
  memcpy(cap + caplen, s, n); caplen += n; cap[caplen] = 0;
}
static void outp(const char *fmt, ...){
  char tmp[2048]; va_list ap; va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
  if(n > (int)sizeof tmp - 1) n = (int)sizeof tmp - 1;
  cap_add(tmp, n);
}
#define RED "\x1b[31m"
#define GRN "\x1b[32m"
#define YEL "\x1b[33m"
#define BLU "\x1b[34m"
#define MAG "\x1b[35m"
#define CYN "\x1b[36m"
#define GRY "\x1b[90m"
#define RST "\x1b[0m"

/* ---------------------------------------------------------------- files */
static char *read_file(const char *name, int *len){
  HANDLE h = API(kernel32, CreateFileA)(name, 0x80000000u, 1, 0, 3, 0, 0);
  if(h == (HANDLE)~0ull) return 0;
  DWORD sz = API(kernel32, GetFileSize)(h, 0); char *b = malloc(sz + 1); DWORD got = 0;
  if(sz) API(kernel32, ReadFile)(h, b, sz, &got, 0);
  API(kernel32, CloseHandle)(h); b[got] = 0; *len = (int)got; return b;
}
static int write_file(const char *name, const char *data, int len){
  HANDLE h = API(kernel32, CreateFileA)(name, 0x40000000u, 0, 0, 2, 0, 0);
  if(h == (HANDLE)~0ull) return 0;
  DWORD w = 0; if(len) API(kernel32, WriteFile)(h, data, len, &w, 0);
  API(kernel32, CloseHandle)(h); return 1;
}
static void print_sanitized(const char *s, int n){          /* only printable characters reach the screen */
  for(int i = 0; i < n; i++){ unsigned char c = s[i]; if(c == '\n' || c == '\t' || (c >= 32 && c < 127)) outp("%c", c); else if(c == '\r'){} else outp("."); }
}

/* ---------------------------------------------------------------- commands: fn(argc, argv, piped input or NULL) */
typedef void (*cmdfn)(int, char**, const char*);
static int sort_names(const void *a, const void *b){ return strcmp(*(char**)a, *(char**)b); }
static void qsort_(char **v, int n){ for(int i = 1; i < n; i++){ char *k = v[i]; int j = i - 1; while(j >= 0 && sort_names(&v[j], &k) > 0){ v[j + 1] = v[j]; j--; } v[j + 1] = k; } }

static void c_help(int argc, char **argv, const char *in){
  (void)argc; (void)argv; (void)in;
  outp(CYN "Willows Terminal" RST " - commands:\n");
  outp("  " GRN "ls" RST " [folder]       list files          " GRN "cat" RST " <file>        show a file\n");
  outp("  " GRN "cd" RST " <folder>       change folder       " GRN "mkdir" RST "/" GRN "rmdir" RST " <f>    make / remove a folder\n");
  outp("  " GRN "open" RST " <path>       open like a double-click   " GRN "rm" RST " [-r] <path>  delete (folders with -r)\n");
  outp("  " GRN "cp" RST " <a> <b>        copy                " GRN "mv" RST " <a> <b>        rename\n");
  outp("  " GRN "rm" RST " <file>         delete              " GRN "touch" RST " <file>      create empty file\n");
  outp("  " GRN "echo" RST " text         print (use > or >>)  " GRN "grep" RST "/" GRN "head" RST "/" GRN "tail" RST "/" GRN "wc" RST "/" GRN "sort" RST "  filters, e.g. ls | grep txt\n");
  outp("  " GRN "ping" RST " <host>       ICMP echo           " GRN "curl" RST " [-I] <url>   fetch a page (http/https)\n");
  outp("  " GRN "wget" RST " <url> [file] download to a file  " GRN "nslookup" RST " <name>   resolve a name\n");
  outp("  " GRN "unzip" RST " <zip> [dir] extract a .zip file\n");
  outp("  " GRN "ipconfig" RST "          network address     " GRN "ps" RST ", " GRN "kill" RST " <pid>    processes\n");
  outp("  " GRN "run" RST " <app> [args]  start a program     " GRN "date" RST ", " GRN "uptime" RST ", " GRN "free" RST "   system info\n");
  outp("  " GRN "calc" RST " 2+3*(4-1)    calculator          " GRN "neofetch" RST "          about this system\n");
  outp("  " GRN "history" RST ", " GRN "clear" RST ", " GRN "exit" RST ", " GRN "reboot" RST ", " GRN "shutdown" RST "\n");
  outp(GRY "Tab completes, Up/Down browse history, PgUp/PgDn scroll, Ctrl+L clears, Ctrl+C cancels the line.\n" RST);
}
static void c_echo(int argc, char **argv, const char *in){ (void)in; for(int i = 1; i < argc; i++) outp("%s%s", i > 1 ? " " : "", argv[i]); outp("\n"); }
static void c_ls(int argc, char **argv, const char *in){
  (void)in; WIN32_FIND_DATAA fd; static char names[256][64]; static DWORD sizes[256], attrs[256]; char *idx[256]; int n = 0;
  const char *pat = "*"; for(int i = 1; i < argc; i++) if(argv[i][0] != '-') pat = argv[i];
  char buf[200]; if(API(kernel32, GetFileAttributesA)(pat) == 0x10){ snprintf(buf, sizeof buf, "%s\\*", pat); pat = buf; }      /* "ls dir" lists the folder */
  HANDLE h = API(kernel32, FindFirstFileA)(pat, &fd);
  if(h != (HANDLE)~0ull){ do { if(n < 256){ snprintf(names[n], 64, "%s", fd.name); sizes[n] = fd.sizeLow; attrs[n] = fd.attr; idx[n] = names[n]; n++; } } while(API(kernel32, FindNextFileA)(h, &fd)); API(kernel32, FindClose)(h); }
  if(!n){ outp(GRY "(empty)\n" RST); return; }
  qsort_(idx, n);
  for(int pass = 0; pass < 2; pass++) for(int i = 0; i < n; i++){                      /* folders first */
    int k = 0; for(; k < n; k++) if(idx[i] == names[k]) break;
    int dir = (attrs[k] & 0x10) != 0; if(dir != (pass == 0)) continue;
    int l = (int)strlen(idx[i]); int exe = l > 4 && !strcmp(idx[i] + l - 4, ".exe");
    if(dir) outp("%8s  " BLU "%s/" RST "\n", "<dir>", idx[i]); else outp("%8u  %s%s" RST "\n", sizes[k], exe ? GRN : "", idx[i]);
  }
}
static void c_cat(int argc, char **argv, const char *in){
  if(argc < 2){ if(in) outp("%s", in); else outp("usage: cat <file>\n"); return; }
  for(int i = 1; i < argc; i++){ int n; char *b = read_file(argv[i], &n); if(!b){ outp(RED "cat: %s: no such file\n" RST, argv[i]); continue; } print_sanitized(b, n); if(n && b[n - 1] != '\n') outp("\n"); free(b); }
}
static void rm_tree(const char *dir){
  char pat[300]; snprintf(pat, sizeof pat, "%s\\*", dir); WIN32_FIND_DATAA fd; static char kids[64][64]; static DWORD ka[64]; int n = 0;
  HANDLE h = API(kernel32, FindFirstFileA)(pat, &fd);
  if(h != (HANDLE)~0ull){ do { if(n < 64){ snprintf(kids[n], 64, "%s", fd.name); ka[n++] = fd.attr; } } while(API(kernel32, FindNextFileA)(h, &fd)); API(kernel32, FindClose)(h); }
  for(int i = 0; i < n; i++){ char p2[400]; snprintf(p2, sizeof p2, "%s\\%s", dir, kids[i]); if(ka[i] & 0x10) rm_tree(p2); else API(kernel32, DeleteFileA)(p2); }
  API(kernel32, RemoveDirectoryA)(dir);
}
static void c_rm(int argc, char **argv, const char *in){
  (void)in; int rec = 0; for(int i = 1; i < argc; i++) if(!strcmp(argv[i], "-r") || !strcmp(argv[i], "-rf")) rec = 1;
  for(int i = 1; i < argc; i++){ if(argv[i][0] == '-') continue;
    DWORD a = API(kernel32, GetFileAttributesA)(argv[i]);
    if(a == 0xFFFFFFFF) outp(RED "rm: %s: no such file\n" RST, argv[i]);
    else if(a & 0x10){ if(rec) rm_tree(argv[i]); else outp(RED "rm: %s is a folder (use rm -r)\n" RST, argv[i]); }
    else API(kernel32, DeleteFileA)(argv[i]);
  }
}
static void c_cp(int argc, char **argv, const char *in){ (void)in; if(argc < 3){ outp("usage: cp <from> <to>\n"); return; } if(!API(kernel32, CopyFileA)(argv[1], argv[2], 0)) outp(RED "cp: cannot copy %s\n" RST, argv[1]); }
static void c_mv(int argc, char **argv, const char *in){ (void)in; if(argc < 3){ outp("usage: mv <from> <to>\n"); return; } if(!API(kernel32, MoveFileA)(argv[1], argv[2])) outp(RED "mv: cannot rename %s (missing, or target exists)\n" RST, argv[1]); }
static void c_touch(int argc, char **argv, const char *in){ (void)in; for(int i = 1; i < argc; i++){ int n; char *b = read_file(argv[i], &n); if(!b) write_file(argv[i], "", 0); else free(b); } }
static void c_pwd(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; char b[200]; API(kernel32, GetCurrentDirectoryA)(sizeof b, b); outp("%s\n", b); }
static void c_cd(int argc, char **argv, const char *in){
  (void)in; const char *t = argc > 1 ? argv[1] : "/Documents"; if(!strcmp(t, "~")) t = "/Documents";
  if(!API(kernel32, SetCurrentDirectoryA)(t)) outp(RED "cd: %s: no such folder\n" RST, t);
}
static void c_mkdir(int argc, char **argv, const char *in){ (void)in; if(argc < 2){ outp("usage: mkdir <name>\n"); return; } for(int i = 1; i < argc; i++) if(!API(kernel32, CreateDirectoryA)(argv[i], 0)) outp(RED "mkdir: cannot create %s\n" RST, argv[i]); }
static void c_rmdir(int argc, char **argv, const char *in){ (void)in; if(argc < 2){ outp("usage: rmdir <folder>\n"); return; } if(!API(kernel32, RemoveDirectoryA)(argv[1])) outp(RED "rmdir: %s: not an empty folder\n" RST, argv[1]); }
static void c_open(int argc, char **argv, const char *in){ (void)in; if(argc < 2){ outp("usage: open <file or folder>\n"); return; } if((unsigned long long)API(shell32, ShellExecuteA)(0, "open", argv[1], 0, 0, 1) <= 32) outp(RED "open: %s: not found\n" RST, argv[1]); }
static void c_whoami(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; outp("user\n"); }
static void c_hostname(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; outp("willows\n"); }
static void c_uname(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; outp("Willows 0.4 x86_64 (Windows-compatible kernel)\n"); }
static void c_set(int argc, char **argv, const char *in){                  /* set NAME=VALUE (programs started afterwards see it); set NAME shows it */
  (void)in; if(argc < 2){ outp("usage: set NAME=VALUE  |  set NAME\n"); return; }
  for(int i = 1; i < argc; i++){
    char *eq = strchr(argv[i], '=');
    if(eq){ *eq = 0; if(!API(kernel32, SetEnvironmentVariableA)(argv[i], eq[1] ? eq + 1 : 0)) outp(RED "set: cannot set %s\n" RST, argv[i]); }
    else { char v[200]; if(API(kernel32, GetEnvironmentVariableA)(argv[i], v, sizeof v)) outp("%s=%s\n", argv[i], v); else outp("%s is not set\n", argv[i]); }
  }
}
static void c_date(int argc, char **argv, const char *in){
  (void)argc; (void)argv; (void)in; SYSTEMTIME t; API(kernel32, GetLocalTime)(&t);
  static const char *const dn[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}, *const mn[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  outp("%s %d %s %d  %02d:%02d:%02d\n", dn[t.dow % 7], t.d, mn[(t.mo + 11) % 12], t.y, t.h, t.mi, t.s);
}
static void c_uptime(int argc, char **argv, const char *in){
  (void)argc; (void)argv; (void)in; unsigned long long ms = API(kernel32, GetTickCount64)(), s = ms / 1000;
  outp("up %llu:%02llu:%02llu\n", s / 3600, (s / 60) % 60, s % 60);
}
static void c_free(int argc, char **argv, const char *in){
  (void)argc; (void)argv; (void)in; MEMORYSTATUSEX m; memset(&m, 0, sizeof m); m.len = sizeof m; API(kernel32, GlobalMemoryStatusEx)(&m);
  outp("total %llu MB   free %llu MB   used %u%%\n", m.total >> 20, m.avail >> 20, m.load);
}
static void c_ps(int argc, char **argv, const char *in){
  (void)argc; (void)argv; (void)in; PROCESSENTRY32A pe; memset(&pe, 0, sizeof pe); pe.size = sizeof pe;
  HANDLE s = API(kernel32, CreateToolhelp32Snapshot)(2, 0); outp(GRY "  PID  NAME\n" RST);
  for(int ok = API(kernel32, Process32First)(s, &pe); ok; ok = API(kernel32, Process32Next)(s, &pe)) outp("%5u  %s\n", pe.pid, pe.exe);
}
static void c_kill(int argc, char **argv, const char *in){
  (void)in; if(argc < 2){ outp("usage: kill <pid>\n"); return; }
  HANDLE h = API(kernel32, OpenProcess)(1, 0, (DWORD)atoi(argv[1]));
  if(!h || !API(kernel32, TerminateProcess)(h, 1)) outp(RED "kill: no such process\n" RST); else outp("terminated %s\n", argv[1]);
}
static void watch_job(unsigned pid); static int job_capture;
static void c_run(int argc, char **argv, const char *in){
  (void)in; if(argc < 2){ outp("usage: run <program> [arguments]\n"); return; }
  char cmd[300]; int n = 0; for(int i = 1; i < argc && n < 280; i++) n += snprintf(cmd + n, sizeof cmd - n, "%s%s", i > 1 ? " " : "", argv[i]);
  PROCESS_INFORMATION pi; if(API(kernel32, CreateProcessA)(0, cmd, 0, 0, 0, 0x40000000, 0, 0, 0, &pi)) watch_job(pi.pid); else outp(RED "run: %s: program not found\n" RST, argv[1]);
}
static void c_ping(int argc, char **argv, const char *in){
  (void)in; if(argc < 2){ outp("usage: ping <host> [-n count]\n"); return; }
  int count = 4; for(int i = 2; i + 1 < argc; i++) if(!strcmp(argv[i], "-n")) count = atoi(argv[i + 1]); if(count < 1) count = 1; if(count > 20) count = 20;
  API(ws2_32, WSAStartup)(0x0202, (char[408]){0});
  struct hostent_ *he = API(ws2_32, gethostbyname)(argv[1]); if(!he){ outp(RED "ping: cannot resolve %s\n" RST, argv[1]); return; }
  unsigned ip = *(unsigned*)he->h_addr_list[0]; const char *ips = API(ws2_32, inet_ntoa)(ip);
  HANDLE h = API(iphlpapi, IcmpCreateFile)(); int ok = 0; outp("Pinging %s [%s] with 32 bytes:\n", argv[1], ips);
  for(int i = 0; i < count; i++){
    unsigned char rep[64];
    if(API(iphlpapi, IcmpSendEcho)(h, ip, "abcdefghijklmnopqrstuvwabcdefghi", 32, 0, rep, sizeof rep, 1500)){ outp("Reply from %s: bytes=32 time=%ums\n", ips, *(unsigned*)(rep + 8)); ok++; }
    else outp(YEL "Request timed out.\n" RST);
    term_write(cap); cap_add("", 0); cap[0] = 0; caplen = 0;               /* show each reply as it arrives */
    API(kernel32, Sleep)(600);
  }
  outp("%d/%d replies\n", ok, count);
}
static void c_nslookup(int argc, char **argv, const char *in){
  (void)in; if(argc < 2){ outp("usage: nslookup <name>\n"); return; }
  API(ws2_32, WSAStartup)(0x0202, (char[408]){0});
  struct hostent_ *he = API(ws2_32, gethostbyname)(argv[1]); if(!he){ outp(RED "can't resolve %s\n" RST, argv[1]); return; }
  outp("Name:    %s\nAddress: " CYN "%s" RST "\n", argv[1], API(ws2_32, inet_ntoa)(*(unsigned*)he->h_addr_list[0]));
}
static void c_ipconfig(int argc, char **argv, const char *in){
  (void)argc; (void)argv; (void)in; API(ws2_32, WSAStartup)(0x0202, (char[408]){0});
  struct hostent_ *he = API(ws2_32, gethostbyname)("willows");
  if(he) outp("Ethernet adapter:\n  IPv4 Address . . . : " CYN "%s" RST "\n", API(ws2_32, inet_ntoa)(*(unsigned*)he->h_addr_list[0])); else outp("no network\n");
}
static HANDLE hnet;
static void fetch(const char *url, int headers_only, const char *save){
  if(!hnet) hnet = API(wininet, InternetOpenA)("WillowsTerminal/1.0", 0, 0, 0, 0);
  char full[700]; if(strstr(url, "://")) snprintf(full, sizeof full, "%s", url); else snprintf(full, sizeof full, "https://%s", url);
  HANDLE h = API(wininet, InternetOpenUrlA)(hnet, full, 0, 0, 0, 0);
  if(!h && !strstr(url, "://")){ snprintf(full, sizeof full, "http://%s", url); h = API(wininet, InternetOpenUrlA)(hnet, full, 0, 0, 0, 0); }
  if(!h){ outp(RED "failed to connect to %s (or its certificate was rejected)\n" RST, url); return; }
  DWORD status = 0, sl = 4; API(wininet, HttpQueryInfoA)(h, 19 | 0x20000000, &status, &sl, 0);
  char ct[100] = ""; DWORD cl = sizeof ct; API(wininet, HttpQueryInfoA)(h, 1, ct, &cl, 0);
  if(headers_only){ outp("HTTP %u\nContent-Type: %s\n", status, ct); API(wininet, InternetCloseHandle)(h); return; }
  int cap2 = 1 << 16, n = 0; char *buf = malloc(cap2); DWORD got;
  while(API(wininet, InternetReadFile)(h, buf + n, cap2 - n - 1, &got) && got){ n += got; if(n + 8192 > cap2){ if(cap2 >= (3 << 20)) break; cap2 *= 2; buf = realloc(buf, cap2); } }
  buf[n] = 0; API(wininet, InternetCloseHandle)(h);
  if(save){ if(write_file(save, buf, n)) outp(GRN "saved %d bytes to %s" RST " (HTTP %u)\n", n, save, status); else outp(RED "cannot write %s\n" RST, save); }
  else { outp(GRY "HTTP %u, %d bytes, %s\n" RST, status, n, ct); print_sanitized(buf, n > 20000 ? 20000 : n); if(n > 20000) outp(GRY "\n... (%d more bytes; use wget to save the page)\n" RST, n - 20000); outp("\n"); }
  free(buf);
}
static void c_curl(int argc, char **argv, const char *in){
  (void)in; int hdr = 0; const char *url = 0, *save = 0;
  for(int i = 1; i < argc; i++){ if(!strcmp(argv[i], "-I")) hdr = 1; else if(!strcmp(argv[i], "-o") && i + 1 < argc) save = argv[++i]; else url = argv[i]; }
  if(!url){ outp("usage: curl [-I] [-o file] <url>\n"); return; } fetch(url, hdr, save);
}
static void repaint(void);
#include "unzip.h"
static void fs_batch_call(int on){                      /* Willows extension (absent under Wine): one disk save at the end instead of one per extracted file */
  static void (WINAPI *f)(int); static int tried;
  if(!tried){ tried = 1; f = API(kernel32, GetProcAddress)(API(kernel32, GetModuleHandleA)("kernel32.dll"), "WillowsFsBatch"); }
  if(f) f(on);
}
static void c_unzip(int argc, char **argv, const char *in){
  (void)in; if(argc < 2){ outp("usage: unzip <file.zip> [folder]\n"); return; }
  char dest[120]; if(argc > 2) snprintf(dest, sizeof dest, "%s", argv[2]);
  else { const char *s = strrchr(argv[1], '/'); const char *b = strrchr(argv[1], '\\'); if(b > s) s = b; s = s ? s + 1 : argv[1]; snprintf(dest, sizeof dest, "%s", s); int l = (int)strlen(dest); if(l > 4 && !stricmp_(dest + l - 4, ".zip")) dest[l - 4] = 0; }
  outp("extracting %s into %s ...\n", argv[1], dest); repaint();
  int n = unzip_file(argv[1], dest, outp, fs_batch_call);
  if(n >= 0) outp(GRN "extracted %d files into %s" RST "\n", n, dest);
}
static void c_wget(int argc, char **argv, const char *in){
  (void)in; if(argc < 2){ outp("usage: wget <url> [file]\n"); return; }
  char name[64]; if(argc > 2) snprintf(name, sizeof name, "%s", argv[2]);
  else { const char *s = strrchr(argv[1], '/'); s = (s && s[1]) ? s + 1 : "index.html"; int k = 0; while(s[k] && s[k] != '?' && k < 60){ name[k] = s[k]; k++; } name[k] = 0; if(!k || strstr(name, "://")) snprintf(name, sizeof name, "index.html"); }
  fetch(argv[1], 0, name);
}
/* integer calculator: + - * / % and parentheses */
static const char *cp; static int calc_err;
static long long expr(void);
static long long atom(void){
  while(*cp == ' ') cp++;
  if(*cp == '('){ cp++; long long v = expr(); while(*cp == ' ') cp++; if(*cp == ')') cp++; else calc_err = 1; return v; }
  if(*cp == '-'){ cp++; return -atom(); }
  if(!isdigit((unsigned char)*cp)){ calc_err = 1; return 0; }
  long long v = 0; while(isdigit((unsigned char)*cp)) v = v * 10 + (*cp++ - '0'); return v;
}
static long long term(void){ long long v = atom(); for(;;){ while(*cp == ' ') cp++; if(*cp == '*'){ cp++; v *= atom(); } else if(*cp == '/' || *cp == '%'){ char o = *cp++; long long d = atom(); if(!d){ calc_err = 2; return 0; } v = o == '/' ? v / d : v % d; } else return v; } }
static long long expr(void){ long long v = term(); for(;;){ while(*cp == ' ') cp++; if(*cp == '+'){ cp++; v += term(); } else if(*cp == '-'){ cp++; v -= term(); } else return v; } }
static void c_calc(int argc, char **argv, const char *in){
  (void)in; char e[300] = ""; for(int i = 1; i < argc; i++){ int l = (int)strlen(e); snprintf(e + l, sizeof e - l, "%s ", argv[i]); }
  if(!e[0]){ outp("usage: calc 2+3*(4-1)\n"); return; }
  cp = e; calc_err = 0; long long v = expr(); while(*cp == ' ') cp++;
  if(calc_err == 2) outp(RED "division by zero\n" RST); else if(calc_err || *cp) outp(RED "syntax error\n" RST); else outp("%lld\n", v);
}
static void c_neofetch(int argc, char **argv, const char *in){
  (void)argc; (void)argv; (void)in; unsigned long long s = API(kernel32, GetTickCount64)() / 1000; MEMORYSTATUSEX m; memset(&m, 0, sizeof m); m.len = sizeof m; API(kernel32, GlobalMemoryStatusEx)(&m);
  outp(GRN "        .  *  .        " RST CYN "user@willows" RST "\n");
  outp(GRN "     .:#@@@@@@@#:.     " RST "----------------\n");
  outp(GRN "   .:#@@@@@@@@@@@#:.   " RST CYN "OS" RST "      Willows 0.4 x86_64\n");
  outp(GRN "  :#@@@@@@@@@@@@@@#:   " RST CYN "Kernel" RST "  Windows-compatible, custom\n");
  outp(GRN "   ':#@@@@@@@@@#:'     " RST CYN "Uptime" RST "  %llum %llus\n", s / 60, s % 60);
  outp(GRN "      '  ||  '         " RST CYN "Memory" RST "  %llu / %llu MB\n", (m.total - m.avail) >> 20, m.total >> 20);
  outp(GRN "         ||            " RST CYN "Shell" RST "   willowsh\n");
  outp(GRN "         ||            " RST "\x1b[31m### " "\x1b[32m### " "\x1b[33m### " "\x1b[34m### " "\x1b[35m### " "\x1b[36m###" RST "\n");
}
static void c_reboot(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; API(user32, ExitWindowsEx)(2, 0); }
static void c_shutdown(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; API(user32, ExitWindowsEx)(1, 0); }
static void c_exit(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; API(kernel32, ExitProcess)(0); }
static void c_sleep(int argc, char **argv, const char *in){                /* sleep SECONDS (scripts: wait for the network, ...) */
  (void)in; int s = argc > 1 ? atoi(argv[1]) : 1; if(s < 0) s = 0; if(s > 3600) s = 3600; API(kernel32, Sleep)((unsigned)s * 1000);
}
static void c_clear(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; head = 0; nl = 1; ocol = 0; view_off = 0; memset(LINE(0), 0, sizeof LINE(0)); }
static char history[64][200]; static int nhist;
static void c_history(int argc, char **argv, const char *in){ (void)argc; (void)argv; (void)in; for(int i = 0; i < nhist; i++) outp("%4d  %s\n", i + 1, history[i]); }

/* filters */
static void for_each_line(const char *in, void (*f)(const char *, int, void *), void *ctx){
  const char *p = in; while(p && *p){ const char *e = strchr(p, '\n'); int n = e ? (int)(e - p) : (int)strlen(p); f(p, n, ctx); p += n; if(*p == '\n') p++; }
}
static void grep_line(const char *l, int n, void *ctx){ char buf[1024]; if(n > 1000) n = 1000; memcpy(buf, l, n); buf[n] = 0; if(strstr(buf, (const char*)ctx)) outp("%s\n", buf); }
static void c_grep(int argc, char **argv, const char *in){ if(argc < 2){ outp("usage: ... | grep <text>\n"); return; } if(!in){ outp("grep: reads from a pipe (try: ls | grep txt)\n"); return; } for_each_line(in, grep_line, argv[1]); }
static void c_wc(int argc, char **argv, const char *in){ (void)argc; (void)argv; int lines = 0, words = 0, chars = 0, inw = 0; for(const char *p = in ? in : ""; *p; p++){ chars++; if(*p == '\n') lines++; if(isspace((unsigned char)*p)) inw = 0; else if(!inw){ inw = 1; words++; } } outp("%d lines  %d words  %d chars\n", lines, words, chars); }
static void c_head(int argc, char **argv, const char *in){ int n = argc > 1 ? atoi(argv[1]) : 10; if(n < 1) n = 10; const char *p = in ? in : ""; while(*p && n--){ const char *e = strchr(p, '\n'); int l = e ? (int)(e - p) : (int)strlen(p); outp("%.*s\n", l, p); p += l; if(*p == '\n') p++; } }
static void c_tail(int argc, char **argv, const char *in){
  int n = argc > 1 ? atoi(argv[1]) : 10; if(n < 1) n = 10; const char *p = in ? in : ""; int total = 0; for(const char *q = p; *q; q++) if(*q == '\n') total++;
  int skip = total > n ? total - n : 0; while(*p && skip){ if(*p == '\n') skip--; p++; } outp("%s", p);
}
static void c_sort(int argc, char **argv, const char *in){
  (void)argc; (void)argv; static char *ls[2000]; int n = 0; char *copy = in ? strdup_(in) : 0; if(!copy) return;
  for(char *p = copy; *p && n < 2000; ){ ls[n++] = p; char *e = strchr(p, '\n'); if(!e) break; *e = 0; p = e + 1; }
  qsort_(ls, n); for(int i = 0; i < n; i++) outp("%s\n", ls[i]); free(copy);
}

static const struct { const char *name; cmdfn fn; } cmds[] = {
  {"help",c_help},{"?",c_help},{"echo",c_echo},{"ls",c_ls},{"dir",c_ls},{"cat",c_cat},{"type",c_cat},{"rm",c_rm},{"del",c_rm},{"cp",c_cp},{"copy",c_cp},
  {"mv",c_mv},{"ren",c_mv},{"touch",c_touch},{"pwd",c_pwd},{"cd",c_cd},{"mkdir",c_mkdir},{"md",c_mkdir},{"rmdir",c_rmdir},{"open",c_open},{"whoami",c_whoami},{"hostname",c_hostname},{"uname",c_uname},{"ver",c_uname},{"date",c_date},
  {"set",c_set},{"export",c_set},{"uptime",c_uptime},{"free",c_free},{"mem",c_free},{"ps",c_ps},{"kill",c_kill},{"run",c_run},{"start",c_run},{"ping",c_ping},{"nslookup",c_nslookup},{"host",c_nslookup},
  {"ipconfig",c_ipconfig},{"ifconfig",c_ipconfig},{"curl",c_curl},{"wget",c_wget},{"unzip",c_unzip},{"calc",c_calc},{"neofetch",c_neofetch},{"about",c_neofetch},{"reboot",c_reboot},
  {"shutdown",c_shutdown},{"poweroff",c_shutdown},{"exit",c_exit},{"sleep",c_sleep},{"clear",c_clear},{"cls",c_clear},{"history",c_history},
  {"grep",c_grep},{"wc",c_wc},{"head",c_head},{"tail",c_tail},{"sort",c_sort},
};
#define NCMDS ((int)(sizeof cmds / sizeof *cmds))

/* ---------------------------------------------------------------- command line parsing and execution */
static int tokenize(char *s, char **argv, int max){
  int n = 0;
  while(*s && n < max - 1){
    while(*s == ' ') s++; if(!*s) break;
    if(*s == '"'){ argv[n++] = ++s; while(*s && *s != '"') s++; if(*s) *s++ = 0; }
    else { argv[n++] = s; while(*s && *s != ' ') s++; if(*s) *s++ = 0; }
  }
  argv[n] = 0; return n;
}
/* A program started from here prints into the terminal: its console output is captured and shown as it arrives. If it stays quiet for
   a moment (a GUI program, a server) the terminal gives you the prompt back and further output goes to the Console window. */
static void repaint(void);
/* job_capture: the running line is redirected or piped: program output stays in cap */
static void watch_job(unsigned pid){
  unsigned long long last = API(kernel32, GetTickCount64)(); char tmp[640]; int held = 0;     /* held: bytes of an escape sequence cut off at the end of the last chunk */
  for(;;){
    int n = API(kernel32, WillowsReadOutput)(pid, tmp + held, 512);
    if(n > 0){
      n += held; held = 0;
      int esc = -1; for(int k = n - 1; k >= 0 && k >= n - 32; k--) if(tmp[k] == 27){ esc = k; break; }
      if(esc >= 0){ int done = 0; for(int k = esc + 2; k < n; k++) if(tmp[k] >= '@' && tmp[k] <= '~'){ done = 1; break; } if(esc == n - 1 || (tmp[esc + 1] == '[' && !done)) held = n - esc; }
      int out = n - held; cap_add(tmp, out); if(!job_capture){ term_write(cap); cap_add("", 0); cap[0] = 0; caplen = 0; repaint(); }   /* redirected or piped: keep it */
      if(held) memmove(tmp, tmp + out, held);
      last = API(kernel32, GetTickCount64)(); }
    else {
      int st = API(kernel32, WillowsProcessRunning)(pid);
      if(!(st & 1)) break;
      if(!(st & 2) && API(kernel32, GetTickCount64)() - last > 1500){ API(kernel32, WillowsReadOutput)(pid, 0, 0); break; }   /* a program that reads the keyboard keeps the terminal */
    }
    MSG msg;
    while(API(user32, PeekMessageA)(&msg, 0, 0, 0, 1)){
      if(msg.message == 0x12){ ztop = -1; ovr = -1; return; }
      if(msg.message == WM_KEYDOWN){ API(kernel32, WillowsSendKey)((int)msg.wParam, 0); continue; }      /* keys go to the running program, not to the prompt */
      if(msg.message == WM_CHAR){
        if((int)msg.wParam == 3){ API(kernel32, WillowsReadOutput)(pid, 0, 0); term_write("^C\n"); ztop = -1; ovr = -1; return; }   /* Ctrl+C: give the prompt back */
        API(kernel32, WillowsSendKey)(0, (int)msg.wParam); continue;
      }
      API(user32, TranslateMessage)(&msg); API(user32, DispatchMessageA)(&msg);
    }
    API(kernel32, Sleep)(8);
  }
  ztop = -1; ovr = -1;
}
/* Not a built-in command: treat it as a program. Looks in the typed path, then Downloads, Documents and Desktop, with and without ".exe". */
static int launch_bad_format;                                  /* set when a matching file exists but is not a runnable 64-bit Windows program */
static int launch_path(const char *path, int argc, char **argv){
  char cmd[360]; int n = snprintf(cmd, sizeof cmd, "%s", path);
  for(int i = 1; i < argc && n < 340; i++) n += snprintf(cmd + n, sizeof cmd - n, " %s", argv[i]);
  PROCESS_INFORMATION pi;
  if(API(kernel32, CreateProcessA)(0, cmd, 0, 0, 0, 0x40000000, 0, 0, 0, &pi)){ watch_job(pi.pid); return 1; }
  if(API(kernel32, GetLastError)() == 193) launch_bad_format = 1;                    /* the file was found but cannot be run */
  return 0;
}
static int try_launch(int argc, char **argv){
  const char *name = argv[0]; if((name[0] == '.' && (name[1] == '/' || name[1] == '\\'))) name += 2;
  char p[200]; int has_ext = 0; { int l = (int)strlen(name); has_ext = l > 4 && !stricmp_(name + l - 4, ".exe"); }
  int has_dir = 0; for(const char *c = name; *c; c++) if(*c == '/' || *c == '\\') has_dir = 1;
  static const char *const dirs[] = {"", "Downloads\\", "Documents\\", "Desktop\\"};
  for(int d = 0; d < 4; d++){
    if(d > 0 && has_dir) break;
    snprintf(p, sizeof p, "%s%s", dirs[d], name); if(launch_path(p, argc, argv)) return 1;
    if(!has_ext){ snprintf(p, sizeof p, "%s%s.exe", dirs[d], name); if(launch_path(p, argc, argv)) return 1; }
  }
  return 0;
}
static void run_stage(char *stage, const char *in){
  char *argv[32]; int argc = tokenize(stage, argv, 32); if(!argc) return;
  for(int i = 0; i < NCMDS; i++) if(!stricmp_(argv[0], cmds[i].name)){ cmds[i].fn(argc, argv, in); return; }
  launch_bad_format = 0;
  if(try_launch(argc, argv)) return;
  if(launch_bad_format) outp(RED "%s: cannot run this file" RST GRY " (Willows runs 64-bit Windows programs; this one is 32-bit or damaged)\n" RST, argv[0]);
  else outp(RED "%s: command not found" RST GRY " (try 'help')\n" RST, argv[0]);
}
static void run_line(char *line){
  /* redirection: last '>' or '>>' outside quotes */
  char *redir = 0; int append = 0; { int q = 0; for(char *p = line; *p; p++){ if(*p == '"') q = !q; if(!q && *p == '>'){ redir = p; append = p[1] == '>'; } } }
  char *target = 0; if(redir){ *redir = 0; target = redir + 1 + append; while(*target == ' ') target++; char *e = target + strlen(target); while(e > target && e[-1] == ' ') *--e = 0; }
  char *stages[8]; int ns = 0; { int q = 0; stages[ns++] = line; for(char *p = line; *p; p++){ if(*p == '"') q = !q; if(!q && *p == '|' && ns < 8){ *p = 0; stages[ns++] = p + 1; } } }
  char *in = 0;
  for(int i = 0; i < ns; i++){
    cap = 0; caplen = capmax = 0; job_capture = (target && *target) || i + 1 < ns; run_stage(stages[i], in); job_capture = 0; free(in); in = cap ? cap : strdup_(""); cap = 0;
  }
  if(target && *target){
    char *old = 0; int on = 0; if(append) old = read_file(target, &on);
    int tl = (int)strlen(in); char *all = malloc(on + tl + 1); if(old){ memcpy(all, old, on); free(old); } memcpy(all + on, in, tl); all[on + tl] = 0;
    if(!write_file(target, all, on + tl)) term_write(RED "cannot write the file\n" RST); free(all);
  } else term_write(in);
  free(in);
}

/* ---------------------------------------------------------------- line editor */
static char inbuf[200]; static int inlen, incur, prompting, prompt_len, hpos = -1, blink = 1, completing_shown;
static void show_prompt(void){
  if(ocol) put_char('\n');
  { char cwd[200], shown[200]; API(kernel32, GetCurrentDirectoryA)(sizeof cwd, cwd); const char *p = cwd + 3; char u[200]; int k = 0; for(; p[k] && k < 190; k++) u[k] = p[k] == '\\' ? '/' : p[k]; u[k] = 0;
    if(!strcmp(u, "Documents")) snprintf(shown, sizeof shown, "~"); else if(!strncmp(u, "Documents/", 10)) snprintf(shown, sizeof shown, "~/%s", u + 10); else snprintf(shown, sizeof shown, "/%s", u);
    ocolor = 2; term_write("user@willows"); ocolor = 0; term_write(":"); ocolor = 4; term_write(shown); ocolor = 0; term_write("$ "); }
  prompt_len = ocol; prompting = 1; inlen = incur = 0; inbuf[0] = 0; hpos = -1; view_off = 0;
}
static void set_input(const char *s){ snprintf(inbuf, sizeof inbuf, "%s", s); inlen = incur = (int)strlen(inbuf); }
static void insert_char(int c){ if(inlen >= (int)sizeof inbuf - 2 || prompt_len + inlen >= cols - 2) return; memmove(inbuf + incur + 1, inbuf + incur, inlen - incur + 1); inbuf[incur++] = (char)c; inlen++; }
static void submit(void){
  prompting = 0; for(int i = 0; i < inlen; i++) put_char((unsigned char)inbuf[i]); put_char('\n');
  char line[200]; snprintf(line, sizeof line, "%s", inbuf);
  if(inlen){ if(nhist == 0 || strcmp(history[nhist - 1], line)){ if(nhist == 64){ memmove(history[0], history[1], 63 * 200); nhist--; } snprintf(history[nhist++], 200, "%s", line); } }
  { char *p = line; while(*p == ' ') p++; if(*p) run_line(p); }
  show_prompt();
}
static void complete(void){
  int start = incur; while(start > 0 && inbuf[start - 1] != ' ') start--;
  char prefix[100]; int pl = incur - start; if(pl > 99) pl = 99; memcpy(prefix, inbuf + start, pl); prefix[pl] = 0;
  int first = 1; for(int i = 0; i < start; i++) if(inbuf[i] != ' ') first = 0;
  static char matches[300][64]; int nm = 0;
  if(first){ for(int i = 0; i < NCMDS && nm < 300; i++) if(!strncasecmp_(cmds[i].name, prefix, pl) && cmds[i].name[0] != '?') snprintf(matches[nm++], 64, "%s", cmds[i].name); }
  else { WIN32_FIND_DATAA fd; HANDLE h = API(kernel32, FindFirstFileA)("*", &fd);
    if(h != (HANDLE)~0ull){ do { if(!strncasecmp_(fd.name, prefix, pl) && nm < 300) snprintf(matches[nm++], 64, "%s", fd.name); } while(API(kernel32, FindNextFileA)(h, &fd)); API(kernel32, FindClose)(h); } }
  if(!nm) return;
  int common = (int)strlen(matches[0]); for(int i = 1; i < nm; i++){ int k = 0; while(k < common && tolower((unsigned char)matches[0][k]) == tolower((unsigned char)matches[i][k])) k++; common = k; }
  if(common > pl){ char add[64]; memcpy(add, matches[0] + pl, common - pl); add[common - pl] = 0; for(char *a = add; *a; a++) insert_char(*a); if(nm == 1) insert_char(' '); }
  else if(nm > 1){                                                        /* show the possibilities, then the prompt again */
    prompting = 0; for(int i = 0; i < inlen; i++) put_char((unsigned char)inbuf[i]); put_char('\n'); ocolor = 8;
    for(int i = 0; i < nm; i++){ term_write(matches[i]); term_write("  "); } ocolor = 0; put_char('\n');
    char keep[200]; snprintf(keep, sizeof keep, "%s", inbuf); int kc = incur; show_prompt(); set_input(keep); incur = kc;
  }
}

/* ---------------------------------------------------------------- painting */
static void fill(HANDLE dc, int x, int y, int w, int h, unsigned c){ if(w <= 0 || h <= 0) return; RECT r = {x, y, x + w, y + h}; HANDLE b = API(gdi32, CreateSolidBrush)(c); API(user32, FillRect)(dc, &r, b); API(gdi32, DeleteObject)(b); }
#define PADX 10
#define PADY 8
static void draw_cells(HANDLE dc, int row, const struct cell *c, int n, int col0){
  for(int k = 0; k < n; k++) if(c[k].bg){ unsigned bgc = c[k].bg == 9 ? 0x2E2A26 : palette[c[k].bg % 9]; fill(dc, PADX + (col0 + k) * cw, PADY + row * chh, cw, chh, bgc); }
  int i = 0; while(i < n){
    while(i < n && (!c[i].ch || c[i].ch == ' ')) i++;
    if(i >= n) break;
    int j = i; char run[16]; int rl = 0; unsigned char col = c[i].col;
    while(j < n && rl < 8 && c[j].ch && c[j].ch != ' ' && c[j].col == col){ run[rl++] = (char)c[j].ch; j++; }
    API(gdi32, SetTextColor)(dc, palette[col % 9]); API(gdi32, TextOutA)(dc, PADX + (col0 + i) * cw, PADY + row * chh, run, rl); i = j;
  }
}
static void paint(HANDLE dc){
  RECT cr; API(user32, GetClientRect)(hwnd, &cr);
  fill(dc, 0, 0, cr.right, cr.bottom, 0x1A110C);                              /* BGR of #0C111A */
  API(gdi32, SelectObject)(dc, hfont); API(gdi32, SetBkMode)(dc, 1);
  int bottom = nl - 1, anchored_top = nl < rows;
  for(int r = 0; r < rows; r++){
    int li = anchored_top ? r : bottom - (rows - 1 - r) - view_off; if(li < 0 || li >= nl) continue;
    int used = MAXC; while(used > 0 && !LINE(li)[used - 1].ch) used--; if(used > cols) used = cols;
    draw_cells(dc, r, LINE(li), used, 0);
  }
  if(prompting && view_off == 0){                                             /* the line being edited + cursor */
    int row = anchored_top ? nl - 1 : rows - 1; struct cell tmp[MAXC]; int n = inlen; if(n > MAXC) n = MAXC;
    for(int i = 0; i < n; i++){ tmp[i].ch = (unsigned char)inbuf[i]; tmp[i].col = 7; tmp[i].bg = 0; }   /* bg too: left unset it was stack garbage (random coloured blocks behind typed letters) */
    draw_cells(dc, row, tmp, n, prompt_len);
    if(blink){ int cx = PADX + (prompt_len + incur) * cw; fill(dc, cx, PADY + row * chh + 1, cw, chh - 2, 0xF6823B);
      if(incur < inlen){ char ch = inbuf[incur]; API(gdi32, SetTextColor)(dc, 0x1A110C); API(gdi32, TextOutA)(dc, cx, PADY + row * chh, &ch, 1); } }
  }
  if(view_off > 0){ char t[40]; snprintf(t, sizeof t, "[scrolled %d lines - End to return]", view_off); API(gdi32, SetTextColor)(dc, 0x80726B); API(gdi32, TextOutA)(dc, PADX, PADY + 0 * chh, t, (int)strlen(t)); }
}
static void relayout(int w, int h){
  cols = (w - 2 * PADX) / cw; rows = (h - 2 * PADY) / chh; if(cols < 20) cols = 20; if(cols > MAXC) cols = MAXC; if(rows < 4) rows = 4;
}
static void repaint(void){ API(user32, InvalidateRect)(hwnd, 0, 0); }
static void scroll_by(int d){ view_off += d; int max = nl - rows; if(max < 0) max = 0; if(view_off > max) view_off = max; if(view_off < 0) view_off = 0; repaint(); }

static LRESULT WINAPI wndproc(HANDLE h, UINT m, WPARAM wp, LPARAM lp){
  switch(m){
    case WM_PAINT: { PAINTSTRUCT ps; HANDLE dc = API(user32, BeginPaint)(h, &ps); if(dc) paint(dc); API(user32, EndPaint)(h, &ps); return 0; }
    case WM_SIZE: { int w = (int)(lp & 0xFFFF), hh = (int)((lp >> 16) & 0xFFFF); if(w > 100 && hh > 60){ relayout(w, hh); repaint(); } return 0; }
    case WM_MOUSEWHEEL: { int d = (short)((wp >> 16) & 0xFFFF); scroll_by(d / 120 * 3); return 0; }
    case WM_CHAR: {
      int c = (int)wp; view_off = 0;
      if(!prompting) return 0;
      if(c == 13) submit();
      else if(c == 8){ if(incur){ memmove(inbuf + incur - 1, inbuf + incur, inlen - incur + 1); incur--; inlen--; } }
      else if(c == 9) complete();
      else if(c == 3){ prompting = 0; for(int i = 0; i < inlen; i++) put_char((unsigned char)inbuf[i]); term_write("^C\n"); show_prompt(); }
      else if(c == 12){ c_clear(0, 0, 0); show_prompt(); }
      else if(c == 1) incur = 0; else if(c == 5) incur = inlen;
      else if(c == 21){ memmove(inbuf, inbuf + incur, inlen - incur + 1); inlen -= incur; incur = 0; }
      else if(c == 11){ inbuf[incur] = 0; inlen = incur; }
      else if(c == 23){ int s = incur; while(s > 0 && inbuf[s - 1] == ' ') s--; while(s > 0 && inbuf[s - 1] != ' ') s--; memmove(inbuf + s, inbuf + incur, inlen - incur + 1); inlen -= incur - s; incur = s; }
      else if(c >= 32 && c < 127) insert_char(c);
      blink = 1; repaint(); return 0;
    }
    case WM_KEYDOWN: {
      int vk = (int)wp; if(!prompting) return 0;
      if(vk == 0x25){ if(incur) incur--; } else if(vk == 0x27){ if(incur < inlen) incur++; }
      else if(vk == 0x24) incur = 0; else if(vk == 0x23){ incur = inlen; view_off = 0; }
      else if(vk == 0x2E){ if(incur < inlen){ memmove(inbuf + incur, inbuf + incur + 1, inlen - incur); inlen--; } }
      else if(vk == 0x26){ if(nhist){ if(hpos < 0) hpos = nhist; if(hpos > 0) hpos--; set_input(history[hpos]); } }
      else if(vk == 0x28){ if(hpos >= 0){ hpos++; if(hpos >= nhist){ hpos = -1; set_input(""); } else set_input(history[hpos]); } }
      else if(vk == 0x21){ scroll_by(rows - 2); return 0; } else if(vk == 0x22){ scroll_by(-(rows - 2)); return 0; }
      else return 0;
      blink = 1; repaint(); return 0;
    }
    case WM_DESTROY: API(user32, PostQuitMessage)(0); return 0;
  }
  return API(user32, DefWindowProcA)(h, m, wp, lp);
}

int main(int argc, char **argv){
  HANDLE hi = API(kernel32, GetModuleHandleA)(0);
  WNDCLASSA wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = "WTERMINAL"; wc.hCursor = API(user32, LoadCursorA)(0, (const char*)32512ull);
  API(user32, RegisterClassA)(&wc);
  hwnd = API(user32, CreateWindowExA)(0, "WTERMINAL", "Terminal", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 860, 520, 0, 0, hi, 0);
  if(!hwnd) return 1;
  hfont = API(gdi32, CreateFontA)(-15, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "Courier New");
  { HANDLE dc = API(user32, BeginPaint)(hwnd, &(PAINTSTRUCT){0}); API(gdi32, SelectObject)(dc, hfont); SIZE sz; API(gdi32, GetTextExtentPoint32A)(dc, "M", 1, &sz); TEXTMETRICA tm; API(gdi32, GetTextMetricsA)(dc, &tm); cw = sz.cx > 0 ? sz.cx : 9; chh = tm.tmHeight > 0 ? tm.tmHeight : 20; }
  { RECT r; API(user32, GetClientRect)(hwnd, &r); relayout(r.right, r.bottom); }
  int runc = argc > 2 && !strcmp(argv[1], "-c");                  /* terminal -c <command line>: run one command at start (System\\autorun.cfg uses it) */
  API(kernel32, SetCurrentDirectoryA)("/Documents"); if(argc > 1 && !runc) API(kernel32, SetCurrentDirectoryA)(argv[1]);
  term_write(CYN "Willows Terminal" RST "  -  type " GRN "help" RST " for commands, " GRN "neofetch" RST " for a look at the system.\n\n");
  show_prompt();
  if(runc){ char l[200]; int n = 0; l[0] = 0; for(int i = 2; i < argc && n < 190; i++) n += snprintf(l + n, sizeof l - n, i > 2 ? " %s" : "%s", argv[i]);
    if(l[0] == '@'){                                               /* -c @file: run every line of the file */
      int len; char *all = read_file(l + 1, &len);
      for(char *p = all; p && *p; ){ char *e = p; while(*e && *e != '\n') e++; char line[200]; int k = (int)(e - p); if(k > 199) k = 199; memcpy(line, p, k); line[k] = 0;
        while(k && line[k - 1] == '\r') line[--k] = 0; if(k){ set_input(line); submit(); } p = *e ? e + 1 : e; }
      free(all);
    } else { set_input(l); submit(); } }
  MSG msg; unsigned long long last_blink = API(kernel32, GetTickCount64)();
  for(;;){
    while(API(user32, PeekMessageA)(&msg, 0, 0, 0, 1)){ if(msg.message == 0x12) return 0; API(user32, TranslateMessage)(&msg); API(user32, DispatchMessageA)(&msg); }
    unsigned long long now = API(kernel32, GetTickCount64)();
    if(now - last_blink >= 530){ last_blink = now; blink = !blink; if(prompting) repaint(); }
    API(kernel32, Sleep)(15);
  }
}
