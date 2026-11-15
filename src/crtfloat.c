/* The C runtime's math functions, from musl (apps/qjs/math, MIT licence), built into the kernel with the Microsoft calling
   convention and SSE (see the Makefile: this file and build/kmath/*.o use -mabi=ms -msse2), so they can be exported directly.
   This file adds the float versions, the MSVC classification helpers and a few functions musl's subset lacks.
   It must not call kernel functions (they use the System V convention); it only computes. */
#include <math.h>
typedef unsigned long long u64; typedef unsigned int u32; typedef long long i64;
struct export { const char *dll, *name; void *fn; };

double exp2_(double x){ return pow(2.0,x); }
double nearbyint_(double x){ return rint(x); }
double remainder_(double x, double y){ double n=rint(x/y); return x-n*y; }
double fma_(double x, double y, double z){ return x*y+z; }
i64 llround_(double x){ return (i64)round(x); }
i64 llrint_(double x){ return (i64)rint(x); }
double logb_(double x){ int e; if(x==0) return -INFINITY; frexp(x,&e); return e-1; }
int ilogb_(double x){ int e; if(x==0) return -2147483647-1; frexp(x,&e); return e-1; }
double nextafter_(double x, double y){
  if(isnan(x)||isnan(y)) return x+y; if(x==y) return y;
  union { double d; u64 i; } u={x}; if(x==0){ u.i=1; return y>0? u.d : -u.d; }
  if((x<y)==(x>0)) u.i++; else u.i--; return u.d;
}
float nextafterf_(float x, float y){
  if(isnan(x)||isnan(y)) return x+y; if(x==y) return y;
  union { float f; u32 i; } u={x}; if(x==0){ u.i=1; return y>0? u.f : -u.f; }
  if((x<y)==(x>0)) u.i++; else u.i--; return u.f;
}
double lgamma_(double x){                                      /* log|Gamma(x)|: Lanczos approximation, reflection for x < 0.5 */
  static const double g=7, c[9]={0.99999999999980993,676.5203681218851,-1259.1392167224028,771.32342877765313,-176.61502916214059,12.507343278686905,-0.13857109526572012,9.9843695780195716e-6,1.5056327351493116e-7};
  if(isnan(x)) return x; if(isinf(x)) return INFINITY;
  if(x<0.5){ double s=sin(M_PI*x); if(s==0) return INFINITY; return log(M_PI/fabs(s))-lgamma_(1-x); }
  x-=1; double a=c[0], t=x+g+0.5; for(int i=1;i<9;i++) a+=c[i]/(x+i);
  return 0.5*log(2*M_PI)+(x+0.5)*log(t)-t+log(a);
}
double tgamma_(double x){ double l=lgamma_(x); double r=exp(l); if(x<0 && fmod(floor(-x),2.0)==0) r=-r; return r; }
double erf_(double x){                                         /* Abramowitz-Stegun 7.1.26 refined with a series near 0 */
  double ax=fabs(x); if(ax<0.5){ double x2=x*x, t=x, s=x; for(int n=1;n<30;n++){ t*=-x2/n; s+=t/(2*n+1); } return s*1.1283791670955126; }
  double t=1/(1+0.5*ax); double y=1-t*exp(-ax*ax-1.26551223+t*(1.00002368+t*(0.37409196+t*(0.09678418+t*(-0.18628806+t*(0.27886807+t*(-1.13520398+t*(1.48851587+t*(-0.82215223+t*0.17087277)))))))));
  return x<0? -y : y;
}
double erfc_(double x){ return 1-erf_(x); }
double fdim_(double x, double y){ return x>y? x-y : 0; }
/* MSVC: _dclass returns FP_INFINITE 1, FP_NAN 2, FP_NORMAL -1, FP_SUBNORMAL -2, FP_ZERO 0 */
short dclass_(double x){ return isnan(x)? 2 : isinf(x)? 1 : x==0? 0 : fabs(x)<2.2250738585072014e-308? -2 : -1; }
short fdclass_(float x){ return isnan(x)? 2 : isinf(x)? 1 : x==0? 0 : fabs((double)x)<1.17549435e-38? -2 : -1; }
int dsign_(double x){ return signbit(x)? 0x8000 : 0; }
int fdsign_(float x){ return signbit(x)? 0x8000 : 0; }
int finite_(double x){ return isfinite(x); }
int isnan__(double x){ return isnan(x); }
int finitef_(float x){ return isfinite(x); }
int isnanf_(float x){ return isnan(x); }
double chgsign_(double x){ return -x; }
double logbd_(double x){ return logb_(x); }

#define F1(n) float n##f_(float x){ return (float)n(x); }
#define F2(n) float n##f_(float x, float y){ return (float)n(x,y); }
F1(tan) F1(asin) F1(acos) F1(atan) F1(sinh) F1(cosh) F1(tanh) F1(asinh) F1(acosh) F1(atanh) F1(exp) F1(expm1) F1(log) F1(log2) F1(log10) F1(log1p)
F1(cbrt) F1(trunc) F1(rint) F1(sin) F1(cos)
F2(pow) F2(atan2) F2(hypot) F2(fmin) F2(fmax) F2(copysign)
float exp2f_(float x){ return (float)pow(2.0,x); }
float nearbyintf_(float x){ return (float)rint(x); }
float lgammaf_(float x){ return (float)lgamma_(x); }
float tgammaf_(float x){ return (float)tgamma_(x); }
float erff_(float x){ return (float)erf_(x); }
float erfcf_(float x){ return (float)erfc_(x); }
float fmaf_(float x, float y, float z){ return (float)((double)x*y+z); }
float scalbnf_(float x, int n){ return (float)scalbn(x,n); }
float ldexpf_(float x, int n){ return (float)scalbn(x,n); }
float frexpf_(float x, int *e){ return (float)frexp(x,e); }
float modff_(float x, float *ip){ double i; double r=modf(x,&i); *ip=(float)i; return (float)r; }
float remainderf_(float x, float y){ return (float)remainder_(x,y); }
i64 lroundf_(float x){ return (i64)round(x); }
i64 lrintf_(float x){ return (i64)rint(x); }
float truncf2_(float x){ return (float)trunc(x); }
long double lgammal_(long double x){ return lgamma_((double)x); }
i64 lround_(double x){ return (i64)round(x); }

#define E(n) {"msvcrt",#n,n}
#define EF(n) {"msvcrt",#n,n##_}
const struct export math2_exports[] = {
  E(acos), E(acosh), E(asin), E(asinh), E(atan), E(atan2), E(atanh), E(cbrt), E(copysign), E(cos), E(cosh), E(exp), E(expm1), E(floor), E(ceil), E(fabs),
  E(fmax), E(fmin), E(fmod), E(frexp), E(hypot), E(ldexp), E(log10), E(log1p), E(log2), E(log), E(lrint), E(modf), E(pow), E(rint), E(round), E(scalbn),
  E(sin), E(sinh), E(sqrt), E(tan), E(tanh), E(trunc), {"msvcrt","_hypot",hypot}, {"msvcrt","_copysign",copysign}, {"msvcrt","scalbln",scalbn}, {"msvcrt","_scalb",scalbn},
  EF(exp2), EF(nearbyint), EF(remainder), EF(fma), EF(llround), EF(llrint), EF(lround), EF(logb), EF(ilogb), EF(nextafter), EF(nextafterf),
  {"msvcrt","nexttoward",nextafter_}, EF(lgamma), EF(tgamma), EF(erf), EF(erfc), EF(fdim), {"msvcrt","_dclass",dclass_}, {"msvcrt","_fdclass",fdclass_},
  {"msvcrt","_dsign",dsign_}, {"msvcrt","_fdsign",fdsign_}, {"msvcrt","_finite",finite_}, {"msvcrt","_isnan",isnan__}, {"msvcrt","_finitef",finitef_}, {"msvcrt","_isnanf",isnanf_},
  {"msvcrt","_chgsign",chgsign_}, {"msvcrt","_logb",logbd_},
  EF(tanf), EF(asinf), EF(acosf), EF(atanf), EF(sinhf), EF(coshf), EF(tanhf), EF(asinhf), EF(acoshf), EF(atanhf), EF(expf), EF(expm1f), EF(logf), EF(log2f),
  EF(log10f), EF(log1pf), EF(cbrtf), EF(rintf), EF(sinf), EF(cosf), EF(powf), EF(atan2f), EF(hypotf), {"msvcrt","_hypotf",hypotf_}, EF(fminf), EF(fmaxf), EF(copysignf),
  {"msvcrt","_copysignf",copysignf_}, EF(exp2f), EF(nearbyintf), EF(lgammaf), EF(tgammaf), EF(erff), EF(erfcf), EF(fmaf), EF(scalbnf), EF(ldexpf), EF(frexpf),
  EF(modff), EF(remainderf), EF(lroundf), EF(lrintf), {"msvcrt","llroundf",lroundf_}, {"msvcrt","llrintf",lrintf_}, {"msvcrt","truncf",truncf2_}, EF(lgammal),
};
const unsigned math2_nexports = sizeof math2_exports / sizeof *math2_exports;
