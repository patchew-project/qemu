#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "migration/qemu-file.h"

#include "migration/qemu-file.h"
#include "migration/global_state.h"
#include "migration/savevm.h"
#include "tests/libqtest.h"
#include "migration/migration.h"
#include "fuzz.h"
#include "tests/libqos/qgraph.h"

#include <stdio.h>
#include <stdlib.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/ioctl.h>

QTestState *s;

QEMUFile *ramfile;
QEMUFile *writefile;
ram_disk *rd;


typedef struct FuzzTargetState {
        FuzzTarget *target;
        QSLIST_ENTRY(FuzzTargetState) target_list;
} FuzzTargetState;

typedef QSLIST_HEAD(, FuzzTargetState) FuzzTargetList;

FuzzTargetList *fuzz_target_list;

/* Save just the VMStateDescriptors */
void save_device_state(void)
{
    writefile = qemu_fopen_ram(&rd);
    global_state_store();
    qemu_save_device_state(writefile);
    qemu_fflush(writefile);
    ramfile = qemu_fopen_ro_ram(rd);
}

/* Save the entire vm state including RAM */
void save_vm_state(void)
{
    writefile = qemu_fopen_ram(&rd);
    vm_stop(RUN_STATE_SAVE_VM);
    global_state_store();
    qemu_savevm_state(writefile, NULL);
    qemu_fflush(writefile);
    ramfile = qemu_fopen_ro_ram(rd);
}

/* Reset state by rebooting */
void reboot()
{
    qemu_system_reset(SHUTDOWN_CAUSE_NONE);
}

/* Restore device state */
void load_device_state()
{
    qemu_freopen_ro_ram(ramfile);

    int ret = qemu_load_device_state(ramfile);
    if (ret < 0) {
        printf("reset error\n");
        exit(-1);
    }
}

/* Restore full vm state */
void load_vm_state()
{
    qemu_freopen_ro_ram(ramfile);

    vm_stop(RUN_STATE_RESTORE_VM);

    int ret = qemu_loadvm_state(ramfile);
    if (ret < 0) {
        printf("reset error\n");
        exit(-1);
    }
    migration_incoming_state_destroy();
    vm_start();
}

void qtest_setup()
{
    s = qtest_fuzz_init(NULL, NULL);
    global_qtest = s;
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
        fprintf(stderr, "Fuzz target list not initialized");
        abort();
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (g_strcmp0(tmp->target->name->str, name) == 0) {
            break;
        }
    }
    return tmp->target;
}

FuzzTarget *fuzz_target;



static void usage(void)
{
    printf("Usage: ./fuzz --FUZZ_TARGET [LIBFUZZER ARGUMENTS]\n");
    printf("where --FUZZ_TARGET is one of:\n");
    FuzzTargetState *tmp;
    if (!fuzz_target_list) {
        fprintf(stderr, "Fuzz target list not initialized");
        abort();
    }
    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        printf(" --%s  : %s\n", tmp->target->name->str,
                tmp->target->description->str);
    }
    exit(0);
}

static void enum_memory(void)
{
    /* TODO: Enumerate interesting memory using memory_region_is_ram */
    return;
}

/* Executed for each fuzzing-input */
int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size)
{
    /* e.g. Device bootstrapping */
    if (fuzz_target->pre_fuzz) {
        fuzz_target->pre_fuzz();
    }

    if (fuzz_target->fuzz) {
        fuzz_target->fuzz(Data, Size);
    }

    /* e.g. Copy counter bitmap to shm*/
    if (fuzz_target->post_fuzz) {
        fuzz_target->post_fuzz();
    }

    /* e.g. Reboot the machine or vmload */
    if (fuzz_target->reset) {
        fuzz_target->reset();
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
        usage();
    }


    /* Identify the fuzz target */
    target_name = (*argv)[1];
    target_name += 2;
    fuzz_target = fuzz_get_target(target_name);

    if (!fuzz_target) {
        fprintf(stderr, "Error: Fuzz fuzz_target name %s not found\n",
                target_name);
        usage();
    }

    if (fuzz_target->pre_main) {
        fuzz_target->pre_main();
    }

    /* Run QEMU's regular vl.c:main */
    qemu_init(*(fuzz_target->main_argc), *(fuzz_target->main_argv), NULL);


    /* Enumerate memory to identify mapped MMIO and I/O regions */
    enum_memory();

    /* Good place to do any one-time device initialization (such as QOS init) */
    if (fuzz_target->pre_save_state) {
        fuzz_target->pre_save_state();
    }

    /* If configured, this is where we save vm or device state to ramdisk */
    if (fuzz_target->save_state) {
        fuzz_target->save_state();
    }

    return 0;
}
