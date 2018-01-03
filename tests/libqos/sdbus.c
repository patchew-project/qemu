/*
 * QTest SD/MMC Bus driver
 *
 * Copyright (c) 2017 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "libqos/sdbus.h"
#include "libqtest.h"
#include "qemu-common.h"

static bool verbose;
#define DPRINTF(fmt, ...)                           \
    do {                                            \
        if (verbose) {                              \
            fprintf(stderr, fmt, ## __VA_ARGS__);   \
        }                                           \
    } while (0)

static ssize_t do_cmd(SDBusAdapter *adapter, enum NCmd cmd, uint32_t arg,
                      uint8_t **response, bool is_app_cmd)
{
    const char *s_cmd = is_app_cmd ? "ACMD" : "CMD";
    ssize_t sz;

    verbose = !!getenv("V");
    if (verbose && !is_app_cmd && (cmd == 55)) {
        verbose = false;
    }

    DPRINTF("-> %s%02u (0x%08x)\n", s_cmd, cmd, arg);
    sz = adapter->do_command(adapter, cmd, arg, response);
    if (response) {
        if (sz < 0) {
            DPRINTF("<- %s%02u (len: %ld)\n", s_cmd, cmd, sz);
        } else if (verbose) {
            char *pfx = g_strdup_printf("<- %s%02u (len: %ld)", s_cmd, cmd, sz);

            qemu_hexdump((const char *)*response, stderr, pfx, sz);
            g_free(pfx);
        }
    } else {
        DPRINTF("<- %s%02u\n", s_cmd, cmd);
    }

    return sz;
}

ssize_t sdbus_do_cmd(SDBusAdapter *adapter, enum NCmd cmd, uint32_t arg,
                     uint8_t **response)
{
    return do_cmd(adapter, cmd, arg, response, false);
}

ssize_t sdbus_do_acmd(SDBusAdapter *adapter, enum ACmd acmd, uint32_t arg,
                      uint16_t address, uint8_t **response)
{
    do_cmd(adapter, 55, address << 16, NULL, false);
    // TODO check rv?

    return do_cmd(adapter, acmd, arg, response, true);
}

void sdbus_write_byte(SDBusAdapter *adapter, uint8_t value)
{
    adapter->write_byte(adapter, value);
}

uint8_t sdbus_read_byte(SDBusAdapter *adapter)
{
    return adapter->read_byte(adapter);
}
