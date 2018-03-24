/* Common code for testing of Dallas/Maxim I2C bus RTC devices
 *
 * Copyright (c) 2018 Michael Davidsaver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the LICENSE file in the top-level directory.
 */
#ifndef DSRTCCOMMON_H
#define DSRTCCOMMON_H

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "libqos/i2c.h"

#define IMX25_I2C_0_BASE 0x43F80000
#define DS1338_ADDR 0x68

static I2CAdapter *i2c;
static uint8_t addr;
static bool use_century;

/* input buffer must have at least 7 elements */
static inline time_t rtc_parse(const uint8_t *buf)
{
    struct tm parts;

    parts.tm_sec = from_bcd(buf[0]);
    parts.tm_min = from_bcd(buf[1]);
    if (buf[2] & 0x40) {
        /* 12 hour */
        /* HOUR register is 1-12. */
        parts.tm_hour = from_bcd(buf[2] & 0x1f);
        g_assert_cmpuint(parts.tm_hour, >=, 1);
        g_assert_cmpuint(parts.tm_hour, <=, 12);
        parts.tm_hour %= 12u; /* wrap 12 -> 0 */
        if (buf[2] & 0x20) {
            parts.tm_hour += 12u;
        }
    } else {
        /* 24 hour */
        parts.tm_hour = from_bcd(buf[2] & 0x3f);
    }
    parts.tm_wday = from_bcd(buf[3]);
    parts.tm_mday = from_bcd(buf[4]);
    parts.tm_mon =  from_bcd((buf[5] & 0x1f) - 1u);
    parts.tm_year = from_bcd(buf[6]);
    if (!use_century || (buf[5] & 0x80)) {
        parts.tm_year += 100u;
    }

    return mktimegm(&parts);
}

static time_t rtc_gettime(void)
{
    uint8_t buf[7];

    /* zero address pointer */
    buf[0] = 0;
    i2c_send(i2c, addr, buf, 1);
    /* read back current time registers */
    i2c_recv(i2c, addr, buf, 7);

    return rtc_parse(buf);
}

#endif /* DSRTCCOMMON_H */
