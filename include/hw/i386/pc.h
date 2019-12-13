#ifndef HW_PC_H
#define HW_PC_H

#include "exec/memory.h"
#include "hw/boards.h"
#include "hw/isa/isa.h"
#include "hw/block/flash.h"
#include "hw/i386/ioapic.h"
#include "hw/i386/x86.h"

#include "qemu/range.h"
#include "qemu/bitmap.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/mem/pc-dimm.h"
#include "hw/mem/nvdimm.h"
#include "hw/acpi/acpi_dev_interface.h"

#define HPET_INTCAP "hpet-intcap"

/**
 * PCMachineState:
 * @acpi_dev: link to ACPI PM device that performs ACPI hotplug handling
 * @boot_cpus: number of present VCPUs
 * @smp_dies: number of dies per one package
 */
struct PCMachineState {
    /*< private >*/
    X86MachineState parent_obj;

    /* <public> */

    /* State for other subsystems/APIs: */
    Notifier machine_done;

    /* Pointers to devices and objects: */
    HotplugHandler *acpi_dev;
    PCIBus *bus;
    I2CBus *smbus;
    PFlashCFI01 *flash[2];

    /* Configuration options: */
    OnOffAuto vmport;
    OnOffAuto smm;

    bool acpi_build_enabled;
    bool smbus_enabled;
    bool sata_enabled;
    bool pit_enabled;

    /* NUMA information: */
    uint64_t numa_nodes;
    uint64_t *node_mem;

    /* ACPI Memory hotplug IO base address */
    hwaddr memhp_io_base;
};

/**
 * PCMachineClass:
 *
 * Compat fields:
 *
 * @enforce_aligned_dimm: check that DIMM's address/size is aligned by
 *                        backend's alignment value if provided
 * @acpi_data_size: Size of the chunk of memory at the top of RAM
 *                  for the BIOS ACPI tables and other BIOS
 *                  datastructures.
 * @gigabyte_align: Make sure that guest addresses aligned at
 *                  1Gbyte boundaries get mapped to host
 *                  addresses aligned at 1Gbyte boundaries. This
 *                  way we can use 1GByte pages in the host.
 *
 */
typedef struct PCMachineClass {
    /*< private >*/
    X86MachineClass parent_class;

    /*< public >*/

    /* Device configuration: */
    bool pci_enabled;
    bool kvmclock_enabled;
    const char *default_nic_model;

    /* Compat options: */

    /* Default CPU model version.  See x86_cpu_set_default_version(). */
    int default_cpu_version;

    /* ACPI compat: */
    bool has_acpi_build;
    bool rsdp_in_ram;
    int legacy_acpi_table_size;
    unsigned acpi_data_size;
    bool do_not_add_smb_acpi;

    /* SMBIOS compat: */
    bool smbios_defaults;
    bool smbios_legacy_mode;
    bool smbios_uuid_encoded;

    /* RAM / address space compat: */
    bool gigabyte_align;
    bool has_reserved_memory;
    bool enforce_aligned_dimm;
    bool broken_reserved_end;

    /* generate legacy CPU hotplug AML */
    bool legacy_cpu_hotplug;

    /* use DMA capable linuxboot option rom */
    bool linuxboot_dma_enabled;

    /* use PVH to load kernels that support this feature */
    bool pvh_enabled;
} PCMachineClass;

#define TYPE_PC_MACHINE "generic-pc-machine"
#define PC_MACHINE(obj) \
    OBJECT_CHECK(PCMachineState, (obj), TYPE_PC_MACHINE)
#define PC_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(PCMachineClass, (obj), TYPE_PC_MACHINE)
#define PC_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(PCMachineClass, (klass), TYPE_PC_MACHINE)

/* i8259.c */

extern DeviceState *isa_pic;
qemu_irq *i8259_init(ISABus *bus, qemu_irq parent_irq);
int pic_read_irq(DeviceState *d);
int pic_get_output(DeviceState *d);

/* ioapic.c */

/* Global System Interrupts */

#define GSI_NUM_PINS IOAPIC_NUM_PINS

typedef struct GSIState {
    qemu_irq i8259_irq[ISA_NUM_IRQS];
    qemu_irq ioapic_irq[IOAPIC_NUM_PINS];
} GSIState;

void gsi_handler(void *opaque, int n, int level);

GSIState *pc_gsi_create(qemu_irq **irqs, bool pci_enabled);

/* vmport.c */
#define TYPE_VMPORT "vmport"
typedef uint32_t (VMPortReadFunc)(void *opaque, uint32_t address);

static inline void vmport_init(ISABus *bus)
{
    isa_create_simple(bus, TYPE_VMPORT);
}

void vmport_register(unsigned char command, VMPortReadFunc *func, void *opaque);
void vmmouse_get_data(uint32_t *data);
void vmmouse_set_data(const uint32_t *data);

/* pc.c */
extern int fd_bootchk;

bool pc_machine_is_smm_enabled(PCMachineState *pcms);

#define PCI_HOST_PROP_PCI_HOLE_START   "pci-hole-start"
#define PCI_HOST_PROP_PCI_HOLE_END     "pci-hole-end"
#define PCI_HOST_PROP_PCI_HOLE64_START "pci-hole64-start"
#define PCI_HOST_PROP_PCI_HOLE64_END   "pci-hole64-end"
#define PCI_HOST_PROP_PCI_HOLE64_SIZE  "pci-hole64-size"
#define PCI_HOST_BELOW_4G_MEM_SIZE     "below-4g-mem-size"
#define PCI_HOST_ABOVE_4G_MEM_SIZE     "above-4g-mem-size"


void pc_pci_as_mapping_init(Object *owner, MemoryRegion *system_memory,
                            MemoryRegion *pci_address_space);

uint64_t pc_pci_hole64_start(void);
DeviceState *pc_vga_init(ISABus *isa_bus, PCIBus *pci_bus);

void pc_i8259_create(ISABus *isa_bus, qemu_irq *i8259_irqs);
void ioapic_init_gsi(GSIState *gsi_state, const char *parent_name);

#define FW_CFG_IO_BASE     0x510

#define PORT92_A20_LINE "a20"

/* hpet.c */
extern int no_hpet;

/* acpi-build.c */
void pc_madt_cpu_entry(AcpiDeviceIf *adev, int uid,
                       const CPUArchIdList *apic_ids, GArray *entry);

extern void igd_passthrough_isa_bridge_create(PCIBus *bus, uint16_t gpu_dev_id);
#endif
