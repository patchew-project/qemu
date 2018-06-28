/*
 * TPM Physical Presence Interface
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TPM_TPM_PPI_H
#define TPM_TPM_PPI_H

#include "hw/acpi/tpm.h"
#include "exec/address-spaces.h"

typedef struct TPMPPI {
    MemoryRegion ram;
} TPMPPI;

bool tpm_ppi_init(TPMPPI *tpmppi, struct MemoryRegion *m,
                  hwaddr addr, Object *obj, Error **errp);

#endif /* TPM_TPM_PPI_H */
