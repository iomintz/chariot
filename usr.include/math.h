
#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define HUGE_VAL 1e10000
#define INFINITY __builtin_huge_val()
#define NAN __builtin_nan("")
#define M_E 2.718281828459045
#define M_PI 3.141592653589793
#define M_PI_2 (M_PI / 2)
#define M_TAU (M_PI * 2)
#define M_LN2 0.69314718055995
#define M_LN10 2.30258509299405
#define M_SQRT2 1.4142135623730951
#define M_SQRT1_2 0.7071067811865475

double acos(double);
float acosf(float);
double asin(double);
float asinf(float);
double atan(double);
float atanf(float);
double atan2(double, double);
float atan2f(float, float);
double cos(double);
float cosf(float);
double cosh(double);
float coshf(float);
double sin(double);
float sinf(float);
double sinh(double);
float sinhf(float);
double tan(double);
float tanf(float);
double tanh(double);
float tanhf(float);
double ceil(double);
float ceilf(float);
double floor(double);
float floorf(float);
double round(double);
float roundf(float);
double fabs(double);
float fabsf(float);
double fmod(double, double);
float fmodf(float, float);
double exp(double);
float expf(float);
double exp2(double);
float exp2f(float);
double frexp(double, int* exp);
float frexpf(float, int* exp);
double log(double);
float logf(float);
double log10(double);
float log10f(float);
double sqrt(double);
float sqrtf(float);
double modf(double, double*);
float modff(float, float*);
double ldexp(double, int exp);
float ldexpf(float, int exp);

double pow(double x, double y);
float powf(float x, float y);

double log2(double);
float log2f(float);
long double log2l(long double);
double frexp(double, int*);
float frexpf(float, int*);
long double frexpl(long double, int*);

double gamma(double);
double expm1(double);
double cbrt(double);
double log1p(double);
double acosh(double);
double asinh(double);
double atanh(double);
double hypot(double, double);
double erf(double);
double erfc(double);



double trunc(double d);

double fmax(double a, double b);
double fmin(double a, double b);
int isinf(double d);
int isnan(double d);


int abs(int);

#ifdef __cplusplus
}
#endif

#endif
