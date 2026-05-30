#ifndef NUMERICAL_SANITIZER_NUMERICAL_SANITIZER_H
#define NUMERICAL_SANITIZER_NUMERICAL_SANITIZER_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
  NSAN_OP_UNKNOWN = 0,
  NSAN_OP_FADD = 1,
  NSAN_OP_FSUB = 2,
  NSAN_OP_FMUL = 3,
  NSAN_OP_FDIV = 4,
  NSAN_OP_FREM = 5,
  NSAN_OP_FNEG = 6,
  NSAN_OP_FPTRUNC = 7,
  NSAN_OP_SELECT = 8,
  NSAN_OP_LOAD = 9,
  NSAN_OP_CALL = 10
};

double __nsan_shadow_from_float(float value);
double __nsan_shadow_load_float(const void *addr, float actual);
void __nsan_shadow_store_float(const void *addr, double shadow);
void __nsan_copy_shadow_bytes(const void *dst, const void *src,
                              unsigned long size);
void __nsan_move_shadow_bytes(const void *dst, const void *src,
                              unsigned long size);
void __nsan_forget_shadow_bytes(const void *addr, unsigned long size);

void __nsan_set_arg_shadow(unsigned index, double shadow);
double __nsan_get_arg_shadow(unsigned index, float actual);
void __nsan_clear_arg_shadows(unsigned count);
void __nsan_set_return_shadow(double shadow);
double __nsan_get_return_shadow(float actual);

double __nsan_shadow_sqrtf(float actual, double x);
double __nsan_shadow_sinf(float actual, double x);
double __nsan_shadow_cosf(float actual, double x);
double __nsan_shadow_tanf(float actual, double x);
double __nsan_shadow_expf(float actual, double x);
double __nsan_shadow_logf(float actual, double x);
double __nsan_shadow_powf(float actual, double x, double y);
double __nsan_shadow_fabsf(float actual, double x);
double __nsan_shadow_floorf(float actual, double x);
double __nsan_shadow_ceilf(float actual, double x);
double __nsan_shadow_fmodf(float actual, double x, double y);
double __nsan_shadow_atanf(float actual, double x);
double __nsan_shadow_atan2f(float actual, double x, double y);
double __nsan_shadow_asinf(float actual, double x);
double __nsan_shadow_acosf(float actual, double x);
double __nsan_shadow_sinhf(float actual, double x);
double __nsan_shadow_coshf(float actual, double x);

void __nsan_check_float(float actual, double shadow, int op,
                        const char *file, const char *function,
                        unsigned line, unsigned column);

void __nsan_check_binary_float(float actual, double shadow,
                               double lhs_shadow, double rhs_shadow, int op,
                               const char *file, const char *function,
                               unsigned line, unsigned column);

#ifdef __cplusplus
}
#endif

#endif
