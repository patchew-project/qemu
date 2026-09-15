/*
 * SPDX-License-Identifier: MIT
 *
 * Remote-port protocol
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/core/remote-port-proto.h"

static const char *rp_cmd_names[RP_CMD_max + 1] = {
    [RP_CMD_nop] = "nop",
    [RP_CMD_hello] = "hello",
    [RP_CMD_cfg] = "cfg",
    [RP_CMD_read] = "read",
    [RP_CMD_write] = "write",
    [RP_CMD_interrupt] = "interrupt",
    [RP_CMD_sync] = "sync",
    [RP_CMD_ats_req] = "ats_request",
    [RP_CMD_ats_inv] = "ats_invalidation",
};

const char *rp_cmd_to_string(enum rp_cmd cmd)
{
    assert(cmd <= RP_CMD_max);
    return rp_cmd_names[cmd];
}

int rp_decode_hdr(struct rp_pkt *pkt)
{
    int used = 0;

    pkt->hdr.cmd = be32_to_cpu(pkt->hdr.cmd);
    pkt->hdr.len = be32_to_cpu(pkt->hdr.len);
    pkt->hdr.id = be32_to_cpu(pkt->hdr.id);
    pkt->hdr.flags = be32_to_cpu(pkt->hdr.flags);
    pkt->hdr.dev = be32_to_cpu(pkt->hdr.dev);
    used += sizeof pkt->hdr;
    return used;
}

int rp_decode_payload(struct rp_pkt *pkt)
{
    int used = 0;

    switch (pkt->hdr.cmd) {
    /* TBD */
    default:
        break;
    }
    return used;
}

void rp_encode_hdr(struct rp_pkt_hdr *hdr, uint32_t cmd, uint32_t id,
                   uint32_t dev, uint32_t len, uint32_t flags)
{
    hdr->cmd = cpu_to_be32(cmd);
    hdr->len = cpu_to_be32(len);
    hdr->id = cpu_to_be32(id);
    hdr->dev = cpu_to_be32(dev);
    hdr->flags = cpu_to_be32(flags);
}
