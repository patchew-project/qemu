/*
 * IBM Common FRU Access Macro - S variant (CFAM-S)
 *
 * Copyright (C) 2026 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/fsi/cfam-s.h"
#include "hw/fsi/fsi.h"

/* bits [22:21] are the slave ID, low 21 bits address registers within it */
#define CFAM_SID_MASK       0x1fffff
#define CFAM_WINDOW_SIZE    0x800000 /* 8 MiB per slave, covers SID 0..3 */

#define CFAM_RESPONDER_BASE 0x400 /* == FSI_RESPONDER_PAGE_SIZE */

/* Config-table word fields (see Linux fsi-master.h) */
#define CFAM_CONF_NEXT          (1u << 31)
#define CFAM_CONF_SLOTS(n)      (((n) & 0xff) << 16)
#define CFAM_CONF_VERSION(v)    (((v) & 0xf) << 12)
#define CFAM_CONF_TYPE(t)       (((t) & 0xff) << 4)
#define CFAM_CHIP_ID_MAJOR(m)   (((m) & 0xf) << 8)

/* Engine IDs (include/linux/fsi.h) */
#define FSI_ENGINE_ID_RESPONDER     0x3
#define FSI_ENGINE_ID_MBOXV1        0x14
#define FSI_CHIP_ID_MAJOR_CFAM_S    0x9 /* major == 9 -> CFAM-S */

/* Engine layout: 0x000 config table, 0x400 responder, 0x800 mbox */
#define CFAM_MBOX_BASE          0x800
#define CFAM_MBOX_SCRATCH_OFF   0xe0
#define CFAM_MBOX_SCRATCH_BASE  (CFAM_MBOX_BASE + CFAM_MBOX_SCRATCH_OFF)

static uint8_t cfam_s_crc4(uint8_t c, uint64_t x, int bits)
{
    static const uint8_t tab[16] = {
        0x0, 0x7, 0xe, 0x9, 0xb, 0xc, 0x5, 0x2,
        0x1, 0x6, 0xf, 0x8, 0xa, 0xd, 0x4, 0x3,
    };
    int i;

    x &= (1ull << bits) - 1;
    bits = (bits + 3) & ~0x3;
    for (i = bits - 4; i >= 0; i -= 4) {
        c = tab[c ^ ((x >> i) & 0xf)];
    }
    return c;
}

static uint32_t cfam_s_cfg_word(uint32_t fields)
{
    return fields | cfam_s_crc4(0, fields >> 4, 28);
}

static uint64_t fsi_cfam_s_read(void *opaque, hwaddr addr, unsigned size)
{
    FSICFAMSState *cfam = FSI_CFAM_S(opaque);
    uint32_t off = (uint32_t)addr & CFAM_SID_MASK;
    uint32_t val = 0;

    if (off < CFAM_RESPONDER_BASE) {
        switch (off) {
        case 0x00:
            /* chip-id: NEXT set, MAJOR=9 (CFAM-S) */
            val = cfam_s_cfg_word(CFAM_CONF_NEXT |
                                  CFAM_CHIP_ID_MAJOR(FSI_CHIP_ID_MAJOR_CFAM_S));
            break;
        case 0x04:
            /* responder engine entry */
            val = cfam_s_cfg_word(CFAM_CONF_NEXT | CFAM_CONF_SLOTS(1) |
                                  CFAM_CONF_VERSION(1) |
                                  CFAM_CONF_TYPE(FSI_ENGINE_ID_RESPONDER));
            break;
        case 0x08:
            /* mailbox engine entry, last (NEXT clear) */
            val = cfam_s_cfg_word(CFAM_CONF_SLOTS(1) | CFAM_CONF_VERSION(1) |
                                  CFAM_CONF_TYPE(FSI_ENGINE_ID_MBOXV1));
            break;
        default:
            break;
        }
    } else if (off >= CFAM_MBOX_SCRATCH_BASE &&
               off < CFAM_MBOX_SCRATCH_BASE + CFAM_S_MBOX_SCRATCH_NUM * 4) {
        val = cfam->mbox_scratch[(off - CFAM_MBOX_SCRATCH_BASE) / 4];
    }

    trace_fsi_cfam_config_read(addr, size);
    return val;
}

static void fsi_cfam_s_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    FSICFAMSState *cfam = FSI_CFAM_S(opaque);
    uint32_t off = (uint32_t)addr & CFAM_SID_MASK;

    if (off >= CFAM_MBOX_SCRATCH_BASE &&
        off < CFAM_MBOX_SCRATCH_BASE + CFAM_S_MBOX_SCRATCH_NUM * 4) {
        cfam->mbox_scratch[(off - CFAM_MBOX_SCRATCH_BASE) / 4] = (uint32_t)data;
    }
    trace_fsi_cfam_config_write(addr, size, data);
}

static const MemoryRegionOps cfam_s_ops = {
    .read = fsi_cfam_s_read,
    .write = fsi_cfam_s_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static void fsi_cfam_s_realize(DeviceState *dev, Error **errp)
{
    FSICFAMSState *cfam = FSI_CFAM_S(dev);

    memory_region_init_io(&cfam->mr, OBJECT(cfam), &cfam_s_ops, cfam,
                          TYPE_FSI_CFAM_S, CFAM_WINDOW_SIZE);
}

static void fsi_cfam_s_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->bus_type = TYPE_FSI_BUS;
    dc->realize = fsi_cfam_s_realize;
}

static const TypeInfo fsi_cfam_s_info = {
    .name = TYPE_FSI_CFAM_S,
    .parent = TYPE_FSI_SLAVE,
    .instance_size = sizeof(FSICFAMSState),
    .class_init = fsi_cfam_s_class_init,
};

static void fsi_cfam_s_register_types(void)
{
    type_register_static(&fsi_cfam_s_info);
}

type_init(fsi_cfam_s_register_types);
