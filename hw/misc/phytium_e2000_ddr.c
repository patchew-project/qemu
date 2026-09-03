/*
 * Phytium E2000 DDR training status
 *
 * The vendor firmware accesses controller and PHY state through an indexed
 * selector/value window. This functional model reproduces the completion and
 * error predicates used during boot; it does not simulate analog DDR training
 * or generate physical calibration results.
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/phytium_e2000_ddr.h"

#include "hw/core/register.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

REG32(SELECTOR, 0x80)
REG32(VALUE, 0x84)

#define PHYTIUM_E2000_DDR_R_MAX                  \
    (PHYTIUM_E2000_DDR_MMIO_SIZE / sizeof(uint32_t))
/*
 * Selectors encode a register index as a byte offset. PBF uses separate
 * direct, indirect, and training banks distinguished by 0x800 and 0x1000
 * index biases.
 */
#define PHYTIUM_E2000_DDR_SELECTOR(reg)          ((reg) * 4)
#define PHYTIUM_E2000_DDR_INDIRECT_SELECTOR(reg) (((reg) + 0x800) * 4)
#define PHYTIUM_E2000_DDR_TRAINING_SELECTOR(reg) (((reg) + 0x1000) * 4)
#define PHYTIUM_E2000_DDR_TRAINING_ERR_FIRST     0xc72
#define PHYTIUM_E2000_DDR_TRAINING_ERR_LAST      0xc79

struct PhytiumE2000DDRState {
    SysBusDevice parent_obj;

    uint32_t regs[PHYTIUM_E2000_DDR_R_MAX];
    RegisterInfo regs_info[PHYTIUM_E2000_DDR_R_MAX];
    RegisterAccessInfo regs_access_info[PHYTIUM_E2000_DDR_R_MAX];
};

static uint64_t phytium_e2000_ddr_value_post_read(RegisterInfo *reg,
                                                  uint64_t value)
{
    PhytiumE2000DDRState *s = PHYTIUM_E2000_DDR(reg->opaque);
    uint32_t selector = s->regs[R_SELECTOR];

    /*
     * These fixed selectors are controller/PHY polls observed in the tested
     * firmware. Return only the ready and completion bits that its loops
     * require, without assigning behavior to the surrounding register bank.
     */
    switch (selector) {
    case PHYTIUM_E2000_DDR_INDIRECT_SELECTOR(0xdc):
        return BIT(0);
    case PHYTIUM_E2000_DDR_SELECTOR(0x256):
        return BIT(0) | BIT(25);
    case PHYTIUM_E2000_DDR_INDIRECT_SELECTOR(0x76):
        return BIT(0) | BIT(14) | BIT(27);
    case PHYTIUM_E2000_DDR_SELECTOR(0x10f):
        return 0x40U << 24;
    case PHYTIUM_E2000_DDR_SELECTOR(0x229):
        return BIT(0);
    case PHYTIUM_E2000_DDR_SELECTOR(0x257):
        return BIT(3);
    case PHYTIUM_E2000_DDR_SELECTOR(0x255):
        return BIT(16);
    case PHYTIUM_E2000_DDR_SELECTOR(0x1d5):
        return BIT(16) | BIT(24) | BIT(25);
    default:
        /*
         * The training bank is read repeatedly for lane state and error
         * summaries. Error selectors report no failure; the remaining values
         * are deterministic so repeated sweeps see stable lane data.
         */
        if (selector >= PHYTIUM_E2000_DDR_TRAINING_SELECTOR(0) &&
            selector < PHYTIUM_E2000_DDR_TRAINING_SELECTOR(0x1000)) {
            uint32_t index = selector / sizeof(uint32_t) - 0x1000;

            if (index >= PHYTIUM_E2000_DDR_TRAINING_ERR_FIRST &&
                index <= PHYTIUM_E2000_DDR_TRAINING_ERR_LAST) {
                return 0;
            }
            /*
             * Preserve the masks and flag combinations checked by the PBF
             * training algorithm for these recurring low-byte selectors.
             */
            if ((index & 0xff) == 0x34) {
                return 0x00000fff;
            }
            if ((index & 0xff) == 0x38) {
                return BIT(4) | BIT(5);
            }
            if ((index & 0xff) == 0x83) {
                return 0;
            }
            if ((index & 0xff) == 0x3b) {
                return 0x01234567U | BIT(26) | BIT(27);
            }
            return 0x01234567U;
        }
        /*
         * Unclassified offsets retain ordinary register storage. This keeps
         * setup writes readable without pretending they affect DDR timing.
         */
        return value;
    }
}

static const MemoryRegionOps phytium_e2000_ddr_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void phytium_e2000_ddr_reset(DeviceState *dev)
{
    PhytiumE2000DDRState *s = PHYTIUM_E2000_DDR(dev);
    int i;

    for (i = 0; i < PHYTIUM_E2000_DDR_R_MAX; i++) {
        register_reset(&s->regs_info[i]);
    }
}

static void phytium_e2000_ddr_init(Object *obj)
{
    PhytiumE2000DDRState *s = PHYTIUM_E2000_DDR(obj);
    RegisterInfoArray *reg_array;
    int i;

    /*
     * Firmware may touch any word in this compact window before selecting a
     * status source, so create registerinfo metadata for the full aperture and
     * specialize only SELECTOR and VALUE.
     */
    for (i = 0; i < PHYTIUM_E2000_DDR_R_MAX; i++) {
        s->regs_access_info[i].name = "DDR_STATUS";
        s->regs_access_info[i].addr = i * sizeof(uint32_t);
    }
    s->regs_access_info[R_SELECTOR].name = "SELECTOR";
    s->regs_access_info[R_VALUE].name = "VALUE";
    s->regs_access_info[R_VALUE].ro = UINT32_MAX;
    s->regs_access_info[R_VALUE].post_read =
        phytium_e2000_ddr_value_post_read;

    reg_array = register_init_block32(
        DEVICE(obj), s->regs_access_info, PHYTIUM_E2000_DDR_R_MAX,
        s->regs_info, s->regs, &phytium_e2000_ddr_ops, false,
        PHYTIUM_E2000_DDR_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &reg_array->mem);
}

static const VMStateDescription phytium_e2000_ddr_vmsd = {
    .name = TYPE_PHYTIUM_E2000_DDR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PhytiumE2000DDRState,
                             PHYTIUM_E2000_DDR_R_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void phytium_e2000_ddr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &phytium_e2000_ddr_vmsd;
    device_class_set_legacy_reset(dc, phytium_e2000_ddr_reset);
}

static const TypeInfo phytium_e2000_ddr_info = {
    .name = TYPE_PHYTIUM_E2000_DDR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PhytiumE2000DDRState),
    .instance_init = phytium_e2000_ddr_init,
    .class_init = phytium_e2000_ddr_class_init,
};

static void phytium_e2000_ddr_register_types(void)
{
    type_register_static(&phytium_e2000_ddr_info);
}

type_init(phytium_e2000_ddr_register_types)
