/*
 * QEMU Alpha DP264/CLIPPER hardware system emulator.
 *
 * Choose CLIPPER IRQ mappings over, say, DP264, MONET, or WEBBRICK
 * variants because CLIPPER doesn't have an SMC669 SuperIO controller
 * that we need to emulate as well.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/target_page.h"
#include "elf.h"
#include "hw/core/loader.h"
#include "alpha_sys.h"
#include "qemu/error-report.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/ide/pci.h"
#include "hw/isa/superio.h"
#include "qemu/datadir.h"

#define TYPE_CLIPPER_MACHINE MACHINE_TYPE_NAME("clipper")
OBJECT_DECLARE_SIMPLE_TYPE(ClipperMachineState, CLIPPER_MACHINE)

#define TYPE_TYPHOON_PCIHOST_CLIPPER "typhoon-pcihost-clipper"

struct ClipperMachineState {
    MachineState parent_obj;

    TyphoonState *typhoon;
};

static uint64_t cpu_alpha_superpage_to_phys(void *opaque, uint64_t addr)
{
    if (((addr >> 41) & 3) == 2) {
        addr &= 0xffffffffffull;
    }
    return addr;
}

/*
 * Note that there are at least 3 viewpoints of IRQ numbers on Alpha systems.
 *  (0) The dev_irq_n lines into the cpu, which we totally ignore,
 *  (1) The DRIR lines in the typhoon chipset,
 *  (2) The "vector" aka mangled interrupt number reported by SRM PALcode,
 *  (3) The interrupt number assigned by the kernel.
 * The following function is concerned with (1) only.
 */

static int clipper_pci_map_irq(PCIDevice *d, int irq_num)
{
    int slot = d->devfn >> 3;

    assert(irq_num >= 0 && irq_num <= 3);

    return (slot + 1) * 4 + irq_num;
}

static void clipper_init(MachineState *machine)
{
    ClipperMachineState *cms = CLIPPER_MACHINE(machine);
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    AlphaCPU *cpus[4];
    PCIBus *pci_bus;
    PCIDevice *pci_dev;
    DeviceState *i82378_dev;
    ISABus *isa_bus;
    long size, i;
    char *palcode_filename;
    uint64_t palcode_entry;
    uint64_t kernel_entry, kernel_low, kernel_high;
    unsigned int smp_cpus = machine->smp.cpus;

    Object * const typhoon_obj = object_new(TYPE_TYPHOON_PCIHOST_CLIPPER);
    TyphoonState * const typhoon = TYPHOON_PCI_HOST_BRIDGE(typhoon_obj);
    cms->typhoon = typhoon;
    object_property_add_child(OBJECT(machine), "typhoon", typhoon_obj);

    /* Create up to 4 cpus.  */
    memset(cpus, 0, sizeof(cpus));
    for (i = 0; i < smp_cpus; ++i) {
        g_autofree char *name = g_strdup_printf("cpu[%ld]", i);
        Object *cpu = object_new(machine->cpu_type);
        object_property_add_child(OBJECT(machine), name, cpu);
        object_property_set_link(typhoon_obj, name, cpu, &error_fatal);
        qdev_realize_and_unref(DEVICE(cpu), NULL, &error_fatal);
        cpus[i] = ALPHA_CPU(cpu);
    }

    /*
     * arg0 -> memory size
     * arg1 -> kernel entry point
     * arg2 -> config word
     *
     * Config word: bits 0-5 -> ncpus
     *              bit  6   -> nographics option (for HWRPB CTB)
     *
     * See init_hwrpb() in the PALcode.
     */
    cpus[0]->env.trap_arg0 = ram_size;
    cpus[0]->env.trap_arg1 = 0;
    cpus[0]->env.trap_arg2 = smp_cpus | (!machine->enable_graphics << 6);

    object_property_set_link(typhoon_obj, TYPHOON_PROP_RAM,
                             OBJECT(machine->ram), &error_fatal);

    /*
     * Init the chipset.  Because we're using CLIPPER IRQ mappings,
     * the minimum PCI device IdSel is 1.
     */
    pci_bus = typhoon_init(typhoon);

    /*
     * Init the PCI -> ISA bridge.
     *
     * Technically, PCI-based Alphas shipped with one of three different
     * PCI-ISA bridges:
     *
     * - Intel i82378 SIO
     * - Cypress CY82c693UB
     * - ALI M1533
     *
     * (An Intel i82375 PCI-EISA bridge was also used on some models.)
     *
     * For simplicity, we model an i82378 here, even though it wouldn't
     * have been on any Tsunami/Typhoon systems; it's close enough, and
     * we don't want to deal with modelling the CY82c693UB (which has
     * incompatible edge/level control registers, plus other peripherals
     * like IDE and USB) or the M1533 (which also has IDE and USB).
     *
     * Importantly, we need to provide a PCI device node for it, otherwise
     * some operating systems won't notice there's an ISA bus to configure.
     */
    i82378_dev = DEVICE(pci_create_simple(pci_bus, PCI_DEVFN(7, 0), "i82378"));
    isa_bus = ISA_BUS(qdev_get_child_bus(i82378_dev, "isa.0"));

    /* Connect the ISA PIC to the Typhoon IRQ used for ISA interrupts. */
    qdev_connect_gpio_out(i82378_dev, 0,
                          qdev_get_gpio_in_named(DEVICE(typhoon),
                                                 TYPHOON_GPIO_ISA_IRQ, 0));

    /* Since we have an SRM-compatible PALcode, use the SRM epoch.  */
    mc146818_rtc_init(isa_bus, 1900,
                      qdev_get_gpio_in_named(DEVICE(typhoon),
                                             TYPHOON_GPIO_RTC_IRQ, 0));

    /* VGA setup.  Don't bother loading the bios.  */
    pci_vga_init(pci_bus);

    /* Network setup.  e1000 is good enough, failing Tulip support.  */
    pci_init_nic_devices(pci_bus, mc->default_nic);

    /* Super I/O */
    isa_create_simple(isa_bus, TYPE_SMC37C669_SUPERIO);

    /* IDE disk setup.  */
    pci_dev = pci_create_simple(pci_bus, -1, "cmd646-ide");
    pci_ide_create_devs(pci_dev);

    /*
     * Load PALcode.  Given that this is not "real" cpu palcode,
     * but one explicitly written for the emulation, we might as
     * well load it directly from and ELF image.
     */
    palcode_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS,
                                      machine->firmware ?: "palcode-clipper");
    if (palcode_filename == NULL) {
        error_report("no palcode provided");
        exit(1);
    }
    size = load_elf(palcode_filename, NULL, cpu_alpha_superpage_to_phys,
                    NULL, &palcode_entry, NULL, NULL, NULL,
                    ELFDATA2LSB, EM_ALPHA, 0, 0);
    if (size < 0) {
        error_report("could not load palcode '%s'", palcode_filename);
        exit(1);
    }
    g_free(palcode_filename);

    /* Start all cpus at the PALcode RESET entry point.  */
    for (i = 0; i < smp_cpus; ++i) {
        cpus[i]->env.pc = palcode_entry;
        cpus[i]->env.palbr = palcode_entry;
    }

    /* Load a kernel.  */
    if (kernel_filename) {
        uint64_t param_offset;

        size = load_elf(kernel_filename, NULL, cpu_alpha_superpage_to_phys,
                        NULL, &kernel_entry, &kernel_low, &kernel_high, NULL,
                        ELFDATA2LSB, EM_ALPHA, 0, 0);
        if (size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }

        cpus[0]->env.trap_arg1 = kernel_entry;

        param_offset = kernel_low - 0x6000;

        if (kernel_cmdline) {
            pstrcpy_targphys("cmdline", param_offset, 0x100, kernel_cmdline);
        }

        if (initrd_filename) {
            hwaddr initrd_base;
            int64_t initrd_size;
            uint64_t initrd_info[2];

            initrd_size = get_image_size(initrd_filename, NULL);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }

            /* Put the initrd image as high in memory as possible.  */
            if (usub64_overflow(ram_size, initrd_size, &initrd_base)) {
                error_report("initial ram disk exceeds allotted ram size");
                exit(1);
            }
            initrd_base &= TARGET_PAGE_MASK;
            if (initrd_base <= kernel_high) {
                warn_report("initial ram disk overlaps with kernel");
            }

            load_image_targphys(initrd_filename, initrd_base,
                                ram_size - initrd_base, NULL);

            stq_le_p(&initrd_info[0], initrd_base + 0xfffffc0000000000ULL);
            stq_le_p(&initrd_info[1], initrd_size);

            rom_add_blob_fixed("initrd_info", initrd_info, sizeof(initrd_info),
                               param_offset + 0x100);
        }
    }
}

static void clipper_machine_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "Alpha DP264/CLIPPER";
    mc->init = clipper_init;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 4;
    mc->is_default = true;
    mc->default_cpu_type = ALPHA_CPU_TYPE_NAME("ev67");
    mc->default_ram_id = TYPHOON_PROP_RAM;
    mc->default_nic = "e1000";
}

static void typhoon_pcihost_clipper_class_init(ObjectClass *oc,
                                               const void *data)
{
    TyphoonClass *tc = TYPHOON_PCI_HOST_BRIDGE_CLASS(oc);

    tc->sys_map_irq = clipper_pci_map_irq;
    tc->devfn_min = PCI_DEVFN(1, 0);
}

static const TypeInfo clipper_types[] = {
    {
        .name = TYPE_CLIPPER_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(ClipperMachineState),
        .class_init = clipper_machine_init,
    },
    {
        .name = TYPE_TYPHOON_PCIHOST_CLIPPER,
        .parent = TYPE_TYPHOON_PCI_HOST_BRIDGE,
        .class_init = typhoon_pcihost_clipper_class_init,
    },
};

DEFINE_TYPES(clipper_types);
