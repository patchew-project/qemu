/*
 * Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "hw/xen/xen-bus.h"
#include "hw/xen/xen-bus-helper.h"
#include "qapi/error.h"

struct xs_state {
    enum xenbus_state statenum;
    const char *statestr;
};
#define XS_STATE(state) { state, #state }

static struct xs_state xs_state[] = {
    XS_STATE(XenbusStateUnknown),
    XS_STATE(XenbusStateInitialising),
    XS_STATE(XenbusStateInitWait),
    XS_STATE(XenbusStateInitialised),
    XS_STATE(XenbusStateConnected),
    XS_STATE(XenbusStateClosing),
    XS_STATE(XenbusStateClosed),
    XS_STATE(XenbusStateReconfiguring),
    XS_STATE(XenbusStateReconfigured),
};

#undef XS_STATE

const char *xs_strstate(enum xenbus_state state)
{
    unsigned int i;

   for (i = 0; i < ARRAY_SIZE(xs_state); i++) {
        if (xs_state[i].statenum == state) {
            return xs_state[i].statestr;
        }
    }

    return "INVALID";
}

void xs_node_create(struct xs_handle *xsh, const char *node,
                    struct xs_permissions perms[],
                    unsigned int nr_perms, Error **errp)
{
    if (!xs_write(xsh, XBT_NULL, node, "", 0)) {
        error_setg_errno(errp, errno, "failed to create node '%s'", node);
        return;
    }

    if (!xs_set_permissions(xsh, XBT_NULL, node,
                            perms, nr_perms)) {
        error_setg_errno(errp, errno, "failed to set node '%s' permissions",
                         node);
    }
}

void xs_node_destroy(struct xs_handle *xsh, const char *node)
{
    xs_rm(xsh, XBT_NULL, node);
}

void xs_node_vprintf(struct xs_handle *xsh, const char *node,
                     const char *key, const char *fmt, va_list ap)
{
    char *path, *value;

    path = (strlen(node) != 0) ? g_strdup_printf("%s/%s", node, key) :
        g_strdup(key);

    value = g_strdup_vprintf(fmt, ap);

    xs_write(xsh, XBT_NULL, path, value, strlen(value));

    g_free(value);
    g_free(path);
}

void xs_node_printf(struct xs_handle *xsh, const char *node,
                    const char *key, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    xs_node_vprintf(xsh, node, key, fmt, ap);
    va_end(ap);
}

int xs_node_vscanf(struct xs_handle *xsh, const char *node, const char *key,
                   const char *fmt, va_list ap)
{
    char *path, *value;
    int rc;

    path = (strlen(node) != 0) ? g_strdup_printf("%s/%s", node, key) :
        g_strdup(key);

    value = xs_read(xsh, XBT_NULL, path, NULL);

    rc = value ? vsscanf(value, fmt, ap) : EOF;

    free(value);
    g_free(path);

    return rc;
}

int xs_node_scanf(struct xs_handle *xsh, const char *node, const char *key,
                  const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = xs_node_vscanf(xsh, node, key, fmt, ap);
    va_end(ap);

    return rc;
}
