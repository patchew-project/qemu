/*
 * Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 */

#ifndef HW_XEN_BUS_HELPER_H
#define HW_XEN_BUS_HELPER_H

const char *xs_strstate(enum xenbus_state state);

void xs_node_create(struct xs_handle *xsh, const char *node,
                    struct xs_permissions perms[],
                    unsigned int nr_perms, Error **errp);
void xs_node_destroy(struct xs_handle *xsh, const char *node);

void xs_node_vprintf(struct xs_handle *xsh, const char *node,
                     const char *key, const char *fmt, va_list ap);
void xs_node_printf(struct xs_handle *xsh, const char *node, const char *key,
                    const char *fmt, ...);

int xs_node_vscanf(struct xs_handle *xsh, const char *node, const char *key,
                   const char *fmt, va_list ap);
int xs_node_scanf(struct xs_handle *xsh, const char *node, const char *key,
                  const char *fmt, ...);

void xs_node_watch(struct xs_handle *xsh, const char *node, const char *key,
                   char *token, Error **errp);
void xs_node_unwatch(struct xs_handle *xsh, const char *node, const char *key,
                     const char *token);

#endif /* HW_XEN_BUS_HELPER_H */
