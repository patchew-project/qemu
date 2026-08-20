/*
 * TI SEC PROXY SysBus device
 *
 * Copyright (c) 2025 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TI K3 SoCs use it as mailbox transport to the DMSC/SYSFW endpoint.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "hw/misc/ti-sec-proxy.h"
#include "hw/core/sysbus.h"
#include "hw/core/register.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "trace.h"

#define SEC_PROXY_MAX_MSG (16)

REG32(SEC_PROXY_0_thread_status, 0x0)
FIELD(SEC_PROXY_0_thread_status, ERROR, 31, 1)
FIELD(SEC_PROXY_0_thread_status, DIR, 30, 1)
FIELD(SEC_PROXY_0_thread_status, MAX_CNT, 16, 8)
FIELD(SEC_PROXY_0_thread_status, CUR_CNT, 0, 8)

REG32(SEC_PROXY_0_thread_threshold, 0x4)
FIELD(SEC_PROXY_0_thread_threshold, THR_CNT, 0, 8)

REG32(SEC_PROXY_0_thread_private, 0x0)
FIELD(SEC_PROXY_0_thread_private, SRC_THR, 0, 10)

REG32(SEC_PROXY_0_thread_message, 0x4)

REG32(SEC_PROXY_0_buffer_l, 0x0)
REG32(SEC_PROXY_0_buffer_h, 0x4)
FIELD(SEC_PROXY_0_buffer_h, BASE_H, 0, 16)

REG32(SEC_PROXY_0_target_l, 0x8)
REG32(SEC_PROXY_0_target_h, 0xc)
FIELD(SEC_PROXY_0_target_h, TARGET_H, 0, 16)

REG32(SEC_PROXY_0_orderid, 0x10)
FIELD(SEC_PROXY_0_orderid, ORDERID, 0, 4)
FIELD(SEC_PROXY_0_orderid, REPLACE, 4, 1)

REG32(SEC_PROXY_0_thread_ctl, 0x0)
FIELD(SEC_PROXY_0_thread_ctl, QUEUE, 0, 16)
FIELD(SEC_PROXY_0_thread_ctl, MAX_CNT, 16, 8)
FIELD(SEC_PROXY_0_thread_ctl, DIR, 31, 1)

REG32(SEC_PROXY_0_thread_evt_map, 0x4)
FIELD(SEC_PROXY_0_thread_evt_map, THR_EVT, 0, 16)
FIELD(SEC_PROXY_0_thread_evt_map, ERR_EVT, 16, 16)

REG32(SEC_PROXY_0_thread_dst, 0x8)
FIELD(SEC_PROXY_0_thread_dst, THREAD, 0, 16)

REG32(SEC_PROXY_0_pid, 0x0)
FIELD(SEC_PROXY_0_pid, SCHEME, 30, 2)
FIELD(SEC_PROXY_0_pid, BU, 28, 2)
FIELD(SEC_PROXY_0_pid, FUNC, 16, 12)
FIELD(SEC_PROXY_0_pid, RTL, 11, 5)
FIELD(SEC_PROXY_0_pid, MAJOR, 8, 3)
FIELD(SEC_PROXY_0_pid, CUSTOM, 6, 2)
FIELD(SEC_PROXY_0_pid, MINOR, 0, 6)

REG32(SEC_PROXY_0_config, 0x4)
FIELD(SEC_PROXY_0_config, MSG_SIZE, 16, 16)
FIELD(SEC_PROXY_0_config, THREADS, 0, 16)

REG32(SEC_PROXY_0_glb_evt, 0x14)
FIELD(SEC_PROXY_0_glb_evt, ERR_EVENT, 0, 16)

static RegisterAccessInfo sec_proxy_mmrs_regs_info[] = {
    {
        .name = "SEC_PROXY_0_pid",
        .addr = A_SEC_PROXY_0_pid,
        .ro = 0xffffffff,
        .reset = 1714843904,
    },
    {
        .name = "SEC_PROXY_0_config",
        .addr = A_SEC_PROXY_0_config,
        .ro = 0xffffffff,
        .reset = 4194380,
    },
    {
        .name = "SEC_PROXY_0_glb_evt",
        .addr = A_SEC_PROXY_0_glb_evt,
        .rsvd = 0xffff0000,
        .reset = 65535,
    }
};

static void ti_sec_proxy_realize(DeviceState *dev_soc, Error **errp)
{
    ERRP_GUARD();
    TISecProxyState *s = TI_SEC_PROXY(dev_soc);

    for (int i = 0; i < SEC_PROXY_THREAD_ID_MAX; ++i) {
        struct TISecProxyThreadInfo *ti = &s->thread_info[i];
        ti->thread_id = i;
        /* K3 secure proxy alternates inbound and outbound threads. */
        ti->is_outbound = (i % 2) ? true : false;
        ti->num_messages = ti->is_outbound ? SEC_PROXY_MSG_MAX_WORDS : 0;
        memset(ti->current_message, 0, sizeof(ti->current_message));
    }
}

/* --- Backend API exported to DMSC -------------------------------------- */
void ti_sec_proxy_register_msg_cb(TISecProxyState *sp, uint16_t thread_id,
                                  TISecProxyMsgCb cb, void *opaque)
{
    if (thread_id >= ARRAY_SIZE(sp->thread_info)) {
        return;
    }

    sp->thread_info[thread_id].cb = cb;
    sp->thread_info[thread_id].cb_opaque = opaque;
}

size_t ti_sec_proxy_push_msg(TISecProxyState *sp, uint16_t thread_id,
                             const uint32_t *words, size_t nbytes)
{
    struct TISecProxyThreadInfo *ti;

    if (thread_id >= ARRAY_SIZE(sp->thread_info)) {
        return 0;
    }

    ti = &sp->thread_info[thread_id];
    if (nbytes > sizeof(ti->current_message) - sizeof(uint32_t)) {
        return 0;
    }

    memcpy(&ti->current_message[1], words, nbytes);
    qemu_irq_raise(sp->irq_evt);

    ti->num_messages++;
    return ti->num_messages;
}

uint32_t ti_sec_proxy_get_msg_words(TISecProxyState *sp)
{
    return sp->msg_words;
}

void ti_sec_proxy_reset_thread_count(TISecProxyState *sp, uint16_t thread_id)
{
    if (thread_id >= ARRAY_SIZE(sp->thread_info)) {
        return;
    }

    sp->thread_info[thread_id].num_messages = 0;
}

static const char *
ti_sec_proxy_get_thread_channel_name(enum TISciThreadIds thread_id)
{
    const char *thread_names[] = {
        "MAIN_0_R5_0_READ_RESPONSE_THREAD", "MAIN_0_R5_0_WRITE_THREAD",
        "MAIN_0_R5_1_READ_RESPONSE_THREAD", "MAIN_0_R5_1_WRITE_THREAD",
        "MAIN_0_R5_2_READ_RESPONSE_THREAD", "MAIN_0_R5_2_WRITE_THREAD",
        "MAIN_0_R5_3_READ_RESPONSE_THREAD", "MAIN_0_R5_3_WRITE_THREAD",
        "A53_0_READ_RESPONSE_THREAD",       "A53_0_WRITE_THREAD",
        "A53_1_READ_RESPONSE_THREAD",       "A53_1_WRITE_THREAD",
        "A53_2_READ_RESPONSE_THREAD",       "A53_2_WRITE_THREAD",
        "A53_3_READ_RESPONSE_THREAD",       "A53_3_WRITE_THREAD",
        "M4_0_READ_RESPONSE_THREAD",        "M4_0_WRITE_THREAD",
        "MAIN_1_R5_0_READ_RESPONSE_THREAD", "MAIN_1_R5_0_WRITE_THREAD",
        "MAIN_1_R5_1_READ_RESPONSE_THREAD", "MAIN_1_R5_1_WRITE_THREAD",
        "MAIN_1_R5_2_READ_RESPONSE_THREAD", "MAIN_1_R5_2_WRITE_THREAD",
        "MAIN_1_R5_3_READ_RESPONSE_THREAD", "MAIN_1_R5_3_WRITE_THREAD",
        "A53_4_READ_RESPONSE_THREAD",       "A53_4_WRITE_THREAD",
        "ICSSG_0_READ_RESPONSE_THREAD",     "ICSSG_0_WRITE_THREAD",
        "ICSSG_1_READ_RESPONSE_THREAD",     "ICSSG_1_WRITE_THREAD"};

    if (thread_id < ARRAY_SIZE(thread_names)) {
        return thread_names[thread_id];
    }
    return "UNKNOWN_THREAD";
}

static void ti_sec_proxy_reset_hold(Object *obj, ResetType type)
{
    TISecProxyState *s = TI_SEC_PROXY(obj);

    for (int i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void ti_sec_proxy_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = ti_sec_proxy_realize;
    rc->phases.hold = ti_sec_proxy_reset_hold;
}

static const MemoryRegionOps ti_sec_proxy_mmr_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t ti_sec_proxy_read_scfg(void *opaque, hwaddr addr, unsigned size)
{
    TISecProxyState *s = opaque;
    if (addr <= 0x10) {
        return 0;
    }

    hwaddr thread_rel_addr = addr - 0x10;
    int thread_num = thread_rel_addr / 0x1000;
    int reg = (thread_rel_addr % 0x1000) / 4;
    struct TISecProxyThreadInfo *tinfo = &s->thread_info[thread_num];

    trace_ti_sec_proxy_read_scfg(
        ti_sec_proxy_get_thread_channel_name(thread_num), reg, addr);
    return tinfo->is_outbound ? 0x0 : 0x80000000;
}

static void ti_sec_proxy_write_scfg(void *opaque, hwaddr addr, uint64_t value,
                                    unsigned size)
{
    if (addr <= 0x10) {
        return;
    }

    int thread_num = addr / 0x1000;
    int reg = (addr % 0x1000) / 4;

    trace_ti_sec_proxy_write_scfg(
        ti_sec_proxy_get_thread_channel_name(thread_num), reg, addr, value);
}

static const MemoryRegionOps ti_sec_proxy_scfg_ops = {
    .read = ti_sec_proxy_read_scfg,
    .write = ti_sec_proxy_write_scfg,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t ti_sec_proxy_read_rt(void *opaque, hwaddr addr, unsigned size)
{
    TISecProxyState *s = opaque;
    int thread_num = addr / 0x1000;
    hwaddr off = addr % 0x1000;

    int byte = off & 0x3;

    struct TISecProxyThreadInfo *tinfo = &s->thread_info[thread_num];

    uint32_t reg_val = (tinfo->num_messages) |
                       (tinfo->is_outbound ? 0x0 : 0x40000000) |
                       (tinfo->is_outbound ? (tinfo->num_messages << 16) : 0x0);

    uint64_t ret;

    if (size == 1) {
        ret = (reg_val >> (8 * byte)) & 0xff;
    } else if (size == 2) {
        ret = (reg_val >> (8 * (byte & ~1))) & 0xffff;
    } else if (size == 4) {
        ret = reg_val;
    } else {
        ret = 0;
    }
    trace_ti_sec_proxy_read_rt(ti_sec_proxy_get_thread_channel_name(thread_num),
                               ret, addr);
    return ret;
}

static void ti_sec_proxy_write_rt(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size)
{
}

static const MemoryRegionOps ti_sec_proxy_rt_ops = {
    .read = ti_sec_proxy_read_rt,
    .write = ti_sec_proxy_write_rt,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t ti_sec_proxy_read_target(void *opaque, hwaddr addr,
                                         unsigned size)
{
    TISecProxyState *s = opaque;
    int thread_num = addr / 0x1000;
    hwaddr off = addr % 0x1000;

    int reg = off >> 2;    /* 32-bit register index */
    int byte = off & 0x3;  /* byte offset within the 32-bit register */

    struct TISecProxyThreadInfo *tinfo = &s->thread_info[thread_num];

    /* First fetch the full little-endian 32-bit register. */
    uint32_t reg_val = (uint32_t)tinfo->current_message[reg];
    uint64_t ret = 0;

    if (size == 1) {
        ret = (reg_val >> (8 * byte)) & 0xffu;
    } else if (size == 2) {
        /*
         * Odd halfword reads are rounded down. Guest drivers use aligned
         * accesses, but this keeps byte-lane handling tolerant.
         */
        ret = (reg_val >> (8 * (byte & ~1))) & 0xffffu;
    } else if (size == 4) {
        ret = reg_val;
    } else {
        /* Shouldn't happen given .valid, but be defensive. */
        ret = 0;
    }

    /* For inbound threads reset the message counter */
    if (!tinfo->is_outbound && reg == SEC_PROXY_MAX_MSG - 1) {
        trace_ti_sec_proxy_complete_read(
            ti_sec_proxy_get_thread_channel_name(thread_num),
            tinfo->num_messages);
        tinfo->num_messages = 0;
        qemu_irq_lower(s->irq_evt);
    }
    return ret;
}

static void ti_sec_proxy_write_target(void *opaque, hwaddr addr, uint64_t value,
                                      unsigned size)
{
    TISecProxyState *s = opaque;
    int thread_num = addr / 0x1000;
    hwaddr off = addr % 0x1000;

    int reg = off >> 2;    /* 32-bit register index */
    int byte = off & 0x3;  /* byte offset within the 32-bit register */

    struct TISecProxyThreadInfo *tinfo = &s->thread_info[thread_num];

    if (reg >= 16) {
        return;
    }

    uint32_t cur = (uint32_t)tinfo->current_message[reg];
    uint32_t v32 = (uint32_t)value;

    if (size == 1) {
        uint32_t mask = 0xffu << (8 * byte);
        cur = (cur & ~mask) | ((v32 & 0xffu) << (8 * byte));
    } else if (size == 2) {
        /* Round odd halfword writes down, same as in the read path. */
        int hbyte = (byte & ~1);
        uint32_t mask = 0xffffu << (8 * hbyte);
        cur = (cur & ~mask) | ((v32 & 0xffffu) << (8 * hbyte));
    } else if (size == 4) {
        cur = v32;
    } else {
        return;
    }

    tinfo->current_message[reg] = cur;

    /*
     * The data-window commit point is the last register. Byte writes can
     * hit it more than once while the last word is assembled.
     */
    if (reg == SEC_PROXY_MAX_MSG - 1) {
        trace_ti_sec_proxy_complete_write(
            ti_sec_proxy_get_thread_channel_name(thread_num),
            tinfo->num_messages);

        if (tinfo->cb) {
            trace_ti_sec_proxy_announce_callback(
                ti_sec_proxy_get_thread_channel_name(thread_num));
            tinfo->cb(tinfo->cb_opaque, thread_num, &tinfo->current_message[1],
                      SEC_PROXY_MAX_MSG);
        }
    }
}

static const MemoryRegionOps ti_sec_proxy_target_ops = {
    .read = ti_sec_proxy_read_target,
    .write = ti_sec_proxy_write_target,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void ti_sec_proxy_init(Object *obj)
{
    TISecProxyState *s = TI_SEC_PROXY(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    s->reg_array = register_init_block32(DEVICE(obj), sec_proxy_mmrs_regs_info,
                                         ARRAY_SIZE(sec_proxy_mmrs_regs_info),
                                         s->regs_info, s->regs,
                                         &ti_sec_proxy_mmr_ops, true, 0x100);
    sysbus_init_mmio(sbd, &s->reg_array->mem);

    memory_region_init_io(&s->iomem_scfg, OBJECT(s), &ti_sec_proxy_scfg_ops, s,
                          "ti-sec-proxy-scfg", 0x80000);
    sysbus_init_mmio(sbd, &s->iomem_scfg);

    memory_region_init_io(&s->iomem_rt, OBJECT(s), &ti_sec_proxy_rt_ops, s,
                          "ti-sec-proxy-rt", 0x80000);
    sysbus_init_mmio(sbd, &s->iomem_rt);

    memory_region_init_io(&s->iomem_target_data, OBJECT(s),
                          &ti_sec_proxy_target_ops, s, "ti-sec-proxy-target",
                          0x80000);
    sysbus_init_mmio(sbd, &s->iomem_target_data);

    sysbus_init_irq(sbd, &s->irq_evt);
}

static const TypeInfo ti_sec_proxy_info = {
    .name = TYPE_TI_SEC_PROXY,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TISecProxyState),
    .class_init = ti_sec_proxy_class_init,
    .instance_init = ti_sec_proxy_init,
};

static void ti_sec_proxy_types(void)
{
    type_register_static(&ti_sec_proxy_info);
}

type_init(ti_sec_proxy_types)
