#pragma once
typedef double double_t; typedef float float_t;
#define FLT_EVAL_METHOD 0
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_LOG2E 1.4426950408889634074
#define M_LOG10E 0.43429448190325182765
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440
#define NAN (__builtin_nan(""))
#define INFINITY (__builtin_inf())
#define HUGE_VAL (__builtin_inf())
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(0,1,4,3,2,x)
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define MATHFN1(n) double n(double);
MATHFN1(sin) MATHFN1(cos) MATHFN1(tan) MATHFN1(asin) MATHFN1(acos) MATHFN1(atan) MATHFN1(sinh) MATHFN1(cosh) MATHFN1(tanh)
MATHFN1(asinh) MATHFN1(acosh) MATHFN1(atanh) MATHFN1(exp) MATHFN1(expm1) MATHFN1(log) MATHFN1(log2) MATHFN1(log10) MATHFN1(log1p)
MATHFN1(sqrt) MATHFN1(cbrt) MATHFN1(floor) MATHFN1(ceil) MATHFN1(trunc) MATHFN1(round) MATHFN1(fabs) MATHFN1(rint)
double pow(double, double); double atan2(double, double); double hypot(double, double); double fmod(double, double);
double frexp(double, int *); double modf(double, double *); double scalbn(double, int); double ldexp(double, int);
long lrint(double); double copysign(double, double); double fmin(double, double); double fmax(double, double);
float sqrtf(float); float fabsf(float); float floorf(float);
