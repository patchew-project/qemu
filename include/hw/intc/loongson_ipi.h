/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2020-2021 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 */

#ifndef LOONGSON_IPI_H
#define LOONGSON_IPI_H

#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_LOONGSON_IPI "loongson.ipi"
#define LOONGSON_IPI(obj) OBJECT_CHECK(struct loongson_ipi, (obj), TYPE_LOONGSON_IPI)

#endif /* LOONGSON_IPI_H */
