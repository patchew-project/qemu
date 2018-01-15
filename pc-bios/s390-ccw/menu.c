/*
 * QEMU S390 Interactive Boot Menu
 *
 * Copyright 2017 IBM Corp.
 * Author: Collin L. Walling <walling@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "menu.h"
#include "s390-ccw.h"

static uint8_t flags;
static uint64_t timeout;

static void zipl_println(const char *data, size_t len)
{
    char buf[len + 1];

    ebcdic_to_ascii(data, buf, len);
    buf[len] = '\n';
    buf[len + 1] = '\0';

    sclp_print(buf);
}

int menu_get_zipl_boot_index(const void *stage2, ZiplParms zipl_parms)
{
    const char *data = stage2 + zipl_parms.menu_start;
    size_t len;
    int ct;

    if (flags & BOOT_MENU_FLAG_ZIPL_OPTS) {
        if (zipl_parms.flag) {
            timeout = zipl_parms.timeout;
        } else {
            return 0; /* Boot default */
        }
    }

    /* Print and count all menu items, including the banner */
    for (ct = 0; *data; ct++) {
        len = strlen(data);
        zipl_println(data, len);
        data += len + 1;

        if (ct < 2) {
            sclp_print("\n");
        }
    }

    sclp_print("\n");

    return 0; /* return user input next patch */
}

void menu_set_parms(uint8_t boot_menu_flag, uint16_t boot_menu_timeout)
{
    flags = boot_menu_flag;
    timeout = boot_menu_timeout;
}

int menu_check_flags(uint8_t check_flags)
{
    return flags & check_flags;
}
