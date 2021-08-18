#ifndef MONITOR_QDEV_H
#define MONITOR_QDEV_H

/*** monitor commands ***/
#include "hw/qdev-core.h"

void hmp_info_qtree(Monitor *mon, const QDict *qdict);
void hmp_info_qdm(Monitor *mon, const QDict *qdict);
void qmp_device_add(QDict *qdict, QObject **ret_data, Error **errp);

int qdev_device_help(QemuOpts *opts);
DeviceClass *qdev_get_device_class(const char **driver, Error **errp);
DeviceState *qdev_device_add(QemuOpts *opts, Error **errp);
void qdev_set_id(DeviceState *dev, const char *id);

#endif
