/*
 * TI K3 boot-ROM emulation: X.509 combined boot image parser
 *
 * Parses the DER wrapper and ext_boot_info extension
 * (OID 1.3.6.1.4.1.294.1.9) for payload type, destination and size.
 * No signature verification, since QEMU models a GP device.
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/k3-bootrom.h"

typedef struct DerSlice {
    const uint8_t *p;
    const uint8_t *end;
} DerSlice;

static bool der_read_tlv(DerSlice *s, uint8_t *tag, DerSlice *content,
                         Error **errp)
{
    uint64_t len;

    if (s->end - s->p < 2) {
        error_setg(errp, "k3-bootrom: truncated DER structure");
        return false;
    }
    *tag = *s->p++;
    len = *s->p++;
    if (len & 0x80) {
        unsigned n = len & 0x7f;

        if (n == 0 || n > 4 || (size_t)(s->end - s->p) < n) {
            error_setg(errp, "k3-bootrom: bad DER length encoding");
            return false;
        }
        len = 0;
        while (n--) {
            len = (len << 8) | *s->p++;
        }
    }
    if ((uint64_t)(s->end - s->p) < len) {
        error_setg(errp, "k3-bootrom: DER length exceeds buffer");
        return false;
    }
    content->p = s->p;
    content->end = s->p + len;
    s->p += len;
    return true;
}

static bool der_read_uint(DerSlice *s, uint64_t *out, Error **errp)
{
    DerSlice c;
    uint8_t tag;
    uint64_t v = 0;

    if (!der_read_tlv(s, &tag, &c, errp)) {
        return false;
    }
    if (tag != 0x02) {
        error_setg(errp, "k3-bootrom: expected INTEGER, got tag 0x%02x",
                   tag);
        return false;
    }
    if (c.p == c.end) {
        error_setg(errp, "k3-bootrom: empty INTEGER");
        return false;
    }
    if (c.end - c.p > 9 || (c.end - c.p == 9 && c.p[0] != 0)) {
        error_setg(errp, "k3-bootrom: INTEGER too large");
        return false;
    }
    for (const uint8_t *q = c.p; q < c.end; q++) {
        v = (v << 8) | *q;
    }
    *out = v;
    return true;
}

static bool der_read_u32(DerSlice *s, uint32_t *out, Error **errp)
{
    uint64_t v;

    if (!der_read_uint(s, &v, errp)) {
        return false;
    }
    if (v > UINT32_MAX) {
        error_setg(errp, "k3-bootrom: integer field %" PRIu64
                   " exceeds 32 bits", v);
        return false;
    }
    *out = v;
    return true;
}

/* Big-endian OCTET STRING (<= 8 bytes), as uint64. */
static bool der_read_addr(DerSlice *s, uint64_t *out, Error **errp)
{
    DerSlice c;
    uint8_t tag;
    uint64_t v = 0;

    if (!der_read_tlv(s, &tag, &c, errp)) {
        return false;
    }
    if (tag != 0x04 || c.end - c.p > 8) {
        error_setg(errp, "k3-bootrom: bad destAddr field (tag 0x%02x)",
                   tag);
        return false;
    }
    for (const uint8_t *q = c.p; q < c.end; q++) {
        v = (v << 8) | *q;
    }
    *out = v;
    return true;
}

/* DER TLV for TI ext_boot_info OID 1.3.6.1.4.1.294.1.9. */
static const uint8_t k3_ext_boot_oid[] = {
    0x06, 0x09, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x26, 0x01, 0x09
};

static const uint8_t *find_bytes(const uint8_t *hay, size_t hay_len,
                                 const uint8_t *needle, size_t needle_len)
{
    if (hay_len < needle_len) {
        return NULL;
    }
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (memcmp(hay + i, needle, needle_len) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

bool k3_bootrom_parse(const uint8_t *buf, size_t len, K3BootImage *out,
                      Error **errp)
{
    DerSlice top = { buf, buf + len };
    DerSlice cert, rest, octets, info;
    const uint8_t *oid;
    uint8_t tag;
    uint64_t v;
    uint64_t payload_off;

    memset(out, 0, sizeof(*out));

    if (!der_read_tlv(&top, &tag, &cert, errp)) {
        return false;
    }
    if (tag != 0x30) {
        error_setg(errp,
                   "k3-bootrom: not an X.509 boot image (tag 0x%02x)", tag);
        return false;
    }
    out->cert_len = cert.end - buf;

    oid = find_bytes(cert.p, cert.end - cert.p, k3_ext_boot_oid,
                     sizeof(k3_ext_boot_oid));
    if (!oid) {
        error_setg(errp, "k3-bootrom: ext_boot_info extension "
                   "(OID 1.3.6.1.4.1.294.1.9) not found");
        return false;
    }
    rest.p = oid + sizeof(k3_ext_boot_oid);
    rest.end = cert.end;

    /* Optional BOOLEAN 'critical', between OID and extnValue. */
    if (rest.p < rest.end && rest.p[0] == 0x01) {
        DerSlice skip;

        if (!der_read_tlv(&rest, &tag, &skip, errp)) {
            return false;
        }
    }
    if (!der_read_tlv(&rest, &tag, &octets, errp)) {
        return false;
    }
    if (tag != 0x04) {
        error_setg(errp, "k3-bootrom: extension value is not an "
                   "OCTET STRING (tag 0x%02x)", tag);
        return false;
    }
    if (!der_read_tlv(&octets, &tag, &info, errp)) {
        return false;
    }
    if (tag != 0x30) {
        error_setg(errp, "k3-bootrom: ext_boot_info is not a SEQUENCE");
        return false;
    }

    if (!der_read_uint(&info, &out->ext_img_size, errp)) {
        return false;
    }
    if (!der_read_uint(&info, &v, errp)) {
        return false;
    }
    if (v == 0 || v > K3_BOOTROM_MAX_COMPS) {
        error_setg(errp, "k3-bootrom: unsupported component count %"
                   PRIu64, v);
        return false;
    }
    out->num_comps = v;

    payload_off = out->cert_len;
    for (uint32_t i = 0; i < out->num_comps; i++) {
        K3BootComponent *c = &out->comps[i];
        DerSlice comp;

        if (!der_read_tlv(&info, &tag, &comp, errp)) {
            return false;
        }
        if (tag != 0x30) {
            error_setg(errp, "k3-bootrom: component %u is not a SEQUENCE",
                       i);
            return false;
        }
        if (!der_read_u32(&comp, &c->comp_type, errp)) {
            return false;
        }
        if (!der_read_u32(&comp, &c->boot_core, errp)) {
            return false;
        }
        if (!der_read_u32(&comp, &c->comp_opts, errp)) {
            return false;
        }
        if (!der_read_addr(&comp, &c->dest_addr, errp)) {
            return false;
        }
        if (!der_read_u32(&comp, &c->comp_size, errp)) {
            return false;
        }
        /* shaType / shaValue are not needed for the loading. */
        /* payload_off is bounded by len below, so it fits into size_t. */
        c->payload_offset = payload_off;
        payload_off += c->comp_size;
        if (payload_off > len) {
            error_setg(errp, "k3-bootrom: image truncated (components "
                       "need %" PRIu64 " bytes, file has %zu)",
                       payload_off, len);
            return false;
        }
    }
    return true;
}
