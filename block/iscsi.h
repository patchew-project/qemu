/*
 * QEMU Block driver for iSCSI images
 *
 * Copyright (c) 2010-2011 Ronnie Sahlberg <ronniesahlberg@gmail.com>
 * Copyright (c) 2012-2016 Peter Lieven <pl@kamp.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_BLOCK_ISCSI_H
#define QEMU_BLOCK_ISCSI_H

/*
 * These properties were historically set using the '-iscsi' arg,
 * but are not settable directly against the blockdev with -drive
 * or equivalent
 */

#define ISCSI_OPT_USER                                          \
    {                                                           \
        .name = "user",                                         \
        .type = QEMU_OPT_STRING,                                \
        .help = "username for CHAP authentication to target",   \
    }

#define ISCSI_OPT_PASSWORD                                      \
    {                                                           \
        .name = "password",                                     \
        .type = QEMU_OPT_STRING,                                \
        .help = "password for CHAP authentication to target",   \
    }

#define ISCSI_OPT_PASSWORD_SECRET                               \
    {                                                           \
        .name = "password-secret",                              \
        .type = QEMU_OPT_STRING,                                \
        .help = "ID of the secret providing password for CHAP " \
        "authentication to target",                             \
    }

#define ISCSI_OPT_HEADER_DIGEST                                 \
    {                                                           \
        .name = "header-digest",                                \
        .type = QEMU_OPT_STRING,                                \
        .help = "HeaderDigest setting. "                        \
                "{CRC32C|CRC32C-NONE|NONE-CRC32C|NONE}",        \
    }

#define ISCSI_OPT_INITIATOR_NAME                                \
    {                                                           \
        .name = "initiator-name",                               \
        .type = QEMU_OPT_STRING,                                \
        .help = "Initiator iqn name to use when connecting",    \
    }

#define ISCSI_OPT_TIMEOUT                                               \
    {                                                                   \
        .name = "timeout",                                              \
        .type = QEMU_OPT_NUMBER,                                        \
        .help = "Request timeout in seconds (default 0 = no timeout)",  \
    }

#endif /* QEMU_BLOCK_ISCSI_H */
