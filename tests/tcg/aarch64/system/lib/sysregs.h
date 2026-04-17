/*
 * AArch64 system register helpers
 *
 * Based on the helpers from Linux
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)

#define read_sysreg(r) ({                                           \
            uint64_t __val;                                         \
            asm volatile("mrs %0, " __stringify(r) : "=r" (__val)); \
            __val;                                                  \
})

#define write_sysreg(v, r) do {                     \
        uint64_t __val = (uint64_t)(v);             \
        asm volatile("msr " __stringify(r) ", %x0"  \
                 : : "rZ" (__val));                 \
} while (0)
