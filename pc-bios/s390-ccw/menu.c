/*
 * QEMU S390 Boot Menu
 *
 * Copyright 2017 IBM Corp.
 * Author: Collin L. Walling <walling@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"

static bool enabled;
static uint64_t timeout;

static int menu_read_index(uint64_t timeout)
{
    char *inp;
    int len;
    int i;

    len = sclp_read(&inp, timeout);

    if (len == 0) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        if (!isdigit(inp[i])) {
            return -1;
        }
    }

    return atoi(inp);
}

static int menu_get_boot_index(int entries)
{
    char tmp[4];
    int boot_index;

    /* Prompt User */
    if (timeout > 0) {
        sclp_print("Please choose (default will boot in ");
        sclp_print(itostr(timeout / 1000, tmp));
        sclp_print(" seconds):\n");
    } else {
        sclp_print("Please choose:\n");
    }

    /* Get Menu Choice */
    boot_index = menu_read_index(timeout);

    while (boot_index < 0 || boot_index >= entries) {
        sclp_print("\nError: undefined configuration"
                   "\nPlease choose:\n");
        boot_index = menu_read_index(0);
    }

    sclp_print("\nBooting entry #");
    sclp_print(itostr(boot_index, tmp));

    return boot_index;
}

static void menu_println(const char *data, size_t len)
{
    char buf[len + 1];

    ebcdic_to_ascii(data, buf, len);
    buf[len] = '\n';
    buf[len + 1] = '\0';

    sclp_print(buf);
}

int menu_get_zipl_boot_index(const char *data)
{
    size_t len;
    int i;

    /* Print all menu items, including the banner */
    for (i = 0; *data != '\0'; i++) {
        len = strlen(data);
        menu_println(data, len);
        if (i < 2) {
            sclp_print("\n");
        }
        data += len + 1;
    }

    sclp_print("\n");

    return menu_get_boot_index(i - 1);
}

int menu_get_enum_boot_index(int entries)
{
    char tmp[4];

    sclp_print("s390x Enumerated Boot Menu.\n\n");

    sclp_print(itostr(entries, tmp));
    sclp_print(" entries detected. Select from boot index 0 to ");
    sclp_print(itostr(entries - 1, tmp));
    sclp_print(".\n\n");

    return menu_get_boot_index(entries);
}

void menu_enable(uint16_t boot_menu_timeout)
{
    timeout = boot_menu_timeout;
    enabled = true;
}

bool menu_is_enabled(void)
{
    return enabled;
}
