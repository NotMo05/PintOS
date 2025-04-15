#include "threads/fixed-point-arith.h"

fixed_point conv_int_to_fp (int n)
{
  return n * F;    
}

int conv_fp_to_int_rnd_zero (fixed_point x)
{
  return x / F;
}

int conv_fp_to_int_rnd_nrst (fixed_point x)
{
  if (x >= 0)
    {
      return (x + F / 2) / F;
    }
  else
    {
      return (x - F / 2) / F;
    }
  
}

fixed_point add_fp_fp (fixed_point x, fixed_point y)
{
  return x + y;
}

fixed_point sub_fp_fp (fixed_point x, fixed_point y)
{
  return x - y;
}

fixed_point add_fp_int (fixed_point x, int n)
{
  return x + n * F;
}

fixed_point sub_fp_int (fixed_point x, int n)
{
  return x - n * F;
}

fixed_point mult_fp_fp (fixed_point x, fixed_point y)
{
  return ((int64_t) x) * y / F;
}

fixed_point mult_fp_int (fixed_point x, int n)
{
  return x * n;
}

fixed_point div_fp_fp (fixed_point x, fixed_point y)
{
  return ((int64_t) x) * F / y;
}

fixed_point div_fp_int (fixed_point x, int n)
{
  return x / n;
}