/*
 * Phytium E2000 extension to the Synopsys DesignWare MCI
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sd/phytium_e2000_mci.h"

#include "qemu/bitops.h"
#include "qemu/module.h"

#define PHYTIUM_E2000_MCI_CCLK_RDY       0x058
#define PHYTIUM_E2000_MCI_CLK_DIVIDER    0x114
#define PHYTIUM_E2000_MCI_VERID          0x280a
#define PHYTIUM_E2000_MCI_HCON           (BIT(27) | BIT(7))
#define PHYTIUM_E2000_MCI_DATA_OFFSET    0x200
#define PHYTIUM_E2000_MCI_FIFO_DEPTH     512

struct PhytiumE2000MciState {
    DwMciState parent_obj;
};

/*
 * The vendor driver polls CCLK_RDY after programming its private divider.
 * QEMU has no controller clock tree, so completion is immediate while the
 * divider remains ordinary storage for firmware readback.
 */
static bool phytium_e2000_mci_read(DwMciState *s, hwaddr offset,
                                   uint64_t *value, unsigned size)
{
    if (size != sizeof(uint32_t)) {
        return false;
    }

    switch (offset) {
    case PHYTIUM_E2000_MCI_CCLK_RDY:
        *value = 1;
        return true;
    case PHYTIUM_E2000_MCI_CLK_DIVIDER:
        *value = s->regs[offset / sizeof(uint32_t)];
        return true;
    default:
        return false;
    }
}

static bool phytium_e2000_mci_write(DwMciState *s, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    if (size != sizeof(uint32_t)) {
        return false;
    }

    switch (offset) {
    case PHYTIUM_E2000_MCI_CCLK_RDY:
        return true;
    case PHYTIUM_E2000_MCI_CLK_DIVIDER:
        s->regs[offset / sizeof(uint32_t)] = value;
        return true;
    default:
        return false;
    }
}

static void phytium_e2000_mci_class_init(ObjectClass *klass, const void *data)
{
    DwMciClass *dmc = DW_MCI_CLASS(klass);

    dmc->verid = PHYTIUM_E2000_MCI_VERID;
    dmc->hcon = PHYTIUM_E2000_MCI_HCON;
    dmc->data_offset = PHYTIUM_E2000_MCI_DATA_OFFSET;
    dmc->fifo_depth = PHYTIUM_E2000_MCI_FIFO_DEPTH;
    dmc->vendor_read = phytium_e2000_mci_read;
    dmc->vendor_write = phytium_e2000_mci_write;
}

static const TypeInfo phytium_e2000_mci_info = {
    .name = TYPE_PHYTIUM_E2000_MCI,
    .parent = TYPE_DW_MCI,
    .instance_size = sizeof(PhytiumE2000MciState),
    .class_init = phytium_e2000_mci_class_init,
};

static void phytium_e2000_mci_register_types(void)
{
    type_register_static(&phytium_e2000_mci_info);
}

type_init(phytium_e2000_mci_register_types)
