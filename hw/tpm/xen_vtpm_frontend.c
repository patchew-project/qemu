/*
 * Connect to Xen vTPM stubdom domain
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

#include "xen_vtpm_frontend.h"
#include "hw/xen/xen_frontend.h"
#include "hw/xen/xen_backend.h"

int xenstore_vtpm_dev;
#ifndef XS_STUBDOM_VTPM_ENABLE
#define XS_STUBDOM_VTPM_ENABLE    "1"
#endif

#ifndef VTPM_PAGE_SIZE
#define VTPM_PAGE_SIZE      4096
#endif

enum tpmif_state {
    /* No contents, vTPM idle, cancel complete */
    TPMIF_STATE_IDLE,
    /* Request ready or vTPM working */
    TPMIF_STATE_SUBMIT,
    /* Response ready or vTPM idle */
    TPMIF_STATE_FINISH,
    /* Cancel requested or vTPM working */
    TPMIF_STATE_CANCEL,
};

static AioContext *vtpm_aio_ctx;

enum status_bits {
    VTPM_STATUS_RUNNING  = 0x1,
    VTPM_STATUS_IDLE     = 0x2,
    VTPM_STATUS_RESULT   = 0x4,
    VTPM_STATUS_CANCELED = 0x8,
};

struct tpmif_shared_page {
    /* Request and response length in bytes */
    uint32_t length;
    /* Enum tpmif_state */
    uint8_t  state;
    /* For the current request */
    uint8_t  locality;
    /* Should be zero */
    uint8_t  pad;
    /* Extra pages for long packets; may be zero */
    uint8_t  nr_extra_pages;
    /*
     * Grant IDs, the length is actually nr_extra_pages.
     * beyond the extra_pages entries is the actual request
     * and response.
     */
    uint32_t extra_pages[0];
};

struct xen_vtpm_dev {
    struct XenDevice xendev;  /* must be first */
    struct           tpmif_shared_page *shr;
    xengntshr_handle *xen_xcs;
    int              ring_ref;
    int              bedomid;
    QEMUBH           *sr_bh;
};

static uint8_t vtpm_status(struct xen_vtpm_dev *vtpmdev)
{
    switch (vtpmdev->shr->state) {
    case TPMIF_STATE_IDLE:
    case TPMIF_STATE_FINISH:
        return VTPM_STATUS_IDLE;
    case TPMIF_STATE_SUBMIT:
    case TPMIF_STATE_CANCEL:
        return VTPM_STATUS_RUNNING;
    default:
        return 0;
    }
}

static bool vtpm_aio_wait(AioContext *ctx)
{
    return aio_poll(ctx, true);
}

static void sr_bh_handler(void *opaque)
{
}

int vtpm_recv(struct XenDevice *xendev, uint8_t* buf, size_t *count)
{
    struct xen_vtpm_dev *vtpmdev = container_of(xendev, struct xen_vtpm_dev,
                                                xendev);
    struct tpmif_shared_page *shr = vtpmdev->shr;
    unsigned int offset;
    size_t length = shr->length;

    if (shr->state == TPMIF_STATE_IDLE) {
        return -ECANCELED;
    }

    offset = sizeof(*shr) + sizeof(shr->extra_pages[0])*shr->nr_extra_pages;
    if (offset > VTPM_PAGE_SIZE) {
        return -EIO;
    }

    if (offset + length > VTPM_PAGE_SIZE) {
        length = VTPM_PAGE_SIZE - offset;
    }

    memcpy(buf, offset + (uint8_t *)shr, length);
    *count = length;

    return 0;
}

int vtpm_send(struct XenDevice *xendev, uint8_t* buf, size_t count)
{
    struct xen_vtpm_dev *vtpmdev = container_of(xendev, struct xen_vtpm_dev,
                                                xendev);
    struct tpmif_shared_page *shr = vtpmdev->shr;
    unsigned int offset = sizeof(*shr) +
                          sizeof(shr->extra_pages[0])*shr->nr_extra_pages;

    if (offset > VTPM_PAGE_SIZE) {
        return -EIO;
    }

    if (offset + count > VTPM_PAGE_SIZE) {
        return -ECANCELED;
    }

    while (vtpm_status(vtpmdev) != VTPM_STATUS_IDLE) {
        vtpm_aio_wait(vtpm_aio_ctx);
    }

    memcpy(offset + (uint8_t *)shr, buf, count);
    shr->length = count;
    barrier();
    shr->state = TPMIF_STATE_SUBMIT;
    xen_wmb();
    xen_pv_send_notify(&vtpmdev->xendev);

    while (vtpm_status(vtpmdev) != VTPM_STATUS_IDLE) {
        vtpm_aio_wait(vtpm_aio_ctx);
    }

    return count;
}

static int vtpm_initialise(struct XenDevice *xendev)
{
    struct xen_vtpm_dev *vtpmdev = container_of(xendev, struct xen_vtpm_dev,
                                                xendev);
    xs_transaction_t xbt = XBT_NULL;
    unsigned int ring_ref;

    vtpmdev->xendev.fe = xenstore_read_be_str(&vtpmdev->xendev, "frontend");
    if (vtpmdev->xendev.fe == NULL) {
        return -1;
    }

    /* Get backend domid */
    if (xenstore_read_fe_int(&vtpmdev->xendev, "backend-id",
                             &vtpmdev->bedomid)) {
        return -1;
    }

    /* Alloc shared page */
    vtpmdev->shr = xengntshr_share_pages(vtpmdev->xen_xcs, vtpmdev->bedomid, 1,
                                         &ring_ref, PROT_READ|PROT_WRITE);
    vtpmdev->ring_ref = ring_ref;
    if (vtpmdev->shr == NULL) {
        return -1;
    }

    /* Create event channel */
    if (xen_fe_alloc_unbound(&vtpmdev->xendev, 0, vtpmdev->bedomid)) {
        xengntshr_unshare(vtpmdev->xen_xcs, vtpmdev->shr, 1);
        return -1;
    }

    xenevtchn_unmask(vtpmdev->xendev.evtchndev,
                     vtpmdev->xendev.local_port);

again:
    xbt = xs_transaction_start(xenstore);
    if (xbt == XBT_NULL) {
        goto abort_transaction;
    }

    if (xenstore_write_int(vtpmdev->xendev.fe, "ring-ref",
                           vtpmdev->ring_ref)) {
        goto abort_transaction;
    }

    if (xenstore_write_int(vtpmdev->xendev.fe, "event-channel",
                           vtpmdev->xendev.local_port)) {
        goto abort_transaction;
    }

    /* Publish protocol v2 feature */
    if (xenstore_write_int(vtpmdev->xendev.fe, "feature-protocol-v2", 1)) {
        goto abort_transaction;
    }

    if (!xs_transaction_end(xenstore, xbt, 0)) {
        if (errno == EAGAIN) {
            goto again;
        }
    }

    return 0;

abort_transaction:
    xengntshr_unshare(vtpmdev->xen_xcs, vtpmdev->shr, 1);
    xs_transaction_end(xenstore, xbt, 1);
    return -1;
}

static int vtpm_free(struct XenDevice *xendev)
{
    struct xen_vtpm_dev *vtpmdev = container_of(xendev, struct xen_vtpm_dev,
                                                xendev);

    aio_poll(vtpm_aio_ctx, false);
    qemu_bh_delete(vtpmdev->sr_bh);
    if (vtpmdev->shr) {
        xengntshr_unshare(vtpmdev->xen_xcs, vtpmdev->shr, 1);
    }
    xengntshr_close(vtpmdev->xen_xcs);
    return 0;
}

static int vtpm_init(struct XenDevice *xendev)
{
    char path[XEN_BUFSIZE];
    char *value;
    unsigned int stubdom_vtpm = 0;

    snprintf(path, sizeof(path), "/local/domain/%d/platform/acpi_stubdom_vtpm",
             xen_domid);
    value = xs_read(xenstore, 0, path, &stubdom_vtpm);
    if (stubdom_vtpm <= 0 || strcmp(value, XS_STUBDOM_VTPM_ENABLE)) {
        free(value);
        return -1;
    }
    free(value);
    return 0;
}

static void vtpm_alloc(struct XenDevice *xendev)
{
    struct xen_vtpm_dev *vtpmdev = container_of(xendev, struct xen_vtpm_dev,
                                                xendev);

    vtpm_aio_ctx = aio_context_new(NULL);
    if (vtpm_aio_ctx == NULL) {
        return;
    }
    vtpmdev->sr_bh = aio_bh_new(vtpm_aio_ctx, sr_bh_handler, vtpmdev);
    qemu_bh_schedule(vtpmdev->sr_bh);
    vtpmdev->xen_xcs = xengntshr_open(0, 0);
    xenstore_vtpm_dev = xendev->dev;
}

static void vtpm_event(struct XenDevice *xendev)
{
    struct xen_vtpm_dev *vtpmdev = container_of(xendev, struct xen_vtpm_dev,
                                                xendev);

    qemu_bh_schedule(vtpmdev->sr_bh);
}

struct XenDevOps xen_vtpmdev_ops = {
    .size             = sizeof(struct xen_vtpm_dev),
    .flags            = DEVOPS_FLAG_IGNORE_STATE |
                        DEVOPS_FLAG_FE,
    .event            = vtpm_event,
    .free             = vtpm_free,
    .init             = vtpm_init,
    .alloc            = vtpm_alloc,
    .initialise       = vtpm_initialise,
    .backend_changed  = xen_fe_backend_changed,
};