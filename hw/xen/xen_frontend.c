/*
 * Xen frontend driver infrastructure
 *
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 *  Copyright (c) 2015 Intel Corporation
 *  Authors:
 *    Quan Xu <quan.xu@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"

#include "hw/xen/xen_pvdev.h"
#include "hw/xen/xen_frontend.h"
#include "hw/xen/xen_backend.h"

static int debug = 0;

char *xenstore_read_fe_str(struct XenDevice *xendev, const char *node)
{
    return xenstore_read_str(xendev->fe, node);
}

int xenstore_read_fe_int(struct XenDevice *xendev, const char *node, int *ival)
{
    return xenstore_read_int(xendev->fe, node, ival);
}

int xenstore_read_fe_uint64(struct XenDevice *xendev, const char *node, uint64_t *uval)
{
    return xenstore_read_uint64(xendev->fe, node, uval);
}

void xen_fe_frontend_changed(struct XenDevice *xendev, const char *node)
{
    int fe_state;

    if (node == NULL  ||  strcmp(node, "state") == 0) {
        if (xenstore_read_fe_int(xendev, "state", &fe_state) == -1) {
            fe_state = XenbusStateUnknown;
        }
        if (xendev->fe_state != fe_state) {
            xen_pv_printf(xendev, 1, "frontend state: %s -> %s\n",
                          xenbus_strstate(xendev->fe_state),
                          xenbus_strstate(fe_state));
        }
        xendev->fe_state = fe_state;
    }
    if (node == NULL  ||  strcmp(node, "protocol") == 0) {
        g_free(xendev->protocol);
        xendev->protocol = xenstore_read_fe_str(xendev, "protocol");
        if (xendev->protocol) {
            xen_pv_printf(xendev, 1, "frontend protocol: %s\n", xendev->protocol);
        }
    }

    if (node) {
        xen_pv_printf(xendev, 2, "frontend update: %s\n", node);
        if (xendev->ops->frontend_changed) {
            xendev->ops->frontend_changed(xendev, node);
        }
    }
}

void xenstore_update_fe(char *watch, struct XenDevice *xendev)
{
    char *node;
    unsigned int len;

    len = strlen(xendev->fe);
    if (strncmp(xendev->fe, watch, len) != 0) {
        return;
    }
    if (watch[len] != '/') {
        return;
    }
    node = watch + len + 1;

    xen_fe_frontend_changed(xendev, node);
    xen_be_check_state(xendev);
}

struct XenDevice *xen_fe_get_xendev(const char *type, int dom, int dev,
                                    char *backend, struct XenDevOps *ops)
{
    struct XenDevice *xendev;

    xendev = xen_pv_find_xendev(type, dom, dev);
    if (xendev) {
        return xendev;
    }

    /* init new xendev */
    xendev = g_malloc0(ops->size);
    xendev->type  = type;
    xendev->dom   = dom;
    xendev->dev   = dev;
    xendev->ops   = ops;

    /*return if the ops->flags is not DEVOPS_FLAG_FE*/
    if (!(ops->flags & DEVOPS_FLAG_FE)) {
        return NULL;
    }

    snprintf(xendev->be, sizeof(xendev->be), "%s", backend);
    snprintf(xendev->name, sizeof(xendev->name), "%s-%d",
             xendev->type, xendev->dev);

    xendev->debug = debug;
    xendev->local_port = -1;

    xendev->evtchndev = xenevtchn_open(NULL, 0);
    if (xendev->evtchndev == NULL) {
        xen_pv_printf(NULL, 0, "can't open evtchn device\n");
        g_free(xendev);
        return NULL;
    }
    fcntl(xenevtchn_fd(xendev->evtchndev), F_SETFD, FD_CLOEXEC);

    if (ops->flags & DEVOPS_FLAG_NEED_GNTDEV) {
        xendev->gnttabdev = xengnttab_open(NULL, 0);
        if (xendev->gnttabdev == NULL) {
            xen_pv_printf(NULL, 0, "can't open gnttab device\n");
            xenevtchn_close(xendev->evtchndev);
            g_free(xendev);
            return NULL;
        }
    } else {
        xendev->gnttabdev = NULL;
    }

    xen_pv_insert_xendev(xendev);

    if (xendev->ops->alloc) {
        xendev->ops->alloc(xendev);
    }

    return xendev;
}

int xen_fe_alloc_unbound(struct XenDevice *xendev, int dom, int remote_dom){
    xendev->local_port = xenevtchn_bind_unbound_port(xendev->evtchndev,
                                                     remote_dom);
    if (xendev->local_port == -1) {
        xen_pv_printf(xendev, 0, "xenevtchn_bind_unbound_port failed\n");
        return -1;
    }
    xen_pv_printf(xendev, 2, "bind evtchn port %d\n", xendev->local_port);
    qemu_set_fd_handler(xenevtchn_fd(xendev->evtchndev),
                        xen_pv_evtchn_event, NULL, xendev);
    return 0;
}

/*
 * Make sure, initialize the 'xendev->fe' in xendev->ops->init() or
 * xendev->ops->initialize()
 */
int xenbus_switch_state(struct XenDevice *xendev, enum xenbus_state xbus)
{
    xs_transaction_t xbt = XBT_NULL;

    if (xendev->fe_state == xbus) {
        return 0;
    }

    xendev->fe_state = xbus;
    if (xendev->fe == NULL) {
        xen_pv_printf(NULL, 0, "xendev->fe is NULL\n");
        return -1;
    }

retry_transaction:
    xbt = xs_transaction_start(xenstore);
    if (xbt == XBT_NULL) {
        goto abort_transaction;
    }

    if (xenstore_write_int(xendev->fe, "state", xbus)) {
        goto abort_transaction;
    }

    if (!xs_transaction_end(xenstore, xbt, 0)) {
        if (errno == EAGAIN) {
            goto retry_transaction;
        }
    }

    return 0;

abort_transaction:
    xs_transaction_end(xenstore, xbt, 1);
    return -1;
}

/*
 * Simplify QEMU side, a thread is running in Xen backend, which will
 * connect frontend when the frontend is initialised. Call these initialised
 * functions.
 */
static int xen_fe_try_init(void *opaque)
{
    struct XenDevOps *ops = opaque;
    int rc = -1;

    if (ops->init) {
        rc = ops->init(NULL);
    }

    return rc;
}

static int xen_fe_try_initialise(struct XenDevice *xendev)
{
    int rc = 0, fe_state;

    if (xenstore_read_fe_int(xendev, "state", &fe_state) == -1) {
        fe_state = XenbusStateUnknown;
    }
    xendev->fe_state = fe_state;

    if (xendev->ops->initialise) {
        rc = xendev->ops->initialise(xendev);
    }
    if (rc != 0) {
        xen_pv_printf(xendev, 0, "initialise() failed\n");
        return rc;
    }

    xenbus_switch_state(xendev, XenbusStateInitialised);
    return 0;
}

static void xen_fe_try_connected(struct XenDevice *xendev)
{
    if (!xendev->ops->connected) {
        return;
    }

    if (xendev->fe_state != XenbusStateConnected) {
        if (xendev->ops->flags & DEVOPS_FLAG_IGNORE_STATE) {
            xen_pv_printf(xendev, 2, "frontend not ready, ignoring\n");
        } else {
            xen_pv_printf(xendev, 2, "frontend not ready (yet)\n");
            return;
        }
    }

    xendev->ops->connected(xendev);
}

static int xen_fe_check(struct XenDevice *xendev, uint32_t domid,
                        int handle)
{
    int rc = 0;

    rc = xen_fe_try_initialise(xendev);
    if (rc != 0) {
        xen_pv_printf(xendev, 0, "xendev %s initialise error\n",
                      xendev->name);
        goto err;
    }
    xen_fe_try_connected(xendev);

    return rc;

err:
    xen_pv_del_xendev(domid, handle);
    return -1;
}

static char *xenstore_fe_get_backend(const char *type, int be_domid,
                                     uint32_t domid, int *hdl)
{
    char *name, *str, *ret = NULL;
    uint32_t i, cdev;
    int handle = 0;
    char path[XEN_BUFSIZE];
    char **dev = NULL;

    name = xenstore_get_domain_name(domid);
    snprintf(path, sizeof(path), "frontend/%s/%d", type, be_domid);
    dev = xs_directory(xenstore, 0, path, &cdev);
    for (i = 0; i < cdev; i++) {
        handle = i;
        snprintf(path, sizeof(path), "frontend/%s/%d/%d",
        type, be_domid, handle);
        str = xenstore_read_str(path, "domain");
        if (!strcmp(name, str)) {
            break;
        }

        free(str);

        /* Not the backend domain */
        if (handle == (cdev - 1)) {
            goto err;
        }
    }

    snprintf(path, sizeof(path), "frontend/%s/%d/%d",
    type, be_domid, handle);
    str = xenstore_read_str(path, "backend");
    if (str != NULL) {
        ret = g_strdup(str);
        free(str);
    }

    *hdl = handle;
    free(dev);

    return ret;
err:
    *hdl = -1;
    free(dev);
    return NULL;
}

static int xenstore_fe_scan(const char *type, uint32_t domid,
                            struct XenDevOps *ops)
{
    struct XenDevice *xendev;
    char path[XEN_BUFSIZE], token[XEN_BUFSIZE];
    unsigned int cdev, j;
    char *backend;
    char **dev = NULL;
    int rc;
    int xenstore_dev;

    /* ops .init check, xendev is NOT initialized */
    rc = xen_fe_try_init(ops);
    if (rc != 0) {
        return -1;
    }

    /* Get /local/domain/0/${type}/{} directory */
    snprintf(path, sizeof(path), "frontend/%s", type);
    dev = xs_directory(xenstore, 0, path, &cdev);
    if (dev == NULL) {
        return 0;
    }

    for (j = 0; j < cdev; j++) {

        /* Get backend via domain name */
        backend = xenstore_fe_get_backend(type, atoi(dev[j]),
                                          domid, &xenstore_dev);
        if (backend == NULL) {
            continue;
        }

        xendev = xen_fe_get_xendev(type, domid, xenstore_dev, backend, ops);
        free(backend);
        if (xendev == NULL) {
            xen_pv_printf(xendev, 0, "xendev is NULL.\n");
            continue;
        }

        /*
         * Simplify QEMU side, a thread is running in Xen backend, which will
         * connect frontend when the frontend is initialised.
         */
        if (xen_fe_check(xendev, domid, xenstore_dev) < 0) {
            xen_pv_printf(xendev, 0, "xendev fe_check error.\n");
            continue;
        }

        /* Setup watch */
        snprintf(token, sizeof(token), "be:%p:%d:%p",
                 type, domid, xendev->ops);
        if (!xs_watch(xenstore, xendev->be, token)) {
            xen_pv_printf(xendev, 0, "xs_watch failed.\n");
            continue;
        }
    }

    free(dev);
    return 0;
}

int xen_fe_register(const char *type, struct XenDevOps *ops)
{
    return xenstore_fe_scan(type, xen_domid, ops);
}
