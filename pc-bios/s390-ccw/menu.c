/*
 * QEMU S390 Interactive Boot Menu
 *
 * Copyright 2018 IBM Corp.
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

/* Offsets from zipl fields to zipl banner start */
#define ZIPL_TIMEOUT_OFFSET 138
#define ZIPL_FLAG_OFFSET    140

static int get_boot_index(int entries)
{
    return 0; /* Implemented next patch */
}

static void zipl_println(const char *data, size_t len)
{
    char buf[len + 2];

    ebcdic_to_ascii(data, buf, len);
    buf[len] = '\n';
    buf[len + 1] = '\0';

    sclp_print(buf);
}

int menu_get_zipl_boot_index(const void *stage2, int offset)
{
    const char *data = stage2 + offset;
    uint16_t zipl_flag = *(uint16_t *)(data - ZIPL_FLAG_OFFSET);
    uint16_t zipl_timeout = *(uint16_t *)(data - ZIPL_TIMEOUT_OFFSET);
    size_t len;
    int ct;

    if (flags & BOOT_MENU_FLAG_ZIPL_OPTS) {
        if (!zipl_flag) {
            return 0; /* Boot default */
        }
        timeout = zipl_timeout;
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

    return get_boot_index(ct - 1);
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
