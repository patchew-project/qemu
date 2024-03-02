/*
 * VIS op helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

/* This function uses non-native bit order */
#define GET_FIELD(X, FROM, TO)                                  \
    ((X) >> (63 - (TO)) & ((1ULL << ((TO) - (FROM) + 1)) - 1))

/* This function uses the order in the manuals, i.e. bit 0 is 2^0 */
#define GET_FIELD_SP(X, FROM, TO)               \
    GET_FIELD(X, 63 - (TO), 63 - (FROM))

target_ulong helper_array8(target_ulong pixel_addr, target_ulong cubesize)
{
    return (GET_FIELD_SP(pixel_addr, 60, 63) << (17 + 2 * cubesize)) |
        (GET_FIELD_SP(pixel_addr, 39, 39 + cubesize - 1) << (17 + cubesize)) |
        (GET_FIELD_SP(pixel_addr, 17 + cubesize - 1, 17) << 17) |
        (GET_FIELD_SP(pixel_addr, 56, 59) << 13) |
        (GET_FIELD_SP(pixel_addr, 35, 38) << 9) |
        (GET_FIELD_SP(pixel_addr, 13, 16) << 5) |
        (((pixel_addr >> 55) & 1) << 4) |
        (GET_FIELD_SP(pixel_addr, 33, 34) << 2) |
        GET_FIELD_SP(pixel_addr, 11, 12);
}

#if HOST_BIG_ENDIAN
#define VIS_B64(n) b[7 - (n)]
#define VIS_SB64(n) sb[7 - (n)]
#define VIS_W64(n) w[3 - (n)]
#define VIS_SW64(n) sw[3 - (n)]
#define VIS_L64(n) l[1 - (n)]
#define VIS_B32(n) b[3 - (n)]
#define VIS_W32(n) w[1 - (n)]
#else
#define VIS_B64(n) b[n]
#define VIS_SB64(n) sb[n]
#define VIS_W64(n) w[n]
#define VIS_SW64(n) sw[n]
#define VIS_L64(n) l[n]
#define VIS_B32(n) b[n]
#define VIS_W32(n) w[n]
#endif

typedef union {
    uint8_t b[8];
    int8_t sb[8];
    uint16_t w[4];
    int16_t sw[4];
    uint32_t l[2];
    uint64_t ll;
    float64 d;
} VIS64;

typedef union {
    uint8_t b[4];
    uint16_t w[2];
    uint32_t l;
    float32 f;
} VIS32;

uint64_t helper_fpmerge(uint32_t src1, uint32_t src2)
{
    VIS32 s1, s2;
    VIS64 d;

    s1.l = src1;
    s2.l = src2;
    d.ll = 0;

    d.VIS_B64(7) = s1.VIS_B32(3);
    d.VIS_B64(6) = s2.VIS_B32(3);
    d.VIS_B64(5) = s1.VIS_B32(2);
    d.VIS_B64(4) = s2.VIS_B32(2);
    d.VIS_B64(3) = s1.VIS_B32(1);
    d.VIS_B64(2) = s2.VIS_B32(1);
    d.VIS_B64(1) = s1.VIS_B32(0);
    d.VIS_B64(0) = s2.VIS_B32(0);

    return d.ll;
}

static inline int do_ms16b(int x, int y)
{
    return ((x * y) + 0x80) >> 8;
}

uint64_t helper_fmul8x16(uint32_t src1, uint64_t src2)
{
    VIS64 d;
    VIS32 s;

    s.l = src1;
    d.ll = src2;

    d.VIS_W64(0) = do_ms16b(s.VIS_B32(0), d.VIS_SW64(0));
    d.VIS_W64(1) = do_ms16b(s.VIS_B32(1), d.VIS_SW64(1));
    d.VIS_W64(2) = do_ms16b(s.VIS_B32(2), d.VIS_SW64(2));
    d.VIS_W64(3) = do_ms16b(s.VIS_B32(3), d.VIS_SW64(3));

    return d.ll;
}

uint64_t helper_fmul8x16a(uint32_t src1, int32_t src2)
{
    VIS32 s;
    VIS64 d;

    s.l = src1;
    d.ll = 0;

    d.VIS_W64(0) = do_ms16b(s.VIS_B32(0), src2);
    d.VIS_W64(1) = do_ms16b(s.VIS_B32(1), src2);
    d.VIS_W64(2) = do_ms16b(s.VIS_B32(2), src2);
    d.VIS_W64(3) = do_ms16b(s.VIS_B32(3), src2);

    return d.ll;
}

uint64_t helper_fmul8sux16(uint64_t src1, uint64_t src2)
{
    VIS64 s, d;

    s.ll = src1;
    d.ll = src2;

    d.VIS_W64(0) = do_ms16b(s.VIS_SB64(1), d.VIS_SW64(0));
    d.VIS_W64(1) = do_ms16b(s.VIS_SB64(3), d.VIS_SW64(1));
    d.VIS_W64(2) = do_ms16b(s.VIS_SB64(5), d.VIS_SW64(2));
    d.VIS_W64(3) = do_ms16b(s.VIS_SB64(7), d.VIS_SW64(3));

    return d.ll;
}

uint64_t helper_fmul8ulx16(uint64_t src1, uint64_t src2)
{
    VIS64 s, d;

    s.ll = src1;
    d.ll = src2;

    d.VIS_W64(0) = do_ms16b(s.VIS_B64(0), d.VIS_SW64(0));
    d.VIS_W64(1) = do_ms16b(s.VIS_B64(2), d.VIS_SW64(1));
    d.VIS_W64(2) = do_ms16b(s.VIS_B64(4), d.VIS_SW64(2));
    d.VIS_W64(3) = do_ms16b(s.VIS_B64(6), d.VIS_SW64(3));

    return d.ll;
}

uint64_t helper_fexpand(uint32_t src2)
{
    VIS32 s;
    VIS64 d;

    s.l = src2;
    d.ll = 0;
    d.VIS_W64(0) = s.VIS_B32(0) << 4;
    d.VIS_W64(1) = s.VIS_B32(1) << 4;
    d.VIS_W64(2) = s.VIS_B32(2) << 4;
    d.VIS_W64(3) = s.VIS_B32(3) << 4;

    return d.ll;
}

#define VIS_CMPHELPER(name, F)                                    \
    uint64_t name##16(uint64_t src1, uint64_t src2)               \
    {                                                             \
        VIS64 s, d;                                               \
                                                                  \
        s.ll = src1;                                              \
        d.ll = src2;                                              \
                                                                  \
        d.VIS_W64(0) = F(s.VIS_W64(0), d.VIS_W64(0)) ? 1 : 0;     \
        d.VIS_W64(0) |= F(s.VIS_W64(1), d.VIS_W64(1)) ? 2 : 0;    \
        d.VIS_W64(0) |= F(s.VIS_W64(2), d.VIS_W64(2)) ? 4 : 0;    \
        d.VIS_W64(0) |= F(s.VIS_W64(3), d.VIS_W64(3)) ? 8 : 0;    \
        d.VIS_W64(1) = d.VIS_W64(2) = d.VIS_W64(3) = 0;           \
                                                                  \
        return d.ll;                                              \
    }                                                             \
                                                                  \
    uint64_t name##32(uint64_t src1, uint64_t src2)               \
    {                                                             \
        VIS64 s, d;                                               \
                                                                  \
        s.ll = src1;                                              \
        d.ll = src2;                                              \
                                                                  \
        d.VIS_L64(0) = F(s.VIS_L64(0), d.VIS_L64(0)) ? 1 : 0;     \
        d.VIS_L64(0) |= F(s.VIS_L64(1), d.VIS_L64(1)) ? 2 : 0;    \
        d.VIS_L64(1) = 0;                                         \
                                                                  \
        return d.ll;                                              \
    }

#define FCMPGT(a, b) ((a) > (b))
#define FCMPEQ(a, b) ((a) == (b))
#define FCMPLE(a, b) ((a) <= (b))
#define FCMPNE(a, b) ((a) != (b))

VIS_CMPHELPER(helper_fcmpgt, FCMPGT)
VIS_CMPHELPER(helper_fcmpeq, FCMPEQ)
VIS_CMPHELPER(helper_fcmple, FCMPLE)
VIS_CMPHELPER(helper_fcmpne, FCMPNE)

uint64_t helper_fcmpeq8(uint64_t src1, uint64_t src2)
{
    uint64_t a = src1 ^ src2;
    uint64_t m = 0x7f7f7f7f7f7f7f7fULL;
    uint64_t c = ~(((a & m) + m) | a | m);

    /* a.......b.......c.......d.......e.......f.......g.......h....... */
    c |= c << 7;
    /* ab......bc......cd......de......ef......fg......gh......h....... */
    c |= c << 14;
    /* abcd....bcde....cdef....defg....efgh....fgh.....gh......h....... */
    c |= c << 28;
    /* abcdefghbcdefgh.cdefgh..defgh...efgh....fgh.....gh......h....... */
    return c >> 56;
}

uint64_t helper_fcmpne8(uint64_t src1, uint64_t src2)
{
    return helper_fcmpeq8(src1, src2) ^ 0xff;
}

uint64_t helper_fcmpule8(uint64_t src1, uint64_t src2)
{
    VIS64 s1, s2;
    uint64_t r = 0;

    s1.ll = src1;
    s2.ll = src2;

    for (int i = 0; i < 8; ++i) {
        r |= (s1.VIS_B64(i) <= s2.VIS_B64(i)) << i;
    }
    return r;
}

uint64_t helper_fcmpugt8(uint64_t src1, uint64_t src2)
{
    return helper_fcmpule8(src1, src2) ^ 0xff;
}

uint64_t helper_pdist(uint64_t sum, uint64_t src1, uint64_t src2)
{
    int i;
    for (i = 0; i < 8; i++) {
        int s1, s2;

        s1 = (src1 >> (56 - (i * 8))) & 0xff;
        s2 = (src2 >> (56 - (i * 8))) & 0xff;

        /* Absolute value of difference. */
        s1 -= s2;
        if (s1 < 0) {
            s1 = -s1;
        }

        sum += s1;
    }

    return sum;
}

uint32_t helper_fpack16(uint64_t gsr, uint64_t rs2)
{
    int scale = (gsr >> 3) & 0xf;
    uint32_t ret = 0;
    int byte;

    for (byte = 0; byte < 4; byte++) {
        uint32_t val;
        int16_t src = rs2 >> (byte * 16);
        int32_t scaled = src << scale;
        int32_t from_fixed = scaled >> 7;

        val = (from_fixed < 0 ?  0 :
               from_fixed > 255 ?  255 : from_fixed);

        ret |= val << (8 * byte);
    }

    return ret;
}

uint64_t helper_fpack32(uint64_t gsr, uint64_t rs1, uint64_t rs2)
{
    int scale = (gsr >> 3) & 0x1f;
    uint64_t ret = 0;
    int word;

    ret = (rs1 << 8) & ~(0x000000ff000000ffULL);
    for (word = 0; word < 2; word++) {
        uint64_t val;
        int32_t src = rs2 >> (word * 32);
        int64_t scaled = (int64_t)src << scale;
        int64_t from_fixed = scaled >> 23;

        val = (from_fixed < 0 ? 0 :
               (from_fixed > 255) ? 255 : from_fixed);

        ret |= val << (32 * word);
    }

    return ret;
}

uint32_t helper_fpackfix(uint64_t gsr, uint64_t rs2)
{
    int scale = (gsr >> 3) & 0x1f;
    uint32_t ret = 0;
    int word;

    for (word = 0; word < 2; word++) {
        uint32_t val;
        int32_t src = rs2 >> (word * 32);
        int64_t scaled = (int64_t)src << scale;
        int64_t from_fixed = scaled >> 16;

        val = (from_fixed < -32768 ? -32768 :
               from_fixed > 32767 ?  32767 : from_fixed);

        ret |= (val & 0xffff) << (word * 16);
    }

    return ret;
}

uint64_t helper_bshuffle(uint64_t gsr, uint64_t src1, uint64_t src2)
{
    union {
        uint64_t ll[2];
        uint8_t b[16];
    } s;
    VIS64 r;
    uint32_t i, mask, host;

    /* Set up S such that we can index across all of the bytes.  */
#if HOST_BIG_ENDIAN
    s.ll[0] = src1;
    s.ll[1] = src2;
    host = 0;
#else
    s.ll[1] = src1;
    s.ll[0] = src2;
    host = 15;
#endif
    mask = gsr >> 32;

    for (i = 0; i < 8; ++i) {
        unsigned e = (mask >> (28 - i*4)) & 0xf;
        r.VIS_B64(i) = s.b[e ^ host];
    }

    return r.ll;
}

uint64_t helper_cmask8(uint64_t gsr, uint64_t src)
{
    uint32_t mask = 0;

    mask |= (src & 0x01 ? 0x00000007 : 0x0000000f);
    mask |= (src & 0x02 ? 0x00000060 : 0x000000e0);
    mask |= (src & 0x04 ? 0x00000500 : 0x00000d00);
    mask |= (src & 0x08 ? 0x00004000 : 0x0000c000);
    mask |= (src & 0x10 ? 0x00030000 : 0x000b0000);
    mask |= (src & 0x20 ? 0x00200000 : 0x00a00000);
    mask |= (src & 0x40 ? 0x01000000 : 0x09000000);
    mask |= (src & 0x80 ? 0x00000000 : 0x80000000);

    return deposit64(gsr, 32, 32, mask);
}

uint64_t helper_cmask16(uint64_t gsr, uint64_t src)
{
    uint32_t mask = 0;

    mask |= (src & 0x1 ? 0x00000067 : 0x000000ef);
    mask |= (src & 0x2 ? 0x00004500 : 0x0000cd00);
    mask |= (src & 0x4 ? 0x00230000 : 0x00ab0000);
    mask |= (src & 0x8 ? 0x01000000 : 0x89000000);

    return deposit64(gsr, 32, 32, mask);
}

uint64_t helper_cmask32(uint64_t gsr, uint64_t src)
{
    uint32_t mask = 0;

    mask |= (src & 0x1 ? 0x00004567 : 0x0000cdef);
    mask |= (src & 0x2 ? 0x01230000 : 0x89ab0000);

    return deposit64(gsr, 32, 32, mask);
}

static inline uint16_t do_fchksm16(uint16_t src1, uint16_t src2)
{
    uint16_t a = src1 + src2;
    uint16_t c = a < src1;
    return a + c;
}

uint64_t helper_fchksm16(uint64_t src1, uint64_t src2)
{
    VIS64 r, s1, s2;

    s1.ll = src1;
    s2.ll = src2;
    r.ll = 0;

    r.VIS_W64(0) = do_fchksm16(s1.VIS_W64(0), s2.VIS_W64(0));
    r.VIS_W64(1) = do_fchksm16(s1.VIS_W64(1), s2.VIS_W64(1));
    r.VIS_W64(2) = do_fchksm16(s1.VIS_W64(2), s2.VIS_W64(2));
    r.VIS_W64(3) = do_fchksm16(s1.VIS_W64(3), s2.VIS_W64(3));

    return r.ll;
}

static inline int16_t do_fmean16(int16_t src1, int16_t src2)
{
    return (src1 + src2 + 1) / 2;
}

uint64_t helper_fmean16(uint64_t src1, uint64_t src2)
{
    VIS64 r, s1, s2;

    s1.ll = src1;
    s2.ll = src2;
    r.ll = 0;

    r.VIS_SW64(0) = do_fmean16(s1.VIS_SW64(0), s2.VIS_SW64(0));
    r.VIS_SW64(1) = do_fmean16(s1.VIS_SW64(1), s2.VIS_SW64(1));
    r.VIS_SW64(2) = do_fmean16(s1.VIS_SW64(2), s2.VIS_SW64(2));
    r.VIS_SW64(3) = do_fmean16(s1.VIS_SW64(3), s2.VIS_SW64(3));

    return r.ll;
}

uint64_t helper_fslas16(uint64_t src1, uint64_t src2)
{
    VIS64 r, s1, s2;

    s1.ll = src1;
    s2.ll = src2;
    r.ll = 0;

    for (int i = 0; i < 4; ++i) {
        int t = s1.VIS_SW64(i) << (s2.VIS_W64(i) % 16);
        t = MIN(t, INT16_MAX);
        t = MAX(t, INT16_MIN);
        r.VIS_SW64(i) = t;
    }

    return r.ll;
}

uint64_t helper_fslas32(uint64_t src1, uint64_t src2)
{
    VIS64 r, s1, s2;

    s1.ll = src1;
    s2.ll = src2;
    r.ll = 0;

    for (int i = 0; i < 2; ++i) {
        int64_t t = (int64_t)(int32_t)s1.VIS_L64(i) << (s2.VIS_L64(i) % 32);
        t = MIN(t, INT32_MAX);
        t = MAX(t, INT32_MIN);
        r.VIS_L64(i) = t;
    }

    return r.ll;
}
