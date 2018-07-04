/*
 * tpm_ppi.c - TPM Physical Presence Interface
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "cpu.h"
#include "sysemu/memory_mapping.h"
#include "sysemu/reset.h"
#include "migration/vmstate.h"
#include "tpm_ppi.h"
#include "trace.h"

static void tpm_ppi_reset(void *opaque)
{
    TPMPPI *tpmppi = opaque;
    char *ptr = memory_region_get_ram_ptr(&tpmppi->ram);

    if (ptr[0x200] & 0x1) {
        GuestPhysBlockList guest_phys_blocks;
        GuestPhysBlock *block;

        guest_phys_blocks_init(&guest_phys_blocks);
        guest_phys_blocks_append(&guest_phys_blocks);
        QTAILQ_FOREACH(block, &guest_phys_blocks.head, next) {
            trace_tpm_ppi_memset(block->host_addr,
                             block->target_end - block->target_start);
            memset(block->host_addr, 0,
                   block->target_end - block->target_start);
        }
        guest_phys_blocks_free(&guest_phys_blocks);
    }
}

bool tpm_ppi_init(TPMPPI *tpmppi, struct MemoryRegion *m,
                  hwaddr addr, Object *obj, Error **errp)
{
    memory_region_init_ram_device_ptr(&tpmppi->ram, obj, "tpm-ppi",
                                      TPM_PPI_ADDR_SIZE, tpmppi->buf);
    vmstate_register_ram(&tpmppi->ram, DEVICE(obj));

    memory_region_add_subregion(m, addr, &tpmppi->ram);
    qemu_register_reset(tpm_ppi_reset, tpmppi);

    return true;
}
