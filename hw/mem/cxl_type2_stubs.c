/*
 * CXL Type-2 device stubs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/cxl/cxl.h"

MemTxResult cxl_type2_read(PCIDevice *d, hwaddr host_addr, uint64_t *data,
                           unsigned size, MemTxAttrs attrs)
{
    return MEMTX_ERROR;
}

MemTxResult cxl_type2_write(PCIDevice *d, hwaddr host_addr, uint64_t data,
                            unsigned size, MemTxAttrs attrs)
{
    return MEMTX_ERROR;
}
