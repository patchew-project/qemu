/*
 * WHPX platform APIC support
 *
 * Copyright (c) 2011 Siemens AG
 *
 * Authors:
 *  Jan Kiszka          <jan.kiszka@siemens.com>
 *  John Starks         <jostarks@microsoft.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/apic-msidef.h"
#include "hw/pci/msi.h"
#include "system/hw_accel.h"
#include "system/whpx.h"
#include "system/whpx-internal.h"
#include "target/i386/whpx/whpx-i386.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "qemu/memalign.h"

typedef struct hv_local_interrupt_controller_state
               hv_local_interrupt_controller_state;

struct MshvAPICState {
    APICCommonState parent_obj;

    uint32_t apic_version;
    uint32_t apic_lvt_cmci;
    uint32_t apic_error_status;
    uint32_t apic_counter_value;
    uint32_t apic_remote_read;
};

#define TYPE_MSHV_APIC "whpx-apic"
OBJECT_DECLARE_SIMPLE_TYPE(MshvAPICState, MSHV_APIC)

struct whpx_lapic_state {
    struct {
        uint32_t data;
        uint32_t padding[3];
    } fields[256];
};

static void populate_hv_lapic_state(hv_local_interrupt_controller_state *hv,
                                    const CPUState *cpu)
{
    uint32_t x2apic_id;
    X86CPU *x86cpu = X86_CPU(cpu);
    MshvAPICState *ms = MSHV_APIC(x86cpu->apic_state);
    APICCommonState *s = &ms->parent_obj;
    size_t i;

    /*
     * x2APIC:
     * - APIC ID is the full 32-bit initial_apic_id
     * - LDR is read-only, architecturally derived from the ID
     * - DFR does not exist in x2APIC mode
     */
    if (is_x2apic_mode(s)) {
        x2apic_id = s->initial_apic_id;

        hv->apic_id  = x2apic_id;
        hv->apic_ldr = ((x2apic_id >> 4) << 16) | (1 << (x2apic_id & 0xf));
        hv->apic_dfr = 0;
    } else {
        hv->apic_id  = s->id << 24;
        hv->apic_ldr = s->log_dest << 24;
        hv->apic_dfr = s->dest_mode << 28 | 0x0fffffff;
    }
    hv->apic_version = ms->apic_version;
    hv->apic_spurious = s->spurious_vec;
    for (i = 0; i < 8; i++) {
        hv->apic_isr[i] = s->isr[i];
        hv->apic_tmr[i] = s->tmr[i];
        hv->apic_irr[i] = s->irr[i];
    }
    hv->apic_esr      = s->esr;
    hv->apic_icr_high = s->icr[1];
    hv->apic_icr_low  = s->icr[0];

    hv->apic_lvt_timer   = s->lvt[APIC_LVT_TIMER];
    hv->apic_lvt_thermal = s->lvt[APIC_LVT_THERMAL];
    hv->apic_lvt_perfmon = s->lvt[APIC_LVT_PERFORM];
    hv->apic_lvt_lint0   = s->lvt[APIC_LVT_LINT0];
    hv->apic_lvt_lint1   = s->lvt[APIC_LVT_LINT1];
    hv->apic_lvt_error   = s->lvt[APIC_LVT_ERROR];
    hv->apic_lvt_cmci    = ms->apic_lvt_cmci;

    hv->apic_error_status         = ms->apic_error_status;
    hv->apic_initial_count        = s->initial_count;
    hv->apic_counter_value        = ms->apic_counter_value;
    hv->apic_divide_configuration = s->divide_conf;
    hv->apic_remote_read          = ms->apic_remote_read;
}

static void populate_apic_state(CPUState *cpu,
                                const hv_local_interrupt_controller_state *hv)
{
    X86CPU *x86cpu = X86_CPU(cpu);
    MshvAPICState *ms = MSHV_APIC(x86cpu->apic_state);
    APICCommonState *s = &ms->parent_obj;
    size_t i;

    /*
     * x2APIC:
     * - APIC ID is the full 32-bit initial_apic_id
     * - LDR is read-only, architecturally derived from the ID
     * - DFR does not exist in x2APIC mode
     */
    if (is_x2apic_mode(s)) {
        s->initial_apic_id = hv->apic_id;
    } else {
        s->id        = hv->apic_id  >> 24;
        s->log_dest  = hv->apic_ldr >> 24;
        s->dest_mode = hv->apic_dfr >> 28;
    }
    ms->apic_version = hv->apic_version;
    s->spurious_vec = hv->apic_spurious;
    for (i = 0; i < 8; i++) {
        s->isr[i] = hv->apic_isr[i];
        s->tmr[i] = hv->apic_tmr[i];
        s->irr[i] = hv->apic_irr[i];
    }
    s->esr = hv->apic_esr;
    s->icr[1] = hv->apic_icr_high;
    s->icr[0] = hv->apic_icr_low;

    s->lvt[APIC_LVT_TIMER]   = hv->apic_lvt_timer;
    s->lvt[APIC_LVT_THERMAL] = hv->apic_lvt_thermal;
    s->lvt[APIC_LVT_PERFORM] = hv->apic_lvt_perfmon;
    s->lvt[APIC_LVT_LINT0]   = hv->apic_lvt_lint0;
    s->lvt[APIC_LVT_LINT1]   = hv->apic_lvt_lint1;
    s->lvt[APIC_LVT_ERROR]   = hv->apic_lvt_error;
    ms->apic_lvt_cmci        = hv->apic_lvt_cmci;

    ms->apic_error_status  = hv->apic_error_status;
    s->initial_count       = hv->apic_initial_count;
    ms->apic_counter_value = hv->apic_counter_value;
    s->divide_conf         = hv->apic_divide_configuration;
    ms->apic_remote_read   = hv->apic_remote_read;
}

static void whpx_put_apic_state_legacy(APICCommonState *s,
                                struct whpx_lapic_state *kapic)
{
    int i;

    memset(kapic, 0, sizeof(*kapic));
    if (s->apicbase & MSR_IA32_APICBASE_EXTD) {
        kapic->fields[0x2].data = s->initial_apic_id;
    } else {
        kapic->fields[0x2].data = s->id << 24;
    }
    kapic->fields[0x3].data = s->version | ((APIC_LVT_NB - 1) << 16);
    kapic->fields[0x8].data = s->tpr;
    kapic->fields[0xd].data = s->log_dest << 24;
    kapic->fields[0xe].data = s->dest_mode << 28 | 0x0fffffff;
    kapic->fields[0xf].data = s->spurious_vec;
    for (i = 0; i < 8; i++) {
        kapic->fields[0x10 + i].data = s->isr[i];
        kapic->fields[0x18 + i].data = s->tmr[i];
        kapic->fields[0x20 + i].data = s->irr[i];
    }

    kapic->fields[0x28].data = s->esr;
    kapic->fields[0x30].data = s->icr[0];
    kapic->fields[0x31].data = s->icr[1];
    for (i = 0; i < APIC_LVT_NB; i++) {
        kapic->fields[0x32 + i].data = s->lvt[i];
    }

    kapic->fields[0x38].data = s->initial_count;
    kapic->fields[0x3e].data = s->divide_conf;
}

static void whpx_get_apic_state_legacy(APICCommonState *s,
                                struct whpx_lapic_state *kapic)
{
    int i, v;

    if (s->apicbase & MSR_IA32_APICBASE_EXTD) {
        assert(kapic->fields[0x2].data == s->initial_apic_id);
    } else {
        s->id = kapic->fields[0x2].data >> 24;
    }
    s->tpr = kapic->fields[0x8].data;
    s->arb_id = kapic->fields[0x9].data;
    s->log_dest = kapic->fields[0xd].data >> 24;
    s->dest_mode = kapic->fields[0xe].data >> 28;
    s->spurious_vec = kapic->fields[0xf].data;
    for (i = 0; i < 8; i++) {
        s->isr[i] = kapic->fields[0x10 + i].data;
        s->tmr[i] = kapic->fields[0x18 + i].data;
        s->irr[i] = kapic->fields[0x20 + i].data;
    }

    s->esr = kapic->fields[0x28].data;
    s->icr[0] = kapic->fields[0x30].data;
    s->icr[1] = kapic->fields[0x31].data;
    for (i = 0; i < APIC_LVT_NB; i++) {
        s->lvt[i] = kapic->fields[0x32 + i].data;
    }

    s->initial_count = kapic->fields[0x38].data;
    s->divide_conf = kapic->fields[0x3e].data;

    v = (s->divide_conf & 3) | ((s->divide_conf >> 1) & 4);
    s->count_shift = (v + 1) & 7;

    s->initial_count_load_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    apic_next_timer(s, s->initial_count_load_time);
}

static int apic_set_base_check(APICCommonState *s, uint64_t val)
{
    /* Enable x2apic when x2apic is not supported by CPU */
    if (!cpu_has_x2apic_feature(&s->cpu->env) &&
        val & MSR_IA32_APICBASE_EXTD) {
        return -1;
    }

    /*
     * Transition into invalid state
     * (s->apicbase & MSR_IA32_APICBASE_ENABLE == 0) &&
     * (s->apicbase & MSR_IA32_APICBASE_EXTD) == 1
     */
    if (!(val & MSR_IA32_APICBASE_ENABLE) &&
        (val & MSR_IA32_APICBASE_EXTD)) {
        return -1;
    }

    /* Invalid transition from disabled mode to x2APIC */
    if (!(s->apicbase & MSR_IA32_APICBASE_ENABLE) &&
        !(s->apicbase & MSR_IA32_APICBASE_EXTD) &&
        (val & MSR_IA32_APICBASE_ENABLE) &&
        (val & MSR_IA32_APICBASE_EXTD)) {
        return -1;
    }

    /* Invalid transition from x2APIC to xAPIC */
    if ((s->apicbase & MSR_IA32_APICBASE_ENABLE) &&
        (s->apicbase & MSR_IA32_APICBASE_EXTD) &&
        (val & MSR_IA32_APICBASE_ENABLE) &&
        !(val & MSR_IA32_APICBASE_EXTD)) {
        return -1;
    }

    return 0;
}

static int apic_set_base(APICCommonState *s, uint64_t val)
{
    if (apic_set_base_check(s, val) < 0) {
        return -1;
    }

    s->apicbase = (val & MSR_IA32_APICBASE_BASE) |
        (s->apicbase & (MSR_IA32_APICBASE_BSP | MSR_IA32_APICBASE_ENABLE));
    if (!(val & MSR_IA32_APICBASE_ENABLE)) {
        s->apicbase &= ~MSR_IA32_APICBASE_ENABLE;
        cpu_clear_apic_feature(&s->cpu->env);
    }

    /* Transition from disabled mode to xAPIC */
    if (!(s->apicbase & MSR_IA32_APICBASE_ENABLE) &&
        (val & MSR_IA32_APICBASE_ENABLE)) {
        s->apicbase |= MSR_IA32_APICBASE_ENABLE;
        cpu_set_apic_feature(&s->cpu->env);
    }

    /* Transition from xAPIC to x2APIC */
    if (cpu_has_x2apic_feature(&s->cpu->env) &&
        !(s->apicbase & MSR_IA32_APICBASE_EXTD) &&
        (val & MSR_IA32_APICBASE_EXTD)) {
        s->apicbase |= MSR_IA32_APICBASE_EXTD;
    }

    return 0;
}

static void whpx_put_apic_base(CPUState *cpu, uint64_t val)
{
    HRESULT hr;
    WHV_REGISTER_VALUE reg_value = {.Reg64 = val};
    WHV_REGISTER_NAME reg_name = WHvX64RegisterApicBase;

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(
             whpx_global.partition,
             cpu->cpu_index,
             &reg_name, 1,
             &reg_value);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set MSR APIC base, hr=%08lx", hr);
    }
}

static void whpx_apic_set_tpr(APICCommonState *s, uint8_t val)
{
    s->tpr = (val & APIC_PR_SUB_CLASS) << APIC_PR_CLASS_SHIFT;
}

static uint8_t whpx_apic_get_tpr(APICCommonState *s)
{
    return s->tpr >> APIC_PR_CLASS_SHIFT;
}

static void whpx_apic_vapic_base_update(APICCommonState *s)
{
    /* not implemented yet */
}

static void whpx_apic_put_legacy(CPUState *cs, run_on_cpu_data data)
{
    APICCommonState *s = data.host_ptr;
    struct whpx_lapic_state kapic;
    HRESULT hr;

    whpx_put_apic_base(CPU(s->cpu), s->apicbase);
    whpx_put_apic_state_legacy(s, &kapic);

    hr = whp_dispatch.WHvSetVirtualProcessorInterruptControllerState2(
        whpx_global.partition,
        cs->cpu_index,
        &kapic,
        sizeof(kapic));
    if (FAILED(hr)) {
        fprintf(stderr,
            "WHvSetVirtualProcessorInterruptControllerState failed: %08lx\n",
             hr);

        abort();
    }
}

static void whpx_apic_get_legacy(APICCommonState *s)
{
    CPUState *cpu = CPU(s->cpu);
    struct whpx_lapic_state kapic;

    HRESULT hr = whp_dispatch.WHvGetVirtualProcessorInterruptControllerState2(
        whpx_global.partition,
        cpu->cpu_index,
        &kapic,
        sizeof(kapic),
        NULL);
    if (FAILED(hr)) {
        fprintf(stderr,
            "WHvSetVirtualProcessorInterruptControllerState failed: %08lx\n",
            hr);

        abort();
    }

    whpx_get_apic_state_legacy(s, &kapic);
}

static void whpx_apic_put(CPUState *cs, run_on_cpu_data data)
{
    MshvAPICState *s = data.host_ptr;
    struct hv_local_interrupt_controller_state kapic;
    HRESULT hr;
    /* buffer aligned to 4k, as *state requires that */
    void *buffer;
    size_t size = 4096;

    if (!whp_dispatch.WHvSetVirtualProcessorState) {
        return whpx_apic_put_legacy(cs, data);
    }

    buffer = qemu_memalign(size, size);
    whpx_put_apic_base(CPU(s->parent_obj.cpu), s->parent_obj.apicbase);
    populate_hv_lapic_state(&kapic, cs);
    memcpy(buffer, &kapic, sizeof(kapic));

    hr = whp_dispatch.WHvSetVirtualProcessorState(
            whpx_global.partition,
            cs->cpu_index,
            WHvVirtualProcessorStateTypeInterruptControllerState2,
            buffer,
            size);

    qemu_vfree(buffer);
    if (FAILED(hr)) {
        fprintf(stderr,
            "WHvSetVirtualProcessorState failed: %08lx\n",
             hr);

        abort();
    }
}

void whpx_apic_get(APICCommonState *s)
{
    CPUState *cpu = CPU(s->cpu);
    struct hv_local_interrupt_controller_state kapic;
    HRESULT hr;
    /* buffer aligned to 4k, as *state requires that */
    void *buffer;
    size_t size = 4096;

    if (!whp_dispatch.WHvSetVirtualProcessorState) {
        return whpx_apic_get_legacy(s);
    }

    buffer = qemu_memalign(size, size);

    hr = whp_dispatch.WHvGetVirtualProcessorState(
        whpx_global.partition,
        cpu->cpu_index,
        WHvVirtualProcessorStateTypeInterruptControllerState2,
        buffer,
        size,
        NULL);

    if (FAILED(hr)) {
        fprintf(stderr,
            "WHvGetVirtualProcessorState failed: %08lx\n",
            hr);

        abort();
    }

    memcpy(&kapic, buffer, sizeof(kapic));
    qemu_vfree(buffer);
    populate_apic_state(cpu, &kapic);
}

static void whpx_apic_pre_save(APICCommonState *s)
{
    whpx_apic_get(s);
}

static void whpx_apic_post_load(APICCommonState *s)
{
    run_on_cpu(CPU(s->cpu), whpx_apic_put, RUN_ON_CPU_HOST_PTR(s));
}

static void whpx_apic_external_nmi(APICCommonState *s)
{
}

static void whpx_send_msi(MSIMessage *msg)
{
    uint64_t addr = msg->address;
    uint32_t data = msg->data;
    uint8_t dest = (addr & MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT;
    uint8_t vector = (data & MSI_DATA_VECTOR_MASK) >> MSI_DATA_VECTOR_SHIFT;
    uint8_t dest_mode = (addr >> MSI_ADDR_DEST_MODE_SHIFT) & 0x1;
    uint8_t trigger_mode = (data >> MSI_DATA_TRIGGER_SHIFT) & 0x1;
    uint8_t delivery = (data >> MSI_DATA_DELIVERY_MODE_SHIFT) & 0x7;

    if (vector == 0) {
        warn_report("Ignoring request for interrupt vector 0");
        return;
    }

    WHV_INTERRUPT_CONTROL interrupt = {
        /* Values correspond to delivery modes */
        .Type = delivery,
        .DestinationMode = dest_mode ?
            WHvX64InterruptDestinationModeLogical :
            WHvX64InterruptDestinationModePhysical,

        .TriggerMode = trigger_mode ?
            WHvX64InterruptTriggerModeLevel : WHvX64InterruptTriggerModeEdge,
        .Reserved = 0,
        .Vector = vector,
        .Destination = dest,
    };
    HRESULT hr = whp_dispatch.WHvRequestInterrupt(whpx_global.partition,
                     &interrupt, sizeof(interrupt));
    if (FAILED(hr)) {
        fprintf(stderr, "whpx: injection failed, MSI (%llx, %x) delivery: %d, "
                "dest_mode: %d, trigger mode: %d, vector: %d, lost (%08lx)\n",
                addr, data, delivery, dest_mode, trigger_mode, vector, hr);
    }
}

static uint64_t whpx_apic_mem_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    return ~(uint64_t)0;
}

static void whpx_apic_mem_write(void *opaque, hwaddr addr,
                                uint64_t data, unsigned size)
{
    MSIMessage msg = { .address = addr, .data = data };
    whpx_send_msi(&msg);
}

static uint32_t set_apic_delivery_mode(uint32_t reg, uint32_t mode)
{
    return ((reg) & ~0x700) | ((mode) << 8);
}

int whpx_apic_init_cpu(CPUState *cpu)
{
    uint32_t *lvt_lint0, *lvt_lint1;
    HRESULT hr;
    struct hv_local_interrupt_controller_state lapic_state = { 0 };
    /* buffer aligned to 4k, as *state requires that */
    void *buffer;
    size_t size = 4096;

    buffer = qemu_memalign(size, size);
    hr = whp_dispatch.WHvGetVirtualProcessorState(
        whpx_global.partition,
        cpu->cpu_index,
        WHvVirtualProcessorStateTypeInterruptControllerState2,
        buffer,
        size,
        NULL);
    if (FAILED(hr)) {
        return hr;
    }

    memcpy(&lapic_state, buffer, sizeof(lapic_state));

    lvt_lint0 = &lapic_state.apic_lvt_lint0;
    *lvt_lint0 = set_apic_delivery_mode(*lvt_lint0, APIC_DM_EXTINT);

    lvt_lint1 = &lapic_state.apic_lvt_lint1;
    *lvt_lint1 = set_apic_delivery_mode(*lvt_lint1, APIC_DM_NMI);

    /* TODO: should we skip setting lapic if the values are the same? */

    memcpy(buffer, &lapic_state, sizeof(lapic_state));
    hr = whp_dispatch.WHvSetVirtualProcessorState(
            whpx_global.partition,
            cpu->cpu_index,
            WHvVirtualProcessorStateTypeInterruptControllerState2,
            buffer,
            size);
    if (FAILED(hr)) {
        return hr;
    }

    qemu_vfree(buffer);
    populate_apic_state(cpu, &lapic_state);

    return 0;
}

static const MemoryRegionOps whpx_apic_io_ops = {
    .read = whpx_apic_mem_read,
    .write = whpx_apic_mem_write,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void whpx_apic_reset(APICCommonState *s)
{
    /* Not used by WHPX. */
    s->wait_for_sipi = 0;

    run_on_cpu(CPU(s->cpu), whpx_apic_put, RUN_ON_CPU_HOST_PTR(s));
}

static const VMStateDescription vmstate_mshv_apic = {
    .name = "mshv-apic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(apic_version, MshvAPICState),
        VMSTATE_UINT32(apic_lvt_cmci, MshvAPICState),
        VMSTATE_UINT32(apic_error_status, MshvAPICState),
        VMSTATE_UINT32(apic_counter_value, MshvAPICState),
        VMSTATE_UINT32(apic_remote_read, MshvAPICState),
        VMSTATE_END_OF_LIST()
    }
};

static void whpx_apic_realize(DeviceState *dev, Error **errp)
{
    APICCommonState *s = APIC_COMMON(dev);
    MshvAPICState *ms = MSHV_APIC(dev);

    memory_region_init_io(&s->io_memory, OBJECT(s), &whpx_apic_io_ops, s,
                          "whpx-apic-msi", APIC_SPACE_SIZE);

    msi_nonbroken = true;

    /*
     * We register this state explicity, rather than going via dc->vmsd.
     * The auto-wiring would register the state with
     * instance_id == VMSTATE_INSTANCE_ID_ANY, which for the APIC doesn't
     * work, b/c the ID carries semantic meaning for restoring the state
     * on the destination (which vcpu it belongs to).
     */
    if (!!whp_dispatch.WHvSetVirtualProcessorState) {
        vmstate_register_with_alias_id(NULL,
                                   s->initial_apic_id, &vmstate_mshv_apic, ms,
                                   -1, 0, NULL);
    }
}

static void whpx_apic_unrealize(DeviceState *dev)
{
    MshvAPICState *ms = MSHV_APIC(dev);

    if (!!whp_dispatch.WHvSetVirtualProcessorState) {
        vmstate_unregister(NULL, &vmstate_mshv_apic, ms);
    }
}

static void whpx_apic_class_init(ObjectClass *klass, const void *data)
{
    APICCommonClass *k = APIC_COMMON_CLASS(klass);

    k->realize = whpx_apic_realize;
    k->unrealize = whpx_apic_unrealize;
    k->reset = whpx_apic_reset;
    k->set_base = apic_set_base;
    k->set_tpr = whpx_apic_set_tpr;
    k->get_tpr = whpx_apic_get_tpr;
    k->pre_save = whpx_apic_pre_save;
    k->post_load = whpx_apic_post_load;
    k->vapic_base_update = whpx_apic_vapic_base_update;
    k->external_nmi = whpx_apic_external_nmi;
    k->send_msi = whpx_send_msi;
}

static const TypeInfo whpx_apic_info = {
    .name = TYPE_MSHV_APIC,
    .parent = TYPE_APIC_COMMON,
    .instance_size = sizeof(MshvAPICState),
    .class_init = whpx_apic_class_init,
};

static void whpx_apic_register_types(void)
{
    type_register_static(&whpx_apic_info);
}

type_init(whpx_apic_register_types)
