/*
 * QEMU Guest Agent BSD-specific command implementations
 *
 * Copyright (c) Virtuozzo International GmbH.
 *
 * Authors:
 *  Alexander Ivanov  <alexander.ivanov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qga-qapi-commands.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "commands-common.h"

#if defined(CONFIG_FSFREEZE) || defined(CONFIG_FSTRIM)
bool build_fs_mount_list(FsMountList *mounts, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return false;
}
#endif

#if defined(CONFIG_FSFREEZE)
int64_t qmp_guest_fsfreeze_do_freeze_list(bool has_mountpoints,
                                          strList *mountpoints,
                                          FsMountList mounts,
                                          Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return 0;
}

int qmp_guest_fsfreeze_do_thaw(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return 0;
}
#endif

GuestDiskInfoList *qmp_guest_get_disks(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestDiskStatsInfoList *qmp_guest_get_diskstats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestCpuStatsList *qmp_guest_get_cpustats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

void qmp_guest_suspend_disk(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_ram(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

int64_t qmp_guest_set_vcpus(GuestLogicalProcessorList *vcpus, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return -1;
}

void qmp_guest_set_user_password(const char *username,
                                 const char *password,
                                 bool crypted,
                                 Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

GuestMemoryBlockList *qmp_guest_get_memory_blocks(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestMemoryBlockResponseList *
qmp_guest_set_memory_blocks(GuestMemoryBlockList *mem_blks, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestMemoryBlockInfo *qmp_guest_get_memory_block_info(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
