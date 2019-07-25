#include "tests/fuzz/ramfile.h"
#include "migration/qemu-file.h"
#include "migration/global_state.h"
#include "migration/savevm.h"
#include "tests/libqtest.h"
#include "exec/memory.h"
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
typedef QSLIST_HEAD(, FuzzTarget) FuzzTargetList;

FuzzTargetList* fuzz_target_list;

uint64_t total_mr_size = 0;
uint64_t mr_index = 0;

const MemoryRegion* mrs[1000];


// Save just the VMStateDescriptors
void save_device_state(void)
{
    writefile = qemu_fopen_ram(&rd);
    global_state_store();
    qemu_save_device_state(writefile);
    qemu_fflush(writefile);
    ramfile = qemu_fopen_ro_ram(rd);
}

// Save the entire vm state including RAM
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
    if (ret < 0){
        printf("reset error\n");
        exit(-1);
    }
}

/* Restore full vm state */
void load_vm_state()
{
    qemu_freopen_ro_ram(ramfile);

    vm_stop(RUN_STATE_RESTORE_VM);
    /* qemu_system_reset(SHUTDOWN_CAUSE_NONE); */

    int ret = qemu_loadvm_state(ramfile);
    if (ret < 0){
        printf("reset error\n");
        exit(-1);
    }
    migration_incoming_state_destroy();
    vm_start();
}

void qtest_setup()
{
    s = qtest_init_fuzz(NULL, NULL);
    global_qtest = s;
}

void fuzz_add_target(const char* name,
        const char* description,
        void(*init_pre_main)(void),
        void(*init_pre_save)(void),
        void(*save_state)(void),
        void(*reset)(void),
        void(*pre_fuzz)(void),
        void(*fuzz)(const unsigned char*, size_t),
        void(*post_fuzz)(void),
        int* main_argc,
        char*** main_argv)
{

    FuzzTarget *target;
    FuzzTarget *tmp;
    if(!fuzz_target_list)
        fuzz_target_list = g_new0(FuzzTargetList, 1);

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (g_strcmp0(tmp->name->str, name) == 0) {
            fprintf(stderr, "Error: Fuzz target name %s already in use\n", name);
            abort();
        }
    }
    target = g_new0(FuzzTarget, 1);
    target->name = g_string_new(name);
    target->description = g_string_new(description);
    target->init_pre_main = init_pre_main;
    target->init_pre_save = init_pre_save;
    target->save_state = save_state;
    target->reset = reset;
    target->pre_fuzz = pre_fuzz;
    target->fuzz = fuzz;
    target->post_fuzz = post_fuzz;
    target->main_argc = main_argc;
    target->main_argv = main_argv;
    QSLIST_INSERT_HEAD(fuzz_target_list, target, target_list);
}


FuzzTarget* fuzz_get_target(char* name)
{
    FuzzTarget* tmp;
    if(!fuzz_target_list){
        fprintf(stderr, "Fuzz target list not initialized");
        abort();
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (g_strcmp0(tmp->name->str, name) == 0) {
            break;
        }
    }
    return tmp;
}

FuzzTarget* fuzz_target;



static void usage(void)
{
    printf("Usage: ./fuzz --FUZZ_TARGET [LIBFUZZER ARGUMENTS]\n");
    printf("where --FUZZ_TARGET is one of:\n");
    FuzzTarget* tmp;
    if(!fuzz_target_list){
        fprintf(stderr, "Fuzz target list not initialized");
        abort();
    }
    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
            printf(" --%s  : %s\n", tmp->name->str, tmp->description->str);
        }
        exit(0);
    }
}

// TODO: Replace this with QEMU's built-in linked list
static void enum_memory(void)
{
    mtree_info(true, true, true);
    fuzz_memory_region *fmr = g_new0(fuzz_memory_region, 1);

    fmr->io = false;
    fmr->start = 0x100000;
    fmr->length = 0x10000;
    fmr->next = fuzz_memory_region_head;
    fuzz_memory_region_tail->next = fmr;
    fuzz_memory_region_tail = fmr;
    fmr = fuzz_memory_region_head;

    while(true){
        fmr = fmr->next;
        if(fmr == fuzz_memory_region_head)
            break;
    }
}

/* Executed for each fuzzing-input */
int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size)
{
    /* e.g. Device bootstrapping */
    if(fuzz_target->pre_fuzz)
        fuzz_target->pre_fuzz();

    if(fuzz_target->fuzz)
        fuzz_target->fuzz(Data, Size);

    /* e.g. Copy counter bitmap to shm*/
    if(fuzz_target->post_fuzz)
        fuzz_target->post_fuzz();

    /* e.g. Reboot the machine or vmload */
    if(fuzz_target->reset)
        fuzz_target->reset();

    return 0;
}

/* Executed once, prior to fuzzing */
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp)
{

    char *target_name;

    // Initialize qgraph and modules
    qos_graph_init();
    module_call_init(MODULE_INIT_FUZZ_TARGET);
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_LIBQOS);

    if(*argc <= 1)
        usage();


    /* Identify the fuzz target */
    target_name = (*argv)[1];
    target_name+=2;
    fuzz_target = fuzz_get_target(target_name);

    if(!fuzz_target)
    {
        fprintf(stderr, "Error: Fuzz fuzz_target name %s not found\n", target_name);
        usage();
    }

    if(fuzz_target->init_pre_main)
        fuzz_target->init_pre_main();

    /* Run QEMU's regular vl.c:main */
    real_main(*(fuzz_target->main_argc), *(fuzz_target->main_argv), NULL);


    /* Enumerate memory to identify mapped MMIO and I/O regions */
    enum_memory();

    /* Good place to do any one-time device initialization (such as QOS init) */
    if(fuzz_target->init_pre_save)
        fuzz_target->init_pre_save();

    /* If configured, this is where we save vm or device state to ramdisk */
    if(fuzz_target->save_state)
        fuzz_target->save_state();

    return 0;
}
