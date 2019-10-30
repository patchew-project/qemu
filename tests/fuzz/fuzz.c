/*
 * fuzzing driver
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include <stdio.h>
#include <stdlib.h>
#include <wordexp.h>


#include "tests/libqtest.h"
#include "sysemu/qtest.h"
#include "fuzz.h"
#include "tests/libqos/qgraph.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"

typedef struct FuzzTargetState {
        FuzzTarget *target;
        QSLIST_ENTRY(FuzzTargetState) target_list;
} FuzzTargetState;

typedef QSLIST_HEAD(, FuzzTargetState) FuzzTargetList;

static const char *fuzz_arch = TARGET_NAME;

static FuzzTargetList *fuzz_target_list;
static FuzzTarget *fuzz_target;
static QTestState *fuzz_qts;



void flush_events(QTestState *s)
{
    int i = 10;
    while (g_main_context_pending(NULL) && i-- > 0) {
        main_loop_wait(false);
    }
}

static QTestState *qtest_setup(void)
{
    qtest_server_set_tx_handler(&qtest_client_inproc_recv, NULL);
    return qtest_inproc_init(false, fuzz_arch, &qtest_server_inproc_recv);
}

void fuzz_add_target(FuzzTarget *target)
{
    FuzzTargetState *tmp;
    FuzzTargetState *target_state;
    if (!fuzz_target_list) {
        fuzz_target_list = g_new0(FuzzTargetList, 1);
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (g_strcmp0(tmp->target->name, target->name) == 0) {
            fprintf(stderr, "Error: Fuzz target name %s already in use\n",
                    target->name);
            abort();
        }
    }
    target_state = g_new0(FuzzTargetState, 1);
    target_state->target = g_new0(FuzzTarget, 1);
    *(target_state->target) = *target;
    QSLIST_INSERT_HEAD(fuzz_target_list, target_state, target_list);
}



static void usage(char *path)
{
    printf("Usage: %s --fuzz-target=FUZZ_TARGET [LIBFUZZER ARGUMENTS]\n", path);
    printf("where FUZZ_TARGET is one of:\n");
    FuzzTargetState *tmp;
    if (!fuzz_target_list) {
        fprintf(stderr, "Fuzz target list not initialized\n");
        abort();
    }
    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        printf(" %s  : %s\n", tmp->target->name,
                tmp->target->description);
    }
    exit(0);
}

static FuzzTarget *fuzz_get_target(char* name)
{
    FuzzTargetState *tmp;
    if (!fuzz_target_list) {
        fprintf(stderr, "Fuzz target list not initialized\n");
        abort();
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (strcmp(tmp->target->name, name) == 0) {
            return tmp->target;
        }
    }
    return NULL;
}


/* Executed for each fuzzing-input */
int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size)
{
    if (fuzz_target->fuzz) {
        fuzz_target->fuzz(fuzz_qts, Data, Size);
    }
    return 0;
}

/* Executed once, prior to fuzzing */
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp)
{

    char *target_name;

    /* Initialize qgraph and modules */
    qos_graph_init();
    module_call_init(MODULE_INIT_FUZZ_TARGET);
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_LIBQOS);

    if (*argc <= 1) {
        usage(**argv);
    }

    /* Identify the fuzz target */
    target_name = (*argv)[1];
    if (!strstr(target_name, "--fuzz-target=")) {
        usage(**argv);
    }

    target_name += strlen("--fuzz-target=");

    fuzz_target = fuzz_get_target(target_name);
    if (!fuzz_target) {
        usage(**argv);
    }

    fuzz_qts = qtest_setup();

    if (!fuzz_target) {
        fprintf(stderr, "Error: Fuzz fuzz_target name %s not found\n",
                target_name);
        usage(**argv);
    }

    if (fuzz_target->pre_vm_init) {
        fuzz_target->pre_vm_init();
    }

    /* Run QEMU's softmmu main with the fuzz-target dependent arguments */
    char *init_cmdline = fuzz_target->get_init_cmdline(fuzz_target);

    wordexp_t result;
    wordexp(init_cmdline, &result, 0);

    qemu_init(result.we_wordc, result.we_wordv, NULL);

    if (fuzz_target->pre_fuzz) {
        fuzz_target->pre_fuzz(fuzz_qts);
    }

    return 0;
}
