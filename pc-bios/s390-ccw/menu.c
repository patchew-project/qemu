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

static uint8_t flags;
static uint64_t timeout;

int menu_get_zipl_boot_index(const void *stage2, int offset)
{
    return 0; /* implemented next patch */
}

void menu_set_parms(uint8_t boot_menu_flag, uint32_t boot_menu_timeout)
{
    flags = boot_menu_flag;
    timeout = boot_menu_timeout;
}

int menu_check_flags(uint8_t check_flags)
{
    return flags & check_flags;
}
