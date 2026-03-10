/* Alpha cores and system support chips.  */

#ifndef HW_ALPHA_SYS_H
#define HW_ALPHA_SYS_H

#include "target/alpha/cpu-qom.h"
#include "hw/pci/pci.h"
#include "hw/core/boards.h"
#include "hw/intc/i8259.h"

#define TYPE_TYPHOON_PCI_HOST_BRIDGE "typhoon-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(TyphoonState, TYPHOON_PCI_HOST_BRIDGE)

PCIBus *typhoon_init(MemoryRegion *,
                     pci_map_irq_fn, uint8_t devfn_min, TyphoonState *);

#define TYPHOON_GPIO_ISA_IRQ "isa-irq"
#define TYPHOON_GPIO_RTC_IRQ "rtc-irq"

/* alpha_pci.c.  */
extern const MemoryRegionOps alpha_pci_ignore_ops;
extern const MemoryRegionOps alpha_pci_conf1_ops;
extern const MemoryRegionOps alpha_pci_iack_ops;

#endif
