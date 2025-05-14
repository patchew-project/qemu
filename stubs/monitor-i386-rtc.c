/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-i386.h"

void qmp_rtc_reset_reinjection(Error **errp)
{
    /*
     * Use of this command is only applicable for x86 machines with an RTC,
     * and on other machines will silently return without performing any
     * action.
     */
}
