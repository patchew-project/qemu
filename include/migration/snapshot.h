/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_SAVEVM_H
#define QEMU_MIGRATION_SAVEVM_H

int save_snapshot(const char *name, Error **errp);
int load_snapshot(const char *name, Error **errp);

#endif
