/*
 * SPDX-License-Identifier: MIT
 *
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
#include "hw/core/qdev-properties.h"
#include "hw/cpu/cluster.h"
#include "qobject/qlist.h"

#define fdt_debug(...) do { \
    qemu_log_mask(LOG_FDT, ": %s: ", __func__); \
    qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
} while (0)

#define fdt_debug_np(...) do { \
    qemu_log_mask(LOG_FDT, "%s", node_path); \
    fdt_debug(__VA_ARGS__); \
} while (0)

/* FIXME: wrap direct calls into libfdt */

#include <libfdt.h>
#include <stdlib.h>

static int fdt_generic_num_cpus;

static int simple_bus_fdt_init(const char *bus_node_path, FDTMachineInfo *fdti);

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq)
{
    FDTMachineInfo *fdti = fdt_init_new_fdti(fdt);

    fdti->irq_base = cpu_irq;

    /* parse the device tree */
    memory_region_transaction_begin();
    fdt_init_set_opaque(fdti, "/", NULL);
    simple_bus_fdt_init("/", fdti);
    while (qemu_co_enter_next(fdti->cq, NULL)) {
        ;
    }
    memory_region_transaction_commit();

    /* FIXME: Populate these from DTS and create CPU clusters.  */
    current_machine->smp.cores = fdt_generic_num_cpus;
    current_machine->smp.cpus = fdt_generic_num_cpus;
    current_machine->smp.max_cpus = fdt_generic_num_cpus;

    fdt_debug("FDT: Device tree scan complete\n");
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

    fdt_debug_np("enter\n");

    /* try instance binding first */
    node_name = strrchr(node_path, '/') + 1;
    fdt_debug_np("node with name: %s\n", node_name ? node_name : "(none)");
    if (!node_name) {
        fdt_debug("FDT: ERROR: nameless node: %s\n", node_path);
    }
    if (!fdt_init_inst_bind(node_path, fdti, node_name)) {
        fdt_debug_np("instance bind successful\n");
        goto exit;
    }

    /* fallback to compatibility binding */
    prop = qemu_fdt_getprop(fdti->fdt, node_path, "compatible",
                                       &prop_len, NULL);
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
        fdt_debug_np("no compatibility found\n");
    }

    /*
     * Try to create the device using device_type property
     * Not every device tree node has compatible  property, so
     * try with device_type.
     */
    prop = qemu_fdt_getprop(fdti->fdt, node_path,
                            "device_type", &prop_len, NULL);
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
        fdt_debug_np("FDT: Unsupported peripheral invalidated - "
                    "compatibilities %s\n", all_compats);
        qemu_fdt_setprop_string(fdti->fdt, node_path, "compatible",
                                "invalidated");
    }
exit:

    fdt_debug_np("exit\n");

    if (!fdt_init_has_opaque(fdti, node_path)) {
        fdt_init_set_opaque(fdti, node_path, NULL);
    }
    g_free(node_path);
    g_free(all_compats);
    g_free(device_type);
    return;
}

static int simple_bus_fdt_init(const char *node_path, FDTMachineInfo *fdti)
{
    int i;
    char **children;
    int num_children = qemu_devtree_get_num_children(fdti->fdt, node_path);

    if (num_children <= 0) {
        return 0;
    }

    children = g_malloc0(sizeof(*children) * num_children);

    num_children = qemu_devtree_get_children(fdti->fdt, node_path, num_children,
                                             children);

    fdt_debug_np("num child devices: %d\n", num_children);

    for (i = 0; i < num_children; i++) {
        struct FDTInitNodeArgs *init_args = g_malloc0(sizeof(*init_args));
        init_args->node_path = children[i];
        init_args->fdti = fdti;
        qemu_coroutine_enter(qemu_coroutine_create(fdt_init_node, init_args));
    }

    g_free(children);
    return 0;
}

static inline const char *trim_vendor(const char *s)
{
    /* FIXME: be more intelligent */
    const char *ret = memchr(s, ',', strlen(s));
    return ret ? ret + 1 : s;
}

static Object *fdt_create_from_compat(const char *compat)
{
    Object *ret = NULL;

    /* Try to create the object */
    ret = object_new(compat);
    if (!ret) {
        const char *no_vendor = trim_vendor(compat);

        if (no_vendor != compat) {
            return fdt_create_from_compat(no_vendor);
        }
    }
    return ret;
}

/*FIXME: roll into device tree functionality */

static inline uint64_t get_int_be(const void *p, int len)
{
    switch (len) {
    case 1:
        return *((uint8_t *)p);
    case 2:
        return be16_to_cpu(*((uint16_t *)p));
    case 4:
        return be32_to_cpu(*((uint32_t *)p));
    case 8:
        return be32_to_cpu(*((uint64_t *)p));
    default:
        fprintf(stderr, "unsupported integer length\n");
        abort();
    }
}

static void fdt_init_qdev_link_prop(Object *obj, ObjectProperty *p,
                                    FDTMachineInfo *fdti,
                                    const char *node_path,
                                    const char *propname, const void* val,
                                    int len)
{
    Object *linked_dev;
    char target_node_path[DT_PATH_LENGTH];
    Error *errp = NULL;

    if (qemu_devtree_get_node_by_phandle(fdti->fdt, target_node_path,
                                         get_int_be(val, len))) {
        error_report("FDT: Invalid phandle in property '%s' of node '%s'",
                     propname, node_path);
        return;
    }

    while (!fdt_init_has_opaque(fdti, target_node_path)) {
        fdt_init_yield(fdti);
    }

    linked_dev = fdt_init_get_opaque(fdti, target_node_path);

    object_property_set_link(obj, propname, linked_dev, &errp);
    if (!errp) {
        fdt_debug_np("set link %s\n", propname);
        return;
    }

    error_free(errp);
    errp = NULL;

    if (object_dynamic_cast(linked_dev, TYPE_DEVICE)) {
        BusState *child_bus;

        /* Check if target has a child bus that satisfies the link type */
        QLIST_FOREACH(child_bus, &DEVICE(linked_dev)->child_bus, sibling) {
            object_property_set_link(obj, propname, OBJECT(child_bus), &errp);
            if (!errp) {
                fdt_debug_np("found matching bus link %s\n",
                                child_bus->name);
                return;
            }

            error_free(errp);
            errp = NULL;
        }
    }

    fdt_debug_np("failed to set link %s\n", propname);
}

static void fdt_init_qdev_scalar_prop(Object *obj, ObjectProperty *p,
                                      FDTMachineInfo *fdti,
                                      const char *node_path,
                                      const char *propname, const void* val,
                                      int len)
{

    /* FIXME: handle generically using accessors and stuff */
    if (!strncmp(p->type, "link", 4)) {
        fdt_init_qdev_link_prop(obj, p, fdti, node_path, propname, val, len);
        return;
    }

    if (!strcmp(p->type, "uint8") || !strcmp(p->type, "uint16") ||
        !strcmp(p->type, "uint32") || !strcmp(p->type, "uint64") ||
        !strcmp(p->type, "int8") || !strcmp(p->type, "int16") ||
        !strcmp(p->type, "int32") || !strcmp(p->type, "int64")) {
        object_property_set_int(obj, propname,
                                get_int_be(val, len), &error_abort);
        fdt_debug_np("set property %s to 0x%llx\n", propname,
                    (unsigned long long)get_int_be(val, len));
        return;
    }

    if (!strcmp(p->type, "boolean") || !strcmp(p->type, "bool")) {
        object_property_set_bool(obj, propname,
                                 !!get_int_be(val, len), &error_abort);
        fdt_debug_np("set property %s to %s\n", propname,
                    get_int_be(val, len) ? "true" : "false");
        return;
    }

    if (!strcmp(p->type, "string") || !strcmp(p->type, "str")) {
        object_property_set_str(obj, propname,
                                (const char *)val, &error_abort);
        fdt_debug_np("set property %s to %s\n", propname, (const char *)val);
        return;
    }

    fdt_debug_np("WARNING: property is of unknown type\n");
}

static size_t fdt_array_elem_len(FDTMachineInfo *fdti,
                                 const char *node_path,
                                 const char *propname)
{
    g_autofree char *elem_cells_propname = NULL;
    Error *err = NULL;
    uint32_t elem_cells;

    /*
     * Default element size to 1 uint32_t cell, unless it is explicitly
     * given in the same FDT node (not inherited).
     */
    elem_cells_propname = g_strconcat("#", propname, "-cells", NULL);
    elem_cells = qemu_fdt_getprop_cell(fdti->fdt, node_path,
                                       elem_cells_propname, 0, &err);

    return (err ? 1 : elem_cells) * 4;
}

static void fdt_init_qdev_array_prop(Object *obj,
                                     FDTMachineInfo *fdti,
                                     const char *node_path,
                                     const char *propname,
                                     const void *value,
                                     int len)
{
    int nr = len;
    uint32_t elem_len;
    QList *qlist = qlist_new();
    const char *prop_type;
    const void *prop_value = value;

    if (!value || !nr) {
        return;
    }

    elem_len = fdt_array_elem_len(fdti, node_path, propname);
    if (nr % elem_len) {
        return;
    }

    nr /= elem_len;

    prop_type = qdev_prop_get_array_elem_type(DEVICE(obj), propname);
    if (!prop_type) {
        fdt_debug_np("fail to get property array elem type\n");
        return;
    }

    while (nr--) {
        if (!strcmp(prop_type, "uint8") || !strcmp(prop_type, "uint16") ||
            !strcmp(prop_type, "uint32") || !strcmp(prop_type, "uint64") ||
            !strcmp(prop_type, "int8") || !strcmp(prop_type, "int16") ||
            !strcmp(prop_type, "int32") || !strcmp(prop_type, "int64")) {
                qlist_append_int(qlist, get_int_be(prop_value, elem_len));
        } else if (!strcmp(prop_type, "boolean") ||
                                            !strcmp(prop_type, "bool")) {
            qlist_append_bool(qlist, !!get_int_be(prop_value, elem_len));
        } else if (!strcmp(prop_type, "string") || !strcmp(prop_type, "str")) {
            qlist_append_str(qlist, (const char *)prop_value);
        }

        prop_value += elem_len;

        /* TBD: add link type support */
    }

    qdev_prop_set_array(DEVICE(obj), propname, qlist);
    fdt_debug_np("set property %s propname to <list>\n", propname);
}

static void fdt_init_qdev_properties(char *node_path, FDTMachineInfo *fdti,
                                     Object *dev)
{
    int node_offset, prop_offset;

    node_offset = fdt_path_offset(fdti->fdt, node_path);
    if (node_offset < 0) {
        return;
    }

    fdt_for_each_property_offset(prop_offset, fdti->fdt, node_offset) {
        const char *fdt_propname;
        const void *value;
        const char *propname;
        int len;
        ObjectProperty *p = NULL;

        value = fdt_getprop_by_offset(fdti->fdt, prop_offset,
                               &fdt_propname, &len);
        if (!value) {
            continue;
        }

        propname = trim_vendor(fdt_propname);

        p = object_property_find(dev, propname);
        if (p) {
            fdt_debug_np("matched property: %s of type %s, len %d\n",
                                            propname, p->type, len);
        }
        if (!p) {
            continue;
        }

        if (!strcmp(p->type, "list")) {
            fdt_init_qdev_array_prop(dev, fdti, node_path,
                                     propname, value, len);
        }

        if (!strcmp(propname, "type")) {
            continue;
        }

        fdt_init_qdev_scalar_prop(dev, p, fdti, node_path,
                                  propname, value, len);
    }
}

static void fdt_init_parent_node(Object *dev, Object *parent, char *node_path)
{
    if (dev->parent) {
        fdt_debug_np("Node already parented - skipping node\n");
    } else if (parent) {
        fdt_debug_np("parenting node\n");
        object_property_add_child(OBJECT(parent),
                              strdup(strrchr(node_path, '/') + 1),
                              OBJECT(dev));
        if (object_dynamic_cast(dev, TYPE_DEVICE)) {
            Object *parent_bus = parent;
            unsigned int depth = 0;

            fdt_debug_np("bus parenting node\n");
            /* Look for an FDT ancestor that is a Bus.  */
            while (parent_bus && !object_dynamic_cast(parent_bus, TYPE_BUS)) {
                /*
                 * Assert against insanely deep hierarchies which are an
                 * indication of loops.
                 */
                assert(depth < 4096);

                parent_bus = parent_bus->parent;
                depth++;
            }

            if (!parent_bus
                && object_dynamic_cast(OBJECT(dev), TYPE_SYS_BUS_DEVICE)) {
                /*
                 * Didn't find any bus. Use the default sysbus one.
                 * This allows ad-hoc busses belonging to sysbus devices to be
                 * visible to -device bus=x.
                 */
                parent_bus = OBJECT(sysbus_get_default());
            }

            if (parent_bus) {
                qdev_set_parent_bus(DEVICE(dev), BUS(parent_bus),
                                    &error_abort);
            }
        }
    } else {
        fdt_debug_np("orphaning node\n");
        if (object_dynamic_cast(OBJECT(dev), TYPE_SYS_BUS_DEVICE)) {
            qdev_set_parent_bus(DEVICE(dev), BUS(sysbus_get_default()),
                                &error_abort);
        }

        /* FIXME: Make this go away (centrally) */
        object_property_add_child(
                              object_get_root(),
                              strrchr(node_path, '/') + 1,
                              OBJECT(dev));
    }
}

static void fdt_init_device_realize(FDTMachineInfo *fdti,  char *node_path,
                                   Object *dev)
{
    if (object_dynamic_cast(dev, TYPE_DEVICE)) {
        DeviceClass *dc = DEVICE_GET_CLASS(dev);
        const char *short_name = strrchr(node_path, '/') + 1;

        /* Regular TYPE_DEVICE houskeeping */
        fdt_debug_np("Short naming node: %s\n", short_name);
        (DEVICE(dev))->id = g_strdup(short_name);

        if (object_dynamic_cast(dev, TYPE_CPU_CLUSTER)) {
            /*
             * CPU clusters must be realized at the end to make sure all child
             * CPUs are parented.
             */
            fdt_init_register_user_cpu_cluster(fdti, OBJECT(dev));
        } else {
            object_property_set_bool(OBJECT(dev), "realized", true,
                                     &error_fatal);
            if (dc->legacy_reset) {
                qemu_register_reset((void (*)(void *))dc->legacy_reset,
                                    dev);
            }
        }
    }
}

static int fdt_init_qdev(char *node_path, FDTMachineInfo *fdti, char *compat)
{
    Object *dev, *parent;
    char *parent_node_path;

    if (!compat) {
        return 1;
    }
    dev = fdt_create_from_compat(compat);
    if (!dev) {
        fdt_debug_np("no match found for %s\n", compat);
        return 1;
    }
    fdt_debug_np("matched compat %s\n", compat);

    /* Do this super early so fdt_generic_num_cpus is correct ASAP */
    if (object_dynamic_cast(dev, TYPE_CPU)) {
        fdt_generic_num_cpus++;
        fdt_debug_np("is a CPU - total so far %d\n", fdt_generic_num_cpus);
    }

    parent_node_path = qemu_devtree_getparent(fdti->fdt, node_path);
    if (!parent_node_path) {
        abort();
    }
    while (!fdt_init_has_opaque(fdti, parent_node_path) &&
           !object_dynamic_cast(dev, TYPE_CPU)) {
        fdt_init_yield(fdti);
    }

    parent = fdt_init_get_opaque(fdti, parent_node_path);

    if (object_dynamic_cast(dev, TYPE_CPU)) {
        parent = fdt_init_get_cpu_cluster(fdti, parent, compat);
    }

    fdt_init_parent_node(dev, parent, node_path);

    fdt_init_set_opaque(fdti, node_path, dev);

    fdt_init_qdev_properties(node_path, fdti, dev);

    fdt_init_device_realize(fdti, node_path, dev);

    g_free(parent_node_path);

    return 0;
}

static const TypeInfo fdt_generic_mmap_info = {
    .name          = TYPE_FDT_GENERIC_MMAP,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(FDTGenericMMapClass),
};

static void fdt_generic_register_types(void)
{
    type_register_static(&fdt_generic_mmap_info);
}
type_init(fdt_generic_register_types)
