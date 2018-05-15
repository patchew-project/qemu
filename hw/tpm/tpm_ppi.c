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

#include "tpm_ppi.h"
#include "trace.h"

static uint64_t tpm_ppi_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    TPMPPI *s = opaque;

    trace_tpm_ppi_mmio_read(addr, size, s->ram[addr]);

    return s->ram[addr];
}

static void tpm_ppi_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    TPMPPI *s = opaque;

    trace_tpm_ppi_mmio_write(addr, size, val);

    s->ram[addr] = val;
}

static const MemoryRegionOps tpm_ppi_memory_ops = {
    .read = tpm_ppi_mmio_read,
    .write = tpm_ppi_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

void tpm_ppi_init_io(TPMPPI *tpmppi, struct MemoryRegion *m, Object *obj)
{
    memory_region_init_io(&tpmppi->mmio, obj, &tpm_ppi_memory_ops,
                          tpmppi, "tpm-ppi-mmio",
                          TPM_PPI_ADDR_SIZE);

    memory_region_add_subregion(m, TPM_PPI_ADDR_BASE, &tpmppi->mmio);
}
