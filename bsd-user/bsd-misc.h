/*
 *  miscellaneous BSD system call shims
 *
 *  Copyright (c) 2013 Stacey D. Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BSD_MISC_H
#define BSD_MISC_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/uuid.h>

#include "qemu-bsd.h"

/* quotactl(2) */
static inline abi_long do_bsd_quotactl(abi_ulong path, abi_long cmd,
        __unused abi_ulong target_addr)
{
    qemu_log("qemu: Unsupported syscall quotactl()\n");
    return -TARGET_ENOSYS;
}

/* reboot(2) */
static inline abi_long do_bsd_reboot(abi_long how)
{
    qemu_log("qemu: Unsupported syscall reboot()\n");
    return -TARGET_ENOSYS;
}

/* uuidgen(2) */
static inline abi_long do_bsd_uuidgen(abi_ulong target_addr, int count)
{
    int i;
    abi_long ret;
    struct uuid *host_uuid;

    if (count < 1 || count > 2048) {
        return -TARGET_EINVAL;
    }

    host_uuid = g_malloc(count * sizeof(struct uuid));

    if (host_uuid == NULL) {
        return -TARGET_ENOMEM;
    }

    ret = get_errno(uuidgen(host_uuid, count));
    if (is_error(ret)) {
        goto out;
    }
    for (i = 0; i < count; i++) {
        ret = host_to_target_uuid(target_addr +
            (abi_ulong)(sizeof(struct target_uuid) * i), &host_uuid[i]);
        if (is_error(ret)) {
            goto out;
        }
    }

out:
    g_free(host_uuid);
    return ret;
}

/*
 * System V Semaphores
 */

/* semget(2) */
static inline abi_long do_bsd_semget(abi_long key, int nsems,
        int target_flags)
{
    return get_errno(semget(key, nsems,
                target_to_host_bitmask(target_flags, ipc_flags_tbl)));
}

/* getdtablesize(2) */
static inline abi_long do_bsd_getdtablesize(void)
{
    return get_errno(getdtablesize());
}

#endif /* BSD_MISC_H */
