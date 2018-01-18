/*
 * Machine 'none' tests.
 *
 * Copyright (c) 2018 Red Hat Inc.
 *
 * Authors:
 *  Igor Mammedov <imammedo@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qemu/cutils.h"
#include "libqtest.h"
#include "qapi/qmp/types.h"

struct arch2cpu {
    const char *arch;
    const char *cpu_model;
};

static struct arch2cpu cpus_map[] = {
    /* tested targets list */
};

static const char *get_cpu_model_by_arch(const char *arch)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(cpus_map); i++) {
        if (!strcmp(arch, cpus_map[i].arch)) {
            return cpus_map[i].cpu_model;
        }
    }
    return NULL;
}

static void test_machine_cpu_cli(void)
{
    char *args;
    QDict *response;
    const char *arch = qtest_get_arch();
    const char *cpu_model = get_cpu_model_by_arch(arch);

    if (!cpu_model) {
        fprintf(stderr, "WARNING: cpu name for target '%s' isn't defined,"
                " add it to cpus_map\n", arch);
        return; /* TODO: die here to force all targets have a test */
    }
    args = g_strdup_printf("-machine none -cpu %s", cpu_model);
    qtest_start(args);

    response = qmp("{ 'execute': 'quit' }");
    g_assert(qdict_haskey(response, "return"));
    QDECREF(response);

    qtest_end();
    g_free(args);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("machine/none/cpu_option", test_machine_cpu_cli);

    return g_test_run();
}
