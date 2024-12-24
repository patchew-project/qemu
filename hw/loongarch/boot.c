/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch boot helper functions.
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include <zlib.h>
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "target/loongarch/cpu.h"
#include "hw/loongarch/virt.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "system/reset.h"
#include "system/qtest.h"

/*
 * Linux Image Format
 * https://docs.kernel.org/arch/loongarch/booting.html
 */
#define LINUX_PE_MAGIC  0x818223cd
#define MZ_MAGIC        0x5a4d /* "MZ" */

struct loongarch_linux_hdr {
    uint32_t mz_magic;
    uint32_t res0;
    uint64_t kernel_entry;
    uint64_t kernel_size;
    uint64_t load_offset;
    uint64_t res1;
    uint64_t res2;
    uint64_t res3;
    uint32_t linux_pe_magic;
    uint32_t pe_header_offset;
} QEMU_PACKED;

struct memmap_entry *memmap_table;
unsigned memmap_entries;

ram_addr_t initrd_offset;
uint64_t initrd_size;

static void generate_secondary_boot_code(void *boot_code)
{
    uint32_t *p = boot_code;

    /* Configure reset ebase. */
    stl_p(p++, 0x0400302c); /* csrwr      $t0, LOONGARCH_CSR_EENTRY  */

    /* Disable interrupt. */
    stl_p(p++, 0x0380100c); /* ori        $t0, $zero, 0x4            */
    stl_p(p++, 0x04000180); /* csrxchg    $zero, $t0, LOONGARCH_CSR_CRMD */

    /* Clear mailbox. */
    stl_p(p++, 0x1400002d); /* lu12i.w    $t1, 1(0x1)                */
    stl_p(p++, 0x038081ad); /* ori        $t1, $t1, CORE_BUF_20      */
    stl_p(p++, 0x06481da0); /* iocsrwr.d  $zero, $t1                 */

    /* Enable IPI interrupt. */
    stl_p(p++, 0x1400002c); /* lu12i.w    $t0, 1(0x1)                */
    stl_p(p++, 0x0400118c); /* csrxchg    $t0, $t0, LOONGARCH_CSR_ECFG */
    stl_p(p++, 0x02fffc0c); /* addi.d     $t0, $r0, -1(0xfff)        */
    stl_p(p++, 0x1400002d); /* lu12i.w    $t1, 1(0x1)                */
    stl_p(p++, 0x038011ad); /* ori        $t1, $t1, CORE_EN_OFF      */
    stl_p(p++, 0x064819ac); /* iocsrwr.w  $t0, $t1                   */
    stl_p(p++, 0x1400002d); /* lu12i.w    $t1, 1(0x1)                */
    stl_p(p++, 0x038081ad); /* ori        $t1, $t1, CORE_BUF_20      */

    /* Wait for wakeup <.L11>: */
    stl_p(p++, 0x06488000); /* idle       0x0                        */
    stl_p(p++, 0x03400000); /* andi       $zero, $zero, 0x0          */
    stl_p(p++, 0x064809ac); /* iocsrrd.w  $t0, $t1                   */
    stl_p(p++, 0x43fff59f); /* beqz       $t0, -12(0x7ffff4) # 48 <.L11> */

    /* Read and clear IPI interrupt. */
    stl_p(p++, 0x1400002d); /* lu12i.w    $t1, 1(0x1)                */
    stl_p(p++, 0x064809ac); /* iocsrrd.w  $t0, $t1                   */
    stl_p(p++, 0x1400002d); /* lu12i.w    $t1, 1(0x1)                */
    stl_p(p++, 0x038031ad); /* ori        $t1, $t1, CORE_CLEAR_OFF   */
    stl_p(p++, 0x064819ac); /* iocsrwr.w  $t0, $t1                   */

    /* Disable IPI interrupt. */
    stl_p(p++, 0x1400002c); /* lu12i.w    $t0, 1(0x1)                */
    stl_p(p++, 0x04001180); /* csrxchg    $zero, $t0, LOONGARCH_CSR_ECFG */

    /* Read mail buf and jump to specified entry. */
    stl_p(p++, 0x1400002d); /* lu12i.w    $t1, 1(0x1)                */
    stl_p(p++, 0x038081ad); /* ori        $t1, $t1, CORE_BUF_20      */
    stl_p(p++, 0x06480dac); /* iocsrrd.d  $t0, $t1                   */
    stl_p(p++, 0x00150181); /* move       $ra, $t0                   */
    stl_p(p++, 0x4c000020); /* jirl       $zero, $ra, 0              */
}

static inline void *guidcpy(void *dst, const void *src)
{
    return memcpy(dst, src, sizeof(efi_guid_t));
}

static void efi_hdr_crc32(efi_table_hdr_t *hdr)
{
    uint32_t val;

    hdr->crc32 = 0;
    val = crc32(0, (const unsigned char *)hdr, hdr->headersize);
    hdr->crc32 = cpu_to_le32(val);
}

static void init_efi_vendor_string(void **p)
{
    uint16_t *vendor_str = *p;

    /* QEMU in UTF16-LE */
    stw_le_p(vendor_str++, 0x0051); /* Q */
    stw_le_p(vendor_str++, 0x0045); /* E */
    stw_le_p(vendor_str++, 0x004D); /* M */
    stw_le_p(vendor_str++, 0x0055); /* U */
    stw_le_p(vendor_str++, 0x0000); /* \0 */

    *p = vendor_str;
    *p = QEMU_ALIGN_PTR_UP(*p, sizeof(target_long));
}

static void memmap_write_descs(efi_memory_desc_t *map)
{
    int i;

    for (i = 0; i < memmap_entries; i++) {
        uint32_t efi_type;
        hwaddr start = memmap_table[i].address;
        hwaddr end = memmap_table[i].address + memmap_table[i].length;

        switch (memmap_table[i].type) {
        case MEMMAP_TYPE_MEMORY:
            efi_type = EFI_CONVENTIONAL_MEMORY;
            break;
        case MEMMAP_TYPE_RESERVED:
            efi_type = EFI_RESERVED_TYPE;
            break;
        case MEMMAP_TYPE_ACPI:
            efi_type = EFI_ACPI_RECLAIM_MEMORY;
            break;
        case MEMMAP_TYPE_NVS:
            efi_type = EFI_ACPI_MEMORY_NVS;
            break;
        default:
            efi_type = EFI_RESERVED_TYPE;
            break;
        }

        if (memmap_table[i].reserved) {
            start = QEMU_ALIGN_DOWN(start, EFI_PAGE_SIZE);
            end = QEMU_ALIGN_UP(end, EFI_PAGE_SIZE);
        } else {
            start = QEMU_ALIGN_UP(start, EFI_PAGE_SIZE);
            end = QEMU_ALIGN_DOWN(end, EFI_PAGE_SIZE);
        }

        map[i].type = cpu_to_le32(efi_type);
        map[i].phys_addr = cpu_to_le64(start);
        map[i].virt_addr = cpu_to_le64(start);
        map[i].num_pages = cpu_to_le64((end - start) >> EFI_PAGE_SHIFT);
    }
}

#define EFI_BOOT_MEMMAP_TABLE_GEN(type)                                     \
static void init_efi_boot_memmap_##type(void *guidp, void **p)              \
{                                                                           \
    struct efi_boot_memmap_##type *boot_memmap = *p;                        \
    efi_guid_t tbl_guid = LINUX_EFI_BOOT_MEMMAP_GUID;                       \
                                                                            \
    /* efi_configuration_table 1 */                                         \
    guidcpy(guidp, &tbl_guid);                                              \
                                                                            \
    boot_memmap->desc_size = cpu_to_le##type(sizeof(efi_memory_desc_t));    \
    boot_memmap->desc_ver = cpu_to_le32(1);                                 \
    boot_memmap->map_size = cpu_to_le##type(boot_memmap->desc_size *        \
                                            memmap_entries);                \
    memmap_write_descs(boot_memmap->map);                                   \
    *p += sizeof(struct efi_boot_memmap_##type);                            \
}

#define EFI_INITRD_TABLE_GEN(type)                                          \
static void init_efi_initrd_table_##type(void *guidp, void **p)             \
{                                                                           \
    efi_guid_t tbl_guid = LINUX_EFI_INITRD_MEDIA_GUID;                      \
    struct efi_initrd_##type *initrd_table = *p;                            \
                                                                            \
    /* efi_configuration_table  */                                          \
    guidcpy(guidp, &tbl_guid);                                              \
                                                                            \
    initrd_table->base = cpu_to_le##type(initrd_offset);                    \
    initrd_table->size = cpu_to_le##type(initrd_size);                      \
    *p += sizeof(struct efi_initrd_##type);                                 \
}

#define BOOTP_ALIGN_PTR_UP(p, s, n) ({                                      \
    uintptr_t __ptr = (uintptr_t)(s) +                                      \
        QEMU_ALIGN_UP((uintptr_t)(p) - (uintptr_t)(s), n);                  \
    (typeof(p))(__ptr);                                                     \
})

#define EFI_INIT_SYSTAB_GEN(type)                                           \
    EFI_BOOT_MEMMAP_TABLE_GEN(type)                                         \
    EFI_INITRD_TABLE_GEN(type)                                              \
static void init_systab_##type(struct loongarch_boot_info *info,            \
                               void *p, void *start)                        \
{                                                                           \
    uint32_t nr_tables = 0;                                                 \
    const efi_guid_t fdt_guid = DEVICE_TREE_GUID;                           \
    struct efi_system_table_##type *systab;                                 \
    struct efi_configuration_table_##type *cfg_tabs;                        \
                                                                            \
    p = BOOTP_ALIGN_PTR_UP(p, start, EFI_TABLE_ALIGN);                      \
    systab = p;                                                             \
    info->a2 = p - start;                                                   \
                                                                            \
    systab->hdr.signature = cpu_to_le64(EFI_SYSTEM_TABLE_SIGNATURE);        \
    systab->hdr.revision = cpu_to_le32(EFI_SPECIFICATION_VERSION);          \
    systab->hdr.headersize =                                                \
            cpu_to_le32(sizeof(struct efi_system_table_##type));            \
    systab->fw_revision =                                                   \
            cpu_to_le32(FW_VERSION << 16 | FW_PATCHLEVEL << 8);             \
    systab->runtime = 0;                                                    \
    systab->boottime = 0;                                                   \
    systab->nr_tables = 0;                                                  \
                                                                            \
    p += sizeof(struct efi_system_table_##type);                            \
    systab->fw_vendor = cpu_to_le##type(p - start);                         \
    init_efi_vendor_string(&p);                                             \
                                                                            \
    p = BOOTP_ALIGN_PTR_UP(p, start, EFI_TABLE_ALIGN);                      \
    systab->tables = cpu_to_le##type(p - start);                            \
    cfg_tabs = p;                                                           \
    p += sizeof(struct efi_configuration_table_##type) * 3;                 \
                                                                            \
    p = BOOTP_ALIGN_PTR_UP(p, start, EFI_TABLE_ALIGN);                      \
    cfg_tabs[nr_tables].table = cpu_to_le##type(p - start);                 \
    init_efi_boot_memmap_##type(&cfg_tabs[nr_tables].guid, &p);             \
    nr_tables++;                                                            \
                                                                            \
    if (initrd_size > 0) {                                                  \
        cfg_tabs[nr_tables].table = cpu_to_le##type(p - start);             \
        init_efi_initrd_table_##type(&cfg_tabs[nr_tables].guid, &p);        \
        nr_tables++;                                                        \
    }                                                                       \
                                                                            \
    guidcpy(&cfg_tabs[nr_tables].guid, &fdt_guid);                          \
    cfg_tabs[nr_tables].table = cpu_to_le##type(FDT_BASE);                  \
    nr_tables++;                                                            \
                                                                            \
    systab->nr_tables = cpu_to_le32(nr_tables);                             \
    efi_hdr_crc32(&systab->hdr);                                            \
}

EFI_INIT_SYSTAB_GEN(32)
EFI_INIT_SYSTAB_GEN(64)

static void init_cmdline(struct loongarch_boot_info *info, void *p, void *start)
{
    hwaddr cmdline_addr = p - start;

    info->a0 = 1;
    info->a1 = cmdline_addr;

    g_strlcpy(p, info->kernel_cmdline, COMMAND_LINE_SIZE);
}

static uint64_t cpu_loongarch_virt_to_phys(void *opaque, uint64_t addr)
{
    return addr & MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS);
}

static int64_t load_loongarch_linux_image(const char *filename,
                                          uint64_t *kernel_entry,
                                          uint64_t *kernel_low,
                                          uint64_t *kernel_high)
{
    gsize len;
    ssize_t size;
    uint8_t *buffer;
    struct loongarch_linux_hdr *hdr;

    /* Load as raw file otherwise */
    if (!g_file_get_contents(filename, (char **)&buffer, &len, NULL)) {
        return -1;
    }
    size = len;

    /* Unpack the image if it is a EFI zboot image */
    if (unpack_efi_zboot_image(&buffer, &size) < 0) {
        g_free(buffer);
        return -1;
    }

    hdr = (struct loongarch_linux_hdr *)buffer;

    if (extract32(le32_to_cpu(hdr->mz_magic), 0, 16) != MZ_MAGIC ||
        le32_to_cpu(hdr->linux_pe_magic) != LINUX_PE_MAGIC) {
        g_free(buffer);
        return -1;
    }

    /* Early kernel versions may have those fields in virtual address */
    *kernel_entry = extract64(le64_to_cpu(hdr->kernel_entry),
                              0, TARGET_PHYS_ADDR_SPACE_BITS);
    *kernel_low = extract64(le64_to_cpu(hdr->load_offset),
                            0, TARGET_PHYS_ADDR_SPACE_BITS);
    *kernel_high = *kernel_low + size;

    rom_add_blob_fixed(filename, buffer, size, *kernel_low);

    g_free(buffer);

    return size;
}

static int64_t load_kernel_info(struct loongarch_boot_info *info)
{
    uint64_t kernel_entry, kernel_low, kernel_high;
    ssize_t kernel_size;

    kernel_size = load_elf(info->kernel_filename, NULL,
                           cpu_loongarch_virt_to_phys, NULL,
                           &kernel_entry, &kernel_low,
                           &kernel_high, NULL, ELFDATA2LSB,
                           EM_LOONGARCH, 1, 0);
    kernel_entry = cpu_loongarch_virt_to_phys(NULL, kernel_entry);
    if (kernel_size < 0) {
        kernel_size = load_loongarch_linux_image(info->kernel_filename,
                                                 &kernel_entry, &kernel_low,
                                                 &kernel_high);
    }

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
        initrd_size = 0;
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

static void loongarch_firmware_boot(LoongArchVirtMachineState *lvms,
                                    struct loongarch_boot_info *info)
{
    fw_cfg_add_kernel_info(info, lvms->fw_cfg);
}

static void init_boot_rom(struct loongarch_boot_info *info, void *p,
                          bool is_64bit)
{
    void *start = p;

    init_cmdline(info, p, start);
    p += COMMAND_LINE_SIZE;

    if (is_64bit)
        init_systab_64(info, p, start);
    else
        init_systab_32(info, p, start);
}

static void loongarch_direct_kernel_boot(struct loongarch_boot_info *info)
{
    void *p, *bp;
    int64_t kernel_addr = VIRT_FLASH0_BASE;
    LoongArchCPU *lacpu = LOONGARCH_CPU(first_cpu);
    CPUState *cs;

    if (info->kernel_filename) {
        kernel_addr = load_kernel_info(info);
    } else {
        if (!qtest_enabled()) {
            warn_report("No kernel provided, booting from flash drive.");
        }
    }

    /* Load cmdline and system tables at [0 - 1 MiB] */
    p = g_malloc0(1 * MiB);
    bp = p;
    init_boot_rom(info, p, is_la64(&lacpu->env));
    rom_add_blob_fixed_as("boot_info", bp, 1 * MiB, 0, &address_space_memory);

    /* Load slave boot code at pflash0 . */
    void *boot_code = g_malloc0(VIRT_FLASH0_SIZE);
    generate_secondary_boot_code(boot_code);
    rom_add_blob_fixed("boot_code", boot_code, VIRT_FLASH0_SIZE, VIRT_FLASH0_BASE);

    CPU_FOREACH(cs) {
        lacpu = LOONGARCH_CPU(cs);
        lacpu->env.load_elf = true;
        if (cs == first_cpu) {
            lacpu->env.elf_address = kernel_addr;
        } else {
            lacpu->env.elf_address = VIRT_FLASH0_BASE;
        }
        lacpu->env.boot_info = info;
    }

    g_free(boot_code);
    g_free(bp);
}

void loongarch_load_kernel(MachineState *ms, struct loongarch_boot_info *info)
{
    LoongArchVirtMachineState *lvms = LOONGARCH_VIRT_MACHINE(ms);
    int i;

    /* register reset function */
    for (i = 0; i < ms->smp.cpus; i++) {
        qemu_register_reset(reset_load_elf, LOONGARCH_CPU(qemu_get_cpu(i)));
    }

    info->kernel_filename = ms->kernel_filename;
    info->kernel_cmdline = ms->kernel_cmdline;
    info->initrd_filename = ms->initrd_filename;

    if (lvms->bios_loaded) {
        loongarch_firmware_boot(lvms, info);
    } else {
        loongarch_direct_kernel_boot(info);
    }
}
