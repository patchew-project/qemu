/*
 * Per-target QAPI introspection test cases
 *
 * Copyright (c) 2016 Red Hat Inc.
 *
 * Authors:
 *  Marc-André Lureau <marcandre.lureau@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp-input-visitor.h"
#include "qapi/error.h"
#include "qapi-visit.h"
#include "libqtest.h"

const char common_args[] = "-nodefaults -machine none";

static void test_qmp_introspect_validate(void)
{
    SchemaInfoList *schema;
    QDict *resp;
    Visitor *v;

    qtest_start(common_args);

    resp = qmp("{'execute': 'query-qmp-schema'}");
    v = qmp_input_visitor_new(qdict_get(resp, "return"), true);
    visit_type_SchemaInfoList(v, NULL, &schema, &error_abort);
    g_assert(schema);

    qapi_free_SchemaInfoList(schema);
    visit_free(v);
    QDECREF(resp);

    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("qmp-introspect/validate", test_qmp_introspect_validate);

    return g_test_run();
}
