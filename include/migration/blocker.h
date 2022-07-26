/*
 * QEMU migration blockers
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

#ifndef MIGRATION_BLOCKER_H
#define MIGRATION_BLOCKER_H

#include "qapi/qapi-types-migration.h"

#define MIG_MODE_ALL MIG_MODE__MAX

/**
 * @migrate_add_blocker - prevent all modes of migration from proceeding
 *
 * @reasonp - address of an error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @returns - 0 on success, -EBUSY/-EACCES on failure, with errp set.
 *
 * *@reasonp is freed and set to NULL if failure is returned.
 * On success, the caller must not free *@reasonp before the blocker is removed.
 */
int migrate_add_blocker(Error **reasonp, Error **errp);

/**
 * @migrate_add_blockers - prevent migration for specified modes from proceeding
 *
 * @reasonp - address of an error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @mode - one or more migration modes to be blocked.  The list is terminated
 *         by -1 or MIG_MODE_ALL.  For the latter, all modes are blocked.
 *
 * @returns - 0 on success, -EBUSY/-EACCES on failure, with errp set.
 *
 * *@reasonp is freed and set to NULL if failure is returned.
 * On success, the caller must not free *@reasonp before the blocker is removed.
 */
int migrate_add_blockers(Error **reasonp, Error **errp, MigMode mode, ...);

/**
 * @migrate_add_blocker_always - permanently prevent migration for specified
 *  modes from proceeding.  The blocker cannot be deleted.
 *
 * @msg - text of error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @mode - one or more migration modes to be blocked.  The list is terminated
 *         by -1 or MIG_MODE_ALL.  For the latter, all modes are blocked.
 *
 * @returns - 0 on success, -EBUSY/-EACCES on failure, with errp set.
 */
int
migrate_add_blocker_always(const char *msg, Error **errp, MigMode mode, ...);

/**
 * @migrate_add_blocker_internal - prevent migration from proceeding without
 *                                 only-migrate implications, for all modes
 *
 * @reasonp - address of an error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @returns - 0 on success, -EBUSY on failure, with errp set.
 *
 * Some of the migration blockers can be temporary (e.g., for a few seconds),
 * so it shouldn't need to conflict with "-only-migratable".  For those cases,
 * we can call this function rather than @migrate_add_blocker().
 *
 * *@reasonp is freed and set to NULL if failure is returned.
 * On success, the caller must not free *@reasonp before the blocker is removed.
 */
int migrate_add_blocker_internal(Error **reasonp, Error **errp);

/**
 * @migrate_del_blocker - remove a migration blocker for all modes and free it.
 *
 * @reasonp - address of the error blocking migration
 *
 * This function frees *@reasonp and sets it to NULL.
 */
void migrate_del_blocker(Error **reasonp);

/**
 * @migrate_remove_blocker - remove a migration blocker for all modes.
 *
 * @reason - the error blocking migration
 *
 */
void migrate_remove_blocker(Error *reason);

#endif
