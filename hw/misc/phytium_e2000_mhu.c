/*
 * Phytium E2000 MHU/SCMI doorbell
 *
 * This is a boot-oriented SCMI transport proxy. It acknowledges requests in
 * shared SRAM so PBF can complete clock and platform setup; it is not a full
 * SCMI protocol server.
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/phytium_e2000_mhu.h"

#include "hw/core/register.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "system/address-spaces.h"

#define PHYTIUM_E2000_PBF_SCMI_MBOX_BASE  0x32a10400
#define PHYTIUM_E2000_SCMI_STATUS_OFFSET  0x04
#define PHYTIUM_E2000_SCMI_LEN_OFFSET     0x14
#define PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET 0x1c
#define PHYTIUM_E2000_SCMI_STATUS_FREE    BIT(0)

/*
 * The SDK defines AP OS status/set/clear at 0x100/0x108/0x110 within a
 * channel. PBF selects the channel at MHU offset 0x200, producing the global
 * offsets below. Writes to AP_OS_SET are the request notification.
 */
REG32(AP_OS_STAT, 0x300)
REG32(AP_OS_SET, 0x308)
REG32(AP_OS_CLR, 0x310)

#define PHYTIUM_E2000_MHU_R_MAX \
    (PHYTIUM_E2000_MHU_MMIO_SIZE / sizeof(uint32_t))

struct PhytiumE2000MHUState {
    SysBusDevice parent_obj;

    uint32_t regs[PHYTIUM_E2000_MHU_R_MAX];
    RegisterInfo regs_info[PHYTIUM_E2000_MHU_R_MAX];
};

static void phytium_e2000_mhu_complete_scmi(void)
{
    uint8_t buf[sizeof(uint32_t)];
    uint32_t len;

    /*
     * Preserve the caller's message length, but reserve one status word for
     * the minimal success response returned in the payload.
     */
    address_space_read(&address_space_memory,
                       PHYTIUM_E2000_PBF_SCMI_MBOX_BASE +
                       PHYTIUM_E2000_SCMI_LEN_OFFSET,
                       MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf));
    len = MAX(ldl_le_p(buf), (uint32_t)sizeof(uint32_t));

    stl_le_p(buf, 0);
    address_space_write(&address_space_memory,
                        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE +
                        PHYTIUM_E2000_SCMI_PAYLOAD_OFFSET,
                        MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf));
    stl_le_p(buf, len);
    address_space_write(&address_space_memory,
                        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE +
                        PHYTIUM_E2000_SCMI_LEN_OFFSET,
                        MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf));
    stl_le_p(buf, PHYTIUM_E2000_SCMI_STATUS_FREE);
    /*
     * Publish the free bit last. PBF polls this field as the ownership handoff
     * and may consume the response immediately after observing it.
     */
    address_space_write(&address_space_memory,
                        PHYTIUM_E2000_PBF_SCMI_MBOX_BASE +
                        PHYTIUM_E2000_SCMI_STATUS_OFFSET,
                        MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf));
}

void phytium_e2000_mhu_seed_mailbox(void)
{
    /*
     * PBR leaves the shared channel available before releasing PBF. Seed the
     * same ownership and success state even before the first doorbell write.
     */
    phytium_e2000_mhu_complete_scmi();
}

static void phytium_e2000_mhu_doorbell_post_write(RegisterInfo *reg,
                                                  uint64_t value)
{
    /*
     * Complete requests synchronously because no separate SCP CPU executes in
     * this model. Zero writes only update doorbell storage.
     */
    if (value) {
        phytium_e2000_mhu_complete_scmi();
    }
}

static const RegisterAccessInfo phytium_e2000_mhu_regs_info[] = {
    /*
     * The functional transport does not model an SCP interrupt line. STAT is
     * therefore idle, SET completes the shared-memory transaction, and CLR
     * remains ordinary register storage for the firmware acknowledge path.
     */
    { .name = "AP_OS_STAT", .addr = A_AP_OS_STAT,
      .ro = UINT32_MAX },
    { .name = "AP_OS_SET", .addr = A_AP_OS_SET,
      .post_write = phytium_e2000_mhu_doorbell_post_write },
    { .name = "AP_OS_CLR", .addr = A_AP_OS_CLR },
};

static const MemoryRegionOps phytium_e2000_mhu_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void phytium_e2000_mhu_reset(DeviceState *dev)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(phytium_e2000_mhu_regs_info); i++) {
        register_reset(&s->regs_info[
            phytium_e2000_mhu_regs_info[i].addr / sizeof(uint32_t)]);
    }
}

static void phytium_e2000_mhu_init(Object *obj)
{
    PhytiumE2000MHUState *s = PHYTIUM_E2000_MHU(obj);
    RegisterInfoArray *reg_array;

    reg_array = register_init_block32(
        DEVICE(obj), phytium_e2000_mhu_regs_info,
        ARRAY_SIZE(phytium_e2000_mhu_regs_info), s->regs_info, s->regs,
        &phytium_e2000_mhu_ops, false, PHYTIUM_E2000_MHU_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &reg_array->mem);
}

static const VMStateDescription phytium_e2000_mhu_vmsd = {
    .name = TYPE_PHYTIUM_E2000_MHU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PhytiumE2000MHUState,
                             PHYTIUM_E2000_MHU_R_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void phytium_e2000_mhu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &phytium_e2000_mhu_vmsd;
    device_class_set_legacy_reset(dc, phytium_e2000_mhu_reset);
}

static const TypeInfo phytium_e2000_mhu_info = {
    .name = TYPE_PHYTIUM_E2000_MHU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PhytiumE2000MHUState),
    .instance_init = phytium_e2000_mhu_init,
    .class_init = phytium_e2000_mhu_class_init,
};

static void phytium_e2000_mhu_register_types(void)
{
    type_register_static(&phytium_e2000_mhu_info);
}

type_init(phytium_e2000_mhu_register_types)
