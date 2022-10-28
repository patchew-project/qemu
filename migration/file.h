#ifndef QEMU_MIGRATION_FILE_H
#define QEMU_MIGRATION_FILE_H

void file_start_outgoing_migration(MigrationState *s,
                                   const char *filename,
                                   Error **errp);

void file_start_incoming_migration(const char *fname, Error **errp);
#endif

