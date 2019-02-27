#ifndef TEST_TCG_S390x_VECTOR_H
#define TEST_TCG_S390x_VECTOR_H

#include <stdint.h>

typedef union S390Vector {
    __uint128_t v;
    uint64_t q[2];
    uint32_t d[4];
    uint16_t w[8];
    uint8_t h[16];
} S390Vector;

#define ES_8    0
#define ES_16   1
#define ES_32   2
#define ES_64   3
#define ES_128  4

static inline void check(const char *s, bool cond)
{
    if (!cond) {
        fprintf(stderr, "Check failed: %s\n", s);
        exit(-1);
    }
}

#endif /* TEST_TCG_S390x_VECTOR_H */
