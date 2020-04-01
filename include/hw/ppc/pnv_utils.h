/*
 * QEMU PowerPC PowerNV utilities
 *
 * Copyright (c) 2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_PNV_UTILS_H
#define PPC_PNV_UTILS_H

/*
 * QEMU version of the GETFIELD/SETFIELD macros used in skiboot to
 * define the register fields.
 */

static inline uint64_t PNV_GETFIELD(uint64_t mask, uint64_t word)
{
    return (word & mask) >> ctz64(mask);
}

static inline uint64_t PNV_SETFIELD(uint64_t mask, uint64_t word,
                                    uint64_t value)
{
    return (word & ~mask) | ((value << ctz64(mask)) & mask);
}

#endif /* PPC_PNV_UTILS_H */
