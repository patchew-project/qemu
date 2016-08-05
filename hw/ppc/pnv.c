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

#include <libfdt.h>

#define FDT_ADDR                0x01000000
#define FDT_MAX_SIZE            0x00100000
#define FW_MAX_SIZE             0x00400000
#define FW_FILE_NAME            "skiboot.lid"

#define MAX_CPUS                255

static void powernv_populate_memory_node(void *fdt, int nodeid, hwaddr start,
                                         hwaddr size)
{
    /* Probably bogus, need to match with what's going on in CPU nodes */
    uint32_t chip_id = nodeid;
    char *mem_name;
    uint64_t mem_reg_property[2];

    mem_reg_property[0] = cpu_to_be64(start);
    mem_reg_property[1] = cpu_to_be64(size);

    mem_name = g_strdup_printf("memory@"TARGET_FMT_lx, start);
    _FDT((fdt_begin_node(fdt, mem_name)));
    g_free(mem_name);
    _FDT((fdt_property_string(fdt, "device_type", "memory")));
    _FDT((fdt_property(fdt, "reg", mem_reg_property,
                       sizeof(mem_reg_property))));
    _FDT((fdt_property_cell(fdt, "ibm,chip-id", chip_id)));
    _FDT((fdt_end_node(fdt)));
}

static int powernv_populate_memory(void *fdt)
{
    hwaddr mem_start, node_size;
    int i, nb_nodes = nb_numa_nodes;
    NodeInfo *nodes = numa_info;
    NodeInfo ramnode;

    /* No NUMA nodes, assume there is just one node with whole RAM */
    if (!nb_numa_nodes) {
        nb_nodes = 1;
        ramnode.node_mem = ram_size;
        nodes = &ramnode;
    }

    for (i = 0, mem_start = 0; i < nb_nodes; ++i) {
        if (!nodes[i].node_mem) {
            continue;
        }
        if (mem_start >= ram_size) {
            node_size = 0;
        } else {
            node_size = nodes[i].node_mem;
            if (node_size > ram_size - mem_start) {
                node_size = ram_size - mem_start;
            }
        }
        for ( ; node_size; ) {
            hwaddr sizetmp = pow2floor(node_size);

            /* mem_start != 0 here */
            if (ctzl(mem_start) < ctzl(sizetmp)) {
                sizetmp = 1ULL << ctzl(mem_start);
            }

            powernv_populate_memory_node(fdt, i, mem_start, sizetmp);
            node_size -= sizetmp;
            mem_start += sizetmp;
        }
    }

    return 0;
}

static void powernv_create_core_node(void *fdt, CPUState *cs, uint32_t chip_id)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    int smt_threads = ppc_get_compat_smt_threads(cpu);
    CPUPPCState *env = &cpu->env;
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    uint32_t servers_prop[smt_threads];
    uint32_t gservers_prop[smt_threads * 2];
    int i, index = ppc_get_vcpu_dt_id(cpu);
    uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                       0xffffffff, 0xffffffff};
    uint32_t tbfreq = PNV_TIMEBASE_FREQ;
    uint32_t cpufreq = 1000000000;
    uint32_t page_sizes_prop[64];
    size_t page_sizes_prop_size;
    char *nodename;

    nodename = g_strdup_printf("%s@%x", dc->fw_name, index);

    _FDT((fdt_begin_node(fdt, nodename)));

    g_free(nodename);

    _FDT((fdt_property_cell(fdt, "reg", index)));
    _FDT((fdt_property_string(fdt, "device_type", "cpu")));

    _FDT((fdt_property_cell(fdt, "cpu-version", env->spr[SPR_PVR])));
    _FDT((fdt_property_cell(fdt, "d-cache-block-size",
                            env->dcache_line_size)));
    _FDT((fdt_property_cell(fdt, "d-cache-line-size",
                            env->dcache_line_size)));
    _FDT((fdt_property_cell(fdt, "i-cache-block-size",
                            env->icache_line_size)));
    _FDT((fdt_property_cell(fdt, "i-cache-line-size",
                            env->icache_line_size)));

    if (pcc->l1_dcache_size) {
        _FDT((fdt_property_cell(fdt, "d-cache-size", pcc->l1_dcache_size)));
    } else {
        error_report("Warning: Unknown L1 dcache size for cpu");
    }
    if (pcc->l1_icache_size) {
        _FDT((fdt_property_cell(fdt, "i-cache-size", pcc->l1_icache_size)));
    } else {
        error_report("Warning: Unknown L1 icache size for cpu");
    }

    _FDT((fdt_property_cell(fdt, "timebase-frequency", tbfreq)));
    _FDT((fdt_property_cell(fdt, "clock-frequency", cpufreq)));
    _FDT((fdt_property_cell(fdt, "ibm,slb-size", env->slb_nr)));
    _FDT((fdt_property_string(fdt, "status", "okay")));
    _FDT((fdt_property(fdt, "64-bit", NULL, 0)));

    if (env->spr_cb[SPR_PURR].oea_read) {
        _FDT((fdt_property(fdt, "ibm,purr", NULL, 0)));
    }

    if (env->mmu_model & POWERPC_MMU_1TSEG) {
        _FDT((fdt_property(fdt, "ibm,processor-segment-sizes",
                           segs, sizeof(segs))));
    }

    /* Advertise VMX/VSX (vector extensions) if available
     *   0 / no property == no vector extensions
     *   1               == VMX / Altivec available
     *   2               == VSX available */
    if (env->insns_flags & PPC_ALTIVEC) {
        uint32_t vmx = (env->insns_flags2 & PPC2_VSX) ? 2 : 1;

        _FDT((fdt_property_cell(fdt, "ibm,vmx", vmx)));
    }

    /* Advertise DFP (Decimal Floating Point) if available
     *   0 / no property == no DFP
     *   1               == DFP available */
    if (env->insns_flags2 & PPC2_DFP) {
        _FDT((fdt_property_cell(fdt, "ibm,dfp", 1)));
    }

    page_sizes_prop_size = ppc_create_page_sizes_prop(env, page_sizes_prop,
                                                  sizeof(page_sizes_prop));
    if (page_sizes_prop_size) {
        _FDT((fdt_property(fdt, "ibm,segment-page-sizes",
                           page_sizes_prop, page_sizes_prop_size)));
    }

    _FDT((fdt_property_cell(fdt, "ibm,chip-id", chip_id)));

    if (cpu->cpu_version) {
        _FDT((fdt_property_cell(fdt, "cpu-version", cpu->cpu_version)));
    }

    /* Build interrupt servers and gservers properties */
    for (i = 0; i < smt_threads; i++) {
        servers_prop[i] = cpu_to_be32(index + i);
        /* Hack, direct the group queues back to cpu 0 */
        gservers_prop[i * 2] = cpu_to_be32(index + i);
        gservers_prop[i * 2 + 1] = 0;
    }
    _FDT((fdt_property(fdt, "ibm,ppc-interrupt-server#s",
                       servers_prop, sizeof(servers_prop))));
    _FDT((fdt_property(fdt, "ibm,ppc-interrupt-gserver#s",
                       gservers_prop, sizeof(gservers_prop))));

    _FDT((fdt_end_node(fdt)));
}

static void *powernv_create_fdt(sPowerNVMachineState *pnv,
                                const char *kernel_cmdline)
{
    void *fdt;
    uint32_t start_prop = cpu_to_be32(pnv->initrd_base);
    uint32_t end_prop = cpu_to_be32(pnv->initrd_base + pnv->initrd_size);
    char *buf;
    const char plat_compat[] = "qemu,powernv\0ibm,powernv";
    int i;

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create(fdt, FDT_MAX_SIZE)));
    _FDT((fdt_finish_reservemap(fdt)));

    /* Root node */
    _FDT((fdt_begin_node(fdt, "")));
    _FDT((fdt_property_string(fdt, "model", "IBM PowerNV (emulated by qemu)")));
    _FDT((fdt_property(fdt, "compatible", plat_compat, sizeof(plat_compat))));

    buf = g_strdup_printf(UUID_FMT, qemu_uuid[0], qemu_uuid[1],
                          qemu_uuid[2], qemu_uuid[3], qemu_uuid[4],
                          qemu_uuid[5], qemu_uuid[6], qemu_uuid[7],
                          qemu_uuid[8], qemu_uuid[9], qemu_uuid[10],
                          qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                          qemu_uuid[14], qemu_uuid[15]);

    _FDT((fdt_property_string(fdt, "vm,uuid", buf)));
    g_free(buf);

    _FDT((fdt_begin_node(fdt, "chosen")));
    if (kernel_cmdline) {
        _FDT((fdt_property_string(fdt, "bootargs", kernel_cmdline)));
    }
    _FDT((fdt_property(fdt, "linux,initrd-start",
                       &start_prop, sizeof(start_prop))));
    _FDT((fdt_property(fdt, "linux,initrd-end",
                       &end_prop, sizeof(end_prop))));
    _FDT((fdt_end_node(fdt)));

    _FDT((fdt_property_cell(fdt, "#address-cells", 0x2)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x2)));

    /* Memory */
    _FDT((powernv_populate_memory(fdt)));

    /* cpus */
    _FDT((fdt_begin_node(fdt, "cpus")));
    _FDT((fdt_property_cell(fdt, "#address-cells", 0x1)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x0)));

    for (i = 0; i < pnv->num_chips; i++) {
        PnvChip *chip = &pnv->chips[i];
        int j;

        for (j = 0; j < chip->num_cores; j++) {
            PowerNVCPUCore *pc = POWERNV_CPU_CORE(chip->cores[j]);
            CPUState *cs = CPU(DEVICE(pc->threads));
            powernv_create_core_node(fdt, cs, chip->chip_id);
        }
    }
    _FDT((fdt_end_node(fdt)));

    _FDT((fdt_end_node(fdt))); /* close root node */
    _FDT((fdt_finish(fdt)));

    return fdt;
}

static void ppc_powernv_reset(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    sPowerNVMachineState *pnv = POWERNV_MACHINE(machine);
    void *fdt;

    pnv->fdt_addr = FDT_ADDR;

    qemu_devices_reset();

    fdt = powernv_create_fdt(pnv, machine->kernel_cmdline);

    cpu_physical_memory_write(pnv->fdt_addr, fdt, fdt_totalsize(fdt));
}

static void ppc_powernv_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    sPowerNVMachineState *pnv = POWERNV_MACHINE(machine);
    long fw_size;
    char *filename;
    int i;

    if (ram_size < (1 * G_BYTE)) {
        error_report("Warning: skiboot may not work with < 1GB of RAM");
    }

    /* allocate RAM */
    memory_region_allocate_system_memory(ram, NULL, "ppc_powernv.ram",
                                         ram_size);
    memory_region_add_subregion(sysmem, 0, ram);

    if (bios_name == NULL) {
        bios_name = FW_FILE_NAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    fw_size = load_image_targphys(filename, 0, FW_MAX_SIZE);
    if (fw_size < 0) {
        hw_error("qemu: could not load OPAL '%s'\n", filename);
        exit(1);
    }
    g_free(filename);

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, kernel_filename);
    if (!filename) {
        hw_error("qemu: could find kernel '%s'\n", kernel_filename);
        exit(1);
    }

    fw_size = load_image_targphys(filename, 0x20000000, 0x2000000);
    if (fw_size < 0) {
        hw_error("qemu: could not load kernel'%s'\n", filename);
        exit(1);
    }
    g_free(filename);

    /* load initrd */
    if (initrd_filename) {
            /* Try to locate the initrd in the gap between the kernel
             * and the firmware. Add a bit of space just in case
             */
            pnv->initrd_base = 0x40000000;
            pnv->initrd_size = load_image_targphys(initrd_filename,
                            pnv->initrd_base, 0x10000000); /* 128MB max */
            if (pnv->initrd_size < 0) {
                    error_report("qemu: could not load initial ram disk '%s'",
                            initrd_filename);
                    exit(1);
            }
    } else {
            pnv->initrd_base = 0;
            pnv->initrd_size = 0;
    }

    /* Create PowerNV chips
     *
     * FIXME: We should decide how many chips to create based on
     * #cores and Venice vs. Murano vs. Naples chip type etc..., for
     * now, just create one chip, with all the cores.
     */
    pnv->num_chips = 1;

    pnv->chips = g_new0(PnvChip, pnv->num_chips);
    for (i = 0; i < pnv->num_chips; i++) {
        PnvChip *chip = &pnv->chips[i];

        object_initialize(chip, sizeof(*chip), TYPE_PNV_CHIP);
        object_property_set_int(OBJECT(chip), i, "chip-id", &error_abort);
        object_property_set_int(OBJECT(chip), smp_cpus, "num-cores",
                                &error_abort);
        object_property_set_str(OBJECT(chip), machine->cpu_model, "cpu-model",
                                &error_abort);
        object_property_set_bool(OBJECT(chip), true, "realized", &error_abort);
    }
}

static void powernv_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = ppc_powernv_init;
    mc->reset = ppc_powernv_reset;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = MAX_CPUS;
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    mc->default_ram_size = 1 * G_BYTE;
}

static const TypeInfo powernv_machine_info = {
    .name          = TYPE_POWERNV_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(sPowerNVMachineState),
    .class_init    = powernv_machine_class_init,
};

static void powernv_machine_2_8_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->name = "powernv-2.8";
    mc->desc = "PowerNV v2.8";
    mc->alias = "powernv";
}

static const TypeInfo powernv_machine_2_8_info = {
    .name          = MACHINE_TYPE_NAME("powernv-2.8"),
    .parent        = TYPE_POWERNV_MACHINE,
    .class_init    = powernv_machine_2_8_class_init,
};

static void pnv_chip_realize(DeviceState *dev, Error **errp)
{
    int i;
    PnvChip *chip = PNV_CHIP(dev);
    char *typename = powernv_cpu_core_typename(chip->cpu_model);

    if (!object_class_by_name(typename)) {
        error_setg(errp, "Unable to find PowerNV CPU Core definition");
        return;
    }

    chip->cores = g_new0(Object *, chip->num_cores);
    for (i = 0; i < chip->num_cores; i++) {
        int core_id = i * smp_threads;
        chip->cores[i] = object_new(typename);
        object_property_set_int(chip->cores[i], smp_threads, "nr-threads",
                                &error_fatal);
        object_property_set_int(chip->cores[i], core_id, CPU_CORE_PROP_CORE_ID,
                                &error_fatal);
        object_property_set_bool(chip->cores[i], true, "realized",
                                 &error_fatal);
    }
    g_free(typename);
}

static Property pnv_chip_properties[] = {
    DEFINE_PROP_UINT32("chip-id", PnvChip, chip_id, 0),
    DEFINE_PROP_STRING("cpu-model", PnvChip, cpu_model),
    DEFINE_PROP_UINT32("num-cores", PnvChip, num_cores, 1),
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
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_class_init,
};


static void powernv_machine_register_types(void)
{
    type_register_static(&powernv_machine_info);
    type_register_static(&powernv_machine_2_8_info);
    type_register_static(&pnv_chip_info);
}

type_init(powernv_machine_register_types)
