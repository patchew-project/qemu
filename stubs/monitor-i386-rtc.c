/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-i386.h"

void qmp_rtc_reset_reinjection(Error **errp)
{
    error_setg(errp,
               "rtc-reset-injection is only available for x86 machines with RTC");
}
