/* jstest: run JavaScript with QuickJS.   jstest "1+2"    or    jstest file.js */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"
#define WINAPI __attribute__((ms_abi))
extern void *(WINAPI *__imp_kernel32__CreateFileA)(const char *, unsigned, unsigned, void *, unsigned, unsigned, void *);
extern unsigned (WINAPI *__imp_kernel32__GetFileSize)(void *, unsigned *);
extern int (WINAPI *__imp_kernel32__ReadFile)(void *, void *, unsigned, unsigned *, void *);
extern int (WINAPI *__imp_kernel32__CloseHandle)(void *);

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv){
  for(int i = 0; i < argc; i++){ const char *s = JS_ToCString(ctx, argv[i]); if(i) putchar(' '); fputs(s ? s : "?", stdout); JS_FreeCString(ctx, s); }
  putchar('\n'); return JS_UNDEFINED;
}
static char *read_file(const char *path){
  void *h = __imp_kernel32__CreateFileA(path, 0x80000000u, 1, 0, 3, 0, 0); if(h == (void *)~0ull) return 0;
  unsigned sz = __imp_kernel32__GetFileSize(h, 0), got = 0; char *b = malloc(sz + 1); __imp_kernel32__ReadFile(h, b, sz, &got, 0); __imp_kernel32__CloseHandle(h); b[got] = 0; return b;
}
int main(int argc, char **argv){
  if(argc < 2){ printf("QuickJS %s\nusage: jstest \"expression\"   or   jstest file.js\n", JS_GetVersion()); return 0; }
  JSRuntime *rt = JS_NewRuntime(); JS_SetMaxStackSize(rt, 400 * 1024); JSContext *ctx = JS_NewContext(rt);
  JSValue g = JS_GetGlobalObject(ctx); JSValue con = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, con, "log", JS_NewCFunction(ctx, js_print, "log", 1)); JS_SetPropertyStr(ctx, g, "console", con);
  JS_SetPropertyStr(ctx, g, "print", JS_NewCFunction(ctx, js_print, "print", 1)); JS_FreeValue(ctx, g);
  char *code = read_file(argv[1]); const char *name = argv[1]; if(!code){ code = strdup(argv[1]); name = "<cmdline>"; }
  JSValue r = JS_Eval(ctx, code, strlen(code), name, JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(r)){ JSValue ex = JS_GetException(ctx); const char *s = JS_ToCString(ctx, ex); printf("Uncaught: %s\n", s ? s : "?");
    JSValue st = JS_GetPropertyStr(ctx, ex, "stack"); if(!JS_IsUndefined(st)){ const char *ss = JS_ToCString(ctx, st); if(ss) printf("%s\n", ss); JS_FreeCString(ctx, ss); } JS_FreeValue(ctx, st); JS_FreeCString(ctx, s); JS_FreeValue(ctx, ex); }
  else if(!JS_IsUndefined(r)){ const char *s = JS_ToCString(ctx, r); printf("=> %s\n", s ? s : "?"); JS_FreeCString(ctx, s); }
  JS_FreeValue(ctx, r);
  { JSContext *c2; while(JS_ExecutePendingJob(rt, &c2) > 0){} }                      /* promise jobs */
  free(code); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 0;
}
