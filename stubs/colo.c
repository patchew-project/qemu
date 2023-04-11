#include "qemu/osdep.h"
#include "qemu/notify.h"
#include "net/colo-compare.h"
#include "migration/colo.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-migration.h"

void colo_compare_cleanup(void)
{
    abort();
}

void colo_shutdown(void)
{
    abort();
}

void *colo_process_incoming_thread(void *opaque)
{
    abort();
}

void colo_checkpoint_notify(void *opaque)
{
    abort();
}

void migrate_start_colo_process(MigrationState *s)
{
    abort();
}

bool migration_in_colo_state(void)
{
    return false;
}

bool migration_incoming_in_colo_state(void)
{
    return false;
}

void qmp_x_colo_lost_heartbeat(Error **errp)
{
    error_setg(errp, "COLO support is not built in");
}
