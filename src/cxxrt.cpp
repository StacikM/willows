/* The kernel's C++ runtime: what g++ expects from the environment when code is built with -fno-exceptions -fno-rtti
   -fno-threadsafe-statics and no standard library. new/delete use the kernel heap; global constructors are listed in
   .init_array (linker.ld) and run by cxx_init() at boot; static destructors never run (the kernel does not exit). */
#include "cxx.h"

extern "C" [[noreturn]] void cxx_fatal(const char *why){ kernel_panic("kernel C++: %s", why); }

void *operator new(size_t n){ void *p = heap_alloc(n ? n : 1); if(!p) cxx_fatal("out of memory in new"); return p; }
void *operator new[](size_t n){ return operator new(n); }
void operator delete(void *p) noexcept { heap_free(p); }
void operator delete[](void *p) noexcept { heap_free(p); }
void operator delete(void *p, size_t) noexcept { heap_free(p); }
void operator delete[](void *p, size_t) noexcept { heap_free(p); }

extern "C" {
  void __cxa_pure_virtual(void){ cxx_fatal("pure virtual function called"); }
  void *__dso_handle = nullptr;
  int __cxa_atexit(void (*)(void *), void *, void *){ return 0; }   /* static destructors: the kernel never exits */

  extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
  void cxx_init(void){ for(void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)(); }
}

/* ---- a self-test, run once at boot: classes with virtual functions, a global constructor, templates, RAII ---- */
namespace {
  struct shape { virtual ~shape() = default; virtual int area() const = 0; };
  struct rect : shape { int w, h; rect(int a, int b) : w(a), h(b) {} int area() const override { return w * h; } };
  struct square : rect { explicit square(int s) : rect(s, s) {} };
  struct boot_marker { int value; boot_marker() : value(42) {} };
  boot_marker marker;                                   /* constructed by cxx_init() through .init_array */
}
extern "C" int cxx_selftest(void){
  if(marker.value != 42) return 0;
  kern::vec<kern::unique<shape>> shapes;
  shapes.push(kern::unique<shape>(new rect(3, 4)));    /* moved into the vector */
  int total = 0; for(auto &s : shapes) total += s->area();
  kern::unique<shape> sq(new square(5)); total += sq->area();
  auto twice = [](int x){ return 2 * x; };
  return total == 37 && twice(kern::max(3, 4)) == 8;
}
