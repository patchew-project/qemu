/*
 * AVR loader helpers
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/loader.h"
#include "elf.h"
#include "boot.h"
#include "qemu/error-report.h"

bool avr_load_firmware(AVRCPU *cpu, MachineState *ms,
                       MemoryRegion *mr, const char *firmware)
{
    const char *filename;
    int bytes_loaded;
    uint64_t entry;
    int e_flags;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (filename == NULL) {
        error_report("Unable to find %s", firmware);
        return false;
    }

    bytes_loaded = load_elf_ram_sym(filename,
                                    NULL, NULL, NULL,
                                    &entry, NULL, NULL,
                                    &e_flags, 0, EM_AVR, 0, 0,
                                    NULL, true, NULL);
    if (bytes_loaded >= 0) {
        /* If ELF file is provided, determine CPU type reading ELF e_flags. */
        const char *elf_cpu = avr_flags_to_cpu_type(e_flags, NULL);
        const char *mcu_cpu_type = object_get_typename(OBJECT(cpu));
        int cpu_len = strlen(mcu_cpu_type) - strlen(AVR_CPU_TYPE_SUFFIX);

        if (entry) {
            error_report("BIOS entry_point must be 0x0000 "
                         "(ELF image '%s' has entry_point 0x%04" PRIx64 ")",
                         firmware, entry);
            return false;
        }
        if (!elf_cpu) {
            warn_report("Could not determine CPU type for ELF image '%s', "
                        "assuming '%.*s' CPU",
                         firmware, cpu_len, mcu_cpu_type);
            return true;
        }
        if (strcmp(elf_cpu, mcu_cpu_type)) {
            error_report("Current machine: %s with '%.*s' CPU",
                         MACHINE_GET_CLASS(ms)->desc, cpu_len, mcu_cpu_type);
            error_report("ELF image '%s' is for '%.*s' CPU",
                         firmware,
                         (int)(strlen(elf_cpu) - strlen(AVR_CPU_TYPE_SUFFIX)),
                         elf_cpu);
            return false;
        }
    } else {
        bytes_loaded = load_image_targphys(filename, OFFSET_CODE,
                                           memory_region_size(mr));
    }
    if (bytes_loaded < 0) {
        error_report("Unable to load firmware image %s as ELF or raw binary",
                     firmware);
        return false;
    }
    return true;
}
