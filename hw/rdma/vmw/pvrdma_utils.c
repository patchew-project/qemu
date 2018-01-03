#include <qemu/osdep.h>
#include <qemu/error-report.h>

#include <cpu.h>
#include "../rdma_utils.h"
#include "pvrdma_utils.h"

void pvrdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len)
{
    pr_dbg("%p\n", buffer);
    if (buffer) {
        pci_dma_unmap(dev, buffer, len, DMA_DIRECTION_TO_DEVICE, 0);
    }
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
        pr_dbg("Fail in pci_dma_map, addr=0x%llx, len=%ld\n",
               (long long unsigned int)addr, len);
        return NULL;
    }

    if (len != plen) {
        pvrdma_pci_dma_unmap(dev, p, len);
        return NULL;
    }

    pr_dbg("0x%llx -> %p (len=%ld)\n", (long long unsigned int)addr, p, len);

    return p;
}

void *pvrdma_map_to_pdir(PCIDevice *pdev, uint64_t pdir_dma, uint32_t nchunks,
                         size_t length)
{
    uint64_t *dir = NULL, *tbl = NULL;
    int tbl_idx, dir_idx, addr_idx;
    void *host_virt = NULL, *curr_page;

    if (!nchunks) {
        pr_dbg("nchunks=0\n");
        goto out;
    }

    dir = pvrdma_pci_dma_map(pdev, pdir_dma, TARGET_PAGE_SIZE);
    if (!dir) {
        error_report("PVRDMA: Fail to map to page directory");
        goto out;
    }

    tbl = pvrdma_pci_dma_map(pdev, dir[0], TARGET_PAGE_SIZE);
    if (!tbl) {
        error_report("PVRDMA: Fail to map to page table 0");
        goto out_unmap_dir;
    }

    curr_page = pvrdma_pci_dma_map(pdev, (dma_addr_t)tbl[0], TARGET_PAGE_SIZE);
    if (!curr_page) {
        error_report("PVRDMA: Fail to map the first page");
        goto out_unmap_tbl;
    }

    host_virt = mremap(curr_page, 0, length, MREMAP_MAYMOVE);
    if (host_virt == MAP_FAILED) {
        host_virt = NULL;
        error_report("PVRDMA: Fail to remap memory for host_virt");
        goto out_unmap_tbl;
    }

    pvrdma_pci_dma_unmap(pdev, curr_page, TARGET_PAGE_SIZE);

    pr_dbg("host_virt=%p\n", host_virt);

    dir_idx = 0;
    tbl_idx = 1;
    addr_idx = 1;
    while (addr_idx < nchunks) {
        if ((tbl_idx == (TARGET_PAGE_SIZE / sizeof(uint64_t)))) {
            tbl_idx = 0;
            dir_idx++;
            pr_dbg("Mapping to table %d\n", dir_idx);
            pvrdma_pci_dma_unmap(pdev, tbl, TARGET_PAGE_SIZE);
            tbl = pvrdma_pci_dma_map(pdev, dir[dir_idx], TARGET_PAGE_SIZE);
            if (!tbl) {
                error_report("PVRDMA: Fail to map to page table %d", dir_idx);
                goto out_unmap_host_virt;
            }
        }

        pr_dbg("guest_dma[%d]=0x%lx\n", addr_idx, tbl[tbl_idx]);

        curr_page = pvrdma_pci_dma_map(pdev, (dma_addr_t)tbl[tbl_idx],
                                       TARGET_PAGE_SIZE);
        if (!curr_page) {
            error_report("PVRDMA: Fail to map to page %d, dir %d", tbl_idx,
                         dir_idx);
            goto out_unmap_host_virt;
        }

        mremap(curr_page, 0, TARGET_PAGE_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED,
               host_virt + TARGET_PAGE_SIZE * addr_idx);

        pvrdma_pci_dma_unmap(pdev, curr_page, TARGET_PAGE_SIZE);

        addr_idx++;

        tbl_idx++;
    }

    goto out_unmap_tbl;

out_unmap_host_virt:
    munmap(host_virt, length);
    host_virt = NULL;

out_unmap_tbl:
    pvrdma_pci_dma_unmap(pdev, tbl, TARGET_PAGE_SIZE);

out_unmap_dir:
    pvrdma_pci_dma_unmap(pdev, dir, TARGET_PAGE_SIZE);

out:
    return host_virt;

}
