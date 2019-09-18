#ifndef FUZZER_H_
#define FUZZER_H_

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "tests/libqtest.h"


typedef struct FuzzTarget {
    GString *name;
    GString *description;
    void(*pre_main)(void);
    void(*pre_fuzz)(QTestState *);
    void(*fuzz)(QTestState *, const unsigned char *, size_t);
    int main_argc;
    char **main_argv;
} FuzzTarget;

void set_fuzz_target_args(int argc, char **argv);
void reboot(QTestState *);
void fuzz_add_target(const char *name, const char *description, FuzzTarget
        *target);

int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size);
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp);

#endif

