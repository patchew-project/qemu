/*
 * Debug information support.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include <elfutils/libdwfl.h>

#include "debuginfo.h"

static QemuMutex lock;
static Dwfl *dwfl;
static const Dwfl_Callbacks dwfl_callbacks = {
    .find_elf = NULL,
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .section_address = NULL,
    .debuginfo_path = NULL,
};

__attribute__((constructor))
static void debuginfo_init(void)
{
    qemu_mutex_init(&lock);
}

bool debuginfo_report_elf(const char *image_name, int image_fd,
                          target_ulong load_bias)
{
    qemu_mutex_lock(&lock);

    if (dwfl == NULL) {
        dwfl = dwfl_begin(&dwfl_callbacks);
    } else {
        dwfl_report_begin_add(dwfl);
    }

    if (dwfl == NULL) {
        qemu_mutex_unlock(&lock);
        return false;
    }

    dwfl_report_elf(dwfl, image_name, image_name, image_fd, load_bias, true);
    dwfl_report_end(dwfl, NULL, NULL);
    qemu_mutex_unlock(&lock);
    return true;
}

bool debuginfo_get_symbol(target_ulong address,
                          const char **symbol, target_ulong *offset)
{
    Dwfl_Module *dwfl_module;
    GElf_Off dwfl_offset;
    GElf_Sym dwfl_sym;

    qemu_mutex_lock(&lock);

    if (dwfl == NULL) {
        qemu_mutex_unlock(&lock);
        return false;
    }

    dwfl_module = dwfl_addrmodule(dwfl, address);
    if (dwfl_module == NULL) {
        qemu_mutex_unlock(&lock);
        return false;
    }

    *symbol = dwfl_module_addrinfo(dwfl_module, address, &dwfl_offset,
                                   &dwfl_sym, NULL, NULL, NULL);
    if (*symbol == NULL) {
        qemu_mutex_unlock(&lock);
        return false;
    }
    *offset = dwfl_offset;
    qemu_mutex_unlock(&lock);
    return true;
}

bool debuginfo_get_line(target_ulong address,
                        const char **file, int *line)
{
    Dwfl_Module *dwfl_module;
    Dwfl_Line *dwfl_line;

    qemu_mutex_lock(&lock);

    if (dwfl == NULL) {
        qemu_mutex_unlock(&lock);
        return false;
    }

    dwfl_module = dwfl_addrmodule(dwfl, address);
    if (dwfl_module == NULL) {
        qemu_mutex_unlock(&lock);
        return false;
    }

    dwfl_line = dwfl_module_getsrc(dwfl_module, address);
    if (dwfl_line == NULL) {
        qemu_mutex_unlock(&lock);
        return false;
    }
    *file = dwfl_lineinfo(dwfl_line, NULL, line, 0, NULL, NULL);
    qemu_mutex_unlock(&lock);
    return true;
}
