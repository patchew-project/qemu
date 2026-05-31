/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Infineon TC39x SoC System emulation.
 *
 * Copyright (c) 2020 Andreas Konopik <andreas.konopik@efs-auto.de>
 * Copyright (c) 2020 David Brenken <david.brenken@efs-auto.de>
 */

#ifndef TC39XB_SOC_H
#define TC39XB_SOC_H

#include "hw/core/sysbus.h"
#include "hw/char/tricore_asclin.h"
#include "hw/intc/tricore_ir.h"
#include "hw/timer/tricore_stm.h"
#include "hw/tricore/tricore_scu.h"
#include "target/tricore/cpu.h"
#include "qom/object.h"

#define TYPE_TC39XB_SOC ("tc39xb-soc")
OBJECT_DECLARE_TYPE(TC39XBSoCState, TC39XBSoCClass, TC39XB_SOC)

typedef struct TC39XBSoCCPUMemState {
    MemoryRegion dspr;
    MemoryRegion pspr;
    MemoryRegion pflash_c;
    MemoryRegion pflash_u;
    MemoryRegion dlmu_c;
    MemoryRegion dlmu_u;
} TC39XBSoCCPUMemState;

typedef struct TC39XBSoCFlashMemState {
    MemoryRegion dflash0;
    MemoryRegion dflash1;
    MemoryRegion olda_c;
    MemoryRegion olda_u;
    MemoryRegion brom_c;
    MemoryRegion brom_u;
    MemoryRegion lmu0_c;
    MemoryRegion lmu0_u;
    MemoryRegion lmu1_c;
    MemoryRegion lmu1_u;
    MemoryRegion lmu2_c;
    MemoryRegion lmu2_u;
    MemoryRegion emem;
} TC39XBSoCFlashMemState;

typedef struct MemmapEntry MemmapEntry;

typedef struct TC39XBSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    TriCoreCPU cpu;
    TriCoreIRState ir;
    TriCoreSTMState stm;
    TriCoreASCLINState asclin;
    TriCoreSCUState scu;

    MemoryRegion dsprX;
    MemoryRegion psprX;

    TC39XBSoCCPUMemState cpu0mem;
    TC39XBSoCCPUMemState cpu1mem;
    TC39XBSoCCPUMemState cpu2mem;
    TC39XBSoCCPUMemState cpu3mem;
    TC39XBSoCCPUMemState cpu4mem;
    TC39XBSoCCPUMemState cpu5mem;

    TC39XBSoCFlashMemState flashmem;
} TC39XBSoCState;

typedef struct TC39XBSoCClass {
    DeviceClass parent_class;

    const char *name;
    const char *cpu_type;
    const MemmapEntry *memmap;
    uint32_t num_cpus;
} TC39XBSoCClass;

enum {
    TC39XB_DSPR5, TC39XB_DCACHE5, TC39XB_DTAG5,
    TC39XB_PSPR5, TC39XB_PCACHE5, TC39XB_PTAG5,
    TC39XB_DSPR4, TC39XB_DCACHE4, TC39XB_DTAG4,
    TC39XB_PSPR4, TC39XB_PCACHE4, TC39XB_PTAG4,
    TC39XB_DSPR3, TC39XB_DCACHE3, TC39XB_DTAG3,
    TC39XB_PSPR3, TC39XB_PCACHE3, TC39XB_PTAG3,
    TC39XB_DSPR2, TC39XB_DCACHE2, TC39XB_DTAG2,
    TC39XB_PSPR2, TC39XB_PCACHE2, TC39XB_PTAG2,
    TC39XB_DSPR1, TC39XB_DCACHE1, TC39XB_DTAG1,
    TC39XB_PSPR1, TC39XB_PCACHE1, TC39XB_PTAG1,
    TC39XB_DSPR0, TC39XB_DCACHE0, TC39XB_DTAG0,
    TC39XB_PSPR0, TC39XB_PCACHE0, TC39XB_PTAG0,
    TC39XB_PFLASH0_C, TC39XB_PFLASH1_C, TC39XB_PFLASH2_C,
    TC39XB_PFLASH3_C, TC39XB_PFLASH4_C, TC39XB_PFLASH5_C,
    TC39XB_OLDA_C, TC39XB_BROM_C,
    TC39XB_DLMU0_C, TC39XB_DLMU1_C, TC39XB_DLMU2_C,
    TC39XB_DLMU3_C, TC39XB_LMU0_C, TC39XB_LMU1_C,
    TC39XB_LMU2_C, TC39XB_DLMU4_C, TC39XB_DLMU5_C,
    TC39XB_EMEM,
    TC39XB_PFLASH0_U, TC39XB_PFLASH1_U, TC39XB_PFLASH2_U,
    TC39XB_PFLASH3_U, TC39XB_PFLASH4_U, TC39XB_PFLASH5_U,
    TC39XB_DFLASH0, TC39XB_DFLASH1,
    TC39XB_OLDA_U, TC39XB_BROM_U,
    TC39XB_DLMU0_U, TC39XB_DLMU1_U, TC39XB_DLMU2_U,
    TC39XB_DLMU3_U, TC39XB_LMU0_U, TC39XB_LMU1_U,
    TC39XB_LMU2_U, TC39XB_DLMU4_U, TC39XB_DLMU5_U,
    TC39XB_PSPRX, TC39XB_DSPRX,
    TC39XB_IR_INT, TC39XB_IR_SRC,
    TC39XB_STM0, TC39XB_ASCLIN0, TC39XB_SCU,
};

#endif
