/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch boot helper functions.
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "target/loongarch/cpu.h"
#include "hw/loongarch/virt.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "sysemu/reset.h"

ram_addr_t initrd_offset;
uint64_t initrd_size;

static const unsigned int slave_boot_code[] = {
                  /* Configure reset ebase.         */
    0x0400302c,   /* csrwr      $r12,0xc            */

                  /* Disable interrupt.             */
    0x0380100c,   /* ori        $r12,$r0,0x4        */
    0x04000180,   /* csrxchg    $r0,$r12,0x0        */

                  /* Clear mailbox.                 */
    0x1400002d,   /* lu12i.w    $r13,1(0x1)         */
    0x038081ad,   /* ori        $r13,$r13,0x20      */
    0x06481da0,   /* iocsrwr.d  $r0,$r13            */

                  /* Enable IPI interrupt.          */
    0x1400002c,   /* lu12i.w    $r12,1(0x1)         */
    0x0400118c,   /* csrxchg    $r12,$r12,0x4       */
    0x02fffc0c,   /* addi.d     $r12,$r0,-1(0xfff)  */
    0x1400002d,   /* lu12i.w    $r13,1(0x1)         */
    0x038011ad,   /* ori        $r13,$r13,0x4       */
    0x064819ac,   /* iocsrwr.w  $r12,$r13           */
    0x1400002d,   /* lu12i.w    $r13,1(0x1)         */
    0x038081ad,   /* ori        $r13,$r13,0x20      */

                  /* Wait for wakeup  <.L11>:       */
    0x06488000,   /* idle       0x0                 */
    0x03400000,   /* andi       $r0,$r0,0x0         */
    0x064809ac,   /* iocsrrd.w  $r12,$r13           */
    0x43fff59f,   /* beqz       $r12,-12(0x7ffff4) # 48 <.L11> */

                  /* Read and clear IPI interrupt.  */
    0x1400002d,   /* lu12i.w    $r13,1(0x1)         */
    0x064809ac,   /* iocsrrd.w  $r12,$r13           */
    0x1400002d,   /* lu12i.w    $r13,1(0x1)         */
    0x038031ad,   /* ori        $r13,$r13,0xc       */
    0x064819ac,   /* iocsrwr.w  $r12,$r13           */

                  /* Disable  IPI interrupt.        */
    0x1400002c,   /* lu12i.w    $r12,1(0x1)         */
    0x04001180,   /* csrxchg    $r0,$r12,0x4        */

                  /* Read mail buf and jump to specified entry */
    0x1400002d,   /* lu12i.w    $r13,1(0x1)         */
    0x038081ad,   /* ori        $r13,$r13,0x20      */
    0x06480dac,   /* iocsrrd.d  $r12,$r13           */
    0x00150181,   /* move       $r1,$r12            */
    0x4c000020,   /* jirl       $r0,$r1,0           */
};

static inline void *guidcpy(void *dst, const void *src)
{
    return memcpy(dst, src, sizeof(efi_guid_t));
}

static void init_efi_boot_memmap(struct efi_system_table *systab,
                                 void *p, void *start)
{
    unsigned i;
    struct efi_boot_memmap *boot_memmap = p;
    efi_guid_t tbl_guid = LINUX_EFI_BOOT_MEMMAP_GUID;

    /* efi_configuration_table 1 */
    guidcpy(&systab->tables[0].guid, &tbl_guid);
    systab->tables[0].table = (struct efi_configuration_table *)(p - start);
    systab->nr_tables = 1;

    boot_memmap->desc_size = sizeof(efi_memory_desc_t);
    boot_memmap->desc_ver = 1;
    boot_memmap->map_size = 0;

    efi_memory_desc_t *map = p + sizeof(struct efi_boot_memmap);
    for (i = 0; i < memmap_entries; i++) {
        map = (void *)boot_memmap + sizeof(*map);
        map[i].type = memmap_table[i].type;
        map[i].phys_addr = memmap_table[i].address;
        map[i].num_pages = memmap_table[i].length >> 16; /* 64KB align*/
        p += sizeof(efi_memory_desc_t);
    }
}

static void init_efi_initrd_table(struct efi_system_table *systab,
                                  void *p, void *start)
{
    efi_guid_t tbl_guid = LINUX_EFI_INITRD_MEDIA_GUID;
    struct efi_initrd *initrd_table  = p;

    /* efi_configuration_table 2 */
    guidcpy(&systab->tables[1].guid, &tbl_guid);
    systab->tables[1].table = (struct efi_configuration_table *)(p - start);
    systab->nr_tables = 2;

    initrd_table->base = initrd_offset;
    initrd_table->size = initrd_size;
}

static void init_efi_fdt_table(struct efi_system_table *systab)
{
    efi_guid_t tbl_guid = DEVICE_TREE_GUID;

    /* efi_configuration_table 3 */
    guidcpy(&systab->tables[2].guid, &tbl_guid);
    systab->tables[2].table = (void *)0x100000; /* fdt_base 1MiB */
    systab->nr_tables = 3;
}

static void init_systab(struct loongarch_boot_info *info, void *p, void *start)
{
    void *bp_tables_start;
    struct efi_system_table *systab = p;

    info->a2 = (uint64_t)p - (uint64_t)start;

    systab->hdr.signature = EFI_SYSTEM_TABLE_SIGNATURE;
    systab->hdr.revision = EFI_SPECIFICATION_VERSION;
    systab->hdr.revision = sizeof(struct efi_system_table),
    systab->fw_revision = FW_VERSION << 16 | FW_PATCHLEVEL << 8;
    systab->runtime = 0;
    systab->boottime = 0;
    systab->nr_tables = 0;

    p += ROUND_UP(sizeof(struct efi_system_table), 64);

    systab->tables = p;
    bp_tables_start = p;

    init_efi_boot_memmap(systab, p, start);
    p += ROUND_UP(sizeof(struct efi_boot_memmap) +
                  sizeof(efi_memory_desc_t) * memmap_entries, 64);
    init_efi_initrd_table(systab, p, start);
    p += ROUND_UP(sizeof(struct efi_initrd), 64);
    init_efi_fdt_table(systab);

    systab->tables = (struct efi_configuration_table *)(bp_tables_start - start);
}

static void init_cmdline(struct loongarch_boot_info *info, void *p, void *start)
{
    hwaddr cmdline_addr = (hwaddr)p - (hwaddr)start;

    info->a0 = 1;
    info->a1 = cmdline_addr;

    memcpy(p, info->kernel_cmdline, COMMAND_LINE_SIZE);
}

static uint64_t cpu_loongarch_virt_to_phys(void *opaque, uint64_t addr)
{
    return addr & MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS);
}

static int64_t load_kernel_info(struct loongarch_boot_info *info)
{
    uint64_t kernel_entry, kernel_low, kernel_high;
    ssize_t kernel_size;

    kernel_size = load_elf(info->kernel_filename, NULL,
                           cpu_loongarch_virt_to_phys, NULL,
                           &kernel_entry, &kernel_low,
                           &kernel_high, NULL, 0,
                           EM_LOONGARCH, 1, 0);

    if (kernel_size < 0) {
        error_report("could not load kernel '%s': %s",
                     info->kernel_filename,
                     load_elf_strerror(kernel_size));
        exit(1);
    }

    if (info->initrd_filename) {
        initrd_size = get_image_size(info->initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = ROUND_UP(kernel_high + 4 * kernel_size, 64 * KiB);

            if (initrd_offset + initrd_size > info->ram_size) {
                error_report("memory too small for initial ram disk '%s'",
                             info->initrd_filename);
                exit(1);
            }

            initrd_size = load_image_targphys(info->initrd_filename, initrd_offset,
                                              info->ram_size - initrd_offset);
        }

        if (initrd_size == (target_ulong)-1) {
            error_report("could not load initial ram disk '%s'",
                         info->initrd_filename);
            exit(1);
        }
    } else {
        error_report("Need initrd!");
        exit(1);
    }

    return kernel_entry;
}

static void reset_load_elf(void *opaque)
{
    LoongArchCPU *cpu = opaque;
    CPULoongArchState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    if (env->load_elf) {
	if (cpu == LOONGARCH_CPU(first_cpu)) {
            env->gpr[4] = env->boot_info->a0;
            env->gpr[5] = env->boot_info->a1;
            env->gpr[6] = env->boot_info->a2;
        }
        cpu_set_pc(CPU(cpu), env->elf_address);
    }
}

static void fw_cfg_add_kernel_info(struct loongarch_boot_info *info,
                                   FWCfgState *fw_cfg)
{
    /*
     * Expose the kernel, the command line, and the initrd in fw_cfg.
     * We don't process them here at all, it's all left to the
     * firmware.
     */
    load_image_to_fw_cfg(fw_cfg,
                         FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                         info->kernel_filename,
                         false);

    if (info->initrd_filename) {
        load_image_to_fw_cfg(fw_cfg,
                             FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                             info->initrd_filename, false);
    }

    if (info->kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(info->kernel_cmdline) + 1);
        fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                          info->kernel_cmdline);
    }
}

static void loongarch_firmware_boot(LoongArchMachineState *lams,
                                    struct loongarch_boot_info *info)
{
    fw_cfg_add_kernel_info(info, lams->fw_cfg);
}

static void init_boot_rom(struct loongarch_boot_info *info, void *p)
{
    void *start = p;

    memcpy(p, &slave_boot_code, sizeof(slave_boot_code));
    p += sizeof(slave_boot_code);

    init_cmdline(info, p, start);
    p += COMMAND_LINE_SIZE;

    init_systab(info, p, start);
}

static void loongarch_direct_kernel_boot(struct loongarch_boot_info *info)
{
    void  *p, *bp;
    int64_t kernel_addr = 0;
    LoongArchCPU *lacpu;
    CPUState *cs;

    if (info->kernel_filename) {
        kernel_addr = load_kernel_info(info);
    } else {
        error_report("Need kernel filename\n");
        exit(1);
    }

    /* Load 'boot_rom' at [0 - 1MiB] */
    p = g_malloc0(1 * MiB);
    bp = p;
    init_boot_rom(info, p);
    rom_add_blob_fixed("boot_rom", bp, 1 * MiB, 0);

    CPU_FOREACH(cs) {
        lacpu = LOONGARCH_CPU(cs);
        lacpu->env.load_elf = true;
        if (cs == first_cpu) {
            lacpu->env.elf_address = kernel_addr;
        } else {
            lacpu->env.elf_address = 0;
        }
        lacpu->env.boot_info = info;
    }

    g_free(bp);
}

void loongarch_load_kernel(MachineState *ms, struct loongarch_boot_info *info)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(ms);
    int i;

    /* register reset function */
    for (i = 0; i < ms->smp.cpus; i++) {
        qemu_register_reset(reset_load_elf, LOONGARCH_CPU(qemu_get_cpu(i)));
    }

    info->kernel_filename = ms->kernel_filename;
    info->kernel_cmdline = ms->kernel_cmdline;
    info->initrd_filename = ms->initrd_filename;

    if (lams->bios_loaded) {
        loongarch_firmware_boot(lams, info);
    } else {
        loongarch_direct_kernel_boot(info);
    }
}
