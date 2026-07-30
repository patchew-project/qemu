#include <stdint.h>
#include <string.h>

__attribute__((annotate("ptr-to-offset: 0, 1"))) void
helper_vec_constant(void *restrict d, void *restrict a) {
    const uint8_t arr[] = {
        1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    };
    for (int i = 0; i < 32; ++i) {
        ((uint8_t *)d)[i] = ((uint8_t *)a)[i] + arr[i];
    }
}

__attribute__((annotate("ptr-to-offset: 0, 1"))) void
helper_vec_splat(void *restrict d, void *restrict a) {
    const uint8_t arr[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
    };
    for (int i = 0; i < 32; ++i) {
        ((uint8_t *)d)[i] = ((uint8_t *)a)[i] + arr[i];
    }
}

#define GENSZ(NAME, SRC, DST)                                                  \
    __attribute__((annotate("ptr-to-offset: 0, 1, 2"))) void                   \
    helper_vec_##NAME##_##SRC##_##DST(void *restrict d, void *restrict a,      \
                                      void *restrict b) {                      \
        for (int i = 0; i < 32; ++i) {                                         \
            ((DST *)d)[i] = (DST)((SRC *)a)[i] + (DST)((SRC *)b)[i];           \
        }                                                                      \
    }

GENSZ(trunc, uint64_t, uint8_t)
GENSZ(trunc, uint64_t, uint16_t)
GENSZ(trunc, uint32_t, uint8_t)
GENSZ(trunc, uint32_t, uint16_t)
GENSZ(trunc, uint16_t, uint8_t)

GENSZ(zext, uint8_t, uint64_t)
GENSZ(zext, uint16_t, uint64_t)
GENSZ(zext, uint8_t, uint32_t)
GENSZ(zext, uint16_t, uint32_t)
GENSZ(zext, uint8_t, uint16_t)

GENSZ(sext, int8_t, int64_t)
GENSZ(sext, int16_t, int64_t)
GENSZ(sext, int8_t, int32_t)
GENSZ(sext, int16_t, int32_t)
GENSZ(sext, int8_t, int16_t)
