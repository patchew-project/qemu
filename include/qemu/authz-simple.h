/*
 * QEMU simple authorization driver
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QAUTHZ_SIMPLE_H__
#define QAUTHZ_SIMPLE_H__

#include "qemu/authz.h"


#define TYPE_QAUTHZ_SIMPLE "authz-simple"

#define QAUTHZ_SIMPLE_CLASS(klass) \
     OBJECT_CLASS_CHECK(QAuthZSimpleClass, (klass), \
                        TYPE_QAUTHZ_SIMPLE)
#define QAUTHZ_SIMPLE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(QAuthZSimpleClass, (obj), \
                      TYPE_QAUTHZ_SIMPLE)
#define QAUTHZ_SIMPLE(obj) \
     INTERFACE_CHECK(QAuthZSimple, (obj), \
                     TYPE_QAUTHZ_SIMPLE)

typedef struct QAuthZSimple QAuthZSimple;
typedef struct QAuthZSimpleClass QAuthZSimpleClass;


/**
 * QAuthZSimple:
 *
 * This authorization driver provides a simple mechanism
 * for granting access by matching user names against a
 * list of globs. Each match rule has an associated policy
 * and a catch all policy applies if no rule matches
 *
 * To create an instance of this class via QMP:
 *
 *  {
 *    "execute": "object-add",
 *    "arguments": {
 *      "qom-type": "authz-simple",
 *      "id": "auth0",
 *      "parameters": {
 *        "rules": [
 *           { "match": "fred", "policy": "allow", "format": "exact" },
 *           { "match": "bob", "policy": "allow", "format": "exact" },
 *           { "match": "danb", "policy": "deny", "format": "exact" },
 *           { "match": "dan*", "policy": "allow", "format": "glob" }
 *        ],
 *        "policy": "deny"
 *      }
 *    }
 *  }
 *
 * Or via the CLI:
 *
 *   $QEMU                                                         \
 *    -object authz-simple,id=acl0,policy=deny,                    \
 *            match.0.name=fred,match.0.policy=allow,format=exact, \
 *            match.1.name=bob,match.1.policy=allow,format=exact,  \
 *            match.2.name=danb,match.2.policy=deny,format=exact,  \
 *            match.3.name=dan\*,match.3.policy=allow,format=exact
 *
 */
struct QAuthZSimple {
    QAuthZ parent_obj;

    QAuthZSimplePolicy policy;
    QAuthZSimpleRuleList *rules;
};


struct QAuthZSimpleClass {
    QAuthZClass parent_class;
};


QAuthZSimple *qauthz_simple_new(const char *id,
                                QAuthZSimplePolicy policy,
                                Error **errp);

ssize_t qauthz_simple_append_rule(QAuthZSimple *auth,
                                  const char *match,
                                  QAuthZSimplePolicy policy,
                                  QAuthZSimpleFormat format,
                                  Error **errp);

ssize_t qauthz_simple_insert_rule(QAuthZSimple *auth,
                                  const char *match,
                                  QAuthZSimplePolicy policy,
                                  QAuthZSimpleFormat format,
                                  size_t index,
                                  Error **errp);

ssize_t qauthz_simple_delete_rule(QAuthZSimple *auth,
                                  const char *match);


#endif /* QAUTHZ_SIMPLE_H__ */

