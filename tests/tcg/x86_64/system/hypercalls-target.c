/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test target invokes a hypercall to write the value 0x1337 to a
 * variable.
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <minilib.h>

#define _hypercall(num, arg0, arg1, arg2, arg3)                      \
    unsigned int a __attribute__((unused)) = 0;                     \
    unsigned int b __attribute__((unused)) = 0;                     \
    unsigned int c __attribute__((unused)) = 0;                     \
    unsigned int d __attribute__((unused)) = 0;                     \
    __asm__ __volatile__("cpuid\n\t"                                \
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)       \
                         : "a"(num), "D"(arg0), "S"(arg1), \
                           "d"(arg2), "c"(arg3));

#define hypercall(num, arg0, arg1, arg2, arg3) \
    { \
        unsigned int __num = 0x4711 | (num << 16); \
        _hypercall(__num, arg0, arg1, arg2, arg3); \
    }

int main(void)
{
    uint16_t value = 0;

    for (size_t i = 0; i < 1000000; i++) {
        hypercall(1, &value, sizeof(value), 0, 0);
        if (value == 0x1337) {
            ml_printf("Victory!\n");
            return 0;
        }
    }
    return 0;
}
