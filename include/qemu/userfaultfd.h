/*
 * Linux UFFD-WP support
 *
 * Copyright Virtuozzo GmbH, 2020
 *
 * Authors:
 *  Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef USERFAULTFD_H
#define USERFAULTFD_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include <linux/userfaultfd.h>

int uffd_create_fd(void);
void uffd_close_fd(int uffd);
int uffd_register_memory(int uffd, hwaddr start, hwaddr length,
        bool track_missing, bool track_wp);
int uffd_unregister_memory(int uffd, hwaddr start, hwaddr length);
int uffd_protect_memory(int uffd, hwaddr start, hwaddr length, bool wp);
int uffd_read_events(int uffd, struct uffd_msg *msgs, int count);
bool uffd_poll_events(int uffd, int tmo);

#endif /* USERFAULTFD_H */
