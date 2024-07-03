/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongarch ipi interrupt header files
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_IPI_H
#define HW_LOONGARCH_IPI_H

#include "qom/object.h"
#include "hw/intc/loongson_ipi_common.h"
#include "hw/sysbus.h"

#define TYPE_LOONGARCH_IPI  "loongarch_ipi"
typedef struct LoongarchIPIClass LoongarchIPIClass;
typedef struct LoongarchIPIState LoongarchIPIState;
DECLARE_OBJ_CHECKERS(LoongarchIPIState, LoongarchIPIClass,
                     LOONGARCH_IPI, TYPE_LOONGARCH_IPI)

struct LoongarchIPIState {
    LoongsonIPICommonState parent_obj;
};

struct LoongarchIPIClass {
    /*< private >*/
    LoongsonIPICommonClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
};

#endif
