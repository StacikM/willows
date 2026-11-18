#pragma once
/* C++ in the kernel: include this instead of kernel.h in src/*.cpp.
   The kernel's C++ is freestanding: no exceptions, no RTTI, no standard library. Classes, templates, virtual functions,
   lambdas, constexpr, RAII and new/delete (on the kernel heap, see cxxrt.cpp) all work; global constructors run at boot.
   Like the C code, C++ code must not use floating point or SSE (-mgeneral-regs-only). */
#include "kernel.h"

typedef unsigned long size_t;
inline void *operator new(size_t, void *p) noexcept { return p; }      /* placement new */
inline void *operator new[](size_t, void *p) noexcept { return p; }

/* the kernel stops with a message (an out-of-memory new, a pure virtual call ...) */
extern "C" [[noreturn]] void cxx_fatal(const char *why);

namespace kern {
  template<class T> constexpr T min(T a, T b){ return a < b ? a : b; }
  template<class T> constexpr T max(T a, T b){ return a > b ? a : b; }
  template<class T> T &&move(T &x){ return static_cast<T &&>(x); }

  /* a heap block freed when it goes out of scope */
  template<class T> class unique {
    T *p;
  public:
    explicit unique(T *q = nullptr) : p(q) {}
    unique(unique &&o) : p(o.p) { o.p = nullptr; }
    unique &operator=(unique &&o){ if(this != &o){ delete p; p = o.p; o.p = nullptr; } return *this; }
    unique(const unique &) = delete; unique &operator=(const unique &) = delete;
    ~unique(){ delete p; }
    T *get() const { return p; } T *operator->() const { return p; } T &operator*() const { return *p; }
    T *release(){ T *q = p; p = nullptr; return q; }
    explicit operator bool() const { return p != nullptr; }
  };

  /* a growable array on the kernel heap */
  template<class T> class vec {
    T *d = nullptr; u64 n = 0, cap = 0;
  public:
    vec() = default; vec(const vec &) = delete; vec &operator=(const vec &) = delete;
    ~vec(){ clear(); heap_free(d); }
    bool push(const T &v){ if(!grow()) return false; new(&d[n++]) T(v); return true; }
    bool push(T &&v){ if(!grow()) return false; new(&d[n++]) T(move(v)); return true; }
    void clear(){ for(u64 i = 0; i < n; i++) d[i].~T(); n = 0; }
    u64 size() const { return n; } T &operator[](u64 i){ return d[i]; } const T &operator[](u64 i) const { return d[i]; }
    T *begin(){ return d; } T *end(){ return d + n; }
  private:
    bool grow(){
      if(n < cap) return true;
      u64 nc = cap ? cap * 2 : 8; T *nd = static_cast<T *>(heap_alloc(nc * sizeof(T))); if(!nd) return false;
      for(u64 i = 0; i < n; i++){ new(&nd[i]) T(move(d[i])); d[i].~T(); }
      heap_free(d); d = nd; cap = nc; return true;
    }
  };
}
