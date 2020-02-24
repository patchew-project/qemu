#ifndef MONITOR_QDEV_H
#define MONITOR_QDEV_H

#include "hw/proxy/qemu-proxy.h"

/*** monitor commands ***/

void hmp_info_qtree(Monitor *mon, const QDict *qdict);
void hmp_info_qdm(Monitor *mon, const QDict *qdict);
void qmp_device_add(QDict *qdict, QObject **ret_data, Error **errp);

DeviceState *qdev_remote_add(QemuOpts *opts, Error **errp);
void qdev_proxy_fire(void);

int qdev_device_help(QemuOpts *opts);
DeviceState *qdev_proxy_add(const char *rid, const char *id, char *bus,
                            char *command, char *exec_name, int socket,
                            bool managed, Error **errp);

struct remote_process {
    int rid;
    int remote_pid;
    unsigned int type;
    int socket;
    char *command;
    char *exec;
    QemuOpts *opts;

    QLIST_ENTRY(remote_process) next;
};

void remote_process_register(struct remote_process *p);

struct remote_process *get_remote_process_type(unsigned int type);
struct remote_process *get_remote_process_rid(unsigned int rid);

DeviceState *qdev_device_add(QemuOpts *opts, Error **errp);
void qdev_set_id(DeviceState *dev, const char *id);

#endif
