/*
 * Unit tests for K3 boot-ROM combined-image parser
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/k3-bootrom.h"

/* Minimal DER emitters. */

static GByteArray *der_tlv(uint8_t tag, const uint8_t *data, size_t len)
{
    GByteArray *a = g_byte_array_new();

    g_byte_array_append(a, &tag, 1);
    if (len < 0x80) {
        uint8_t l = len;
        g_byte_array_append(a, &l, 1);
    } else if (len <= 0xffff) {
        uint8_t l[3] = { 0x82, len >> 8, len & 0xff };
        g_byte_array_append(a, l, 3);
    } else {
        g_assert_not_reached();
    }
    if (data) {
        g_byte_array_append(a, data, len);
    }
    return a;
}

static GByteArray *der_wrap(uint8_t tag, GByteArray *inner)
{
    GByteArray *a = der_tlv(tag, inner->data, inner->len);
    g_byte_array_unref(inner);
    return a;
}

static void der_append(GByteArray *dst, GByteArray *src)
{
    g_byte_array_append(dst, src->data, src->len);
    g_byte_array_unref(src);
}

static GByteArray *der_uint(uint64_t v)
{
    uint8_t buf[9];
    int n = 0;
    uint64_t t = v;

    do {
        n++;
        t >>= 8;
    } while (t);
    if (v >> (n * 8 - 1) & 1) {
        n++; /* leading zero keeps it positive */
    }
    for (int i = 0; i < n; i++) {
        buf[i] = v >> ((n - 1 - i) * 8);
    }
    return der_tlv(0x02, buf, n);
}

/* OID 1.3.6.1.4.1.294.1.9 (ext_boot_info), pre-encoded TLV */
static const uint8_t ext_boot_oid[] = {
    0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x26, 0x01, 0x09
};
/* OID 2.16.840.1.101.3.4.2.3 (sha512), pre-encoded TLV */
static const uint8_t sha512_oid[] = {
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03
};

static GByteArray *der_component(uint32_t ctype, uint32_t core,
                                 uint32_t opts, uint32_t dest, uint64_t size)
{
    GByteArray *seq = g_byte_array_new();
    uint8_t dest_be[4] = { dest >> 24, dest >> 16, dest >> 8, dest };
    uint8_t sha[64] = { 0 };

    der_append(seq, der_uint(ctype));
    der_append(seq, der_uint(core));
    der_append(seq, der_uint(opts));
    der_append(seq, der_tlv(0x04, dest_be, sizeof(dest_be)));
    der_append(seq, der_uint(size));
    g_byte_array_append(seq, sha512_oid, sizeof(sha512_oid));
    der_append(seq, der_tlv(0x04, sha, sizeof(sha)));
    return der_wrap(0x30, seq);
}

/*
 * Layout follows u-boot tools/binman/btool/openssl.py
 * x509_cert_rom_combined(): SBL, SYSFW, SYSFW-DATA payloads are after
 * the certificate.
 */
static GByteArray *make_image(const uint8_t *sbl, size_t sbl_len)
{
    static const uint8_t sysfw_blob[16] = "SYSFW-payload";
    static const uint8_t cfg_blob[8] = "BCFG";
    GByteArray *info = g_byte_array_new();
    GByteArray *ext, *cert, *img;

    der_append(info, der_uint(sbl_len + sizeof(sysfw_blob)
                              + sizeof(cfg_blob)));      /* extImgSize */
    der_append(info, der_uint(3));                       /* numComp */
    der_append(info, der_component(K3_COMP_TYPE_SBL, 16, 0,
                                   0x70000000, sbl_len));
    der_append(info, der_component(K3_COMP_TYPE_SYSFW, 0, 0,
                                   0x44000, sizeof(sysfw_blob)));
    der_append(info, der_component(K3_COMP_TYPE_SYSFW_DATA, 0, 0,
                                   0x7b000, sizeof(cfg_blob)));
    info = der_wrap(0x30, info);

    /* extension is SEQ { OID, OCTETSTRING { info } } */
    ext = g_byte_array_new();
    g_byte_array_append(ext, ext_boot_oid, sizeof(ext_boot_oid));
    der_append(ext, der_wrap(0x04, info));
    ext = der_wrap(0x30, ext);

    /* fake cert: top-level SEQUENCE around the extension */
    cert = der_wrap(0x30, ext);

    img = g_byte_array_new();
    g_byte_array_append(img, cert->data, cert->len);
    g_byte_array_unref(cert);
    g_byte_array_append(img, sbl, sbl_len);
    g_byte_array_append(img, sysfw_blob, sizeof(sysfw_blob));
    g_byte_array_append(img, cfg_blob, sizeof(cfg_blob));
    return img;
}

/* Certificate-only image with caller given component sizes. */
static GByteArray *make_cert_with_sizes(const uint64_t *sizes, size_t n)
{
    GByteArray *info = g_byte_array_new();
    GByteArray *ext;
    uint64_t total = 0;

    for (size_t i = 0; i < n; i++) {
        total += sizes[i];
    }
    der_append(info, der_uint(total));                   /* extImgSize */
    der_append(info, der_uint(n));                       /* numComp */
    for (size_t i = 0; i < n; i++) {
        der_append(info, der_component(K3_COMP_TYPE_SBL, 16, 0,
                                       0x70000000, sizes[i]));
    }
    info = der_wrap(0x30, info);

    ext = g_byte_array_new();
    g_byte_array_append(ext, ext_boot_oid, sizeof(ext_boot_oid));
    der_append(ext, der_wrap(0x04, info));
    ext = der_wrap(0x30, ext);
    return der_wrap(0x30, ext);
}

static void test_parse_ok(void)
{
    static const uint8_t sbl[32] = "SBL-payload";
    GByteArray *img = make_image(sbl, sizeof(sbl));
    K3BootImage out;
    Error *err = NULL;

    g_assert_true(k3_bootrom_parse(img->data, img->len, &out, &err));
    g_assert_null(err);
    g_assert_cmpuint(out.num_comps, ==, 3);
    g_assert_cmpuint(out.comps[0].comp_type, ==, K3_COMP_TYPE_SBL);
    g_assert_cmphex(out.comps[0].dest_addr, ==, 0x70000000);
    g_assert_cmpuint(out.comps[0].comp_size, ==, sizeof(sbl));
    g_assert_cmpuint(out.comps[0].payload_offset, ==, out.cert_len);
    g_assert_cmphex(out.comps[1].dest_addr, ==, 0x44000);
    g_assert_cmpuint(out.comps[2].payload_offset, ==,
                     out.cert_len + sizeof(sbl) + 16);
    g_assert_cmpint(memcmp(img->data + out.comps[0].payload_offset,
                           sbl, sizeof(sbl)), ==, 0);
    g_byte_array_unref(img);
}

static void test_parse_not_der(void)
{
    static const uint8_t junk[64] = { 0xff, 0x00, 0x41 };
    K3BootImage out;
    Error *err = NULL;

    g_assert_false(k3_bootrom_parse(junk, sizeof(junk), &out, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_parse_no_extension(void)
{
    /* Valid DER SEQUENCE, but no ext_boot_info OID. */
    static const uint8_t seq[] = { 0x30, 0x03, 0x02, 0x01, 0x05 };
    K3BootImage out;
    Error *err = NULL;

    g_assert_false(k3_bootrom_parse(seq, sizeof(seq), &out, &err));
    g_assert_nonnull(err);
    error_free(err);
}

static void test_parse_truncated_payload(void)
{
    static const uint8_t sbl[32] = "SBL-payload";
    GByteArray *img = make_image(sbl, sizeof(sbl));
    K3BootImage out;
    Error *err = NULL;

    /* Truncated payload: comp_size claims are beyond file. */
    g_assert_false(k3_bootrom_parse(img->data, img->len - 20, &out, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_byte_array_unref(img);
}

static void test_parse_size_exceeds_u32(void)
{
    /* comp_size wider than 32 bits must not truncate silently. */
    static const uint64_t sizes[1] = { UINT32_MAX + 1ull };
    GByteArray *img = make_cert_with_sizes(sizes, G_N_ELEMENTS(sizes));
    K3BootImage out;
    Error *err = NULL;

    g_assert_false(k3_bootrom_parse(img->data, img->len, &out, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_byte_array_unref(img);
}

static void test_parse_size_sum_wraps_32bit(void)
{
    /*
     * Each single size fits in 32 bits, but their sum must not wrap the
     * running total.
     */
    static const uint64_t sizes[3] = {
        0xf0000000, 0xf0000000, 0xf0000000
    };
    GByteArray *img = make_cert_with_sizes(sizes, G_N_ELEMENTS(sizes));
    K3BootImage out;
    Error *err = NULL;

    g_assert_false(k3_bootrom_parse(img->data, img->len, &out, &err));
    g_assert_nonnull(err);
    error_free(err);
    g_byte_array_unref(img);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/k3-bootrom/parse-ok", test_parse_ok);
    g_test_add_func("/k3-bootrom/not-der", test_parse_not_der);
    g_test_add_func("/k3-bootrom/no-extension", test_parse_no_extension);
    g_test_add_func("/k3-bootrom/truncated", test_parse_truncated_payload);
    g_test_add_func("/k3-bootrom/size-exceeds-u32",
                    test_parse_size_exceeds_u32);
    g_test_add_func("/k3-bootrom/size-sum-wraps-32bit",
                    test_parse_size_sum_wraps_32bit);
    return g_test_run();
}
