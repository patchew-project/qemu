/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors:
 *  Magnus Kulke      <magnuskulke@microsoft.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "system/mshv.h"

void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add)
{
	error_report("unimplemented");
	abort();
}

