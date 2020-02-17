/*
 * ARM mach-virt emulation
 *
 * Copyright (c) 2013 Linaro Limited
 * Copyright (c) 2020 Huawei.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/arm.h"
#include "hw/arm/microvm.h"
#include "sysemu/device_tree.h"
#include "sysemu/numa.h"
#include "sysemu/sysemu.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/arm/fdt.h"
#include "kvm_arm.h"

/* Number of external interrupt lines to configure the GIC with */
#define NUM_IRQS 256

#define PLATFORM_BUS_NUM_IRQS 64

/* Legacy RAM limit in GB (< version 4.0) */
#define LEGACY_RAMLIMIT_GB 255
#define LEGACY_RAMLIMIT_BYTES (LEGACY_RAMLIMIT_GB * GiB)

/* Addresses and sizes of our components.
 * 0..128MB is space for a flash device so we can run bootrom code such as UEFI.
 * 128MB..256MB is used for miscellaneous device I/O.
 * 256MB..1GB is reserved for possible future PCI support (ie where the
 * PCI memory window will go if we add a PCI host controller).
 * 1GB and up is RAM (which may happily spill over into the
 * high memory region beyond 4GB).
 * This represents a compromise between how much RAM can be given to
 * a 32 bit VM and leaving space for expansion and in particular for PCI.
 * Note that devices should generally be placed at multiples of 0x10000,
 * to accommodate guests using 64K pages.
 */
static MemMapEntry base_memmap[] = {
    /* Space up to 0x8000000 is reserved for a boot ROM */
    [VIRT_CPUPERIPHS] =         { 0x08000000, 0x00020000 },
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x08000000, 0x00010000 },
    [VIRT_GIC_CPU] =            { 0x08010000, 0x00010000 },
    /* This redistributor space allows up to 2*64kB*123 CPUs */
    [VIRT_GIC_REDIST] =         { 0x080A0000, 0x00F60000 },
    [VIRT_UART] =               { 0x09000000, 0x00001000 },
    [VIRT_RTC] =                { 0x09010000, 0x00001000 },
    [VIRT_MMIO] =               { 0x0a000000, 0x00000200 },
    /* Actual RAM size depends on initial RAM and device memory settings */
    [VIRT_MEM] =                { 0x40000000, LEGACY_RAMLIMIT_BYTES },
    /* Additional 64 MB redist region (can contain up to 512 redistributors) */
    [VIRT_HIGH_GIC_REDIST2] =   { 0x4000000000ULL, 0x4000000 },
};

static const int a15irqmap[] = {
    [VIRT_UART] = 1,
    [VIRT_RTC] = 2,
    [VIRT_MMIO] = 16, /* ...to 16 + NUM_VIRTIO_TRANSPORTS - 1 */
};

static void fdt_gic_intc_node(MicrovmMachineState *mms)
{
    char *nodename;
    ArmMachineState *ams = ARM_MACHINE(mms);

    if (ams->gic_version == 3) {
        return;
    }

    nodename = g_strdup_printf("/intc@%" PRIx64,
                               ams->memmap[VIRT_GIC_DIST].base);

    qemu_fdt_setprop_sized_cells(ams->fdt, nodename, "reg",
            2, ams->memmap[VIRT_GIC_DIST].base,
            2, ams->memmap[VIRT_GIC_DIST].size,
            2, ams->memmap[VIRT_GIC_CPU].base,
            2, ams->memmap[VIRT_GIC_CPU].size);

    g_free(nodename);
}


static void create_gic(MicrovmMachineState *mms)
{
    ArmMachineState *ams = ARM_MACHINE(mms);

    qdev_create_gic(ams);
    qdev_init_nofail(ams->gic);

    init_gic_sysbus(ams);
    fdt_add_gic_node(ams);
    fdt_gic_intc_node(mms);
}

static
void microvm_machine_done(Notifier *notifier, void *data)
{
    ArmMachineState *ams = container_of(notifier, ArmMachineState,
                                         machine_done);
    MachineState *ms = MACHINE(ams);
    ARMCPU *cpu = ARM_CPU(first_cpu);
    struct arm_boot_info *info = &ams->bootinfo;
    AddressSpace *as = arm_boot_address_space(cpu, info);

    if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms) < 0) {
        exit(1);
    }
}

static void microvm_init(MachineState *machine)
{
    ArmMachineState *ams = ARM_MACHINE(machine);
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *possible_cpus;
    MemoryRegion *sysmem = get_system_memory();
    int n, arm_max_cpus;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    bool aarch64 = true;
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int max_cpus = machine->smp.max_cpus;

    /* microvm, only support KVM */
    if (!kvm_enabled()) {
        error_report("microvm requires KVM");
        exit(1);
    }

    /* We can probe only here because during property set
     * KVM is not available yet
     */
    if (ams->gic_version <= 0) {
        ams->gic_version = kvm_arm_vgic_probe();
        if (!ams->gic_version) {
            error_report(
                "Unable to determine GIC version supported by host");
            exit(1);
        }
    }

    if (!cpu_type_valid(machine->cpu_type)) {
        error_report("mach-virt: CPU type %s not supported", machine->cpu_type);
        exit(1);
    }

    ams->psci_conduit = QEMU_PSCI_CONDUIT_HVC;

    /* The maximum number of CPUs depends on the GIC version, or on how
     * many redistributors we can fit into the memory map.
     */
    if (ams->gic_version == 3) {
        arm_max_cpus =
            ams->memmap[VIRT_GIC_REDIST].size / GICV3_REDIST_SIZE;
        arm_max_cpus +=
            ams->memmap[VIRT_HIGH_GIC_REDIST2].size / GICV3_REDIST_SIZE;
    } else {
        arm_max_cpus = GIC_NCPU;
    }

    if (max_cpus > arm_max_cpus) {
        error_report("Number of SMP CPUs requested (%d) exceeds max CPUs "
                     "supported by machine 'mach-microvm' (%d)",
                     max_cpus, arm_max_cpus);
        exit(1);
    }

    ams->smp_cpus = smp_cpus;

    create_fdt(ams);

    possible_cpus = mc->possible_cpu_arch_ids(machine);
    for (n = 0; n < possible_cpus->len; n++) {
        Object *cpuobj;
        CPUState *cs;

        if (n >= smp_cpus) {
            break;
        }

        cpuobj = object_new(possible_cpus->cpus[n].type);
        object_property_set_int(cpuobj, possible_cpus->cpus[n].arch_id,
                                "mp-affinity", NULL);

        cs = CPU(cpuobj);
        cs->cpu_index = n;

        numa_cpu_pre_plug(&possible_cpus->cpus[cs->cpu_index], DEVICE(cpuobj),
                          &error_fatal);

        aarch64 &= object_property_get_bool(cpuobj, "aarch64", NULL);

        object_property_set_int(cpuobj, ams->psci_conduit,
                                    "psci-conduit", NULL);

         /* Secondary CPUs start in PSCI powered-down state */
        if (n > 0) {
            object_property_set_bool(cpuobj, true,
                                     "start-powered-off", NULL);
        }

        if (object_property_find(cpuobj, "pmu", NULL)) {
            object_property_set_bool(cpuobj, false, "pmu", NULL);
        }

        if (object_property_find(cpuobj, "reset-cbar", NULL)) {
            object_property_set_int(cpuobj, ams->memmap[VIRT_CPUPERIPHS].base,
                                    "reset-cbar", &error_abort);
        }

        object_property_set_link(cpuobj, OBJECT(sysmem), "memory",
                                 &error_abort);

        object_property_set_bool(cpuobj, true, "realized", &error_fatal);
        object_unref(cpuobj);
    }
    fdt_add_timer_nodes(ams);
    fdt_add_cpu_nodes(ams);

    memory_region_allocate_system_memory(ram, NULL, "mach-virt.ram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, ams->memmap[VIRT_MEM].base, ram);

    create_gic(mms);

    create_uart(ams, VIRT_UART, sysmem, serial_hd(0));
    create_rtc(ams);

    /* Create mmio transports, so the user can create virtio backends
     * (which will be automatically plugged in to the transports). If
     * no backend is created the transport will just sit harmlessly idle.
     */
    create_virtio_devices(ams);

    ams->bootinfo.ram_size = machine->ram_size;
    ams->bootinfo.nb_cpus = smp_cpus;
    ams->bootinfo.board_id = -1;
    ams->bootinfo.loader_start = ams->memmap[VIRT_MEM].base;
    ams->bootinfo.get_dtb = machvirt_dtb;
    ams->bootinfo.skip_dtb_autoload = true;
    ams->bootinfo.firmware_loaded = false;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &ams->bootinfo);

    ams->machine_done.notify = microvm_machine_done;
    qemu_add_machine_init_done_notifier(&ams->machine_done);
}

static void microvm_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "QEMU ARM MicroVM Virtual Machine";
    mc->init = microvm_init;
    /* Start with max_cpus set to 512, which is the maximum supported by KVM.
     * The value may be reduced later when we have more information about the
     * configuration of the particular instance.
     */
    mc->max_cpus = 512;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("host");
    mc->default_machine_opts = "accel=kvm";
}

static void microvm_instance_init(Object *obj)
{
    ArmMachineState *ams = ARM_MACHINE(obj);

    ams->memmap = base_memmap;
    ams->irqmap = a15irqmap;
}

static const TypeInfo microvm_machine_info = {
    .name          = TYPE_MICROVM_MACHINE,
    .parent        = TYPE_ARM_MACHINE,
    .instance_size = sizeof(MicrovmMachineState),
    .instance_init = microvm_instance_init,
    .class_size    = sizeof(MicrovmMachineClass),
    .class_init    = microvm_class_init,
    .interfaces = (InterfaceInfo[]) {
         { }
    },
};

static void microvm_machine_init(void)
{
    type_register_static(&microvm_machine_info);
}

type_init(microvm_machine_init);
