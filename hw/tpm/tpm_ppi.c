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
#include "tpm_ppi.h"

bool tpm_ppi_init(TPMPPI *tpmppi, struct MemoryRegion *m,
                  hwaddr addr, Object *obj, Error **errp)
{
    Error *err = NULL;

    memory_region_init_ram(&tpmppi->ram, obj, "tpm-ppi",
                           TPM_PPI_ADDR_SIZE, &err);
    if (err) {
        error_propagate(errp, err);
        return false;
    }

    memory_region_add_subregion(m, addr, &tpmppi->ram);
    return true;
}
