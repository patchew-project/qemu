/*
 * RP2040 "not yet implemented" diagnostics
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "qemu/log.h"

void rp2040_log_nyi(const char *component, const char *feature,
                    const char *detail)
{
    if (g_str_has_prefix(component, "rp2040.")) {
        component += strlen("rp2040.");
    }

    qemu_log_mask(LOG_UNIMP, "Not yet implemented: rp2040.%s: %s%s%s\n",
                  component, feature, detail ? ": " : "",
                  detail ? detail : "");
}

void rp2040_log_unimplemented_read(const char *component, unsigned size,
                                   uint64_t addr, uint64_t offset,
                                   uint64_t value)
{
    char detail[128];

    snprintf(detail, sizeof(detail),
             "size %u, addr 0x%08" PRIx64 ", offset 0x%04" PRIx64
             " -> 0x%0*" PRIx64,
             size, addr, offset, size << 1, value);
    rp2040_log_nyi(component, "unimplemented read", detail);
}

void rp2040_log_unimplemented_write(const char *component, unsigned size,
                                    uint64_t addr, uint64_t offset,
                                    uint64_t value)
{
    char detail[128];

    snprintf(detail, sizeof(detail),
             "size %u, addr 0x%08" PRIx64 ", offset 0x%04" PRIx64
             ", value 0x%0*" PRIx64,
             size, addr, offset, size << 1, value);
    rp2040_log_nyi(component, "unimplemented write", detail);
}
