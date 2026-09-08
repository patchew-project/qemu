/*
 * Phytium E2000 QSPI controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/ssi/phytium_qspi.h"

#include "hw/core/register.h"
#include "hw/core/irq.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

REG32(FLASH_CAP, 0x00)
REG32(RD_CFG, 0x04)
    FIELD(RD_CFG, CMD, 24, 8)
    FIELD(RD_CFG, ADDR_SEL, 19, 1)
    FIELD(RD_CFG, LATENCY, 18, 1)
    FIELD(RD_CFG, DUMMY, 4, 5)
REG32(WR_CFG, 0x08)
    FIELD(WR_CFG, CMD, 24, 8)
    FIELD(WR_CFG, ADDR_SEL, 4, 1)
    FIELD(WR_CFG, MODE, 3, 1)
REG32(FLUSH, 0x0c)
REG32(CMD_PORT, 0x10)
    FIELD(CMD_PORT, CMD, 24, 8)
    FIELD(CMD_PORT, CS, 19, 2)
    FIELD(CMD_PORT, CMD_ADDR, 15, 1)
    FIELD(CMD_PORT, LATENCY, 14, 1)
    FIELD(CMD_PORT, DATA_XFER, 13, 1)
    FIELD(CMD_PORT, ADDR_SEL, 12, 1)
    FIELD(CMD_PORT, DUMMY, 7, 5)
    FIELD(CMD_PORT, RW_NUM, 3, 3)
REG32(ADDR_PORT, 0x14)
REG32(HD_PORT, 0x18)
REG32(LD_PORT, 0x1c)
REG32(FUN_SET, 0x20)
REG32(WIP, 0x24)
REG32(WP, 0x28)
REG32(MODE, 0x2c)
REG32(LEGACY_CTRL, 0x30)

#define PHYTIUM_E2000_QSPI_R_MAX     (A_LEGACY_CTRL / sizeof(uint32_t) + 1)
#define PHYTIUM_E2000_QSPI_PAGE_SIZE 256

struct PhytiumE2000QSPIState {
    SysBusDevice parent_obj;

    uint32_t regs[PHYTIUM_E2000_QSPI_R_MAX];
    RegisterInfo regs_info[PHYTIUM_E2000_QSPI_R_MAX];
    MemoryRegion direct_mr;
    SSIBus *spi;
    qemu_irq cs;
    uint8_t write_buffer[PHYTIUM_E2000_QSPI_PAGE_SIZE];
    uint32_t write_address;
    uint16_t write_len;
    bool write_pending;
    bool resetting;
};

static void phytium_e2000_qspi_select(PhytiumE2000QSPIState *s)
{
    qemu_irq_lower(s->cs);
}

static void phytium_e2000_qspi_deselect(PhytiumE2000QSPIState *s)
{
    qemu_irq_raise(s->cs);
}

static void phytium_e2000_qspi_send_address(PhytiumE2000QSPIState *s,
                                            uint32_t address,
                                            unsigned int length)
{
    int shift;

    for (shift = (length - 1) * 8; shift >= 0; shift -= 8) {
        ssi_transfer(s->spi, extract32(address, shift, 8));
    }
}

static void phytium_e2000_qspi_send_dummy(PhytiumE2000QSPIState *s,
                                          unsigned int cycles)
{
    unsigned int i;

    for (i = 0; i < DIV_ROUND_UP(cycles, 8); i++) {
        ssi_transfer(s->spi, 0);
    }
}

static void phytium_e2000_qspi_exec_port(PhytiumE2000QSPIState *s)
{
    uint32_t cfg = s->regs[R_CMD_PORT];
    uint64_t tx = s->regs[R_LD_PORT] |
                  ((uint64_t)s->regs[R_HD_PORT] << 32);
    uint64_t rx = 0;
    unsigned int cs = FIELD_EX32(cfg, CMD_PORT, CS);
    unsigned int length;
    unsigned int i;

    if (cs != 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: command selects unwired CS%u\n",
                      TYPE_PHYTIUM_E2000_QSPI, cs);
        s->regs[R_LD_PORT] = 0;
        s->regs[R_HD_PORT] = 0;
        return;
    }

    phytium_e2000_qspi_select(s);
    ssi_transfer(s->spi, FIELD_EX32(cfg, CMD_PORT, CMD));

    if (FIELD_EX32(cfg, CMD_PORT, CMD_ADDR)) {
        length = FIELD_EX32(cfg, CMD_PORT, ADDR_SEL) ? 4 : 3;
        phytium_e2000_qspi_send_address(s, s->regs[R_ADDR_PORT], length);
    }

    if (FIELD_EX32(cfg, CMD_PORT, LATENCY)) {
        phytium_e2000_qspi_send_dummy(
            s, FIELD_EX32(cfg, CMD_PORT, DUMMY) + 1);
    }

    if (FIELD_EX32(cfg, CMD_PORT, DATA_XFER)) {
        length = FIELD_EX32(cfg, CMD_PORT, RW_NUM) + 1;
        for (i = 0; i < length; i++) {
            rx = deposit64(rx, i * 8, 8,
                           ssi_transfer(s->spi, extract64(tx, i * 8, 8)));
        }
    }

    phytium_e2000_qspi_deselect(s);
    s->regs[R_LD_PORT] = extract64(rx, 0, 32);
    s->regs[R_HD_PORT] = extract64(rx, 32, 32);
}

static void phytium_e2000_qspi_ld_post_write(RegisterInfo *reg,
                                             uint64_t value)
{
    PhytiumE2000QSPIState *s = PHYTIUM_E2000_QSPI(reg->opaque);

    /*
     * LD_PORT is the final staging write and commits the command assembled in
     * CMD_PORT, ADDR_PORT, HD_PORT, and LD_PORT. register_reset() invokes the
     * same callback, so suppress SPI side effects while installing defaults.
     */
    if (!s->resetting) {
        phytium_e2000_qspi_exec_port(s);
    }
}

static void phytium_e2000_qspi_flush_write_buffer(PhytiumE2000QSPIState *s)
{
    uint32_t cfg = s->regs[R_WR_CFG];
    unsigned int address_len;
    unsigned int i;

    if (!s->write_pending) {
        return;
    }
    if (!FIELD_EX32(cfg, WR_CFG, MODE)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: FLUSH with direct writes disabled\n",
                      TYPE_PHYTIUM_E2000_QSPI);
        return;
    }

    phytium_e2000_qspi_select(s);
    ssi_transfer(s->spi, FIELD_EX32(cfg, WR_CFG, CMD));
    address_len = FIELD_EX32(cfg, WR_CFG, ADDR_SEL) ? 4 : 3;
    phytium_e2000_qspi_send_address(s, s->write_address, address_len);
    for (i = 0; i < s->write_len; i++) {
        ssi_transfer(s->spi, s->write_buffer[i]);
    }
    phytium_e2000_qspi_deselect(s);

    s->write_pending = false;
    s->write_address = 0;
    s->write_len = 0;
}

static uint64_t phytium_e2000_qspi_flush_pre_write(RegisterInfo *reg,
                                                   uint64_t value)
{
    PhytiumE2000QSPIState *s = PHYTIUM_E2000_QSPI(reg->opaque);

    if (value & BIT(0)) {
        phytium_e2000_qspi_flush_write_buffer(s);
    }
    return 0;
}

static const RegisterAccessInfo phytium_e2000_qspi_regs_info[] = {
    { .name = "FLASH_CAP", .addr = A_FLASH_CAP,
      .rsvd = 0xfffc0000 },
    { .name = "RD_CFG", .addr = A_RD_CFG },
    { .name = "WR_CFG", .addr = A_WR_CFG,
      .rsvd = 0x00fffc00 },
    { .name = "FLUSH", .addr = A_FLUSH,
      .rsvd = 0xfffffffe,
      .pre_write = phytium_e2000_qspi_flush_pre_write },
    { .name = "CMD_PORT", .addr = A_CMD_PORT,
      .rsvd = BIT(23) },
    { .name = "ADDR_PORT", .addr = A_ADDR_PORT },
    { .name = "HD_PORT", .addr = A_HD_PORT },
    { .name = "LD_PORT", .addr = A_LD_PORT,
      .post_write = phytium_e2000_qspi_ld_post_write },
    { .name = "FUN_SET", .addr = A_FUN_SET },
    { .name = "WIP", .addr = A_WIP,
      .rsvd = 0x00ffffe0 },
    { .name = "WP", .addr = A_WP,
      .rsvd = 0xfffc0000 },
    { .name = "MODE", .addr = A_MODE,
      .rsvd = 0xffff0000 },
    { .name = "LEGACY_CTRL", .addr = A_LEGACY_CTRL,
      .rsvd = 0xfffffeff },
};

static const MemoryRegionOps phytium_e2000_qspi_regs_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static uint64_t phytium_e2000_qspi_direct_read(void *opaque, hwaddr address,
                                               unsigned int size)
{
    PhytiumE2000QSPIState *s = opaque;
    uint32_t cfg = s->regs[R_RD_CFG];
    uint64_t value = 0;
    unsigned int address_len;
    unsigned int i;

    phytium_e2000_qspi_select(s);
    ssi_transfer(s->spi, FIELD_EX32(cfg, RD_CFG, CMD));
    address_len = FIELD_EX32(cfg, RD_CFG, ADDR_SEL) ? 4 : 3;
    phytium_e2000_qspi_send_address(s, address, address_len);

    if (FIELD_EX32(cfg, RD_CFG, LATENCY)) {
        phytium_e2000_qspi_send_dummy(
            s, FIELD_EX32(cfg, RD_CFG, DUMMY) + 1);
    }

    for (i = 0; i < size; i++) {
        value = deposit64(value, i * 8, 8, ssi_transfer(s->spi, 0));
    }
    phytium_e2000_qspi_deselect(s);

    return value;
}

static void phytium_e2000_qspi_direct_write(void *opaque, hwaddr address,
                                            uint64_t value,
                                            unsigned int size)
{
    PhytiumE2000QSPIState *s = opaque;
    uint32_t cfg = s->regs[R_WR_CFG];
    unsigned int i;

    if (!FIELD_EX32(cfg, WR_CFG, MODE)) {
        return;
    }

    /*
     * The direct aperture accepts multiple CPU stores for one page program.
     * Buffer them until FLUSH supplies the transaction boundary required by
     * the flash command.
     */
    if (!s->write_pending) {
        if ((address & ~(PHYTIUM_E2000_QSPI_PAGE_SIZE - 1)) !=
            ((address + size - 1) &
             ~(PHYTIUM_E2000_QSPI_PAGE_SIZE - 1))) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: direct write crosses a flash page\n",
                          TYPE_PHYTIUM_E2000_QSPI);
            return;
        }
        s->write_pending = true;
        s->write_address = address;
        s->write_len = 0;
    } else if (address != s->write_address + s->write_len ||
               (s->write_address &
                (PHYTIUM_E2000_QSPI_PAGE_SIZE - 1)) +
               s->write_len + size > PHYTIUM_E2000_QSPI_PAGE_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: non-contiguous direct writes before FLUSH\n",
                      TYPE_PHYTIUM_E2000_QSPI);
        return;
    }

    for (i = 0; i < size; i++) {
        s->write_buffer[s->write_len++] = extract64(value, i * 8, 8);
    }
}

static const MemoryRegionOps phytium_e2000_qspi_direct_ops = {
    .read = phytium_e2000_qspi_direct_read,
    .write = phytium_e2000_qspi_direct_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static void phytium_e2000_qspi_reset(DeviceState *dev)
{
    PhytiumE2000QSPIState *s = PHYTIUM_E2000_QSPI(dev);
    size_t i;

    s->resetting = true;
    for (i = 0; i < ARRAY_SIZE(phytium_e2000_qspi_regs_info); i++) {
        register_reset(&s->regs_info[
            phytium_e2000_qspi_regs_info[i].addr / sizeof(uint32_t)]);
    }
    memset(s->write_buffer, 0, sizeof(s->write_buffer));
    s->write_address = 0;
    s->write_len = 0;
    s->write_pending = false;
    s->resetting = false;
    phytium_e2000_qspi_deselect(s);
}

static void phytium_e2000_qspi_init(Object *obj)
{
    PhytiumE2000QSPIState *s = PHYTIUM_E2000_QSPI(obj);
    RegisterInfoArray *reg_array;

    s->spi = ssi_create_bus(DEVICE(obj), "spi");
    qdev_init_gpio_out_named(DEVICE(obj), &s->cs, "cs", 1);

    reg_array = register_init_block32(
        DEVICE(obj), phytium_e2000_qspi_regs_info,
        ARRAY_SIZE(phytium_e2000_qspi_regs_info), s->regs_info, s->regs,
        &phytium_e2000_qspi_regs_ops, false,
        PHYTIUM_E2000_QSPI_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &reg_array->mem);

    memory_region_init_io(&s->direct_mr, obj,
                          &phytium_e2000_qspi_direct_ops, s,
                          TYPE_PHYTIUM_E2000_QSPI ".direct",
                          PHYTIUM_E2000_QSPI_DIRECT_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->direct_mr);
}

static const VMStateDescription phytium_e2000_qspi_vmsd = {
    .name = TYPE_PHYTIUM_E2000_QSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PhytiumE2000QSPIState,
                             PHYTIUM_E2000_QSPI_R_MAX),
        VMSTATE_UINT8_ARRAY(write_buffer, PhytiumE2000QSPIState,
                            PHYTIUM_E2000_QSPI_PAGE_SIZE),
        VMSTATE_UINT32(write_address, PhytiumE2000QSPIState),
        VMSTATE_UINT16(write_len, PhytiumE2000QSPIState),
        VMSTATE_BOOL(write_pending, PhytiumE2000QSPIState),
        VMSTATE_END_OF_LIST()
    },
};

static void phytium_e2000_qspi_class_init(ObjectClass *klass,
                                          const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &phytium_e2000_qspi_vmsd;
    device_class_set_legacy_reset(dc, phytium_e2000_qspi_reset);
}

static const TypeInfo phytium_e2000_qspi_info = {
    .name = TYPE_PHYTIUM_E2000_QSPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PhytiumE2000QSPIState),
    .instance_init = phytium_e2000_qspi_init,
    .class_init = phytium_e2000_qspi_class_init,
};

static void phytium_e2000_qspi_register_types(void)
{
    type_register_static(&phytium_e2000_qspi_info);
}

type_init(phytium_e2000_qspi_register_types)
