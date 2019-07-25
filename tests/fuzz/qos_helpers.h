#ifndef QOS_HELPERS_H
#define QOS_HELPERS_H

#include "qemu/osdep.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "libqtest.h"
#include "qapi/qmp/qlist.h"
#include "libqos/qgraph_internal.h"


void qos_set_machines_devices_available(void);
void *allocate_objects(QTestState *qts, char **path, QGuestAllocator **p_alloc);
void walk_path(QOSGraphNode *orig_path, int len);
void qos_build_main_args(void);
#endif
