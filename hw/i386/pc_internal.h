#ifndef HW_I386_PC_INTERNAL_H
#define HW_I386_PC_INTERNAL_H

#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/i386/pc.h"
#include "hw/block/fdc.h"
#include "net/net.h"

#define PC_MACHINE_ACPI_DEVICE_PROP     "acpi-device"
#define PC_MACHINE_DEVMEM_REGION_SIZE   "device-memory-region-size"
#define PC_MACHINE_MAX_RAM_BELOW_4G     "max-ram-below-4g"
#define PC_MACHINE_VMPORT               "vmport"
#define PC_MACHINE_SMM                  "smm"
#define PC_MACHINE_SMBUS                "smbus"
#define PC_MACHINE_SATA                 "sata"
#define PC_MACHINE_PIT                  "pit"

void pc_register_ferr_irq(qemu_irq irq);
void pc_acpi_smi_interrupt(void *opaque, int irq, int level);

void pc_hot_add_cpu(MachineState *ms, const int64_t id, Error **errp);
void pc_smp_parse(MachineState *ms, QemuOpts *opts);

void pc_guest_info_init(PCMachineState *pcms);

void xen_load_linux(PCMachineState *pcms);
void pc_memory_init(PCMachineState *pcms,
                    MemoryRegion *system_memory,
                    MemoryRegion *rom_memory,
                    MemoryRegion **ram_memory);

void pc_basic_device_init(ISABus *isa_bus, qemu_irq *gsi,
                          ISADevice **rtc_state,
                          bool create_fdctrl,
                          bool no_vmport,
                          bool has_pit,
                          uint32_t hpet_irqs);
void pc_init_ne2k_isa(ISABus *bus, NICInfo *nd);
void pc_cmos_init(PCMachineState *pcms,
                  BusState *ide0, BusState *ide1,
                  ISADevice *s);
void pc_nic_init(PCMachineClass *pcmc, ISABus *isa_bus, PCIBus *pci_bus);

ISADevice *pc_find_fdc0(void);
int cmos_get_fd_drive_type(FloppyDriveType fd0);

#define FW_CFG_IO_BASE     0x510

/* pc_sysfw.c */
void pc_system_flash_create(PCMachineState *pcms);
void pc_system_firmware_init(PCMachineState *pcms, MemoryRegion *rom_memory);

extern GlobalProperty pc_compat_4_1[];
extern const size_t pc_compat_4_1_len;

extern GlobalProperty pc_compat_4_0[];
extern const size_t pc_compat_4_0_len;

extern GlobalProperty pc_compat_3_1[];
extern const size_t pc_compat_3_1_len;

extern GlobalProperty pc_compat_3_0[];
extern const size_t pc_compat_3_0_len;

extern GlobalProperty pc_compat_2_12[];
extern const size_t pc_compat_2_12_len;

extern GlobalProperty pc_compat_2_11[];
extern const size_t pc_compat_2_11_len;

extern GlobalProperty pc_compat_2_10[];
extern const size_t pc_compat_2_10_len;

extern GlobalProperty pc_compat_2_9[];
extern const size_t pc_compat_2_9_len;

extern GlobalProperty pc_compat_2_8[];
extern const size_t pc_compat_2_8_len;

extern GlobalProperty pc_compat_2_7[];
extern const size_t pc_compat_2_7_len;

extern GlobalProperty pc_compat_2_6[];
extern const size_t pc_compat_2_6_len;

extern GlobalProperty pc_compat_2_5[];
extern const size_t pc_compat_2_5_len;

extern GlobalProperty pc_compat_2_4[];
extern const size_t pc_compat_2_4_len;

extern GlobalProperty pc_compat_2_3[];
extern const size_t pc_compat_2_3_len;

extern GlobalProperty pc_compat_2_2[];
extern const size_t pc_compat_2_2_len;

extern GlobalProperty pc_compat_2_1[];
extern const size_t pc_compat_2_1_len;

extern GlobalProperty pc_compat_2_0[];
extern const size_t pc_compat_2_0_len;

extern GlobalProperty pc_compat_1_7[];
extern const size_t pc_compat_1_7_len;

extern GlobalProperty pc_compat_1_6[];
extern const size_t pc_compat_1_6_len;

extern GlobalProperty pc_compat_1_5[];
extern const size_t pc_compat_1_5_len;

extern GlobalProperty pc_compat_1_4[];
extern const size_t pc_compat_1_4_len;

/*
 * Helper for setting model-id for CPU models that changed model-id
 * depending on QEMU versions up to QEMU 2.4.
 */
#define PC_CPU_MODEL_IDS(v) \
    { "qemu32-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },\
    { "qemu64-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },\
    { "athlon-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },

#define DEFINE_PC_MACHINE(suffix, namestr, initfn, optsfn) \
    static void pc_machine_##suffix##_class_init(ObjectClass *oc, void *data) \
    { \
        MachineClass *mc = MACHINE_CLASS(oc); \
        optsfn(mc); \
        mc->init = initfn; \
    } \
    static const TypeInfo pc_machine_type_##suffix = { \
        .name       = namestr TYPE_MACHINE_SUFFIX, \
        .parent     = TYPE_PC_MACHINE, \
        .class_init = pc_machine_##suffix##_class_init, \
    }; \
    static void pc_machine_init_##suffix(void) \
    { \
        type_register(&pc_machine_type_##suffix); \
    } \
    type_init(pc_machine_init_##suffix)

#endif
