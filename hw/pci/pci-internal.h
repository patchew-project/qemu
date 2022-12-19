#ifndef HW_PCI_PCI_INTERNAL_H
#define HW_PCI_PCI_INTERNAL_H

#include "qemu/queue.h"

typedef struct {
    uint16_t class;
    const char *desc;
    const char *fw_name;
    uint16_t fw_ign_bits;
} pci_class_desc;

typedef QLIST_HEAD(, PCIHostState) PCIHostStateList;

extern PCIHostStateList pci_host_bridges;

const pci_class_desc *get_class_desc(int class);
PCIBus *pci_find_bus_nr(PCIBus *bus, int bus_num);

#endif
