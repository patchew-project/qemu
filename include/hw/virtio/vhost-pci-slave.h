#ifndef QEMU_VHOST_PCI_SLAVE_H
#define QEMU_VHOST_PCI_SLAVE_H

#include "linux-headers/linux/vhost.h"

#include "sysemu/char.h"
#include "exec/memory.h"

typedef struct Remoteq {
    uint16_t last_avail_idx;
    uint32_t vring_num;
    int kickfd;
    int callfd;
    int enabled;
    struct vhost_vring_addr addr;
    QLIST_ENTRY(Remoteq) node;
} Remoteq;

typedef struct RemoteMem {
    uint64_t gpa;
    uint64_t size;
} RemoteMem;

#define MAX_GUEST_REGION 8
/*
 * The basic vhost-pci device struct.
 * It is set up by vhost-pci-slave, and shared to the device emulation.
 */
typedef struct VhostPCIDev {
    /* Ponnter to the slave device */
    VirtIODevice *vdev;
    uint16_t dev_type;
    uint64_t feature_bits;
    /* Records the end (offset to the BAR) of the last mapped region */
    uint64_t bar_map_offset;
    /* The MemoryRegion that will be registered with a vhost-pci device BAR */
    MemoryRegion *bar_mr;
    /* Add to the bar MemoryRegion */
    MemoryRegion *sub_mr;
    void *mr_map_base[MAX_GUEST_REGION];
    uint64_t mr_map_size[MAX_GUEST_REGION];

    uint16_t remote_mem_num;
    RemoteMem remote_mem[MAX_GUEST_REGION];
    uint16_t remoteq_num;
    QLIST_HEAD(, Remoteq) remoteq_list;
} VhostPCIDev;

/* Currenltly, a slave supports the creation of only one vhost-pci device */
typedef struct VhostPCISlave {
    VhostPCIDev *vp_dev;
    CharBackend chr_be;
} VhostPCISlave;

extern int vhost_pci_slave_init(QemuOpts *opts);

extern int vhost_pci_slave_cleanup(void);

extern int vp_slave_send_feature_bits(uint64_t features);

VhostPCIDev *get_vhost_pci_dev(void);

#endif
