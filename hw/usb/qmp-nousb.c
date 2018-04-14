/*
 * QMP Target options - Commands handled based on a target config
 *                      versus a host config
 *
 * Copyright (c) 2018 Alexander Kappner <agk@godking.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qmp/qerror.h"

UsbDeviceInfoList *qmp_query_usbhost(Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "usb");
    return NULL;
};
