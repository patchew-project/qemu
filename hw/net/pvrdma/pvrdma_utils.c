#include <qemu/osdep.h>
#include <cpu.h>
#include <hw/pci/pci.h>
#include <hw/net/pvrdma/pvrdma_utils.h>
#include <hw/net/pvrdma/pvrdma.h>

void pvrdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len)
{
    pr_dbg("%p\n", buffer);
    pci_dma_unmap(dev, buffer, len, DMA_DIRECTION_TO_DEVICE, 0);
}

void *pvrdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen)
{
    void *p;
    hwaddr len = plen;

    if (!addr) {
        pr_dbg("addr is NULL\n");
        return NULL;
    }

    p = pci_dma_map(dev, addr, &len, DMA_DIRECTION_TO_DEVICE);
    if (!p) {
        return NULL;
    }

    if (len != plen) {
        pvrdma_pci_dma_unmap(dev, p, len);
        return NULL;
    }

    pr_dbg("0x%llx -> %p (len=%ld)\n", (long long unsigned int)addr, p, len);

    return p;
}
