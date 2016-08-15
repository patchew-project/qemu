/*
 * QEMU simple authorization object
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
#include <glib.h>

#include "qemu/authz-simple.h"

static void test_authz_default_deny(void)
{
    QAuthZSimple *auth = qauthz_simple_new("auth0",
                                           QAUTHZ_SIMPLE_POLICY_DENY,
                                           &error_abort);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_default_allow(void)
{
    QAuthZSimple *auth = qauthz_simple_new("auth0",
                                           QAUTHZ_SIMPLE_POLICY_ALLOW,
                                           &error_abort);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_explicit_deny(void)
{
    QAuthZSimple *auth = qauthz_simple_new("auth0",
                                           QAUTHZ_SIMPLE_POLICY_ALLOW,
                                           &error_abort);

    qauthz_simple_append_rule(auth, "fred", QAUTHZ_SIMPLE_POLICY_DENY,
                              QAUTHZ_SIMPLE_FORMAT_EXACT, &error_abort);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_explicit_allow(void)
{
    QAuthZSimple *auth = qauthz_simple_new("auth0",
                                           QAUTHZ_SIMPLE_POLICY_DENY,
                                           &error_abort);

    qauthz_simple_append_rule(auth, "fred", QAUTHZ_SIMPLE_POLICY_ALLOW,
                              QAUTHZ_SIMPLE_FORMAT_EXACT, &error_abort);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}


static void test_authz_complex(void)
{
#ifndef CONFIG_FNMATCH
    Error *local_err = NULL;
#endif
    QAuthZSimple *auth = qauthz_simple_new("auth0",
                                           QAUTHZ_SIMPLE_POLICY_DENY,
                                           &error_abort);

    qauthz_simple_append_rule(auth, "fred", QAUTHZ_SIMPLE_POLICY_ALLOW,
                              QAUTHZ_SIMPLE_FORMAT_EXACT, &error_abort);
    qauthz_simple_append_rule(auth, "bob", QAUTHZ_SIMPLE_POLICY_ALLOW,
                              QAUTHZ_SIMPLE_FORMAT_EXACT, &error_abort);
    qauthz_simple_append_rule(auth, "dan", QAUTHZ_SIMPLE_POLICY_DENY,
                              QAUTHZ_SIMPLE_FORMAT_EXACT, &error_abort);
#ifdef CONFIG_FNMATCH
    qauthz_simple_append_rule(auth, "dan*", QAUTHZ_SIMPLE_POLICY_ALLOW,
                              QAUTHZ_SIMPLE_FORMAT_GLOB, &error_abort);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));
    g_assert(qauthz_is_allowed(QAUTHZ(auth), "bob", &error_abort));
    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));
    g_assert(qauthz_is_allowed(QAUTHZ(auth), "danb", &error_abort));
#else
    g_assert(qauthz_simple_append_rule(auth, "dan*",
                                       QAUTHZ_SIMPLE_POLICY_ALLOW,
                                       QAUTHZ_SIMPLE_FORMAT_GLOB,
                                       &local_err) < 0);
    g_assert(local_err != NULL);
    error_free(local_err);
#endif

    object_unparent(OBJECT(auth));
}

static void test_authz_add_remove(void)
{
    QAuthZSimple *auth = qauthz_simple_new("auth0",
                                           QAUTHZ_SIMPLE_POLICY_ALLOW,
                                           &error_abort);

    g_assert_cmpint(qauthz_simple_append_rule(auth, "fred",
                                              QAUTHZ_SIMPLE_POLICY_ALLOW,
                                              QAUTHZ_SIMPLE_FORMAT_EXACT,
                                              &error_abort),
                    ==, 0);
    g_assert_cmpint(qauthz_simple_append_rule(auth, "bob",
                                              QAUTHZ_SIMPLE_POLICY_ALLOW,
                                              QAUTHZ_SIMPLE_FORMAT_EXACT,
                                              &error_abort),
                    ==, 1);
    g_assert_cmpint(qauthz_simple_append_rule(auth, "dan",
                                              QAUTHZ_SIMPLE_POLICY_DENY,
                                              QAUTHZ_SIMPLE_FORMAT_EXACT,
                                              &error_abort),
                    ==, 2);
    g_assert_cmpint(qauthz_simple_append_rule(auth, "frank",
                                              QAUTHZ_SIMPLE_POLICY_DENY,
                                              QAUTHZ_SIMPLE_FORMAT_EXACT,
                                              &error_abort),
                    ==, 3);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));

    g_assert_cmpint(qauthz_simple_delete_rule(auth, "dan"),
                    ==, 2);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));

    g_assert_cmpint(qauthz_simple_insert_rule(auth, "dan",
                                              QAUTHZ_SIMPLE_POLICY_DENY,
                                              QAUTHZ_SIMPLE_FORMAT_EXACT,
                                              2,
                                              &error_abort),
                    ==, 2);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));

    object_unparent(OBJECT(auth));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);

    g_test_add_func("/auth/simple/default/deny", test_authz_default_deny);
    g_test_add_func("/auth/simple/default/allow", test_authz_default_allow);
    g_test_add_func("/auth/simple/explicit/deny", test_authz_explicit_deny);
    g_test_add_func("/auth/simple/explicit/allow", test_authz_explicit_allow);
    g_test_add_func("/auth/simple/complex", test_authz_complex);
    g_test_add_func("/auth/simple/add-remove", test_authz_add_remove);

    return g_test_run();
}
