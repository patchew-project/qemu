/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Windows Hypervisor Platform accelerator (WHPX)
 *
 * Copyright (c) 2025 Mohamed Mediouni
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "system/address-spaces.h"
#include "system/ioport.h"
#include "gdbstub/helpers.h"
#include "qemu/accel.h"
#include "accel/accel-ops.h"
#include "system/whpx.h"
#include "system/cpus.h"
#include "system/runstate.h"
#include "qemu/main-loop.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-common.h"
#include "qapi/qapi-visit-common.h"
#include "migration/blocker.h"
#include "accel/accel-cpu-target.h"
#include <winerror.h>

#include "syndrome.h"
#include "cpu.h"
#include "cpregs.h"
#include "internals.h"

#include "system/whpx-internal.h"
#include "system/whpx-accel-ops.h"
#include "system/whpx-all.h"
#include "system/whpx-common.h"
#include "whpx_arm.h"
#include "hw/arm/bsa.h"
#include "arm-powerctl.h"

#include <winhvplatform.h>
#include <winhvplatformdefs.h>

struct whpx_reg_match {
    WHV_REGISTER_NAME reg;
    uint64_t offset;
};

static const struct whpx_reg_match whpx_reg_match[] = {
    { WHvArm64RegisterX0,   offsetof(CPUARMState, xregs[0]) },
    { WHvArm64RegisterX1,   offsetof(CPUARMState, xregs[1]) },
    { WHvArm64RegisterX2,   offsetof(CPUARMState, xregs[2]) },
    { WHvArm64RegisterX3,   offsetof(CPUARMState, xregs[3]) },
    { WHvArm64RegisterX4,   offsetof(CPUARMState, xregs[4]) },
    { WHvArm64RegisterX5,   offsetof(CPUARMState, xregs[5]) },
    { WHvArm64RegisterX6,   offsetof(CPUARMState, xregs[6]) },
    { WHvArm64RegisterX7,   offsetof(CPUARMState, xregs[7]) },
    { WHvArm64RegisterX8,   offsetof(CPUARMState, xregs[8]) },
    { WHvArm64RegisterX9,   offsetof(CPUARMState, xregs[9]) },
    { WHvArm64RegisterX10,  offsetof(CPUARMState, xregs[10]) },
    { WHvArm64RegisterX11,  offsetof(CPUARMState, xregs[11]) },
    { WHvArm64RegisterX12,  offsetof(CPUARMState, xregs[12]) },
    { WHvArm64RegisterX13,  offsetof(CPUARMState, xregs[13]) },
    { WHvArm64RegisterX14,  offsetof(CPUARMState, xregs[14]) },
    { WHvArm64RegisterX15,  offsetof(CPUARMState, xregs[15]) },
    { WHvArm64RegisterX16,  offsetof(CPUARMState, xregs[16]) },
    { WHvArm64RegisterX17,  offsetof(CPUARMState, xregs[17]) },
    { WHvArm64RegisterX18,  offsetof(CPUARMState, xregs[18]) },
    { WHvArm64RegisterX19,  offsetof(CPUARMState, xregs[19]) },
    { WHvArm64RegisterX20,  offsetof(CPUARMState, xregs[20]) },
    { WHvArm64RegisterX21,  offsetof(CPUARMState, xregs[21]) },
    { WHvArm64RegisterX22,  offsetof(CPUARMState, xregs[22]) },
    { WHvArm64RegisterX23,  offsetof(CPUARMState, xregs[23]) },
    { WHvArm64RegisterX24,  offsetof(CPUARMState, xregs[24]) },
    { WHvArm64RegisterX25,  offsetof(CPUARMState, xregs[25]) },
    { WHvArm64RegisterX26,  offsetof(CPUARMState, xregs[26]) },
    { WHvArm64RegisterX27,  offsetof(CPUARMState, xregs[27]) },
    { WHvArm64RegisterX28,  offsetof(CPUARMState, xregs[28]) },
    { WHvArm64RegisterFp,  offsetof(CPUARMState, xregs[29]) },
    { WHvArm64RegisterLr,  offsetof(CPUARMState, xregs[30]) },
    { WHvArm64RegisterPc,   offsetof(CPUARMState, pc) },
};

static const struct whpx_reg_match whpx_fpreg_match[] = {
    { WHvArm64RegisterQ0,  offsetof(CPUARMState, vfp.zregs[0]) },
    { WHvArm64RegisterQ1,  offsetof(CPUARMState, vfp.zregs[1]) },
    { WHvArm64RegisterQ2,  offsetof(CPUARMState, vfp.zregs[2]) },
    { WHvArm64RegisterQ3,  offsetof(CPUARMState, vfp.zregs[3]) },
    { WHvArm64RegisterQ4,  offsetof(CPUARMState, vfp.zregs[4]) },
    { WHvArm64RegisterQ5,  offsetof(CPUARMState, vfp.zregs[5]) },
    { WHvArm64RegisterQ6,  offsetof(CPUARMState, vfp.zregs[6]) },
    { WHvArm64RegisterQ7,  offsetof(CPUARMState, vfp.zregs[7]) },
    { WHvArm64RegisterQ8,  offsetof(CPUARMState, vfp.zregs[8]) },
    { WHvArm64RegisterQ9,  offsetof(CPUARMState, vfp.zregs[9]) },
    { WHvArm64RegisterQ10, offsetof(CPUARMState, vfp.zregs[10]) },
    { WHvArm64RegisterQ11, offsetof(CPUARMState, vfp.zregs[11]) },
    { WHvArm64RegisterQ12, offsetof(CPUARMState, vfp.zregs[12]) },
    { WHvArm64RegisterQ13, offsetof(CPUARMState, vfp.zregs[13]) },
    { WHvArm64RegisterQ14, offsetof(CPUARMState, vfp.zregs[14]) },
    { WHvArm64RegisterQ15, offsetof(CPUARMState, vfp.zregs[15]) },
    { WHvArm64RegisterQ16, offsetof(CPUARMState, vfp.zregs[16]) },
    { WHvArm64RegisterQ17, offsetof(CPUARMState, vfp.zregs[17]) },
    { WHvArm64RegisterQ18, offsetof(CPUARMState, vfp.zregs[18]) },
    { WHvArm64RegisterQ19, offsetof(CPUARMState, vfp.zregs[19]) },
    { WHvArm64RegisterQ20, offsetof(CPUARMState, vfp.zregs[20]) },
    { WHvArm64RegisterQ21, offsetof(CPUARMState, vfp.zregs[21]) },
    { WHvArm64RegisterQ22, offsetof(CPUARMState, vfp.zregs[22]) },
    { WHvArm64RegisterQ23, offsetof(CPUARMState, vfp.zregs[23]) },
    { WHvArm64RegisterQ24, offsetof(CPUARMState, vfp.zregs[24]) },
    { WHvArm64RegisterQ25, offsetof(CPUARMState, vfp.zregs[25]) },
    { WHvArm64RegisterQ26, offsetof(CPUARMState, vfp.zregs[26]) },
    { WHvArm64RegisterQ27, offsetof(CPUARMState, vfp.zregs[27]) },
    { WHvArm64RegisterQ28, offsetof(CPUARMState, vfp.zregs[28]) },
    { WHvArm64RegisterQ29, offsetof(CPUARMState, vfp.zregs[29]) },
    { WHvArm64RegisterQ30, offsetof(CPUARMState, vfp.zregs[30]) },
    { WHvArm64RegisterQ31, offsetof(CPUARMState, vfp.zregs[31]) },
};

#define WHPX_SYSREG(crn, crm, op0, op1, op2) \
        ENCODE_AA64_CP_REG(CP_REG_ARM64_SYSREG_CP, crn, crm, op0, op1, op2)

struct whpx_sreg_match {
    WHV_REGISTER_NAME reg;
    uint32_t key;
    bool global;
    uint32_t cp_idx;
};

static struct whpx_sreg_match whpx_sreg_match[] = {
/* Do not currently deal with the debug registers: leave them here for experimentation
    { WHvArm64RegisterDbgbvr0El1, WHPX_SYSREG(0, 0, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr0El1, WHPX_SYSREG(0, 0, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr0El1, WHPX_SYSREG(0, 0, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr0El1, WHPX_SYSREG(0, 0, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr0El1, WHPX_SYSREG(0, 1, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr0El1, WHPX_SYSREG(0, 1, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr0El1, WHPX_SYSREG(0, 1, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr0El1, WHPX_SYSREG(0, 1, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr2El1, WHPX_SYSREG(0, 2, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr2El1, WHPX_SYSREG(0, 2, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr2El1, WHPX_SYSREG(0, 2, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr2El1, WHPX_SYSREG(0, 2, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr3El1, WHPX_SYSREG(0, 3, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr3El1, WHPX_SYSREG(0, 3, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr3El1, WHPX_SYSREG(0, 3, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr3El1, WHPX_SYSREG(0, 3, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr4El1, WHPX_SYSREG(0, 4, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr4El1, WHPX_SYSREG(0, 4, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr4El1, WHPX_SYSREG(0, 4, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr4El1, WHPX_SYSREG(0, 4, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr5El1, WHPX_SYSREG(0, 5, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr5El1, WHPX_SYSREG(0, 5, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr5El1, WHPX_SYSREG(0, 5, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr5El1, WHPX_SYSREG(0, 5, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr6El1, WHPX_SYSREG(0, 6, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr6El1, WHPX_SYSREG(0, 6, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr6El1, WHPX_SYSREG(0, 6, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr6El1, WHPX_SYSREG(0, 6, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr7El1, WHPX_SYSREG(0, 7, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr7El1, WHPX_SYSREG(0, 7, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr7El1, WHPX_SYSREG(0, 7, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr7El1, WHPX_SYSREG(0, 7, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr8El1, WHPX_SYSREG(0, 8, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr8El1, WHPX_SYSREG(0, 8, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr8El1, WHPX_SYSREG(0, 8, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr8El1, WHPX_SYSREG(0, 8, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr9El1, WHPX_SYSREG(0, 9, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr9El1, WHPX_SYSREG(0, 9, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr9El1, WHPX_SYSREG(0, 9, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr9El1, WHPX_SYSREG(0, 9, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr10El1, WHPX_SYSREG(0, 10, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr10El1, WHPX_SYSREG(0, 10, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr10El1, WHPX_SYSREG(0, 10, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr10El1, WHPX_SYSREG(0, 10, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr11El1, WHPX_SYSREG(0, 11, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr11El1, WHPX_SYSREG(0, 11, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr11El1, WHPX_SYSREG(0, 11, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr11El1, WHPX_SYSREG(0, 11, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr12El1, WHPX_SYSREG(0, 12, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr12El1, WHPX_SYSREG(0, 12, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr12El1, WHPX_SYSREG(0, 12, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr12El1, WHPX_SYSREG(0, 12, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr13El1, WHPX_SYSREG(0, 13, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr13El1, WHPX_SYSREG(0, 13, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr13El1, WHPX_SYSREG(0, 13, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr13El1, WHPX_SYSREG(0, 13, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr14El1, WHPX_SYSREG(0, 14, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr14El1, WHPX_SYSREG(0, 14, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr14El1, WHPX_SYSREG(0, 14, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr14El1, WHPX_SYSREG(0, 14, 2, 0, 7) },

    { WHvArm64RegisterDbgbvr15El1, WHPX_SYSREG(0, 15, 2, 0, 4) },
    { WHvArm64RegisterDbgbcr15El1, WHPX_SYSREG(0, 15, 2, 0, 5) },
    { WHvArm64RegisterDbgwvr15El1, WHPX_SYSREG(0, 15, 2, 0, 6) },
    { WHvArm64RegisterDbgwcr15El1, WHPX_SYSREG(0, 15, 2, 0, 7) },
*/
#ifdef SYNC_NO_RAW_REGS
    /*
     * The registers below are manually synced on init because they are
     * marked as NO_RAW. We still list them to make number space sync easier.
     */
    { WHvArm64RegisterMidrEl1, WHPX_SYSREG(0, 0, 3, 0, 0) },
    { WHvArm64RegisterMpidrEl1, WHPX_SYSREG(0, 0, 3, 0, 5) },
    { WHvArm64RegisterIdPfr0El1, WHPX_SYSREG(0, 4, 3, 0, 0) },
#endif
    { WHvArm64RegisterIdPfr1El1, WHPX_SYSREG(0, 4, 3, 0, 1), true },
    { WHvArm64RegisterIdDfr0El1, WHPX_SYSREG(0, 5, 3, 0, 0), true },
    { WHvArm64RegisterIdAa64Dfr1El1, WHPX_SYSREG(0, 5, 3, 0, 1), true },
    { WHvArm64RegisterIdAa64Isar0El1, WHPX_SYSREG(0, 6, 3, 0, 0), true },
    { WHvArm64RegisterIdAa64Isar1El1, WHPX_SYSREG(0, 6, 3, 0, 1), true },
#ifdef SYNC_NO_MMFR0
    /* We keep the hardware MMFR0 around. HW limits are there anyway */
    { WHvArm64RegisterIdAa64Mmfr0El1, WHPX_SYSREG(0, 7, 3, 0, 0) },
#endif
    { WHvArm64RegisterIdAa64Mmfr1El1, WHPX_SYSREG(0, 7, 3, 0, 1), true },
    { WHvArm64RegisterIdAa64Mmfr2El1, WHPX_SYSREG(0, 7, 3, 0, 2), true },
    { WHvArm64RegisterIdAa64Mmfr3El1, WHPX_SYSREG(0, 7, 3, 0, 3), true },

    { WHvArm64RegisterMdscrEl1, WHPX_SYSREG(0, 2, 2, 0, 2) },
    { WHvArm64RegisterSctlrEl1, WHPX_SYSREG(1, 0, 3, 0, 0) },
    { WHvArm64RegisterCpacrEl1, WHPX_SYSREG(1, 0, 3, 0, 2) },
    { WHvArm64RegisterTtbr0El1, WHPX_SYSREG(2, 0, 3, 0, 0) },
    { WHvArm64RegisterTtbr1El1, WHPX_SYSREG(2, 0, 3, 0, 1) },
    { WHvArm64RegisterTcrEl1, WHPX_SYSREG(2, 0, 3, 0, 2) },

    { WHvArm64RegisterApiAKeyLoEl1, WHPX_SYSREG(2, 1, 3, 0, 0) },
    { WHvArm64RegisterApiAKeyHiEl1, WHPX_SYSREG(2, 1, 3, 0, 1) },
    { WHvArm64RegisterApiBKeyLoEl1, WHPX_SYSREG(2, 1, 3, 0, 2) },
    { WHvArm64RegisterApiBKeyHiEl1, WHPX_SYSREG(2, 1, 3, 0, 3) },
    { WHvArm64RegisterApdAKeyLoEl1, WHPX_SYSREG(2, 2, 3, 0, 0) },
    { WHvArm64RegisterApdAKeyHiEl1, WHPX_SYSREG(2, 2, 3, 0, 1) },
    { WHvArm64RegisterApdBKeyLoEl1, WHPX_SYSREG(2, 2, 3, 0, 2) },
    { WHvArm64RegisterApdBKeyHiEl1, WHPX_SYSREG(2, 2, 3, 0, 3) },
    { WHvArm64RegisterApgAKeyLoEl1, WHPX_SYSREG(2, 3, 3, 0, 0) },
    { WHvArm64RegisterApgAKeyHiEl1, WHPX_SYSREG(2, 3, 3, 0, 1) },

    { WHvArm64RegisterSpsrEl1, WHPX_SYSREG(4, 0, 3, 0, 0) },
    { WHvArm64RegisterElrEl1, WHPX_SYSREG(4, 0, 3, 0, 1) },
    { WHvArm64RegisterSpEl1, WHPX_SYSREG(4, 1, 3, 0, 0) },
    { WHvArm64RegisterEsrEl1, WHPX_SYSREG(5, 2, 3, 0, 0) },
    { WHvArm64RegisterFarEl1, WHPX_SYSREG(6, 0, 3, 0, 0) },
    { WHvArm64RegisterParEl1, WHPX_SYSREG(7, 4, 3, 0, 0) },
    { WHvArm64RegisterMairEl1, WHPX_SYSREG(10, 2, 3, 0, 0) },
    { WHvArm64RegisterVbarEl1, WHPX_SYSREG(12, 0, 3, 0, 0) },
    { WHvArm64RegisterContextidrEl1, WHPX_SYSREG(13, 0, 3, 0, 1) },
    { WHvArm64RegisterTpidrEl1, WHPX_SYSREG(13, 0, 3, 0, 4) },
    { WHvArm64RegisterCntkctlEl1, WHPX_SYSREG(14, 1, 3, 0, 0) },
    { WHvArm64RegisterCsselrEl1, WHPX_SYSREG(0, 0, 3, 2, 0) },
    { WHvArm64RegisterTpidrEl0, WHPX_SYSREG(13, 0, 3, 3, 2) },
    { WHvArm64RegisterTpidrroEl0, WHPX_SYSREG(13, 0, 3, 3, 3) },
    { WHvArm64RegisterCntvCtlEl0, WHPX_SYSREG(14, 3, 3, 3, 1) },
    { WHvArm64RegisterCntvCvalEl0, WHPX_SYSREG(14, 3, 3, 3, 2) },
    { WHvArm64RegisterSpEl1, WHPX_SYSREG(4, 1, 3, 4, 0) },
};

static void flush_cpu_state(CPUState *cpu)
{
    if (cpu->vcpu_dirty) {
        whpx_set_registers(cpu, WHPX_SET_RUNTIME_STATE);
        cpu->vcpu_dirty = false;
    }
}

static void whpx_get_reg(CPUState *cpu, WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE* val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;

    flush_cpu_state(cpu);

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(whpx->partition, cpu->cpu_index,
         &reg, 1, val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to get register %08x, hr=%08lx", reg, hr);
    }
}

static void whpx_set_reg(CPUState *cpu, WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;
    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(whpx->partition, cpu->cpu_index,
         &reg, 1, &val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set register %08x, hr=%08lx", reg, hr);
    }
}

static void whpx_get_global_reg(WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE *val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;

    hr = whp_dispatch.WHvGetVirtualProcessorRegisters(whpx->partition, WHV_ANY_VP,
         &reg, 1, val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to get register %08x, hr=%08lx", reg, hr);
    }
}

static void whpx_set_global_reg(WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;
    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(whpx->partition, WHV_ANY_VP,
         &reg, 1, &val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set register %08x, hr=%08lx", reg, hr);
    }
}

static uint64_t whpx_get_gp_reg(CPUState *cpu, int rt)
{
    if (rt >= 31) {
        return 0;
    }
    WHV_REGISTER_NAME reg = WHvArm64RegisterX0 + rt;
    WHV_REGISTER_VALUE val;
    whpx_get_reg(cpu, reg, &val);

    return val.Reg64;
}

static void whpx_set_gp_reg(CPUState *cpu, int rt, uint64_t val)
{
    if (rt >= 31) {
        abort();
    }
    WHV_REGISTER_NAME reg = WHvArm64RegisterX0 + rt;
    WHV_REGISTER_VALUE reg_val = {.Reg64 = val};

    whpx_set_reg(cpu, reg, reg_val);
}

static int whpx_handle_mmio(CPUState *cpu, WHV_MEMORY_ACCESS_CONTEXT *ctx)
{
    uint64_t syndrome = ctx->Syndrome;

    bool isv = syndrome & ARM_EL_ISV;
    bool iswrite = (syndrome >> 6) & 1;
    bool sse = (syndrome >> 21) & 1;
    uint32_t sas = (syndrome >> 22) & 3;
    uint32_t len = 1 << sas;
    uint32_t srt = (syndrome >> 16) & 0x1f;
    uint32_t cm = (syndrome >> 8) & 0x1;
    uint64_t val = 0;

    if (cm) {
        /* We don't cache MMIO regions */
        abort();
        return 0;
    }

    assert(isv);

    if (iswrite) {
        val = whpx_get_gp_reg(cpu, srt);
        address_space_write(&address_space_memory,
                            ctx->Gpa,
                            MEMTXATTRS_UNSPECIFIED, &val, len);
    } else {
        address_space_read(&address_space_memory,
                           ctx->Gpa,
                           MEMTXATTRS_UNSPECIFIED, &val, len);
        if (sse) {
            val = sextract64(val, 0, len * 8);
        }
        whpx_set_gp_reg(cpu, srt, val);
    }

    return 0;
}

static void whpx_psci_cpu_off(ARMCPU *arm_cpu)
{
    int32_t ret = arm_set_cpu_off(arm_cpu_mp_affinity(arm_cpu));
    assert(ret == QEMU_ARM_POWERCTL_RET_SUCCESS);
}

int whpx_vcpu_run(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    AccelCPUState *vcpu = cpu->accel;
    int ret;


    g_assert(bql_locked());

    if (whpx->running_cpus++ == 0) {
        ret = whpx_first_vcpu_starting(cpu);
        if (ret != 0) {
            return ret;
        }
    }

    bql_unlock();


    cpu_exec_start(cpu);
    do {
        bool advance_pc = false;
        if (cpu->vcpu_dirty) {
            whpx_set_registers(cpu, WHPX_SET_RUNTIME_STATE);
            cpu->vcpu_dirty = false;
        }

        if (qatomic_read(&cpu->exit_request)) {
            whpx_vcpu_kick(cpu);
        }

        hr = whp_dispatch.WHvRunVirtualProcessor(
            whpx->partition, cpu->cpu_index,
            &vcpu->exit_ctx, sizeof(vcpu->exit_ctx));

        if (FAILED(hr)) {
            error_report("WHPX: Failed to exec a virtual processor,"
                         " hr=%08lx", hr);
            ret = -1;
            break;
        }

        switch (vcpu->exit_ctx.ExitReason) {
        case WHvRunVpExitReasonGpaIntercept:
        case WHvRunVpExitReasonUnmappedGpa:
            advance_pc = true;

            if (vcpu->exit_ctx.MemoryAccess.Syndrome >> 8 & 0x1) {
                error_report("WHPX: cached access to unmapped memory"
                "Pc = 0x%llx Gva = 0x%llx Gpa = 0x%llx",
                vcpu->exit_ctx.MemoryAccess.Header.Pc,
                vcpu->exit_ctx.MemoryAccess.Gpa,
                vcpu->exit_ctx.MemoryAccess.Gva);
                break;
            }

            ret = whpx_handle_mmio(cpu, &vcpu->exit_ctx.MemoryAccess);
            break;
        case WHvRunVpExitReasonCanceled:
            cpu->exception_index = EXCP_INTERRUPT;
            ret = 1;
            break;
        case WHvRunVpExitReasonArm64Reset:
            if (vcpu->exit_ctx.Arm64Reset.ResetType == WHvArm64ResetTypePowerOff) {
                qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            } else if (vcpu->exit_ctx.Arm64Reset.ResetType == WHvArm64ResetTypeReboot) {
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            } else {
                abort();
            }
            bql_lock();
            whpx_psci_cpu_off(arm_cpu);
            bql_unlock();
            break;
        case WHvRunVpExitReasonNone:
        case WHvRunVpExitReasonUnrecoverableException:
        case WHvRunVpExitReasonInvalidVpRegisterValue:
        case WHvRunVpExitReasonUnsupportedFeature:
        default:
            error_report("WHPX: Unexpected VP exit code 0x%08x",
                         vcpu->exit_ctx.ExitReason);
            whpx_get_registers(cpu);
            bql_lock();
            qemu_system_guest_panicked(cpu_get_crash_info(cpu));
            bql_unlock();
            break;
        }
        if (advance_pc) {
            WHV_REGISTER_VALUE pc;

            flush_cpu_state(cpu);
            pc.Reg64 = vcpu->exit_ctx.MemoryAccess.Header.Pc + 4;
            whpx_set_reg(cpu, WHvArm64RegisterPc, pc);
        }
    } while (!ret);

    cpu_exec_end(cpu);

    bql_lock();
    current_cpu = cpu;

    if (--whpx->running_cpus == 0) {
        whpx_last_vcpu_stopping(cpu);
    }

    qatomic_set(&cpu->exit_request, false);

    return ret < 0;
}

static void clean_whv_register_value(WHV_REGISTER_VALUE *val)
{
    memset(val, 0, sizeof(WHV_REGISTER_VALUE));
}

void whpx_get_registers(CPUState *cpu)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    WHV_REGISTER_VALUE val;
    int i;

    for (i = 0; i < ARRAY_SIZE(whpx_reg_match); i++) {
        whpx_get_reg(cpu, whpx_reg_match[i].reg, &val);
        *(uint64_t *)((void *)env + whpx_reg_match[i].offset) = val.Reg64;
    }

    for (i = 0; i < ARRAY_SIZE(whpx_fpreg_match); i++) {
        whpx_get_reg(cpu, whpx_reg_match[i].reg, &val);
        memcpy((void *)env + whpx_fpreg_match[i].offset, &val, sizeof(val.Reg128));
    }

    whpx_get_reg(cpu, WHvArm64RegisterPc, &val);
    env->pc = val.Reg64;

    whpx_get_reg(cpu, WHvArm64RegisterFpcr, &val);
    vfp_set_fpcr(env, val.Reg32);

    whpx_get_reg(cpu, WHvArm64RegisterFpsr, &val);
    vfp_set_fpsr(env, val.Reg32);

    whpx_get_reg(cpu, WHvArm64RegisterPstate, &val);
    pstate_write(env, val.Reg32);

    for (i = 0; i < ARRAY_SIZE(whpx_sreg_match); i++) {
        if (whpx_sreg_match[i].global == true) {
            continue;
        }
        if (whpx_sreg_match[i].cp_idx == -1) {
            continue;
        }

        whpx_get_reg(cpu, whpx_sreg_match[i].reg, &val);

        arm_cpu->cpreg_values[whpx_sreg_match[i].cp_idx] = val.Reg64;
    }

    /* WHP disallows us from reading global regs as a vCPU */
    for (i = 0; i < ARRAY_SIZE(whpx_sreg_match); i++) {
        if (whpx_sreg_match[i].global == false) {
            continue;
        }
        if (whpx_sreg_match[i].cp_idx == -1) {
            continue;
        }

        whpx_get_global_reg(whpx_sreg_match[i].reg, &val);

        arm_cpu->cpreg_values[whpx_sreg_match[i].cp_idx] = val.Reg64;
    }
    assert(write_list_to_cpustate(arm_cpu));

    aarch64_restore_sp(env, arm_current_el(env));
}

void whpx_set_registers(CPUState *cpu, int level)
{
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    WHV_REGISTER_VALUE val;
    clean_whv_register_value(&val);
    int i;

    assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));

    for (i = 0; i < ARRAY_SIZE(whpx_reg_match); i++) {
        val.Reg64 = *(uint64_t *)((void *)env + whpx_reg_match[i].offset);
        whpx_set_reg(cpu, whpx_reg_match[i].reg, val);
    }

    for (i = 0; i < ARRAY_SIZE(whpx_fpreg_match); i++) {
        memcpy(&val.Reg128, (void *)env + whpx_fpreg_match[i].offset, sizeof(val.Reg128));
        whpx_set_reg(cpu, whpx_reg_match[i].reg, val);
    }

    clean_whv_register_value(&val);
    val.Reg64 = env->pc;
    whpx_set_reg(cpu, WHvArm64RegisterPc, val);

    clean_whv_register_value(&val);
    val.Reg32 = vfp_get_fpcr(env);
    whpx_set_reg(cpu, WHvArm64RegisterFpcr, val);
    val.Reg32 = vfp_get_fpsr(env);
    whpx_set_reg(cpu, WHvArm64RegisterFpsr, val);
    val.Reg32 = pstate_read(env);
    whpx_set_reg(cpu, WHvArm64RegisterPstate, val);

    aarch64_save_sp(env, arm_current_el(env));

    assert(write_cpustate_to_list(arm_cpu, false));
    for (i = 0; i < ARRAY_SIZE(whpx_sreg_match); i++) {
        if (whpx_sreg_match[i].global == true) {
            continue;
        }

        if (whpx_sreg_match[i].cp_idx == -1) {
            continue;
        }
        clean_whv_register_value(&val);
        val.Reg64 = arm_cpu->cpreg_values[whpx_sreg_match[i].cp_idx];
        whpx_set_reg(cpu, whpx_sreg_match[i].reg, val);
    }

    /* Currently set global regs every time. */
    for (i = 0; i < ARRAY_SIZE(whpx_sreg_match); i++) {
        if (whpx_sreg_match[i].global == false) {
            continue;
        }

        if (whpx_sreg_match[i].cp_idx == -1) {
            continue;
        }
        clean_whv_register_value(&val);
        val.Reg64 = arm_cpu->cpreg_values[whpx_sreg_match[i].cp_idx];
        whpx_set_global_reg(whpx_sreg_match[i].reg, val);
    }
}

static uint32_t max_vcpu_index;

static void whpx_cpu_update_state(void *opaque, bool running, RunState state)
{
}

uint32_t whpx_arm_get_ipa_bit_size(void)
{
    WHV_CAPABILITY whpx_cap;
    UINT32 whpx_cap_size;
    HRESULT hr;
    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodePhysicalAddressWidth, &whpx_cap,
        sizeof(whpx_cap), &whpx_cap_size);
    if (FAILED(hr)) {
        error_report("WHPX: failed to get supported"
             "physical address width, hr=%08lx", hr);
    }

    /*
     * We clamp any IPA size we want to back the VM with to a valid PARange
     * value so the guest doesn't try and map memory outside of the valid range.
     * This logic just clamps the passed in IPA bit size to the first valid
     * PARange value <= to it.
     */
    return round_down_to_parange_bit_size(whpx_cap.PhysicalAddressWidth);
}

static void clamp_id_aa64mmfr0_parange_to_ipa_size(ARMISARegisters *isar)
{
    uint32_t ipa_size = whpx_arm_get_ipa_bit_size();
    uint64_t id_aa64mmfr0;

    /* Clamp down the PARange to the IPA size the kernel supports. */
    uint8_t index = round_down_to_parange_index(ipa_size);
    id_aa64mmfr0 = GET_IDREG(isar, ID_AA64MMFR0);
    id_aa64mmfr0 = (id_aa64mmfr0 & ~R_ID_AA64MMFR0_PARANGE_MASK) | index;
    SET_IDREG(isar, ID_AA64MMFR0, id_aa64mmfr0);
}

int whpx_init_vcpu(CPUState *cpu)
{
    HRESULT hr;
    struct whpx_state *whpx = &whpx_global;
    AccelCPUState *vcpu = NULL;
    ARMCPU *arm_cpu = ARM_CPU(cpu);
    CPUARMState *env = &arm_cpu->env;
    int ret;

    uint32_t sregs_match_len = ARRAY_SIZE(whpx_sreg_match);
    uint32_t sregs_cnt = 0;
    WHV_REGISTER_VALUE val;
    int i;

    vcpu = g_new0(AccelCPUState, 1);

    hr = whp_dispatch.WHvCreateVirtualProcessor(
        whpx->partition, cpu->cpu_index, 0);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to create a virtual processor,"
                     " hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    /* Assumption that CNTFRQ_EL0 is the same between the VMM and the partition. */
    asm volatile("mrs %0, cntfrq_el0" : "=r"(arm_cpu->gt_cntfrq_hz));

    cpu->vcpu_dirty = true;
    cpu->accel = vcpu;
    max_vcpu_index = max(max_vcpu_index, cpu->cpu_index);
    qemu_add_vm_change_state_handler(whpx_cpu_update_state, env);

    env->aarch64 = true;

    /* Allocate enough space for our sysreg sync */
    arm_cpu->cpreg_indexes = g_renew(uint64_t, arm_cpu->cpreg_indexes,
                                     sregs_match_len);
    arm_cpu->cpreg_values = g_renew(uint64_t, arm_cpu->cpreg_values,
                                    sregs_match_len);
    arm_cpu->cpreg_vmstate_indexes = g_renew(uint64_t,
                                             arm_cpu->cpreg_vmstate_indexes,
                                             sregs_match_len);
    arm_cpu->cpreg_vmstate_values = g_renew(uint64_t,
                                            arm_cpu->cpreg_vmstate_values,
                                            sregs_match_len);

    memset(arm_cpu->cpreg_values, 0, sregs_match_len * sizeof(uint64_t));

    /* Populate cp list for all known sysregs */
    for (i = 0; i < sregs_match_len; i++) {
        const ARMCPRegInfo *ri;
        uint32_t key = whpx_sreg_match[i].key;

        ri = get_arm_cp_reginfo(arm_cpu->cp_regs, key);
        if (ri) {
            assert(!(ri->type & ARM_CP_NO_RAW));
            whpx_sreg_match[i].cp_idx = sregs_cnt;
            arm_cpu->cpreg_indexes[sregs_cnt++] = cpreg_to_kvm_id(key);
        } else {
            whpx_sreg_match[i].cp_idx = -1;
        }
    }
    arm_cpu->cpreg_array_len = sregs_cnt;
    arm_cpu->cpreg_vmstate_array_len = sregs_cnt;

    assert(write_cpustate_to_list(arm_cpu, false));

    /* Set CP_NO_RAW system registers on init */
    val.Reg64 = arm_cpu->midr;
    whpx_set_reg(cpu, WHvArm64RegisterMidrEl1,
                              val);

    clean_whv_register_value(&val);

    /* bit 31 of MPIDR_EL1 is RES1, and this is enforced by WHPX */
    val.Reg64 = 0x80000000 + arm_cpu->mp_affinity;
    whpx_set_reg(cpu, WHvArm64RegisterMpidrEl1,
                              val);

    clamp_id_aa64mmfr0_parange_to_ipa_size(&arm_cpu->isar);
    return 0;

error:
    g_free(vcpu);

    return ret;

}

void whpx_cpu_instance_init(CPUState *cs)
{
}

int whpx_accel_init(AccelState *as, MachineState *ms)
{
    struct whpx_state *whpx;
    int ret;
    HRESULT hr;
    WHV_CAPABILITY whpx_cap;
    UINT32 whpx_cap_size;
    WHV_PARTITION_PROPERTY prop;
    WHV_CAPABILITY_FEATURES features = {0};
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    int pa_range = 0;

    whpx = &whpx_global;
    /* on arm64 Windows Hypervisor Platform, vGICv3 always used */
    whpx->kernel_irqchip = true;

    if (!init_whp_dispatch()) {
        ret = -ENOSYS;
        goto error;
    }

    if (mc->whpx_get_physical_address_range) {
        pa_range = mc->whpx_get_physical_address_range(ms);
        if (pa_range < 0) {
            return -EINVAL;
        }
    }

    whpx->mem_quota = ms->ram_size;

    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeHypervisorPresent, &whpx_cap,
        sizeof(whpx_cap), &whpx_cap_size);
    if (FAILED(hr) || !whpx_cap.HypervisorPresent) {
        error_report("WHPX: No accelerator found, hr=%08lx", hr);
        ret = -ENOSPC;
        goto error;
    }

    hr = whp_dispatch.WHvGetCapability(
        WHvCapabilityCodeFeatures, &features, sizeof(features), NULL);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to query capabilities, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    if (!features.Arm64Support) {
        error_report("WHPX: host OS exposing pre-release WHPX implementation. "
            "Please update your operating system to at least build 26100.3915");
        ret = -EINVAL;
        goto error;
    }

    hr = whp_dispatch.WHvCreatePartition(&whpx->partition);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to create partition, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));
    prop.ProcessorCount = ms->smp.cpus;
    hr = whp_dispatch.WHvSetPartitionProperty(
        whpx->partition,
        WHvPartitionPropertyCodeProcessorCount,
        &prop,
        sizeof(WHV_PARTITION_PROPERTY));

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set partition processor count to %u,"
                     " hr=%08lx", prop.ProcessorCount, hr);
        ret = -EINVAL;
        goto error;
    }

    if (!whpx->kernel_irqchip_allowed) {
        error_report("WHPX: on Arm, only kernel-irqchip=on is currently supported");
        ret = -EINVAL;
        goto error;
    }

    memset(&prop, 0, sizeof(WHV_PARTITION_PROPERTY));

    hr = whp_dispatch.WHvSetupPartition(whpx->partition);
    if (FAILED(hr)) {
        error_report("WHPX: Failed to setup partition, hr=%08lx", hr);
        ret = -EINVAL;
        goto error;
    }

    whpx_memory_init();

    return 0;

error:

    if (NULL != whpx->partition) {
        whp_dispatch.WHvDeletePartition(whpx->partition);
        whpx->partition = NULL;
    }

    return ret;
}
