/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2020 Huacai Chen <chenhc@lemote.com>
 * Copyright (c) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 */

#ifndef LOONSGON_LIOINTC_H
#define LOONGSON_LIOINTC_H

#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define NUM_IRQS                32

#define NUM_CORES               4
#define NUM_IPS                 4
#define NUM_PARENTS             (NUM_CORES * NUM_IPS)
#define PARENT_COREx_IPy(x, y)  (NUM_IPS * x + y)

#define R_MAPPER_START          0x0
#define R_MAPPER_END            0x20
#define R_ISR                   R_MAPPER_END
#define R_IEN                   0x24
#define R_IEN_SET               0x28
#define R_IEN_CLR               0x2c
#define R_ISR_SIZE              0x8
#define R_START                 0x40
#define R_END                   0x64

#define TYPE_LOONGSON_LIOINTC "loongson.liointc"
DECLARE_INSTANCE_CHECKER(struct loongson_liointc, LOONGSON_LIOINTC,
                         TYPE_LOONGSON_LIOINTC)

#endif /* LOONGSON_LIOINTC_H */
