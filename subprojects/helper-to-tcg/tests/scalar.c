#include <stdint.h>

/* Simple arithmetic */
uint32_t helper_add(uint32_t a, uint32_t b) {
    return a + b;
}

/* Control flow reducable to conditinal move */
uint32_t helper_cmov(uint32_t c0, uint32_t c1, uint32_t a, uint32_t b) {
    if (c0 < c1) {
        return a;
    } else {
        return b;
    }
}
