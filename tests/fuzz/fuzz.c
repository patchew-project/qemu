#include "qemu/osdep.h"

#include <stdio.h>
#include <stdlib.h>


#include "tests/libqtest.h"
#include "sysemu/qtest.h"
#include "fuzz.h"
#include "tests/libqos/qgraph.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"

typedef struct FuzzTargetState {
        FuzzTarget *target;
        QSLIST_ENTRY(FuzzTargetState) target_list;
} FuzzTargetState;

typedef QSLIST_HEAD(, FuzzTargetState) FuzzTargetList;

static const char *fuzz_arch = TARGET_NAME;

static FuzzTargetList *fuzz_target_list;
static FuzzTarget *fuzz_target;
static QTestState *fuzz_qts;
static bool trace;


void set_fuzz_target_args(int argc, char **argv)
{
    if (fuzz_target) {
        fuzz_target->main_argc = argc;
        fuzz_target->main_argv = argv;
    }
}

void reboot(QTestState *s)
{
    qemu_system_reset(SHUTDOWN_CAUSE_GUEST_RESET);
}

static QTestState *qtest_setup(void)
{
    qtest_server_set_tx_handler(&qtest_client_inproc_recv, NULL);
    return qtest_inproc_init(trace, fuzz_arch, &qtest_server_inproc_recv);
}

void fuzz_add_target(const char *name, const char *description,
        FuzzTarget *target)
{
    FuzzTargetState *tmp;
    FuzzTargetState *target_state;
    if (!fuzz_target_list) {
        fuzz_target_list = g_new0(FuzzTargetList, 1);
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (g_strcmp0(tmp->target->name->str, name) == 0) {
            fprintf(stderr, "Error: Fuzz target name %s already in use\n",
                    name);
            abort();
        }
    }
    target_state = g_new0(FuzzTargetState, 1);
    target_state->target = g_new0(FuzzTarget, 1);
    *(target_state->target) = *target;
    target_state->target->name = g_string_new(name);
    target_state->target->description = g_string_new(description);
    QSLIST_INSERT_HEAD(fuzz_target_list, target_state, target_list);
}


static FuzzTarget *fuzz_get_target(char* name)
{
    FuzzTargetState *tmp;
    if (!fuzz_target_list) {
        fprintf(stderr, "Fuzz target list not initialized\n");
        abort();
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (g_strcmp0(tmp->target->name->str, name) == 0) {
            break;
        }
    }
    return tmp->target;
}


static void usage(char *path)
{
    printf("Usage: %s --FUZZ_TARGET [LIBFUZZER ARGUMENTS]\n", path);
    printf("where --FUZZ_TARGET is one of:\n");
    FuzzTargetState *tmp;
    if (!fuzz_target_list) {
        fprintf(stderr, "Fuzz target list not initialized\n");
        abort();
    }
    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        printf(" --%s  : %s\n", tmp->target->name->str,
                tmp->target->description->str);
    }
    exit(0);
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

    char *target_name, *trace_qtest;

    /* --trace is useful for outputting a log of qtest commands that trigger
     * a crash. The log can can then be replayed with a simple qtest script. */
    if (*argc > 2) {
        trace_qtest = (*argv)[2];
        if (strcmp(trace_qtest, "--trace") == 0) {
            trace = true;
        }
    }

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
    target_name += 2;
    fuzz_target = fuzz_get_target(target_name);

    fuzz_qts = qtest_setup(void);

    if (!fuzz_target) {
        fprintf(stderr, "Error: Fuzz fuzz_target name %s not found\n",
                target_name);
        usage(**argv);
    }

    if (fuzz_target->pre_main) {
        fuzz_target->pre_main();
    }

    if (trace) {
        printf("### cmd_line: ");
        for (int i = 0; i < (fuzz_target->main_argc); i++) {
            printf("%s ", ((fuzz_target->main_argv))[i]);
        }
        printf("\n");
    }

    /* Run QEMU's softmmu main with the calculated arguments*/
    qemu_init(fuzz_target->main_argc, fuzz_target->main_argv, NULL);

    /* If configured, this is a good spot to set up snapshotting */
    if (fuzz_target->pre_fuzz) {
        fuzz_target->pre_fuzz(fuzz_qts);
    }

    if (trace) {
        printf("### END INITIALIZATION\n");
    }

    return 0;
}
