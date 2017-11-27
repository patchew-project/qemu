/*
 * SCLP ASCII access driver
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"
#include "sclp.h"

#define KEYCODE_NO_INP '\0'
#define KEYCODE_ARROWS '\033'
#define KEYCODE_BACKSP '\177'
#define KEYCODE_ENTER  '\r'

long write(int fd, const void *str, size_t len);

static char _sccb[PAGE_SIZE] __attribute__((__aligned__(4096)));

const unsigned char ebc2asc[256] =
      /* 0123456789abcdef0123456789abcdef */
        "................................" /* 1F */
        "................................" /* 3F */
        " ...........<(+|&.........!$*);." /* 5F first.chr.here.is.real.space */
        "-/.........,%_>?.........`:#@'=\""/* 7F */
        ".abcdefghi.......jklmnopqr......" /* 9F */
        "..stuvwxyz......................" /* BF */
        ".ABCDEFGHI.......JKLMNOPQR......" /* DF */
        "..STUVWXYZ......0123456789......";/* FF */

/* Perform service call. Return 0 on success, non-zero otherwise. */
static int sclp_service_call(unsigned int command, void *sccb)
{
        int cc;

        asm volatile(
                "       .insn   rre,0xb2200000,%1,%2\n"  /* servc %1,%2 */
                "       ipm     %0\n"
                "       srl     %0,28"
                : "=&d" (cc) : "d" (command), "a" (__pa(sccb))
                : "cc", "memory");
        consume_sclp_int();
        if (cc == 3)
                return -EIO;
        if (cc == 2)
                return -EBUSY;
        return 0;
}

static void sclp_set_write_mask(void)
{
    WriteEventMask *sccb = (void *)_sccb;

    sccb->h.length = sizeof(WriteEventMask);
    sccb->mask_length = sizeof(unsigned int);
    sccb->receive_mask = SCLP_EVENT_MASK_MSG_ASCII;
    sccb->cp_receive_mask = SCLP_EVENT_MASK_MSG_ASCII;
    sccb->send_mask = SCLP_EVENT_MASK_MSG_ASCII;
    sccb->cp_send_mask = SCLP_EVENT_MASK_MSG_ASCII;

    sclp_service_call(SCLP_CMD_WRITE_EVENT_MASK, sccb);
}

void sclp_setup(void)
{
    sclp_set_write_mask();
}

static int _strlen(const char *str)
{
    int i;
    for (i = 0; *str; i++)
        str++;
    return i;
}

long write(int fd, const void *str, size_t len)
{
    WriteEventData *sccb = (void *)_sccb;
    const char *p = str;
    size_t data_len = 0;
    size_t i;

    if (fd != 1 && fd != 2) {
        return -EIO;
    }

    for (i = 0; i < len; i++) {
        if ((data_len + 1) >= SCCB_DATA_LEN) {
            /* We would overflow the sccb buffer, abort early */
            len = i;
            break;
        }

        if (*p == '\n') {
            /* Terminal emulators might need \r\n, so generate it */
            sccb->data[data_len++] = '\r';
        }

        sccb->data[data_len++] = *p;
        p++;
    }

    sccb->h.length = sizeof(WriteEventData) + data_len;
    sccb->h.function_code = SCLP_FC_NORMAL_WRITE;
    sccb->ebh.length = sizeof(EventBufferHeader) + data_len;
    sccb->ebh.type = SCLP_EVENT_ASCII_CONSOLE_DATA;
    sccb->ebh.flags = 0;

    sclp_service_call(SCLP_CMD_WRITE_EVENT_DATA, sccb);

    return len;
}

void sclp_print(const char *str)
{
    write(1, str, _strlen(str));
}

void sclp_get_loadparm_ascii(char *loadparm)
{

    ReadInfo *sccb = (void *)_sccb;

    memset((char *)_sccb, 0, sizeof(ReadInfo));
    sccb->h.length = sizeof(ReadInfo);
    if (!sclp_service_call(SCLP_CMDW_READ_SCP_INFO, sccb)) {
        ebcdic_to_ascii((char *) sccb->loadparm, loadparm, 8);
    }
}

static void read(char **str)
{
    ReadEventData *sccb = (void *)_sccb;
    *str = (char *)(&sccb->ebh) + 7;

    sccb->h.length = SCCB_SIZE;
    sccb->h.function_code = SCLP_UNCONDITIONAL_READ;
    sccb->ebh.length = sizeof(EventBufferHeader);
    sccb->ebh.type = SCLP_EVENT_ASCII_CONSOLE_DATA;
    sccb->ebh.flags = 0;

    sclp_service_call(SCLP_CMD_READ_EVENT_DATA, sccb);
}

static inline void enable_clock_int(void)
{
    uint64_t tmp = 0;

    asm volatile(
        "stctg      0,0,%0\n"
        "oi         6+%0, 0x8\n"
        "lctlg      0,0,%0"
        : : "Q" (tmp)
    );
}

static inline void disable_clock_int(void)
{
    uint64_t tmp = 0;

    asm volatile(
        "stctg      0,0,%0\n"
        "ni         6+%0, 0xf7\n"
        "lctlg      0,0,%0"
        : : "Q" (tmp)
    );
}

static inline bool check_clock_int(void)
{
    uint16_t code;

    consume_sclp_int();

    asm volatile(
        "lh         1, 0x86(0,0)\n"
        "sth        1, %0"
        : "=r" (code)
    );

    return code == 0x1004;
}

static inline void set_clock_comparator(uint64_t time)
{
    asm volatile("sckc %0" : : "Q" (time));
}

/* sclp_read
 *
 * Reads user input from the sclp console into a buffer. The buffer
 * is set and the length is returned only if the enter key was detected.
 *
 * @param buf_ptr - a pointer to the buffer
 *
 * @param timeout - time (in milliseconds) to wait before abruptly
 *                  ending user-input read loop. if 0, then loop
 *                  until an enter key is detected
 *
 * @return - the length of the data in the buffer
 */
int sclp_read(char **buf_ptr, uint64_t timeout)
{
    char *inp = NULL;
    char buf[255];
    uint8_t len = 0;
    uint64_t seconds;

    memset(buf, 0, sizeof(buf));

    if (timeout) {
        seconds = get_second() + timeout / 1000;
        set_clock_comparator((seconds * 1000000) << 12);
        enable_clock_int();
    }

    while (!check_clock_int()) {
        read(&inp);

        switch (inp[0]) {
        case KEYCODE_NO_INP:
        case KEYCODE_ARROWS:
            continue;
        case KEYCODE_BACKSP:
            if (len > 0) {
                len--;

                /* Remove last character */
                buf[len] = ' ';
                write(1, "\r", 1);
                write(1, buf, len + 1);

                /* Reset cursor */
                buf[len] = 0;
                write(1, "\r", 1);
                write(1, buf, len);
            }
            continue;
        case KEYCODE_ENTER:
            disable_clock_int();

            *buf_ptr = buf;
            return len;
        }

        /* Echo input and add to buffer */
        if (len < sizeof(buf)) {
            buf[len] = inp[0];
            len++;
            write(1, inp, 1);
        }
    }

    disable_clock_int();
    return 0;
}
