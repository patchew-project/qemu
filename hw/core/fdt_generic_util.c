// SPDX-License-Identifier: MIT
/*
 * Utility functions for fdt generic framework
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
 * Copyright (c) 2009 Michal Simek.
 * Copyright (c) 2011 PetaLogix Qld Pty Ltd.
 * Copyright (c) 2011 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>.
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
#include "hw/core/fdt_generic_util.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/system.h"
#include "system/reset.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/config-file.h"
#include "hw/core/boards.h"
#include "qemu/option.h"

#ifndef FDT_GENERIC_UTIL_ERR_DEBUG
#define FDT_GENERIC_UTIL_ERR_DEBUG 3
#endif
#define DB_PRINT(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, ": %s: ", __func__); \
        qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
    } \
} while (0)

#define DB_PRINT_NP(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, "%s", node_path); \
        DB_PRINT((lvl), ## __VA_ARGS__); \
    } \
} while (0)

/* FIXME: wrap direct calls into libfdt */

#include <libfdt.h>
#include <stdlib.h>

static int fdt_generic_num_cpus;

static int simple_bus_fdt_init(char *bus_node_path, FDTMachineInfo *fdti);

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq)
{
    char node_path[DT_PATH_LENGTH];
    FDTMachineInfo *fdti = fdt_init_new_fdti(fdt);

    fdti->irq_base = cpu_irq;

    /* parse the device tree */
    if (!qemu_devtree_get_root_node(fdt, node_path)) {
        memory_region_transaction_begin();
        fdt_init_set_opaque(fdti, node_path, NULL);
        simple_bus_fdt_init(node_path, fdti);
        while (qemu_co_enter_next(fdti->cq, NULL)) {
            ;
        }
        memory_region_transaction_commit();
    } else {
        fprintf(stderr, "FDT: ERROR: cannot get root node from device tree %s\n"
            , node_path);
    }

    /* FIXME: Populate these from DTS and create CPU clusters.  */
    current_machine->smp.cores = fdt_generic_num_cpus;
    current_machine->smp.cpus = fdt_generic_num_cpus;
    current_machine->smp.max_cpus = fdt_generic_num_cpus;

    DB_PRINT(0, "FDT: Device tree scan complete\n");
    return fdti;
}

struct FDTInitNodeArgs {
    char *node_path;
    char *parent_path;
    FDTMachineInfo *fdti;
};

static int fdt_init_qdev(char *node_path, FDTMachineInfo *fdti, char *compat);

static int check_compat(const char *prefix, const char *compat,
                        char *node_path, FDTMachineInfo *fdti)
{
    char *compat_prefixed = g_strconcat(prefix, compat, NULL);
    const int done = !fdt_init_compat(node_path, fdti, compat_prefixed);
    g_free(compat_prefixed);
    return done;
}

static void fdt_init_node(void *args)
{
    struct FDTInitNodeArgs *a = args;
    char *node_path = a->node_path;
    FDTMachineInfo *fdti = a->fdti;
    g_free(a);

    simple_bus_fdt_init(node_path, fdti);

    const char *prop;
    char *all_compats = NULL, *node_name;
    char *device_type = NULL;
    int prop_len;

    DB_PRINT_NP(1, "enter\n");

    /* try instance binding first */
    node_name = qemu_devtree_get_node_name(fdti->fdt, node_path);
    DB_PRINT_NP(1, "node with name: %s\n", node_name ? node_name : "(none)");
    if (!node_name) {
        printf("FDT: ERROR: nameless node: %s\n", node_path);
    }
    if (!fdt_init_inst_bind(node_path, fdti, node_name)) {
        DB_PRINT_NP(0, "instance bind successful\n");
        goto exit;
    }

    /* fallback to compatibility binding */
    prop = qemu_fdt_getprop(fdti->fdt, node_path, "compatible",
                            &prop_len, false, NULL);
    all_compats = g_memdup2(prop, prop_len);
    if (all_compats) {
        char *compat = all_compats;
        char * const end = &all_compats[prop_len - 1]; /* points to nul */

        while (compat < end) {
            if (check_compat("compatible:", compat, node_path, fdti)) {
                goto exit;
            }

            if (!fdt_init_qdev(node_path, fdti, compat)) {
                check_compat("postinit:", compat, node_path, fdti);
                goto exit;
            }

            /* Scan forward to the end of the current compat. */
            while (compat < end && *compat) {
                ++compat;
            }

            /* Replace nul with space for the debug printf below. */
            if (compat < end) {
                *compat++ = ' ';
            }
        };
    } else {
        DB_PRINT_NP(0, "no compatibility found\n");
    }

    /*
     * Try to create the device using device_type property
     * Not every device tree node has compatible  property, so
     * try with device_type.
     */
    prop = qemu_fdt_getprop(fdti->fdt, node_path,
                            "device_type", &prop_len, false, NULL);
    device_type = g_memdup2(prop, prop_len);
    if (device_type) {
        if (check_compat("device_type:", device_type, node_path, fdti)) {
            goto exit;
        }

        if (!fdt_init_qdev(node_path, fdti, device_type)) {
            goto exit;
        }
    }

    if (all_compats) {
        DB_PRINT_NP(0, "FDT: Unsupported peripheral invalidated - "
                    "compatibilities %s\n", all_compats);
        qemu_fdt_setprop_string(fdti->fdt, node_path, "compatible",
                                "invalidated");
    }
exit:

    DB_PRINT_NP(1, "exit\n");

    if (!fdt_init_has_opaque(fdti, node_path)) {
        fdt_init_set_opaque(fdti, node_path, NULL);
    }
    g_free(node_path);
    g_free(all_compats);
    g_free(device_type);
    return;
}

static int simple_bus_fdt_init(char *node_path, FDTMachineInfo *fdti)
{
    int i;
    int num_children = qemu_devtree_get_num_children(fdti->fdt, node_path,
                                                        1);
    char **children;

    if (num_children == 0) {
        return 0;
    }
    children = qemu_devtree_get_children(fdti->fdt, node_path, 1);

    DB_PRINT_NP(num_children ? 0 : 1, "num child devices: %d\n", num_children);

    for (i = 0; i < num_children; i++) {
        struct FDTInitNodeArgs *init_args = g_malloc0(sizeof(*init_args));
        init_args->node_path = children[i];
        init_args->fdti = fdti;
        qemu_coroutine_enter(qemu_coroutine_create(fdt_init_node, init_args));
    }

    g_free(children);
    return 0;
}

static int fdt_init_qdev(char *node_path, FDTMachineInfo *fdti, char *compat)
{
    return 0;
}

