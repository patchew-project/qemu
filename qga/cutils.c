/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "cutils.h"

#include "qapi/error.h"

/**
 * qga_open_cloexec:
 * @name: the pathname to open
 * @flags: as in open()
 * @mode: as in open()
 * @errp: pointer to Error*, or NULL
 *
 * A wrapper for open() function which sets O_CLOEXEC.
 *
 * On error, -1 is returned and @errp is set.
 */
int qga_open_cloexec(const char *name, int flags, mode_t mode, Error **errp)
{
    int ret;

#ifdef O_CLOEXEC
    ret = open(name, flags | O_CLOEXEC, mode);
#else
    ret = open(name, flags, mode);
    if (ret >= 0) {
        qemu_set_cloexec(ret);
    }
#endif
    if (ret == -1) {
        error_setg_errno(errp, errno, "Failed to open file '%s'", name);
    }

    return ret;
}
