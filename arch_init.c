/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "sysemu/sysemu.h"
#include "sysemu/arch_init.h"
#include "hw/pci/pci.h"
#include "hw/audio/soundhw.h"
#include "hw/smbios/smbios.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qmp-commands.h"
#include "hw/acpi/acpi.h"
#include "qemu/help_option.h"

#ifdef TARGET_SPARC
int graphic_width = 1024;
int graphic_height = 768;
int graphic_depth = 8;
#else
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 32;
#endif


#if defined(TARGET_ALPHA)
#define QEMU_ARCH QEMU_ARCH_ALPHA
#elif defined(TARGET_ARM)
#define QEMU_ARCH QEMU_ARCH_ARM
#elif defined(TARGET_CRIS)
#define QEMU_ARCH QEMU_ARCH_CRIS
#elif defined(TARGET_I386)
#define QEMU_ARCH QEMU_ARCH_I386
#elif defined(TARGET_M68K)
#define QEMU_ARCH QEMU_ARCH_M68K
#elif defined(TARGET_LM32)
#define QEMU_ARCH QEMU_ARCH_LM32
#elif defined(TARGET_MICROBLAZE)
#define QEMU_ARCH QEMU_ARCH_MICROBLAZE
#elif defined(TARGET_MIPS)
#define QEMU_ARCH QEMU_ARCH_MIPS
#elif defined(TARGET_MOXIE)
#define QEMU_ARCH QEMU_ARCH_MOXIE
#elif defined(TARGET_OPENRISC)
#define QEMU_ARCH QEMU_ARCH_OPENRISC
#elif defined(TARGET_PPC)
#define QEMU_ARCH QEMU_ARCH_PPC
#elif defined(TARGET_S390X)
#define QEMU_ARCH QEMU_ARCH_S390X
#elif defined(TARGET_SH4)
#define QEMU_ARCH QEMU_ARCH_SH4
#elif defined(TARGET_SPARC)
#define QEMU_ARCH QEMU_ARCH_SPARC
#elif defined(TARGET_XTENSA)
#define QEMU_ARCH QEMU_ARCH_XTENSA
#elif defined(TARGET_UNICORE32)
#define QEMU_ARCH QEMU_ARCH_UNICORE32
#elif defined(TARGET_TRICORE)
#define QEMU_ARCH QEMU_ARCH_TRICORE
#endif

const uint32_t arch_type = QEMU_ARCH;

static struct defconfig_file {
    const char *filename;
    /* Indicates it is an user config file (disabled by -no-user-config) */
    bool userconfig;
} default_config_files[] = {
    { CONFIG_QEMU_CONFDIR "/qemu.conf",                   true },
    { NULL }, /* end of list */
};

int qemu_read_default_config_files(bool userconfig)
{
    int ret;
    struct defconfig_file *f;

    for (f = default_config_files; f->filename; f++) {
        if (!userconfig && f->userconfig) {
            continue;
        }
        ret = qemu_read_config_file(f->filename);
        if (ret < 0 && ret != -ENOENT) {
            return ret;
        }
    }

    return 0;
}

void do_acpitable_option(const QemuOpts *opts)
{
#ifdef TARGET_I386
    Error *err = NULL;

    acpi_table_add(opts, &err);
    if (err) {
        error_reportf_err(err, "Wrong acpi table provided: ");
        exit(1);
    }
#endif
}

void do_smbios_option(QemuOpts *opts)
{
#ifdef TARGET_I386
    smbios_entry_add(opts);
#endif
}

int kvm_available(void)
{
#ifdef CONFIG_KVM
    return 1;
#else
    return 0;
#endif
}

int xen_available(void)
{
#ifdef CONFIG_XEN
    return 1;
#else
    return 0;
#endif
}


TargetInfo *qmp_query_target(Error **errp)
{
    TargetInfo *info = g_malloc0(sizeof(*info));

    info->arch = g_strdup(TARGET_NAME);

    return info;
}
