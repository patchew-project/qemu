#ifndef QEMU_VHOST_PCI_SLAVE_H
#define QEMU_VHOST_PCI_SLAVE_H

#include <linux/vhost.h>

#include "sysemu/char.h"
#include "exec/memory.h"
#include "standard-headers/linux/vhost_pci_net.h"

typedef struct PeerVqNode {
    uint16_t last_avail_idx;
    uint32_t vring_num;
    int kickfd;
    int callfd;
    int enabled;
    struct vhost_vring_addr addr;
    QLIST_ENTRY(PeerVqNode) node;
} PeerVqNode;

typedef struct VhostPCISlave {
    CharBackend chr_be;
    uint16_t dev_type;
    uint64_t feature_bits;
    /* hotplugged memory should be mapped following the offset */
    uint64_t bar_map_offset;
    MemoryRegion *bar_mr;
    MemoryRegion *sub_mr;
    void *mr_map_base[MAX_GUEST_REGION];
    uint64_t mr_map_size[MAX_GUEST_REGION];
    struct peer_mem_msg pmem_msg;
    uint16_t pvq_num;
    QLIST_HEAD(, PeerVqNode) pvq_list;
} VhostPCISlave;

extern VhostPCISlave *vp_slave;

extern int vhost_pci_slave_init(QemuOpts *opts);

extern int vhost_pci_slave_cleanup(void);

#endif
