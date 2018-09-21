/*
 * QEMU EDID generator.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/bswap.h"
#include "hw/display/edid.h"

static const struct edid_mode {
    uint32_t xres;
    uint32_t yres;
    uint32_t byte;
    uint32_t xtra3;
    uint32_t bit;
} modes[] = {
    /* other */
    { .xres = 2048,   .yres = 1152 },
    { .xres = 1920,   .yres = 1440 },
    { .xres = 1920,   .yres = 1080 },

    /* additional standard timings 3 (all @75Hz) */
    { .xres = 1920,   .yres = 1200,   .xtra3 = 11,   .bit = 7 },
    { .xres = 1856,   .yres = 1392,   .xtra3 = 10,   .bit = 2 },
    { .xres = 1792,   .yres = 1344,   .xtra3 = 10,   .bit = 4 },
    { .xres = 1600,   .yres = 1200,   .xtra3 = 10,   .bit = 7 },
    { .xres = 1680,   .yres = 1050,   .xtra3 =  9,   .bit = 4 },
    { .xres = 1440,   .yres = 1050,   .xtra3 =  8,   .bit = 0 },
    { .xres = 1440,   .yres =  900,   .xtra3 =  8,   .bit = 4 },
    { .xres = 1280,   .yres =  768,   .xtra3 =  7,   .bit = 5 },

    /* established timings (all @75Hz) */
    { .xres = 1280,   .yres = 1024,   .byte  = 36,   .bit = 0 },
    { .xres = 1024,   .yres =  768,   .byte  = 36,   .bit = 1 },
    { .xres =  800,   .yres =  600,   .byte  = 36,   .bit = 6 },
    { .xres =  640,   .yres =  480,   .byte  = 35,   .bit = 2 },
};

static int edid_std_mode(uint8_t *mode, uint32_t xres, uint32_t yres)
{
    uint32_t aspect;

    if (xres == 0 || yres == 0) {
        mode[0] = 0x01;
        mode[1] = 0x01;
        return 0;

    } else if (xres * 10 == yres * 16) {
        aspect = 0;
    } else if (xres * 3 == yres * 4) {
        aspect = 1;
    } else if (xres * 4 == yres * 5) {
        aspect = 2;
    } else if (xres * 9 == yres * 16) {
        aspect = 3;
    } else {
        return -1;
    }

    mode[0] = (xres / 8) - 31;
    mode[1] = ((aspect << 6) | (75 - 60));
    return 0;
}

static void edid_fill_modes(uint8_t *edid, uint8_t *xtra3,
                            uint32_t maxx, uint32_t maxy)
{
    const struct edid_mode *mode;
    int std = 38;
    int rc, i;

    for (i = 0; i < ARRAY_SIZE(modes); i++) {
        mode = modes + i;

        if ((maxx && mode->xres > maxx) ||
            (maxy && mode->yres > maxy)) {
            continue;
        }

        if (mode->byte) {
            edid[mode->byte] |= (1 << mode->bit);
        } else if (mode->xtra3 && xtra3) {
            xtra3[mode->xtra3] |= (1 << mode->bit);
        } else if (std < 54) {
            rc = edid_std_mode(edid + std, mode->xres, mode->yres);
            if (rc == 0) {
                std += 2;
            }
        }
    }

    while (std < 54) {
        edid_std_mode(edid + std, 0, 0);
        std += 2;
    }
}

static void edid_checksum(uint8_t *edid)
{
    uint32_t sum = 0;
    int i;

    for (i = 0; i < 127; i++) {
        sum += edid[i];
    }
    sum &= 0xff;
    if (sum) {
        edid[127] = 0x100 - sum;
    }
}

static void edid_desc_type(uint8_t *desc, uint8_t type)
{
    desc[0] = 0;
    desc[1] = 0;
    desc[2] = 0;
    desc[3] = type;
    desc[4] = 0;
}

static void edid_desc_text(uint8_t *desc, uint8_t type,
                           const char *text)
{
    size_t len;

    edid_desc_type(desc, type);
    memset(desc + 5, ' ', 13);

    len = strlen(text);
    if (len > 12)
        len = 12;
    strncpy((char*)(desc + 5), text, len);
    desc[ 5 + len ] = '\n';
}

static void edid_desc_ranges(uint8_t *desc)
{
    edid_desc_type(desc, 0xfd);

    /* vertical */
    desc[ 5] =  50;
    desc[ 6] = 100;

    /* horizontal */
    desc[ 7] =  30;
    desc[ 8] = 120;

    /* dot clock */
    desc[ 9] = 250 / 10;

    /* padding */
    desc[11] = '\n';
    memset(desc + 12, ' ', 6);
}

/* additional standard timings 3 */
static void edid_desc_xtra3_std(uint8_t *desc)
{
    edid_desc_type(desc, 0xf7);
    desc[5] = 10;
}

static void edid_desc_dummy(uint8_t *desc)
{
    edid_desc_type(desc, 0x10);
}

static void edid_desc_timing(uint8_t *desc,
                             uint32_t xres, uint32_t yres,
                             uint32_t dpi)
{
    /* physical display size */
    uint32_t xmm = xres * dpi / 254;
    uint32_t ymm = yres * dpi / 254;

    /* pull some realistic looking timings out of thin air */
    uint32_t xfront = xres * 25 / 100;
    uint32_t xsync  = xres *  3 / 100;
    uint32_t xblank = xres * 35 / 100;

    uint32_t yfront = yres *  5 / 1000;
    uint32_t ysync  = yres *  5 / 1000;
    uint32_t yblank = yres * 35 / 1000;

    uint32_t clock  = 75 * (xres + xblank) * (yres + yblank);

    *(uint32_t*)(desc) = cpu_to_le32(clock / 10000);

    desc[2] = xres   & 0xff;
    desc[3] = xblank & 0xff;
    desc[4] = ((( xres   & 0xf00 ) >> 4) |
               (( xblank & 0xf00 ) >> 8));

    desc[5] = yres   & 0xff;
    desc[6] = yblank & 0xff;
    desc[7] = ((( yres   & 0xf00 ) >> 4) |
               (( yblank & 0xf00 ) >> 8));

    desc[8] = xfront & 0xff;
    desc[9] = xsync  & 0xff;

    desc[10] = ((( yfront & 0x00f ) << 4) |
                (( ysync  & 0x00f ) << 0));
    desc[11] = ((( xfront & 0x300 ) >> 2) |
                (( xsync  & 0x300 ) >> 4) |
                (( yfront & 0x030 ) >> 2) |
                (( ysync  & 0x030 ) >> 4));

    desc[12] = xmm & 0xff;
    desc[13] = ymm & 0xff;
    desc[14] = ((( xmm & 0xf00 ) >> 4) |
                (( ymm & 0xf00 ) >> 8));

    desc[17] = 0x18;
}

void qemu_edid_generate(uint8_t *edid, size_t size,
                        qemu_edid_info *info)
{
    uint32_t desc = 54;
    uint8_t *xtra3 = NULL;

    /* =============== set defaults  =============== */

    if (!info->vendor || strlen(info->vendor) != 3) {
        info->vendor = "EMU";
    }
    if (!info->name) {
        info->name = "QEMU Monitor";
    }
    if (!info->dpi) {
        info->dpi = 100;
    }
    if (!info->prefx) {
        info->prefx = 1024;
    }
    if (!info->prefy) {
        info->prefy = 768;
    }

    /* =============== header information =============== */

    /* fixed */
    edid[0] = 0x00;
    edid[1] = 0xff;
    edid[2] = 0xff;
    edid[3] = 0xff;
    edid[4] = 0xff;
    edid[5] = 0xff;
    edid[6] = 0xff;
    edid[7] = 0x00;

    /* manufacturer id, product code, serial number */
    uint16_t vendor_id = ((((info->vendor[0] - '@') & 0x1f) << 10) |
                          (((info->vendor[1] - '@') & 0x1f) <<  5) |
                          (((info->vendor[2] - '@') & 0x1f) <<  0));
    uint16_t model_nr = 0x1234;
    uint32_t serial_nr = info->serial ? atoi(info->serial) : 0;
    *(uint16_t*)(edid +  8) = cpu_to_be16(vendor_id);
    *(uint16_t*)(edid + 10) = cpu_to_le16(model_nr);
    *(uint32_t*)(edid + 12) = cpu_to_le32(serial_nr);

    /* manufacture week and year */
    edid[16] = 42;
    edid[17] = 2014 - 1990;

    /* edid version */
    edid[18] = 1;
    edid[19] = 4;


    /* =============== basic display parameters =============== */

    /* video input: digital, 8bpc, displayport */
    edid[20] = 0xa5;

    /* screen size: undefined */
    edid[21] = info->prefx * info->dpi / 2540;
    edid[22] = info->prefy * info->dpi / 2540;

    /* display gamma: 1.0 */
    edid[23] = 0x00;

    /* supported features bitmap: preferred timing */
    edid[24] = 0x02;


    /* =============== chromaticity coordinates =============== */

    /* TODO (bytes 25 -> 34) */


    /* =============== established timing bitmap =============== */
    /* =============== standard timing information =============== */

    /* both filled by edid_fill_modes() */


    /* =============== descriptor blocks =============== */

    edid_desc_timing(edid + desc, info->prefx, info->prefy, info->dpi);
    desc += 18;

    edid_desc_ranges(edid + desc);
    desc += 18;

    if (info->name) {
        edid_desc_text(edid + desc, 0xfc, info->name);
        desc += 18;
    }

    if (info->serial) {
        edid_desc_text(edid + desc, 0xff, info->serial);
        desc += 18;
    }

    if (desc < 126) {
        xtra3 = edid + desc;
        edid_desc_xtra3_std(xtra3);
        desc += 18;
    }

    while (desc < 126) {
        edid_desc_dummy(edid + desc);
        desc += 18;
    }

    /* =============== finish up =============== */

    edid_fill_modes(edid, xtra3, info->maxx, info->maxy);
    edid_checksum(edid);
}

size_t qemu_edid_size(uint8_t *edid)
{
    uint32_t exts;

    if (edid[0] != 0x00 ||
        edid[1] != 0xff) {
        /* doesn't look like a valid edid block */
        return 0;
    }

    exts = edid[126];
    return 128 * (exts + 1);
}
