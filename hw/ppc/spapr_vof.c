/*
 * SPAPR machine hooks to Virtual Open Firmware,
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/vof.h"
#include "hw/ppc/spapr_vof.h"
#include "hw/core/qdev.h"
#include "hw/core/loader.h"
#include "system/system.h"
#include "system/block-backend.h"
#include "system/block-backend-global-state.h"
#include "qom/qom-qobject.h"
#include "target/ppc/cpu.h"
#include "elf.h"
#include "trace.h"

#define SPAPR_VOF_PREP_MAX_SIZE (10 * 1024 * 1024)

target_ulong spapr_h_vof_client(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                target_ulong opcode, target_ulong *_args)
{
    int ret = vof_client_call(MACHINE(spapr), spapr->vof, spapr->fdt_blob,
                              ppc64_phys_to_real(_args[0]));

    if (ret) {
        return H_PARAMETER;
    }
    return H_SUCCESS;
}

void spapr_vof_client_dt_finalize(SpaprMachineState *spapr, void *fdt)
{
    g_autofree char *stdout_path = spapr_vio_stdout_path(spapr->vio_bus);

    vof_build_dt(fdt, spapr->vof);

    int chosen;
    _FDT(chosen = fdt_path_offset(fdt, "/chosen"));

    if (spapr->vof->bootargs) {
        /*
         * If the client did not change "bootargs", spapr_dt_chosen() must have
         * stored machine->kernel_cmdline in it before getting here.
         */
        _FDT(fdt_setprop_string(fdt, chosen, "bootargs", spapr->vof->bootargs));
    }

    if (spapr->vof->disk_boot) {
        /*
         * If disk boot is detected change the "qemu,boot-kernel" to hold
         * kernel_addr/kernel_size which contain the GRUB entry point and size
         */
        uint64_t kern[2];
        kern[0] = cpu_to_be64(spapr->kernel_addr);
        kern[1] = cpu_to_be64(spapr->kernel_size);
        _FDT(fdt_setprop(fdt, chosen, "qemu,boot-kernel", &kern, sizeof(kern)));

        if (spapr->vof->bootpath) {
            _FDT(fdt_setprop_string(fdt, chosen, "bootpath",
                                    spapr->vof->bootpath));
        }
    }

    /*
     * SLOF-less setup requires an open instance of stdout for early
     * kernel printk. By now all phandles are settled so we can open
     * the default serial console.
     */
    if (stdout_path) {
        _FDT(vof_client_open_store(fdt, spapr->vof, "/chosen", "stdout",
                                   stdout_path));
        _FDT(vof_client_open_store(fdt, spapr->vof, "/chosen", "stdin",
                                   stdout_path));
    }
}

static bool vof_elf_segment_cb(void *opaque,
                                uint64_t paddr, uint64_t vaddr,
                                uint64_t filesz, uint64_t memsz)
{
    Vof *vof = opaque;

    if (memsz == 0) {
        return true;
    }

    if (paddr != vaddr) {
        error_report("spapr_vof_load_elf: segment paddr/vaddr mismatch "
                     "(paddr=0x%" PRIx64 " vaddr=0x%" PRIx64 ")",
                     paddr, vaddr);
        return false;
    }

    if (vof_claim(vof, paddr, memsz, 0) == -1) {
        error_report("spapr_vof_load_elf: vof_claim failed for "
                     "paddr=0x%" PRIx64 " size=0x%" PRIx64, paddr, memsz);
        return false;
    }

    return true;
}

static bool spapr_vof_try_prep_boot(SpaprMachineState *spapr, Vof *vof)
{
    BlockBackend *blk;
    DeviceState *dev;
    uint64_t partition_offset;
    uint64_t partition_size;
    uint8_t *prep_data;
    uint64_t entry_point;
    uint64_t load_size;
    int64_t blk_len;

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {

        if (!blk_is_inserted(blk)) {
            continue;
        }

        partition_offset = 0;
        partition_size   = 0;

        if (!spapr_vof_find_prep_partition(blk, &partition_offset,
                                           &partition_size)) {
            continue;
        }

        blk_len = blk_getlength(blk);
        if (blk_len < 0 ||
            partition_size == 0 ||
            partition_size > SPAPR_VOF_PREP_MAX_SIZE ||
            partition_offset > (uint64_t)blk_len ||
            partition_size > (uint64_t)blk_len - partition_offset) {
            continue;
        }

        prep_data = g_malloc(partition_size);
        if (blk_pread(blk, partition_offset, partition_size,
                      prep_data, 0) < 0) {
            g_free(prep_data);
            continue;
        }

        guint saved_claimed_len = vof->claimed->len;
        uint64_t saved_claimed_base = vof->claimed_base;

        uint64_t lowaddr = UINT64_MAX, highaddr = 0;
        entry_point = 0;
        ssize_t ret = load_elf_ram_sym_buf(prep_data, partition_size,
                                           NULL, NULL, NULL,
                                           &entry_point, &lowaddr, &highaddr,
                                           NULL, ELFDATA2MSB,
                                           PPC_ELF_MACHINE, 0, 0,
                                           NULL, false, NULL,
                                           vof_elf_segment_cb, vof);
        if (ret <= 0) {
            error_report("spapr_vof_try_prep_boot: %s", load_elf_strerror(ret));
            /* Roll back any partial claims made during the failed load. */
            g_array_set_size(vof->claimed, saved_claimed_len);
            vof->claimed_base = saved_claimed_base;
            g_free(prep_data);
            continue;
        }
        load_size = highaddr - lowaddr;

        g_free(prep_data);

        spapr->kernel_addr = entry_point;
        spapr->kernel_size = load_size;

        vof->disk_boot = true;
        dev = blk_get_attached_dev(blk);
        if (dev) {
            vof->bootpath = qdev_get_fw_dev_path(dev);
        }
        return true;
    }
    return false;
}

void spapr_vof_reset(SpaprMachineState *spapr, void *fdt, Error **errp)
{
    target_ulong stack_ptr;
    Vof *vof = spapr->vof;
    PowerPCCPU *first_ppc_cpu = POWERPC_CPU(first_cpu);
    MachineState *machine = MACHINE(spapr);

    vof_init(vof, spapr->rma_size, errp);

    stack_ptr = vof_claim(vof, 0, VOF_STACK_SIZE, VOF_STACK_SIZE);
    if (stack_ptr == -1) {
        error_setg(errp, "Memory allocation for stack failed");
        return;
    }
    /* Stack grows downwards plus reserve space for the minimum stack frame */
    stack_ptr += VOF_STACK_SIZE - 0x20;

    if (machine->kernel_filename && spapr->kernel_size &&
        vof_claim(vof, spapr->kernel_addr, spapr->kernel_size, 0) == -1) {
        error_setg(errp, "Memory for kernel is in use");
        return;
    }

    if (spapr->initrd_size &&
        vof_claim(vof, spapr->initrd_base, spapr->initrd_size, 0) == -1) {
        error_setg(errp, "Memory for initramdisk is in use");
        return;
    }

    /*
     * Disk boot: load GRUB from the PReP boot partition on the block device, if
     * no kernel/initrd are provided
     */
    if (!machine->kernel_filename) {
        spapr_vof_try_prep_boot(spapr, vof);
    }

    spapr_vof_client_dt_finalize(spapr, fdt);

    spapr_cpu_set_entry_state(first_ppc_cpu, SPAPR_ENTRY_POINT,
                              stack_ptr, spapr->initrd_base,
                              spapr->initrd_size);

    /*
     * At this point the expected allocation map is:
     *
     * Kernel + initrd boot:
     *   0..d80       - the initial firmware
     *   8000..10000  - stack
     *   10000..20000 - vscsi-report-luns table (up to 64 KB, if needed)
     *   400000..     - kernel
     *   3ea0000..    - initramdisk
     *
     * Disk (GRUB) boot:
     *   0..d80       - the initial firmware
     *   8000..10000  - stack
     *   10000..20000 - vscsi-report-luns table (up to 64 KB, if needed)
     *   400000..     - GRUB (loaded from PReP partition)
     *
     * We skip writing FDT as nothing expects it; OF client interface is
     * going to be used for reading the device tree.
     */
}

void spapr_vof_quiesce(MachineState *ms)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(ms);

    spapr->fdt_size = fdt_totalsize(spapr->fdt_blob);
    spapr->fdt_initial_size = spapr->fdt_size;
}

bool spapr_vof_setprop(MachineState *ms, const char *path, const char *propname,
                       void *val, int vallen)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(ms);

    /*
     * We only allow changing properties which we know how to update in QEMU
     * OR
     * the ones which we know that they need to survive during "quiesce".
     */

    if (strcmp(path, "/rtas") == 0) {
        if (strcmp(propname, "linux,rtas-base") == 0 ||
            strcmp(propname, "linux,rtas-entry") == 0) {
            /* These need to survive quiesce so let them store in the FDT */
            return true;
        }
    }

    if (strcmp(path, "/chosen") == 0) {
        if (strcmp(propname, "bootargs") == 0) {
            Vof *vof = spapr->vof;

            g_free(vof->bootargs);
            vof->bootargs = g_strndup(val, vallen);
            return true;
        }
        if (strcmp(propname, "linux,initrd-start") == 0) {
            if (vallen == sizeof(uint32_t)) {
                spapr->initrd_base = ldl_be_p(val);
                return true;
            }
            if (vallen == sizeof(uint64_t)) {
                spapr->initrd_base = ldq_be_p(val);
                return true;
            }
            return false;
        }
        if (strcmp(propname, "linux,initrd-end") == 0) {
            if (vallen == sizeof(uint32_t)) {
                spapr->initrd_size = ldl_be_p(val) - spapr->initrd_base;
                return true;
            }
            if (vallen == sizeof(uint64_t)) {
                spapr->initrd_size = ldq_be_p(val) - spapr->initrd_base;
                return true;
            }
            return false;
        }
    }

    return true;
}
