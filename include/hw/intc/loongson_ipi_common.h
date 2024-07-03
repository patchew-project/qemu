/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson ipi interrupt header files
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGSON_IPI_COMMON_H
#define HW_LOONGSON_IPI_COMMON_H

#include "hw/sysbus.h"
#include "hw/core/cpu.h"
#include "qom/object.h"

/* Mainy used by iocsr read and write */
#define SMP_IPI_MAILBOX      0x1000ULL
#define CORE_STATUS_OFF       0x0
#define CORE_EN_OFF           0x4
#define CORE_SET_OFF          0x8
#define CORE_CLEAR_OFF        0xc
#define CORE_BUF_20           0x20
#define CORE_BUF_28           0x28
#define CORE_BUF_30           0x30
#define CORE_BUF_38           0x38
#define IOCSR_IPI_SEND        0x40
#define IOCSR_MAIL_SEND       0x48
#define IOCSR_ANY_SEND        0x158

#define MAIL_SEND_ADDR        (SMP_IPI_MAILBOX + IOCSR_MAIL_SEND)
#define MAIL_SEND_OFFSET      0
#define ANY_SEND_OFFSET       (IOCSR_ANY_SEND - IOCSR_MAIL_SEND)

#define IPI_MBX_NUM           4

#define TYPE_LOONGSON_IPI_COMMON "loongson_ipi_common"
typedef struct LoongsonIPICommonClass LoongsonIPICommonClass;
typedef struct LoongsonIPICommonState LoongsonIPICommonState;
DECLARE_OBJ_CHECKERS(LoongsonIPICommonState, LoongsonIPICommonClass,
                     LOONGSON_IPI_COMMON, TYPE_LOONGSON_IPI_COMMON)

typedef struct IPICore {
    LoongsonIPICommonState *ipi;
    uint32_t status;
    uint32_t en;
    uint32_t set;
    uint32_t clear;
    /* 64bit buf divide into 2 32bit buf */
    uint32_t buf[IPI_MBX_NUM * 2];
    qemu_irq irq;
} IPICore;

struct LoongsonIPICommonState {
    SysBusDevice parent_obj;
    MemoryRegion ipi_iocsr_mem;
    MemoryRegion ipi64_iocsr_mem;
    uint32_t num_cpu;
    IPICore *cpu;
};

struct LoongsonIPICommonClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    void (*pre_save)(LoongsonIPICommonState *s);
    void (*post_load)(LoongsonIPICommonState *s);
    AddressSpace *(*get_iocsr_as)(CPUState *cpu);
    CPUState *(*cpu_by_arch_id)(int64_t id);
};

#endif
