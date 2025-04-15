#ifndef PINTOS_47_FIXED_POINT_H
#define PINTOS_47_FIXED_POINT_H

#include <debug.h>
#include <stdint.h>

#define FP_Q 14
#define FP_P 17
#define FP_F (1 << FP_Q)

static inline int32_t convert_to_fp(int32_t n) {
    return n * FP_F;
}

static inline int32_t convert_to_int(int32_t x) {
    return x / FP_F;
}

static inline int32_t fpadd_fp(int32_t x, int32_t y) {
    return x + y;
}

static inline int32_t fpsub_fp(int32_t x, int32_t y) {
    return x - y;
}

static inline int32_t fpadd_int(int32_t x, int32_t n) {
    return x + convert_to_fp(n);
}

static inline int32_t fpsub_int(int32_t x, int32_t n) {
    return x - convert_to_fp(n);
}

static inline int32_t fpmul_fp(int32_t x, int32_t y) {
    return ((int64_t) x) * y / FP_F;
}

static inline int32_t fpmul_int(int32_t x, int32_t n) {
    return x * n;
}

static inline int32_t fpdiv_fp(int32_t x, int32_t y) {
    return ((int64_t) x) * FP_F / y;
}

static inline int32_t fpdiv_int(int32_t x, int32_t n) {
    return x / n;
}

#endif //PINTOS_47_FIXED_POINT_H
