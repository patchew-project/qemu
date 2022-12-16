/*
 * QEMU Xen emulation: Shared/overlay pages support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

void xen_overlay_create(void);
int xen_overlay_map_page(uint32_t space, uint64_t idx, uint64_t gpa);
void *xen_overlay_page_ptr(uint32_t space, uint64_t idx);

int xen_sync_long_mode(void);
int xen_set_long_mode(bool long_mode);
bool xen_is_long_mode(void);
