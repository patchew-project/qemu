/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2015 Imagination Technologies
 *
 */

#ifndef MIPS_CMGCR_H
#define MIPS_CMGCR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MIPS_GCR "mips-gcr"
OBJECT_DECLARE_SIMPLE_TYPE(MIPSGCRState, MIPS_GCR)

#define GCR_BASE_ADDR           0x1fbf8000ULL
#define GCR_ADDRSPACE_SZ        0x8000

/* Offsets to register blocks */
#define MIPS_GCB_OFS        0x0000 /* Global Control Block */
#define MIPS_CLCB_OFS       0x2000 /* Core Local Control Block */
#define MIPS_COCB_OFS       0x4000 /* Core Other Control Block */
#define MIPS_GDB_OFS        0x6000 /* Global Debug Block */

/* Global Control Block Register Map */
#define GCR_CONFIG_OFS      0x0000
#define GCR_BASE_OFS        0x0008
#define GCR_REV_OFS         0x0030
#define GCR_GIC_BASE_OFS    0x0080
#define GCR_CPC_BASE_OFS    0x0088
#define GCR_GIC_STATUS_OFS  0x00D0
#define GCR_CPC_STATUS_OFS  0x00F0
#define GCR_L2_CONFIG_OFS   0x0130
#define GCR_SYS_CONFIG2_OFS 0x0150
#define GCR_SCRATCH0_OFS    0x0280
#define GCR_SCRATCH1_OFS    0x0288
#define GCR_SEM_OFS         0x0640

/* Core Local and Core Other Block Register Map */
#define GCR_CL_COH_EN_OFS    0x0008 /* Core-Local */
#define GCR_CL_CONFIG_OFS    0x0010 /* Core-Local */
#define GCR_CL_REDIRECT_OFS  0x0018 /* VP-Local */
#define GCR_CL_RESETBASE_OFS 0x0020 /* VP-Local */
#define GCR_CL_ID_OFS        0x0028 /* Core-Local */
#define GCR_CL_SCRATCH_OFS   0x0060 /* VP-Local */

/* GCR_L2_CONFIG register fields */
#define GCR_L2_CONFIG_BYPASS_SHF    20
#define GCR_L2_CONFIG_BYPASS_MSK    ((0x1ULL) << GCR_L2_CONFIG_BYPASS_SHF)

/* GCR_SYS_CONFIG2 register fields */
#define GCR_SYS_CONFIG2_MAXVP_SHF    0
#define GCR_SYS_CONFIG2_MAXVP_MSK    ((0xFULL) << GCR_SYS_CONFIG2_MAXVP_SHF)

/* GCR_BASE register fields */
#define GCR_BASE_GCRBASE_MSK     0xffffffff8000ULL

/* GCR_GIC_BASE register fields */
#define GCR_GIC_BASE_GICEN_MSK   1
#define GCR_GIC_BASE_GICBASE_MSK 0xFFFFFFFE0000ULL
#define GCR_GIC_BASE_MSK (GCR_GIC_BASE_GICEN_MSK | GCR_GIC_BASE_GICBASE_MSK)

/* GCR_SEM register fields */
#define GCR_SEM_DATA_MSK  0x00000000EFFFFFFFULL
#define GCR_SEM_LOCK_MASK 0x0000000080000000ULL

/* GCR_CPC_BASE register fields */
#define GCR_CPC_BASE_CPCEN_MSK   1
#define GCR_CPC_BASE_CPCBASE_MSK 0xFFFFFFFF8000ULL
#define GCR_CPC_BASE_MSK (GCR_CPC_BASE_CPCEN_MSK | GCR_CPC_BASE_CPCBASE_MSK)

/* GCR_CL_REDIRECT_OFS register fields */
#define GCR_CL_REDIRECT_VP_MSK 0x7U
#define GCR_CL_REDIRECT_VP_SHF 0
#define GCR_CL_REDIRECT_CORE_MSK 0xF00U
#define GCR_CL_REDIRECT_CORE_SHF 8

/* GCR_CL_RESETBASE_OFS register fields */
#define GCR_CL_RESET_BASE_RESETBASE_MSK 0xFFFFF000U
#define GCR_CL_RESET_BASE_MSK GCR_CL_RESET_BASE_RESETBASE_MSK

typedef struct MIPSGCRVPState MIPSGCRVPState;
struct MIPSGCRVPState {
    uint32_t redirect;
    uint64_t reset_base;
    uint64_t scratch;
};

typedef struct MIPSGCRPCoreState MIPSGCRPCoreState;
struct MIPSGCRPCoreState {
    int32_t num_vps; /* Number of VPs in that core */
    uint32_t coh_en;
    /* VP Local/Other Registers */
    MIPSGCRVPState *vps;
};

struct MIPSGCRState {
    SysBusDevice parent_obj;

    int32_t gcr_rev;
    int32_t num_pcores; /* Number of physical cores */
    int32_t num_vps; /* Number of VPs per physical core */

    uint64_t scratch[2]; /* GCR Scratch */
    uint32_t sem; /* GCR Semaphore */
    hwaddr gcr_base;
    MemoryRegion iomem;
    MemoryRegion *cpc_mr;
    MemoryRegion *gic_mr;

    uint64_t cpc_base;
    uint64_t gic_base;

    /* Core Local/Other Registers */
    MIPSGCRPCoreState *pcs;
};

static inline int mips_gcr_get_current_corenum(MIPSGCRState *s)
{
    return current_cpu->cpu_index / s->num_vps;
}

static inline int mips_gcr_get_current_vpid(MIPSGCRState *s)
{
    return current_cpu->cpu_index % s->num_vps;
}

static inline int mips_gcr_get_redirect_corenum(MIPSGCRState *s)
{
    int current_corenum = mips_gcr_get_current_corenum(s);
    int current_vpid = mips_gcr_get_current_vpid(s);
    MIPSGCRVPState *current_vps = &s->pcs[current_corenum].vps[current_vpid];

    return (current_vps->redirect & GCR_CL_REDIRECT_CORE_MSK) >>
            GCR_CL_REDIRECT_CORE_SHF;
}

static inline int mips_gcr_get_redirect_vpid(MIPSGCRState *s)
{
    int current_corenum = mips_gcr_get_current_corenum(s);
    int current_vpid = mips_gcr_get_current_vpid(s);
    MIPSGCRVPState *current_vps = &s->pcs[current_corenum].vps[current_vpid];

    return (current_vps->redirect & GCR_CL_REDIRECT_VP_MSK) >>
            GCR_CL_REDIRECT_VP_SHF;
}

static inline int mips_gcr_get_redirect_vpnum(MIPSGCRState *s)
{
    int core = mips_gcr_get_redirect_corenum(s);
    int vpid = mips_gcr_get_redirect_vpid(s);
    return core * s->num_vps + vpid;
}

#endif /* MIPS_CMGCR_H */
