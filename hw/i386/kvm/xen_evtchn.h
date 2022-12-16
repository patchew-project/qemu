/*
 * QEMU Xen emulation: Event channel support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */


void xen_evtchn_create(void);
int xen_evtchn_set_callback_param(uint64_t param);

struct evtchn_status;
int xen_evtchn_status_op(struct evtchn_status *status);
