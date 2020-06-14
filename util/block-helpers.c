/*
 * Block utility functions
 *
 * Copyright (c) 2020 Coiby Xu <coiby.xu@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "block-helpers.h"

/*
 * Logical block size input validation
 *
 * The size should meet the following conditions:
 * 1. min=512
 * 2. max=32768
 * 3. a power of 2
 *
 *  Moved from hw/core/qdev-properties.c:set_blocksize()
 */
void check_logical_block_size(const char *id, const char *name, uint16_t value,
                     Error **errp)
{
    const int64_t min = 512;
    const int64_t max = 32768;

    /* value of 0 means "unset" */
    if (value && (value < min || value > max)) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE,
                   id, name, (int64_t)value, min, max);
        return;
    }

    /* We rely on power-of-2 blocksizes for bitmasks */
    if ((value & (value - 1)) != 0) {
        error_setg(errp,
                   "Property %s.%s doesn't take value '%" PRId64
                   "', it's not a power of 2",
                   id, name, (int64_t)value);
        return;
    }
}
