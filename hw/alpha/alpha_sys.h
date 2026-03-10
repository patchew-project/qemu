/* Alpha cores and system support chips.  */

#ifndef HW_ALPHA_SYS_H
#define HW_ALPHA_SYS_H

#include "target/alpha/cpu-qom.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/core/boards.h"
#include "hw/intc/i8259.h"

#define TYPE_TYPHOON_PCI_HOST_BRIDGE "typhoon-pcihost"

typedef struct TyphoonClass {
    PCIHostBridgeClass parent_class;

    pci_map_irq_fn sys_map_irq;
    uint8_t devfn_min;
} TyphoonClass;

OBJECT_DECLARE_TYPE(TyphoonState, TyphoonClass, TYPHOON_PCI_HOST_BRIDGE)

#define TYPHOON_PROP_RAM "ram"
#define TYPHOON_PCI_BUS_NAME "pci"

#define TYPHOON_GPIO_ISA_IRQ "isa-irq"
#define TYPHOON_GPIO_RTC_IRQ "rtc-irq"

/* alpha_pci.c.  */
extern const MemoryRegionOps alpha_pci_ignore_ops;
extern const MemoryRegionOps alpha_pci_conf1_ops;
extern const MemoryRegionOps alpha_pci_iack_ops;

#endif
