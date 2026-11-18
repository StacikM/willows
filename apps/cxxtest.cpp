// cxxtest: C++ exceptions on Willows. Built with MinGW-w64 (x86_64-w64-mingw32-g++ -static), whose throw/catch goes through
// Windows' structured exception handling: RaiseException, the frames' language handler (_GCC_specific_handler), RtlUnwindEx,
// RtlVirtualUnwind and RtlLookupFunctionEntry on the program's .pdata. `cxxtest` prints PASS/FAIL lines and a summary.
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

static int fails, total;
static void check(const char *what, bool ok){ total++; if(!ok) fails++; std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); }

static int alive;                                   // objects constructed and not yet destroyed
struct Guard { int id; explicit Guard(int i) : id(i) { alive++; } ~Guard(){ alive--; } };

__attribute__((noinline)) static void deep(int n){
  Guard g(n);
  std::vector<std::string> work(3, "some text that is long enough to live on the heap");
  if(n == 0) throw std::runtime_error("from the bottom");
  deep(n - 1);
}
__attribute__((noinline)) static int thrower(int v){ if(v > 0) throw v; return v; }

static long WINAPI on_raise(EXCEPTION_POINTERS *p){
  if(p->ExceptionRecord->ExceptionCode == 0xE0000042) return EXCEPTION_CONTINUE_EXECUTION;
  return EXCEPTION_CONTINUE_SEARCH;
}

int main(){
  // 1. a plain int, caught one frame up
  int got = 0; try { thrower(7); } catch(int v){ got = v; }
  check("throw int, catch int", got == 7);

  // 2. through several frames, destructors run on the way, caught by the base class
  std::string msg; alive = 0;
  try { deep(5); } catch(const std::exception &e){ msg = e.what(); }
  check("exception through 6 frames, caught as std::exception", msg == "from the bottom");
  if(msg != "from the bottom") std::printf("      (what() = \"%s\")\n", msg.c_str());
  check("destructors ran during the unwind", alive == 0);

  // 3. rethrow from a catch block
  int rethrown = 0;
  try { try { thrower(3); } catch(int){ throw; } } catch(int v){ rethrown = v; }
  check("rethrow with throw;", rethrown == 3);

  // 4. a different exception thrown inside a catch block
  std::string second;
  try { try { thrower(1); } catch(int){ throw std::logic_error("second"); } } catch(const std::logic_error &e){ second = e.what(); }
  check("new exception thrown inside a catch block", second == "second");
  if(second != "second") std::printf("      (what() = \"%s\")\n", second.c_str());

  // 5. catch(...) and a loop of many throws (the stack and heap stay balanced)
  int caught = 0; for(int i = 0; i < 200; i++){ try { if(i % 2) throw i; else throw std::string("x"); } catch(...){ caught++; } }
  check("200 throws caught with catch(...)", caught == 200);

  // 6. RaiseException with a vectored handler that continues execution
  PVOID h = AddVectoredExceptionHandler(1, on_raise);
  RaiseException(0xE0000042, 0, 0, nullptr);
  check("RaiseException continued by a vectored handler", true);
  RemoveVectoredExceptionHandler(h);

  // 7. a stack walk with RtlCaptureContext / RtlLookupFunctionEntry / RtlVirtualUnwind finds main's caller
  CONTEXT ctx; RtlCaptureContext(&ctx); int frames = 0;
  for(; frames < 32 && ctx.Rip; frames++){
    DWORD64 base; PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
    if(!fe){ ctx.Rip = *(DWORD64 *)ctx.Rsp; ctx.Rsp += 8; break; }
    void *hd; DWORD64 frame; RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fe, &ctx, &hd, &frame, nullptr);
  }
  check("stack walk through the unwind tables", frames >= 1);

  std::printf("\n%s: %d of %d checks passed\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", total - fails, total);
  return fails;
}
