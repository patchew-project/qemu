/*
 * Xor Based Zero Run Length Encoding
 *
 * Copyright 2013 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Orit Wasserman  <owasserm@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "xbzrle.h"

static int next_run(uint8_t *old_buf, uint8_t *new_buf, int off, int slen,
                    bool zrun)
{
    uint32_t len = 0;
    long res;

    res = (slen - off) % sizeof(long);

    /* first unaligned bytes */
    while (res) {
        uint8_t xor = old_buf[off + len] ^ new_buf[off + len];

        if (!(zrun ^ !!xor)) {
            break;
        }
        len++;
        res--;
    }

    if (res) {
        return len;
    }

    /* word at a time for speed, use of 32-bit long okay */
    while (off + len < slen) {
        /* truncation to 32-bit long okay */
        unsigned long mask = (unsigned long)0x0101010101010101ULL;
        long xor = (*(long *)(old_buf + off + len)) ^
                   (*(long *)(new_buf + off + len));

        if (zrun && !(zrun ^ !!xor)) {
            break;
        } else if (!zrun && ((xor - mask) & ~xor & (mask << 7))) {
            break;
        }

        len += sizeof(long);
    }

    /* go over the rest */
    while (off + len < slen) {
        uint8_t xor = old_buf[off + len] ^ new_buf[off + len];

        if (!(zrun ^ !!xor)) {
            break;
        }

        len++;
    }

    return len;
}

/*
  page = zrun nzrun
       | zrun nzrun page

  zrun = length

  nzrun = length byte...

  length = uleb128 encoded integer
 */
int xbzrle_encode_buffer(uint8_t *old_buf, uint8_t *new_buf, int slen,
                         uint8_t *dst, int dlen)
{
    bool zrun = true;
    int len, src_off = 0, dst_off = 0;

    g_assert(!(((uintptr_t)old_buf | (uintptr_t)new_buf | slen) %
               sizeof(long)));

    for (; src_off < slen; src_off += len, zrun = !zrun) {
        /* overflow */
        if (dst_off + 2 > dlen) {
            return -1;
        }

        len = next_run(old_buf, new_buf, src_off, slen, zrun);

        if (zrun) {
            /* buffer unchanged */
            if (len == slen) {
                return 0;
            }
            /* skip last zero run */
            if (src_off + len == slen) {
                return dst_off;
            }
        }

        dst_off += uleb128_encode_small(dst + dst_off, len);
        if (!zrun) {
            /* overflow */
            if (dst_off + len > dlen) {
                return -1;
            }
            memcpy(dst + dst_off, new_buf + src_off, len);
            dst_off += len;
        }
    }

    return dst_off;
}

int xbzrle_decode_buffer(uint8_t *src, int slen, uint8_t *dst, int dlen)
{
    int i = 0, d = 0;
    int ret;
    uint32_t count = 0;

    while (i < slen) {

        /* zrun */
        if ((slen - i) < 2) {
            return -1;
        }

        ret = uleb128_decode_small(src + i, &count);
        if (ret < 0 || (i && !count)) {
            return -1;
        }
        i += ret;
        d += count;

        /* overflow */
        if (d > dlen) {
            return -1;
        }

        /* nzrun */
        if ((slen - i) < 2) {
            return -1;
        }

        ret = uleb128_decode_small(src + i, &count);
        if (ret < 0 || !count) {
            return -1;
        }
        i += ret;

        /* overflow */
        if (d + count > dlen || i + count > slen) {
            return -1;
        }

        memcpy(dst + d, src + i, count);
        d += count;
        i += count;
    }

    return d;
}
