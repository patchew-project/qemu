/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface master
 */

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "qemu/log.h"

#include "hw/fsi/bits.h"
#include "hw/fsi/fsi-master.h"

#define TYPE_OP_BUS "opb"

#define TO_REG(x)                               ((x) >> 2)

#define FSI_MMODE                               TO_REG(0x000)
#define   FSI_MMODE_IPOLL_DMA_EN                BE_BIT(0)
#define   FSI_MMODE_HW_ERROR_RECOVERY_EN        BE_BIT(1)
#define   FSI_MMODE_RELATIVE_ADDRESS_EN         BE_BIT(2)
#define   FSI_MMODE_PARITY_CHECK_EN             BE_BIT(3)
#define   FSI_MMODE_CLOCK_DIVIDER_0             BE_GENMASK(4, 13)
#define   FSI_MMODE_CLOCK_DIVIDER_1             BE_GENMASK(14, 23)
#define   FSI_MMODE_DEBUG_EN                    BE_BIT(24)

#define FSI_MDELAY                              TO_REG(0x004)
#define   FSI_MDELAY_ECHO_0                     BE_GENMASK(0, 3)
#define   FSI_MDELAY_SEND_0                     BE_GENMASK(4, 7)
#define   FSI_MDELAY_ECHO_1                     BE_GENMASK(8, 11)
#define   FSI_MDELAY_SEND_1                     BE_GENMASK(12, 15)

#define FSI_MENP0                               TO_REG(0x010)
#define FSI_MENP32                              TO_REG(0x014)
#define FSI_MSENP0                              TO_REG(0x018)
#define FSI_MLEVP0                              TO_REG(0x018)
#define FSI_MSENP32                             TO_REG(0x01c)
#define FSI_MLEVP32                             TO_REG(0x01c)
#define FSI_MCENP0                              TO_REG(0x020)
#define FSI_MREFP0                              TO_REG(0x020)
#define FSI_MCENP32                             TO_REG(0x024)
#define FSI_MREFP32                             TO_REG(0x024)

#define FSI_MAEB                                TO_REG(0x070)
#define   FSI_MAEB_ANY_CPU_ERROR                BE_BIT(0)
#define   FSI_MAEB_ANY_DMA_ERROR                BE_GENMASK(1, 16)
#define   FSI_MAEB_ANY_PARITY_ERROR             BE_BIT(17)

#define FSI_MVER                                TO_REG(0x074)
#define   FSI_MVER_VERSION                      BE_GENMASK(0, 7)
#define   FSI_MVER_BRIDGES                      BE_GENMASK(8, 15)
#define   FSI_MVER_PORTS                        BE_GENMASK(16, 23)

#define FSI_MRESP0                              TO_REG(0x0d0)
#define   FSI_MRESP0_RESET_PORT_GENERAL         BE_BIT(0)
#define   FSI_MRESP0_RESET_PORT_ERROR           BE_BIT(1)
#define   FSI_MRESP0_RESET_ALL_BRIDGES_GENERAL  BE_BIT(2)
#define   FSI_MRESP0_RESET_ALL_PORTS_GENERAL    BE_BIT(3)
#define   FSI_MRESP0_RESET_MASTER               BE_BIT(4)
#define   FSI_MRESP0_RESET_PARITY_ERROR_LATCH   BE_BIT(5)

#define FSI_MRESB0                              TO_REG(0x1d0)
#define   FSI_MRESB0_RESET_GENERAL              BE_BIT(0)
#define   FSI_MRESB0_RESET_ERROR                BE_BIT(1)
#define   FSI_MRESB0_SET_DMA_SUSPEND            BE_BIT(5)
#define   FSI_MRESB0_CLEAR_DMA_SUSPEND          BE_BIT(6)
#define   FSI_MRESB0_SET_DELAY_MEASURE          BE_BIT(7)

#define FSI_MECTRL                              TO_REG(0x2e0)
#define   FSI_MECTRL_TEST_PULSE                 BE_GENMASK(0, 7)
#define   FSI_MECTRL_INHIBIT_PARITY_ERROR       BE_GENMASK(8, 15)
#define   FSI_MECTRL_ENABLE_OPB_ERR_ACK         BE_BIT(16)
#define   FSI_MECTRL_AUTO_TERMINATE             BE_BIT(17)
#define   FSI_MECTRL_PORT_ERROR_FREEZE          BE_BIT(18)

static uint64_t fsi_master_read(void *opaque, hwaddr addr, unsigned size)
{
    FSIMasterState *s = FSI_MASTER(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: read @0x%" HWADDR_PRIx " size=%d\n",
                  __func__, addr, size);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds read: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return 0;
    }

    return s->regs[TO_REG(addr)];
}

static void fsi_master_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    FSIMasterState *s = FSI_MASTER(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: write @0x%" HWADDR_PRIx " size=%d "
                  "value=%"PRIx64"\n", __func__, addr, size, data);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds write: %"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return;
    }

    switch (TO_REG(addr)) {
    case FSI_MENP0:
        s->regs[FSI_MENP0] = data;
        break;
    case FSI_MENP32:
        s->regs[FSI_MENP32] = data;
        break;
    case FSI_MSENP0:
        s->regs[FSI_MENP0] |= data;
        break;
    case FSI_MSENP32:
        s->regs[FSI_MENP32] |= data;
        break;
    case FSI_MCENP0:
        s->regs[FSI_MENP0] &= ~data;
        break;
    case FSI_MCENP32:
        s->regs[FSI_MENP32] &= ~data;
        break;
    case FSI_MRESP0:
        /* Perform necessary resets leave register 0 to indicate no errors */
        break;
    case FSI_MRESB0:
        if (data & FSI_MRESB0_RESET_GENERAL) {
            device_cold_reset(DEVICE(opaque));
        }
        if (data & FSI_MRESB0_RESET_ERROR) {
            /* FIXME: this seems dubious */
            device_cold_reset(DEVICE(opaque));
        }
        break;
    default:
        s->regs[TO_REG(addr)] = data;
    }
}

static const struct MemoryRegionOps fsi_master_ops = {
    .read = fsi_master_read,
    .write = fsi_master_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void fsi_master_realize(DeviceState *dev, Error **errp)
{
    FSIMasterState *s = FSI_MASTER(dev);
    Error *err = NULL;

    qbus_init(&s->bus, sizeof(s->bus), TYPE_FSI_BUS, DEVICE(s), NULL);

    memory_region_init_io(&s->iomem, OBJECT(s), &fsi_master_ops, s,
                          TYPE_FSI_MASTER, 0x10000000);
    memory_region_init(&s->opb2fsi, OBJECT(s), "fsi.opb2fsi", 0x10000000);

    object_property_set_bool(OBJECT(&s->bus), "realized", true, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->opb2fsi, 0, &s->bus.slave.mr);
}

static void fsi_master_reset(DeviceState *dev)
{
    FSIMasterState *s = FSI_MASTER(dev);

    /* ASPEED default */
    s->regs[FSI_MVER] = 0xe0050101;
}

static void fsi_master_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_OP_BUS;
    dc->desc = "FSI Master";
    dc->realize = fsi_master_realize;
    dc->reset = fsi_master_reset;
}

static const TypeInfo fsi_master_info = {
    .name = TYPE_FSI_MASTER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FSIMasterState),
    .class_init = fsi_master_class_init,
};

static void fsi_register_types(void)
{
    type_register_static(&fsi_master_info);
}

type_init(fsi_register_types);
