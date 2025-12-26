/*
 * Shared definitions for the TCG printf-style helper.
 *
 * Copyright (c) 2025 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TCG_TCG_PRINT_H
#define TCG_TCG_PRINT_H

#define TCG_PRINT_MAX_ARGS 5

typedef enum TCGPrintArgType {
    TCG_PRINT_ARG_END = 0,
    TCG_PRINT_ARG_I32 = 1,
    TCG_PRINT_ARG_I64 = 2,
    TCG_PRINT_ARG_PTR = 3,
} TCGPrintArgType;

#define TCG_PRINT_DESC_COUNT_MASK 0xF
#define TCG_PRINT_DESC_SHIFT 4
#define TCG_PRINT_DESC_BITS_PER_ARG 3
#define TCG_PRINT_DESC_TYPE_MASK ((1u << TCG_PRINT_DESC_BITS_PER_ARG) - 1)

static inline unsigned tcg_print_desc_count(uint32_t desc)
{
    return desc & TCG_PRINT_DESC_COUNT_MASK;
}

static inline unsigned tcg_print_desc_type(uint32_t desc, unsigned index)
{
    return (desc >> (TCG_PRINT_DESC_SHIFT +
                     index * TCG_PRINT_DESC_BITS_PER_ARG))
        & TCG_PRINT_DESC_TYPE_MASK;
}

static inline uint32_t tcg_print_desc_add_type(uint32_t desc, unsigned index,
                                               TCGPrintArgType type)
{
    return desc | ((uint32_t)type & TCG_PRINT_DESC_TYPE_MASK)
        << (TCG_PRINT_DESC_SHIFT + index * TCG_PRINT_DESC_BITS_PER_ARG);
}

#endif /* TCG_TCG_PRINT_H */
