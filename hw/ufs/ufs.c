/*
 * QEMU Universal Flash Storage (UFS) Controller
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "ufs.h"

/* The QEMU-UFS device follows spec version 3.1 */
#define UFS_SPEC_VER 0x00000310
#define UFS_MAX_NUTRS 32
#define UFS_MAX_NUTMRS 8

static void ufs_irq_check(UfsHc *u)
{
    PCIDevice *pci = PCI_DEVICE(u);
    uint32_t is = ldl_le_p(&u->reg.is);
    uint32_t ie = ldl_le_p(&u->reg.ie);

    if ((is & UFS_INTR_MASK) & ie) {
        trace_ufs_irq_raise();
        pci_irq_assert(pci);
    } else {
        trace_ufs_irq_lower();
        pci_irq_deassert(pci);
    }
}

static void ufs_process_uiccmd(UfsHc *u, uint32_t val)
{
    uint32_t is = ldl_le_p(&u->reg.is);
    uint32_t hcs = ldl_le_p(&u->reg.hcs);
    uint32_t ucmdarg1 = ldl_le_p(&u->reg.ucmdarg1);
    uint32_t ucmdarg2 = ldl_le_p(&u->reg.ucmdarg2);
    uint32_t ucmdarg3 = ldl_le_p(&u->reg.ucmdarg3);

    trace_ufs_process_uiccmd(val, ucmdarg1, ucmdarg2, ucmdarg3);
    /*
     * Only the essential uic commands for running drivers on Linux and Windows
     * are implemented.
     */
    switch (val) {
    case UIC_CMD_DME_LINK_STARTUP:
        hcs = FIELD_DP32(hcs, HCS, DP, 1);
        hcs = FIELD_DP32(hcs, HCS, UTRLRDY, 1);
        hcs = FIELD_DP32(hcs, HCS, UTMRLRDY, 1);
        ucmdarg2 = UIC_CMD_RESULT_SUCCESS;
        break;
    /* TODO: Revisit it when Power Management is implemented */
    case UIC_CMD_DME_HIBER_ENTER:
        is = FIELD_DP32(is, IS, UHES, 1);
        hcs = FIELD_DP32(hcs, HCS, UPMCRS, PWR_LOCAL);
        ucmdarg2 = UIC_CMD_RESULT_SUCCESS;
        break;
    case UIC_CMD_DME_HIBER_EXIT:
        is = FIELD_DP32(is, IS, UHXS, 1);
        hcs = FIELD_DP32(hcs, HCS, UPMCRS, PWR_LOCAL);
        ucmdarg2 = UIC_CMD_RESULT_SUCCESS;
        break;
    default:
        ucmdarg2 = UIC_CMD_RESULT_FAILURE;
    }

    is = FIELD_DP32(is, IS, UCCS, 1);

    stl_le_p(&u->reg.is, is);
    stl_le_p(&u->reg.hcs, hcs);
    stl_le_p(&u->reg.ucmdarg1, ucmdarg1);
    stl_le_p(&u->reg.ucmdarg2, ucmdarg2);
    stl_le_p(&u->reg.ucmdarg3, ucmdarg3);

    ufs_irq_check(u);
}

static void ufs_write_reg(UfsHc *u, hwaddr offset, uint32_t data, unsigned size)
{
    uint32_t is = ldl_le_p(&u->reg.is);
    uint32_t hcs = ldl_le_p(&u->reg.hcs);
    uint32_t hce = ldl_le_p(&u->reg.hce);
    uint32_t utrlcnr = ldl_le_p(&u->reg.utrlcnr);
    uint32_t utrlba, utmrlba;

    switch (offset) {
    case A_IS:
        is &= ~data;
        stl_le_p(&u->reg.is, is);
        ufs_irq_check(u);
        break;
    case A_IE:
        stl_le_p(&u->reg.ie, data);
        ufs_irq_check(u);
        break;
    case A_HCE:
        if (!FIELD_EX32(hce, HCE, HCE) && FIELD_EX32(data, HCE, HCE)) {
            hcs = FIELD_DP32(hcs, HCS, UCRDY, 1);
            hce = FIELD_DP32(hce, HCE, HCE, 1);
            stl_le_p(&u->reg.hcs, hcs);
            stl_le_p(&u->reg.hce, hce);
        } else if (FIELD_EX32(hce, HCE, HCE) && !FIELD_EX32(data, HCE, HCE)) {
            hcs = 0;
            hce = FIELD_DP32(hce, HCE, HCE, 0);
            stl_le_p(&u->reg.hcs, hcs);
            stl_le_p(&u->reg.hce, hce);
        }
        break;
    case A_UTRLBA:
        utrlba = data & R_UTRLBA_UTRLBA_MASK;
        stl_le_p(&u->reg.utrlba, utrlba);
        break;
    case A_UTRLBAU:
        stl_le_p(&u->reg.utrlbau, data);
        break;
    case A_UTRLDBR:
        /* Not yet supported */
        break;
    case A_UTRLRSR:
        stl_le_p(&u->reg.utrlrsr, data);
        break;
    case A_UTRLCNR:
        utrlcnr &= ~data;
        stl_le_p(&u->reg.utrlcnr, utrlcnr);
        break;
    case A_UTMRLBA:
        utmrlba = data & R_UTMRLBA_UTMRLBA_MASK;
        stl_le_p(&u->reg.utmrlba, utmrlba);
        break;
    case A_UTMRLBAU:
        stl_le_p(&u->reg.utmrlbau, data);
        break;
    case A_UICCMD:
        ufs_process_uiccmd(u, data);
        break;
    case A_UCMDARG1:
        stl_le_p(&u->reg.ucmdarg1, data);
        break;
    case A_UCMDARG2:
        stl_le_p(&u->reg.ucmdarg2, data);
        break;
    case A_UCMDARG3:
        stl_le_p(&u->reg.ucmdarg3, data);
        break;
    case A_UTRLCLR:
    case A_UTMRLDBR:
    case A_UTMRLCLR:
    case A_UTMRLRSR:
        trace_ufs_err_unsupport_register_offset(offset);
        break;
    default:
        trace_ufs_err_invalid_register_offset(offset);
        break;
    }
}

static uint64_t ufs_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    UfsHc *u = (UfsHc *)opaque;
    uint8_t *ptr = (uint8_t *)&u->reg;
    uint64_t value;

    if (addr > sizeof(u->reg) - size) {
        trace_ufs_err_invalid_register_offset(addr);
        return 0;
    }

    value = ldn_le_p(ptr + addr, size);
    trace_ufs_mmio_read(addr, value, size);
    return value;
}

static void ufs_mmio_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    UfsHc *u = (UfsHc *)opaque;

    if (addr > sizeof(u->reg) - size) {
        trace_ufs_err_invalid_register_offset(addr);
        return;
    }

    trace_ufs_mmio_write(addr, data, size);
    ufs_write_reg(u, addr, data, size);
}

static const MemoryRegionOps ufs_mmio_ops = {
    .read = ufs_mmio_read,
    .write = ufs_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static bool ufs_check_constraints(UfsHc *u, Error **errp)
{
    if (u->params.nutrs > UFS_MAX_NUTRS) {
        error_setg(errp, "nutrs must be less than or equal to %d",
                   UFS_MAX_NUTRS);
        return false;
    }

    if (u->params.nutmrs > UFS_MAX_NUTMRS) {
        error_setg(errp, "nutmrs must be less than or equal to %d",
                   UFS_MAX_NUTMRS);
        return false;
    }

    return true;
}

static void ufs_init_pci(UfsHc *u, PCIDevice *pci_dev)
{
    uint8_t *pci_conf = pci_dev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_conf, 0x1);

    memory_region_init_io(&u->iomem, OBJECT(u), &ufs_mmio_ops, u, "ufs",
                          u->reg_size);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &u->iomem);
    u->irq = pci_allocate_irq(pci_dev);
}

static void ufs_init_hc(UfsHc *u)
{
    uint32_t cap = 0;

    u->reg_size = pow2ceil(sizeof(UfsReg));

    memset(&u->reg, 0, sizeof(u->reg));
    cap = FIELD_DP32(cap, CAP, NUTRS, (u->params.nutrs - 1));
    cap = FIELD_DP32(cap, CAP, RTT, 2);
    cap = FIELD_DP32(cap, CAP, NUTMRS, (u->params.nutmrs - 1));
    cap = FIELD_DP32(cap, CAP, AUTOH8, 0);
    cap = FIELD_DP32(cap, CAP, 64AS, 1);
    cap = FIELD_DP32(cap, CAP, OODDS, 0);
    cap = FIELD_DP32(cap, CAP, UICDMETMS, 0);
    cap = FIELD_DP32(cap, CAP, CS, 0);
    stl_le_p(&u->reg.cap, cap);
    stl_le_p(&u->reg.ver, UFS_SPEC_VER);
}

static void ufs_realize(PCIDevice *pci_dev, Error **errp)
{
    UfsHc *u = UFS(pci_dev);

    if (!ufs_check_constraints(u, errp)) {
        return;
    }

    ufs_init_hc(u);
    ufs_init_pci(u, pci_dev);
}

static Property ufs_props[] = {
    DEFINE_PROP_STRING("serial", UfsHc, params.serial),
    DEFINE_PROP_UINT8("nutrs", UfsHc, params.nutrs, 32),
    DEFINE_PROP_UINT8("nutmrs", UfsHc, params.nutmrs, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription ufs_vmstate = {
    .name = "ufs",
    .unmigratable = 1,
};

static void ufs_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = ufs_realize;
    pc->vendor_id = PCI_VENDOR_ID_REDHAT;
    pc->device_id = PCI_DEVICE_ID_REDHAT_UFS;
    pc->class_id = PCI_CLASS_STORAGE_UFS;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Universal Flash Storage";
    device_class_set_props(dc, ufs_props);
    dc->vmsd = &ufs_vmstate;
}

static const TypeInfo ufs_info = {
    .name = TYPE_UFS,
    .parent = TYPE_PCI_DEVICE,
    .class_init = ufs_class_init,
    .instance_size = sizeof(UfsHc),
    .interfaces = (InterfaceInfo[]){ { INTERFACE_PCIE_DEVICE }, {} },
};

static void ufs_register_types(void)
{
    type_register_static(&ufs_info);
}

type_init(ufs_register_types)
