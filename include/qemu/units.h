/*
 * IEC binary prefixes definitions
 *
 * Copyright (C) 2015 Nikunj A Dadhania, IBM Corporation
 * Copyright (C) 2018 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_UNITS_H
#define QEMU_UNITS_H

#define KiB     (INT64_C(1) << 10)
#define MiB     (INT64_C(1) << 20)
#define GiB     (INT64_C(1) << 30)
#define TiB     (INT64_C(1) << 40)
#define PiB     (INT64_C(1) << 50)
#define EiB     (INT64_C(1) << 60)

#define SI_k 1000LL
#define SI_M 1000000LL
#define SI_G 1000000000LL
#define SI_T 1000000000000LL
#define SI_P 1000000000000000LL
#define SI_E 1000000000000000000LL

#endif
