#ifndef QEMU_VHOST_PCI_SLAVE_H
#define QEMU_VHOST_PCI_SLAVE_H

#include "sysemu/char.h"

typedef struct VhostPCISlave {
    CharBackend chr_be;
} VhostPCISlave;

extern VhostPCISlave *vp_slave;

extern int vhost_pci_slave_init(QemuOpts *opts);

extern int vhost_pci_slave_cleanup(void);

#endif
