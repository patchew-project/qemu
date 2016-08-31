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
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/cutils.h"

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

static void *powernv_create_fdt(PnvMachineState *pnv,
                                const char *kernel_cmdline)
{
    void *fdt;
    char *buf;
    const char plat_compat[] = "qemu,powernv\0ibm,powernv";
    int off;

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
    .class_init    = powernv_machine_class_init,
};

static void powernv_machine_register_types(void)
{
    type_register_static(&powernv_machine_info);
}

type_init(powernv_machine_register_types)
