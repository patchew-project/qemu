/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/nvram/fw_cfg.h"
#include "multiboot.h"
#include "multiboot_header.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/sysemu.h"

/* Show multiboot debug output */
//#define DEBUG_MULTIBOOT

#ifdef DEBUG_MULTIBOOT
#define mb_debug(a...) fprintf(stderr, ## a)
#else
#define mb_debug(a...)
#endif

#define MULTIBOOT_STRUCT_ADDR 0x9000

#if MULTIBOOT_STRUCT_ADDR > 0xf0000
#error multiboot struct needs to fit in 16 bit real mode
#endif

/* Region offsets */
#define ADDR_E820_MAP MULTIBOOT_STRUCT_ADDR
#define ADDR_MBI      (ADDR_E820_MAP + 0x500)

typedef struct {
    /* buffer holding kernel, cmdlines and mb_infos */
    void *mb_buf;
    /* address in target */
    hwaddr mb_buf_phys;
    /* size of mb_buf in bytes */
    unsigned mb_buf_size;
    /* offset of mb-info's in bytes */
    hwaddr offset_mbinfo;
    /* offset in buffer for cmdlines in bytes */
    hwaddr offset_cmdlines;
    /* offset in buffer for bootloader name in bytes */
    hwaddr offset_bootloader;
    /* offset of modules in bytes */
    hwaddr offset_mods;
    /* available slots for mb modules infos */
    int mb_mods_avail;
    /* currently used slots of mb modules */
    int mb_mods_count;
} MultibootState;

const char *bootloader_name = "qemu";

static uint32_t mb_add_cmdline(MultibootState *s, const char *cmdline)
{
    hwaddr p = s->offset_cmdlines;
    char *b = (char *)s->mb_buf + p;

    memcpy(b, cmdline, strlen(cmdline) + 1);
    s->offset_cmdlines += strlen(b) + 1;
    return s->mb_buf_phys + p;
}

static uint32_t mb_add_bootloader(MultibootState *s, const char *bootloader)
{
    hwaddr p = s->offset_bootloader;
    char *b = (char *)s->mb_buf + p;

    memcpy(b, bootloader, strlen(bootloader) + 1);
    s->offset_bootloader += strlen(b) + 1;
    return s->mb_buf_phys + p;
}

static void mb_add_mod(MultibootState *s,
                       hwaddr start, hwaddr end,
                       hwaddr cmdline_phys)
{
    multiboot_module_t *mod;
    assert(s->mb_mods_count < s->mb_mods_avail);

    mod = s->mb_buf + s->offset_mbinfo + sizeof(multiboot_module_t) * s->mb_mods_count;

    stl_p(&mod->mod_start, start);
    stl_p(&mod->mod_end,   end);
    stl_p(&mod->cmdline,   cmdline_phys);

    mb_debug("mod%02d: "TARGET_FMT_plx" - "TARGET_FMT_plx"\n",
             s->mb_mods_count, start, end);

    s->mb_mods_count++;
}

int load_multiboot(FWCfgState *fw_cfg,
                   FILE *f,
                   const char *kernel_filename,
                   const char *initrd_filename,
                   const char *kernel_cmdline,
                   int kernel_file_size,
                   uint8_t *header)
{
    int i;
    bool is_multiboot = false;
    uint32_t flags = 0;
    uint32_t mh_entry_addr;
    uint32_t mh_load_addr;
    uint32_t mb_kernel_size;
    MultibootState mbs;
    multiboot_info_t bootinfo;
    uint8_t *mb_bootinfo_data;
    uint32_t cmdline_len;
    struct multiboot_header *multiboot_header;

    /* Ok, let's see if it is a multiboot image.
       The header is 12x32bit long, so the latest entry may be 8192 - 48. */
    for (i = 0; i < (8192 - 48); i += 4) {
        multiboot_header = (struct multiboot_header *)(header + i);
        if (ldl_p(&multiboot_header->magic) == MULTIBOOT_HEADER_MAGIC) {
            uint32_t checksum = ldl_p(&multiboot_header->checksum);
            flags = ldl_p(&multiboot_header->flags);
            checksum += flags;
            checksum += (uint32_t)MULTIBOOT_HEADER_MAGIC;
            if (!checksum) {
                is_multiboot = true;
                break;
            }
        }
    }

    if (!is_multiboot)
        return 0; /* no multiboot */

    mb_debug("qemu: I believe we found a multiboot image!\n");
    memset(&bootinfo, 0, sizeof(bootinfo));
    memset(&mbs, 0, sizeof(mbs));

    if (flags & MULTIBOOT_VIDEO_MODE) {
        fprintf(stderr, "qemu: multiboot knows VBE. we don't.\n");
    }
    if (!(flags & MULTIBOOT_AOUT_KLUDGE)) {
        uint64_t elf_entry;
        uint64_t elf_low, elf_high;
        int kernel_size;
        fclose(f);

        if (((struct elf64_hdr*)header)->e_machine == EM_X86_64) {
            fprintf(stderr, "Cannot load x86-64 image, give a 32bit one.\n");
            exit(1);
        }

        kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                               &elf_low, &elf_high, 0, I386_ELF_MACHINE,
                               0, 0);
        if (kernel_size < 0) {
            fprintf(stderr, "Error while loading elf kernel\n");
            exit(1);
        }
        mh_load_addr = elf_low;
        mb_kernel_size = elf_high - elf_low;
        mh_entry_addr = elf_entry;

        mbs.mb_buf = g_malloc(mb_kernel_size);
        if (rom_copy(mbs.mb_buf, mh_load_addr, mb_kernel_size) != mb_kernel_size) {
            fprintf(stderr, "Error while fetching elf kernel from rom\n");
            exit(1);
        }

        mb_debug("qemu: loading multiboot-elf kernel (%#x bytes) with entry %#zx\n",
                  mb_kernel_size, (size_t)mh_entry_addr);
    } else {
        /* Valid if mh_flags sets MULTIBOOT_AOUT_KLUDGE. */
        uint32_t mh_header_addr = ldl_p(&multiboot_header->header_addr);
        uint32_t mh_load_end_addr = ldl_p(&multiboot_header->load_end_addr);
        uint32_t mh_bss_end_addr = ldl_p(&multiboot_header->bss_end_addr);
        mh_load_addr = ldl_p(&multiboot_header->load_addr);
        uint32_t mb_kernel_text_offset = i - (mh_header_addr - mh_load_addr);
        uint32_t mb_load_size = 0;
        mh_entry_addr = ldl_p(&multiboot_header->entry_addr);

        if (mh_load_end_addr) {
            mb_kernel_size = mh_bss_end_addr - mh_load_addr;
            mb_load_size = mh_load_end_addr - mh_load_addr;
        } else {
            mb_kernel_size = kernel_file_size - mb_kernel_text_offset;
            mb_load_size = mb_kernel_size;
        }

        /* Valid if mh_flags sets MULTIBOOT_VIDEO_MODE.
        uint32_t mh_mode_type = ldl_p(&multiboot_header->mode_type);
        uint32_t mh_width = ldl_p(&multiboot_header->width);
        uint32_t mh_height = ldl_p(&multiboot_header->height);
        uint32_t mh_depth = ldl_p(&multiboot_header->depth); */

        mb_debug("multiboot: mh_header_addr = %#x\n", mh_header_addr);
        mb_debug("multiboot: mh_load_addr = %#x\n", mh_load_addr);
        mb_debug("multiboot: mh_load_end_addr = %#x\n", mh_load_end_addr);
        mb_debug("multiboot: mh_bss_end_addr = %#x\n", mh_bss_end_addr);
        mb_debug("qemu: loading multiboot kernel (%#x bytes) at %#x\n",
                 mb_load_size, mh_load_addr);

        mbs.mb_buf = g_malloc(mb_kernel_size);
        fseek(f, mb_kernel_text_offset, SEEK_SET);
        if (fread(mbs.mb_buf, 1, mb_load_size, f) != mb_load_size) {
            fprintf(stderr, "fread() failed\n");
            exit(1);
        }
        memset(mbs.mb_buf + mb_load_size, 0, mb_kernel_size - mb_load_size);
        fclose(f);
    }

    mbs.mb_buf_phys = mh_load_addr;

    mbs.mb_buf_size = TARGET_PAGE_ALIGN(mb_kernel_size);
    mbs.offset_mbinfo = mbs.mb_buf_size;

    /* Calculate space for cmdlines, bootloader name, and mb_mods */
    cmdline_len = strlen(kernel_filename) + 1;
    cmdline_len += strlen(kernel_cmdline) + 1;
    if (initrd_filename) {
        const char *r = initrd_filename;
        cmdline_len += strlen(r) + 1;
        mbs.mb_mods_avail = 1;
        while (*(r = get_opt_value(NULL, 0, r))) {
           mbs.mb_mods_avail++;
           r++;
        }
    }

    mbs.mb_buf_size += cmdline_len;
    mbs.mb_buf_size += sizeof(multiboot_module_t) * mbs.mb_mods_avail;
    mbs.mb_buf_size += strlen(bootloader_name) + 1;

    mbs.mb_buf_size = TARGET_PAGE_ALIGN(mbs.mb_buf_size);

    /* enlarge mb_buf to hold cmdlines, bootloader, mb-info structs */
    mbs.mb_buf            = g_realloc(mbs.mb_buf, mbs.mb_buf_size);
    mbs.offset_cmdlines   = mbs.offset_mbinfo + mbs.mb_mods_avail * sizeof(multiboot_module_t);
    mbs.offset_bootloader = mbs.offset_cmdlines + cmdline_len;

    if (initrd_filename) {
        const char *next_initrd;
        char not_last, tmpbuf[strlen(initrd_filename) + 1];

        mbs.offset_mods = mbs.mb_buf_size;

        do {
            char *next_space;
            int mb_mod_length;
            uint32_t offs = mbs.mb_buf_size;

            next_initrd = get_opt_value(tmpbuf, sizeof(tmpbuf), initrd_filename);
            not_last = *next_initrd;
            /* if a space comes after the module filename, treat everything
               after that as parameters */
            hwaddr c = mb_add_cmdline(&mbs, tmpbuf);
            if ((next_space = strchr(tmpbuf, ' ')))
                *next_space = '\0';
            mb_debug("multiboot loading module: %s\n", tmpbuf);
            mb_mod_length = get_image_size(tmpbuf);
            if (mb_mod_length < 0) {
                fprintf(stderr, "Failed to open file '%s'\n", tmpbuf);
                exit(1);
            }

            mbs.mb_buf_size = TARGET_PAGE_ALIGN(mb_mod_length + mbs.mb_buf_size);
            mbs.mb_buf = g_realloc(mbs.mb_buf, mbs.mb_buf_size);

            load_image(tmpbuf, (unsigned char *)mbs.mb_buf + offs);
            mb_add_mod(&mbs, mbs.mb_buf_phys + offs,
                       mbs.mb_buf_phys + offs + mb_mod_length, c);

            mb_debug("mod_start: %p\nmod_end:   %p\n  cmdline: "TARGET_FMT_plx"\n",
                     (char *)mbs.mb_buf + offs,
                     (char *)mbs.mb_buf + offs + mb_mod_length, c);
            initrd_filename = next_initrd+1;
        } while (not_last);
    }

    /* Commandline support */
    char kcmdline[strlen(kernel_filename) + strlen(kernel_cmdline) + 2];
    snprintf(kcmdline, sizeof(kcmdline), "%s %s",
             kernel_filename, kernel_cmdline);
    stl_p(&bootinfo.cmdline, mb_add_cmdline(&mbs, kcmdline));

    stl_p(&bootinfo.boot_loader_name, mb_add_bootloader(&mbs, bootloader_name));

    stl_p(&bootinfo.mods_addr,  mbs.mb_buf_phys + mbs.offset_mbinfo);
    stl_p(&bootinfo.mods_count, mbs.mb_mods_count); /* mods_count */

    /* the kernel is where we want it to be now */
    stl_p(&bootinfo.flags, MULTIBOOT_INFO_MEMORY
                           | MULTIBOOT_INFO_BOOTDEV
                           | MULTIBOOT_INFO_CMDLINE
                           | MULTIBOOT_INFO_MODS
                           | MULTIBOOT_INFO_MEM_MAP
                           | MULTIBOOT_INFO_BOOT_LOADER_NAME);
    stl_p(&bootinfo.boot_device, 0x8000ffff); /* XXX: use the -boot switch? */
    stl_p(&bootinfo.mmap_addr,   ADDR_E820_MAP);

    mb_debug("multiboot: mh_entry_addr = %#x\n", mh_entry_addr);
    mb_debug("           mb_buf_phys   = "TARGET_FMT_plx"\n", mbs.mb_buf_phys);
    mb_debug("           mod_start     = "TARGET_FMT_plx"\n", mbs.mb_buf_phys + mbs.offset_mods);
    mb_debug("           mb_mods_count = %d\n", mbs.mb_mods_count);

    /* save bootinfo off the stack */
    mb_bootinfo_data = g_malloc(sizeof(bootinfo));
    memcpy(mb_bootinfo_data, &bootinfo, sizeof(bootinfo));

    /* Pass variables to option rom */
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ENTRY, mh_entry_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, mh_load_addr);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, mbs.mb_buf_size);
    fw_cfg_add_bytes(fw_cfg, FW_CFG_KERNEL_DATA,
                     mbs.mb_buf, mbs.mb_buf_size);

    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, ADDR_MBI);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, sizeof(bootinfo));
    fw_cfg_add_bytes(fw_cfg, FW_CFG_INITRD_DATA, mb_bootinfo_data,
                     sizeof(bootinfo));

    option_rom[nb_option_roms].name = "multiboot.bin";
    option_rom[nb_option_roms].bootindex = 0;
    nb_option_roms++;

    return 1; /* yes, we are multiboot */
}
