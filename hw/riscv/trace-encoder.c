/*
 * Emulation of a RISC-V Trace Encoder
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "trace-encoder.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "trace.h"
#include "system/device_tree.h"
#include "hw/register.h"
#include "cpu.h"
#include "hw/riscv/trace-ram-sink.h"
#include "rv-trace-messages.h"

/*
 * Size of header + payload since we're not sending
 * srcID and timestamp.
 */
#define TRACE_MSG_MAX_SIZE 32

static TracePrivLevel trencoder_get_curr_priv_level(TraceEncoder *te)
{
    CPURISCVState *env = &te->cpu->env;

    switch (env->priv) {
    case PRV_U:
        return env->virt_enabled ? VU : U;
    case PRV_S:
        return env->virt_enabled ? VS : S_HS;
    case PRV_M:
        return M;
    }

    /*
     * Return a reserved value to signal an error.
     * TODO: handle Debug (D).
     */
    return RESERVED;
}

/*
 * trTeControl register fields
 */
REG32(TR_TE_CONTROL, 0x0)
    FIELD(TR_TE_CONTROL, ACTIVE, 0, 1)
    FIELD(TR_TE_CONTROL, ENABLE, 1, 1)
    FIELD(TR_TE_CONTROL, INST_TRACING, 2, 1)
    FIELD(TR_TE_CONTROL, EMPTY, 3, 1)
    FIELD(TR_TE_CONTROL, INST_MODE, 4, 3)
    FIELD(TR_TE_CONTROL, CONTEXT, 9, 1)
    FIELD(TR_TE_CONTROL, INST_STALL_ENA, 13, 1)
    FIELD(TR_TE_CONTROL, INHIBIT_SRC, 15, 1)
    FIELD(TR_TE_CONTROL, INST_SYNC_MODE, 16, 2)
    FIELD(TR_TE_CONTROL, INST_SYNC_MAX, 20, 4)
    FIELD(TR_TE_CONTROL, FORMAT, 24, 3)
    /* reserved bits */
    FIELD(TR_TE_CONTROL, RSVP1, 7, 2)
    FIELD(TR_TE_CONTROL, RSVP2, 10, 1)
    FIELD(TR_TE_CONTROL, RSVP3, 14, 1)
    FIELD(TR_TE_CONTROL, RSVP4, 18, 2)
    FIELD(TR_TE_CONTROL, RSVP5, 27, 4)

#define R_TR_TE_CONTROL_RSVP_BITS (MAKE_64BIT_MASK(32, 32) | \
                                   R_TR_TE_CONTROL_RSVP1_MASK | \
                                   R_TR_TE_CONTROL_RSVP2_MASK | \
                                   R_TR_TE_CONTROL_RSVP3_MASK | \
                                   R_TR_TE_CONTROL_RSVP4_MASK | \
                                   R_TR_TE_CONTROL_RSVP5_MASK)

/* trTeControlEmpty is the only RO field and reset value */
#define R_TR_TE_CONTROL_RESET R_TR_TE_CONTROL_EMPTY_MASK
#define R_TR_TE_CONTROL_RO_BITS R_TR_TE_CONTROL_EMPTY_MASK

/*
 * trTeImpl register fields
 */
REG32(TR_TE_IMPL, 0x4)
    FIELD(TR_TE_IMPL, VER_MAJOR, 0, 4)
    FIELD(TR_TE_IMPL, VER_MINOR, 4, 4)
    FIELD(TR_TE_IMPL, COMP_TYPE, 8, 4)
    FIELD(TR_TE_IMPL, PROTOCOL_MAJOR, 16, 4)
    FIELD(TR_TE_IMPL, PROTOCOL_MINOR, 20, 4)
    /* reserved bits */
    FIELD(TR_TE_IMPL, RSVP1, 12, 4)
    FIELD(TR_TE_IMPL, RSVP2, 24, 8)

#define R_TR_TE_IMPL_RSVP_BITS (MAKE_64BIT_MASK(32, 32) | \
                                R_TR_TE_IMPL_RSVP1_MASK | \
                                R_TR_TE_IMPL_RSVP2_MASK)

#define R_TR_TE_IMPL_RO_BITS (R_TR_TE_IMPL_VER_MAJOR_MASK | \
                              R_TR_TE_IMPL_VER_MINOR_MASK | \
                              R_TR_TE_IMPL_COMP_TYPE_MASK | \
                              R_TR_TE_IMPL_PROTOCOL_MAJOR_MASK | \
                              R_TR_TE_IMPL_PROTOCOL_MINOR_MASK)

#define R_TR_TE_IMPL_RESET (BIT(0) | BIT(8))

REG32(TR_TE_INST_FEATURES, 0x8)
    FIELD(TR_TE_INST_FEATURES, NO_ADDR_DIFF, 0, 1)

static uint32_t trencoder_read_reg(TraceEncoder *te, uint32_t reg_addr)
{
    hwaddr addr = te->dest_baseaddr + reg_addr;
    uint32_t val;

    cpu_physical_memory_read(addr, &val, sizeof(uint32_t));
    return val;
}

static void trencoder_write_reg(TraceEncoder *te, uint32_t reg_addr,
                                uint32_t val)
{
    hwaddr addr = te->dest_baseaddr + reg_addr;

    cpu_physical_memory_write(addr, &val, sizeof(uint32_t));
}

static hwaddr trencoder_read_ramsink_writep(TraceEncoder *te)
{
    hwaddr ret = trencoder_read_reg(te, A_TR_RAM_WP_HIGH);
    ret <<= 32;
    ret += trencoder_read_reg(te, A_TR_RAM_WP_LOW);

    return ret;
}

static hwaddr trencoder_read_ramsink_ramlimit(TraceEncoder *te)
{
    hwaddr ret = trencoder_read_reg(te, A_TR_RAM_LIMIT_HIGH);
    ret <<= 32;
    ret += trencoder_read_reg(te, A_TR_RAM_LIMIT_LOW);

    return ret;
}

static uint64_t trencoder_te_ctrl_set_hardwire_vals(uint64_t input)
{
    input = FIELD_DP32(input, TR_TE_CONTROL, INST_MODE, 0x6);
    input = FIELD_DP32(input, TR_TE_CONTROL, CONTEXT, 0);
    input = FIELD_DP32(input, TR_TE_CONTROL, INST_STALL_ENA, 0);
    input = FIELD_DP32(input, TR_TE_CONTROL, INHIBIT_SRC, 1);
    input = FIELD_DP32(input, TR_TE_CONTROL, FORMAT, 0);

    /* SYNC_MODE and SYNC_MAX will be revisited */
    input = FIELD_DP32(input, TR_TE_CONTROL, INST_SYNC_MODE, 0);
    input = FIELD_DP32(input, TR_TE_CONTROL, INST_SYNC_MAX, 0);

    return input;
}

static uint64_t trencoder_te_ctrl_prew(RegisterInfo *reg, uint64_t val)
{
    TraceEncoder *te = TRACE_ENCODER(reg->opaque);
    uint32_t trTeActive = ARRAY_FIELD_EX32(te->regs, TR_TE_CONTROL, ACTIVE);
    uint32_t trTeInstTracing = ARRAY_FIELD_EX32(te->regs, TR_TE_CONTROL,
                                                INST_TRACING);
    uint32_t temp;

    val = trencoder_te_ctrl_set_hardwire_vals(val);

    if (!trTeActive) {
        /*
         * 11.2 Reset and discovery, table 58, trTeControl = 0x1
         * means "Release from reset and set all defaults." Do
         * that only if trTeActive is 0.
         */
        if (val == 0x1) {
            val = FIELD_DP32(val, TR_TE_CONTROL, EMPTY, 1);

            return val;
        }

        /*
         * 11.3 Enabling and Disabling hints that the device must
         * be activated first (trTeActive = 1), then enabled.
         * Do not enable the device if it's not active
         * beforehand.
         */
        temp = FIELD_EX32(val, TR_TE_CONTROL, ENABLE);
        if (temp) {
            val = FIELD_DP32(val, TR_TE_CONTROL, ENABLE, 0);
        }
    }

    /*
     * Do not allow inst tracing to start if the device isn't
     * already enabled. Do not allow enabling the devince and
     * and enable tracing at the same time.
     */
    if (!te->enabled && trTeInstTracing) {
        val = FIELD_DP32(val, TR_TE_CONTROL, INST_TRACING, 0);
    }

    return val;
}

static void trencoder_te_ctrl_postw(RegisterInfo *reg, uint64_t val)
{
    TraceEncoder *te = TRACE_ENCODER(reg->opaque);
    uint32_t trTeActive = ARRAY_FIELD_EX32(te->regs, TR_TE_CONTROL, ACTIVE);
    uint32_t trTeEnable = ARRAY_FIELD_EX32(te->regs, TR_TE_CONTROL, ENABLE);
    uint32_t trTeInstTracing = ARRAY_FIELD_EX32(te->regs, TR_TE_CONTROL,
                                                INST_TRACING);
    RISCVCPU *cpu = te->cpu;
    CPURISCVState *env = &cpu->env;

    if (!trTeActive) {
        te->enabled = false;
        te->trace_running = false;
        te->trace_next_insn = false;

        env->trace_running = false;
        return;
    }

    if (te->enabled && !trTeEnable) {
        /* TODO: this should cause a pending trace data flush. */
    }

    te->enabled = trTeEnable ? true : false;

    if (!te->trace_running && trTeInstTracing) {
        /* Starting trace. Ask the CPU for the first trace insn */
        te->trace_next_insn = true;

        te->ramsink_ramstart = trencoder_read_ramsink_writep(te);
        te->ramsink_ramlimit = trencoder_read_ramsink_ramlimit(te);
    }

    te->trace_running = trTeInstTracing ? true : false;
    env->trace_running = te->trace_running;
}

static RegisterAccessInfo trencoder_regs_info[] = {
    {   .name = "TR_TE_CONTROL", .addr = A_TR_TE_CONTROL,
        .rsvd = R_TR_TE_CONTROL_RSVP_BITS,
        .reset = R_TR_TE_CONTROL_RESET,
        .ro = R_TR_TE_CONTROL_RO_BITS,
        .pre_write = &trencoder_te_ctrl_prew,
        .post_write = &trencoder_te_ctrl_postw,
    },
    {   .name = "TR_TE_IMPL", .addr = A_TR_TE_IMPL,
        .rsvd = R_TR_TE_IMPL_RSVP_BITS,
        .reset = R_TR_TE_IMPL_RESET,
        .ro = R_TR_TE_IMPL_RO_BITS,
    },
    {   .name = "TR_TE_INST_FEATURES", .addr = A_TR_TE_INST_FEATURES,
        .reset = R_TR_TE_INST_FEATURES_NO_ADDR_DIFF_MASK,
        .ro = ~0,
    },
};

static uint64_t trencoder_read(void *opaque, hwaddr addr, unsigned size)
{
    TraceEncoder *te = TRACE_ENCODER(opaque);
    RegisterInfo *r = &te->regs_info[addr / 4];

    if (!r->data) {
        trace_trencoder_read_error(addr);
        return 0;
    }

    return register_read(r, ~0, NULL, false);
}

static void trencoder_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    TraceEncoder *te = TRACE_ENCODER(opaque);
    RegisterInfo *r = &te->regs_info[addr / 4];

    if (!r->data) {
        trace_trencoder_write_error(addr, value);
        return;
    }

    register_write(r, value, ~0, NULL, false);
}

static const MemoryRegionOps trencoder_ops = {
    .read = trencoder_read,
    .write = trencoder_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void trencoder_reset(DeviceState *dev)
{
    TraceEncoder *te = TRACE_ENCODER(dev);
    RISCVCPU *cpu = te->cpu;
    CPURISCVState *env = &cpu->env;

    for (int i = 0; i < ARRAY_SIZE(te->regs_info); i++) {
        register_reset(&te->regs_info[i]);
    }

    te->enabled = false;
    te->trace_running = false;
    te->trace_next_insn = false;
    env->trace_running = false;
}

static void trencoder_realize(DeviceState *dev, Error **errp)
{
    TraceEncoder *te = TRACE_ENCODER(dev);

    memory_region_init_io(&te->reg_mem, OBJECT(dev),
                          &trencoder_ops, te,
                          TYPE_TRACE_ENCODER,
                          te->reg_mem_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &te->reg_mem);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, te->baseaddr);

    /* RegisterInfo init taken from hw/dma/xlnx-zdma.c */
    for (int i = 0; i < ARRAY_SIZE(trencoder_regs_info); i++) {
        uint32_t reg_idx = trencoder_regs_info[i].addr / 4;
        RegisterInfo *r = &te->regs_info[reg_idx];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&te->regs[reg_idx],
            .data_size = sizeof(uint32_t),
            .access = &trencoder_regs_info[i],
            .opaque = te,
        };
    }
}

static void trencoder_update_ramsink_writep(TraceEncoder *te,
                                            hwaddr wp_val,
                                            bool wrapped)
{
    uint32_t wp_low = trencoder_read_reg(te, A_TR_RAM_WP_LOW);

    wp_low = FIELD_DP32(wp_low, TR_RAM_WP_LOW, ADDR,
                        extract64(wp_val, 2, 30));

    if (wrapped) {
        wp_low = FIELD_DP32(wp_low, TR_RAM_WP_LOW, WRAP, 1);
    }

    trencoder_write_reg(te, A_TR_RAM_WP_LOW, wp_low);
    trencoder_write_reg(te, A_TR_RAM_WP_HIGH, extract64(wp_val, 32, 32));
}

static void trencoder_send_message_smem(TraceEncoder *trencoder,
                                        uint8_t *msg, uint8_t msg_size)
{
    hwaddr dest = trencoder_read_ramsink_writep(trencoder);
    bool wrapped = false;

    msg_size = QEMU_ALIGN_UP(msg_size, 4);

    /* clear trRamWrap before writing to SMEM */
    dest = FIELD_DP64(dest, TR_RAM_WP_LOW, WRAP, 0);

    /*
     * Fill with null bytes if we can't fit the packet in
     * ramlimit, set wrap and write the packet in ramstart.
     */
    if (dest + msg_size > trencoder->ramsink_ramlimit) {
        g_autofree uint8_t *null_packet = NULL;
        uint8_t null_size = trencoder->ramsink_ramlimit - dest;

        null_packet = g_malloc0(null_size);
        cpu_physical_memory_write(dest, null_packet, null_size);

        dest = trencoder->ramsink_ramstart;
        wrapped = true;
    }

    cpu_physical_memory_write(dest, msg, msg_size);
    dest += msg_size;

    trencoder_update_ramsink_writep(trencoder, dest, wrapped);
}

static void trencoder_send_sync_msg(Object *trencoder_obj, uint64_t pc)
{
    TraceEncoder *trencoder = TRACE_ENCODER(trencoder_obj);
    TracePrivLevel priv = trencoder_get_curr_priv_level(trencoder);
    g_autofree uint8_t *msg = g_malloc0(TRACE_MSG_MAX_SIZE);
    uint8_t msg_size;

    trencoder->first_pc = pc;
    msg_size = rv_etrace_gen_encoded_sync_msg(msg, pc, priv);

    trencoder_send_message_smem(trencoder, msg, msg_size);
}

static void trencoder_send_updiscon(TraceEncoder *trencoder, uint64_t pc)
{
    g_autofree uint8_t *format2_msg = g_malloc0(TRACE_MSG_MAX_SIZE);
    uint8_t addr_msb = extract64(pc, 31, 1);
    bool notify = addr_msb;
    bool updiscon = !notify;
    uint8_t msg_size;

    msg_size = rv_etrace_gen_encoded_format2_msg(format2_msg, pc,
                                                 notify,
                                                 updiscon);
    trencoder_send_message_smem(trencoder, format2_msg, msg_size);

    trencoder->updiscon_pending = false;
}

void trencoder_set_first_trace_insn(Object *trencoder_obj, uint64_t pc)
{
    TraceEncoder *trencoder = TRACE_ENCODER(trencoder_obj);
    TracePrivLevel priv = trencoder_get_curr_priv_level(trencoder);
    g_autofree uint8_t *msg = g_malloc0(TRACE_MSG_MAX_SIZE);
    uint8_t msg_size;

    if (trencoder->updiscon_pending) {
        trencoder_send_updiscon(trencoder, pc);
    }

    trencoder->first_pc = pc;
    trace_trencoder_first_trace_insn(pc);
    msg_size = rv_etrace_gen_encoded_sync_msg(msg, pc, priv);

    trencoder_send_message_smem(trencoder, msg, msg_size);
}

void trencoder_trace_trap_insn(Object *trencoder_obj,
                               uint64_t pc, uint32_t ecause,
                               bool is_interrupt,
                               uint64_t tval)
{
    TraceEncoder *trencoder = TRACE_ENCODER(trencoder_obj);
    TracePrivLevel priv = trencoder_get_curr_priv_level(trencoder);
    g_autofree uint8_t *msg = g_malloc0(TRACE_MSG_MAX_SIZE);
    uint8_t msg_size;

    if (trencoder->updiscon_pending) {
        trencoder_send_updiscon(trencoder, pc);
    }

    msg_size = rv_etrace_gen_encoded_trap_msg(msg, pc, priv,
                                              ecause, is_interrupt,
                                              tval);

    trencoder_send_message_smem(trencoder, msg, msg_size);
}

void trencoder_trace_ppccd(Object *trencoder_obj, uint64_t pc)
{
    TraceEncoder *trencoder = TRACE_ENCODER(trencoder_obj);

    if (trencoder->updiscon_pending) {
        trencoder_send_updiscon(trencoder, pc);
    }

    trencoder_send_sync_msg(trencoder_obj, pc);
}

void trencoder_report_updiscon(Object *trencoder_obj)
{
    TraceEncoder *trencoder = TRACE_ENCODER(trencoder_obj);

    trencoder->updiscon_pending = true;
}

static const Property trencoder_props[] = {
    /*
     * We need a link to the associated CPU to
     * enable/disable tracing.
     */
    DEFINE_PROP_LINK("cpu", TraceEncoder, cpu, TYPE_RISCV_CPU, RISCVCPU *),
    DEFINE_PROP_UINT64("baseaddr", TraceEncoder, baseaddr, 0),
    DEFINE_PROP_UINT64("dest-baseaddr", TraceEncoder, dest_baseaddr, 0),
    DEFINE_PROP_UINT64("ramsink-ramstart", TraceEncoder,
                       ramsink_ramstart, 0),
    DEFINE_PROP_UINT64("ramsink-ramlimit", TraceEncoder,
                       ramsink_ramlimit, 0),
    DEFINE_PROP_UINT32("reg-mem-size", TraceEncoder,
                       reg_mem_size, TRACE_R_MAX * 4),
    DEFINE_PROP_INT32("cpu-id", TraceEncoder, cpu_id, 0),
};

static const VMStateDescription vmstate_trencoder = {
    .name = TYPE_TRACE_ENCODER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TraceEncoder, TRACE_R_MAX),
        VMSTATE_UINT64(baseaddr, TraceEncoder),
        VMSTATE_UINT64(dest_baseaddr, TraceEncoder),
        VMSTATE_UINT64(ramsink_ramstart, TraceEncoder),
        VMSTATE_UINT64(ramsink_ramlimit, TraceEncoder),
        VMSTATE_INT32(cpu_id, TraceEncoder),
        VMSTATE_UINT64(first_pc, TraceEncoder),
        VMSTATE_END_OF_LIST(),
    }
};

static void trencoder_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, trencoder_reset);
    device_class_set_props(dc, trencoder_props);
    dc->realize = trencoder_realize;
    dc->vmsd = &vmstate_trencoder;
}

static const TypeInfo trencoder_info = {
    .name          = TYPE_TRACE_ENCODER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TraceEncoder),
    .class_init    = trencoder_class_init,
};

static void trencoder_register_types(void)
{
    type_register_static(&trencoder_info);
}

type_init(trencoder_register_types)
