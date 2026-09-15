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
    case RP_CMD_hello:
        assert(pkt->hdr.len >= sizeof pkt->hello.version);
        pkt->hello.version.major = be16_to_cpu(pkt->hello.version.major);
        pkt->hello.version.minor = be16_to_cpu(pkt->hello.version.minor);
        used += sizeof pkt->hello.version;

        if ((pkt->hdr.len - used) >= sizeof pkt->hello.caps) {
            void *offset;
            int i;

            pkt->hello.caps.offset = be32_to_cpu(pkt->hello.caps.offset);
            pkt->hello.caps.len = be16_to_cpu(pkt->hello.caps.len);

            offset = (char *)pkt + pkt->hello.caps.offset;
            for (i = 0; i < pkt->hello.caps.len; i++) {
                uint32_t cap;

                /*
                 * We don't know if offset is 32bit aligned so use
                 * memcpy to do the endian conversion.
                 */
                memcpy(&cap, offset + i * sizeof cap, sizeof cap);
                cap = be32_to_cpu(cap);
                memcpy(offset + i * sizeof cap, &cap, sizeof cap);
            }
            used += sizeof pkt->hello.caps;
        } else {
            pkt->hello.caps.offset = 0;
            pkt->hello.caps.len = 0;
        }

        /*
         * Consume everything ignoring additional headers we do not yet
         * know about.
         */
        used = pkt->hdr.len;
        break;
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

size_t rp_encode_hello_caps(uint32_t id, uint32_t dev, struct rp_pkt_hello *pkt,
                            uint16_t version_major, uint16_t version_minor,
                            uint32_t *caps, uint32_t *caps_out,
                            uint32_t caps_len)
{
    size_t psize = sizeof *pkt + sizeof caps[0] * caps_len;
    unsigned int i;

    rp_encode_hdr(&pkt->hdr, RP_CMD_hello, id, dev,
                  psize - sizeof pkt->hdr, 0);
    pkt->version.major = cpu_to_be16(version_major);
    pkt->version.minor = cpu_to_be16(version_minor);

    /* Feature list is appeneded right after the hello packet.  */
    pkt->caps.offset = cpu_to_be32(sizeof *pkt);
    pkt->caps.len = cpu_to_be16(caps_len);

    for (i = 0; i < caps_len; i++) {
        uint32_t cap;

        cap = caps[i];
        caps_out[i] = cpu_to_be32(cap);
    }
    return sizeof *pkt;
}

void rp_process_caps(struct rp_peer_state *peer,
                     void *caps, size_t caps_len)
{
    int i;

    assert(peer->caps.busaccess_ext_base == false);

    for (i = 0; i < caps_len; i++) {
        uint32_t cap;

        memcpy(&cap, caps + i * sizeof cap, sizeof cap);

        switch (cap) {
        case CAP_BUSACCESS_EXT_BASE:
            peer->caps.busaccess_ext_base = true;
            break;
        case CAP_BUSACCESS_EXT_BYTE_EN:
            peer->caps.busaccess_ext_byte_en = true;
            break;
        case CAP_WIRE_POSTED_UPDATES:
            peer->caps.wire_posted_updates = true;
            break;
        case CAP_ATS:
            peer->caps.ats = true;
            break;
        }
    }
}

void rp_dpkt_alloc(RemotePortDynPkt *dpkt, size_t size)
{
    if (dpkt->size < size) {
        char *u8;
        dpkt->pkt = realloc(dpkt->pkt, size);
        u8 = (void *) dpkt->pkt;
        memset(u8 + dpkt->size, 0, size - dpkt->size);
        dpkt->size = size;
    }
}

void rp_dpkt_swap(RemotePortDynPkt *a, RemotePortDynPkt *b)
{
    struct rp_pkt *tmp_pkt;
    size_t tmp_size;

    tmp_pkt = a->pkt;
    tmp_size = a->size;
    a->pkt = b->pkt;
    a->size = b->size;
    b->pkt = tmp_pkt;
    b->size = tmp_size;
}

bool rp_dpkt_is_valid(RemotePortDynPkt *dpkt)
{
    return dpkt->size > 0 && dpkt->pkt->hdr.len;
}

void rp_dpkt_invalidate(RemotePortDynPkt *dpkt)
{
    assert(rp_dpkt_is_valid(dpkt));
    dpkt->pkt->hdr.len = 0;
}

inline void rp_dpkt_free(RemotePortDynPkt *dpkt)
{
    dpkt->size = 0;
    free(dpkt->pkt);
}
