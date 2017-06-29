/*
 * QOM link property
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QOM_LINK_PROPERTY_H
#define QOM_LINK_PROPERTY_H

#include "qom/object.h"

typedef struct {
    Object **child;
    void (*check)(Object *, const char *, Object *, Error **);
    ObjectPropertyLinkFlags flags;
} LinkProperty;

void object_get_link_property(Object *obj, Visitor *v,
                              const char *name, void *opaque,
                              Error **errp);
void object_set_link_property(Object *obj, Visitor *v,
                              const char *name, void *opaque,
                              Error **errp);
#endif
