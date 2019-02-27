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

static inline void check(const char *s, bool cond)
{
    if (!cond) {
        fprintf(stderr, "Check failed: %s\n", s);
        exit(-1);
    }
}

#endif /* TEST_TCG_S390x_VECTOR_H */
