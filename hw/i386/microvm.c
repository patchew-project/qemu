/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Red Hat, Inc.
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
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpus.h"
#include "sysemu/numa.h"

#include "hw/loader.h"
#include "hw/nmi.h"
#include "hw/kvm/clock.h"
#include "hw/i386/microvm.h"
#include "hw/i386/pc.h"
#include "target/i386/cpu.h"
#include "hw/timer/i8254.h"
#include "hw/char/serial.h"
#include "hw/i386/topology.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/i386/mptable.h"

#include "cpu.h"
#include "elf.h"
#include "pvh.h"
#include "kvm_i386.h"
#include "hw/xen/start_info.h"

static void microvm_gsi_handler(void *opaque, int n, int level)
{
    qemu_irq *ioapic_irq = opaque;

    qemu_set_irq(ioapic_irq[n], level);
}

static void microvm_legacy_init(MicrovmMachineState *mms)
{
    ISABus *isa_bus;
    GSIState *gsi_state;
    qemu_irq *i8259;
    int i;

    assert(kvm_irqchip_in_kernel());
    gsi_state = g_malloc0(sizeof(*gsi_state));
    mms->gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);

    isa_bus = isa_bus_new(NULL, get_system_memory(), get_system_io(),
                          &error_abort);
    isa_bus_irqs(isa_bus, mms->gsi);

    assert(kvm_pic_in_kernel());
    i8259 = kvm_i8259_init(isa_bus);

    for (i = 0; i < ISA_NUM_IRQS; i++) {
        gsi_state->i8259_irq[i] = i8259[i];
    }

    kvm_pit_init(isa_bus, 0x40);

    for (i = 0; i < VIRTIO_NUM_TRANSPORTS; i++) {
        int nirq = VIRTIO_IRQ_BASE + i;
        ISADevice *isadev = isa_create(isa_bus, TYPE_ISA_SERIAL);
        qemu_irq mmio_irq;

        isa_init_irq(isadev, &mmio_irq, nirq);
        sysbus_create_simple("virtio-mmio",
                             VIRTIO_MMIO_BASE + i * 512,
                             mms->gsi[VIRTIO_IRQ_BASE + i]);
    }

    g_free(i8259);

    serial_hds_isa_init(isa_bus, 0, 1);
}

static void microvm_ioapic_init(MicrovmMachineState *mms)
{
    qemu_irq *ioapic_irq;
    DeviceState *ioapic_dev;
    SysBusDevice *d;
    int i;

    assert(kvm_irqchip_in_kernel());
    ioapic_irq = g_new0(qemu_irq, IOAPIC_NUM_PINS);
    kvm_pc_setup_irq_routing(true);

    assert(kvm_ioapic_in_kernel());
    ioapic_dev = qdev_create(NULL, "kvm-ioapic");

    object_property_add_child(qdev_get_machine(),
                              "ioapic", OBJECT(ioapic_dev), NULL);

    qdev_init_nofail(ioapic_dev);
    d = SYS_BUS_DEVICE(ioapic_dev);
    sysbus_mmio_map(d, 0, IO_APIC_DEFAULT_ADDRESS);

    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        ioapic_irq[i] = qdev_get_gpio_in(ioapic_dev, i);
    }

    mms->gsi = qemu_allocate_irqs(microvm_gsi_handler,
                                  ioapic_irq, IOAPIC_NUM_PINS);

    for (i = 0; i < VIRTIO_NUM_TRANSPORTS; i++) {
        sysbus_create_simple("virtio-mmio",
                             VIRTIO_MMIO_BASE + i * 512,
                             mms->gsi[VIRTIO_IRQ_BASE + i]);
    }
}

static void microvm_memory_init(MicrovmMachineState *mms)
{
    MachineState *machine = MACHINE(mms);
    MemoryRegion *ram, *ram_below_4g, *ram_above_4g;
    MemoryRegion *system_memory = get_system_memory();

    if (machine->ram_size > MICROVM_MAX_BELOW_4G) {
        mms->above_4g_mem_size = machine->ram_size - MICROVM_MAX_BELOW_4G;
        mms->below_4g_mem_size = MICROVM_MAX_BELOW_4G;
    } else {
        mms->above_4g_mem_size = 0;
        mms->below_4g_mem_size = machine->ram_size;
    }

    ram = g_malloc(sizeof(*ram));
    memory_region_allocate_system_memory(ram, NULL, "microvm.ram",
                                         machine->ram_size);

    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", ram,
                             0, mms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);

    e820_add_entry(0, mms->below_4g_mem_size, E820_RAM);

    if (mms->above_4g_mem_size > 0) {
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g", ram,
                                 mms->below_4g_mem_size,
                                 mms->above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, mms->above_4g_mem_size, E820_RAM);
    }
}

static void microvm_cpus_init(const char *typename, Error **errp)
{
    int i;

    for (i = 0; i < smp_cpus; i++) {
        Object *cpu = NULL;
        Error *local_err = NULL;

        cpu = object_new(typename);

        object_property_set_uint(cpu, i, "apic-id", &local_err);
        object_property_set_bool(cpu, true, "realized", &local_err);

        object_unref(cpu);
        error_propagate(errp, local_err);
    }
}

static void microvm_machine_state_init(MachineState *machine)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    Error *local_err = NULL;

    if (machine->kernel_filename == NULL) {
        error_report("missing kernel image file name, required by microvm");
        exit(1);
    }

    microvm_memory_init(mms);

    microvm_cpus_init(machine->cpu_type, &local_err);
    if (local_err) {
        error_report_err(local_err);
        exit(1);
    }

    if (mms->legacy) {
        microvm_legacy_init(mms);
    } else {
        microvm_ioapic_init(mms);
    }

    kvmclock_create();

    if (!pvh_load_elfboot(machine->kernel_filename, NULL, NULL)) {
        error_report("Error while loading elf kernel");
        exit(1);
    }

    if (machine->initrd_filename) {
        uint32_t initrd_max;
        gsize initrd_size;
        gchar *initrd_data;
        GError *gerr = NULL;

        if (!g_file_get_contents(machine->initrd_filename, &initrd_data,
                                 &initrd_size, &gerr)) {
            error_report("qemu: error reading initrd %s: %s\n",
                         machine->initrd_filename, gerr->message);
            exit(1);
        }

        initrd_max = mms->below_4g_mem_size - HIMEM_START;
        if (initrd_size >= initrd_max) {
            error_report("qemu: initrd is too large, cannot support."
                         "(max: %"PRIu32", need %"PRId64")\n",
                         initrd_max, (uint64_t)initrd_size);
            exit(1);
        }

        address_space_write(&address_space_memory,
                            HIMEM_START, MEMTXATTRS_UNSPECIFIED,
                            (uint8_t *) initrd_data, initrd_size);

        g_free(initrd_data);

        mms->initrd_addr = HIMEM_START;
        mms->initrd_size = initrd_size;
    }

    mms->elf_entry = pvh_get_start_addr();
}

static gchar *microvm_get_mmio_cmdline(gchar *name)
{
    gchar *cmdline;
    gchar *separator;
    long int index;
    int ret;

    separator = g_strrstr(name, ".");
    if (!separator) {
        return NULL;
    }

    if (qemu_strtol(separator + 1, NULL, 10, &index) != 0) {
        return NULL;
    }

    cmdline = g_malloc0(VIRTIO_CMDLINE_MAXLEN);
    ret = g_snprintf(cmdline, VIRTIO_CMDLINE_MAXLEN,
                     " virtio_mmio.device=512@0x%lx:%ld",
                     VIRTIO_MMIO_BASE + index * 512,
                     VIRTIO_IRQ_BASE + index);
    if (ret < 0 || ret >= VIRTIO_CMDLINE_MAXLEN) {
        g_free(cmdline);
        return NULL;
    }

    return cmdline;
}

static void microvm_setup_pvh(MicrovmMachineState *mms,
                              const gchar *kernel_cmdline)
{
    struct hvm_memmap_table_entry *memmap_table;
    struct hvm_start_info *start_info;
    BusState *bus;
    BusChild *kid;
    gchar *cmdline;
    int cmdline_len;
    int memmap_entries;
    int i;

    cmdline = g_strdup(kernel_cmdline);

    /*
     * Find MMIO transports with attached devices, and add them to the kernel
     * command line.
     */
    bus = sysbus_get_default();
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        ObjectClass *class = object_get_class(OBJECT(dev));

        if (class == object_class_by_name(TYPE_VIRTIO_MMIO)) {
            VirtIOMMIOProxy *mmio = VIRTIO_MMIO(OBJECT(dev));
            VirtioBusState *mmio_virtio_bus = &mmio->bus;
            BusState *mmio_bus = &mmio_virtio_bus->parent_obj;

            if (!QTAILQ_EMPTY(&mmio_bus->children)) {
                gchar *mmio_cmdline = microvm_get_mmio_cmdline(mmio_bus->name);
                if (mmio_cmdline) {
                    char *newcmd = g_strjoin(NULL, cmdline, mmio_cmdline, NULL);
                    g_free(mmio_cmdline);
                    g_free(cmdline);
                    cmdline = newcmd;
                }
            }
        }
    }

    cmdline_len = strlen(cmdline);

    address_space_write(&address_space_memory,
                        KERNEL_CMDLINE_START, MEMTXATTRS_UNSPECIFIED,
                        (uint8_t *) cmdline, cmdline_len);

    g_free(cmdline);

    memmap_entries = e820_get_num_entries();
    memmap_table = g_new0(struct hvm_memmap_table_entry, memmap_entries);
    for (i = 0; i < memmap_entries; i++) {
        uint64_t address, length;
        struct hvm_memmap_table_entry *entry = &memmap_table[i];

        if (e820_get_entry(i, E820_RAM, &address, &length)) {
            entry->addr = address;
            entry->size = length;
            entry->type = E820_RAM;
            entry->reserved = 0;
        }
    }

    address_space_write(&address_space_memory,
                        MEMMAP_START, MEMTXATTRS_UNSPECIFIED,
                        (uint8_t *) memmap_table,
                        memmap_entries * sizeof(struct hvm_memmap_table_entry));

    g_free(memmap_table);

    start_info = g_malloc0(sizeof(struct hvm_start_info));

    start_info->magic = XEN_HVM_START_MAGIC_VALUE;
    start_info->version = 1;

    start_info->nr_modules = 0;
    start_info->cmdline_paddr = KERNEL_CMDLINE_START;
    start_info->memmap_entries = memmap_entries;
    start_info->memmap_paddr = MEMMAP_START;

    if (mms->initrd_addr) {
        struct hvm_modlist_entry *entry = g_new0(struct hvm_modlist_entry, 1);

        entry->paddr = mms->initrd_addr;
        entry->size = mms->initrd_size;

        address_space_write(&address_space_memory,
                            MODLIST_START, MEMTXATTRS_UNSPECIFIED,
                            (uint8_t *) entry,
                            sizeof(struct hvm_modlist_entry));
        g_free(entry);

        start_info->nr_modules = 1;
        start_info->modlist_paddr = MODLIST_START;
    } else {
        start_info->nr_modules = 0;
    }

    address_space_write(&address_space_memory,
                        PVH_START_INFO, MEMTXATTRS_UNSPECIFIED,
                        (uint8_t *) start_info,
                        sizeof(struct hvm_start_info));

    g_free(start_info);
}

static void microvm_init_page_tables(void)
{
    uint64_t val = 0;
    int i;

    val = PDPTE_START | 0x03;
    address_space_write(&address_space_memory,
                        PML4_START, MEMTXATTRS_UNSPECIFIED,
                        (uint8_t *) &val, 8);
    val = PDE_START | 0x03;
    address_space_write(&address_space_memory,
                        PDPTE_START, MEMTXATTRS_UNSPECIFIED,
                        (uint8_t *) &val, 8);

    for (i = 0; i < 512; i++) {
        val = (i << 21) + 0x83;
        address_space_write(&address_space_memory,
                            PDE_START + (i * 8), MEMTXATTRS_UNSPECIFIED,
                            (uint8_t *) &val, 8);
    }
}

static void microvm_cpu_reset(CPUState *cs, uint64_t elf_entry)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    struct SegmentCache seg_code = { .selector = 0x8,
                                     .base = 0x0,
                                     .limit = 0xffffffff,
                                     .flags = 0xc09b00 };
    struct SegmentCache seg_data = { .selector = 0x10,
                                     .base = 0x0,
                                     .limit = 0xffffffff,
                                     .flags = 0xc09300 };
    struct SegmentCache seg_tr = { .selector = 0x18,
                                   .base = 0x0,
                                   .limit = 0xffff,
                                   .flags = 0x8b00 };

    memcpy(&env->segs[R_CS], &seg_code, sizeof(struct SegmentCache));
    memcpy(&env->segs[R_DS], &seg_data, sizeof(struct SegmentCache));
    memcpy(&env->segs[R_ES], &seg_data, sizeof(struct SegmentCache));
    memcpy(&env->segs[R_FS], &seg_data, sizeof(struct SegmentCache));
    memcpy(&env->segs[R_GS], &seg_data, sizeof(struct SegmentCache));
    memcpy(&env->segs[R_SS], &seg_data, sizeof(struct SegmentCache));
    memcpy(&env->tr, &seg_tr, sizeof(struct SegmentCache));

    env->regs[R_EBX] = PVH_START_INFO;

    cpu_set_pc(cs, elf_entry);
    cpu_x86_update_cr3(env, 0);
    cpu_x86_update_cr4(env, 0);
    cpu_x86_update_cr0(env, CR0_PE_MASK);

    x86_update_hflags(env);
}

static void microvm_mptable_setup(MicrovmMachineState *mms)
{
    char *mptable;
    int size;

    mptable = mptable_generate(smp_cpus, EBDA_START, &size);
    address_space_write(&address_space_memory,
                        EBDA_START, MEMTXATTRS_UNSPECIFIED,
                        (uint8_t *) mptable, size);
    g_free(mptable);
}

static bool microvm_machine_get_legacy(Object *obj, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    return mms->legacy;
}

static void microvm_machine_set_legacy(Object *obj, bool value, Error **errp)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);

    mms->legacy = value;
}

static void microvm_machine_reset(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MicrovmMachineState *mms = MICROVM_MACHINE(machine);
    CPUState *cs;
    X86CPU *cpu;

    qemu_devices_reset();

    microvm_mptable_setup(mms);
    microvm_setup_pvh(mms, machine->kernel_cmdline);
    microvm_init_page_tables();

    CPU_FOREACH(cs) {
        cpu = X86_CPU(cs);

        if (cpu->apic_state) {
            device_reset(cpu->apic_state);
        }

        microvm_cpu_reset(cs, mms->elf_entry);
    }
}

static void x86_nmi(NMIState *n, int cpu_index, Error **errp)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        X86CPU *cpu = X86_CPU(cs);

        if (!cpu->apic_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_NMI);
        } else {
            apic_deliver_nmi(cpu->apic_state);
        }
    }
}

static void microvm_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->init = microvm_machine_state_init;

    mc->family = "microvm_i386";
    mc->desc = "Microvm (i386)";
    mc->units_per_default_bus = 1;
    mc->no_floppy = 1;
    machine_class_allow_dynamic_sysbus_dev(mc, "sysbus-debugcon");
    machine_class_allow_dynamic_sysbus_dev(mc, "sysbus-debugexit");
    mc->max_cpus = 288;
    mc->has_hotpluggable_cpus = false;
    mc->auto_enable_numa_with_memhp = false;
    mc->default_cpu_type = X86_CPU_TYPE_NAME("host");
    mc->nvdimm_supported = false;
    mc->default_machine_opts = "accel=kvm";

    /* Machine class handlers */
    mc->reset = microvm_machine_reset;

    /* NMI handler */
    nc->nmi_monitor_handler = x86_nmi;

    object_class_property_add_bool(oc, MICROVM_MACHINE_LEGACY,
                                   microvm_machine_get_legacy,
                                   microvm_machine_set_legacy,
                                   &error_abort);
}

static const TypeInfo microvm_machine_info = {
    .name          = TYPE_MICROVM_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(MicrovmMachineState),
    .class_size    = sizeof(MicrovmMachineClass),
    .class_init    = microvm_class_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_NMI },
         { }
    },
};

static void microvm_machine_init(void)
{
    type_register_static(&microvm_machine_info);
}
type_init(microvm_machine_init);
