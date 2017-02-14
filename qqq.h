/*
 * This work is licensed under the terms of the GNU GPL
 * version 2. Seethe COPYING file in the top-level directory.
 *
 * A module for pacing the rate of advance of the computer
 * clock in reference to an external simulation clock. The
 * basic approach used here is adapted from QBox from Green
 * Socs. The mode of operation is as follows:
 *
 * The simulator uses pipes to exchange time advance data.
 * The external simulator starts the exchange by forking a
 * QEMU process and passing is descriptors for a read and
 * write pipe. Then the external simulator writes an integer
 * (native endian) to the pipe to indicate the number of
 * microseconds that QEMU should advance. QEMU advances its
 * virtual clock by this amount and writes to its write pipe
 * the actual number of microseconds that have advanced.
 * This process continues until a pipe on either side is
 * closed, which will either cause QEMU to exit (if the
 * external simulator closes a pipe) or raise SIGPIPE in the
 * external simulator (if QEMU closes a pipe).
 *
 * Authors:
 *   James Nutaro <nutaro@gmail.com>
 *
 */
#ifndef QQQ_H
#define QQQ_H

#include "qemu/osdep.h"
#include "qemu-options.h"

void qqq_sync(void);
bool qqq_enabled(void);
void setup_qqq(QemuOpts *opts);

#endif
