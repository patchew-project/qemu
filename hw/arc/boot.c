/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#include "qemu/osdep.h"
#include "boot.h"
#include "elf.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "qemu/units.h"

void arc_cpu_reset(void *opaque)
{
    ARCCPU *cpu = opaque;
    CPUARCState *env = &cpu->env;
    const struct arc_boot_info *info = env->boot_info;

    cpu_reset(CPU(cpu));

    /*
     * Right before start CPU gets reset wiping out everything
     * but PC which we set on Elf load.
     *
     * And if we still want to pass something like U-Boot data
     * via CPU registers we have to do it here.
     */

    if (info->kernel_cmdline && strlen(info->kernel_cmdline)) {
        /*
         * Load "cmdline" far enough from the kernel image.
         * Round by MAX page size for ARC - 16 KiB.
         */
        hwaddr cmdline_addr = info->ram_start +
            QEMU_ALIGN_UP(info->ram_size / 2, 16 * KiB);
        cpu_physical_memory_write(cmdline_addr, info->kernel_cmdline,
                                  strlen(info->kernel_cmdline));

        /* We're passing "cmdline" */
        cpu->env.r[0] = ARC_UBOOT_CMDLINE;
        cpu->env.r[2] = cmdline_addr;
    }
}


void arc_load_kernel(ARCCPU *cpu, struct arc_boot_info *info)
{
    hwaddr entry;
    int elf_machine, kernel_size;

    if (!info->kernel_filename) {
        error_report("missing kernel file");
        exit(EXIT_FAILURE);
    }

    elf_machine = cpu->env.family > 2 ? EM_ARC_COMPACT2 : EM_ARC_COMPACT;
    kernel_size = load_elf(info->kernel_filename, NULL, NULL, NULL,
                           &entry, NULL, NULL, NULL, ARC_ENDIANNESS_LE,
                           elf_machine, 1, 0);

    if (kernel_size < 0) {
        int is_linux;

        kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                  &is_linux, NULL, NULL);
        if (!is_linux) {
            error_report("Wrong U-Boot image, only Linux kernel is supported");
            exit(EXIT_FAILURE);
        }
    }

    if (kernel_size < 0) {
        error_report("No kernel image found");
        exit(EXIT_FAILURE);
    }

    cpu->env.boot_info = info;

    /* Set CPU's PC to point to the entry-point */
    cpu->env.pc = entry;
}


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
