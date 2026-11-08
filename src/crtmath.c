#include "kernel.h"
/* Floating-point C runtime functions. This file is built WITH SSE (the rest of the kernel is not), so doubles/floats can be used here.
   Only simple, dependency-free implementations: enough for typical programs. */
#define M(n) {"msvcrt", #n, m_##n}
static double pi_ = 3.14159265358979323846;
static double WINAPI m_floor(double x){ if(x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0) return x; long long i = (long long)x; double r = (double)i; return r > x ? r - 1 : r; }
static double WINAPI m_ceil(double x){ if(x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0) return x; long long i = (long long)x; double r = (double)i; return r < x ? r + 1 : r; }
static double WINAPI m_trunc(double x){ if(x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0) return x; return (double)(long long)x; }
static double WINAPI m_round(double x){ return x >= 0 ? m_floor(x + 0.5) : m_ceil(x - 0.5); }
static double WINAPI m_fabs(double x){ return x < 0 ? -x : x; }
static double WINAPI m_sqrt(double x){ double r; __asm__("sqrtsd %1,%0":"=x"(r):"x"(x)); return r; }
static double WINAPI m_fmod(double x, double y){ if(y == 0 || x != x || y != y) return 0.0 / 0.0 * 0; return x - m_trunc(x / y) * y; }
static double sin_core(double x){
  x = m_fmod(x, 2 * pi_); if(x > pi_) x -= 2 * pi_; if(x < -pi_) x += 2 * pi_;
  double x2 = x * x, t = x, s = x; for(int i = 1; i < 14; i++){ t *= -x2 / ((2 * i) * (2 * i + 1)); s += t; } return s;
}
static double WINAPI m_sin(double x){ return sin_core(x); }
static double WINAPI m_cos(double x){ return sin_core(x + pi_ / 2); }
static float WINAPI m_floorf(float x){ return (float)m_floor(x); }
static float WINAPI m_ceilf(float x){ return (float)m_ceil(x); }
static float WINAPI m_roundf(float x){ return (float)m_round(x); }
static float WINAPI m_fabsf(float x){ return x < 0 ? -x : x; }
static float WINAPI m_sqrtf(float x){ return (float)m_sqrt(x); }
static float WINAPI m_fmodf(float x, float y){ return (float)m_fmod(x, y); }
static float WINAPI m_sinf(float x){ return (float)sin_core(x); }
static float WINAPI m_cosf(float x){ return (float)sin_core(x + pi_ / 2); }
const struct export math_exports[] = { M(floor), M(ceil), M(trunc), M(round), M(fabs), M(sqrt), M(fmod), M(sin), M(cos), M(floorf), M(ceilf), M(roundf), M(fabsf), M(sqrtf), M(fmodf), M(sinf), M(cosf) };
const unsigned math_nexports = sizeof math_exports / sizeof *math_exports;
