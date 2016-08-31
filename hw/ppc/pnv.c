/*
 * QEMU PowerPC PowerNV model
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2010 David Gibson, IBM Corporation.
 * Copyright (c) 2014-2016 BenH, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "hw/hw.h"
#include "target-ppc/cpu.h"
#include "qemu/log.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_core.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/cutils.h"

#include "hw/ppc/pnv_xscom.h"

#include <libfdt.h>

#define FDT_ADDR                0x01000000
#define FDT_MAX_SIZE            0x00100000

#define FW_FILE_NAME            "skiboot.lid"
#define FW_LOAD_ADDR            0x0
#define FW_MAX_SIZE             0x00400000

#define KERNEL_LOAD_ADDR        0x20000000
#define INITRD_LOAD_ADDR        0x40000000

/*
 * On Power Systems E880, the max cpus (threads) should be :
 *     4 * 4 sockets * 12 cores * 8 threads = 1536
 * Let's make it 2^11
 */
#define MAX_CPUS                2048

static void powernv_populate_memory_node(void *fdt, int chip_id, hwaddr start,
                                         hwaddr size)
{
    char *mem_name;
    uint64_t mem_reg_property[2];
    int off;

    mem_reg_property[0] = cpu_to_be64(start);
    mem_reg_property[1] = cpu_to_be64(size);

    mem_name = g_strdup_printf("memory@"TARGET_FMT_lx, start);
    off = fdt_add_subnode(fdt, 0, mem_name);
    g_free(mem_name);

    _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
    _FDT((fdt_setprop(fdt, off, "reg", mem_reg_property,
                       sizeof(mem_reg_property))));
    _FDT((fdt_setprop_cell(fdt, off, "ibm,chip-id", chip_id)));
}


/*
 * Memory nodes are created by hostboot, one for each range of memory
 * that has a different "affinity". In practice, it means one range
 * per chip.
 */
static int powernv_populate_memory(void *fdt)
{
    int chip_id = 0;
    hwaddr chip_ramsize = ram_size;
    hwaddr chip_start = 0;

    /* Only one chip for the moment */
    powernv_populate_memory_node(fdt, chip_id, chip_start, chip_ramsize);

    return 0;
}

/*
 * The PowerNV cores (and threads) need to use real HW ids and not an
 * incremental index like it has been done on other platforms. This HW
 * id is called a PIR and is used in the device tree, in the XSCOM
 * communication to address cores, in the interrupt servers.
 */
static void powernv_create_core_node(PnvCore *pc, void *fdt,
                                     int cpus_offset, int chip_id)
{
    CPUCore *core = CPU_CORE(pc);
    CPUState *cs = CPU(DEVICE(pc->threads));
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    int smt_threads = ppc_get_compat_smt_threads(cpu);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    uint32_t servers_prop[smt_threads];
    uint32_t gservers_prop[smt_threads * 2];
    int i;
    uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                       0xffffffff, 0xffffffff};
    uint32_t tbfreq = PNV_TIMEBASE_FREQ;
    uint32_t cpufreq = 1000000000;
    uint32_t page_sizes_prop[64];
    size_t page_sizes_prop_size;
    const uint8_t pa_features[] = { 24, 0,
                                    0xf6, 0x3f, 0xc7, 0xc0, 0x80, 0xf0,
                                    0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
                                    0x80, 0x00, 0x80, 0x00, 0x80, 0x00 };
    int offset;
    char *nodename;

    nodename = g_strdup_printf("%s@%x", dc->fw_name, core->core_id);
    offset = fdt_add_subnode(fdt, cpus_offset, nodename);
    _FDT(offset);
    g_free(nodename);

    _FDT((fdt_setprop_cell(fdt, offset, "ibm,chip-id", chip_id)));

    _FDT((fdt_setprop_cell(fdt, offset, "reg", core->core_id)));
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,pir", core->core_id)));
    _FDT((fdt_setprop_string(fdt, offset, "device_type", "cpu")));

    _FDT((fdt_setprop_cell(fdt, offset, "cpu-version", env->spr[SPR_PVR])));
    _FDT((fdt_setprop_cell(fdt, offset, "d-cache-block-size",
                            env->dcache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "d-cache-line-size",
                            env->dcache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "i-cache-block-size",
                            env->icache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "i-cache-line-size",
                            env->icache_line_size)));

    if (pcc->l1_dcache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "d-cache-size",
                               pcc->l1_dcache_size)));
    } else {
        error_report("Warning: Unknown L1 dcache size for cpu");
    }
    if (pcc->l1_icache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "i-cache-size",
                               pcc->l1_icache_size)));
    } else {
        error_report("Warning: Unknown L1 icache size for cpu");
    }

    _FDT((fdt_setprop_cell(fdt, offset, "timebase-frequency", tbfreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "clock-frequency", cpufreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,slb-size", env->slb_nr)));
    _FDT((fdt_setprop_string(fdt, offset, "status", "okay")));
    _FDT((fdt_setprop(fdt, offset, "64-bit", NULL, 0)));

    if (env->spr_cb[SPR_PURR].oea_read) {
        _FDT((fdt_setprop(fdt, offset, "ibm,purr", NULL, 0)));
    }

    if (env->mmu_model & POWERPC_MMU_1TSEG) {
        _FDT((fdt_setprop(fdt, offset, "ibm,processor-segment-sizes",
                           segs, sizeof(segs))));
    }

    /* Advertise VMX/VSX (vector extensions) if available
     *   0 / no property == no vector extensions
     *   1               == VMX / Altivec available
     *   2               == VSX available */
    if (env->insns_flags & PPC_ALTIVEC) {
        uint32_t vmx = (env->insns_flags2 & PPC2_VSX) ? 2 : 1;

        _FDT((fdt_setprop_cell(fdt, offset, "ibm,vmx", vmx)));
    }

    /* Advertise DFP (Decimal Floating Point) if available
     *   0 / no property == no DFP
     *   1               == DFP available */
    if (env->insns_flags2 & PPC2_DFP) {
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,dfp", 1)));
    }

    page_sizes_prop_size = ppc_create_page_sizes_prop(env, page_sizes_prop,
                                                  sizeof(page_sizes_prop));
    if (page_sizes_prop_size) {
        _FDT((fdt_setprop(fdt, offset, "ibm,segment-page-sizes",
                           page_sizes_prop, page_sizes_prop_size)));
    }

    _FDT((fdt_setprop(fdt, offset, "ibm,pa-features",
                       pa_features, sizeof(pa_features))));

    if (cpu->cpu_version) {
        _FDT((fdt_setprop_cell(fdt, offset, "cpu-version", cpu->cpu_version)));
    }

    /* Build interrupt servers and gservers properties */
    for (i = 0; i < smt_threads; i++) {
        servers_prop[i] = cpu_to_be32(core->core_id + i);
        /* Hack, direct the group queues back to cpu 0
         *
         * FIXME: check that we still need this hack with real HW
         * ids. Probably not.
         */
        gservers_prop[i * 2] = cpu_to_be32(core->core_id + i);
        gservers_prop[i * 2 + 1] = 0;
    }
    _FDT((fdt_setprop(fdt, offset, "ibm,ppc-interrupt-server#s",
                       servers_prop, sizeof(servers_prop))));
    _FDT((fdt_setprop(fdt, offset, "ibm,ppc-interrupt-gserver#s",
                       gservers_prop, sizeof(gservers_prop))));
}

static void *powernv_create_fdt(PnvMachineState *pnv,
                                const char *kernel_cmdline)
{
    void *fdt;
    char *buf;
    const char plat_compat[] = "qemu,powernv\0ibm,powernv";
    int off;
    int i;
    int cpus_offset;

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create_empty_tree(fdt, FDT_MAX_SIZE)));

    /* Root node */
    _FDT((fdt_setprop_cell(fdt, 0, "#address-cells", 0x2)));
    _FDT((fdt_setprop_cell(fdt, 0, "#size-cells", 0x2)));
    _FDT((fdt_setprop_string(fdt, 0, "model",
                             "IBM PowerNV (emulated by qemu)")));
    _FDT((fdt_setprop(fdt, 0, "compatible", plat_compat,
                      sizeof(plat_compat))));

    buf = g_strdup_printf(UUID_FMT, qemu_uuid[0], qemu_uuid[1],
                          qemu_uuid[2], qemu_uuid[3], qemu_uuid[4],
                          qemu_uuid[5], qemu_uuid[6], qemu_uuid[7],
                          qemu_uuid[8], qemu_uuid[9], qemu_uuid[10],
                          qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                          qemu_uuid[14], qemu_uuid[15]);
    _FDT((fdt_setprop_string(fdt, 0, "vm,uuid", buf)));
    g_free(buf);

    off = fdt_add_subnode(fdt, 0, "chosen");
    if (kernel_cmdline) {
        _FDT((fdt_setprop_string(fdt, off, "bootargs", kernel_cmdline)));
    }

    if (pnv->initrd_size) {
        uint32_t start_prop = cpu_to_be32(pnv->initrd_base);
        uint32_t end_prop = cpu_to_be32(pnv->initrd_base + pnv->initrd_size);

        _FDT((fdt_setprop(fdt, off, "linux,initrd-start",
                               &start_prop, sizeof(start_prop))));
        _FDT((fdt_setprop(fdt, off, "linux,initrd-end",
                               &end_prop, sizeof(end_prop))));
    }

    /* Memory */
    powernv_populate_memory(fdt);

    /* Populate XSCOM for each chip */
    for (i = 0; i < pnv->num_chips; i++) {
        xscom_populate_fdt(pnv->chips[i]->xscom, fdt, 0);
    }

    /* cpus */
    cpus_offset = fdt_add_subnode(fdt, 0, "cpus");
    _FDT(cpus_offset);
    _FDT((fdt_setprop_cell(fdt, cpus_offset, "#address-cells", 0x1)));
    _FDT((fdt_setprop_cell(fdt, cpus_offset, "#size-cells", 0x0)));

    for (i = 0; i < pnv->num_chips; i++) {
        PnvChip *chip = pnv->chips[i];
        int j;

        for (j = 0; j < chip->num_cores; j++) {
            powernv_create_core_node(&chip->cores[j], fdt, cpus_offset,
                                     chip->chip_id);
        }
    }

    return fdt;
}

static void ppc_powernv_reset(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    PnvMachineState *pnv = POWERNV_MACHINE(machine);
    void *fdt;

    pnv->fdt_addr = FDT_ADDR;

    qemu_devices_reset();

    fdt = powernv_create_fdt(pnv, machine->kernel_cmdline);

    cpu_physical_memory_write(pnv->fdt_addr, fdt, fdt_totalsize(fdt));
}

static void ppc_powernv_init(MachineState *machine)
{
    PnvMachineState *pnv = POWERNV_MACHINE(machine);
    ram_addr_t ram_size = machine->ram_size;
    MemoryRegion *ram;
    char *fw_filename;
    long fw_size;
    long kernel_size;
    int i;
    char *chip_typename;

    /* allocate RAM */
    if (ram_size < (1 * G_BYTE)) {
        error_report("Warning: skiboot may not work with < 1GB of RAM");
    }

    ram = g_new(MemoryRegion, 1);
    memory_region_allocate_system_memory(ram, NULL, "ppc_powernv.ram",
                                         ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    /* load skiboot firmware  */
    if (bios_name == NULL) {
        bios_name = FW_FILE_NAME;
    }

    fw_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    fw_size = load_image_targphys(fw_filename, FW_LOAD_ADDR, FW_MAX_SIZE);
    if (fw_size < 0) {
        hw_error("qemu: could not load OPAL '%s'\n", fw_filename);
        exit(1);
    }
    g_free(fw_filename);

    /* load kernel */
    kernel_size = load_image_targphys(machine->kernel_filename,
                                      KERNEL_LOAD_ADDR, 0x2000000);
    if (kernel_size < 0) {
        hw_error("qemu: could not load kernel'%s'\n", machine->kernel_filename);
        exit(1);
    }

    /* load initrd */
    if (machine->initrd_filename) {
        pnv->initrd_base = INITRD_LOAD_ADDR;
        pnv->initrd_size = load_image_targphys(machine->initrd_filename,
                                  pnv->initrd_base, 0x10000000); /* 128MB max */
        if (pnv->initrd_size < 0) {
            error_report("qemu: could not load initial ram disk '%s'",
                         machine->initrd_filename);
            exit(1);
        }
    }

    /* Create the processor chips */
    chip_typename = g_strdup_printf(TYPE_PNV_CHIP "-%s", machine->cpu_model);

    pnv->chips = g_new0(PnvChip *, pnv->num_chips);
    for (i = 0; i < pnv->num_chips; i++) {
        Object *chip = object_new(chip_typename);
        object_property_set_int(chip, CHIP_HWID(i), "chip-id", &error_abort);
        object_property_set_int(chip, smp_cores, "num-cores", &error_abort);
        /*
         * We could set a custom cores_mask for the chip here.
         */

        object_property_set_bool(chip, true, "realized", &error_abort);
        pnv->chips[i] = PNV_CHIP(chip);
    }
    g_free(chip_typename);
}

/* Allowed core identifiers on a POWER8 Processor Chip :
 *
 * <EX0 reserved>
 *  EX1  - Venice only
 *  EX2  - Venice only
 *  EX3  - Venice only
 *  EX4
 *  EX5
 *  EX6
 * <EX7,8 reserved> <reserved>
 *  EX9  - Venice only
 *  EX10 - Venice only
 *  EX11 - Venice only
 *  EX12
 *  EX13
 *  EX14
 * <EX15 reserved>
 */
#define POWER8E_CORE_MASK  (~0xffff8f8f)
#define POWER8_CORE_MASK   (~0xffff8181)

static void pnv_chip_power8nvl_realize(PnvChip *chip, Error **errp)
{
    ;
}

static void pnv_chip_power8nvl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->realize = pnv_chip_power8nvl_realize;
    k->cpu_model = "POWER8NVL";
    k->chip_type = PNV_CHIP_P8NVL;
    k->chip_f000f = 0x120d304980000000ull;
    k->cores_max = 12;
    k->cores_mask = POWER8_CORE_MASK;
    dc->desc = "PowerNV Chip POWER8NVL";
}

static const TypeInfo pnv_chip_power8nvl_info = {
    .name          = TYPE_PNV_CHIP_POWER8NVL,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChipPower8NVL),
    .class_init    = pnv_chip_power8nvl_class_init,
};

static void pnv_chip_power8_realize(PnvChip *chip, Error **errp)
{
    ;
}

static void pnv_chip_power8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->realize = pnv_chip_power8_realize;
    k->cpu_model = "POWER8";
    k->chip_type = PNV_CHIP_P8;
    k->chip_f000f = 0x220ea04980000000ull;
    k->cores_max = 12;
    k->cores_mask = POWER8_CORE_MASK;
    dc->desc = "PowerNV Chip POWER8";
}

static const TypeInfo pnv_chip_power8_info = {
    .name          = TYPE_PNV_CHIP_POWER8,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChipPower8),
    .class_init    = pnv_chip_power8_class_init,
};

static void pnv_chip_power8e_realize(PnvChip *chip, Error **errp)
{
    ;
}

static void pnv_chip_power8e_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->realize = pnv_chip_power8e_realize;
    k->cpu_model = "POWER8E";
    k->chip_type = PNV_CHIP_P8E;
    k->chip_f000f = 0x221ef04980000000ull;
    k->cores_max = 6;
    k->cores_mask = POWER8E_CORE_MASK;
    dc->desc = "PowerNV Chip POWER8E";
}

static const TypeInfo pnv_chip_power8e_info = {
    .name          = TYPE_PNV_CHIP_POWER8E,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChipPower8e),
    .class_init    = pnv_chip_power8e_class_init,
};

/*
 * This is different for POWER9 so we might need a ops in the chip to
 * calculate the core pirs
 */
#define P8_PIR(chip_id, core_id) (((chip_id) << 7) | ((core_id) << 3))

static void pnv_chip_realize(DeviceState *dev, Error **errp)
{
    PnvChip *chip = PNV_CHIP(dev);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    char *typename = pnv_core_typename(pcc->cpu_model);
    size_t typesize = object_type_get_instance_size(typename);
    int i, core_hwid;

    if (!object_class_by_name(typename)) {
        error_setg(errp, "Unable to find PowerNV CPU Core '%s'", typename);
        return;
    }

    /* Set up XSCOM bus */
    chip->xscom = xscom_create(chip);

    if (chip->num_cores > pcc->cores_max) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: too many cores for chip ! "
                      "Limiting to %d\n", __func__, pcc->cores_max);
        chip->num_cores = pcc->cores_max;
    }

    chip->cores = g_new0(PnvCore, chip->num_cores);

    /* no custom mask for this chip, let's use the default one from
     * the chip class */
    if (!chip->cores_mask) {
        chip->cores_mask = pcc->cores_mask;
    }

    for (i = 0, core_hwid = 0; (core_hwid < sizeof(chip->cores_mask) * 8)
             && (i < chip->num_cores); core_hwid++) {
        PnvCore *pnv_core = &chip->cores[i];
        DeviceState *qdev;

        if (!(chip->cores_mask & (1 << core_hwid))) {
            continue;
        }

        object_initialize(pnv_core, typesize, typename);
        object_property_set_int(OBJECT(pnv_core), smp_threads, "nr-threads",
                                &error_fatal);
        object_property_set_int(OBJECT(pnv_core),
                                P8_PIR(chip->chip_id, core_hwid),
                                CPU_CORE_PROP_CORE_ID, &error_fatal);
        object_property_set_bool(OBJECT(pnv_core), true, "realized",
                                 &error_fatal);
        object_unref(OBJECT(pnv_core));
        i++;

        /* Attach the core to its XSCOM bus */
        qdev = qdev_create(&chip->xscom->bus, TYPE_PNV_CORE_XSCOM);
        qdev_prop_set_uint32(qdev, "core-pir",
                             P8_PIR(chip->chip_id, core_hwid));
        qdev_init_nofail(qdev);

        pnv_core->xd = PNV_CORE_XSCOM(qdev);
    }
    g_free(typename);

    pcc->realize(chip, errp);
}

static Property pnv_chip_properties[] = {
    DEFINE_PROP_UINT32("chip-id", PnvChip, chip_id, 0),
    DEFINE_PROP_UINT32("num-cores", PnvChip, num_cores, 1),
    DEFINE_PROP_UINT32("cores-mask", PnvChip, cores_mask, 0x0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_chip_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_chip_realize;
    dc->props = pnv_chip_properties;
    dc->desc = "PowerNV Chip";
 }

static const TypeInfo pnv_chip_info = {
    .name          = TYPE_PNV_CHIP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .class_init    = pnv_chip_class_init,
    .class_size    = sizeof(PnvChipClass),
    .abstract      = true,
};

static char *pnv_get_num_chips(Object *obj, Error **errp)
{
    return g_strdup_printf("%d", POWERNV_MACHINE(obj)->num_chips);
}

static void pnv_set_num_chips(Object *obj, const char *value, Error **errp)
{
    PnvMachineState *pnv = POWERNV_MACHINE(obj);
    int num_chips;

    if (sscanf(value, "%d", &num_chips) != 1) {
        error_setg(errp, "invalid num_chips property: '%s'", value);
    }

    /*
     * FIXME: should we decide on how many chips we can create based
     * on #cores and Venice vs. Murano vs. Naples chip type etc...,
     */
    pnv->num_chips = num_chips;
}

static void powernv_machine_initfn(Object *obj)
{
    PnvMachineState *pnv = POWERNV_MACHINE(obj);
    pnv->num_chips = 1;

    object_property_add_str(obj, "num-chips", pnv_get_num_chips,
                            pnv_set_num_chips, NULL);
    object_property_set_description(obj, "num-chips",
                                    "Specifies the number of processor chips",
                                    NULL);
}

static void powernv_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "IBM PowerNV (Non-Virtualized)";
    mc->init = ppc_powernv_init;
    mc->reset = ppc_powernv_reset;
    mc->max_cpus = MAX_CPUS;
    mc->block_default_type = IF_IDE; /* Pnv provides a AHCI device for
                                      * storage */
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    mc->default_ram_size = 1 * G_BYTE;
}

static const TypeInfo powernv_machine_info = {
    .name          = TYPE_POWERNV_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(PnvMachineState),
    .instance_init = powernv_machine_initfn,
    .class_init    = powernv_machine_class_init,
};

static void powernv_machine_register_types(void)
{
    type_register_static(&powernv_machine_info);
    type_register_static(&pnv_chip_info);
    type_register_static(&pnv_chip_power8e_info);
    type_register_static(&pnv_chip_power8_info);
    type_register_static(&pnv_chip_power8nvl_info);
}

type_init(powernv_machine_register_types)
