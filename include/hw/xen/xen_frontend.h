#ifndef QEMU_HW_XEN_FRONTEND_H
#define QEMU_HW_XEN_FRONTEND_H 1

#include "hw/xen/xen_pvdev.h"

char *xenstore_read_fe_str(struct XenDevice *xendev, const char *node);
int xenstore_read_fe_int(struct XenDevice *xendev, const char *node, int *ival);
int xenstore_read_fe_uint64(struct XenDevice *xendev, const char *node, uint64_t *uval);
void xenstore_update_fe(char *watch, struct XenDevice *xendev);

struct XenDevice *xen_fe_get_xendev(const char *type, int dom, int dev,
                                    char *backend, struct XenDevOps *ops);
void xen_fe_frontend_changed(struct XenDevice *xendev, const char *node);
void xen_fe_backend_changed(struct XenDevice *xendev, const char *node);
int xen_fe_register(const char *type, struct XenDevOps *ops);
int xen_fe_alloc_unbound(struct XenDevice *xendev, int dom, int remote_dom);
int xenbus_switch_state(struct XenDevice *xendev, enum xenbus_state xbus);


#endif /* QEMU_HW_XEN_FRONTEND_H */
