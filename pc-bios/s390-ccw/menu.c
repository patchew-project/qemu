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

#define KEYCODE_NO_INP '\0'
#define KEYCODE_ESCAPE '\033'
#define KEYCODE_BACKSP '\177'
#define KEYCODE_ENTER  '\r'

/* Offsets from zipl fields to zipl banner start */
#define ZIPL_TIMEOUT_OFFSET 138
#define ZIPL_FLAG_OFFSET    140

#define TOD_CLOCK_SECOND      0xf4240000

static uint8_t flags;
static uint64_t timeout;

static inline void enable_clock_int(void)
{
    uint64_t tmp = 0;

    asm volatile(
        "stctg      0,0,%0\n"
        "oi         6+%0, 0x8\n"
        "lctlg      0,0,%0"
        : : "Q" (tmp) : "memory"
    );
}

static inline void disable_clock_int(void)
{
    uint64_t tmp = 0;

    asm volatile(
        "stctg      0,0,%0\n"
        "ni         6+%0, 0xf7\n"
        "lctlg      0,0,%0"
        : : "Q" (tmp) : "memory"
    );
}

static inline void set_clock_comparator(uint64_t time)
{
    asm volatile("sckc %0" : : "Q" (time));
}

static inline bool check_clock_int(void)
{
    uint16_t *code = (uint16_t *)0x86; /* low-core external interrupt code */

    consume_sclp_int();

    return *code == 0x1004;
}

static void clear_pending_irqs(void)
{
    uint64_t time = 50 * TOD_CLOCK_SECOND / 0x3e8;

    sclp_clear_write_mask();

    set_clock_comparator(get_clock() + time);
    enable_clock_int();
    consume_sclp_int();
    disable_clock_int();

    sclp_setup(); /* re-enable write mask */
}

static int read_prompt(char *buf, size_t len)
{
    char inp[2] = {};
    uint8_t idx = 0;
    uint64_t time;

    if (timeout) {
        time = get_clock() + timeout * TOD_CLOCK_SECOND;
        set_clock_comparator(time);
        enable_clock_int();
        timeout = 0;
    }

    while (!check_clock_int()) {

        sclp_read(inp, 1); /* Process only one character at a time */

        switch (inp[0]) {
        case KEYCODE_NO_INP:
        case KEYCODE_ESCAPE:
            continue;
        case KEYCODE_BACKSP:
            if (idx > 0) {
                buf[--idx] = 0;
                sclp_print("\b \b");
            }
            continue;
        case KEYCODE_ENTER:
            disable_clock_int();
            return idx;
        default:
            /* Echo input and add to buffer */
            if (idx < len) {
                buf[idx++] = inp[0];
                sclp_print(inp);
            }
        }
    }

    disable_clock_int();
    *buf = 0;

    return 0;
}

static int get_index(void)
{
    char buf[10];
    int len;
    int i;

    memset(buf, 0, sizeof(buf));

    len = read_prompt(buf, sizeof(buf));

    /* If no input, boot default */
    if (len == 0) {
        return 0;
    }

    /* Check for erroneous input */
    for (i = 0; i < len; i++) {
        if (!isdigit(buf[i])) {
            return -1;
        }
    }

    return atoui(buf);
}

static void boot_menu_prompt(bool retry)
{
    char tmp[6];

    if (retry) {
        sclp_print("\nError: undefined configuration"
                   "\nPlease choose:\n");
    } else if (timeout > 0) {
        sclp_print("Please choose (default will boot in ");
        sclp_print(itostr(timeout, tmp, sizeof(tmp)));
        sclp_print(" seconds):\n");
    } else {
        sclp_print("Please choose:\n");
    }
}

static int get_boot_index(int entries)
{
    int boot_index;
    bool retry = false;
    char tmp[5];

    do {
        boot_menu_prompt(retry);
        boot_index = get_index();
        retry = true;
    } while (boot_index < 0 || boot_index >= entries);

    sclp_print("\nBooting entry #");
    sclp_print(itostr(boot_index, tmp, sizeof(tmp)));

    clear_pending_irqs();

    return boot_index;
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

int menu_get_enum_boot_index(int entries)
{
    char tmp[4];

    sclp_print("s390x Enumerated Boot Menu.\n\n");

    sclp_print(itostr(entries, tmp, sizeof(tmp)));
    sclp_print(" entries detected. Select from boot index 0 to ");
    sclp_print(itostr(entries - 1, tmp, sizeof(tmp)));
    sclp_print(".\n\n");

    return get_boot_index(entries);
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
