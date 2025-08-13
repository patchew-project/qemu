/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "migration/qemu-file.h"


int qemu_file_get_fd(QEMUFile *f)
{
    return -1;
}

int qemu_file_put_fd(QEMUFile *f, int fd)
{
    return -1;
}
