#include "qemu/osdep.h"
#include "channel.h"
#include "io/channel-file.h"
#include "file.h"
#include "qemu/error-report.h"


void file_start_outgoing_migration(MigrationState *s, const char *fname, Error **errp)
{
	QIOChannelFile *ioc;

	ioc = qio_channel_file_new_path(fname, O_CREAT|O_TRUNC|O_WRONLY, 0660, errp);
	if (!ioc) {
		error_report("Error creating a channel");
		return;
	}

	qio_channel_set_name(QIO_CHANNEL(ioc), "migration-file-outgoing");
	migration_channel_connect(s, QIO_CHANNEL(ioc), NULL, NULL);
	object_unref(OBJECT(ioc));
}


