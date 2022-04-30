/*
 * Hosted file support for semihosting syscalls.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOSTING_GUESTFD_H
#define SEMIHOSTING_GUESTFD_H

typedef enum GuestFDType {
    GuestFDUnused = 0,
    GuestFDHost = 1,
    GuestFDGDB = 2,
    GuestFDStatic = 3,
} GuestFDType;

/*
 * Guest file descriptors are integer indexes into an array of
 * these structures (we will dynamically resize as necessary).
 */
typedef struct GuestFD {
    GuestFDType type;
    union {
        int hostfd;
        struct {
            const uint8_t *data;
            size_t len;
            size_t off;
        } staticfile;
    };
} GuestFD;

int alloc_guestfd(void);
void dealloc_guestfd(int guestfd);
GuestFD *get_guestfd(int guestfd);

void associate_guestfd(int guestfd, int hostfd);
void staticfile_guestfd(int guestfd, const uint8_t *data, size_t len);

#endif /* SEMIHOSTING_GUESTFD_H */
