/*
 * Renesas RX CRC Calculator (CRCA)
 *
 * Datasheet: RX65N Group, RX651 Group User's Manual: Hardware
 *            (Rev.2.40 R01UH0590EJ0240), section 42
 *
 * Copyright (c) 2026 Umar, Sayyad Mahammad
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Writing to CRCDIR runs the selected polynomial over the data and leaves the
 * result in CRCDOR, which doubles as the initial value: it is zero out of
 * reset, DORCLR puts it back, and firmware wanting a different seed writes it
 * directly. There is no final XOR, so the familiar CRC-32 of a message is
 * obtained by seeding CRCDOR with all ones and inverting the result.
 *
 * CRCCR.LMS picks the bit order. MSB first is the plain shift-left form; LSB
 * first is the reflected form, computed with the reversed polynomial. The two
 * worked examples in section 42.3 pin this down: with the CCITT polynomial and
 * a cleared CRCDOR, feeding 0xf0 gives 0xef1f MSB first and 0xf78f LSB first.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "hw/misc/renesas_rx_crc.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

REG8(CRCCR, 0)
    FIELD(CRCCR, GPS, 0, 3)
    FIELD(CRCCR, LMS, 6, 1)
    FIELD(CRCCR, DORCLR, 7, 1)

#define R_CRCDIR    0x04
#define R_CRCDOR    0x08

/* CRCCR is read/write apart from the write-only DORCLR bit. */
#define CRCCR_RW_MASK   (R_CRCCR_GPS_MASK | R_CRCCR_LMS_MASK)

/*
 * Generating polynomials selected by CRCCR.GPS, without the implicit top
 * term. Entries 0, 6 and 7 select "no calculation".
 */
static const struct {
    uint32_t poly;
    uint8_t width;
} crc_gps[8] = {
    [1] = { 0x07,       8  },   /* X8 + X2 + X + 1                          */
    [2] = { 0x8005,     16 },   /* X16 + X15 + X2 + 1                       */
    [3] = { 0x1021,     16 },   /* X16 + X12 + X5 + 1                       */
    [4] = { 0x04c11db7, 32 },   /* X32 + X26 + X23 + ... + X2 + X + 1       */
    [5] = { 0x1edc6f41, 32 },   /* X32 + X28 + X27 + ... + X8 + X6 + 1      */
};

static uint32_t bitreverse(uint32_t value, unsigned width)
{
    return revbit32(value) >> (32 - width);
}

/* Run nbits of data through the selected polynomial. */
static void crc_feed(RenesasRxCrcState *s, uint32_t data, unsigned nbits)
{
    unsigned gps = FIELD_EX8(s->crccr, CRCCR, GPS);
    uint32_t poly = crc_gps[gps].poly;
    unsigned width = crc_gps[gps].width;
    uint32_t mask, crc;

    if (width == 0) {
        return;             /* GPS selects no calculation */
    }
    if (nbits > width) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas-rx-crc: %u-bit write to CRCDIR with a %u-bit "
                      "polynomial selected\n", nbits, width);
        nbits = width;
    }

    mask = width == 32 ? UINT32_MAX : (1u << width) - 1;
    crc = s->crcdor & mask;

    if (FIELD_EX8(s->crccr, CRCCR, LMS)) {
        /* MSB first: shift up, align the data with the top of the register. */
        crc ^= (data << (width - nbits)) & mask;
        while (nbits--) {
            crc = (crc & (mask ^ (mask >> 1))) ? ((crc << 1) ^ poly) & mask
                                               : (crc << 1) & mask;
        }
    } else {
        /* LSB first: the reflected form, so shift down with a reversed poly. */
        uint32_t rpoly = bitreverse(poly, width);

        crc ^= data & mask;
        while (nbits--) {
            crc = (crc & 1) ? (crc >> 1) ^ rpoly : crc >> 1;
        }
    }

    s->crcdor = crc;
}

static uint64_t crc_read(void *opaque, hwaddr offset, unsigned size)
{
    RenesasRxCrcState *s = opaque;

    switch (offset) {
    case A_CRCCR:
        /* DORCLR always reads back as 0. */
        return s->crccr & CRCCR_RW_MASK;
    case R_CRCDIR:
        /*
         * CRCDIR is documented readable, but the hardware keeps no separate
         * copy of the data written to it, so nothing meaningful comes back.
         */
        return 0;
    case R_CRCDOR:
        if (size >= 4) {
            return s->crcdor;
        }
        return s->crcdor & ((1u << (size * 8)) - 1);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas-rx-crc: read from unmapped offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void crc_write(void *opaque, hwaddr offset, uint64_t value,
                      unsigned size)
{
    RenesasRxCrcState *s = opaque;

    switch (offset) {
    case A_CRCCR:
        s->crccr = value & CRCCR_RW_MASK;
        if (FIELD_EX8(value, CRCCR, DORCLR)) {
            s->crcdor = 0;
        }
        break;
    case R_CRCDIR:
        crc_feed(s, value, size * 8);
        break;
    case R_CRCDOR:
        /* Writable so firmware can seed the calculation. */
        if (size >= 4) {
            s->crcdor = value;
        } else {
            uint32_t m = (1u << (size * 8)) - 1;
            s->crcdor = (s->crcdor & ~m) | (value & m);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas-rx-crc: write to unmapped offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps crc_ops = {
    .read = crc_read,
    .write = crc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = { .min_access_size = 1, .max_access_size = 4 },
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

static void rx_crc_reset(DeviceState *dev)
{
    RenesasRxCrcState *s = RENESAS_RX_CRC(dev);

    s->crccr = 0;
    s->crcdor = 0;
}

static void rx_crc_init(Object *obj)
{
    RenesasRxCrcState *s = RENESAS_RX_CRC(obj);

    memory_region_init_io(&s->memory, obj, &crc_ops, s,
                          "renesas-rx-crc", RX_CRC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->memory);
}

static const VMStateDescription vmstate_rx_crc = {
    .name = "renesas-rx-crc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(crccr, RenesasRxCrcState),
        VMSTATE_UINT32(crcdor, RenesasRxCrcState),
        VMSTATE_END_OF_LIST()
    }
};

static void rx_crc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rx_crc;
    device_class_set_legacy_reset(dc, rx_crc_reset);
}

static const TypeInfo rx_crc_info = {
    .name          = TYPE_RENESAS_RX_CRC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RenesasRxCrcState),
    .instance_init = rx_crc_init,
    .class_init    = rx_crc_class_init,
};

static void rx_crc_register_types(void)
{
    type_register_static(&rx_crc_info);
}

type_init(rx_crc_register_types)
