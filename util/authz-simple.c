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

#include "qemu/osdep.h"
#include "qemu/authz-simple.h"
#include "qom/object_interfaces.h"
#include "qapi-visit.h"

#ifdef CONFIG_FNMATCH
#include <fnmatch.h>
#endif

static bool qauthz_simple_is_allowed(QAuthZ *authz,
                                     const char *identity,
                                     Error **errp)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(authz);
    QAuthZSimpleRuleList *rules = sauthz->rules;

    while (rules) {
        QAuthZSimpleRule *rule = rules->value;
        QAuthZSimpleFormat format = rule->has_format ? rule->format :
            QAUTHZ_SIMPLE_FORMAT_EXACT;

        switch (format) {
        case QAUTHZ_SIMPLE_FORMAT_EXACT:
            if (strcmp(rule->match, identity) == 0) {
                return rule->policy == QAUTHZ_SIMPLE_POLICY_ALLOW;
            }
            break;
#ifdef CONFIG_FNMATCH
        case QAUTHZ_SIMPLE_FORMAT_GLOB:
            if (fnmatch(rule->match, identity, 0) == 0) {
                return rule->policy == QAUTHZ_SIMPLE_POLICY_ALLOW;
            }
            break;
#else
            return false;
#endif
        default:
            return false;
        }
        rules = rules->next;
    }

    return sauthz->policy == QAUTHZ_SIMPLE_POLICY_ALLOW;
}


static void
qauthz_simple_prop_set_policy(Object *obj,
                              int value,
                              Error **errp G_GNUC_UNUSED)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);

    sauthz->policy = value;
}


static int
qauthz_simple_prop_get_policy(Object *obj,
                              Error **errp G_GNUC_UNUSED)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);

    return sauthz->policy;
}


static void
qauthz_simple_prop_get_rules(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);

    visit_type_QAuthZSimpleRuleList(v, name, &sauthz->rules, errp);
}

static void
qauthz_simple_prop_set_rules(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);
    QAuthZSimpleRuleList *oldrules;
#ifndef CONFIG_FNMATCH
    QAuthZSimpleRuleList *rules;
#endif

    oldrules = sauthz->rules;
    visit_type_QAuthZSimpleRuleList(v, name, &sauthz->rules, errp);

#ifndef CONFIG_FNMATCH
    rules = sauthz->rules;
    while (rules) {
        QAuthZSimpleRule *rule = rules->value;
        if (rule->has_format &&
            rule->format == QAUTHZ_SIMPLE_FORMAT_GLOB) {
            error_setg(errp, "Glob format not supported on this platform");
            qapi_free_QAuthZSimpleRuleList(sauthz->rules);
            sauthz->rules = oldrules;
            return;
        }
    }
#endif

    qapi_free_QAuthZSimpleRuleList(oldrules);
}


static void
qauthz_simple_complete(UserCreatable *uc, Error **errp)
{
}


static void
qauthz_simple_finalize(Object *obj)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);

    qapi_free_QAuthZSimpleRuleList(sauthz->rules);
}


static void
qauthz_simple_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    QAuthZClass *authz = QAUTHZ_CLASS(oc);

    ucc->complete = qauthz_simple_complete;
    authz->is_allowed = qauthz_simple_is_allowed;

    object_class_property_add_enum(oc, "policy",
                                   "QAuthZSimplePolicy",
                                   QAuthZSimplePolicy_lookup,
                                   qauthz_simple_prop_get_policy,
                                   qauthz_simple_prop_set_policy,
                                   NULL);

    object_class_property_add(oc, "rules", "QAuthZSimpleRule",
                              qauthz_simple_prop_get_rules,
                              qauthz_simple_prop_set_rules,
                              NULL, NULL, NULL);
}


QAuthZSimple *qauthz_simple_new(const char *id,
                                QAuthZSimplePolicy policy,
                                Error **errp)
{
    return QAUTHZ_SIMPLE(
        object_new_with_props(TYPE_QAUTHZ_SIMPLE,
                              object_get_objects_root(),
                              id, errp,
                              "policy", QAuthZSimplePolicy_lookup[policy],
                              NULL));
}


ssize_t qauthz_simple_append_rule(QAuthZSimple *auth,
                                  const char *match,
                                  QAuthZSimplePolicy policy,
                                  QAuthZSimpleFormat format,
                                  Error **errp)
{
    QAuthZSimpleRule *rule;
    QAuthZSimpleRuleList *rules, *tmp;
    size_t i = 0;

#ifndef CONFIG_FNMATCH
    if (format == QAUTHZ_SIMPLE_FORMAT_GLOB) {
        error_setg(errp, "Glob format not supported on this platform");
        return -1;
    }
#endif

    rule = g_new0(QAuthZSimpleRule, 1);
    rule->policy = policy;
    rule->match = g_strdup(match);
    rule->format = format;
    rule->has_format = true;

    tmp = g_new0(QAuthZSimpleRuleList, 1);
    tmp->value = rule;

    rules = auth->rules;
    if (rules) {
        while (rules->next) {
            i++;
            rules = rules->next;
        }
        rules->next = tmp;
        return i + 1;
    } else {
        auth->rules = tmp;
        return 0;
    }
}


ssize_t qauthz_simple_insert_rule(QAuthZSimple *auth,
                                  const char *match,
                                  QAuthZSimplePolicy policy,
                                  QAuthZSimpleFormat format,
                                  size_t index,
                                  Error **errp)
{
    QAuthZSimpleRule *rule;
    QAuthZSimpleRuleList *rules, *tmp;
    size_t i = 0;

#ifndef CONFIG_FNMATCH
    if (format == QAUTHZ_SIMPLE_FORMAT_GLOB) {
        error_setg(errp, "Glob format not supported on this platform");
        return -1;
    }
#endif

    rule = g_new0(QAuthZSimpleRule, 1);
    rule->policy = policy;
    rule->match = g_strdup(match);
    rule->format = format;
    rule->has_format = true;

    tmp = g_new0(QAuthZSimpleRuleList, 1);
    tmp->value = rule;

    rules = auth->rules;
    if (rules && index > 0) {
        while (rules->next && i < (index - 1)) {
            i++;
            rules = rules->next;
        }
        tmp->next = rules->next;
        rules->next = tmp;
        return i + 1;
    } else {
        tmp->next = auth->rules;
        auth->rules = tmp;
        return 0;
    }
}


ssize_t qauthz_simple_delete_rule(QAuthZSimple *auth, const char *match)
{
    QAuthZSimpleRule *rule;
    QAuthZSimpleRuleList *rules, *prev;
    size_t i = 0;

    prev = NULL;
    rules = auth->rules;
    while (rules) {
        rule = rules->value;
        if (g_str_equal(rule->match, match)) {
            if (prev) {
                prev->next = rules->next;
            } else {
                auth->rules = rules->next;
            }
            rules->next = NULL;
            qapi_free_QAuthZSimpleRuleList(rules);
            return i;
        }
        prev = rules;
        rules = rules->next;
        i++;
    }

    return -1;
}


static const TypeInfo qauthz_simple_info = {
    .parent = TYPE_QAUTHZ,
    .name = TYPE_QAUTHZ_SIMPLE,
    .instance_size = sizeof(QAuthZSimple),
    .instance_finalize = qauthz_simple_finalize,
    .class_size = sizeof(QAuthZSimpleClass),
    .class_init = qauthz_simple_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qauthz_simple_register_types(void)
{
    type_register_static(&qauthz_simple_info);
}


type_init(qauthz_simple_register_types);
