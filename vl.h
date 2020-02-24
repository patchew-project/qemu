/*
 * Copyright © 2018, 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VL_H
#define VL_H

/***********************************************************/
/* QEMU Block devices */

#define HD_OPTS "media=disk"
#define CDROM_OPTS "media=cdrom"
#define FD_OPTS ""
#define PFLASH_OPTS ""
#define MTD_OPTS ""
#define SD_OPTS ""


#define HAS_ARG 0x0001
typedef struct QEMUOption {
    const char *name;
    int flags;
    int index;
    uint32_t arch_mask;
} QEMUOption;

const QEMUOption *lookup_opt(int argc, char **argv,
                                    const char **poptarg, int *poptind);

int drive_init_func(void *opaque, QemuOpts *opts, Error **errp);
int device_init_func(void *opaque, QemuOpts *opts, Error **errp);

#if defined(CONFIG_MPQEMU)
int rdevice_init_func(void *opaque, QemuOpts *opts, Error **errp);
#endif

#endif /* VL_H */

