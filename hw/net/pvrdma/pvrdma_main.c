#include <qemu/osdep.h>
#include <hw/hw.h>
#include <hw/pci/pci.h>
#include <hw/pci/pci_ids.h>
#include <hw/pci/msi.h>
#include <hw/pci/msix.h>
#include <hw/qdev-core.h>
#include <hw/qdev-properties.h>
#include <cpu.h>

#include "hw/net/pvrdma/pvrdma.h"
#include "hw/net/pvrdma/pvrdma_defs.h"
#include "hw/net/pvrdma/pvrdma_utils.h"
#include "hw/net/pvrdma/pvrdma_dev_api.h"
#include "hw/net/pvrdma/pvrdma_rm.h"
#include "hw/net/pvrdma/pvrdma_kdbr.h"
#include "hw/net/pvrdma/pvrdma_qp_ops.h"

static Property pvrdma_dev_properties[] = {
    DEFINE_PROP_UINT64("sys-image-guid", PVRDMADev, sys_image_guid, 0),
    DEFINE_PROP_UINT64("node-guid", PVRDMADev, node_guid, 0),
    DEFINE_PROP_UINT64("network-prefix", PVRDMADev, network_prefix, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void free_dev_ring(PCIDevice *pci_dev, Ring *ring, void *ring_state)
{
    ring_free(ring);
    pvrdma_pci_dma_unmap(pci_dev, ring_state, TARGET_PAGE_SIZE);
}

static int init_dev_ring(Ring *ring, struct pvrdma_ring **ring_state,
                         const char *name, PCIDevice *pci_dev,
                         dma_addr_t dir_addr, u32 num_pages)
{
    __u64 *dir, *tbl;
    int rc = 0;

    pr_dbg("Initializing device ring %s\n", name);
    pr_dbg("pdir_dma=0x%llx\n", (long long unsigned int)dir_addr);
    pr_dbg("num_pages=%d\n", num_pages);
    dir = pvrdma_pci_dma_map(pci_dev, dir_addr, TARGET_PAGE_SIZE);
    if (!dir) {
        pr_err("Fail to map to page directory\n");
        rc = -ENOMEM;
        goto out;
    }
    tbl = pvrdma_pci_dma_map(pci_dev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        pr_err("Fail to map to page table\n");
        rc = -ENOMEM;
        goto out_free_dir;
    }

    *ring_state = pvrdma_pci_dma_map(pci_dev, tbl[0], TARGET_PAGE_SIZE);
    if (!*ring_state) {
        pr_err("Fail to map to ring state\n");
        rc = -ENOMEM;
        goto out_free_tbl;
    }
    /* RX ring is the second */
    (struct pvrdma_ring *)(*ring_state)++;
    rc = ring_init(ring, name, pci_dev, (struct pvrdma_ring *)*ring_state,
                   (num_pages - 1) * TARGET_PAGE_SIZE /
                   sizeof(struct pvrdma_cqne), sizeof(struct pvrdma_cqne),
                   (dma_addr_t *)&tbl[1], (dma_addr_t)num_pages - 1);
    if (rc != 0) {
        pr_err("Fail to initialize ring\n");
        rc = -ENOMEM;
        goto out_free_ring_state;
    }

    goto out_free_tbl;

out_free_ring_state:
    pvrdma_pci_dma_unmap(pci_dev, *ring_state, TARGET_PAGE_SIZE);

out_free_tbl:
    pvrdma_pci_dma_unmap(pci_dev, tbl, TARGET_PAGE_SIZE);

out_free_dir:
    pvrdma_pci_dma_unmap(pci_dev, dir, TARGET_PAGE_SIZE);

out:
    return rc;
}

static void free_dsr(PVRDMADev *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);

    if (!dev->dsr_info.dsr) {
        return;
    }

    free_dev_ring(pci_dev, &dev->dsr_info.async,
                  dev->dsr_info.async_ring_state);

    free_dev_ring(pci_dev, &dev->dsr_info.cq, dev->dsr_info.cq_ring_state);

    pvrdma_pci_dma_unmap(pci_dev, dev->dsr_info.req,
                         sizeof(union pvrdma_cmd_req));

    pvrdma_pci_dma_unmap(pci_dev, dev->dsr_info.rsp,
                         sizeof(union pvrdma_cmd_resp));

    pvrdma_pci_dma_unmap(pci_dev, dev->dsr_info.dsr,
                         sizeof(struct pvrdma_device_shared_region));

    dev->dsr_info.dsr = NULL;
}

static int load_dsr(PVRDMADev *dev)
{
    int rc = 0;
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    DSRInfo *dsr_info;
    struct pvrdma_device_shared_region *dsr;

    free_dsr(dev);

    /* Map to DSR */
    pr_dbg("dsr_dma=0x%llx\n", (long long unsigned int)dev->dsr_info.dma);
    dev->dsr_info.dsr = pvrdma_pci_dma_map(pci_dev, dev->dsr_info.dma,
                                sizeof(struct pvrdma_device_shared_region));
    if (!dev->dsr_info.dsr) {
        pr_err("Fail to map to DSR\n");
        rc = -ENOMEM;
        goto out;
    }

    /* Shortcuts */
    dsr_info = &dev->dsr_info;
    dsr = dsr_info->dsr;

    /* Map to command slot */
    pr_dbg("cmd_dma=0x%llx\n", (long long unsigned int)dsr->cmd_slot_dma);
    dsr_info->req = pvrdma_pci_dma_map(pci_dev, dsr->cmd_slot_dma,
                                       sizeof(union pvrdma_cmd_req));
    if (!dsr_info->req) {
        pr_err("Fail to map to command slot address\n");
        rc = -ENOMEM;
        goto out_free_dsr;
    }

    /* Map to response slot */
    pr_dbg("rsp_dma=0x%llx\n", (long long unsigned int)dsr->resp_slot_dma);
    dsr_info->rsp = pvrdma_pci_dma_map(pci_dev, dsr->resp_slot_dma,
                                       sizeof(union pvrdma_cmd_resp));
    if (!dsr_info->rsp) {
        pr_err("Fail to map to response slot address\n");
        rc = -ENOMEM;
        goto out_free_req;
    }

    /* Map to CQ notification ring */
    rc = init_dev_ring(&dsr_info->cq, &dsr_info->cq_ring_state, "dev_cq",
                       pci_dev, dsr->cq_ring_pages.pdir_dma,
                       dsr->cq_ring_pages.num_pages);
    if (rc != 0) {
        pr_err("Fail to map to initialize CQ ring\n");
        rc = -ENOMEM;
        goto out_free_rsp;
    }

    /* Map to event notification ring */
    rc = init_dev_ring(&dsr_info->async, &dsr_info->async_ring_state,
                       "dev_async", pci_dev, dsr->async_ring_pages.pdir_dma,
                       dsr->async_ring_pages.num_pages);
    if (rc != 0) {
        pr_err("Fail to map to initialize event ring\n");
        rc = -ENOMEM;
        goto out_free_rsp;
    }

    goto out;

out_free_rsp:
    pvrdma_pci_dma_unmap(pci_dev, dsr_info->rsp, sizeof(union pvrdma_cmd_resp));

out_free_req:
    pvrdma_pci_dma_unmap(pci_dev, dsr_info->req, sizeof(union pvrdma_cmd_req));

out_free_dsr:
    pvrdma_pci_dma_unmap(pci_dev, dsr_info->dsr,
                         sizeof(struct pvrdma_device_shared_region));
    dsr_info->dsr = NULL;

out:
    return rc;
}

static void init_dev_caps(PVRDMADev *dev)
{
    struct pvrdma_device_shared_region *dsr;

    if (dev->dsr_info.dsr == NULL) {
        pr_err("Can't initialized DSR\n");
        return;
    }

    dsr = dev->dsr_info.dsr;

    dsr->caps.fw_ver = PVRDMA_FW_VERSION;
    pr_dbg("fw_ver=0x%lx\n", dsr->caps.fw_ver);

    dsr->caps.mode = PVRDMA_DEVICE_MODE_ROCE;
    pr_dbg("mode=%d\n", dsr->caps.mode);

    dsr->caps.gid_types |= PVRDMA_GID_TYPE_FLAG_ROCE_V1;
    pr_dbg("gid_types=0x%x\n", dsr->caps.gid_types);

    dsr->caps.max_uar = RDMA_BAR2_UAR_SIZE;
    pr_dbg("max_uar=%d\n", dsr->caps.max_uar);

    if (rm_get_max_pds(&dsr->caps.max_pd)) {
        return;
    }
    pr_dbg("max_pd=%d\n", dsr->caps.max_pd);

    if (rm_get_max_gids(&dsr->caps.gid_tbl_len)) {
        return;
    }
    pr_dbg("gid_tbl_len=%d\n", dsr->caps.gid_tbl_len);

    if (rm_get_max_cqs(&dsr->caps.max_cq)) {
        return;
    }
    pr_dbg("max_cq=%d\n", dsr->caps.max_cq);

    if (rm_get_max_cqes(&dsr->caps.max_cqe)) {
        return;
    }
    pr_dbg("max_cqe=%d\n", dsr->caps.max_cqe);

    if (rm_get_max_qps(&dsr->caps.max_qp)) {
        return;
    }
    pr_dbg("max_qp=%d\n", dsr->caps.max_qp);

    dsr->caps.sys_image_guid = cpu_to_be64(dev->sys_image_guid);
    pr_dbg("sys_image_guid=%llx\n",
           (long long unsigned int)be64_to_cpu(dsr->caps.sys_image_guid));

    dsr->caps.node_guid = cpu_to_be64(dev->node_guid);
    pr_dbg("node_guid=%llx\n",
           (long long unsigned int)be64_to_cpu(dsr->caps.node_guid));

    if (rm_get_phys_port_cnt(&dsr->caps.phys_port_cnt)) {
        return;
    }
    pr_dbg("phys_port_cnt=%d\n", dsr->caps.phys_port_cnt);

    if (rm_get_max_qp_wrs(&dsr->caps.max_qp_wr)) {
        return;
    }
    pr_dbg("max_qp_wr=%d\n", dsr->caps.max_qp_wr);

    if (rm_get_max_sges(&dsr->caps.max_sge)) {
        return;
    }
    pr_dbg("max_sge=%d\n", dsr->caps.max_sge);

    if (rm_get_max_mrs(&dsr->caps.max_mr)) {
        return;
    }
    pr_dbg("max_mr=%d\n", dsr->caps.max_mr);

    if (rm_get_max_pkeys(&dsr->caps.max_pkeys)) {
        return;
    }
    pr_dbg("max_pkeys=%d\n", dsr->caps.max_pkeys);

    if (rm_get_max_ah(&dsr->caps.max_ah)) {
        return;
    }
    pr_dbg("max_ah=%d\n", dsr->caps.max_ah);

    pr_dbg("Initialized\n");
}

static void free_ports(PVRDMADev *dev)
{
    int i;

    for (i = 0; i < MAX_PORTS; i++) {
        free(dev->ports[i].gid_tbl);
        kdbr_free_port(dev->ports[i].kdbr_port);
    }
}

static int init_ports(PVRDMADev *dev)
{
    int i, ret = 0;
    __u32 max_port_gids;
    __u32 max_port_pkeys;

    memset(dev->ports, 0, sizeof(dev->ports));

    ret = rm_get_max_port_gids(&max_port_gids);
    if (ret != 0) {
        goto err;
    }

    ret = rm_get_max_port_pkeys(&max_port_pkeys);
    if (ret != 0) {
        goto err;
    }

    for (i = 0; i < MAX_PORTS; i++) {
        dev->ports[i].state = PVRDMA_PORT_DOWN;

        dev->ports[i].pkey_tbl = malloc(sizeof(*dev->ports[i].pkey_tbl) *
                                        max_port_pkeys);
        if (dev->ports[i].gid_tbl == NULL) {
            goto err_free_ports;
        }

        memset(dev->ports[i].gid_tbl, 0, sizeof(dev->ports[i].gid_tbl));
    }

    return 0;

err_free_ports:
    free_ports(dev);

err:
    pr_err("Fail to initialize device's ports\n");

    return ret;
}

static void activate_device(PVRDMADev *dev)
{
    set_reg_val(dev, PVRDMA_REG_ERR, 0);
    pr_dbg("Device activated\n");
}

static int quiesce_device(PVRDMADev *dev)
{
    pr_dbg("Device quiesced\n");
    return 0;
}

static int reset_device(PVRDMADev *dev)
{
    pr_dbg("Device reset complete\n");
    return 0;
}

static uint64_t regs_read(void *opaque, hwaddr addr, unsigned size)
{
    PVRDMADev *dev = opaque;
    __u32 val;

    /* pr_dbg("addr=0x%lx, size=%d\n", addr, size); */

    if (get_reg_val(dev, addr, &val)) {
        pr_dbg("Error trying to read REG value from address 0x%x\n",
               (__u32)addr);
        return -EINVAL;
    }

    /* pr_dbg("regs[0x%x]=0x%x\n", (__u32)addr, val); */

    return val;
}

static void regs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PVRDMADev *dev = opaque;

    /* pr_dbg("addr=0x%lx, val=0x%x, size=%d\n", addr, (uint32_t)val, size); */

    if (set_reg_val(dev, addr, val)) {
        pr_err("Error trying to set REG value, addr=0x%x, val=0x%lx\n",
               (__u32)addr, val);
        return;
    }

    /* pr_dbg("regs[0x%x]=0x%lx\n", (__u32)addr, val); */

    switch (addr) {
    case PVRDMA_REG_DSRLOW:
        dev->dsr_info.dma = val;
        break;
    case PVRDMA_REG_DSRHIGH:
        dev->dsr_info.dma |= val << 32;
        load_dsr(dev);
        init_dev_caps(dev);
        break;
    case PVRDMA_REG_CTL:
        switch (val) {
        case PVRDMA_DEVICE_CTL_ACTIVATE:
            activate_device(dev);
            break;
        case PVRDMA_DEVICE_CTL_QUIESCE:
            quiesce_device(dev);
            break;
        case PVRDMA_DEVICE_CTL_RESET:
            reset_device(dev);
            break;
        }
    case PVRDMA_REG_IMR:
        pr_dbg("Interrupt mask=0x%lx\n", val);
        dev->interrupt_mask = val;
        break;
    case PVRDMA_REG_REQUEST:
        if (val == 0) {
            execute_command(dev);
        }
    default:
        break;
    }
}

static const MemoryRegionOps regs_ops = {
    .read = regs_read,
    .write = regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = sizeof(uint32_t),
        .max_access_size = sizeof(uint32_t),
    },
};

static uint64_t uar_read(void *opaque, hwaddr addr, unsigned size)
{
    PVRDMADev *dev = opaque;
    __u32 val;

    pr_dbg("addr=0x%lx, size=%d\n", addr, size);

    if (get_uar_val(dev, addr, &val)) {
        pr_dbg("Error trying to read UAR value from address 0x%x\n",
               (__u32)addr);
        return -EINVAL;
    }

    pr_dbg("uar[0x%x]=0x%x\n", (__u32)addr, val);

    return val;
}

static void uar_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PVRDMADev *dev = opaque;

    /* pr_dbg("addr=0x%lx, val=0x%x, size=%d\n", addr, (uint32_t)val, size); */

    if (set_uar_val(dev, addr, val)) {
        pr_err("Error trying to set UAR value, addr=0x%x, val=0x%lx\n",
               (__u32)addr, val);
        return;
    }

    /* pr_dbg("uar[0x%x]=0x%lx\n", (__u32)addr, val); */

    switch (addr) {
    case PVRDMA_UAR_QP_OFFSET:
        pr_dbg("UAR QP command, addr=0x%x, val=0x%lx\n", (__u32)addr, val);
        if (val & PVRDMA_UAR_QP_SEND) {
            qp_send(dev, val & PVRDMA_UAR_HANDLE_MASK);
        }
        if (val & PVRDMA_UAR_QP_RECV) {
            qp_recv(dev, val & PVRDMA_UAR_HANDLE_MASK);
        }
        break;
    case PVRDMA_UAR_CQ_OFFSET:
        pr_dbg("UAR CQ command, addr=0x%x, val=0x%lx\n", (__u32)addr, val);
        rm_req_notify_cq(dev, val & PVRDMA_UAR_HANDLE_MASK,
                 val & ~PVRDMA_UAR_HANDLE_MASK);
        break;
    default:
        pr_err("Unsupported command, addr=0x%x, val=0x%lx\n", (__u32)addr, val);
        break;
    }
}

static const MemoryRegionOps uar_ops = {
    .read = uar_read,
    .write = uar_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = sizeof(uint32_t),
        .max_access_size = sizeof(uint32_t),
    },
};

static void init_pci_config(PCIDevice *pdev)
{
    pdev->config[PCI_INTERRUPT_PIN] = 1;
}

static void init_bars(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    /* BAR 0 - MSI-X */
    memory_region_init(&dev->msix, OBJECT(dev), "pvrdma-msix",
                       RDMA_BAR0_MSIX_SIZE);
    pci_register_bar(pdev, RDMA_MSIX_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &dev->msix);

    /* BAR 1 - Registers */
    memset(&dev->regs_data, 0, RDMA_BAR1_REGS_SIZE);
    memory_region_init_io(&dev->regs, OBJECT(dev), &regs_ops, dev,
                          "pvrdma-regs", RDMA_BAR1_REGS_SIZE);
    pci_register_bar(pdev, RDMA_REG_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &dev->regs);

    /* BAR 2 - UAR */
    memset(&dev->uar_data, 0, RDMA_BAR2_UAR_SIZE);
    memory_region_init_io(&dev->uar, OBJECT(dev), &uar_ops, dev, "rdma-uar",
                          RDMA_BAR2_UAR_SIZE);
    pci_register_bar(pdev, RDMA_UAR_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &dev->uar);
}

static void init_regs(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    set_reg_val(dev, PVRDMA_REG_VERSION, PVRDMA_HW_VERSION);
    set_reg_val(dev, PVRDMA_REG_ERR, 0xFFFF);
}

static void uninit_msix(PCIDevice *pdev, int used_vectors)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);
    int i;

    for (i = 0; i < used_vectors; i++) {
        msix_vector_unuse(pdev, i);
    }

    msix_uninit(pdev, &dev->msix, &dev->msix);
}

static int init_msix(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);
    int i;
    int rc;

    rc = msix_init(pdev, RDMA_MAX_INTRS, &dev->msix, RDMA_MSIX_BAR_IDX,
                   RDMA_MSIX_TABLE, &dev->msix, RDMA_MSIX_BAR_IDX,
                   RDMA_MSIX_PBA, 0, NULL);

    if (rc < 0) {
        pr_err("Fail to initialize MSI-X\n");
        return rc;
    }

    for (i = 0; i < RDMA_MAX_INTRS; i++) {
        rc = msix_vector_use(PCI_DEVICE(dev), i);
        if (rc < 0) {
            pr_err("Fail mark MSI-X vercor %d\n", i);
            uninit_msix(pdev, i);
            return rc;
        }
    }

    return 0;
}

static int pvrdma_init(PCIDevice *pdev)
{
    int rc;
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    pr_info("Initializing device %s %x.%x\n", pdev->name,
            PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

    dev->dsr_info.dsr = NULL;

    init_pci_config(pdev);

    init_bars(pdev);

    init_regs(pdev);

    rc = init_msix(pdev);
    if (rc != 0) {
        goto out;
    }

    rc = kdbr_init();
    if (rc != 0) {
        goto out;
    }

    rc = rm_init(dev);
    if (rc != 0) {
        goto out;
    }

    rc = init_ports(dev);
    if (rc != 0) {
        goto out;
    }

    rc = qp_ops_init();
    if (rc != 0) {
        goto out;
    }

out:
    if (rc != 0) {
        pr_err("Device fail to load\n");
    }

    return rc;
}

static void pvrdma_exit(PCIDevice *pdev)
{
    PVRDMADev *dev = PVRDMA_DEV(pdev);

    pr_info("Closing device %s %x.%x\n", pdev->name,
            PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

    qp_ops_fini();

    free_ports(dev);

    rm_fini(dev);

    kdbr_fini();

    free_dsr(dev);

    if (msix_enabled(pdev)) {
        uninit_msix(pdev, RDMA_MAX_INTRS);
    }
}

static void pvrdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = pvrdma_init;
    k->exit = pvrdma_exit;
    k->vendor_id = PCI_VENDOR_ID_VMWARE;
    k->device_id = PCI_DEVICE_ID_VMWARE_PVRDMA;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_OTHER;

    dc->desc = "RDMA Device";
    dc->props = pvrdma_dev_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo pvrdma_info = {
    .name = PVRDMA_HW_NAME,
    .parent    = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PVRDMADev),
    .class_init = pvrdma_class_init,
};

static void register_types(void)
{
    type_register_static(&pvrdma_info);
}

type_init(register_types)
