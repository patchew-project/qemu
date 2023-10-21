/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#ifndef FSI_FSI_H
#define FSI_FSI_H

/* Bitwise operations at the word level. */
#define BE_BIT(x)                          BIT(31 - (x))
#define GENMASK(t, b) \
    (((1ULL << ((t) + 1)) - 1) & ~((1ULL << (b)) - 1))
#define BE_GENMASK(t, b)                   GENMASK(BE_BIT(t), BE_BIT(b))

#endif
