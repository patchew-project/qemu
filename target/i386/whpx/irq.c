/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/apic-msidef.h"
#include "hw/pci/msi.h"
#include "system/hw_accel.h"
#include "system/whpx.h"
#include "system/whpx-internal.h"
#include "irq.h"
#include "system/whpx-accel-ops.h"
#include "system/whpx-all.h"
#include "system/whpx-common.h"
#include "qemu/memalign.h"
#include "qemu/main-loop.h"


/* Structure definition from Hyper-V, to keep unaltered */
typedef struct _HV_X64_INTERRUPT_CONTROLLER_STATE
{
    UINT32 ApicId;
    UINT32 ApicVersion;
    UINT32 ApicLdr;
    UINT32 ApicDfr;
    UINT32 ApicSpurious;
    UINT32 ApicIsr[8];
    UINT32 ApicTmr[8];
    UINT32 ApicIrr[8];
    UINT32 ApicEsr;
    UINT32 ApicIcrHigh;
    UINT32 ApicIcrLow;
    UINT32 ApicLvtTimer;
    UINT32 ApicLvtThermal;
    UINT32 ApicLvtPerfmon;
    UINT32 ApicLvtLint0;
    UINT32 ApicLvtLint1;
    UINT32 ApicLvtError;
    UINT32 ApicLvtCmci;
    UINT32 ApicErrorStatus;
    UINT32 ApicInitialCount;
    UINT32 ApicCounterValue;
    UINT32 ApicDivideConfiguration;
    UINT32 ApicRemoteRead;

} HV_X64_INTERRUPT_CONTROLLER_STATE, *PHV_X64_INTERRUPT_CONTROLLER_STATE;

int whpx_request_interrupt(uint32_t interrupt_type, uint32_t vector,
                           uint32_t vp_index, bool logical_dest_mode,
                           bool level_triggered)
{
    HRESULT hr;

    if (vector == 0) {
        warn_report("Ignoring request for interrupt vector 0");
        return 0;
    }

    WHV_INTERRUPT_CONTROL interrupt = {
        .Type = interrupt_type,
        .DestinationMode = logical_dest_mode ?
            WHvX64InterruptDestinationModeLogical :
            WHvX64InterruptDestinationModePhysical,

        .TriggerMode = level_triggered ?
            WHvX64InterruptTriggerModeLevel : WHvX64InterruptTriggerModeEdge,
        .Reserved = 0,
        .Vector = vector,
        .Destination = vp_index,
    };

    hr = whp_dispatch.WHvRequestInterrupt(whpx_global.partition,
                     &interrupt, sizeof(interrupt));
    if (FAILED(hr)) {
        error_report("Failed to request interrupt");
        return -errno;
    }
    return 0;
}

static uint32_t set_apic_delivery_mode(uint32_t reg, uint32_t mode)
{
    return ((reg) & ~0x700) | ((mode) << 8);
}

static int get_lapic(CPUState* cpu, HV_X64_INTERRUPT_CONTROLLER_STATE* state)
{
    HRESULT hr;
    UINT32 BytesWritten;

    size_t size = 4096;
    /* buffer aligned to 4k, as *state requires that */
    void *buffer = qemu_memalign(size, size);

    hr = whp_dispatch.WHvGetVirtualProcessorState(
            whpx_global.partition,
            cpu->cpu_index,
            WHvVirtualProcessorStateTypeInterruptControllerState2,
            buffer,
            size, &BytesWritten);
    
    if (!FAILED(hr)) {
        memcpy(state, buffer, sizeof(*state));
    } else {
        error_report("Failed to get LAPIC");
        return -1;
    }
    return 0;
}

static int set_lapic(CPUState* cpu, HV_X64_INTERRUPT_CONTROLLER_STATE* state)
{
    HRESULT hr;

    size_t size = 4096;
    /* buffer aligned to 4k, as *state requires that */
    void *buffer = qemu_memalign(size, size);
    memcpy(buffer, state, sizeof(*state));

    hr = whp_dispatch.WHvSetVirtualProcessorState(
            whpx_global.partition,
            cpu->cpu_index,
            WHvVirtualProcessorStateTypeInterruptControllerState2,
            state,
            sizeof(HV_X64_INTERRUPT_CONTROLLER_STATE));
    if (FAILED(hr)) {
        error_report("Failed to set LAPIC");
        return -1;
    }
    return 0;
}

int whpx_set_lint(CPUState* cpu)
{
    int ret;
    uint32_t *lvt_lint0, *lvt_lint1;

    HV_X64_INTERRUPT_CONTROLLER_STATE lapic_state = { 0 };
    ret = get_lapic(cpu, &lapic_state);
    if (ret < 0) {
        return ret;
    }

    lvt_lint0 = &lapic_state.ApicLvtLint0;
    *lvt_lint0 = set_apic_delivery_mode(*lvt_lint0, APIC_DM_EXTINT);

    lvt_lint1 = &lapic_state.ApicLvtLint1;
    *lvt_lint1 = set_apic_delivery_mode(*lvt_lint1, APIC_DM_NMI);

    /* TODO: should we skip setting lapic if the values are the same? */

    return set_lapic(cpu, &lapic_state);
}
