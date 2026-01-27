#ifndef QEMU_MIGRATION_NETPASS_H
#define QEMU_MIGRATION_NETPASS_H

#include "qemu/typedefs.h"
#include "qom/object.h"

#define TYPE_FILTER_NETPASS "filter-netpass"
OBJECT_DECLARE_SIMPLE_TYPE(NetPassState, FILTER_NETPASS)

int migration_netpass_setup(Error **errp);
void migration_netpass_activate(void);
void migration_netpass_cleanup(void);

#endif
