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
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/fw-path-provider.h"
#include "elf.h"
#include "net/net.h"
#include "sysemu/block-backend.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "sysemu/numa.h"
#include "kvm_ppc.h"
#include "mmu-hash64.h"
#include "qom/cpu.h"

#include "hw/boards.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/loader.h"

#include "exec/address-spaces.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/nmi.h"

#include "hw/compat.h"

#include <libfdt.h>

#define FDT_ADDR                0x01000000
#define FDT_MAX_SIZE            0x00100000
#define FW_MAX_SIZE             0x00400000
#define FW_FILE_NAME            "skiboot.lid"
#define KERNEL_FILE_NAME        "skiroot.lid"
#define KERNEL_LOAD_ADDR        0x20000000

#define TIMEBASE_FREQ           512000000ULL

#define MAX_CPUS                255

#define PHANDLE_XICP            0x00001111

typedef struct sPowerNVMachineState sPowerNVMachineState;

#define TYPE_POWERNV_MACHINE      "powernv-machine"
#define POWERNV_MACHINE(obj) \
    OBJECT_CHECK(sPowerNVMachineState, (obj), TYPE_POWERNV_MACHINE)

/**
 * sPowerNVMachineState:
 */
struct sPowerNVMachineState {
    /*< private >*/
    MachineState parent_obj;
    PnvSystem sys;
};

static size_t create_page_sizes_prop(CPUPPCState *env, uint32_t *prop,
                                     size_t maxsize)
{
    size_t maxcells = maxsize / sizeof(uint32_t);
    int i, j, count;
    uint32_t *p = prop;

    for (i = 0; i < PPC_PAGE_SIZES_MAX_SZ; i++) {
        struct ppc_one_seg_page_size *sps = &env->sps.sps[i];

        if (!sps->page_shift) {
            break;
        }
        for (count = 0; count < PPC_PAGE_SIZES_MAX_SZ; count++) {
            if (sps->enc[count].page_shift == 0) {
                break;
            }
        }
        if ((p - prop) >= (maxcells - 3 - count * 2)) {
            break;
        }
        *(p++) = cpu_to_be32(sps->page_shift);
        *(p++) = cpu_to_be32(sps->slb_enc);
        *(p++) = cpu_to_be32(count);
        for (j = 0; j < count; j++) {
            *(p++) = cpu_to_be32(sps->enc[j].page_shift);
            *(p++) = cpu_to_be32(sps->enc[j].pte_enc);
        }
    }

    return (p - prop) * sizeof(uint32_t);
}

static void powernv_populate_memory_node(void *fdt, int nodeid, hwaddr start,
                                         hwaddr size)
{
    /* Probablly bogus, need to match with what's going on in CPU nodes */
    uint32_t chip_id[] = {
        cpu_to_be32(0x0), cpu_to_be32(nodeid)
    };
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
    _FDT((fdt_property(fdt, "ibm,chip-id", chip_id, sizeof(chip_id))));
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

static void powernv_create_cpu_node(void *fdt, CPUState *cs, int smt_threads)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    uint32_t servers_prop[smt_threads];
    uint32_t gservers_prop[smt_threads * 2];
    int i, index = ppc_get_vcpu_dt_id(cpu);
    uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                       0xffffffff, 0xffffffff};
    uint32_t tbfreq = kvm_enabled() ? kvmppc_get_tbfreq() : TIMEBASE_FREQ;
    uint32_t cpufreq = kvm_enabled() ? kvmppc_get_clockfreq() : 1000000000;
    uint32_t page_sizes_prop[64];
    size_t page_sizes_prop_size;
    char *nodename;

    if ((index % smt_threads) != 0) {
        return;
    }

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

    page_sizes_prop_size = create_page_sizes_prop(env, page_sizes_prop,
                                                  sizeof(page_sizes_prop));
    if (page_sizes_prop_size) {
        _FDT((fdt_property(fdt, "ibm,segment-page-sizes",
                           page_sizes_prop, page_sizes_prop_size)));
    }

    /* XXX Just a hack for now */
    _FDT((fdt_property_cell(fdt, "ibm,chip-id", 0)));

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

static void *powernv_create_fdt(PnvSystem *sys, const char *kernel_cmdline,
                                uint32_t initrd_base, uint32_t initrd_size)
{
    void *fdt;
    CPUState *cs;
    int smt = kvmppc_smt_threads();
    uint32_t start_prop = cpu_to_be32(initrd_base);
    uint32_t end_prop = cpu_to_be32(initrd_base + initrd_size);
    char *buf;
    const char plat_compat[] = "qemu,powernv\0ibm,powernv";

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create(fdt, FDT_MAX_SIZE)));
    _FDT((fdt_finish_reservemap(fdt)));

    /* Root node */
    _FDT((fdt_begin_node(fdt, "")));
    _FDT((fdt_property_string(fdt, "model", "IBM PowerNV (emulated by qemu)")));
    _FDT((fdt_property(fdt, "compatible", plat_compat, sizeof(plat_compat))));

    /*
     * Add info to guest to indentify which host is it being run on
     * and what is the uuid of the guest
     */
    if (kvmppc_get_host_model(&buf)) {
        _FDT((fdt_property_string(fdt, "host-model", buf)));
        g_free(buf);
    }
    if (kvmppc_get_host_serial(&buf)) {
        _FDT((fdt_property_string(fdt, "host-serial", buf)));
        g_free(buf);
    }

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

    /* cpus */
    _FDT((fdt_begin_node(fdt, "cpus")));
    _FDT((fdt_property_cell(fdt, "#address-cells", 0x1)));
    _FDT((fdt_property_cell(fdt, "#size-cells", 0x0)));

    CPU_FOREACH(cs) {
        powernv_create_cpu_node(fdt, cs, smt);
    }

    _FDT((fdt_end_node(fdt)));

    /* Memory */
    _FDT((powernv_populate_memory(fdt)));

    /* /hypervisor node */
    if (kvm_enabled()) {
        uint8_t hypercall[16];

        /* indicate KVM hypercall interface */
        _FDT((fdt_begin_node(fdt, "hypervisor")));
        _FDT((fdt_property_string(fdt, "compatible", "linux,kvm")));
        if (kvmppc_has_cap_fixup_hcalls()) {
            /*
             * Older KVM versions with older guest kernels were broken with the
             * magic page, don't allow the guest to map it.
             */
            kvmppc_get_hypercall(first_cpu->env_ptr, hypercall,
                                 sizeof(hypercall));
            _FDT((fdt_property(fdt, "hcall-instructions", hypercall,
                              sizeof(hypercall))));
        }
        _FDT((fdt_end_node(fdt)));
    }

    _FDT((fdt_end_node(fdt))); /* close root node */
    _FDT((fdt_finish(fdt)));

    return fdt;
}

static void powernv_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cpu_reset(cs);

    env->spr[SPR_PIR] = ppc_get_vcpu_dt_id(cpu);
    env->spr[SPR_HIOR] = 0;
    env->gpr[3] = FDT_ADDR;
    env->nip = 0x10;
    env->msr |= MSR_HVB;
}

static void pnv_create_chip(PnvSystem *sys, unsigned int chip_no)
{
    PnvChip *chip = &sys->chips[chip_no];

    if (chip_no >= PNV_MAX_CHIPS) {
            return;
    }

    /* XXX Improve chip numbering to better match HW */
    chip->chip_id = chip_no;
}

static void ppc_powernv_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    uint32_t initrd_base = 0;
    long initrd_size = 0;
    PowerPCCPU *cpu;
    CPUPPCState *env;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    sPowerNVMachineState *pnv_machine = POWERNV_MACHINE(machine);
    PnvSystem *sys = &pnv_machine->sys;
    long fw_size;
    char *filename;
    void *fdt;
    int i;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = kvm_enabled() ? "host" : "POWER8";
    }

    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(cpu_model);
        if (cpu == NULL) {
            error_report("Unable to find PowerPC CPU definition");
            exit(1);
        }
        env = &cpu->env;

        /* Set time-base frequency to 512 MHz */
        cpu_ppc_tb_init(env, TIMEBASE_FREQ);

        /* MSR[IP] doesn't exist nowadays */
        env->msr_mask &= ~(1 << 6);

        qemu_register_reset(powernv_cpu_reset, cpu);
    }

    /* allocate RAM */
    memory_region_allocate_system_memory(ram, NULL, "ppc_powernv.ram",
                                         ram_size);
    memory_region_add_subregion(sysmem, 0, ram);

    /* XXX We should decide how many chips to create based on #cores and
     * Venice vs. Murano vs. Naples chip type etc..., for now, just create
     * one chip. Also creation of the CPUs should be done per-chip
     */
    sys->num_chips = 1;

    /* Create only one PHB for now until I figure out what's wrong
     * when I create more (resource assignment failures in Linux)
     */
    pnv_create_chip(sys, 0);

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


    if (kernel_filename == NULL) {
        kernel_filename = KERNEL_FILE_NAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, kernel_filename);
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
            initrd_base = 0x40000000;
            initrd_size = load_image_targphys(initrd_filename, initrd_base,
                                              0x10000000); /* 128MB max */
            if (initrd_size < 0) {
                    error_report("qemu: could not load initial ram disk '%s'",
                            initrd_filename);
                    exit(1);
            }
    } else {
            initrd_base = 0;
            initrd_size = 0;
    }
    fdt = powernv_create_fdt(sys, machine->kernel_cmdline,
                             initrd_base, initrd_size);
    cpu_physical_memory_write(FDT_ADDR, fdt, fdt_totalsize(fdt));
}

static int powernv_kvm_type(const char *vm_type)
{
    /* Always force PR KVM */
    return 2;
}

static void ppc_cpu_do_nmi_on_cpu(void *arg)
{
    CPUState *cs = arg;

    cpu_synchronize_state(cs);
    ppc_cpu_do_system_reset(cs);
}

static void powernv_nmi(NMIState *n, int cpu_index, Error **errp)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        async_run_on_cpu(cs, ppc_cpu_do_nmi_on_cpu, cs);
    }
}

static void powernv_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->init = ppc_powernv_init;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = MAX_CPUS;
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    mc->kvm_type = powernv_kvm_type;

    nc->nmi_monitor_handler = powernv_nmi;
}

static const TypeInfo powernv_machine_info = {
    .name          = TYPE_POWERNV_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(sPowerNVMachineState),
    .class_init    = powernv_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NMI },
        { }
    },
};

static void powernv_machine_2_5_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->name = "powernv-2.5";
    mc->desc = "PowerNV v2.5";
    mc->alias = "powernv";
}

static const TypeInfo powernv_machine_2_5_info = {
    .name          = MACHINE_TYPE_NAME("powernv-2.5"),
    .parent        = TYPE_POWERNV_MACHINE,
    .class_init    = powernv_machine_2_5_class_init,
};

static void powernv_machine_register_types(void)
{
    type_register_static(&powernv_machine_info);
    type_register_static(&powernv_machine_2_5_info);
}

type_init(powernv_machine_register_types)
