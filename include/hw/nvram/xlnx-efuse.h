/*
 * QEMU model of the Xilinx eFuse core
 *
 * Copyright (c) 2015 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgari@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef XLNX_EFUSE_H
#define XLNX_EFUSE_H

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "hw/qdev-core.h"

#define TYPE_XLNX_EFUSE "xlnx,efuse"

typedef struct XLNXEFuseLkSpec {
    uint16_t row;
    uint16_t lk_bit;
} XLNXEFuseLkSpec;

typedef struct XLNXEFuse {
    DeviceState parent_obj;
    BlockBackend *blk;
    bool blk_ro;
    uint32_t *fuse32;

    DeviceState *dev;

    bool init_tbits;
    int drv_index;

    uint8_t efuse_nr;
    uint32_t efuse_size;

    uint32_t *ro_bits;
    uint32_t ro_bits_cnt;
} XLNXEFuse;

uint32_t xlnx_efuse_calc_crc(const uint32_t *data, unsigned u32_cnt,
                             unsigned zpads);

bool xlnx_efuse_get_bit(XLNXEFuse *s, unsigned int bit);
bool xlnx_efuse_set_bit(XLNXEFuse *s, unsigned int bit);
bool xlnx_efuse_k256_check(XLNXEFuse *s, uint32_t crc, unsigned start);
uint32_t xlnx_efuse_tbits_check(XLNXEFuse *s);

/* Return whole row containing the given bit address */
static inline uint32_t xlnx_efuse_get_row(XLNXEFuse *s, unsigned int bit)
{
    if (!(s->fuse32)) {
        return 0;
    } else {
        unsigned int row_idx = bit / 32;

        assert(row_idx < (s->efuse_size * s->efuse_nr / 32));
        return s->fuse32[row_idx];
    }
}

#endif
