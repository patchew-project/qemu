/*
 * SD card bus QMP debugging interface (for QTesting).
 *
 * Copyright (c) 2017 ?
 *
 * Author:
 *  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "hw/sd/sd.h"
#include "qmp-commands.h"

SDBusCommandResponse *qmp_x_debug_sdbus_command(const char *qom_path,
                                                uint8_t command,
                                                bool has_arg, uint64_t arg,
                                                bool has_crc, uint16_t crc,
                                                Error **errp)
{
    uint8_t response[16 + 1];
    SDBusCommandResponse *res;
    bool ambiguous = false;
    Object *obj;
    SDBus *sdbus;
    int sz;

    obj = object_resolve_path(qom_path, &ambiguous);
    if (obj == NULL) {
        if (ambiguous) {
            error_setg(errp, "Path '%s' is ambiguous", qom_path);
        } else {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", qom_path);
        }
        return NULL;
    }
    sdbus = (SDBus *)object_dynamic_cast(obj, TYPE_SD_BUS);
    if (sdbus == NULL) {
        error_set(errp, ERROR_CLASS_GENERIC_ERROR,
                  "Device '%s' not a sd-bus", qom_path);
        return NULL;
    }

    res = g_new0(SDBusCommandResponse, 1);
    sz = sdbus_do_command(sdbus,
                          &(SDRequest){ command, arg, has_crc ? crc : -1 },
                          response);
    if (sz > 0) {
        res->has_base64 = true;
        res->base64 = g_base64_encode(response, sz);
    }

    return res;
}
