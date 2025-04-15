#ifndef THREADS_FIXED_POINT_ARITH
#define THREADS_FIXED_POINT_ARITH

#define F (1 << 14)
typedef int fixed_point;

#include <stdint.h>

fixed_point conv_int_to_fp (int n);
int conv_fp_to_int_rnd_zero (fixed_point x);
int conv_fp_to_int_rnd_nrst (fixed_point x);
fixed_point add_fp_fp (fixed_point x, fixed_point y);
fixed_point sub_fp_fp (fixed_point x, fixed_point y);
fixed_point add_fp_int (fixed_point x, int n);
fixed_point sub_fp_int (fixed_point x, int n);
fixed_point mult_fp_fp (fixed_point x, fixed_point y);
fixed_point mult_fp_int (fixed_point x, int n);
fixed_point div_fp_fp (fixed_point x, fixed_point y);
fixed_point div_fp_int (fixed_point x, int n);

#endif /* threads/fixed-point-arith.h */