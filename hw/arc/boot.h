#ifndef ARC_BOOT_H
#define ARC_BOOT_H

#include "hw/hw.h"
#include "cpu.h"

struct arc_boot_info {
    hwaddr ram_start;
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
};

void arc_cpu_reset(void *opaque);
void arc_load_kernel(ARCCPU *cpu, struct arc_boot_info *boot_info);

#endif /* ARC_BOOT_H */


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
